import os, matplotlib.pyplot as plt

fileName1 = "outputAlgorithm1"
fileName2 = "outputAlgorithm2"

class valObj(object):
     iterCount = 0
     totalCtrlMsg = 0

     def __init__(self, iterCount, totalCtrlMsg, totalRspTime):
        self.iterCount = iterCount
        self.totalCtrlMsg  = totalCtrlMsg
        self.totalRspTime  = totalRspTime

# Read data from file
def readData(filename):
    dct = {}
    with open(filename + ".txt", 'rt') as file:
        for line in file:
            line    = line.strip().replace("\n",'')
            values  = line.split(' ')
            iter    = int(values[0])
            ctrlVal = int(values[1])
            # Convert response time to milliseconds
            rspTime = float(values[2].rstrip('\n')) * 1000

            if iter in dct:
                dct[iter].iterCount = dct[iter].iterCount + 1
                dct[iter].totalCtrlMsg = dct[iter].totalCtrlMsg + int(ctrlVal)
                dct[iter].totalRspTime = dct[iter].totalRspTime + float(rspTime)
            else:
                dct[iter] = valObj(1, ctrlVal, rspTime)
    return dct

def plotGraph(dct1, dct2, yLabel, plotName):

    xValues = []
    yValuesAlgo1 = []
    yValuesAlgo2 = []

    # Create lists for plot-function
    for key1,val1 in dct1.items():
        for key2,val2 in dct2.items():
            if key1 == key2:
                xValues.append(key1)
                yValuesAlgo1.append(val1)
                yValuesAlgo2.append(val2)

    plt.xlabel("Number of nodes")
    plt.ylabel(yLabel)

    maxYLim = max( max(yValuesAlgo1), max(yValuesAlgo2))
    plt.ylim([0, maxYLim * 1.2])
    plt.xlim([4,max(xValues) + 1])

    legene_p, = plt.plot(xValues, yValuesAlgo1, 'r', label='Algorithm-1')
    legene_r, = plt.plot(xValues, yValuesAlgo2, 'g', label='Algorithm-2')

    plt.legend(handles=[legene_p, legene_r], loc='upper center', bbox_to_anchor=(0.5, 1.05), ncol=2)

    try: # Remove file, if already exists
        os.remove(plotName + ".png")
    except:
        pass
    plt.savefig(plotName + ".png")
    plt.close()

# Average the values
def averagData(dct):
    dctCtrl = {}
    dctRspTime = {}

    for key,val in dct.items():
        avgCtrlMsg = float(val.totalCtrlMsg)/float(val.iterCount)
        avgRspTime = float(val.totalRspTime)/float(val.iterCount)

        dctCtrl[key]    = avgCtrlMsg
        dctRspTime[key] = avgRspTime

    return dctCtrl,dctRspTime

# Main-function
def main():
    
    # Read data for Algorithm - 1
    dct1 = readData(fileName1)
    dct1CtrlMsg, dct1RspTime = averagData(dct1)

    # Read data for Algorithm - 2
    dct2 = readData(fileName2)
    dct2CtrlMsg, dct2RspTime = averagData(dct2)

    # Plot Control Messages Graph
    plotGraph(dct1CtrlMsg, dct2CtrlMsg, "Average No. of Control Messages per node", "ControlMsgComplexity")

    #Plot Response Time Graph
    plotGraph(dct1RspTime, dct2RspTime, "Average Response Time per Node (in ms)", "ResponseTimeComplexity")

if __name__ == "__main__": main()

