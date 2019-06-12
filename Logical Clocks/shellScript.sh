# Sanity Check
if [ "$#" -ne 1 ]; then
  echo "Please specify the starting port number."
  exit 1
fi


# Remove temporary files, if exists
fileName=("outputEntries.txt")
echo -n "" > $fileName
chmod 777 $fileName

# Execute script on different port numbers, as when the program terminates
# some sockets may not be properly closed hence will effect the next set
# of executions
startPortNum=$1

# To work with different topologies for graph generation, 
# the input topology file should change.
inTopoFileName=""

for algoType in {1,0}
do
    for ((iter=5; iter<=10; iter++))
    do
        inTopoFileName="in-params"$iter".txt"
        for run in {1,2}
        do
            echo "Running: " $algoType $iter $startPortNum $inTopoFileName "Iter: " $run
            ./sourceCode $startPortNum $algoType $inTopoFileName
            startPortNum=`expr $startPortNum + 20`
        done
    done
done

# Python-script to generate graph
python pyScript.py
