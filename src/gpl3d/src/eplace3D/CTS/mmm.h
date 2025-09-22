#ifndef MMM_H
#define MMM_H
#include "ctsdb.h"
#include "CTSglobal.h"
#include "CTSobjects.h"
#include "CTSplot.h"
class MMMNode;
class MMM
{
public:
    MMM()
    {
        Init();
    }
    MMM(CTSDB *_db)
    {
        Init();
        ctsdb = _db;
    }
    void Init()
    {
        ctsdb = NULL;
        sinks.clear();
        mergeNodes.clear();
        invertedGamma.SetZero();
        globalDensityOverflow = 0.0;
        clockWirelength = 0.0;
        totalLevelCount = 0;
        maxToSourceDistance = 0.0; 
    }
    CTSDB *ctsdb;
    vector<MMMNode *> sinks;
    vector<MMMNode *> mergeNodes; // use shared_ptr to avoid memory leakage
    VECTOR_2D invertedGamma;      // identical to the invertedGamma in eplace.h
    double globalDensityOverflow; // identical to the globalDensityOverflow(tau) in eplace.h
    double clockWirelength;
    int totalLevelCount;
    double maxToSourceDistance; // max distance to source from all sinks

    void initSinkNodes();
    void updateSinkLocation();
    MMMNode *MMMmain(vector<MMMNode *> currentSinks, int cutDirection); // cutDirection: 0 for x direction and 1 for y direction
    void doMMM();
    void clearTree();
    void buildTree();
    void drawSolution();
    // void calMaxToSourceDistance(MMMNode *root)
    // {
    //     // maxToSourceDistance = 0.0;
    //     for (MMMNode *curSink : sinks)
    //     {
    //         double curDistance = sqrt(pow(curSink->x - root->x, 2) + pow(curSink->y - root->y, 2));
    //         if (curDistance > maxToSourceDistance)
    //         {
    //             maxToSourceDistance = curDistance;
    //         }
    //     }
    // }

    int countLevel(MMMNode *);

    void accumulateGradients();

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
    VECTOR_2D calculateArborealGradientFor2PinNet_WA(MMMNode *lowerLevelNode, MMMNode *higherLevelNode);
    // double calculateLevelWeight(int);
};

class MMMNode : public Point_2D
{
public:
    MMMNode()
    {
        Init();
    }
    MMMNode(Point_2D point)
    {
        Init();
        x = point.x;
        y = point.y;
    }
    ~MMMNode()
    {
        // cout<<"deleting node: "<<id<<endl;
    }
    void Init()
    {
        id = -1;
        leftChild = NULL;
        rightChild = NULL;
        parent = NULL;
        loadCapacitance = 0.0;
        accumulatedArborealGradient.SetZero();
        level = 0;
        accumulatedPreconditioner = 0.0;
    }

    int id;
    MMMNode *leftChild;
    MMMNode *rightChild;
    MMMNode *parent;
    double loadCapacitance;
    VECTOR_2D accumulatedArborealGradient;
    float accumulatedPreconditioner;
    int level;
};

int partition(vector<double> &, int l, int r);
double getMedian(vector<double> &, int l, int r);

// double accumulateManhattanDistanceToRoot(MMMNode* cur) {
//     if (!cur->parent) {
//         // 到达根节点，距离为0
//         return 0;
//     }
//     double dist = std::abs(cur->x - cur->parent->x) + std::abs(cur->y - cur->parent->y);
//     return dist + accumulateManhattanDistanceToRoot(cur->parent);
// }

#endif