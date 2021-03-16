/* Generic */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>


/* Network */
#include <netdb.h>
#include <sys/socket.h>

#define BUF_SIZE 100

sem_t semaphore;

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
  // Establish connection with <hostname>:<port>
  while(1){
  int threadSpecificClientfd = establishConnection(getHostInfo(cmdLine[1], cmdLine[2]));//I think the command lines scope is viewable from this function since it is called in main.
  if (threadSpecificClientfd == -1) {
    fprintf(stderr,
            "[main:73] Failed to connect to: %s:%s%s \n",
            cmdLine[1], cmdLine[2], cmdLine[5]);
    pthread_exit(((void *)"Failed To Connect"));//TODO erase so it constantly tries to connect

  }
  
     // Send GET request > stdout
     
     //printf("BegLOoP");
    GET(threadSpecificClientfd, cmdLine[5]); //packs the request in a buffer box. puts request in socketand it ships b/c its connected
   //printf("After get");
   int error;
    while ((error = recv(threadSpecificClientfd, threadSpecificBuf, BUF_SIZE, 0)) > 0) {//wait for response and store info in buf
      if(error == -1){printf("ERROR NUMBER %d", errno);}
      if(error == 0){printf("Disconnected");}
      printf("error is %d", error);
      printf("FPUTS RETURNS %d ERRNO IS %d", fputs(threadSpecificBuf, stdout), errno);//output buf info on terminal
      memset(threadSpecificBuf, 0, BUF_SIZE);//refreshes the buffer to get more data at beg
      //printf("after recv");
    }
  }
}
void * FIFORoutine(void *args){
  char threadSpecificBuf[BUF_SIZE];
  char **cmdLine = (char**)args;
  // Establish connection with <hostname>:<port>
  while(1){
    sem_wait(&semaphore);
  int threadSpecificClientfd = establishConnection(getHostInfo(cmdLine[1], cmdLine[2]));//I think the command lines scope is viewable from this function since it is called in main.
  if (threadSpecificClientfd == -1) {
    fprintf(stderr,
            "[main:73] Failed to connect to: %s:%s%s \n",
            cmdLine[1], cmdLine[2], cmdLine[5]);
    pthread_exit(((void *)"Failed To Connect"));//TODO erase so it constantly tries to connect

  }
  
     // Send GET request > stdout
     
     //printf("BegLOoP");
    GET(threadSpecificClientfd, cmdLine[5]); //packs the request in a buffer box. puts request in socketand it ships b/c its connected
   //printf("After get");
   int error;
    while ((error = recv(threadSpecificClientfd, threadSpecificBuf, BUF_SIZE, 0)) > 0) {//wait for response and store info in buf
      if(error == -1){printf("ERROR NUMBER %d", errno);}
      if(error == 0){printf("Disconnected");}
      printf("error is %d", error);
      printf("FPUTS RETURNS %d ERRNO IS %d", fputs(threadSpecificBuf, stdout), errno);//output buf info on terminal
      memset(threadSpecificBuf, 0, BUF_SIZE);//refreshes the buffer to get more data at beg
      //printf("after recv");
    }
    sem_post(&semaphore);
    sleep(1);//maybe to get a better distribution of threads working (this one will be put a the "end of the line")
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
  if((numOfTs = atoi(argv[3])) <= 0){
    printf("Thread Specification %s is invalid. Use any integer > 0", argv[3]);
    return 1;
  }
  if(!strcmp(argv[4], "CONCUR")){
    int i;
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t)* numOfTs);
	  for(i = 0; i < numOfTs;i++){
      if(pthread_create(&threads[i], NULL, &CONCURRoutine, argv) != 0){
        printf("failed to create threads\n");
      }
      else{
        printf("Created Thread!");
        if(pthread_detach(threads[i]) != 0){
          printf("failed to detach threads");
        }
        else{
          fprintf(stdout, "detached Thread!\n");
        }
      }
    }
  }
  else if(!strcmp(argv[4], "FIFO")){
    sem_init(&semaphore, 0,1);
    int i;
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t)* numOfTs);
	  for(i = 0; i < numOfTs;i++){
      if(pthread_create(&threads[i], NULL, &FIFORoutine, argv) != 0){
        printf("failed to create threads\n");
      }
      else{
        printf("Created Thread!");
        if(pthread_detach(threads[i]) != 0){
          printf("failed to detach threads");
        }
        else{
          fprintf(stdout, "detached Thread!\n");
        }
      }
    }
    sem_destroy(&semaphore);
  }
  else if(!strcmp(argv[4], "SINGLE")){
    char buf[BUF_SIZE];
     int clientfd = establishConnection(getHostInfo(argv[1], argv[2]));
    if (clientfd == -1) {
    fprintf(stderr,
            "[main:73] Failed to connect to: %s:%s%s \n",
            argv[1], argv[2], argv[5]);
    return 3;
  }

  // Send GET request > stdout
    GET(clientfd, argv[5]); //packs the request in a buffer box. puts request in socketand it ships b/c its connected
    while (recv(clientfd, buf, BUF_SIZE, 0) > 0) {//wait for response and store info in buf
      fputs(buf, stdout);//output buf info on terminal
      memset(buf, 0, BUF_SIZE);//refreshes the buffer to get more data at beg
    }

  close(clientfd);
  }
  else{
    printf("Scheduling Policy %s is invalid. Use either \"CONCUR\" or \"FIFO\"", argv[4]);
    return 1;
  }
  
  
  
     


  // // Establish connection with <hostname>:<port>

  //sem_destroy(&semaphore);
  pthread_exit((void*)"Error");
}
