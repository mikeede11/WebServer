#include <stdio.h>
//int xStatReqCompleteCount//do we count a disregarded/rejected request as one that has arrived? assume not. README
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/sysinfo.h> // for default num of threads to use
#include <sys/time.h>
#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404

#define FIFO_MODE 0
#define HPIC_MODE 1
#define HPHC_MODE 2

#define HTML_FILE 0
#define IMAGE_FILE 1
#define OTHER_FILE 2

struct
{
	char *ext;
	char *filetype;
} extensions[] = {
	{"gif", "image/gif"},
	{"jpg", "image/jpg"},
	{"jpeg", "image/jpeg"},
	{"png", "image/png"},
	{"ico", "image/ico"},
	{"zip", "image/zip"},
	{"gz", "image/gz"},
	{"tar", "image/tar"},
	{"htm", "text/html"},
	{"html", "text/html"},
	{0, 0}};

typedef struct RequestData
{
	int clientSocket;
	int xStatReqArrivalCount;
	suseconds_t xStatReqArrivalTime;
	int xStatReqDispatchCount;
	suseconds_t xStatReqDispatchTime;
	int xStatReqAge;
	char buffer[BUFSIZE + 1]; // buffer string
	int fd;					  // file descriptor
	int fileType;			  // e.g. HTML_FILE
	long ret;				  // no idea

} RequestData;

pthread_mutex_t mutexQueue;
pthread_cond_t cond_varQueue;
RequestData *requests;
int currentReqCount;
int totalRequestsArrived;
int requestsDispatched;
int xStatReqCompleteCount;
int hit;
struct timeval serverStartStamp;
int schedalg;

void web(RequestData req, int hitArg);

void safeWrite(int fd, void *buf, size_t cnt, char *msg)
{
	if (write(fd, buf, cnt) < 0)
	{
		printf("%s: %s\n", msg, strerror(errno));
		exit(1);
	}
}

void logger(int type, char *s1, char *s2, int socket_fd)
{
	int fd;
	char logbuffer[BUFSIZE * 2];

	switch (type)
	{
	case ERROR:
		(void)sprintf(logbuffer, "ERROR: %s:%s Errno=%d exiting pid=%d", s1, s2, errno, getpid());
		break;
	case FORBIDDEN:
		safeWrite(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n", 271, "Write Error: ");
		(void)sprintf(logbuffer, "FORBIDDEN: %s:%s", s1, s2);
		break;
	case NOTFOUND:
		safeWrite(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n", 224, "Write Error: ");
		(void)sprintf(logbuffer, "NOT FOUND: %s:%s", s1, s2);
		break;
	case LOG:
		(void)sprintf(logbuffer, " INFO: %s:%s:%d", s1, s2, socket_fd);
		break;
	}
	/* No checks here, nothing can be done with a failure anyway */
	if ((fd = open("nweb.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0)
	{
		safeWrite(fd, logbuffer, strlen(logbuffer), "Write Error: ");
		safeWrite(fd, "\n", 1, "Write Error: ");
		(void)close(fd);
	}

	if (type == ERROR || type == NOTFOUND || type == FORBIDDEN)
		exit(3);
}

int chooseByFile(RequestData *request, int fileType)
{
	int i;
	for (i = 0; i < currentReqCount; i++)
	{
		if (request[i].fileType == fileType)
		{
			return i;
		}
	}
	return 0; // returns 0 if there is no HTML or Image in the request queue
}

void *threadReqProc(void *args)
{
	while (1)
	{
		RequestData requestInfo;
		struct timeval dispatchEnd;
		int hitNum;

		pthread_mutex_lock(&mutexQueue);
		while (currentReqCount == 0)
		{
			pthread_cond_wait(&cond_varQueue, &mutexQueue);
		}
		int index = 0; // if ANY or FIFO
		if (schedalg == HPIC_MODE)
		{
			index = chooseByFile(requests, IMAGE_FILE);
		}
		else if (schedalg == HPHC_MODE)
		{
			index = chooseByFile(requests, HTML_FILE);
		}

		requestInfo = requests[index];

		gettimeofday(&dispatchEnd, NULL);
		requestInfo.xStatReqDispatchTime = ((dispatchEnd.tv_sec * 1000) + (dispatchEnd.tv_usec / 1000)) - ((serverStartStamp.tv_sec * 1000) + (serverStartStamp.tv_usec / 1000));
		requestInfo.xStatReqDispatchCount = requestsDispatched++;

		int i;
		for (i = index; i < currentReqCount - 1; i++)
		{ /* adjust queue */
			requests[i] = requests[i + 1];
		}
		int j;
		for (j = 0; j < index; j++)
			requests[j].xStatReqAge += 1; //increments the Age Stat by one if the index that was taken off the queue was higher than it
		currentReqCount--;
		hitNum = hit;
		pthread_mutex_unlock(&mutexQueue);
		web(requestInfo, hitNum);
	}
}
/* this is a child web server process, so we can exit on errors
static buffer and extension struct */
void web(RequestData req, int hitArg)
{
	struct timeval requestCompletion;
	int fd = req.clientSocket;
	int j, file_fd, buflen;
	long i, ret, len;
	char *fstr;
	char *buffer = req.buffer;
	ret = req.ret;
	if (ret > 0 && ret < BUFSIZE)
	{					 /* return code is valid chars */
		buffer[ret] = 0; /* terminate the buffer */
	}
	else
	{
		buffer[0] = 0;
	}

	for (i = 0; i < ret; i++) /* remove CF and LF characters */
		if (buffer[i] == '\r' || buffer[i] == '\n')
			buffer[i] = '*';
	logger(LOG, "request", buffer, hitArg);

	if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4))
	{ //is request valid format?
		logger(FORBIDDEN, "Only simple GET operation supported", buffer, fd);
	}
	for (i = 4; i < BUFSIZE; i++)
	{ /* null terminate after the second space to ignore extra stuff */
		if (buffer[i] == ' ')
		{ /* string is "GET URL " +lots of other stuff */
			buffer[i] = 0;
			break;
		}
	}
	for (j = 0; j < i - 1; j++) /* check for illegal parent directory use .. */
		if (buffer[j] == '.' && buffer[j + 1] == '.')
		{
			logger(FORBIDDEN, "Parent directory (..) path names not supported", buffer, fd);
		}
	if (!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "get /\0", 6)) /* convert no filename to index file */
		(void)strcpy(buffer, "GET /index.html");

	/* work out the file type and check we support it */
	buflen = strlen(buffer);
	fstr = (char *)0;
	for (i = 0; extensions[i].ext != 0; i++)
	{
		len = strlen(extensions[i].ext);
		if (!strncmp(&buffer[buflen - len], extensions[i].ext, len))
		{
			fstr = extensions[i].filetype;
			break;
		}
	}
	if (fstr == 0)
		logger(FORBIDDEN, "file extension type not supported", buffer, fd);

	if ((file_fd = open(&buffer[5], O_RDONLY)) == -1)
	{ /* open the file for reading */
		logger(NOTFOUND, "failed to open file", &buffer[5], fd);
	}
	logger(LOG, "SEND", &buffer[5], hitArg);
	gettimeofday(&requestCompletion, NULL);
	len = (long)lseek(file_fd, (off_t)0, SEEK_END);																																																																																																																																																														   /* lseek to the file end to find the length */
	(void)lseek(file_fd, (off_t)0, SEEK_SET);																																																																																																																																																															   /* lseek back to the file start ready for reading */
	(void)sprintf(buffer, "HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\nX-stat-req-arrival-count: %d\nX-stat-req-arrival-time: %ld milliseconds\nX-stat-req-dispatch-count: %d\nX-stat-req-dispatch-time: %ld milliseconds\nX-stat-req-complete-count: %d\nX-stat-req-complete-time: %ld milliseconds\nX-stat-req-age: %d\n\n", VERSION, len, fstr, req.xStatReqArrivalCount, req.xStatReqArrivalTime, req.xStatReqDispatchCount, req.xStatReqDispatchTime, xStatReqCompleteCount++, ((requestCompletion.tv_sec * 1000) + (requestCompletion.tv_usec / 1000)) - ((serverStartStamp.tv_sec * 1000) + (serverStartStamp.tv_usec / 1000)), req.xStatReqAge); /* Header + a blank line */
	logger(LOG, "Header", buffer, hitArg);
	safeWrite(fd, buffer, strlen(buffer), "Write Error: "); //puts the contents of index.html header/request

	// detach threads?
	//TODO:  readme, courier fixed width font, error check return #, block instead of conditional buffer
	/* send file in 8KB block - last block may be smaller */
	while ((ret = read(file_fd, buffer, BUFSIZE)) > 0)
	{ //puts the contents of index.html into buffer
		safeWrite(fd, buffer, ret, "Write Error: ");
	}
	sleep(1); /* allow socket to drain before signalling the socket is closed */
	close(fd);
	//pthread_exit(NULL) ?
}

void threadSystemSetUp(int threads)
{
	if (pthread_mutex_init(&mutexQueue, NULL) != 0)
	{
		perror("pthread_mutex_init() error");
		exit(1);
	}
	if (pthread_cond_init(&cond_varQueue, NULL) != 0)
	{
		perror("pthread_cond_init() error");
		exit(2);
	}
	pthread_t *threadsArr = (pthread_t *)malloc(sizeof(pthread_t) * threads);
	for (int i = 0; i < threads; i++)
	{
		if (pthread_create(&threadsArr[i], NULL, &threadReqProc, NULL) != 0)
		{
			printf("Failed to create threads\n");
		}
		else
		{
			fprintf(stdout, "Created Thread!\n");
		}
	}
}
int main(int argc, char **argv)
{
	int i, port, listenfd, socketfd, thread_pool_size, bufferSize, file_type; //listenfd is file descriptor to listening socket. socketfd is the socket of the client (where request is coming from/or the return addr)
	struct timeval endStamp;
	gettimeofday(&serverStartStamp, NULL); //note the time the server started for later stats
	socklen_t length;
	static struct sockaddr_in cli_addr;	 /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */

	/*PART 1 OF MAIN - Ensure server is executed with a portnumber
	specified and a dir(Top dir) where server can fetch files from (./)*/
	if (argc < 6 || argc > 6 || !strcmp(argv[1], "-?"))
	{
		(void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
					 "\tnweb is a small and very safe mini web server\n"
					 "\tnweb only servers out file/web pages with extensions named below\n"
					 "\t and only from the named directory or its sub-directories.\n"
					 "\tThere is no fancy features = safe and secure.\n\n"
					 "\tExample: nweb 8181 /home/nwebdir &\n\n"
					 "\tOnly Supports:",
					 VERSION);
		for (i = 0; extensions[i].ext != 0; i++)
			(void)printf(" %s", extensions[i].ext);

		(void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
					 "\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
					 "\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n");
		exit(0);
	}
	if (!strncmp(argv[2], "/", 2) || !strncmp(argv[2], "/etc", 5) ||
		!strncmp(argv[2], "/bin", 5) || !strncmp(argv[2], "/lib", 5) ||
		!strncmp(argv[2], "/tmp", 5) || !strncmp(argv[2], "/usr", 5) ||
		!strncmp(argv[2], "/dev", 5) || !strncmp(argv[2], "/sbin", 6))
	{
		(void)printf("ERROR: Bad top directory %s, see nweb -?\n", argv[2]);
		exit(3);
	}
	if (chdir(argv[2]) == -1)
	{
		(void)printf("ERROR: Can't Change to directory %s\n", argv[2]);
		exit(4);
	}

	if ((thread_pool_size = atoi(argv[3]))  <= 0)
	{						 
		(void)printf("ERROR: '%s' is not a valid number of threads. Only integers > 0.\n", argv[3]);
		exit(5);
	}

	if (!strncmp(argv[5], "FIFO", 5) || !strncmp(argv[5], "ANY", 4))
	{
		schedalg = FIFO_MODE; // FIFO / ANY
	}
	else if (!strncmp(argv[5], "HPIC", 5))
	{
		schedalg = HPIC_MODE; // HPIC
	}
	else if (!strncmp(argv[5], "HPHC", 5))
	{
		schedalg = HPHC_MODE; // HPHC
	}
	else
	{
		(void)printf("ERROR: '%s' is not a valid scheduling policy\n", argv[5]);
		exit(6);
	}

	if ((bufferSize = atoi(argv[4])) <= 0)
	{
		(void)printf("ERROR: %s is not a valid buffer number. Must be an Integer > 0.\n", argv[4]);
		exit(7);
	}

	requests = (RequestData *)malloc(sizeof(requests) * bufferSize);

	/*Create Daemon Process*/
	if (fork() != 0)
		return 0;					//parent/foreground killed
	(void)signal(SIGCHLD, SIG_IGN); // ignore child death b/c main needs to be receiving requests
	(void)signal(SIGHUP, SIG_IGN);	// ignore terminal hangups
	for (i = 0; i < 32; i++)
		(void)close(i); //close open files
	(void)setpgrp();	/*break away from process group so its not effected by user like logging off*/
	logger(LOG, "nweb starting", argv[1], getpid());

	/* Initializes Condition Variables, mutex, and Creates Thread Pool. */
	threadSystemSetUp(thread_pool_size);

	/* SETUP THE NETWORK SOCKET (332 - 350) */
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) //we have a handle to the socket
		logger(ERROR, "system call", "socket", 0);

	port = atoi(argv[1]); //set port
	if (port < 0 || port > 60000)
		logger(ERROR, "Invalid port number (try 1->60000)", argv[1], 0); //We have a port (meet me at the..)

	/*This code establishes the rules for communication w/ the server */
	serv_addr.sin_family = AF_INET;				   //Set the servers address family (IPv4)
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // bind to/recieve from any address - were not selective who requests. as long as you have an address(IPv4 fam) ill talk to you
	serv_addr.sin_port = htons(port);

	/* bind() associates the socket with its local address [that's why server side binds, so that clients can use that address to connect to server socket.]*/
	if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		logger(ERROR, "system call", "bind", 0);
	if (listen(listenfd, bufferSize) < 0)
	{ //this socket is for recieving and can queue bufferSize # of requests
		logger(ERROR, "system call", "listen", 0);
	}

	/*CONSTANTLY RECEIVE REQUESTS (PRODUCER)*/
	for (hit = 1;; hit++)
	{
		length = sizeof(cli_addr);
		if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0) //wait for requests from client at the listenfd socket
			logger(ERROR, "system call", "accept", 0);

		gettimeofday(&endStamp, NULL);

		pthread_mutex_lock(&mutexQueue);
		if (currentReqCount < bufferSize)
		{									 //Is there room in the buffer? If yes then process request and place it on buffer, otherwise ignore loop around.
			static char buffer[BUFSIZE + 1]; /* static so zero filled */

			long ret = read(socketfd, buffer, BUFSIZE); /* read Web request and put it into buffer */
			if (ret == 0 || ret == -1)
			{ /* read failure stop now */
				logger(FORBIDDEN, "failed to read browser request", "", socketfd);
			}

			// COPY REQUEST TO HAVE ONE TO DETERMINE FILE TYPE AND OTHER TO GIVE TO WEB()
			char old_buffer[strlen(buffer)];
			strcpy(old_buffer, buffer);
			char *token = strtok(buffer, " ");
			token = strtok(NULL, " ");

			// BEGIN SCHEDULING CODE
			if (!strcasecmp(token + (strlen(token) - 5), ".html") || !strcasecmp(token + (strlen(token) - 4), ".htm"))
			{
				file_type = HTML_FILE;
			}
			else if (!strcasecmp(token + (strlen(token) - 4), ".png") || !strcasecmp(token + (strlen(token) - 4), ".jpg") || !strcasecmp(token + (strlen(token) - 5), ".jpeg") || !strcasecmp(token + (strlen(token) - 4), ".gif") || !strcasecmp(token + (strlen(token) - 4), ".ico") || !strcasecmp(token + (strlen(token) - 4), ".zip") || !strcasecmp(token + (strlen(token) - 3), ".gz") || !strcasecmp(token + (strlen(token) - 4), ".tar"))
			{
				file_type = IMAGE_FILE;
			}
			else
			{
				file_type = OTHER_FILE;
			}
			// END SCHEDULING CODE

			// FILL A STRUCT WITH REQUEST DATA
			RequestData *req = requests + currentReqCount; /*Position of struct in request queue*/
			req->clientSocket = socketfd;
			req->xStatReqArrivalCount = totalRequestsArrived++;
			req->xStatReqArrivalTime = ((endStamp.tv_sec * 1000) + (endStamp.tv_usec / 1000)) - ((serverStartStamp.tv_sec * 1000) + (serverStartStamp.tv_usec / 1000)); //in milliseconds
			req->fd = socketfd;
			req->ret = ret;
			req->fileType = file_type;
			strcpy(req->buffer, old_buffer);
			currentReqCount++;
		}
		else
		{
			printf("Buffer Full!\n");
		}
		pthread_mutex_unlock(&mutexQueue);
		pthread_cond_signal(&cond_varQueue);
	}
	pthread_mutex_destroy(&mutexQueue);
	pthread_cond_destroy(&cond_varQueue);
}
