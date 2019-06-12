/* 
 * Author : Jonathan Marbaniang
 * Algorithm : A More Efficient Message-Optimal Algorithm for Distributed
Termination Detection.

 */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <ctime>
#include <chrono>
#include <vector>
#include <queue>
#include <set>
#include <list>
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

#define SUCCESS 1
#define FAILURE 0

#define DEBUG 0

#define BUFSIZE 256
#define IPV4_ADDR_LEN 20
#define THREAD_SLEEP_MILLISECONDS 100
#define MAX_POSSIBLE_NEIGHBOURS 50
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
    APPLICATION = 2,
    START = 3,
    FINISHED = 1
} eMsgType;

/* Enum: Type of State */
typedef enum e_State {
    PASSIVE = 0,
    ACTIVE = 1
} eState;


/* Parameters */
// Problem statement related params
uint                        nodeId, nodes, minMsgLt, maxMsgLt, interEventTime, maxSent;
uint                        msgSentPhase, msgToBeSentPhase;
ulong                       msgSent = 0;
std::vector<uint>           nbrs;
ulong                       noControlMsgs;
ulong                       noChildCtrlMsgs;

// Algorithmic params
uint                        parentId, rootId;
std::set<uint>              childrenIdSet;
uint                        maxHopCount;
bool                        termDetectingState;
eState                      state;
uint                        in[MAX_POSSIBLE_NEIGHBOURS];//n neigbours
uint                        out=0;
uint                        k;
double                      totalRspTime,childTime;
ulong                       totalApplMsgs,childApplMsgs;
std::list<double>           applMsgStartTime[MAX_POSSIBLE_NEIGHBOURS];

// Socket/IP Layer related params
char                        selfAddr[IPV4_ADDR_LEN];
uint                        selfPort;
std::map<uint,nbrParams>    nbrMap;
std::map<uint,uint>         nbrSockfd;

// System related params
pthread_mutex_t             sharedLock, fileLock;
FILE*                       logFile;
char                        logFileName[75]; 


/* Function Protoypes */
// General Functions
void retreiveNodeParams(uint nodeId, char *fileName, char* ipAddr, uint &port);
bool retreiveNodeRelatives();
void serverUpdateToken(char* message);
void sendMessage(uint sockfd, uint nbrId, bool startMsg, bool finishedMsg);
void rootNodeWriteResult();
void startCxtForApplMsg(uint nbrId);
void removeCtxForAppMsg(uint nbrId);

// Client Functions
void *ClientThreadFunction(void* params);
bool ClientConnectToNbr(uint &sockfd, char* nbrAddr, uint nbrPort);

// Server Functions
void *ServerThreadFunction(void* params);
bool HandleMessage(uint clntSock);
bool AcceptTCPConnection(uint servSock, uint &clntSock);

//Algorithm Specific Functions
void respond_minor();
void respond_major();

int main(int argc, char *argv[])
{
    uint i,j,k,u,v;
    uint temp, tempPort;
    int rc;
    char tempAddr[IPV4_ADDR_LEN];
    pthread_t ptrServerThread, ptrClientThread;
    char directoryPath[]="/home/michail/work/comp/Submission/";
    bool initActive = false;
    char inParamsFileName[128] = {0}, topologyFileName[128] = {0};

    // Sanity Check
    if(argc < 2)
    {
        printf("Please mention the Node identifier of this process.");
        return 0;
    }

    nodeId = atoi(argv[1]);

    // Log File Name
    sprintf(logFileName, "%s/logs/logFile_%u.txt", directoryPath, nodeId);

    // Restart Log-file
    logFile = fopen(logFileName, "w");
    fprintf(logFile, " ");
    fclose(logFile);

    // Node - Identifier
    LOG_MESSAGE("Node-Id: %u", nodeId);

    // Input Paramters Path
    sprintf(inParamsFileName, "%sInput/in-params%s.txt", directoryPath, argc == 3 ? (char*)argv[2] : "");
    sprintf(topologyFileName, "%sInput/topology%s.txt", directoryPath, argc == 3 ? (char*)argv[2] : "");

    // Input Parameters FileName
    if( NULL == freopen(inParamsFileName, "r", stdin))
    {
        LOG_MESSAGE("Failed to open file %s. Exiting...", inParamsFileName);
        exit(0);
    }

    scanf("%u %u %u %u %u\n", &nodes, &minMsgLt, &maxMsgLt, &interEventTime, &maxSent);
    scanf("%u\n", &rootId);

    // Input Node-Parameters
    if( NULL == freopen(topologyFileName, "r", stdin))
    {
        LOG_MESSAGE("Failed to open file %s. Exiting...", topologyFileName);
        exit(0);
    }

    scanf("%u", &temp);
    
    // Sanity Check
    if(temp != nodes)
    {
        LOG_MESSAGE("Mismatch in %s and %s configuration of nodes. Exiting...",
                    inParamsFileName, topologyFileName);
        return 0;
    }

    // Retrieve self node's IP Address and Port
    retreiveNodeParams(nodeId, topologyFileName, selfAddr, selfPort);

    if(DEBUG) {
        LOG_MESSAGE("Self - IP: %s | Port: %u", selfAddr, selfPort);
    }
    
    // Retrieve Neighbours
    char tempBuff[BUFSIZE];
    for(i=0;i<nodes;i++)
    {
        scanf("%u ", &temp);
        memset(tempBuff, 0x00, sizeof(tempBuff));
        scanf("%[^\n]\n", tempBuff);

        if(temp == nodeId)
        {
            char *tempPtr = strtok(tempBuff, " ");
            while(tempPtr != NULL)
            {
                nbrs.push_back(atoi(tempPtr));
                tempPtr = strtok(NULL, " ");
            }
        }
    }

    // Retreive Spanning Tree's Parent and Children
    if( SUCCESS != retreiveNodeRelatives())
    {
        LOG_MESSAGE("Incorrect Spanning Tree in configuration File.");
        exit(1);
    }
    else
    {
        char buf[BUFSIZE] = {0}, temp[BUFSIZE] = {0};
        for(auto it = childrenIdSet.cbegin(); it != childrenIdSet.cend(); it++)
        {
            memset(temp, 0x00, sizeof(temp));
            sprintf(temp, "%u ", *it);
            strcat(buf, temp);
        }

        // If Leaf node
        if(childrenIdSet.size() == 0)
        {
            childrenIdSet.clear();
        }
        if(DEBUG)
        {
            LOG_MESSAGE("Parent: %u | Children: %s", parentId, buf);
        }
    }
    fflush(stdin);

    // Retreive Node's Parameters
    uint nbrId;
    for(auto it=nbrs.cbegin(); it != nbrs.cend(); it++)
    {
        nbrId = *it;
        retreiveNodeParams(*it, topologyFileName, tempAddr, tempPort);
        fflush(stdin);
        strcpy(nbrMap[nbrId].ipAddr, tempAddr);
        nbrMap[nbrId].port = tempPort;
    }

    // Initialize Global Locks
    pthread_mutex_init(&fileLock,0);
    pthread_mutex_init(&sharedLock,0);

    termDetectingState = false;

    // Activate Node, if configured
    if(initActive == true)
    {
        pthread_mutex_lock(&sharedLock);

        state = ACTIVE;
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

void serverUpdateToken(char* message)
{
    uint nbrId;
    eMsgType msgType;
    char *strtokPtr = NULL, tempMsg[BUFSIZE] = {0};
    strcpy(tempMsg, message);

    strtokPtr = strtok(message, "|");
    do
    {
        // Retreive - NbrId    
        nbrId = atoi(strtokPtr);
    
        // Retreive - Type of message
        strtokPtr = strtok(NULL, "|");

        msgType = (!strncmp(strtokPtr,"A",1)) ? APPLICATION : (!strncmp(strtokPtr,"S",1)) ? START : FINISHED;

        pthread_mutex_lock(&sharedLock);

        /*
        if Rcvd:FINISHED
            Retrieve 'k'
            decrement 'k' from 'out'
            if in detecting mode AND PASSIVE
                call respond_major();

        else if Rcvd:START
            switch to detecting mode
            send START message to each child
            if PASSIVE
                call respond_minor();
                call respond_major();
                
        else if Rcvd:APPLICATION
            increment in[sender]
            if parent is NULL AND I'm not root
                set sender as parent
        */

        // If Rcvd:FINISHED Message
        if(msgType == FINISHED)
        {
            // Remove context for the Application Msg
            removeCtxForAppMsg(nbrId);

            char tempCh;
            uint hopCount = 0;
            ulong rcvdCtrlMsg = 0;
            sscanf(strtokPtr, "%c(%u,%lu,%lf,%lu)", &tempCh, &hopCount, &rcvdCtrlMsg, &childTime, &childApplMsgs);

            if(hopCount > maxHopCount)
                maxHopCount = hopCount;

            noChildCtrlMsgs += rcvdCtrlMsg;
            totalRspTime+=childTime;
            totalApplMsgs+=childApplMsgs;

            strtokPtr = strtok(NULL, "|");

            //Retrieve 'k'
            k=atoi(strtokPtr);

            GET_LOCAL_TIME();
            LOG_MESSAGE("%s Node %u receives FINISHED %u from Node %u at depth: %u.", 
                strTime, nodeId,k, nbrId, hopCount);

            //decrement 'k' from 'out'
            out=out-k;


            if(DEBUG)
            {
				GET_LOCAL_TIME();
	            LOG_MESSAGE("%s Node %u Response Time: %lf.\n Node %u Total Response Time: %lf", 
    	            strTime, nbrId, childTime, nodeId,totalRspTime);            	
            }

            //if in detecting mode AND PASSIVE
            if((state == PASSIVE)&&(termDetectingState == true))
            {
                //call respond_major();
                respond_major();
            }
        }
        // If Rcvd:START Message
        else if(msgType == START)
        {
        	GET_LOCAL_TIME();
            LOG_MESSAGE("%s Node %u receives START message from Node %u.", 
                strTime, nodeId, nbrId);

            //switch to detecting mode
            termDetectingState = true;
            maxHopCount = 0;
            noChildCtrlMsgs = 0;
            
            //increment in[sender]
            in[nbrId]= in[nbrId]+1;

            //Go active
            state=ACTIVE;

            // Send START Signal to children
            for(auto it = childrenIdSet.cbegin(); it != childrenIdSet.cend(); it++)
            {
                nbrId = *it;
                sendMessage(nbrSockfd[nbrId], nbrId, true, false);
                out=out+1;
            }

            //if PASSIVE
            if(state==PASSIVE)
            {
                //call respond_minor();
                respond_minor();
                //call respond_major();
                respond_major();
            }

        }
        // If: Application Message
        else if(msgType == APPLICATION)
        {
            GET_LOCAL_TIME();
            LOG_MESSAGE("%s Node %u receives a message from Node %u.", strTime, nodeId, nbrId);

            //increment in[sender]
            in[nbrId]= in[nbrId]+1;

            //if parent is NULL AND not root            
            if ((nodeId!=rootId)&&(parentId== 0))
            {
                //set sender as parent
                parentId=nbrId;
                nbrSockfd[parentId]=nbrSockfd[nbrId];
            }

            // If state: Passive
            if(state == PASSIVE)
            {
                // If not reached limit then
                if(msgSent != maxSent)
                {
                    // Go Active
                    state = ACTIVE;
                    msgSentPhase = 0;
                    srand (time(NULL));
                    msgToBeSentPhase = minMsgLt + (rand() % (maxMsgLt - minMsgLt));

                    // 10:03 Node 4 becomes active.
                    GET_LOCAL_TIME();
                    LOG_MESSAGE("%s Node %u becomes active.", strTime, nodeId);
                }
            }
        }

        pthread_mutex_unlock(&sharedLock);

        strtokPtr = strtok(NULL, "|");
        if(strtokPtr != NULL && DEBUG)
        {
            LOG_MESSAGE("Observed multiple messages in one packet: %s. Have to handle it!!!", tempMsg);
        }

    } while(strtokPtr != NULL);

    return;
}

void sendMessage(uint sockfd, uint nbrId, bool startMsg, bool finishedMsg)
{
    char message[BUFSIZE] = {0};
    memset(message, 0x00, sizeof(message));

    GET_LOCAL_TIME();
    uint depth=0;

    // Send Token Message to Parent
    if(startMsg)
    {
        // Concatenate Node-id with MsgType
        //uint depth = 0;
        /* If leaf node: then depth = 1
           Else: depth = child(depth) + 1
        */  
        if(childrenIdSet.size() != 0)
            depth = maxHopCount;

        noControlMsgs++;
        sprintf(message,"%u|S|", nodeId);
        GET_LOCAL_TIME();
        LOG_MESSAGE("%s Node %u sent a START message to Node %u.", strTime, nodeId, nbrId);
    }
    else if(finishedMsg)
    {
        // Concatenate Node-id with MsgType
        noControlMsgs++;
        sprintf(message,"%u|%c(%u,%lu,%lf,%lu)|%u|", nodeId, 'F', depth + 1, noControlMsgs + noChildCtrlMsgs, totalRspTime, totalApplMsgs,in[nbrId]);
        noControlMsgs=0;//Reset the Control messages counter
        noChildCtrlMsgs=0;
        totalRspTime=0;//Reset the Response time counter
        totalApplMsgs=0;
        in[nbrId]=0;
    }
    // Sends Application Message to Neighbor
    else
    {
        // Concatenate the message with Node-id
        sprintf(message,"%u|A|", nodeId);
        out=out+1;
    }

    //GET_LOCAL_TIME();
    //LOG_MESSAGE("%s Neighbor sockfd: %u.", strTime, sockfd);

    // Send message to neighbour
    size_t messageLen = strlen(message);
    ssize_t sentLen = send(sockfd, message, messageLen, 0);
    if (sentLen < 0) {
        perror("send() failed");
    } else if (sentLen != messageLen) {
        perror("send(): sent unexpected number of bytes");
    } else {
        if(DEBUG)
        {
            LOG_MESSAGE("Message: %s", message);
        }
    }
    return;
}

void *ClientThreadFunction(void* params)
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

    LOG_MESSAGE("Connected to all neighbours.");
    sleep(5);

    /*
    if root AND want to initiate 'termination detection'
		Enter 'termination detection' mode
		Send START Signal to children

		if PASSIVE
			call respond_minor();
			call respond_major();
    */

    //if root AND want to initiate 'termination detection'
    if(nodeId==rootId)
    {
        //Enter 'termination detection' mode
        termDetectingState=true;

        // Send START Signal to children
        for(auto it = childrenIdSet.cbegin(); it != childrenIdSet.cend(); it++)
        {
            nbrId = *it;
            sendMessage(nbrSockfd[nbrId], nbrId, true, false);
            out=out+1;
        }

        //if PASSIVE
        if(state==PASSIVE)
        {
            //call respond_minor();
            respond_minor();
            //call respond_major();
            respond_major();
        }
    }

    do
    {
        pthread_mutex_lock(&sharedLock);
        GET_LOCAL_TIME();
        LOG_MESSAGE("%s State: %s and %sDetecting.", strTime,
                        state == PASSIVE ? "Passive" : "Active",
                        termDetectingState == false && nodeId == rootId ? "Non-" : "");

        // Process is Passive
        if(state == PASSIVE)
        {
            if(termDetectingState == true)
            {
                // If Root
                if(nodeId == rootId)
                {
                    //call respond_minor();
                    respond_minor();
                    //call respond_major();
                    respond_major();
                }
                // If not Root
                else
                {
                    //call respond_minor();
                    respond_minor();
                    //call respond_major();
                    respond_major();
                }
            }
        }
        else // Process is Active
        {
            GET_LOCAL_TIME();

            // Send message to a random neighbour
            srand(time(NULL));
            uint nbrId = 0;
            uint sock=0;
            while(sock==0)
            {
            	nbrId = nbrs[rand() % nbrs.size()];
            	sock = nbrSockfd[nbrId];
            }


            sendMessage(nbrSockfd[nbrId], nbrId, false, false);

            // Maintain context for the Application Msg
            startCxtForApplMsg(nbrId);
            
            // 10:02 Node 5 send a message to Node 9.
            LOG_MESSAGE("%s Node %u sends a message to Node %u.", strTime, nodeId, nbrId);

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
                
                //Go Passive
                state = PASSIVE;
                LOG_MESSAGE("%s Node %u has turned Passive.", strTime, nodeId);

                if(termDetectingState == true)
                {
                    //If Root
                    if(nodeId == rootId)
                    {
                        //call respond_minor();
                        respond_minor();
                        //call respond_major();
                        respond_major();
                    }
                    else
                    {
                        GET_LOCAL_TIME();
                        //call respond_minor();
                        respond_minor();
                        //call respond_major();
                        respond_major();
                    }
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

bool retreiveNodeRelatives()
{
    uint tempParent, tempChild;
    char tempBuffer[BUFSIZE] = {0}, *strtokPtr;
    childrenIdSet.clear();

    while(scanf("%[^\n\EOF]\n", tempBuffer) != EOF)
    {
        strtokPtr = strtok(tempBuffer, " ");
        tempParent = atoi(strtokPtr);

        strtokPtr = strtok(NULL, " ");
        // Retreive info about its children
        if(tempParent == nodeId)
        {
            while(strtokPtr != NULL)
            {
                childrenIdSet.insert(atoi(strtokPtr));
                strtokPtr = strtok(NULL, " ");
            }
        }
        // Check if this is the parent
        else 
        {
            while(strtokPtr != NULL)
            {
                if(atoi(strtokPtr) == nodeId)
                {
                    if(parentId != 0)
                        return FAILURE;
                    else
                        parentId = tempParent;
                }
                strtokPtr = strtok(NULL, " ");
            }
        }
    }

    // Root is parent to itself
    if(nodeId == rootId)
        parentId = nodeId;

    return (parentId != 0) ? SUCCESS : FAILURE;
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

        // Update Token and its State
        serverUpdateToken(message);
    } while(0);

	return retVal;
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

void *ServerThreadFunction(void* params)
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

/*Function to send acknowledgements to all neighbours 
once a node has gone passive and is in detecting state*/
void respond_minor()
{
    uint nbrId;
    //Send acknowledgements for all received Application messages except to the parent
    for(auto it=nbrMap.cbegin(); it!=nbrMap.cend(); it++)
    {
        nbrId = it->first;
        if((nbrId!=parentId)&&(in[nbrId]!=0))
        {
        	if(nbrSockfd[nbrId]>0)
            {
                GET_LOCAL_TIME();
                LOG_MESSAGE("%s Node %u sent FINISHED message to Node %u",strTime,nodeId,nbrId);
	            sendMessage(nbrSockfd[nbrId], nbrId, false, true);
            }
        }
    }
}

/*Function to send acknowledgement to the parent
once a node has received acknowledgemnts for all its application messages*/
void respond_major()
{
    GET_LOCAL_TIME();
    LOG_MESSAGE("%s Node State: num_unack_msgs: %u",strTime,out);

    //All acknowledgements have been received
    if(out==0)
    {
        //If Root
        if (nodeId==rootId)
        { 
            //Announce Termination
            rootNodeWriteResult();
            GET_LOCAL_TIME();
            LOG_MESSAGE("%s Node %u has detected global Termination. Control Msgs: %lu. | Avg.Response Time: %lf sec.",
                                        strTime, nodeId, noChildCtrlMsgs + noControlMsgs, totalRspTime/(float)totalApplMsgs);
            termDetectingState = false;
        }
        //If not Root
        else if(parentId!=0)
        {
            //send acknowledgement to the parent
            GET_LOCAL_TIME();
            LOG_MESSAGE("%s Node %u sent FINISHED message to Parent Node %u",strTime,nodeId,parentId);
            sendMessage(nbrSockfd[parentId], parentId, false, true);

            //Reset the parent node
            parentId=0;
        }
	}
}

/* Function writes the No. of Control Msgs on 
   Termination detection to the output file */
void rootNodeWriteResult()
{
    FILE *outputFile;
    outputFile = fopen("Output/outputAlgorithm1.txt", "a+");
    if(outputFile == NULL)
    {
        LOG_MESSAGE("Error while opening Output File. Exiting...");
        return;
    }
    fprintf(outputFile, "%u %lu %f\n", nodes, noChildCtrlMsgs + noControlMsgs, totalRspTime/totalApplMsgs);
    fclose(outputFile);

    return;
}

// Function returns the time in secs.ms since the start of the day
float getTimeSecsDotMs()
{
    using namespace std::chrono;

    auto now = system_clock::now();
    char buff[64] ={0};

    // tt stores time in seconds since epoch
    std::time_t tt = system_clock::to_time_t(now);

    // broken time as of now
    std::tm bt = *std::localtime(&tt);

    // alter broken time to the beginning of today
    bt.tm_hour = 0;
    bt.tm_min = 0;
    bt.tm_sec = 0;

    // convert broken time back into std::time_t
    tt = std::mktime(&bt);

    // start of today in system_clock units
    auto start_of_today = system_clock::from_time_t(tt);

    // today's duration in system clock units
    auto length_of_today = now - start_of_today;

    // seconds since start of today
    seconds secs = duration_cast<seconds>(length_of_today); // whole seconds

    // milliseconds since start of today
    milliseconds ms = duration_cast<milliseconds>(length_of_today);

    // subtract the number of seconds from the number of milliseconds
    // to get the current millisecond
    ms -= secs;

    sprintf(buff, "%lld.%lld", (long long int)secs.count(), (long long int)ms.count());

    return strtod((char*)buff, NULL);
}


// Function maintains the context for Appl Msg, 
// to calculate response time on receiving its Ack
void startCxtForApplMsg(uint nbrId)
{
    float startTime = getTimeSecsDotMs();
    applMsgStartTime[nbrId].push_back(startTime);

    // Incr the no. of application message sent since
    // the last STOP sent to the parent.
    // This includes the APPLICATION messages sent by both
    // the Node and its children(if any)
    totalApplMsgs++;

    return;
}
    

// Function removes context of Appl Msg,
// it calculates the response time
void removeCtxForAppMsg(uint nbrId)
{
    float startTime, endTime;
    endTime = getTimeSecsDotMs();
    

    // One FINISHED message for all Sent Application Messages

    while(!applMsgStartTime[nbrId].empty())
    {
        startTime = applMsgStartTime[nbrId].front();
        applMsgStartTime[nbrId].pop_front();
        totalRspTime = totalRspTime + (endTime - startTime);
    }

    return;
}
