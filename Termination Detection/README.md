# Comparing Distributed Termination Detection Algorithms

1. Huang’s weight-throwing algorithm
2. Spanning Tree based token algorithm

The Application - 
Implement a distributed system consisting of n nodes, numbered 0 to n-1, arranged in a certain topology (e.g., ring, mesh etc.).
All channels in the system are bidirectional, reliable and satisfy the first-in-first-out (FIFO) property.
A reliable socket connection (TCP or SCTP) is used to implement a channel. 

## Application
All nodes execute the following protocol:

1. Initially, each node (also denoted as a process) in the system is either active or passive. This is
specified in the input configuration file.

2. When a node Ni is active, it sends a certain number of messages randomly chosen between
minMsgLt and maxMsgLt to its neighbors. Suppose Ni has k neighbors. Then it randomly
chooses a neighbor out of k and then sends a message. Ni sends messages with a delay that is
exponentially distributed with an average delay. After sending M messages to its neighbors
where M is randomly chosen to be such that minMsgLt <= M <= maxMsgLt, Ni becomes passive.
An active node on receiving the message does not do anything.

3. Only an active node can send a message.

4. A passive node, on receiving a message, becomes active and follows the steps mentioned in Step 2.
But to ensure that the computation eventually terminates, we add one extra rule: Once a node Ni
has sent maxSent, it can’t become active on the receipt of any new message.
Also ensure that all the messages sent by each node is logged onto a local buffer along with the
time-stamp in the format: “Time: Message sent by Ni to Nj ”.

We refer to the protocol described above as Random Multicast or RM protocol.

## Network Topology 
<p align="center"> <img src="https://github.com/MrinalAich/Distributed-Computing/blob/master/Termination%20Detection/Figures/network-topology.jpg" width="400" height="300" /> </p>

## Analysis
<p align="center"> <img src="https://github.com/MrinalAich/Distributed-Computing/blob/master/Termination%20Detection/Figures/comaprison-figure.jpeg" width="400" height="300" /> </p>

### Weight throwing algorithm:
As the number of nodes increases in the network, it has higher message complexity as each node sends its final weight (after termination) to the coordinator node, independently. The control messages are merely forwarded by the intermediate nodes. 
(Note: The constraint that the network topology is a complete graph is relaxed in the problem statement, hence control messages are being routed towards the coordinator node).

### Spanning Tree algorithm:
Each nodes sends its termination information/token only to its parent. The parent on accumulating tokens from its children, sends its own token to its parent. Thus, less messages are in transit. 

### Anomalies:
For lesser number of nodes, the message complexities are comparable possibly due to the randomness or specific nature of the graph/network topology.

### Working Snapshot
The algorithms was tested in Microsoft Azure Virtual Machines. The following snapshot shows the working on the application.
<p align="center"> <img src="https://github.com/MrinalAich/Distributed-Computing/blob/master/Termination%20Detection/Figures/azure_remote_VM_at_microsoft_cloud_logFile.png" width="700" height="400" /> </p>

