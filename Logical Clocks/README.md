# Compare Logical Clock algorithms

Performance comparision of 
1. Vector Clock Algorithm 
2. SK-Optimization Logical Clock Algorithm

## Network Topology 
<p align="center"> <img src="https://github.com/MrinalAich/Distributed-Computing/blob/master/Logical%20Clocks/Figures/network-topology.png" width="400" height="300" /> </p>

System Model:
The entire distributed network topology is created by considering each Node as a thread. These node-threads further create server thread and client threads to communicate with their neighbours. Each client thread is connected to a node-neighbour.
Reason for multiple-Client Threads over single-Client Thread:
Single Client Thread : If a message is delayed over an interface, it will block other messages to sent over the other interfaces causing possible bottleneck. Hence, multiple-client thread system model is choosen.

Message Passing:
Node-thread shares a message queue with each of its client-threads. 
The node-thread pushes a control message into the queue of its client-thread which is to be sent to its neighbour. The client thread checks for a message and sends it over the TCP connection.

## Analysis
The graph is plotted by running the code over the two algorithms with different number of nodes across multiple runs.

<p align="center"> <img src="https://github.com/MrinalAich/Distributed-Computing/blob/master/Logical%20Clocks/Figures/ComparisionGraph.png" width="400" height="300" /> </p>

**Algorithm Complexity**

In Vector clock algorithm, the entire vector clock of the sender process is sent. So, the number of entries will be the same and it increases linearly with the number of nodes in the topology.

In SK optimization technique, between successive messages sent to the same process only a few entries of the vector
clock at the sender process are likely to change. Hence, messages contain only those entries of a vector clock that differ
since the last message sent to the same process.

**Storage Space**

Vector clock algorithm – Each nodes maintains a vector clock of the size of the number of nodes in the topology. Hence,
storage space is O(n).

SK optimization technique – In addition to the vector clock, each node maintains two vectors `LastUpdate` and
`LastSent` of size equal to the number of entries in the topology which are used for sending specific entries of the vector
clock. So, the storage space is also O(n).


## Contents
The folder contains the following:
1. `sourceCode.cpp` - C++ source code.
2. `shellScript.sh` - Shell Script to execute the code over multiple iterations.
3. `outputEntries.txt` - The cpp code writes AlgorithmType, #Nodes, #Entries, #Messages for each iteration in this file. 4. `pyScript.py` - Python script to generate graph as per the data in `outputEntries.txt`.
5. `in-params.txt` - Input file for a single-run from the Command Prompt.
6. `ComparisionGraph.png` - Graph as per the problem Statement.
7. `logSKOpt.txt` - Log-file for SK Optimization algorithm of a four-nodes cyclic graph.
8. `logVecClk.txt` - Log-file for Vector Clock algorithm of a four-nodes cyclic graph.
9+. `in-params<#Nodes>.txt` - Input files for executing in shellScript used for generating graph.

**System Model:**
The `sourceCode.cpp` creates the entire distributed network topology by creating each Node as a thread. These node- threads further create `server thread` and `client threads` to communicate with their neighbours. Each client thread is connected to a node-neighbour.
**Message Passing:**
Node-thread shares a message queue with its client-threads.
The node-thread pushes a control message into the queue of its client-thread to be sent to its neighbour.

**Packages required:**

1. gcc version = 4.8.4
2. python 2.7 (to generate graph using script)

**Command Line argument for sourceCode.cpp** :

1. StartPortNum [Mandatory]: Starting port numbers of each server sockets of a node. eg. If StartPortNum = 4000 Node-1 Server Port Num : 4000 + 1 and so on.
2. AlgorithmType [Mandatory]: The type of algorithm the program should execute eg. 0 – Vector Clock Algorithm 1 – SK Optimization
3. FileName [Optional]: Input Filename. [Default : “in-params.txt”] 
4. Order of arguments
./executable_name < StartPortNum> < AlgorithmType> < FileName>

**Command Line argument for `shellScript.sh` :**

1. StartPortNum [Mandatory]: Starting port numbers for the first iteration of `sourceCode.cpp`. eg. ./shellScript.sh <StartPortNum>

**Execution:**
1. Check maximum number of threads per process the system supports: cat /proc/sys/kernel/threads-max 
If more threads are required : echo 31529 > /proc/sys/kernel/threads-max
2. Making the script executable: chmod +x shellScript.sh
3. Compiling the code: g++ sourceCode.cpp -g -lpthread -o sourceCode
4. Executing the Shell Script: ./shellScript.sh <StartPortNum>
5. Executing the cpp file: ./sourceCode <StartPortNum> <AlgorithmType> <FileName>
