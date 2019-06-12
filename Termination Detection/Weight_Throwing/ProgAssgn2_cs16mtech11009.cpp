/* 
 * Author : Mrinal Aich
 * Algorithm: Weight throwing based Termination Detection
 */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <ctime>
#include <vector>
#include <set>
#include <queue>
#include <map>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#define uint unsigned int
#define ulong unsigned long
#define MAX_NUM_NBRS 12
#define INITIAL_WEIGHT 1073741824.00000
#define IPV4_ADDR_LEN 20

#define SUCCESS 1
#define FAILURE 0

#define WHITE 0
#define BLACK 1

#define DEBUG 0
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

/* Data Structures */
/* Parameters of Neighbours */ 
typedef struct neighbourParams {
    char ipAddr[IPV4_ADDR_LEN];
    uint port;
} nbrParams;

/* Enum: Type of Message */
typedef enum e_msgType {
    APPLICATION = 0,
    CONTROL = 1
} eMsgType;

/* Enum: Type of State */
typedef enum e_State {
    PASSIVE = 0,
    ACTIVE = 1
} eState;

/* Parameters */
// Problem statement related params
uint                        nodeId, nodes, minMsgLt, maxMsgLt, interEventTime, maxSent;
uint                        parentId, coordinatorId;
uint                        msgSentPhase, msgToBeSentPhase;
std::vector<uint>           nbrs;
double                      weight = 0.0;
double                      finalWt = 0.0;

// Algorithmic params
eState                      state;
ulong                       noControlMsgs;
ulong                       msgSent;

// Socket/IP Layer related params
char                        selfAddr[IPV4_ADDR_LEN];
uint                        selfPort;
std::map<uint,uint>         nbrSockfd;
std::map<uint, nbrParams>   nbrMap;

// System related params
pthread_mutex_t             sharedLock, fileLock;
FILE*                       logFile;
char                        logFileName[25]; 

/* Function Protoypes */
// General Functions
void retreiveNodeParams(uint nodeId, char *fileName, char* ipAddr, uint &port);
void serverUpdateWeight(char* message);
void sendMessage(uint sockfd, uint nbrId, bool controlMsg, bool forwardControlMsg, char* msgToForward);
uint retrieveNodesParent(char *fileName);

// Client Functions
void *ClientThreadFunction(void *threadParams);
bool ClientConnectToNbr(uint &sockfd, char* nbrAddr, uint nbrPort);

// Server Functions
void *ServerThreadFunction(void *ptrParams);
bool HandleMessage(uint clntSock);
bool AcceptTCPConnection(uint servSock, uint &clntSock);

int main(int argc, char *argv[])
{
    uint i,j,k;
    uint u,v,temp, tempPort;
    int rc;
    char tempAddr[IPV4_ADDR_LEN], tempBuf[BUFSIZE];
    pthread_t ptrServerThread, ptrClientThread;

    // Sanity Check
    if(argc < 2)
    {
        printf("Please mention the Node identifier of this process.\n");
        return 0;
    }

    // Node - Identifier
    nodeId = atoi(argv[1]);
    printf("Node-Id: %u\n", nodeId);

    // Log File Name
    sprintf(logFileName, "logFile_%u.txt", nodeId);

    // Restart Log-file
    logFile = fopen(logFileName, "w");
    fprintf(logFile, " ");
    fclose(logFile);

    // Input Parameters FileName
    if (argc == 3)
        freopen((char*)argv[2], "r", stdin);
    else
        freopen("in-params.txt", "r", stdin);

    scanf("%u %u %u %u %u\n", &nodes, &minMsgLt, &maxMsgLt, &interEventTime, &maxSent);
    scanf("%u", &coordinatorId);

    // Get number of active nodes
    std::set<uint> activeNodes;
    while(scanf("%u", &j) != EOF)
        activeNodes.insert(j);

    memset(tempBuf, 0x00, sizeof(tempBuf));
    uint cx = 0;
    for(auto it = activeNodes.cbegin(); it != activeNodes.cend(); it++)
    {
        if(cx >= 0 && cx < BUFSIZE)
            cx += snprintf(tempBuf+cx, BUFSIZE-cx, " %u", *it);
    }

    //Initially nodes 2 5 7 8 are active.
    LOG_MESSAGE("Initially node(s)%s are active...", tempBuf);

    // Input Node-Parameters
    if (argc == 4)
        freopen((char*)argv[3], "r", stdin);
    else
        freopen("topology.txt", "r", stdin);

    scanf("%u", &temp);
    
    // Sanity Check
    if(temp != nodes)
    {
        LOG_MESSAGE("Mismatch in %s and %s configuration of nodes. Exiting...\n",
                    argc == 3 ? (char*)argv[2] : "in-params.txt",
                    argc == 4 ? (char*)argv[3] : "topology.txt");
        return 0;
    }

    // Retrieve self node's IP Address and Port
    retreiveNodeParams(nodeId, argc == 4 ? (char*)argv[3] : (char*)"topology.txt", selfAddr, selfPort);

    // Retrieve Neighbours
    char tempBuff[BUFSIZE];
    do
    {
        scanf("%u", &temp);
        memset(tempBuff, 0x00, sizeof(tempBuff));
        scanf("%[^\n]", tempBuff); 

        if(temp == nodeId)
        {
            char *tempPtr;
            tempPtr = strtok(tempBuff, " ");
            while(tempPtr != NULL)
            {
                nbrs.push_back(atoi(tempPtr));
                tempPtr = strtok(NULL, " ");
            }
            break;
        }
    } while(true);
    fflush(stdin);

    // Get Node's parent in the Spanning Tree
    // with coordinator as the root
    if(coordinatorId != nodeId)
        parentId = retrieveNodesParent(argc == 4 ? (char*)argv[3] : (char*)"topology.txt");
    else
        parentId = nodeId;

    if(DEBUG)
    {
        LOG_MESSAGE("Self - IP: %s | Port: %u\nParent: %u", selfAddr, selfPort, parentId);
    }

    // Retreive Node's Parameters
    uint nbrId;
    for(auto it=nbrs.cbegin(); it != nbrs.cend(); it++)
    {
        nbrId = *it;
        retreiveNodeParams(*it, argc == 4 ? (char*)argv[3] : (char*)"topology.txt", tempAddr, tempPort);
        fflush(stdin);
        strcpy(nbrMap[nbrId].ipAddr, tempAddr);
        nbrMap[nbrId].port = tempPort;
    }

    // Initialize Global Locks
    pthread_mutex_init(&fileLock,0);
    pthread_mutex_init(&sharedLock,0);

    // Initialize the Coordinator Node
    if(coordinatorId == nodeId)
    {
        pthread_mutex_lock(&sharedLock);
        finalWt = INITIAL_WEIGHT * activeNodes.size();
        pthread_mutex_unlock(&sharedLock);
    }

    // Initialize the Active Nodes
    if(activeNodes.find(nodeId) != activeNodes.end())
    {
        pthread_mutex_lock(&sharedLock);
        state = ACTIVE;
        weight = INITIAL_WEIGHT;

        msgSentPhase = 0;
        srand (time(NULL));
        msgToBeSentPhase = minMsgLt + (rand() % (maxMsgLt - minMsgLt));

        pthread_mutex_unlock(&sharedLock);
    }

    // Create Server Thread
    rc = pthread_create(&ptrServerThread, NULL, ServerThreadFunction, NULL);
    if (rc)
    {
        LOG_MESSAGE("Error:unable to create Server thread");
    }

    // Delay added for Server Sockets to get ready
    sleep(3);

    // Create Client Thread
    rc = pthread_create(&ptrClientThread, NULL, ClientThreadFunction, NULL);
    if(rc)
    {
        LOG_MESSAGE("Error:unable to %d create Client thread ", rc);
    }

    pthread_join(ptrServerThread, NULL);
    pthread_join(ptrClientThread, NULL);

    // Cleanup Memory
    pthread_mutex_destroy(&fileLock);
    pthread_mutex_destroy(&sharedLock);

    return 0;
}

void serverUpdateWeight(char* message)
{
    uint nbrId, temp;
    double rcvdWt;
    eMsgType msgType;
    char *strtokPtr = NULL;

    char tempMsg[BUFSIZE] = {0};
    strcpy(tempMsg, message);

    strtokPtr = strtok(message, "|");

    do
    {
        // Retreive - NbrId    
        nbrId = atoi(strtokPtr);
    
        // Retreive - Type of message
        strtokPtr = strtok(NULL, "|");
        temp = atoi(strtokPtr);

        msgType = (temp == 0) ? APPLICATION : CONTROL;

        // Retreive - Weight in the message
        strtokPtr = strtok(NULL, "|");
        rcvdWt = atof(strtokPtr);

        pthread_mutex_lock(&sharedLock);

        if(msgType == CONTROL && coordinatorId != nodeId)
        {
            char tempMsg[BUFSIZE] = {0};
            sprintf(tempMsg, "%u|%u|%lf|", nbrId, temp, rcvdWt);

            // Forward the message to its parent
            sendMessage(nbrSockfd[parentId], parentId, false, true, tempMsg);
        }
        else // if(msgType == APPLICATION) or Control Message destined to itself
        {
            GET_LOCAL_TIME();

            LOG_MESSAGE("%s Node %u receives a message from Node %u.", strTime, nodeId, nbrId);

            // Update Weight of the node
            weight = weight + rcvdWt;
            if(DEBUG) {
                LOG_MESSAGE("Updated Weight: %lf | Added: %lf", weight, rcvdWt);
            }

            // Activate Process,if it does not
            // meet the termination criterion
            if(state == PASSIVE && msgSent != maxSent)
            {
                state = ACTIVE;
                msgSentPhase = 0;

                srand (time(NULL));
                msgToBeSentPhase = (rand() % (maxMsgLt - minMsgLt)) + minMsgLt;

                LOG_MESSAGE("%s Node %u becomes active.", strTime, nodeId);
            }

            // Check for termination detection
            if(nodeId == coordinatorId)
            {
                if(state == PASSIVE && ((weight >= (finalWt - 0.2)) && (weight <= (finalWt + 0.2))))
                {
                    LOG_MESSAGE("%s Node %u has detected termination.", strTime, nodeId);
                }
            }
        }
        pthread_mutex_unlock(&sharedLock);

        strtokPtr = strtok(NULL, "|");
        if(strtokPtr != NULL && DEBUG)
        {
            LOG_MESSAGE("Observed multiple messages in one packet: %s.", tempMsg);
        }

    } while(strtokPtr != NULL);

    return;
}

void sendMessage(uint sockfd, uint nbrId, bool controlMsg, bool forwardControlMsg, char* msgToForward)
{
    char message[BUFSIZE] = {0};
    double sendWt = 0.0;
    GET_LOCAL_TIME();

    memset(message, 0x00, sizeof(message));

    // Send Control Message to Coordinator
    if(controlMsg)
    {
        sendWt = weight;
        weight = 0.0;
        // 10:30 Node 2 announces termination
        LOG_MESSAGE("%s Node %u announces termination.", strTime, nodeId);

        // Concatenate the message with Node-id
        sprintf(message,"%u|%u|%lf|", nodeId, (uint)controlMsg, sendWt);
        noControlMsgs++;
    }
    // Forward a received control message to Coordinator
    else if(forwardControlMsg)
    {
        strcpy(message, msgToForward);
        noControlMsgs++;
    }
    // Sends Application Message to Neighbor
    else
    {
        sendWt = weight/2;
        weight /= 2;
        // 10:02 Node 5 send a message to Node 9.
        LOG_MESSAGE("%s Node %u sends a message to Node %u.", strTime, nodeId, nbrId);

        // Concatenate the message with Node-id
        sprintf(message,"%u|%u|%lf|", nodeId, (uint)controlMsg, sendWt);
    }

    // Send message to neighbour
    size_t messageLen = strlen(message);
    ssize_t sentLen = send(sockfd, message, messageLen, 0);
    if (sentLen < 0) {
        perror("send() failed");
    } else if (sentLen != messageLen) {
        perror("send(): sent unexpected number of bytes");
    } else {
        if(DEBUG) {
            LOG_MESSAGE("Weight sent: %lf | Message: %s", sendWt, message);
        }
    }
    return;
}

void retreiveNodeParams(uint nodeId, char *fileName, char* ipAddr, uint &port)
{
    uint temp,tempPort;
    char tempIpAddr[IPV4_ADDR_LEN];
    // Read from configuration file
    fflush(stdin);
    freopen(fileName, "r", stdin);
    scanf("%u\n", &temp);

    // Retrieve node's IP Address
    for(uint i=0;i<nodes;i++)
    {
        memset(tempIpAddr, 0x00, sizeof(tempIpAddr));
        // Format : 1 - 127.0.0.1:3333
        scanf("%u - %15[^:]:%u\n", &temp, tempIpAddr, &tempPort);

        if(temp == nodeId)
        {
            strcpy(ipAddr, tempIpAddr);
            port = tempPort;
        }
    }
    return;
}

bool AcceptTCPConnection(uint servSock, uint &clntSock) 
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

void *ClientThreadFunction(void *params)
{
    char *message, nbrAddr[IPV4_ADDR_LEN];
    uint sockfd;
    uint nbrId, nbrPort;
    nbrSockfd.clear();

    for(auto it=nbrMap.cbegin(); it!=nbrMap.cend(); it++)
    {
        nbrId = it->first;
        strcpy(nbrAddr, (char*)it->second.ipAddr);
        nbrPort = it->second.port;

        if( SUCCESS != ClientConnectToNbr(sockfd, nbrAddr, nbrPort))
        {
            LOG_MESSAGE("Unable to connect to %u at %s:%u.", nbrId, nbrAddr, nbrPort);
            pthread_exit(NULL);
        }
        else
        {
            if(DEBUG) {
                LOG_MESSAGE("Connected to %u at %s:%u. Going to sleep.", nbrId, nbrAddr, nbrPort);
            }
            nbrSockfd[nbrId] = sockfd;
        }
    }

    sleep(5);

    do
    {
        pthread_mutex_lock(&sharedLock);

        GET_LOCAL_TIME();
        LOG_MESSAGE("%s State: %s and weight: %lf | Control Msgs Sent: %lu",
                            strTime, state == PASSIVE ? "Passive" : "Active", weight, noControlMsgs);

        // If the process remains in Passive state, then
        // it informs the coordinator about its new weight
        if(state == PASSIVE && weight != 0.0)
        {
            // Check for termination detection
            if(nodeId == coordinatorId)
            {
                if(state == PASSIVE && ((weight >= (finalWt - 0.2)) && (weight <= (finalWt + 0.2))))
                {
                    GET_LOCAL_TIME();
                    LOG_MESSAGE("%s Node %u has detected termination.", strTime, nodeId);
                }
            }
            else
                sendMessage(nbrSockfd[parentId], coordinatorId, true, false, NULL);
        }
        else if(state == ACTIVE)
        {
            // Send message to a random neighbour
            srand(time(NULL));
            uint nbrId = nbrs[rand() % nbrs.size()];
            sendMessage(nbrSockfd[nbrId], nbrId, false, false, NULL);

            msgSentPhase++;
            msgSent++;
            if(DEBUG) {
                LOG_MESSAGE("This Phase: %u | Total: %lu", msgSentPhase, msgSent);
            }

            // If termination criterion statisfies, go Passive
            if((msgSentPhase == msgToBeSentPhase) || (msgSent == maxSent))
            {
                // Inter-event Time
                usleep(interEventTime);

                state = PASSIVE;
                // Check for termination detection
                if(nodeId == coordinatorId)
                {
                    if(state == PASSIVE && ((weight >= (finalWt - 0.2)) || (weight <= (finalWt + 0.2))))
                    {
                        GET_LOCAL_TIME();
                        LOG_MESSAGE("%s Node %u has detected termination.", strTime, nodeId);
                    }
                }
                else
                {
                    sendMessage(nbrSockfd[parentId], coordinatorId, true, false, NULL);
                }
            }
        }
        pthread_mutex_unlock(&sharedLock);

        // Inter-event Time
        usleep(interEventTime);

    } while(true);

    close(sockfd);
    pthread_exit(NULL);
}

bool ClientConnectToNbr(uint &sockfd, char* nbrAddr, uint nbrPort)
{
    bool retVal = SUCCESS;
    do
    {
        char servIP[64];
        strcpy(servIP, nbrAddr);
        in_port_t servPort = nbrPort;

        if(DEBUG) {
            LOG_MESSAGE("Connecting to IP: %s | Port: %u", servIP, servPort);
        }

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
        while(connect(sockfd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) 
        {

        }
    } while(0);

    return retVal;
}

void *ServerThreadFunction(void *params)
{
    uint servSock;
    char servIP[64];
    strcpy(servIP, selfAddr);
    in_port_t servPort = selfPort;

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
					if (SUCCESS != AcceptTCPConnection(servSock, newClntSock))
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
					if( SUCCESS != HandleMessage(currSock))
						FD_CLR(currSock, &orgSockSet);
				}
            }
        }

    } while(true);

	int closingSock;
	for (closingSock = 0; closingSock < maxDescriptor + 1; closingSock++)
		close(closingSock);

    pthread_exit(NULL);
}

/* Interprets the Message from the Neighbour */
bool HandleMessage(uint clntSock)
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
        if(DEBUG) {
            LOG_MESSAGE("Node-%u: Received Message - %s", nodeId, message);
        }

        // Update Self-Weight
        serverUpdateWeight(message);
    } while(0);

	return retVal;
}

uint retrieveNodesParent(char *fileName)
{
#define MAX_NODES 100
    uint i,temp,nodes,u,v;
    char buffer[BUFSIZE], nextCh;
    std::vector<uint> graph[MAX_NODES];

    // Read from configuration file
    fflush(stdin);
    freopen(fileName, "r", stdin);
    scanf("%u\n", &nodes);

    for(i=0;i<nodes;i++)
    {
        // Ignore the IP addresses
        scanf("%[^\n]\n", buffer);
        //LOG_MESSAGE("Read: %s", buffer);
    }

    // Input Topology Graph
    char *tempPtr,tempBuffer[BUFSIZE];
    for(i=0;i<nodes;i++)
    {
        scanf("%u ", &u);
        memset(tempBuffer, 0x00, sizeof(tempBuffer));
        scanf("%[^\n]\n", tempBuffer);

        tempPtr = strtok(tempBuffer, " ");
        while(tempPtr!=NULL)
        {
            v = atoi(tempPtr);
            graph[u].push_back(v);
            tempPtr = strtok(NULL, " \n");
        }
    }

    // Create Spanning Tree with the coordinator node
    // as the root - BFS
    uint color[MAX_NODES] = {WHITE};
    std::queue<uint> q;
    q.push(coordinatorId);
    color[coordinatorId] = BLACK;

    while(!q.empty())
    {
        u = q.front();
        q.pop();

        for(auto it = graph[u].cbegin(); it != graph[u].cend(); it++)
        {
            v = *it;
            if(color[v] == WHITE)
            {
                q.push(v);
                color[v] = BLACK;
                if(v == nodeId)
                    return u;
            }
        }
    }
    fflush(stdin);
    return 0;
}