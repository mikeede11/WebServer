/* Generic */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

/* Threads */
#include <pthread.h>
#include <semaphore.h>

/* Network */
#include <netdb.h>
#include <sys/socket.h>

#define BUF_SIZE 100
#define FILE_1 5
#define FILE_2 6

sem_t semaphore;
bool thereAreTwoFiles = false;


void connectSendRecieve(char buf[], char** cmdLine, int fileIndex);

// Get host information (used to establishConnection)
struct addrinfo *getHostInfo(char* host, char* port) {
  int r;
  struct addrinfo hints, *getaddrinfo_res;
  // Setup hints
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if ((r = getaddrinfo(host, port, &hints, &getaddrinfo_res))) {
    fprintf(stderr, "[getHostInfo:21:getaddrinfo] %s\n", gai_strerror(r));
    return NULL;
  }

  return getaddrinfo_res;
}

// Establish connection with host
int establishConnection(struct addrinfo *info) {
  if (info == NULL) return -1;

  int clientfd;
  for (;info != NULL; info = info->ai_next) {
    if ((clientfd = socket(info->ai_family,
                           info->ai_socktype,
                           info->ai_protocol)) < 0) {
      perror("[establishConnection:35:socket]");
      continue;
    }

    if (connect(clientfd, info->ai_addr, info->ai_addrlen) < 0) {
      close(clientfd);
      perror("[establishConnection:42:connect]");
      continue;//repeatedly tries to connect
    }

    freeaddrinfo(info);
    return clientfd;
  }

  freeaddrinfo(info);
  return -1;
}

// Send GET request
void GET(int clientfd, char *path) {
  char req[1000] = {0};
  sprintf(req, "GET %s HTTP/1.0\r\n\r\n", path);//packs the request in a buffer box
  send(clientfd, req, strlen(req), 0);//puts request in socket(ready to ship)
}

void * CONCURRoutine(void *args){
  char threadSpecificBuf[BUF_SIZE];
  char **cmdLine = (char**)args;
  bool currentFile = false;//false is file 1, true is file 2
  int file;
  // Establish connection with <hostname>:<port>
  while(1){
    
    file = thereAreTwoFiles ? (currentFile ? FILE_1: FILE_2): FILE_1;//are there two files? if not default to file one. if there are then use file that currentfile is set to.
    connectSendRecieve(threadSpecificBuf, cmdLine, file);
    currentFile = !currentFile;//flip currentFile
  }
}
void * FIFORoutine(void *args){
  char threadSpecificBuf[BUF_SIZE];
  char **cmdLine = (char**)args;
  bool currentFile = false;
  int file;
  // Establish connection with <hostname>:<port>
  while(1){
    sem_wait(&semaphore);
    file = thereAreTwoFiles ? (currentFile ? FILE_1: FILE_2): FILE_1;//are there two files? if not default to file one. if there are then use file that currentfile is set to.
    connectSendRecieve(threadSpecificBuf,cmdLine, file);
    currentFile = !currentFile;//flip currentFile
    sem_post(&semaphore);
    sleep(1);//maybe to get a better distribution of threads working (this one will be put a the "end of the line"). Also waits a sec b4 reconnect
  }
}

void connectSendRecieve(char buf[], char** cmdLine, int fileIndex){
  int threadSpecificClientfd = establishConnection(getHostInfo(cmdLine[1], cmdLine[2]));//I think the command lines scope is viewable from this function since it is called in main.
  if (threadSpecificClientfd == -1) {
    fprintf(stderr,
            "[main:73] Failed to connect to: %s:%s%s \n",
            cmdLine[1], cmdLine[2], cmdLine[fileIndex]);//little bug wont notice
    return;

  }
  GET(threadSpecificClientfd, cmdLine[fileIndex]); //packs the request in a buffer box. puts request in socketand it ships b/c its connected
  while (recv(threadSpecificClientfd, buf, BUF_SIZE, 0) > 0) {//wait for response and store info in buf
    fputs(buf, stdout);//output buf info on terminal
    memset(buf, 0, BUF_SIZE);//refreshes the buffer to get more data at beg
  }
}

void threadSetUp(int threads, char **argv, void *(*start_routine) (void *)){
   int i;
    pthread_t* threadsArr = (pthread_t*)malloc(sizeof(pthread_t)* threads);
	  for(i = 0; i < threads;i++){
      if(pthread_create(&threadsArr[i], NULL, start_routine, argv) != 0){
        printf("failed to create threads\n");
      }
      else{
        printf("Created Thread!");
        if(pthread_detach(threadsArr[i]) != 0){
          printf("failed to detach threads");
        }
        else{
          fprintf(stdout, "detached Thread!\n");
        }
      }
    }
}

int main(int argc, char **argv) {
  //int clientfd;
  //char buf[BUF_SIZE];
  int numOfTs;

  if (argc != 6 && argc != 7) {//TODO Add additional file capabality
    fprintf(stderr, "USAGE: ./httpclient <hostname> <port> <request path>\n");
    return 1;
  }
  if(argc == 7){thereAreTwoFiles = true;}
  if((numOfTs = atoi(argv[3])) <= 0){
    printf("Thread Specification %s is invalid. Use any integer > 0", argv[3]);
    exit(1);
  }
  if(!strcmp(argv[4], "CONCUR")){
    threadSetUp(numOfTs,argv,&CONCURRoutine);
  }
  else if(!strcmp(argv[4], "FIFO")){
    sem_init(&semaphore, 0,1);
    threadSetUp(numOfTs,argv, &FIFORoutine);
    //sem_destroy(&semaphore);//should we do this?
  }
  else{
    printf("Scheduling Policy %s is invalid. Use either \"CONCUR\" or \"FIFO\"", argv[4]);
    return 1;
  }

  sem_destroy(&semaphore);
  pthread_exit((void*)"Error");
  return 1;
}
// else if(!strcmp(argv[4], "SINGLE")){
  //   char buf[BUF_SIZE];
  //    int clientfd = establishConnection(getHostInfo(argv[1], argv[2]));
  //   if (clientfd == -1) {
  //   fprintf(stderr,
  //           "[main:73] Failed to connect to: %s:%s%s \n",
  //           argv[1], argv[2], argv[5]);
  //   return 3;
  // }

  // // Send GET request > stdout
  //   GET(clientfd, argv[5]); //packs the request in a buffer box. puts request in socketand it ships b/c its connected
  //   while (recv(clientfd, buf, BUF_SIZE, 0) > 0) {//wait for response and store info in buf
  //     fputs(buf, stdout);//output buf info on terminal
  //     memset(buf, 0, BUF_SIZE);//refreshes the buffer to get more data at beg
  //   }

  // close(clientfd);
  // }
