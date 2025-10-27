#ifndef PLACER_H
#define PLACER_H

#include <functional>
#include <unordered_map>
#include <vector>

#include "global.h"
#include "opt.h"
#include "placedb.h"

namespace gpl3d {
namespace td {
class TimingManager;
}
}  // namespace gpl3d

class Bin3D
{
 public:
  Bin3D() { init(); }
  POS_3D center;  // 中心
  POS_3D llf;     // 左下前
  POS_3D urb;     // 右上后
  double width;
  double height;
  double thickness;

  double volume;
  double nodeDensity;
  double terminalDensity;
  double baseDensity;

  VECTOR_3D E;  // electric field
  double phi;

  void init()
  {
    center.SetZero();
    llf.SetZero();
    urb.SetZero();
    nodeDensity = 0;
    terminalDensity = 0;
    baseDensity = 0;
    E.SetZero();
    phi = 0;
    volume = 0;
    thickness = 1.0;
    width = 0;
    height = 0;
  }
  double getHeight() { return height; }
  double getWidth() { return width; }
  double getVolume()
  {
    volume = width * height * thickness;
    return width * height * thickness;
  }
  bool inside(const POS_3D& point) const
  {
    return (point.x >= llf.x && point.x <= urb.x && point.y >= llf.y
            && point.y <= urb.y && point.z >= llf.z && point.z <= urb.z);
  }

 private:
};

class ElectricMesh2D
{
 public:
  ElectricMesh2D() = default;
  ElectricMesh2D(POS_2D ll, POS_2D ur, size_t dimX, size_t dimY);
  void clearDensity();
  void addFillerDensity(const CRect& rec, double scale);
  void addModuleDensity(const CRect& rec, double scale);
  void calcElectricField();
  double getBinWidth() const { return binHeight; };
  double getBinHeight() const { return binWidth; };
  double getBinArea() const { return binWidth * binHeight; };
  // first argument of acc is the density value, second value is the acc
  double accumulateModuleDensity(
      function<double(const double, const double)> acc,
      double initial) const;
  VECTOR_2D getElectricForce(const CRect&) const;
  void show() const;

 private:
  POS_2D ll, ur;
  double binWidth, binHeight;
  size_t dimX, dimY;
  // density from movable nodes
  vector<vector<double>> rho_fillers;
  vector<vector<double>> rho_modules;
  vector<vector<VECTOR_2D>> E;
};

class TierPlacer : DifferentiableFunction
{
 public:
  TierPlacer(PlaceDB* db,
             unordered_map<Module*, ModulePosition>&& position,
             double targetDensity = 1.0);
  void setTiming(gpl3d::td::TimingManager* tm, int k_timing);
  vector<double*> getParams();
  vector<double> getGradient();
  void place();
  void plotCurrentPlacement(string name);

 private:
  PlaceDB* db;
  vector<Module*> freeNodes;
  vector<Module*> terminals;
  vector<Module> fillers;
  unordered_map<Module*, ModulePosition> modulePosition;
  unordered_map<Module*, VECTOR_2D> wirelengthGradient;
  unordered_map<Module*, VECTOR_2D> densityGradient;
  vector<int> moduleTypeCount;
  // 添加时钟相关的成员
  vector<vector<Module*>> ffNodesPerTier;           // 按层分别处理FF节点
  unordered_map<Module*, VECTOR_2D> clockGradient;  // 时钟网络的梯度
  unordered_map<Module*, VECTOR_2D>
      clockGradientLastTime;                           // 上一次迭代的时钟梯度
  unordered_map<Module*, VECTOR_2D> clocknetGradient;  // 时钟梯度预处理器
  unordered_map<Module*, VECTOR_2D>
      clockMassGradient;                  // 上一次迭代的时钟梯度预处理器
  bool clockOptimizationEnabled = false;  // 是否启用时钟优化
  void updateClockGradient();
  void addClkNetGradient(const vector<Module*>& ffNodesCurTier);
  void addClkNetGradientMassCenter(const vector<Module*>& ffNodesCurTier);
  double clkWLPerTier(int tierId);
  vector<double> clockWLPerTier;  // 每层的时钟网络线长
  double clkawareTime = 0.0;      // 时钟优化所用时间
  struct WAClkWirelengthGradientCache
  {
    POS_2D pinPos;
    VECTOR_2D expPositive, expNegative;
  };
  void addWAClkWirelengthGradientOneNet(
      const Net& n,
      vector<WAClkWirelengthGradientCache>& cache);
  /*
      statistics
  */
  std::size_t numTiers;
  CRect coreRegion;
  vector<double> movableNodesAreaPerTier;
  vector<int> movableNodesNumberPerTier;
  vector<double> terminalAreaPerTier;
  vector<size_t> fillerNumPerTier;
  gpl3d::td::TimingManager* timing_ = nullptr;
  int k_timing_ = 15;
  double partitionMacroRatioConstraint;

  double targetDensity;
  double lambda;
  VECTOR_2D invertedGamma;
  double globalDensityOverflow;
  double bestGlobalDensityOverflow;
  int bestIteration;
  vector<double> densityOverflowPerTier;
  vector<int> TSVCountPerTier;
  vector<double> TSVareaPerTier;
  void updatePenaltyFactor(double curHpwl, double lastHpwl);
  void updateDensityOverflow();

  bool stopCondition();
  vector<bool> stopConditionPerTier;

  /*
      initialization
  */
  void setCoreRegion();
  void fillerInitialization();
  void meshInitialization();
  void nodeInitialization();
  void penaltyFactorInitialization();
  void gradientInitialization();

  /*
      wirelength
  */
  double totalHPWL();
  double getNetHPWL(const Net& net);
  void updateWirelengthGradient();
  struct WAWirelengthGradientCache
  {
    POS_2D pinPos;
    VECTOR_2D expPositive, expNegative;
  };
  void addWAWirelengthGradientOneNet(const Net& n,
    std::vector<WAWirelengthGradientCache>& cache,
    double net_weight);
  void addLSEWirelengthGradientOneNet(const Net& n);

  /*
      density
  */
  vector<ElectricMesh2D> meshPerTier;
  void updateDensityGradient();

  /*
      utils
  */
  double computeAvgCellArea();

  /*
      output book shelf format
  */
  void outputBookShelfByTier(string suffix, bool plOnly = false);
  void outputPLByTier(size_t tierId);
  void outputNodesByTier(size_t tierId);
  void outputNetsByTier(size_t tierId);
  void outputSCLByTier(size_t tierId);
  void outputAUXByTier(size_t tierId);
  void outputtechByTier(size_t tierId);
  void outputFFsByTier(size_t tierId);

  void finishTierPlacer(int iterCount);

  void legalandDP();
};

#define HPWL_REF 3.5e5  // e5
#define MULTIPLIER_BASE 1.1
#define MULTIPLIER_UPPERBOUND 1.2  // 1.1
#define MULTIPLIER_LOWERBOUND 0.75

// //for big case
// #define MULTIPLIER_BASE 1.1
// #define MULTIPLIER_UPPERBOUND 1.4 //1.1
// #define MULTIPLIER_LOWERBOUND 0.8

class SpacePlacer : DifferentiableFunction
{
 public:
  SpacePlacer(PlaceDB* db, double targetDensity = 1.0);
  void plotCurrentPlacement(std::string figName);
  void placeInitialization();
  vector<double*> getParams();
  vector<double> getGradient();
  unordered_map<Module*, ModulePosition> getPartitionedPosition();
  void place();
  // double TSVsize;

 private:
  PlaceDB* db;
  vector<Module> fillers;
  vector<Module*> freeNodes;
  vector<Module*> terminals;
  vector<Module*> clkFFs;
  unordered_map<Module*, POS_3D> module3DPosition;
  unordered_map<Module*, VECTOR_3D> wirelengthGradient;
  unordered_map<Module*, VECTOR_3D> densityGradient;

  /*
      placement region
  */
  CRect3D coreCube;
  std::vector<std::vector<std::vector<Bin3D>>> bins;
  void setCoreCube();

  /*
      important parameters
  */
  // the lagrange multiplier used in the final cost function
  double lambda;
  // the smooth parameter in wirelengh model
  VECTOR_3D invertedGamma;
  float targetDensity;
  float globalDensityOverflow;

  // freeNodes + filler
  void updatePenaltyFactor(double lastHpwl, double curHpwl);
  bool stopCondition();

  /*
      wirelength related
  */
  double wirelengthScaleZ;
  double totalHPWL();
  double getNetHPWL(const Net& net);
  // calculate the wirelength gradient with respect to  all freenodes.
  // the order of returned gradient should be the same as netpins
  void updateWirelengthGradient();
  void addLSEWirelengthGradientOneNet(const Net& n);
  struct WAWirelengthGradientCache
  {
    POS_3D pinPos;
    VECTOR_3D expPositive, expNegative;
  };
  void addWAWirelengthGradientOneNet(const Net& n,
                                     vector<WAWirelengthGradientCache>& cache);
  /*
      clock related
  */
  void addClkNetGradient();
  void addClkNetGradientMassCenter();
  double clkawareTime = 0.0;  // 时钟优化所用时间

  /*
      density related
  */
  double moduleThickness;
  double freeNodesArea;
  double terminalArea;
  void storeBins(int bx,
                 int by,
                 int bz,
                 double minX,
                 double minY,
                 double minZ,
                 double binWidth,
                 double binHeight,
                 double binThickness);
  double computeAvgCellVolume();
  int nearestPowerOfTwo(int num);
  void updateBinDensity();
  void updateDensityGradient();
  void updateDensityOverflow();
  // Initialization
  void penaltyFactorInitialization();
  void gradientInitialization();
  void fillerInitialization();
  void nodeInitialization();
  void binInitialization();

  /*
      partition
  */
  void InitialPartition(
      std::unordered_map<Module*, ModulePosition>& moduleVector,
      bool isFirstCall = false);
  void preCalculateTSV(
      std::unordered_map<Module*, ModulePosition>& moduleVector);
  void UpdatePartitionDatabase(
      std::unordered_map<Module*, ModulePosition>& moduleVector);
  void SetTSVPosition(
      std::unordered_map<Module*, ModulePosition>& moduleVector);
  void writePlaceInfo();
  void writePartitionedPosition(
      std::unordered_map<Module*, ModulePosition>& partitionedPosition);
  void readPartitionedPosition(
      std::unordered_map<Module*, ModulePosition>& partitionedPosition);
  void partitionTool(int numTiers, int seed);
  vector<int> TSVCountPerTier;
  double partitionMacroRatioConstraint;
};

class TimingDrivenPartitioner
{
 public:
  TimingDrivenPartitioner(PlaceDB& db);

 private:
  double eval_delay_lowerbound();
  PlaceDB& db;
  vector<std::size_t> partition_result;
};

#endif