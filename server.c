#include <stdio.h>

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
#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404

#define FIFO_MODE 0
#define HPIC_MODE 1
#define HPHC_MODE 2


#define HTML_FILE 0
#define IMAGE_FILE 1
#define OTHER_FILE 2


struct {
	char *ext;
	char *filetype;
} extensions [] = {
	{"gif", "image/gif" },
	{"jpg", "image/jpg" },
	{"jpeg","image/jpeg"},
	{"png", "image/png" },
	{"ico", "image/ico" },
	{"zip", "image/zip" },
	{"gz",  "image/gz"  },
	{"tar", "image/tar" },
	{"htm", "text/html" },
	{"html","text/html" },
	{0,0} };

struct request
{
	char buffer[BUFSIZE+1]; // buffer string
	int fd;			// file descriptor
	int fileType;	// e.g. HTML_FILE
	int ret;		// no idea
};

void safeWrite(int fd, void* buf, size_t cnt, char* msg);
void web(struct request request1, int index, int hitArg);

void logger(int type, char *s1, char *s2, int socket_fd)
{
	int fd ;
	char logbuffer[BUFSIZE*2];

	switch (type) {
	case ERROR:
		(void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",s1, s2, errno,getpid());
		break;
	case FORBIDDEN:
		safeWrite(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",271, "Write Error: ");
		(void)sprintf(logbuffer,"FORBIDDEN: %s:%s",s1, s2);
		break;
	case NOTFOUND:
		safeWrite(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",224, "Write Error: ");
		(void)sprintf(logbuffer,"NOT FOUND: %s:%s",s1, s2);
		break;
	case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",s1, s2,socket_fd); break;
	}
	/* No checks here, nothing can be done with a failure anyway */
	if((fd = open("nweb.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		safeWrite(fd,logbuffer,strlen(logbuffer), "Write Error: ");
		safeWrite(fd,"\n",1, "Write Error: ");
		(void)close(fd);
	}

	if(type == ERROR || type == NOTFOUND || type == FORBIDDEN) exit(3);
}

void safeWrite(int fd, void* buf, size_t cnt, char* msg){
	if(write(fd, buf, cnt) < 0){
		printf("%s: %s\n", msg, strerror(errno));
		exit(1);
	}
}
pthread_mutex_t mutexQueue;
pthread_cond_t cond_varQueue;
//pthread_mutex_init(&mutexQueue, NULL);
//pthread_cond_init(&cond_varQueue, NULL);
int* requests;
int currentReqCount = 0;
int hit;

int schedalg;

// struct request html_requests[bufferSize];
struct request* html_requests;
int currentHtmlCount = 0;

// struct request image_requests[bufferSize];
struct request* image_requests;
int currentImageCount = 0;

// struct request all_requests[bufferSize];
struct request* all_requests;
int currentAllCount = 0;


int chooseHTML(struct request* request1) {
	int i;
	for (i = 0; i < currentAllCount; i++) {
		if (request1[i].fileType == HTML_FILE) {
			return i;
		}
	}
	return 0; // return 0 if there is no HTML is the request queue
}

int chooseImage(struct request* request1) {
	int i;
	for (i = 0; i < currentAllCount; i++) {
		if (request1[i].fileType == IMAGE_FILE) {
			return i;
		}
	}
	return 0; // return 0 if there is no HTML is the request queue
}

void * threadReqProc(void * args){
	while(1) {
		// int socket;
		struct request currRequest;
		int hitNum;

		pthread_mutex_lock(&mutexQueue);
		while(currentReqCount == 0){
			printf("Condwait called %d\n", currentReqCount);
			pthread_cond_wait(&cond_varQueue, &mutexQueue);
		}
		// socket = requests[0];
		int index = 0; // if ANY or FIFO
		if (schedalg == HPIC_MODE) {
			index = chooseImage(all_requests);
			printf("Image chosen!, index: %d\n", index);
		} else if (schedalg == HPHC_MODE) {
			index = chooseHTML(all_requests);
			printf("HTML chosen, index: %d\n", index);
		}
		currRequest = all_requests[index];

		int i;
		for(i = index; i < currentReqCount - 1;i++) {
			requests[i] = requests[i + 1];
			all_requests[i] = all_requests[i + 1];
		}
		currentReqCount--;
		currentAllCount--;
		hitNum = hit;

		// (void)close(listenfd);
		pthread_mutex_unlock(&mutexQueue);
		// for(int j = 0; j < 5000000000; j++);

		web(currRequest, 0, hitNum);
		//web(socket, hitNum);
	}
}
/* this is a child web server process, so we can exit on errors
static buffer and extension struct */
void web(struct request request1, int index, int hitArg)
{
	printf("Please work1, %d\n", request1.fd);
	int fd = request1.fd;
	char* buffer = request1.buffer;

	printf("Please work 1.5 %d\n", index);
	int j, file_fd, buflen;
	long i, ret, len;
	char * fstr;
	/*cond var*/
	// static char buffer[BUFSIZE+1]; /* static so zero filled */


	// ret =read(fd,buffer, BUFSIZE); 	/* read Web request in one go */
	/*cond var - or maybe as long as we use buffer*/
	// if(ret == 0 || ret == -1) {	/* read failure stop now */
	// 	logger(FORBIDDEN,"failed to read browser request","",fd);
	// }
	ret = request1.ret;
	if(ret > 0 && ret < BUFSIZE){	/* return code is valid chars */
		buffer[ret]=0;		/* terminate the buffer */
	}else{
	 	buffer[0]=0;
	}
	printf("Please work2\n");

	for(i=0;i<ret;i++)	/* remove CF and LF characters */
		if(buffer[i] == '\r' || buffer[i] == '\n')
			buffer[i]='*';
	logger(LOG,"request",buffer,hitArg);
	if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) ) {//is request valid format?
		logger(FORBIDDEN,"Only simple GET operation supported",buffer,fd);
	}
	printf("Please work3\n");
	for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
		if(buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
			buffer[i] = 0;
			break;
		}
	}
	for(j=0;j<i-1;j++) 	/* check for illegal parent directory use .. */
		if(buffer[j] == '.' && buffer[j+1] == '.') {
			logger(FORBIDDEN,"Parent directory (..) path names not supported",buffer,fd);
		}
	if( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) ) /* convert no filename to index file */
		(void)strcpy(buffer,"GET /index.html");

	/* work out the file type and check we support it */
	buflen=strlen(buffer);
	fstr = (char *)0;
	for(i=0;extensions[i].ext != 0;i++) {
		len = strlen(extensions[i].ext);
		if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
			fstr =extensions[i].filetype;
			break;
		}
	}
	printf("Please work4\n");
	if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);

	if(( file_fd = open(&buffer[5],O_RDONLY)) == -1) {  /* open the file for reading */
		logger(NOTFOUND, "failed to open file",&buffer[5],fd);
	}
	logger(LOG,"SEND",&buffer[5],hitArg);
	len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
	      (void)lseek(file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
          (void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */
	logger(LOG,"Header",buffer,hitArg);
	safeWrite(fd,buffer,strlen(buffer), "Write Error: ");
	printf("Please work5\n");

    /* Send the statistical headers described in the paper, example below

    (void)sprintf(buffer,"X-stat-req-arrival-count: %d\r\n", xStatReqArrivalCount);
	(void)write(fd,buffer,strlen(buffer));
    */

    /* send file in 8KB block - last block may be smaller */
	printf("BUFFER: %s\n", buffer);
	while (	(ret = read(file_fd, buffer, BUFSIZE)) > 0 ) {
		safeWrite(fd,buffer,ret, "Write Error: ");
	}
	sleep(1);	/* allow socket to drain before signalling the socket is closed */
	close(fd);
	printf("BUFFER 2: %s\n", buffer);
	//pthread_exit(NULL)//exit(1);//the child process terminates once it has dealt with request
}

int main(int argc, char **argv)
{
	int i, port, listenfd, socketfd, thread_pool_size, bufferSize, file_type;//listenfd is file descriptor to listening socket.
	//socketfd is the socket of the client (where request is coming from/or the return addr)
	socklen_t length;
	static struct sockaddr_in cli_addr; /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */
	/*PART 1 OF MAIN - Ensure server is executed with a portnumber
	specified and a dir(Top dir) where server can fetch files from (./)*/
	if( argc < 6  || argc > 6 || !strcmp(argv[1], "-?") ) {
		(void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
	"\tnweb is a small and very safe mini web server\n"
	"\tnweb only servers out file/web pages with extensions named below\n"
	"\t and only from the named directory or its sub-directories.\n"
	"\tThere is no fancy features = safe and secure.\n\n"
	"\tExample: nweb 8181 /home/nwebdir &\n\n"
	"\tOnly Supports:", VERSION);
		for(i=0;extensions[i].ext != 0;i++)
			(void)printf(" %s",extensions[i].ext);

		(void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
	"\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
	"\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n"  );
		exit(0);
	}
	if( !strncmp(argv[2],"/"   ,2 ) || !strncmp(argv[2],"/etc", 5 ) ||
	    !strncmp(argv[2],"/bin",5 ) || !strncmp(argv[2],"/lib", 5 ) ||
	    !strncmp(argv[2],"/tmp",5 ) || !strncmp(argv[2],"/usr", 5 ) ||
	    !strncmp(argv[2],"/dev",5 ) || !strncmp(argv[2],"/sbin",6) ){
		(void)printf("ERROR: Bad top directory %s, see nweb -?\n",argv[2]);
		exit(3);
	}
	if(chdir(argv[2]) == -1){
		(void)printf("ERROR: Can't Change to directory %s\n",argv[2]);
		exit(4);
	}
	/*Part 2 set up accept()/wait for client requests loop
	whats with deamon and no zomb*/
	/* Become deamon + unstopable and no zombies children (= no wait())
	Explain - we want the main program to run in the background because we want the server to be
	accepting requests, but we also want to send the requests and the user can only have control of one process
	at a time (foreground) so we send the server main program to the run in the background. This is a daemon.
	It is accomplished by forking the main process and ending the parent process(ret 0) and allowing the child
	to be the sole process executing the main program. when child processes are created they (almost by def) run in the background
	b/c the foreground process is calling a process to run */
	if((thread_pool_size = atoi(argv[3])) == 0){//HMM doesnt take 10 arg
		thread_pool_size = 5;//get_nprocs();
	}
	bufferSize = atoi(argv[4]);




	if (!strncmp(argv[5], "FIFO", 5) || !strncmp(argv[5],"ANY", 4)) {
		schedalg = FIFO_MODE; // FIFO / ANY
	} else if (!strncmp(argv[5], "HPIC", 5)) {
		schedalg = HPIC_MODE; // HPIC
	} else if (!strncmp(argv[5], "HPHC", 5)) {
		schedalg = HPHC_MODE; // HPHC
	} else {
		(void)printf("ERROR: '%s' is not a valid scheduling policy\n",argv[5]);
		exit(5);
	}


	requests = (int*)malloc(sizeof(int )* bufferSize);//test


	// if(fork() != 0)
	// 	return 0;  //parent returns OK to shell - so parent is gone? yes
	// (void)signal(SIGCHLD, SIG_IGN); // ignore child death b/c main needs to be receiving requests
	// (void)signal(SIGHUP, SIG_IGN); // ignore terminal hangups -client-server?
	// for(i=0;i<32;i++)
	// 	(void)close(i);		//close open files why?
	// (void)setpgrp();		/*break away from process group so its not effected by user like logging off*/
	logger(LOG,"nweb starting",argv[1],getpid());
	if (pthread_mutex_init(&mutexQueue, NULL) != 0) {
		perror("pthread_mutex_init() error");
		exit(1);
	}

	if (pthread_cond_init(&cond_varQueue, NULL) != 0) {
		perror("pthread_cond_init() error");
		exit(2);
	}
	//pthread_mutex_init(&mutexQueue, NULL);
	//pthread_cond_init(&cond_varQueue, NULL);
	pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t)* thread_pool_size);
	for(int i = 0; i < thread_pool_size;i++){
		if(pthread_create(&threads[i], NULL, &threadReqProc, NULL) != 0){
			printf("failed to create threads\n");
		}
		else{
			fprintf(stdout, "created Thread!\n");
		}
	}
	//two cond var concepts A) a dispatcher thread whos cond var is to aportion a client request to a thread when one is available a thread needs to signal once its done
	//also a thread only executes web code if A) theres a request in the buffer
	//fprintf(stdout, "HIYA");

	/* SETUP THE NETWORK SOCKET */
	//reestablish connection
	if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)//we have a handle to the socket
		logger(ERROR, "system call","socket",0);
	port = atoi(argv[1]);
	if(port < 0 || port >60000)
		logger(ERROR,"Invalid port number (try 1->60000)",argv[1],0);//We have a port (meet me at the..)
	/*This code establishes the rules for communication w/ the server*/
	serv_addr.sin_family = AF_INET;//Set the servers address family (IPv4)
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);//bind to/recieve from any address - were not sellective who requests. as long as you have an address(IPv4 fam) ill talk to you
	serv_addr.sin_port = htons(port);//(client meet me at port...)
	if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)//bind() associates the socket with its local address [that's why server side binds, so that clients can use that address to connect to server.]
		logger(ERROR,"system call","bind",0);
	if( listen(listenfd,bufferSize) <0){//this socket is for recieving and can queue 64 requests
		logger(ERROR,"system call","listen",0);
	}


	//fprintf(stdout, "LINE 272");
	/*NETWORK SOCKET SETUP*/
	//question should the amount of requests the socket can queue be modified or only do accept()when threads available?
	//I assume its foucsed on accept b/c theres no waiting really it just sends errorback to client.
	//this isnt what our server should be doing. it should process request when it has available thread.

	all_requests = (struct request*)malloc(sizeof(all_requests) *bufferSize);

	for(hit=1; ;hit++) {
		printf("HERE 1\n");
		length = sizeof(cli_addr);
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)//wait for requests from client put
			logger(ERROR,"system call","accept",0);

		printf("HERE 1.5\n");
		// BEGIN SCHEDULING CODE
		static char buffer[BUFSIZE+1]; /* static so zero filled */
		int ret =read(socketfd,buffer,BUFSIZE); 	/* read Web request in one go */
		printf("HERE 1.7\n");
		if(ret == 0 || ret == -1) {	/* read failure stop now */
			logger(FORBIDDEN,"failed to read browser request","",socketfd);
		}
		printf("HERE 2\n");

		char old_buffer[strlen(buffer)];
		strcpy(old_buffer, buffer);
		char* token = strtok(buffer, " ");
		token = strtok(NULL, " ");

		if (!strcasecmp(token + (strlen(token)-5), ".html")) {
			file_type = HTML_FILE;

			// printf("HTML!\n");
		} else if (!strcasecmp(token + (strlen(token)-4), ".png") || !strcasecmp(token + (strlen(token)-4), ".jpg")) {
			file_type = IMAGE_FILE;
		} else {
			file_type = OTHER_FILE;
		}

		printf("HERE 3\n");

		// END SCHEDULING CODE

		pthread_mutex_lock(&mutexQueue);
		/*while(busy_threads >= thread_pool_size){//queue count >= thread pool size
			pthread_cond_wait(&cond_var, &mutex)
		}*/
		if(currentReqCount < bufferSize) {
			all_requests[currentAllCount].fd = socketfd;
			all_requests[currentAllCount].ret = ret;
			all_requests[currentAllCount].fileType = file_type; // e.g. HTML_FILE
			strcpy( all_requests[currentAllCount].buffer, old_buffer );

			currentReqCount++;
			currentAllCount++;

		}
		printf("HERE 4\n");


		pthread_mutex_unlock(&mutexQueue);
		pthread_cond_signal(&cond_varQueue);
		//(void)close(socketfd); // put at end of web function

		//fill up a queue of socketfds as long as possible or number of threads.
		//give the threads a method that takes the socketfd from the queue when queue is not empty.
		//use mutexs on both ends so when we add to the queue its not messed up and take from queue its not messed up.
		//also use pthreads_cond_wait to make sure we don get into busy loops when a cond is not true.
		/*if((pid = fork()) < 0) {
			logger(ERROR,"system call","fork",0);
		}
		else {
			if(pid == 0) { 	//child
				(void)close(listenfd);
				web(socketfd,hit); // never returns
			} else { 	// parent
				(void)close(socketfd);//we gave the socket to the child who will deal w/ that request
			}
		}*/
	}
	/*for(i = 0; i < thread_pool_size;i++){
		if(pthread_join(threads[i], NULL) != 0){
			printf("failed to create threads");
		}
	}*/
	pthread_mutex_destroy(&mutexQueue);
    pthread_cond_destroy(&cond_varQueue);
}
