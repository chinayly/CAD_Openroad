#include "gpl3d/Placer3d.h"

#include "DME.h"
#include "ctsdb.h"
#include "gpl3d/parser_odb.h"
#include "mmm.h"
#include "odbStats.h"
#include "par/PartitionMgr.h"
#include "placedb.h"
#include "placer.h"
#include "topology.h"
#include "utl/Logger.h"
#include "td/TimingManager.h"
#include <unistd.h>
#include "ord/OpenRoad.hh"
#include "td/TimingOracle.h"
namespace gpl3d {

using utl::GPL3D;

void Placer3d::init(odb::dbDatabase* db,
                    sta::dbNetwork* db_network,
                    sta::dbSta* sta,
                    utl::Logger* logger)
{
  db_ = db;
  db_network_ = db_network;
  sta_ = sta;
  logger_ = logger;
}

void Gpl3dInit(ord::OpenRoad* openroad,
  PlaceDB* place_db,
  sta::dbSta* dbsta,
  utl::Logger* logger)
{
Tcl_Interp* interp = openroad ? openroad->tclInterp() : nullptr;

// 构造并持有 oracle（确保生命周期覆盖整个会话）
static gpl3d::td::TimingOracle s_oracle(place_db, dbsta, logger);
s_oracle.registerTcl(interp);  // ★ 注册 gpl3d::td::timing_iteration

// 你已有的其它命令注册...
// Tcl_CreateObjCommand(interp, "gpl3d::import_place_db", ...);
// Tcl_CreateObjCommand(interp, "gpl3d::placer3d_run", ...);
}

void Placer3d::run()
{
  logger_->info(utl::GPL3D, 0, "Begin running ePlace3D...");

  // Step1: 用 parser 把 OpenDB 转换到 PlaceDB
  PlaceDB pdb;
  parser::getOpenRoadParser()->importOpenRoadData(db_, pdb);

  std::cout << "[Check] PlaceDB summary: modules=" << pdb.dbModules.size()
            << ", nets=" << pdb.dbNets.size()
            << ", rows=" << pdb.dbSiteRows.size() << std::endl;

  // Step2: 构建 ePlace3D Placer
  double GPtime;

  time_start(&GPtime);

  int tierCount = 2;
  if (gArg.CheckExist("t3")) {
    tierCount = 3;
  }
  if (gArg.CheckExist("t4")) {
    tierCount = 4;
  }

  if (gArg.CheckExist("pseudoClkNet")) {
    printf("Adding clock net gradient in space placer\n");
  }
  if (gArg.CheckExist("TPpseudoClkNet")) {
    printf("Adding clock net gradient in tier placer\n");
  }
  if (gArg.CheckExist("pseudoClkMassCenter")) {
    printf("Adding clock mass center gradient in space placer\n");
  }
  if (gArg.CheckExist("clkwirelevel")) {
    printf("Adding clock wirelength level weight\n");
  }

  pdb.convertTo3d(tierCount);
  double targetDensity = 0.9;
  gpl3d::td::NetWeightScheduler::Params sched_p; // 默认即可
  gpl3d::td::TimingManager::Config cfg;          // 只做 setup
  auto timing = std::make_unique<gpl3d::td::TimingManager>(&pdb, sta_, logger_, sched_p, cfg);

  gpl3d::td::RCTreeBuilder3D::TechParams tech;
  tech.RperUm.assign(std::max(2, tierCount), 0.08);
  tech.CperUm.assign(std::max(2, tierCount), 0.20e-15);
  tech.Rtsv = 0.03;
  tech.Ctsv = 20e-15;
  timing->setTechParams(tech);

  SpacePlacer placer3d(&pdb, targetDensity);
  placer3d.placeInitialization();

  // std::cout << "[Debug] total modules = " << pdb.dbModules.size()
  //           << ", freeNodes = " << placer3d.getFreeNodeCount() << std::endl;

  // auto freeNodes = placer3d.getFreeNodes();
  // for (int i = 0; i < 10 && i < (int) freeNodes.size(); i++) {
  //   auto* mod = freeNodes[i];
  //   std::cout << "  FreeNode " << mod->name << std::endl;}

  placer3d.place();

  printf(
      "\n====Space Placement finished, now start partition and tier "
      "placement====\n");
  std::unordered_map<Module*, ModulePosition> partitionedPosition;
   partitionedPosition = placer3d.getPartitionedPosition();

  TierPlacer tp(&pdb, std::move(partitionedPosition), 0.95);
  tp.setTiming(timing.get(), /*k_timing=*/15);

  tp.place();

  time_end(&GPtime);

  printf("\nGlobal Placement time: %.2f seconds\n\n", GPtime);


  logger_->info(utl::GPL3D, 0, "ePlace3D finished.");
}

}  // namespace gpl3d
