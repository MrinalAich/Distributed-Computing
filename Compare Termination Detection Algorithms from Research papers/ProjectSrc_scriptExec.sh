# Algorithm source Codes
algorithm1="ProjectSrc-cs16mtech11006_11009_Algo1"
algorithm2="ProjectSrc-cs16mtech11006_11009_Algo2"

# Remove temporary files, if exists
fileName=("Output/outputAlgorithm1.txt" "Output/outputAlgorithm2.txt")
fileIndex=0
length=${#fileName[@]}
while [ $fileIndex -lt $length ]
do
    if [ -f ${fileName[$fileIndex]} ] ; then
        rm -rf ${fileName[$fileIndex]}
    fi
    echo -n "" > ${fileName[$fileIndex]}
    chmod 777 ${fileName[$fileIndex]}
    fileIndex=`expr $fileIndex + 1`    
done

# Force Stop all processes, if running
for PROCESS_EXEC_CMD in $algorithm2 $algorithm1
do
    killall -9 $PROCESS_EXEC_CMD
done

#sleep 2m

# Compiling the Source Codes
if [ $# -eq 2 ];  then
    if [ $2 -eq '1' ]; then
        echo "Compiling Source Codes"
        g++ -g -std=c++11 $algorithm1.cpp -lpthread -o $algorithm1
        g++ -g -std=c++11 $algorithm2.cpp -lpthread -o $algorithm2
    fi
fi


# No. of iterations
noOfIterations=5

if [ $# -eq 0 ];  then
    echo "No arguments supplied. Taking defaults!!!"
    maxNodes=5
else
    maxNodes=$1
fi


# Execute the algorithms
for PROCESS_EXEC_CMD in $algorithm1
do
    for nodeNum in $(seq 5 $maxNodes)
    do
        for iter in $(seq 1 $noOfIterations)
        do
            for nodeId in $(seq 1 $nodeNum)
            do
                echo "$PROCESS_EXEC_CMD - Running: " NodeId: $nodeId Of Total:$nodeNum " | Iter: " $iter
                eval './$PROCESS_EXEC_CMD $nodeId $nodeNum &'
            done
            sleep 4m

            # Force Stop all executables of the code...
            killall -9 $PROCESS_EXEC_CMD
            echo "Going to sleep. Check process"
            sleep 2m
        done
    done
done

# Plot Comparision Graph
python plotGraph.py
