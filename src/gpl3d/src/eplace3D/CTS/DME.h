#ifndef DME_H
#define DME_H

#include "arghandler.h"
#include "ctsdb.h"
#include "topology.h"
#include "CTSglobal.h"
#include "CTSobjects.h"
#include "global.h"
#include "CTSplot.h"

class ZSTDMERouter
{
public:
    ZSTDMERouter()
    {
        init();
    }
    ZSTDMERouter(CTSDB *_db)
    {
        init();
        ctsdb = _db;
    }

    int sinkCount;  // test 1
    int delayModel; // 0 for linear and 1 for elmore
    int totalLevelCount;

    TreeTopology *topology;

    double totalWirelength;
    VECTOR_2D invertedGamma;      // identical to the invertedGamma in eplace.h
    double globalDensityOverflow; // identical to the globalDensityOverflow(tau) in eplace.h

    vector<Point_2D> treeNodeLocation; // also known as pl in the ZST DME paper by abk
    vector<SteinerPoint *> solution;

    CTSDB *ctsdb;

    void init()
    {
        ctsdb = NULL;
        sinkCount = 0;
        delayModel = -1;

        treeNodeLocation.clear();
        solution.clear();

        topology = NULL;
        totalWirelength = 0;

        invertedGamma.SetZero();
        globalDensityOverflow = 0.0;

        totalLevelCount = 0;
    }

    void setTopology(TreeTopology *_topology)
    {
        topology = _topology;
    }
    void setDelayModel(int _delayModel)
    {
        delayModel = _delayModel;
    }
    // Generate embedding
    void ZSTDME(); // Deferred-Merge Embedding
    void topDown();
    void bottomUp();
    void repairSolution(); // eliminate overlap between clock tree and obstacles, performed after buildSolution. Ignore its impact on skew for now. It will cause higher wirelength, for sure

    void drawBottomUp();
    void drawBottomUpMerge(string name, TRR trr1, TRR trr2, Segment merge);

    double buildSolution();
    void drawSolution();

    void constructVirtualTree();

    VECTOR_2D calculateArborealGradientFor2PinNet_WA(TreeNode *lowerLevelNode, TreeNode *higherLevelNode);

    int countLevel(TreeNode *);
    // double calculateLevelWeight(int);

    void setArborealGradientParameters(VECTOR_2D _invertedGamma, double _globalDensityOverflow) // call this in placer
    {
        setDensityOverflow(_globalDensityOverflow);
        setInvertedGamma(_invertedGamma);
    }

    void setDensityOverflow(double _globalDensityOverflow)
    {
        globalDensityOverflow = _globalDensityOverflow;
    }

    void setInvertedGamma(VECTOR_2D _invertedGamma)
    {
        invertedGamma = _invertedGamma;
    }

    Segment nineRegionBasedFeasibleMergeSegmentCutting(Segment, Segment); // See the paper in README
};

class BSTDMERouter
{
};

#endif