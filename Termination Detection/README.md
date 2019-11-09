Implementing Distributed Termination Detection Algorithms

1. Huang’s weight-throwing algorithm
2. Spanning Tree based token algorithm

Implement both these algorithms in C++

The Application - 
Implement a distributed system consisting of n nodes, numbered 0 to n-1, arranged in a certain topology (e.g., ring, mesh etc.).
All channels in the system are bidirectional, reliable and satisfy the first-in-first-out (FIFO) property.
A reliable socket connection (TCP or SCTP) is used to implement a channel. 

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
