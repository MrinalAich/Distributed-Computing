#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <chrono>
#include <ctime>
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
#define MAX_NBR_NODES 20
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
    STOP = 3,
    RESUME = 1,
    APPLICATION = 2,
    ACKNOWLEDGE = 4,
    TERMINATION = 5
} eMsgType;

/* Enum: Type of State */
typedef enum e_State {
    PASSIVE = 0,
    ACTIVE = 1
} eState;

/* Pair: For maintaining Source, Destination */
typedef pair<unsigned int, unsigned int> ipair;

typedef struct sReplyAck_t {
    uint nbrId;
    uint srcId, dstId;
} sReplyAck;

/* Pair: For maintaining Child's State */
typedef pair<unsigned int, eState> iStatePair;

/* Parameters */
// Problem statement related params
uint                        nodeId, nodes, minMsgLt, maxMsgLt, interEventTime, maxSent;
uint                        msgSentPhase, msgToBeSentPhase;
ulong                       msgSent = 0;
std::vector<uint>           nbrs;
ulong                       noControlMsgs;
ulong                       noChildCtrlMsgs;
double                      totalRspTime;
ulong                       totalApplMsgs;
std::list<double>           applMsgStartTime[MAX_NBR_NODES];

// Algorithmic params
uint                        parentId, rootId;
std::set<uint>              childrenIdSet;
std::vector<iStatePair>     childState;
ulong                       toRcvAck;
eState                      state;
ulong                       toSendAck;
std::vector<sReplyAck>      replyChildAck;
bool                        termDetectingState;

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
bool checkAllChildrenPassive();
void rootNodeWriteResult();
void startCxtForApplMsg(uint nbrId);
void removeCtxForAppMsg(uint nbrId);
void sendTerminationMsgs();

// Algorithm related Functions
void serverHandleMessage(char* message);
void sendMessage(uint sockfd, uint nbrId, eMsgType msgType, uint src, uint dst, bool receiversAck);
void *ClientThreadFunction(void* params);

// Client Functions
bool ClientConnectToNbr(uint &sockfd, char* nbrAddr, uint nbrPort);

// Server Functions
void *ServerThreadFunction(void* params);
bool HandleMessage(uint clntSock);
bool AcceptTCPConnection(uint servSock, uint &clntSock);


int main(int argc, char *argv[])
{
    uint i,j,k,u,v;
    uint temp, tempPort;
    int rc;
    char tempAddr[IPV4_ADDR_LEN];
    pthread_t ptrServerThread, ptrClientThread;
    char directoryPath[] = "/home/michail/work/comp/Submission/";
    char inParamsFileName[128] = {0}, topologyFileName[128] = {0};

    // Sanity Check
    if(argc < 2)
    {
        printf("Please mention the Node identifier of this process.\n");
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
        LOG_MESSAGE("Mismatch in %s and %s configuration of nodes. Exiting...\n",
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
    uint tempChildId;
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

    // All nodes are initially Activate,
    {
        pthread_mutex_lock(&sharedLock);

        state = ACTIVE;
        msgSentPhase = 0;
        srand (time(NULL));
        msgToBeSentPhase = minMsgLt + (rand() % (maxMsgLt - minMsgLt));
       
        pthread_mutex_unlock(&sharedLock);
    }

    // Mark Children who are active
    for(auto iter1 = childrenIdSet.cbegin(); iter1 != childrenIdSet.cend(); iter1++)
        childState.push_back(make_pair(*iter1, ACTIVE));

    // Initialize Control Message count
    noControlMsgs = 0;
    noChildCtrlMsgs = 0;
    totalRspTime = 0.0;
    totalApplMsgs = 0;
    termDetectingState = true;

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

/* Function to handle Message from Socket */
void serverHandleMessage(char* message)
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

        msgType = (!strncmp(strtokPtr,"A",1)) ? APPLICATION : (!strncmp(strtokPtr,"R",1)) ? RESUME : (!strncmp(strtokPtr,"S",1)) ? STOP : (!strncmp(strtokPtr,"K",1)) ? ACKNOWLEDGE : TERMINATION;

        pthread_mutex_lock(&sharedLock);

        /*
        If Rcvd: STOP Message then
            childState[j]: Passive
            checkForTermination

        Else If Rcvd: APPLICATION Message then
            If state is Passive
                If not root
                    Send RESUME to Parent
                    Incr toSendAck
                    Mark replyChildAck
                Else
                    Send Acknowledge towards child
                If not reached limit then
                    Go Active
                    Send random msgs to Neighbors
            Else
                Send Acknowledge

        Else If Rcvd: RESUME then
            Retreive Src and Dst
            childState[child]: Active
            If state is Passive:
                Go Active
                Mark replyChildAck
                If not root
                    Send RESUME to Parent
                Else
                    Send Acknowledge towards child
            Else
                Send ACKNOWLEDGE towards child

        Else If Rcvd: ACKNOWLEDGE then
            Retreive Src and Dst
            If Message Destination then
                Send ACKNOWLEDGE to sender
                Decr toSendAck
            Else If Message Source then
                Decr toRcvAck
                If termination satisfies then
                    Go passive
            Else If Intermediate Node then
                Send ACKNOWLEDGE towards child
                Remove replyChildAck
                CheckForPassive

        Else If Rcvd: TERMINATION then
            Send TERMINATION to all children

        */

        // If Rcvd: STOP Message
        if(msgType == STOP)
        {
            /*
            If Rcvd: STOP Message then
                childState[j]: Passive
                checkForTermination
            */
            GET_LOCAL_TIME();
            LOG_MESSAGE("%s Node %u receives STOP message from Child %u.", strTime, nodeId, nbrId);

            char tempChar;
            ulong childCtrlMsg, childApplMsg;
            float childRspTime;
            sscanf(strtokPtr, "%c(%lu,%lu,%f)", &tempChar, &childCtrlMsg, &childApplMsg, &childRspTime);
            noChildCtrlMsgs = noChildCtrlMsgs + childCtrlMsg;

            // Mark Child Passive
            for(auto it = childState.cbegin(); it != childState.cend(); it++)
            {
                if(it->first == nbrId)
                {
                    childState.push_back(make_pair(it->first, PASSIVE));
                    childState.erase(it);
                    break;
                }
            }
            
            if(DEBUG)
            {
                LOG_MESSAGE("Updated: |%lu,%f| - |%lu,%f|", totalApplMsgs, totalRspTime, childApplMsg, childRspTime);
            }

            // Update Application Message statistics from the child
            totalApplMsgs += childApplMsg;
            totalRspTime  += childRspTime;

            // Check for Termination
            if( state == PASSIVE && checkAllChildrenPassive() && !toRcvAck && !toSendAck && termDetectingState)
            {
                // Send Stop to Parent
                if( nodeId != rootId )
                {
                    GET_LOCAL_TIME();
                    LOG_MESSAGE("%s Node %u sends STOP message to Parent Node %u.", strTime, nodeId, parentId);

                    sendMessage(nbrSockfd[parentId], parentId, STOP, 0, 0, false);
                    termDetectingState = false;
                }
                else
                {
                    termDetectingState = false;

                    // Announce Termination Detection
                    rootNodeWriteResult();
                    GET_LOCAL_TIME();
                    LOG_MESSAGE("%s Node %u has detected global Termination. Control Msgs:%lu | Avg.Response Time:%f sec.",
                                            strTime, nodeId, noChildCtrlMsgs + noControlMsgs + nodes - 1, totalRspTime/(float)totalApplMsgs);

                    // Send Termination Msg to children
                    sendTerminationMsgs();
                }
            }
        }

        // If Rcvd: APPLICATION Message
        else if(msgType == APPLICATION)
        {
            /*
            Else If Rcvd: APPLICATION Message then
                If state is Passive
                    If not root
                        Send RESUME to Parent
                        Incr toSendAck
                        Mark replyChildAck
                    Else
                        Send Acknowledge towards child

                    If not reached limit then
                        Go Active
                        Send random msgs to Neighbors
                Else
                    Send Acknowledge
            */

            GET_LOCAL_TIME();
            LOG_MESSAGE("%s Node %u receives a message from Node %u.", strTime, nodeId, nbrId);

            // If state is Passive
            if(state == PASSIVE)
            {
                GET_LOCAL_TIME();
                if( nodeId != rootId)
                {
                    LOG_MESSAGE("%s Node %u sends RESUME message(%u->%u) to Parent Node %u.", strTime, nodeId, nbrId, nodeId, parentId);

                    // Send RESUME to Parent
                    sendMessage(nbrSockfd[parentId], parentId, RESUME, nbrId, nodeId, false);

                    // Incr toSendAck
                    toSendAck++;

                    // Mark replyChildAck
                    sReplyAck tempReplyAck = {nbrId, nbrId, nodeId};
                    replyChildAck.push_back(tempReplyAck);

                    // Termination Detecting State
                    termDetectingState = true;
                }
                else
                {
                    LOG_MESSAGE("%s Node %u sends ACKNOWLEDGE message(%u->%u) to Node %u.", strTime, nodeId, nbrId, nodeId, nbrId);

                    // Send ACKNOWLEDGE to Nbr
                    sendMessage(nbrSockfd[nbrId], nbrId, ACKNOWLEDGE, nbrId, nodeId, true);
                }

                // If limit not reached then
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

            // If state is Active
            else if( state == ACTIVE)
            {
                GET_LOCAL_TIME();
                LOG_MESSAGE("%s Node %u sends ACKNOWLEDGE message(%u->%u) to Node %u.", strTime, nodeId, nbrId, nodeId, nbrId);

                // Send ACKNOWLEDGE to Nbr
                sendMessage(nbrSockfd[nbrId], nbrId, ACKNOWLEDGE, nbrId, nodeId, true);
            }
        }

        // If Rcvd: RESUME Message
        else if(msgType == RESUME)
        {
            /*
            Else If Rcvd: RESUME then
                Retreive Src and Dst
                childState[child]: Active
                If state is Passive:
                    Go Active
                    If not root
                        Send RESUME to Parent
                        Mark replyChildAck
                        Incr toSendAck
                    Else
                        Send Acknowledge towards child
                Else
                    Send ACKNOWLEDGE towards child
            */
            uint srcId, dstId;
            char tempChar;
            // Retreive Src and Dst
            sscanf(strtokPtr, "%c(%u,%u)", &tempChar, &srcId, &dstId);

            GET_LOCAL_TIME();
            LOG_MESSAGE("%s Node %u receives RESUME message(%u->%u) from Child Node %u.", strTime, nodeId, srcId, dstId, nbrId);

            // Mark Child Active
            for(auto it = childState.cbegin(); it != childState.cend(); it++)
            {
                if(it->first == nbrId)
                {
                    childState.push_back(make_pair(it->first, ACTIVE));
                    childState.erase(it);
                    break;
                }
            }

            // If state is Passive
            if(state == PASSIVE)
            {
                GET_LOCAL_TIME();

                // If not root
                if(nodeId != rootId)
                {
                    LOG_MESSAGE("%s Node %u sends RESUME message(%u->%u) to Parent Node %u.", strTime, nodeId, srcId, dstId, parentId);

                    // Send RESUME to Parent
                    sendMessage(nbrSockfd[parentId], parentId, RESUME, srcId, dstId, false);
                    
                    // Incr toSendAck
                    toSendAck++;

                    // Mark replyChildAck
                    sReplyAck tempReplyAck = {nbrId, srcId, dstId};
                    replyChildAck.push_back(tempReplyAck);

                    // Termination Detecting State
                    termDetectingState = true;
                }
                else
                {
                    LOG_MESSAGE("%s Node %u sends ACKNOWLEDGE message(%u->%u) to Child Node %u.", strTime, nodeId, srcId, dstId, nbrId);

                    //Send ACKNOWLEDGE towards child
                    sendMessage(nbrSockfd[nbrId], nbrId, ACKNOWLEDGE, srcId, dstId, false);
                }

                // If limit not reached then
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
            // If state is Active
            else if(state == ACTIVE)
            {
                GET_LOCAL_TIME();
                LOG_MESSAGE("%s Node %u sends ACKNOWLEDGE message(%u->%u) to Child Node %u.", strTime, nodeId, srcId, dstId, nbrId);

                //Send ACKNOWLEDGE towards child
                sendMessage(nbrSockfd[nbrId], nbrId, ACKNOWLEDGE, srcId, dstId, false);
            }
        }
        // If Rcvd: TERMINATION then
        else if (msgType == TERMINATION)
        {
            GET_LOCAL_TIME();
            LOG_MESSAGE("%s Node %u receives TERMINATION message from Parent Node %u.", strTime, nodeId, parentId);

            // Send Termination Msg to children
            sendTerminationMsgs();
        }
        // If Rcvd: ACKNOWLEDGE Message
        else if(msgType == ACKNOWLEDGE)
        {
            uint srcId, dstId;
            char tempChar;
            // Retreive Src and Dst
            sscanf(strtokPtr, "%c(%u,%u)", &tempChar, &srcId, &dstId);

            GET_LOCAL_TIME();
            LOG_MESSAGE("%s Node %u receives ACKNOWLEDGE message(%u->%u) from Node %u.", strTime, nodeId, srcId, dstId, nbrId);

            /*
            Else If Rcvd: ACKNOWLEDGE then
                Retreive Src and Dst
                If Message Destination then
                    Send ACKNOWLEDGE to sender
                    Decr toSendAck
                Else If Message Source then
                    If replyChildAck then
                        Check Dst in replyChildAck
                    If Dst not in replyChildAck
                        Remove replyChildAck
                        Send ACKNOWLEDGE towards sender
                    Else
                        Decr toRcvAck
                        If termination satisfies then
                        Go passive
                Else If Intermediate Node then
                    Remove replyChildAck
                    Send ACKNOWLEDGE towards child
                    Check For Passive
            */

            // If Message Destination then
            if (nodeId == dstId)
            {
                GET_LOCAL_TIME();
                LOG_MESSAGE("%s Node %u sends ACKNOWLEDGE message(%u->%u) to Nbr %u.", strTime, nodeId, srcId, dstId, srcId);

                // Send ACKNOWLEDGE to sender
                sendMessage(nbrSockfd[srcId], srcId, ACKNOWLEDGE, srcId, dstId, true);

                // Decr toSendAck
                toSendAck--;
            }

            // Else If Message Source then
            else if(nodeId == srcId)
            {
                /*
                Else If Message Source then
                    If replyChildAck then
                        Check Dst in replyChildAck
                    If Dst not in replyChildAck
                        Remove replyChildAck
                        Send ACKNOWLEDGE towards sender
                    Else
                        Decr toRcvAck
                        If termination satisfies then
                        Go passive
                */

                uint childId = 0;
                // If replyChildAck then
                if(replyChildAck.cbegin() != replyChildAck.cend())
                {
                    // Check Dst in replyChildAck
                    for(auto it = replyChildAck.cbegin(); it != replyChildAck.cend(); it++)
                    {
                        if(it->srcId == srcId && it->dstId == dstId)
                        {
                            childId = it->nbrId;
                            replyChildAck.erase(it);
                            break;
                        }
                    }
                }

                // If Dst not in replyChildAck
                if(childId != 0)
                {
                    GET_LOCAL_TIME();
                    LOG_MESSAGE("%s Node %u sends ACKNOWLEDGE message(%u->%u) to Child %u.", strTime, nodeId, srcId, dstId, childId);

                    // Send ACKNOWLEDGE to sender
                    sendMessage(nbrSockfd[childId], childId, ACKNOWLEDGE, srcId, dstId, true);

                    // Decr toSendAck
                    toSendAck--;
                }
                else
                {
                    // Decr toRcvAck
                    toRcvAck--;

                    // Remove context for the Application Msg
                    removeCtxForAppMsg(nbrId);

                    // Check for Termination
                    if( checkAllChildrenPassive() && !toRcvAck && !toSendAck && termDetectingState)
                    {
                        // Go passive
                        state = PASSIVE;

                        // Send Stop to Parent
                        if( nodeId != rootId )
                        {
                            GET_LOCAL_TIME();
                            LOG_MESSAGE("%s Node %u sends STOP message to Parent Node %u.", strTime, nodeId, parentId);

                            sendMessage(nbrSockfd[parentId], parentId, STOP, 0, 0, false);

                            // Termination Detecting State
                            termDetectingState = false;
                        }
                        else
                        {
                            termDetectingState = false;

                            // Announce Termination Detection
                            rootNodeWriteResult();
                            GET_LOCAL_TIME();
                            LOG_MESSAGE("%s Node %u has detected global Termination. Control Msgs: %lu | Avg.Response Time:%f sec.",
                                                    strTime, nodeId, noChildCtrlMsgs + noControlMsgs + nodes - 1, totalRspTime/(float)totalApplMsgs);

                            // Send Termination Msg to children
                            sendTerminationMsgs();
                        }
                    }    
                }
            }
            // Else If Intermediate Node then
            else if( nodeId != srcId && nodeId != dstId )
            {
                uint childId = 0;
                // Decr toSendAck
                toSendAck--;

                // Remove replyChildAck
                for(auto it = replyChildAck.cbegin(); it != replyChildAck.cend(); it++)
                {
                    if(it->srcId == srcId && it->dstId == dstId)
                    {
                        childId = it->nbrId;
                        replyChildAck.erase(it);
                        break;
                    }
                }

                if(childId == 0)
                {
                    LOG_MESSAGE("replyChildAck not found... Programming Error!!!");
                }

                GET_LOCAL_TIME();
                LOG_MESSAGE("%s Node %u sends ACKNOWLEDGE message(%u->%u) to Child %u.", strTime, nodeId, srcId, dstId, childId);

                // Send ACKNOWLEDGE to child
                sendMessage(nbrSockfd[childId], childId, ACKNOWLEDGE, srcId, dstId, false);

                // Check For Termination
                if( state == PASSIVE && checkAllChildrenPassive() && !toRcvAck && !toSendAck && termDetectingState)
                {
                    // Send Stop to Parent
                    if( nodeId != rootId )
                    {
                        GET_LOCAL_TIME();
                        LOG_MESSAGE("%s Node %u sends STOP message to Parent Node %u.", strTime, nodeId, parentId);

                        sendMessage(nbrSockfd[parentId], parentId, STOP, 0, 0, false);
                        termDetectingState = false;
                    }
                    else
                    {
                        termDetectingState = false;

                        // Announce Termination Detection
                        rootNodeWriteResult();
                        GET_LOCAL_TIME();
                        LOG_MESSAGE("%s Node %u has detected global Termination. Control Msgs: %lu | Avg.Response Time:%f sec.",
                                                strTime, nodeId, noChildCtrlMsgs + noControlMsgs + nodes - 1, totalRspTime/(float)totalApplMsgs);

                        // Send Termination Msg to children
                        sendTerminationMsgs();
                    }
                }
            }            
            else
            {
                LOG_MESSAGE("Programming Error!!!\n");
            }
        }

        pthread_mutex_unlock(&sharedLock);

        strtokPtr = strtok(NULL, "|");
        if(strtokPtr != NULL && DEBUG)
        {
            LOG_MESSAGE("Observed multiple messages in one packet: %s", tempMsg);
        }

    } while(strtokPtr != NULL);

    return;
}

/* Server Function to send Message */
void sendMessage(uint sockfd, uint nbrId, eMsgType msgType, uint srcId, uint dstId, bool receiversAck)
{
    char message[BUFSIZE] = {0};
    memset(message, 0x00, sizeof(message));

    GET_LOCAL_TIME();

    switch (msgType)
    {
    case STOP:
    // Concatenate Node-id with MsgType
        noControlMsgs++;
        sprintf(message,"%u|%c(%lu,%lu,%f)|", nodeId, 'S', noControlMsgs + noChildCtrlMsgs, totalApplMsgs, totalRspTime);
        noChildCtrlMsgs = 0;
        noControlMsgs = 0;
        totalRspTime = 0.0;
        totalApplMsgs = 0;
        break;

    // Concatenate Node-id with MsgType
    case RESUME:
        noControlMsgs++;
        // Mark Source & Destination of this Resume
        sprintf(message,"%u|%c(%u,%u)|", nodeId, 'R', srcId, dstId);
        break;

    // Sends Termination Message to Child
    case TERMINATION:
        noControlMsgs++;
        sprintf(message, "%u|%c|", nodeId, 'T');
        break;

    // Sends Application Message to Neighbor
    case APPLICATION:
        // Concatenate the message with Node-id
        sprintf(message,"%u|A|", nodeId);
        break;
       
    // Sends Acknowledge Message to Neighbor
    case ACKNOWLEDGE:
        if(!receiversAck)
            noControlMsgs++;
        // Mark Source & Destination of this Acknowledge
        sprintf(message,"%u|%c(%u,%u)|", nodeId, 'K', srcId, dstId);
        break;

    default:
        LOG_MESSAGE("Programming Error!!!");
    }

    // Send message to neighbour
    size_t messageLen = strlen(message);
    ssize_t sentLen = send(sockfd, message, messageLen, 0);
    if (sentLen < 0) {
        LOG_MESSAGE("Node %u send failed to %u | Msg: %s", nodeId, nbrId, message);
        printf("At Node %u\n", nodeId);
        perror("send() failed");
    } else if (sentLen != messageLen) {
        perror("send(): sent unexpected number of bytes");
    } else {
        if(DEBUG)
        {
            LOG_MESSAGE("Node-%u: Sent Message: %s to Node: %u", nodeId, message, nbrId);
        }
    }
    return;
}

/* Client Function to send Message to neighbors */
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

    do
    {
        pthread_mutex_lock(&sharedLock);
        GET_LOCAL_TIME();
        LOG_MESSAGE("%s Node State: %s|num_unack_msgs:%lu|num_unsentack_msgs:%lu.", strTime,
                        state == PASSIVE ? "Passive" : "Active",
                        toRcvAck, toSendAck);

        /*      
        If state: Passive then
            If Check children state and No toRcvAck and No toSendAck then
                If not root
                    Send STOP to parent
                Else
                    Announce termination

        Else state: Active then
            Choose any neighbour to send Application Msg
            Incr toRcvAck
            If termination satisfies then
                Go passive
                If Check children state and No toRcvAck and No toSendAck then
                    If node is root
                        Announce Termination detection
                    Else
                        Send Stop to parent
        */

        // Process is Passive
        if(state == PASSIVE)
        {
            // Check for Termination
            if( checkAllChildrenPassive() && !toRcvAck && !toSendAck && termDetectingState)
            {
                // Send Stop to Parent
                if( nodeId != rootId )
                {
                    GET_LOCAL_TIME();
                    LOG_MESSAGE("%s Node %u sends STOP message to Parent Node %u.", strTime, nodeId, parentId);
                    sendMessage(nbrSockfd[parentId], parentId, STOP, 0, 0, false);
                    termDetectingState = false;
                }
                else
                {
                    termDetectingState = false;

                    // Announce Termination Detection
                    rootNodeWriteResult();
                    GET_LOCAL_TIME();
                    LOG_MESSAGE("%s Node %u has detected global Termination. Control Msgs: %lu | Avg.Response Time:%f sec.",
                                            strTime, nodeId, noChildCtrlMsgs + noControlMsgs + nodes - 1, totalRspTime/(float)totalApplMsgs);

                    // Send Termination Msg to children
                    sendTerminationMsgs();
                }
            }
        }
        // Process is Active
        else if(state == ACTIVE)
        {
            GET_LOCAL_TIME();

            // Send message to a random neighbour
            srand(time(NULL));
            uint nbrId = nbrs[rand() % nbrs.size()];
            sendMessage(nbrSockfd[nbrId], nbrId, APPLICATION, 0, 0, false);

            // Maintain context for the Application Msg
            startCxtForApplMsg(nbrId);
            
            // 10:02 Node 5 send a message to Node 9.
            LOG_MESSAGE("%s Node %u sends a message to Node %u.", strTime, nodeId, nbrId);

            msgSentPhase++;
            msgSent++;
            toRcvAck++;

            if(DEBUG) {
                LOG_MESSAGE("This Phase: %u | Total: %lu", msgSentPhase, msgSent);
            }

            // If termination criterion statisfies, go Passive
            if((msgSentPhase == msgToBeSentPhase) || (msgSent == maxSent))
            {
                // Inter-event Time
                usleep(interEventTime);
   
                // 10:30 Node 2 announces termination
                GET_LOCAL_TIME();
                LOG_MESSAGE("%s Node %u announces termination.", strTime, nodeId);

                state = PASSIVE;

                // Check for Termination
                if( checkAllChildrenPassive() && !toRcvAck && !toSendAck && termDetectingState)
                {
                    // Send Stop to Parent
                    if( nodeId != rootId )
                    {
                        GET_LOCAL_TIME();
                        LOG_MESSAGE("%s Node %u sends STOP message to Parent Node %u.", strTime, nodeId, parentId);

                        sendMessage(nbrSockfd[parentId], parentId, STOP, 0, 0, false);
                        termDetectingState = false;
                    }
                    else
                    {
                        termDetectingState = false;

                        // Announce Termination Detection
                        rootNodeWriteResult();
                        GET_LOCAL_TIME();
                        LOG_MESSAGE("%s Node %u has detected global Termination. Control Msgs: %lu | Avg.Response Time:%f sec.",
                                                strTime, nodeId, noChildCtrlMsgs + noControlMsgs + nodes - 1, totalRspTime/(float)totalApplMsgs);

                        // Send Termination Msg to children
                        sendTerminationMsgs();
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

/* Function retreives Node's Parent and Children */
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

/* Function retreives Node's parameters like
   IP Address, Port, Children */
void retreiveNodeParams(uint nodeId, char *fileName, char* ipAddr, uint &port)
{
    uint temp,tempPort;
    char tempIpAddr[IPV4_ADDR_LEN];
    // Read from configuration file
    fflush(stdin);
    if(NULL == freopen(fileName, "r", stdin))
    {
        LOG_MESSAGE("Failed to open file %s. Exiting...", fileName);
        exit(0);
    }
    

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

        // Handle Message as per Algorithm
        serverHandleMessage(message);
    } while(0);

	return retVal;
}

/* Server socket function to accept TCP Connections */
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

/* Server Thread Function */
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

/* Function to connect to its neighbors */
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

/* Functions checks whether all children are Passive or not */
bool checkAllChildrenPassive()
{
    uint cx=0;
    char buff[128] = {0};

    bool retVal = SUCCESS;
    for(auto it = childState.cbegin(); it != childState.cend(); it++)
    {
        if( it->second == ACTIVE )
        {
            cx = snprintf(buff+cx, 128-cx, " %u", it->first);
            retVal = FAILURE;
        }
    }
    // Debugging
    if(DEBUG && cx)
    {
        LOG_MESSAGE("Node %u - Active Nodes: %s", nodeId, buff);
    }
    return retVal;
}

/* Function writes the No. of Control Msgs on 
   Termination detection to the output file */
void rootNodeWriteResult()
{
    FILE *outputFile;
    outputFile = fopen("Output/outputAlgorithm2.txt", "a+");
    if(outputFile == NULL)
    {
        LOG_MESSAGE("Error while opening Output File. Exiting...");
        return;
    }
    // nodes - 1 indicates the no. of Termination Messages
    fprintf(outputFile, "%u %lu %f %lu\n", nodes, noChildCtrlMsgs + noControlMsgs + nodes - 1, totalRspTime/(float)totalApplMsgs, totalApplMsgs);
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

    sprintf(buff, "%lld.%lld", secs.count(), ms.count());

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
    
    // Sanity Check
    if (!applMsgStartTime[nbrId].size())
    {
        GET_LOCAL_TIME();
        LOG_MESSAGE("Programming Error!!!, Received a Ack for an unsent Application msg.");
        return;
    }

    // Assuming random order of received Ack msg.
    // This randomness won't the affect global sum.
    startTime = applMsgStartTime[nbrId].front();
    applMsgStartTime[nbrId].pop_front();
    totalRspTime = totalRspTime + (endTime - startTime);

    return;
}

// Function sends Termination Message to its children
void sendTerminationMsgs()
{
    uint childId;
    for(auto it = childrenIdSet.cbegin(); it != childrenIdSet.cend(); it++)
    {
        childId = *it;

        GET_LOCAL_TIME();
        LOG_MESSAGE("%s Node %u sends TERMINATION message to Child Node %u.", strTime, nodeId, childId);

        //Send TERMINATION to child
        sendMessage(nbrSockfd[childId], childId, TERMINATION, 0, 0, false);
    }

    return;
}
