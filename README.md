#WebServer
A multithreaded web server

##Michael Edelman & Shahar Lazarev

##How work was divided
Divided work: The work was divided on-the-spot as we went further into the project. It didn't make sense for us to try to divide it up early on because we didn't have a clear understanding of the exact details and difficulties that would be involved in the different sections. We first looked over the project specifications to try many times to understand the requirements. We then spent many hours slowly but surely learning about web servers, creating and managing threads, and many other subsidiary topics. After that we divided the work according to spare time to work on the project. Shahar worked to get the original server and client to compile and run. Michael focused on part 1 of the project, to get the multithreaded server working. Then, concurrently, Shahar worked on setting up the scheduling policies, and Michael worked on getting the client multithreaded (which would be similar to the server) and on the statistics.

##Design Overview

Server Input: The Server recieves input from the command line that specifies the A) port at which to connect with it at, B) the directory that contains the files it will 'serve', C) the number of N threads it will create to distribute processing client requests, D) the number that denotes the size of a buffer that will contain the 'requests to do', and E) a string denoting a priority hierarchy to the requests (some have priority before others).

###Validation and SetUp
We validate the input with various checks. Once validated, we establish the scheduling algorithm by setting a global variable, schedalg to an integer that represents which scheduling algorithm was selected. We use Macros for the integers for clarity {FIFO_MODE = 0, HPIC_MODE = 1, HPHC_MODE = 2}. Note: FIFO_MODE also represents the ANY scheduling policy. We create a Daemon process so the Server can be in the background continually receiving requests. We then precede to setup the multi-threaded feature with the threadSystemSetUp(). At this point N threads have been initated that will wait to be signaled by the Main thread to process a request (details for next paragraph). Back In the main thread, The Network Socket is setup to receive requests and using the accept() and an infinite loop the server will constantly be waiting for and putting requests in a buffer.

###Multi-threading and the Buffer
The relationship between the Main thread and the worker threads models a producer-consumer relationship. We us a queue of structs called requests that is shared by the main thread and the worker threads. The main thread puts requests on it and the worker threads take them off it as determined by the scheduling policy. Since all these threads are executing concurrently we use condition variables and mutexes to organize the threads such that one's execution does not corrupt another's. The requests queue also acts as our buffer because the Main thread will only put a request on the buffer requests the queue if the current request count (maintained by currentReqCount) is less than the specified buffer size. Otherwise the main thread will simply ignore that request and loop around.

###The Request Struct
We needed to use the request data to figure out the file extension before the main thread adds the task to the buffer. Therefore we created a struct called RequestData which stored the data we received and then we passed in the RequestData struct into the web() method. The RequestData struct also contains all the usage statistic variables. It collects these statistics as its passed from the Main thread, to its dispatch thread, to the web function until the response has been completed.

###Scheduling Algorithm 
If a specific Scheduling algorithm was set other than ANY or FIFO, we use the chooseFile() function to pick out the next request index with the highest priority wether that be an image or text file as per the scheduling algorithm (Each struct contains wether each request is an image or text file). Otherwise we just use a queue to model the typical FIFO behavior. We then adjust the queue after accordingly.

Throughout We maintain Usage statistics that we write to the HTTP Header. As the Web Server we modified the web() takes the request and sends back the client the requested file.

###The Client 
Input: The Client recieves command line info that specifies the host it will be requesting data from, the port, The number of threads N it will use to concurrently and repeatedly request the server, a string that denotes the scheduling algorithm which will determine how to send the requests. The last two arguments are the files being requested (their directories). The second one is optional.

The client validates the info and then precedes with a course of action depending on the scheduling policy. if it was CONCUR, the client creates N threads and indiscriminately processes the request repeatedly. If it was FIFO then we use a semaphore to ensure only one thread requests at a time. The threads go in aloop and constantly reconnect and send requests. If there are two files each thread alternates between requesting for that file. This is achieved with a boolean variable called currentFile. when its value is false/0 a thread requests for file 1. When its true/1 a thread requests for the second file. fter every request we switch the value of this variable so the thread will alternate.

The code is well structured/organized, efficient, and readable/clean (in my opinion). It also contains helpful comments

##Complete Specification
A major ambiguity which was the relationship between the main thread and the buffer and threads. At one point we weren't sure if the requests should be put directly into the buffer and then passed to the threads, or they should go directly to the threads, and when all the threads were full, they should begin to occupy the buffer. After closer inspection of the requirements we decided that it was the former, and that's how we implemented our buffer.

Another Ambiguity was what to do with client requests when the buffer was full. Should they count towards statistics? should they be completely ignored? We chose the latter.

With regards to implementing the ANY policy, the specifications said that it can be any of the policies so we defaulted it to the FIFO policy.

##Known Bugs or Problems
There is nothing that is known to not work that is required to work by the project doc. There are concerns however because usually every malloc() is supposed to have a free() and the pthread, mutex, and semaphore initializations are conventionally paired with their explicit destruction. However since both the Server and the Client are perpetually running programs we felt this was not supposed to be done. We are unsure if there was anything we should have done about it though. Again evrything seems to work fine.

##Testing
We tested by running the Server and the client in different modes. All seemingly worked as we received the files we requested, wether we requested an image, text or both. We also carefully analyzed the statistics to see if they made sense we determined they did. We also used the web browser to see if our statistics were recognized as part of the HTTP Header. They were. We woul also intentionally overload the server with either an image or picture request in certain priority modes. based on the priority and since the requests were too much only one the prioritized extension should show up which is exactly what we observed. We did a multitude of other tests.
