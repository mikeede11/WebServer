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
#include <sys/time.h>
#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404


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

typedef struct RequestData{
	int clientSocket;
	int xStatReqArrivalCount;//add in main - totalRequestsArrived
	suseconds_t xStatReqArrivalTime;//add in main after calc. make sure passby val (use global startStamp)
	int xStatReqDispatchCount;//add this into an individual struct when you take off requests[]
	suseconds_t xStatReqDispatchTime;//add and calculate in thread once you take job (use global startStamp)
	//int xStatReqCompleteCount//do we count a disregarded/rejected request as one that has arrived? assume not.
	//we define completed as the point after the file has been read
	// and just before the worker thread starts writing the response on the socket.
	//nvrm maintain global var and pass that in at this moment in web
	//xStatReqCompleteTime// You can  just calculate this also at that point
	int xStatReqDispatchesThatCameB4Req;//requestsDispatched + size of requests[] or assoc. arrays (sll done at time req is received)
	//xStateReqAge;//xStatReqDispatchCount - xStatReqDispatchesThatCameB4Req

}RequestData;
void safeWrite(int fd, void* buf, size_t cnt, char* msg);
void web(RequestData req, int hitArg);

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
	printf("ENDlOGGER");

	if(type == ERROR || type == NOTFOUND || type == FORBIDDEN) exit(3);
	printf("actual end logger");
}

void safeWrite(int fd, void* buf, size_t cnt, char* msg){
	if(write(fd, buf, cnt) < 0){
		printf("%s: %s\n", msg, strerror(errno));
		exit(1);
	}
}
pthread_mutex_t mutexQueue;
pthread_cond_t cond_varQueue;
RequestData* requests;
int currentReqCount;
int totalRequestsArrived;
int requestsDispatched;
int xStatReqCompleteCount;
int hit;
struct timeval serverStartStamp;

void * threadReqProc(void * args){
	while(1){
		RequestData requestInfo;
		struct timeval dispatchEnd;
		int hitNum;

		pthread_mutex_lock(&mutexQueue);
		while(currentReqCount == 0){
			printf("Condwait called %d", currentReqCount);
			pthread_cond_wait(&cond_varQueue, &mutexQueue);
		}
		requestInfo = requests[0];
		gettimeofday(&dispatchEnd, NULL);
		requestInfo.xStatReqDispatchTime = (dispatchEnd.tv_sec * 1000000 + dispatchEnd.tv_usec) - (serverStartStamp.tv_sec * 1000000 + serverStartStamp.tv_usec);
		requestInfo.xStatReqDispatchCount = requestsDispatched++;
		int i;
		for(i = 0; i < currentReqCount - 1;i++){
			requests[i] = requests[i + 1];
		}
		--currentReqCount;
		hitNum = hit;
		//printf("SOCKETFD'S VALUE IS %d ", socket);
		pthread_mutex_unlock(&mutexQueue);
		printf("Statement B4 WEB");
		web(requestInfo, hitNum);
		//web(socket, hitNum);
	}
}
/* this is a child web server process, so we can exit on errors
static buffer and extension struct */
void web(RequestData req, int hitArg)
{
	printf("BegWeb");
	struct timeval requestCompletion;
	int fd = req.clientSocket;
	int j, file_fd, buflen;
	long i, ret, len;
	char * fstr;
	/*cond var*/
	static char buffer[BUFSIZE+1]; /* static so zero filled */

	ret =read(fd,buffer,BUFSIZE); 	/* read Web request in one go */
	/*cond var - or maybe as long as we use buffer*/
	printf("MidWeb-4");
	if(ret == 0 || ret == -1) {	/* read failure stop now */
		logger(FORBIDDEN,"failed to read browser request","",fd);
	}
	printf("MidWeb-3");
	if(ret > 0 && ret < BUFSIZE){	/* return code is valid chars */
		buffer[ret]=0;		/* terminate the buffer */
		printf("BIK1");
	}else{
	 	buffer[0]=0;
		 printf("Buffer had Nothing");
	}
	printf("MidWeb-2");
	for(i=0;i<ret;i++)	/* remove CF and LF characters */
		if(buffer[i] == '\r' || buffer[i] == '\n')
			buffer[i]='*';
	printf("MidWeb-1");
	logger(LOG,"request",buffer,hitArg);
	printf("MidWeb");
	if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) ) {//is request valid format?
		logger(FORBIDDEN,"Only simple GET operation supported",buffer,fd);
	}
	printf("MidWeb2");
	for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
		if(buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
			buffer[i] = 0;
			break;
		}
	}
	printf("MidWeb3");
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
	printf("MidWeb4");
	if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);
	printf("MidWeb4.5");
	for(int i = 4; i < buflen; i++){
		printf("%c", buffer[i]);
	}
	if(( file_fd = open(&buffer[5],O_RDONLY)) == -1) {  /* open the file for reading (example - index.html) */
		logger(NOTFOUND, "failed to open file",&buffer[5],fd);
	}
	printf("MidWeb5");
	logger(LOG,"SEND",&buffer[5],hitArg);
	gettimeofday(&requestCompletion, NULL);
	len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
	      (void)lseek(file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
          (void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\nX-stat-req-arrival-count: %d\nX-stat-req-arrival-time: %ld\nX-stat-req-dispatch-count: %d\nX-stat-req-dispatch-time: %ld\nX-stat-req-complete-count: %d\nX-stat-req-complete-time: %ld\n\n", VERSION, len, fstr, req.xStatReqArrivalCount,req.xStatReqArrivalTime,req.xStatReqDispatchCount, req.xStatReqDispatchTime, xStatReqCompleteCount++,(requestCompletion.tv_sec * 1000000 + requestCompletion.tv_usec) - (serverStartStamp.tv_sec * 1000000 + serverStartStamp.tv_usec)); /* Header + a blank line */
	logger(LOG,"Header",buffer,hitArg);
	printf("LOGGER MID 6");
	printf("Buffer Before Send %s", buffer);
	
    // Send the statistical headers described in the paper, example below
    
    // (void)sprintf(buffer,"X-stat-req-arrival-count: %d\r\n", xStatReqArrivalCount);
	//  (void)safeWrite(fd,buffer,strlen(buffer), "Write Error: ");
	// (void)sprintf(buffer,"X-stat-req-arrival-count: %d\r\n", req.xStatReqArrivalCount);//it can be a race condition - b/c thats what it is.
	// (void)safeWrite(fd,buffer,strlen(buffer), "Write Error: ");
	// (void)sprintf(buffer,"X-stat-req-arrival-time: %ld\r\n", req.xStatReqArrivalTime);//it can be a race condition - b/c thats what it is.
	// (void)safeWrite(fd,buffer,strlen(buffer), "Write Error: ");
	// (void)sprintf(buffer,"X-stat-req-dispatch-count: %d\r\n", req.xStatReqDispatchCount);//it can be a race condition - b/c thats what it is.
	// (void)safeWrite(fd,buffer,strlen(buffer), "Write Error: ");
	// (void)sprintf(buffer,"X-stat-req-dispatch-time: %ld\r\n", req.xStatReqDispatchTime);//it can be a race condition - b/c thats what it is.
	// (void)safeWrite(fd,buffer,strlen(buffer), "Write Error: ");
	// (void)sprintf(buffer,"X-stat-req-complete-count: %d\r\n", xStatReqCompleteCount++);//it can be a race condition - b/c thats what it is.
	// (void)safeWrite(fd,buffer,strlen(buffer), "Write Error: ");
	// gettimeofday(&requestCompletion, NULL);
	// (void)sprintf(buffer,"X-stat-req-complete-time: %ld\r\n", (requestCompletion.tv_sec * 1000000 + requestCompletion.tv_usec) - (serverStartStamp.tv_sec * 1000000 + serverStartStamp.tv_usec));
	// (void)safeWrite(fd,buffer,strlen(buffer), "Write Error: ");
	//TODO ADD THE OTHER STATISTICS
	//TODO TEST
	//TODO Clean code(print statements, thread, exits and destroys. malloc - free etc, safe functions, comments, delete additional files in dir)
	//TODO read proj doc carefully
	
	safeWrite(fd,buffer,strlen(buffer), "Write Error: ");//puts the contents of index.html header/request
    /* send file in 8KB block - last block may be smaller */
	printf("LOGGER MID 7");
	while (	(ret = read(file_fd, buffer, BUFSIZE)) > 0 ) {//puts the contents of index.html into buffer
		printf("Buffer AFTER Send %s", buffer);
		safeWrite(fd,buffer,ret, "Write Error: ");
	}
	sleep(1);	/* allow socket to drain before signalling the socket is closed */
	close(fd);
	//pthread_exit(NULL)//exit(1);//the child process terminates once it has dealt with request
}

int main(int argc, char **argv)
{
	struct timeval endStamp;
	gettimeofday(&serverStartStamp, NULL); 
	int i, port, listenfd, socketfd, thread_pool_size, bufferSize;//listenfd is file descriptor to listening socket. 
	//socketfd is the socket of the client (where request is coming from/or the return addr)
	socklen_t length;
	static struct sockaddr_in cli_addr; /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */
	/*PART 1 OF MAIN - Ensure server is executed with a portnumber 
	specified and a dir(Top dir) where server can fetch files from (./)*/
	if( argc < 5  || argc > 5 || !strcmp(argv[1], "-?") ) {
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
	printf("LOOK THIS THE THE THREADPOOL SIZE: %d\n", thread_pool_size);
	bufferSize = atoi(argv[4]);
	requests = malloc(sizeof(requests )* bufferSize);//test
	// if(fork() != 0)
	// 	return 0;  //parent returns OK to shell - so parent is gone? yes
	// (void)signal(SIGCHLD, SIG_IGN); // ignore child death b/c main needs to be receiving requests 
	// (void)signal(SIGHUP, SIG_IGN); // ignore terminal hangups -client-server?
	// for(i=0;i<32;i++)
	// 	(void)close(i);		//close open files why?
	// (void)setpgrp();		/*break away from process group so its not effected by user like logging off*/
	logger(LOG,"nweb starting",argv[1],getpid());
	printf("Hello in child");
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
	
	for(hit=1; ;hit++) {
		length = sizeof(cli_addr);
		printf("Buffer BEFORE ACCEPT\n");
		if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)//wait for requests from client put 
			logger(ERROR,"system call","accept",0);
		
		gettimeofday(&endStamp, NULL);
		printf("Buffer LOCK\n");
		pthread_mutex_lock(&mutexQueue);
		printf("Buffer LOCK 22222222\n");
		printf("SOCKETFD'S VALUE IS %d and listenfd's value is %d", socketfd, listenfd);
		printf("\n\nBUFFER SIZE: %d\n", currentReqCount);
		if(currentReqCount < bufferSize){
			RequestData* req = requests + currentReqCount;
			req->clientSocket = socketfd;
			req->xStatReqArrivalCount = totalRequestsArrived++;
			req->xStatReqArrivalTime = (endStamp.tv_sec * 1000000 + endStamp.tv_usec) - (serverStartStamp.tv_sec * 1000000 + serverStartStamp.tv_usec);
			currentReqCount++;
			printf("REQUESTS[0]: STRUCT'S SOCKET VALUE IS %d", requests[hit - 1].clientSocket);
			//requests[currentReqCount++] = req;
		}
		else{
			printf("Buffer Full!\n");
			//(void)close(socketfd);//you will not use this fd b/c it didnt fit in the queue
		}
		printf("Buffer something\n");

		pthread_mutex_unlock(&mutexQueue);
		printf("Buffer something 2222222\n");
		pthread_cond_signal(&cond_varQueue);
		printf("Buffer something 333333333\n");
		//printf("%d",currentReqCount);
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
	//}
	/*for(i = 0; i < thread_pool_size;i++){
		if(pthread_join(threads[i], NULL) != 0){
			printf("failed to create threads");
		}
	}*/
	}
	pthread_mutex_destroy(&mutexQueue);
    pthread_cond_destroy(&cond_varQueue);
}
