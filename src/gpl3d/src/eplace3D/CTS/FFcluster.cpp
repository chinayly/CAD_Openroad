#include "FFcluster.h"

struct bestAffinityCompare // non-decreasing or non increasing order????? its different for sorting and for priority queue(min/max heap)
{
    bool operator()(const double &s1, const double &s2) const
    {
        return s1 > s2;
    }
};

void ClusterFF::initFFDBfromPlaceDBandCTSDB(PlaceDB *, CTSDB *)
{
}

void ClusterFF::clustering()
{
    int unmergedNodeCount = clusterTreeNodes.size();

    int leafCount = clusterTreeNodes.size();
    cout << "leafCount: " << leafCount << endl;
    // treeNodes is the S, after every merge merged, the merge node is added to S
    // vector<NngNodePair> currentMinimumPairs;

    int unmergedNodeCount = clusterTreeNodes.size();

    multimap<double, int, bestAffinityCompare> Q; //! int for node ID, double for the max affinity of the node with that ID with any other node
    multimap<double, int, bestAffinityCompare>::iterator Qiter;
    // Step 1: calculated best mate for all nodes and add them to Q
    while (unmergedNodeCount > 1)
    {
        updateBestMateForEveryNode();//need to 'enlarge the search grid', or we may not be able to get a final tree
        //! choose pair with the highest affinity in Q, merge the node with its best mate
        while (!Q.empty())
        {
            Qiter=Q.begin();
            Q.erase(Qiter);
            
            int masterID=Qiter->second; 
            int targetMateID = clusterTreeNodes[Qiter->second].bestMateID;
            
            if(clusterTreeNodes[masterID].merged||clusterTreeNodes[targetMateID].merged)

            TreeNode *mergeNode = new TreeNode(globalTreeNodes.size()); //!

            // TRR merge, remember to update delay and capacitane
            // capacitance of treeNodes are updated in DME

            mergeNode->leftChild = globalTreeNodes[best_pair.from];
            mergeNode->rightChild = globalTreeNodes[best_pair.to];

            TRRBasedMerge(mergeNode, mergeNode->leftChild, mergeNode->rightChild);

            globalTreeNodes[best_pair.from]->parent = mergeNode;
            globalTreeNodes[best_pair.to]->parent = mergeNode;

            globalTreeNodes.emplace_back(mergeNode);

            globalMerged.emplace_back(false);
            globalMerged[best_pair.from] = true;
            globalMerged[best_pair.to] = true;
            // cout<<"merging "<<best_pair.from<<" "<<best_pair.to<<endl;

            unmergedNodeCount--;

            if (unmergedNodeCount > 1) // avoid bug when we reach the root
            {
                //! push the nearest neighbor of the mergedNode to the priority queue
                double cost = numeric_limits<double>::max();
                int to = -1;
                for (int i = 0; i < globalTreeNodes.size(); i++)
                {
                    assert(globalTreeNodes[i]->id == i);
                    if (!globalMerged[i])
                    {

                        if ((i != mergeNode->id))
                        {
                            double newCost = nearestNeighborPairCost(globalTreeNodes[i], mergeNode);
                            if (newCost < cost)
                            {
                                cost = newCost;
                                to = i;
                            }
                        }
                        assert(to != -1);
                    }
                }
                assert(globalTreeNodes[to]->id == to);
                globalMinimumPairs.push(NngNodePair(mergeNode->id, globalTreeNodes[to]->id, cost));
            }
        }
    }

    int tempsize = 0;
    root = globalTreeNodes.back(); //! the last node in treeNodes should be the root node.
    std::function<void(TreeNode *)> preOrderTraversal = [&](TreeNode *curNode)
    {
        if (curNode != nullptr)
        {
            int curId = curNode->id;
            preOrderTraversal(curNode->leftChild);
            preOrderTraversal(curNode->rightChild);
            tempsize++;
            // cout << "preing " << curId << endl;
        }
    };
    preOrderTraversal(root);
    assert(tempsize == globalTreeNodes.size());
    nodeCount = globalTreeNodes.size();
}

void ClusterFF::updateBestMateForEveryNode()
{
    for (ClusterFFNode curTreeNode : clusterTreeNodes)
    {
        updateBestMate(curTreeNode.id);
    }
}

void ClusterFF::updateBestMate(int nodeIndex) // find best mate for clusterTreeNodes[nodeIndex]
{
}
