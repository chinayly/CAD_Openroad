#ifndef FFCLUSTER_H
#define FFCLUSTER_H
#include "placedb.h"
#include "topology.h"
// implementing best choice cluster according to ntuplace to cluster flip-flops(FFs) to build clock tree topology based on only netlist, see the code of ntuplaceMRT
class ClusterFFNet;

class ClusterFF
{
public:
    ClusterFF()
    {
        Init();
    }
    void Init()
    {
        clusterTreeNodes.clear();
        Nets.clear();
    }
    void initFFDBfromPlaceDBandCTSDB(PlaceDB *, CTSDB *);

    void clustering(); // build TreeTopology through
    void updateBestMateForEveryNode();
    void updateBestMate(int nodeIndex);

    vector<ClusterFFNode> clusterTreeNodes;
    vector<ClusterFFNet> Nets;
};

class ClusterFFNode // quite like the clusterModule in ntuplace
{
public:
    ClusterFFNode()
    {
        Init();
    }
    void Init()
    {
        id = -1;
        merged = false;
        bestMateID = -1;
        bestMateCost = -1;
    }
    int id;
    set<int> connectedNets; // store net id(net->idx)
    TreeNode *corresbondingTreeNode;
    bool merged;
    int bestMateID;
    double bestMateCost;
};

class ClusterFFNet
{
public:
    ClusterFFNet()
    {
    }
    void Init()
    {
    }
    int id;
    set<int> FFindex; //! the index of an FF is the index of that FF in the vector PlaceDB->dbFFS, which equals its id as a ClusterFFNode
    int netSize;      //! netSize include nonFF modules but since we conduct clustering only on FFs, so nonFF modules(id) are not stored in the net(we only have set<int> FFindex here), because we don't care who they are
};
#endif