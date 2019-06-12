/* 
 * Author : Mrinal Aich
 */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <ctime>
#include <queue>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;
/*
 * Limitation: 
 * 1. Max number of Nodes - MAX_NUM_NODES
 * 2. Max number of outLinks - MAX_NUM_NBRS
 */
#define uint unsigned int
#define MAX_NUM_NODES 12
#define MAX_NUM_NBRS 12
#define SUCCESS 1
#define FAILURE 0
#define BUFSIZE 256
#define THREAD_SLEEP_MILLISECONDS 100
#define GET_LOCAL_TIME() char strTime[32] = {0}; \
                         struct tm *localTime; \
                         time_t currentTime; \
                         time( &currentTime ); \
                         localTime = localtime( &currentTime ); \
                         sprintf(strTime, "%u:%u:%u", localTime->tm_hour, localTime->tm_min, localTime->tm_sec);

#define LOG_MESSAGE(format, ...)    { \
                                        pthread_mutex_lock(&fileLock); \
                                        logFile = fopen(logFileName, "a+"); \
                                        fprintf(logFile, format "\n", ##__VA_ARGS__); \
                                        fclose(logFile); \
                                        pthread_mutex_unlock(&fileLock); \
                                    }

#define GET_VECTOR_CLOCK(buf)   pthread_mutex_lock(&vectorLock); \
                                /* Create Message with Vector Clock */ \
                                char temp[64]; \
                                sprintf(buf, "[%u, ", ptrVectorTime[0]); \
                                for(uint i = 1; i< nodes-1; i++) { \
                                    sprintf(temp, "%u, ", ptrVectorTime[i]); \
                                    strcat(msg, temp); } \
                                sprintf(temp, "%u]", ptrVectorTime[nodes-1]); \
                                strcat(msg, temp); \
                                pthread_mutex_unlock(&vectorLock);

/* Data Structures */
/* Parameters for Node Thread */ 
struct nodeThreadParams {
    uint nodeID;
    std::vector<uint> adjList;
};

/* Parameters for Client Thread */ 
struct clientThreadParams {
    uint threadID;
    uint nodeID;
    uint nbrID;
    std::queue<char*> *ptrThreadQueue;
    pthread_mutex_t   *ptrQueueLock;
    pthread_mutex_t   *ptrVectorLock;
    uint              *ptrVectorTime;
};

/* Parameters for Server Thread */ 
struct serverThreadParams {
    uint nodeID;
    pthread_mutex_t  *ptrVectorLock;
    uint             *ptrVectorTime;
    uint             *ptrLUpdate;
};

/* Enum: Type of Event */
typedef enum e_EventType {
    INTERNAL = 0,
    SEND_MESSAGE = 1
} eventType;

/* Enum: Type of Algorithm */
typedef enum e_TimeAlgorithm {
    VECTOR_CLOCK = 0,
    SK_OPTIMIZATION = 1
} eTimeAlgo;

// Main Thread Parameters
uint nodes, interEventTime, ratioNum, ratioDeno;
uint PORT_START, NO_OF_EVENTS_TO_EXECUTE;
std::vector<uint>   graph[MAX_NUM_NODES];
pthread_mutex_t     fileLock;
FILE*               logFile;
char                logFileName[25];
eTimeAlgo           algorithm;
uint                noEntriesReq;
uint                noMsgs;

/* Function Protoypes */
// General Functions
eventType getRandEvent(std::vector<uint> &randEvents);
void strVectorClock(char* msg, pthread_mutex_t* ptrVectorLock,
                    uint* ptrVectorTime, bool lockReq);
void *NodeThreadFunction(void *nodeParams);
void calcRatio(char* ratio);

// Client Functions
void *ClientThreadFunction(void *threadParams);
bool ClientConnectToNbr(uint &sockfd, uint nbrPort);
bool sendMessageToNbr(uint nodeID, uint threadID, uint nbrID,
                      pthread_mutex_t* ptrThreadQueueLock, std::queue<char*>  *ptrMessageQueue,
                      pthread_mutex_t* ptrVectorLock, uint *ptrVectorTime,
                      uint* ptrLSent, uint* ptrLUpdate);


// Server Functions
void *ServerThreadFunction(void *ptrParams);
bool HandleMessage(uint clntSock, uint nodeID, pthread_mutex_t* ptrVectorLock, uint* ptrVectorTime, uint* ptrLUpdate);
bool AcceptTCPConnection(uint nodeID, uint servSock, uint &clntSock);
bool ServerSocketCreate(uint nodeID, pthread_mutex_t* ptrVectorLock, uint* ptrVectorTime, uint* ptrLUpdate);
void serverThreadUpdateClock(char* message, uint nodeID, pthread_mutex_t* ptrVectorLock, uint* ptrVectorTime, uint* ptrLUpdate);

int main(int argc, char *argv[])
{
    uint i,j,k,id;
    uint u,v;
    int rc;
    char ratio[16] = {0};

    // Sanity Check
    if(argc < 3)
    {
        printf("Invalid Input Arguments.\n");
        return 0;
    }

    PORT_START = atoi(argv[1]);
    // Algorithm to follow
    algorithm = atoi(argv[2]) == 0 ? VECTOR_CLOCK : SK_OPTIMIZATION;
    // Log File Name
    if(atoi(argv[2]) == 0)
        strcpy(logFileName, "logVecClk.txt");
    else
        strcpy(logFileName, "logSKOpt.txt");

    pthread_t *ptrNodeThreads;

    // Input FileName
    if (argc == 4)
        freopen((char*)argv[3], "r", stdin);
    else
        freopen("in-params.txt", "r", stdin);
    scanf("%u %u %s", &nodes , &interEventTime, ratio);

    calcRatio(ratio);

    // Network Topology
    for(i=0;i<nodes;i++)
        graph[i].clear();

    char inputStr[64], *ptr;
    for(uint iter = 0; iter < nodes; iter++)
    {
        scanf("%u", &u);
        memset(inputStr, 0x00, sizeof(inputStr));
        scanf("%[^\n]s", inputStr);

        ptr = strtok(inputStr, " ");
        while(ptr != NULL)
        {
            v = atoi(ptr);
            graph[u-1].push_back(v-1);
            ptr = strtok(NULL, " ");
        }
        getchar();
    }

    // Initialize Global Locks
    pthread_mutex_init(&fileLock,0);

    // Restart Log-file
    logFile = fopen(logFileName, "w");
    fprintf(logFile, "");
    fclose(logFile);

    // Create Node Thread
    ptrNodeThreads = (pthread_t*)malloc(sizeof(pthread_t) * nodes);
    nodeThreadParams nodeParams[MAX_NUM_NODES] = {0};
    for(uint nodeID = 0; nodeID < nodes; nodeID++)
    {
        memset(&nodeParams[nodeID], 0x00, sizeof(nodeParams[nodeID]));
        nodeParams[nodeID].nodeID = nodeID;
        nodeParams[nodeID].adjList.clear();
        nodeParams[nodeID].adjList = graph[nodeID];
 
        rc = pthread_create(&ptrNodeThreads[i], NULL, NodeThreadFunction, (void *)&nodeParams[nodeID]);
        if (rc)
        {
            LOG_MESSAGE("Error:unable to create Node thread");
        }
    }

    usleep(50000000);


    LOG_MESSAGE("#Entries: %u | #Msgs: %u", noEntriesReq, noMsgs);

    // Cleanup Memory
    pthread_mutex_destroy(&fileLock);

    // Output No. of entries used
    FILE* outputFile = fopen("outputEntries.txt", "a+");
    fprintf(outputFile, "%u %u %u %u\n", atoi(argv[2]), nodes, noEntriesReq, noMsgs);
    fclose(outputFile);

    // Cleanup Memory
    pthread_mutex_destroy(&fileLock);

    return 0;
}

void *NodeThreadFunction(void *params)
{
    uint i, rc;
    nodeThreadParams* sParams = (nodeThreadParams*)params;

    uint nodeID                 = sParams->nodeID;
    std::vector<uint> adjList   = sParams->adjList;
    uint nbrs                   = adjList.size();

    // Node Specific Variables
    pthread_t*          ptrThreads;
    clientThreadParams* ptrClientParams;
    serverThreadParams  sServerParams;
    pthread_mutex_t     vectorLock;
    pthread_mutex_t*    ptrThreadQueueLock;
    std::queue<char*>   ptrMessageQueue[MAX_NUM_NBRS];
    uint                *ptrVectorTime, *ptrLSent = NULL, *ptrLUpdate = NULL;
    std::vector<uint>   randEvents;

    // Dynamically allocate Data Structures
    ptrThreads          = (pthread_t*)malloc(sizeof(pthread_t) * (nbrs+1));
    ptrClientParams     = (clientThreadParams*)malloc(sizeof(clientThreadParams) * nbrs);
    ptrThreadQueueLock  = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t) * nbrs);
    ptrVectorTime       = (uint*)malloc(sizeof(uint) * nodes);
    if(algorithm == SK_OPTIMIZATION)
    {
        ptrLSent            = (uint*)malloc(sizeof(uint) * nodes);
        ptrLUpdate          = (uint*)malloc(sizeof(uint) * nodes);
    }
    
    // Initialize Vector Clock
    for(i=0; i<nodes;i++)
    {
        ptrVectorTime[i] = 0;
        if(algorithm == SK_OPTIMIZATION)
        {
            ptrLSent[i]   = 0;
            ptrLUpdate[i] = 0;
        }
    }

    // Initialize Vector Lock
    pthread_mutex_init(&vectorLock,0);

    // Create Server Thread
    serverThreadParams serverParams = {0};
    serverParams.nodeID = nodeID;
    serverParams.ptrVectorLock = &vectorLock;
    serverParams.ptrVectorTime = ptrVectorTime;
    serverParams.ptrLUpdate    = ptrLUpdate;
    
    rc = pthread_create(&ptrThreads[nbrs], NULL, ServerThreadFunction, (void *)&serverParams);
    if (rc)
    {
        LOG_MESSAGE("Error:unable to create Server thread");
    }

    // Delay added for Server Sockets to get ready
    sleep(3);

    // Create Client Threads
    for(i=0; i<nbrs; i++)
    {
        memset(&ptrThreadQueueLock[i], 0x00, sizeof(ptrThreadQueueLock[i]));

        pthread_mutex_init(&ptrThreadQueueLock[i],0);

        ptrClientParams[i].threadID         = i;
        ptrClientParams[i].nodeID           = nodeID;
        ptrClientParams[i].nbrID            = adjList[i];
        ptrClientParams[i].ptrThreadQueue   = &ptrMessageQueue[i];
        ptrClientParams[i].ptrQueueLock     = &ptrThreadQueueLock[i];
        ptrClientParams[i].ptrVectorLock    = &vectorLock;
        ptrClientParams[i].ptrVectorTime    = ptrVectorTime;

        rc = pthread_create(&ptrThreads[i], NULL, ClientThreadFunction, (void *)&ptrClientParams[i]);
        if(rc)
        {
            LOG_MESSAGE("Error:unable to %d create Client thread ", rc);
        }
    }

    // Vector Clock Algorithm starts HERE... Ufff!!!
    // The setup was itself a big task...
    usleep(THREAD_SLEEP_MILLISECONDS * 10 * 1000);
    
    uint noOfEvents = 1, event;
    char msg[BUFSIZE];
    while(noOfEvents <= NO_OF_EVENTS_TO_EXECUTE)
    {
        usleep(interEventTime * 1000);

        if(getRandEvent(randEvents) == INTERNAL)
        {
            // Increment Local Clock
            pthread_mutex_lock(&vectorLock);
            event = ++ptrVectorTime[nodeID];
            if(algorithm == SK_OPTIMIZATION)
                ptrLUpdate[nodeID]++;

            memset(msg, 0x00, sizeof(msg));
            strVectorClock(msg, &vectorLock, ptrVectorTime, false);

            pthread_mutex_unlock(&vectorLock);

            //Process1 executes internal event e11 at 10:00, vc: [1 0 0 0]
            GET_LOCAL_TIME();
            LOG_MESSAGE("Process%u executes internal event e%u at %s, vc: %s", nodeID+1, event, strTime, msg);
        }
        else // SEND_MESSAGE To a Nbr
        {
            uint randNbr = rand() % nbrs;
            if(SUCCESS != sendMessageToNbr(nodeID, randNbr, adjList[randNbr], 
                        &ptrThreadQueueLock[randNbr], &ptrMessageQueue[randNbr],
                        &vectorLock, ptrVectorTime, ptrLSent, ptrLUpdate))
            {
                LOG_MESSAGE("Unable to send Message to Neighbor...");
            }
        }

        noOfEvents++;
    }

    // Join all threads
    for(i=0;i<=nbrs;i++)
        pthread_join(ptrThreads[i],NULL);

    // Cleanup Memory
    pthread_mutex_destroy(&vectorLock);
    for(i=0;i<nbrs;i++)
        pthread_mutex_destroy(&ptrThreadQueueLock[i]);

    free(ptrThreads);
    free(ptrClientParams);
    free(ptrThreadQueueLock);
    free(ptrVectorTime);

    pthread_exit(NULL);
}

/* Server Thread Functions */

void serverThreadUpdateClock(char* message, uint nodeID, pthread_mutex_t* ptrVectorLock, uint* ptrVectorTime, uint* ptrLUpdate)
{
    char *ptr, tempStrVector[BUFSIZE];
    uint messageVectorVal;
    
    ptr = strtok(message, " [,]");
    uint nbrID = atoi(ptr);
    
    pthread_mutex_lock(ptrVectorLock);

    // Increment Local Clock
    ptrVectorTime[nodeID]++;

    // Receiver's Vector Clock Algorithm
    if( algorithm == VECTOR_CLOCK)
    {
        ptr = strtok(NULL, "[,]");
        for(uint i=0; (i<nodes) & (ptr != NULL); i++)
        {
            messageVectorVal = atoi(ptr);
            ptrVectorTime[i] = max(ptrVectorTime[i], messageVectorVal);

            ptr = strtok(NULL, "[,]");
        }
    }
    else //if( algorithm == SK_OPTIMIZATION)
    {
        // Update Last Update
        ptrLUpdate[nodeID] = ptrVectorTime[nodeID];
        uint messageVectorIndex;
        ptr = strtok(NULL, "(,)");
        while(ptr != NULL)
        {
            messageVectorIndex = atoi(ptr);
            ptr = strtok(NULL, "(,)");

            messageVectorVal = atoi(ptr);
            ptr = strtok(NULL, "(,)");

            if( ptrVectorTime[messageVectorIndex-1] < messageVectorVal )
            {
                ptrVectorTime[messageVectorIndex-1] = messageVectorVal;
                // Update Last Update Vector
                ptrLUpdate[messageVectorIndex - 1]  = ptrVectorTime[nodeID];
            }
        }
    }

    memset(tempStrVector, 0x00, sizeof(tempStrVector));
    strVectorClock(tempStrVector, ptrVectorLock, ptrVectorTime, false);
    
    //Process2 receives m31 from process3 at 10:05, vc: [0 3 1 0]
    GET_LOCAL_TIME();
    LOG_MESSAGE("Process%u receives m%u%u from process%u at %s, vc: %s", nodeID+1, nbrID, nodeID+1, nbrID, strTime, tempStrVector);

    pthread_mutex_unlock(ptrVectorLock);
}


void *ServerThreadFunction(void *params)
{
    serverThreadParams* sParams     = (serverThreadParams*)params;

    if(SUCCESS != ServerSocketCreate(sParams->nodeID, sParams->ptrVectorLock, sParams->ptrVectorTime, sParams->ptrLUpdate))
    {
        LOG_MESSAGE("Unable to create Server Socket. Existing!!!");
        exit(1);
    }
}

bool ServerSocketCreate(uint nodeID, pthread_mutex_t* ptrVectorLock, uint* ptrVectorTime, uint* ptrLUpdate)
{
    uint servSock;
    char servIP[64];
    strcpy(servIP, "127.0.0.1");
    in_port_t servPort = PORT_START + nodeID;

	// create socket for incoming connections
	if ((servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
		perror("socket() failed");
		exit(-1);
	}

  	// Set local parameters
	struct sockaddr_in servAddr;
	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(servPort);

	// Bind to the local address
	if (bind(servSock, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
		perror("bind() failed");
		exit(-1);
	}

	// Listen to the client
	if (listen(servSock, nodes) < 0) {
		perror("listen() failed");
		exit(-1);
	}

	// Prepare for using select()
	fd_set orgSockSet; // Set of socket descriptors for select
	FD_ZERO(&orgSockSet);
	FD_SET(servSock, &orgSockSet);
	int maxDescriptor = servSock;

	// Setting select timeout as Zero makes select non-blocking
	struct timeval timeOut;
	timeOut.tv_sec = 0; // 0 sec
	timeOut.tv_usec = 100; // 0 microsec

    // Server Loop
	do
    {
		// The following process has to be done every time
		// because select() overwrite fd_set.
		fd_set currSockSet;
		memcpy(&currSockSet, &orgSockSet, sizeof(fd_set));

		select(maxDescriptor + 1, &currSockSet, NULL, NULL, &timeOut);

		for (uint currSock = 0; currSock <= maxDescriptor; currSock++)
        {
			if (FD_ISSET(currSock, &currSockSet))
            {
				// A new client, Establish TCP connection, 
                // register a new socket to fd_sed to watch with select()
				if (currSock  == servSock)
                {
					uint newClntSock;
					if (SUCCESS != AcceptTCPConnection(nodeID, servSock, newClntSock))
                    {
                        LOG_MESSAGE("Unable to Accept Connection from Neighbor");
                        exit(-1);
                    }

					FD_SET(newClntSock, &orgSockSet);
					if (maxDescriptor < newClntSock)
						maxDescriptor = newClntSock;
				}
                // Handle the message
				else
                {
					if( SUCCESS != HandleMessage(currSock, nodeID, ptrVectorLock, ptrVectorTime, ptrLUpdate))
						FD_CLR(currSock, &orgSockSet);
				}
            }
        }

    } while(true);

	int closingSock;
	for (closingSock = 0; closingSock < maxDescriptor + 1; closingSock++)
		close(closingSock);

    return SUCCESS;
}

bool AcceptTCPConnection(uint nodeID, uint servSock, uint &clntSock) 
{
    bool retVal = SUCCESS;

    do
    {
        struct sockaddr_in clntAddr;
	    socklen_t clntAddrLen = sizeof(clntAddr);

        // Wait for a client to connect
        clntSock = accept(servSock, (struct sockaddr *) &clntAddr, &clntAddrLen);
        if (clntSock < 0) {
            perror("accept() failed");
            retVal = FAILURE;
            break;
        }

        char clntIpAddr[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &clntAddr.sin_addr.s_addr, clntIpAddr, sizeof(clntIpAddr)) == NULL)
        {
            LOG_MESSAGE("Unable to get client IP Address");
            retVal = FAILURE;
            break;
        }
    } while(0);

	return retVal;
}

/* Interprets the Message from the Neighbour */
bool HandleMessage(uint clntSock, uint nodeID, pthread_mutex_t* ptrVectorLock, uint* ptrVectorTime, uint* ptrLUpdate)
{
    bool retVal = SUCCESS;
	// Receive data
	char message[BUFSIZE];
	memset(message, 0, BUFSIZE);

    do
    {
        ssize_t recvLen = recv(clntSock, message, BUFSIZE, 0);
        if (recvLen < 0)
        {
            perror("recv() failed");
            retVal = FAILURE;
            break;
        }
        else if(recvLen == 0)
        {
            retVal = FAILURE;
            break;
        }
        message[recvLen] = '\0';
        
        // Update the Vector Clock
        serverThreadUpdateClock(message, nodeID, ptrVectorLock, ptrVectorTime, ptrLUpdate);

    } while(0);

	return retVal;
}



/* Client Thread Functions */

bool sendMessageToNbr(uint nodeID, uint threadID, uint nbrID,
                      pthread_mutex_t* ptrThreadQueueLock, std::queue<char*>* ptrMessageQueue,
                      pthread_mutex_t* ptrVectorLock, uint *ptrVectorTime,
                      uint* ptrLSent, uint* ptrLUpdate)
{
    char *msg = (char*)malloc(BUFSIZE);
    char tempMsg[BUFSIZE] = {0}, tempVector[BUFSIZE] = {0};
    uint event;

    // Increment Local Clock
    pthread_mutex_lock(ptrVectorLock);
    event = ++ptrVectorTime[nodeID];

    memset(tempVector, 0x00, sizeof(tempMsg));
    strVectorClock(tempVector, ptrVectorLock, ptrVectorTime, false);
    // Vector Clock Algorithm
    if(algorithm == VECTOR_CLOCK)
    {
        strcpy(tempMsg, tempVector);
        noEntriesReq += nodes;
    }

    // SK-Optimization Algorithm
    else
    {
        // Update Last Sent and Update
        ptrLSent[nbrID] = ptrVectorTime[nodeID];
        ptrLUpdate[nodeID] = ptrVectorTime[nodeID];
        for(uint i=0;i<nodes;i++)
        {
            if(ptrLSent[i] < ptrLUpdate[i])
            {
                sprintf(tempMsg + strlen(tempMsg), "(%u,%u),", i+1, ptrVectorTime[i]);
                noEntriesReq += 1;
            }
        }
        tempMsg[strlen(tempMsg)-1] = '\0';
    }
    noMsgs += 1;

    pthread_mutex_unlock(ptrVectorLock);

    //Process3 sends message m31 to process2 at 10:02, vc: [0 0 1 0]
    GET_LOCAL_TIME();
    LOG_MESSAGE("Process%u sends message m%u%u to process%u at %s, vc: %s", nodeID+1, nodeID+1, nbrID+1, nbrID+1, strTime, tempVector);

    // Concatenate the message with Node-id
    sprintf(msg,"%u %s", nodeID+1, tempMsg);

    // Push Message in the Client Threads Message Queue
    pthread_mutex_lock(ptrThreadQueueLock);
    ptrMessageQueue->push(msg);
    pthread_mutex_unlock(ptrThreadQueueLock);

    return SUCCESS;
}

void *ClientThreadFunction(void *params)
{
    clientThreadParams* sParams         = (clientThreadParams*)params;
    uint threadID                       = sParams->threadID;
    uint nodeID                         = sParams->nodeID;
    uint nbrID                          = sParams->nbrID;
    std::queue<char*> *ptrThreadQueue   = sParams->ptrThreadQueue;
    pthread_mutex_t* ptrQueueLock       = sParams->ptrQueueLock;
    pthread_mutex_t* ptrVectorLock      = sParams->ptrVectorLock;
    uint*            ptrVectorTime      = sParams->ptrVectorTime;

    char *message;
    uint sockfd;
    if( SUCCESS != ClientConnectToNbr(sockfd, nbrID))
        pthread_exit(NULL);

    do
    {
        // Thread sleeps
        usleep(THREAD_SLEEP_MILLISECONDS * 1000);

        pthread_mutex_lock(ptrQueueLock);
        bool threadEmpty = ptrThreadQueue->empty();
        pthread_mutex_unlock(ptrQueueLock);

        // Check whether Message available to send
        if(!threadEmpty)
        {
            // Mutex Lock for accessing the MessageQueue 
            pthread_mutex_lock(ptrQueueLock);

            message = ptrThreadQueue->front();
            ptrThreadQueue->pop();

            pthread_mutex_unlock(ptrQueueLock);

            size_t messageLen = strlen(message);
            // Send string to server
            ssize_t sentLen = send(sockfd, message, messageLen, 0);
            if (sentLen < 0) {
                perror("send() failed");
            } else if (sentLen != messageLen) {
                perror("send(): sent unexpected number of bytes");
            } else {
                //LOG_MESSAGE("Message Sent...");
                free(message);
            }
        }
    } while(1);

    close(sockfd);
    pthread_exit(NULL);
}

bool ClientConnectToNbr(uint &sockfd, uint nbrPort)
{
    bool retVal = SUCCESS;
    do
    {
        char servIP[64];
        strcpy(servIP, "127.0.0.1");
        in_port_t servPort = PORT_START + nbrPort;

        //Creat a socket
        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd < 0) {
            perror("socket() failed");
            retVal = FAILURE;
            break;
        }

        // Set the server address
        struct sockaddr_in servAddr;
        memset(&servAddr, 0, sizeof(servAddr));
        servAddr.sin_family = AF_INET;
        int err = inet_pton(AF_INET, servIP, &servAddr.sin_addr.s_addr);
        if (err <= 0) {
            perror("inet_pton() failed");
            retVal = FAILURE;
            break;
        }
        servAddr.sin_port = htons(servPort);

        // Connect to server
        if (connect(sockfd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
            perror("connect() failed");
            retVal = FAILURE;
            break;
        }
    } while(0);

    return retVal;
}

/* General Functions */
void strVectorClock(char* msg, pthread_mutex_t* ptrVectorLock, uint* ptrVectorTime, bool lockReq)
{
    if(lockReq)
        pthread_mutex_lock(ptrVectorLock);

    /* Create Message with Vector Clock */
    char temp[64];
    sprintf(msg, "[%u, ", ptrVectorTime[0]);
    for(uint i = 1; i< nodes-1; i++) {
        sprintf(temp, "%u, ", ptrVectorTime[i]);
        strcat(msg, temp); }
    sprintf(temp, "%u]", ptrVectorTime[nodes-1]);
    strcat(msg, temp);

    if(lockReq)
        pthread_mutex_unlock(ptrVectorLock);

    return;
}

eventType getRandEvent(std::vector<uint> &randEvents)
{
    uint randVal, retVal;
    if(!randEvents.size())
    {
        srand (time(NULL));
        for(int i=0;i<ratioDeno;i++)
            randEvents.insert(randEvents.end(), i);
    }

    randVal = rand() % randEvents.size();
    retVal = randEvents[randVal];
    randEvents.erase(randEvents.begin()+randVal);

    return  retVal < ratioNum ? (eventType)INTERNAL : (eventType)SEND_MESSAGE;
}

void calcRatio(char* ratio)
{
    char *ptr = NULL;
    ratioDeno = 1;

    ptr = strtok(ratio,".");
    ratioNum = atoi(ptr);
    ptr = strtok(NULL, ".");

    if(ptr!=NULL)
    {
        char ch;
        ratioDeno = (int)pow(10,strlen(ptr));
        for(uint i = 0; i< strlen(ptr); i++)
        {
            ch = ptr[i];
            ratioNum = ratioNum*(int)pow(10,1);
            ratioNum += ch-48;
        }
    }

    ratioDeno += ratioNum;
    NO_OF_EVENTS_TO_EXECUTE = ratioDeno;
    return;
}
