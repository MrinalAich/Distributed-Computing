from collections import defaultdict
import matplotlib.pyplot as plt
import re

VEC_CLK = 0
SK_OPT = 1

# Data-structure
data = dict()
data[VEC_CLK] = {}
data[SK_OPT] = {}

with open("outputEntries.txt") as file:
    for line in file:
        my_regex = re.compile(r"^(?P<algorithm>[0-9]+)\s(?P<nodes>[0-9]+)\s(?P<entries>[0-9]+)\s(?P<messages>[0-9]+)\n")
        match = re.search(my_regex,line)

        algo = int(match.group('algorithm'))
        nodes = int(match.group('nodes'))
        
        if nodes not in data[algo]:
            data[algo].setdefault(nodes, {})
            data[algo][nodes]['entries'] = 0
            data[algo][nodes]['iter'] = 0
            data[algo][nodes]['avg'] = []
            data[algo][nodes]['messages'] = 0

        data[algo][nodes]['entries'] = data[algo][nodes]['entries'] + int(match.group('entries'))
        data[algo][nodes]['messages'] = data[algo][nodes]['messages'] + int(match.group('messages'))
        data[algo][nodes]['iter'] = data[algo][nodes]['iter'] + 1
        data[algo][nodes]['avg'].append(float(data[algo][nodes]['entries']) / float(data[algo][nodes]['messages']))

# wrt SK Optimization
skEntries = []
skNodes = []
for key, value in data[SK_OPT].items():
    skNodes.append(key)
    sumIter = 0.0
    for item in value['avg']:
        sumIter = sumIter + item
    skEntries.append(float(sumIter)/float(value['iter']))

# wrt Vector Clock
vcEntries = []
vcNodes = []
for key, value in data[VEC_CLK].items():
    vcNodes.append(key)
    sumIter = 0
    for item in value['avg']:
        sumIter = sumIter + item
    vcEntries.append(sumIter/value['iter'])

#print str(skNodes) + " | " + str(skEntries)
#print str(vcNodes) + " | " + str(vcEntries)

# Plot generation
legene_r, = plt.plot(vcNodes, vcEntries, 'r', label='VectorClock')
legene_b, = plt.plot(skNodes, skEntries, 'b', label='SK Optimization')

plt.xlabel("No. of Nodes")
plt.ylabel("Avg. No. of Entries per Message")
plt.xlim([4,11])
plt.legend(handles=[legene_r, legene_b], loc='upper center', bbox_to_anchor=(0.5, 1.05), ncol=2, fontsize=10)
    
plotName = "ComparisionGraph"
try: # Remove file, if already exists
    os.remove(plotName + ".png")
except:
    pass
plt.savefig(plotName + ".png")
plt.close()
