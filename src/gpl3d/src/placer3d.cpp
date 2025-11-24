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
#include "td/Timingtype.h"
#include "tcl.h"
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

// 全局 TimingManager 访问点（用于 TCL 命令）
static gpl3d::td::TimingManager* g_timing_manager = nullptr;
static PlaceDB* g_place_db = nullptr;
static utl::Logger* g_logger = nullptr;
static gpl3d::td::TimingOracle* g_timing_oracle = nullptr;  // 用于 TCL 命令

// TCL 命令：timing_iteration（使用全局 TimingOracle）
static int timing_iteration_cmd(ClientData client_data, Tcl_Interp* tcl, int objc, Tcl_Obj* const objv[])
{
  if (!g_timing_oracle) {
    Tcl_SetResult(tcl, const_cast<char*>("TimingOracle not initialized. Call gpl3d::placer3d_run first."), TCL_STATIC);
    return TCL_ERROR;
  }

  bool do_hold = false;
  for (int i = 1; i < objc; ++i) {
    const char* key = Tcl_GetString(objv[i]);
    if (strcmp(key, "-hold") == 0 && i + 1 < objc) {
      int v = 0;
      if (Tcl_GetIntFromObj(tcl, objv[++i], &v) != TCL_OK) return TCL_ERROR;
      do_hold = (v != 0);
    } else if (key[0] == '-') {
      Tcl_SetResult(tcl, const_cast<char*>("unknown option"), TCL_STATIC);
      return TCL_ERROR;
    }
  }

  gpl3d::td::StaSummary sum = g_timing_oracle->updateAndReport(do_hold);

  Tcl_Obj* dict = Tcl_NewDictObj();
  Tcl_DictObjPut(tcl, dict, Tcl_NewStringObj("WNS_setup",-1), Tcl_NewDoubleObj(sum.WNS_setup));
  Tcl_DictObjPut(tcl, dict, Tcl_NewStringObj("TNS_setup",-1), Tcl_NewDoubleObj(sum.TNS_setup));
  Tcl_DictObjPut(tcl, dict, Tcl_NewStringObj("WNS_hold",-1),  Tcl_NewDoubleObj(sum.WNS_hold));
  Tcl_DictObjPut(tcl, dict, Tcl_NewStringObj("TNS_hold",-1),  Tcl_NewDoubleObj(sum.TNS_hold));
  Tcl_SetObjResult(tcl, dict);
  return TCL_OK;
}

// TCL 命令：应用网络权重
// 用法: gpl3d::td::apply_net_weights {net_name1 weight1 net_name2 weight2 ...}
static int apply_net_weights_cmd(ClientData client_data, Tcl_Interp* tcl, int objc, Tcl_Obj* const objv[])
{
  if (objc != 2) {
    Tcl_SetResult(tcl, const_cast<char*>("usage: gpl3d::td::apply_net_weights <dict>"), TCL_STATIC);
    return TCL_ERROR;
  }

  if (!g_timing_manager || !g_place_db) {
    Tcl_SetResult(tcl, const_cast<char*>("TimingManager or PlaceDB not initialized. Call gpl3d::placer3d_run first."), TCL_STATIC);
    return TCL_ERROR;
  }

  // 解析 TCL 字典
  Tcl_Obj* dict_obj = objv[1];
  Tcl_DictSearch search;
  Tcl_Obj *key_obj, *value_obj;
  int done = 0;

  if (Tcl_DictObjFirst(tcl, dict_obj, &search, &key_obj, &value_obj, &done) != TCL_OK) {
    Tcl_SetResult(tcl, const_cast<char*>("invalid dict format"), TCL_STATIC);
    return TCL_ERROR;
  }

  std::unordered_map<std::string, double> net_weights;
  while (!done) {
    const char* net_name = Tcl_GetString(key_obj);
    double weight = 0.0;
    if (Tcl_GetDoubleFromObj(tcl, value_obj, &weight) != TCL_OK) {
      Tcl_DictObjDone(&search);
      Tcl_SetResult(tcl, const_cast<char*>("invalid weight value"), TCL_STATIC);
      return TCL_ERROR;
    }
    net_weights[std::string(net_name)] = weight;
    Tcl_DictObjNext(&search, &key_obj, &value_obj, &done);
  }
  Tcl_DictObjDone(&search);

  // 应用权重
  g_timing_manager->setWeightsFromDict(net_weights, g_place_db);

  return TCL_OK;
}

void Gpl3dInit(ord::OpenRoad* openroad,
  PlaceDB* place_db,
  sta::dbSta* dbsta,
  utl::Logger* logger)
{
Tcl_Interp* interp = openroad ? openroad->tclInterp() : nullptr;
// 调试：打印初始化调用（便于排查命令是否被注册）
fprintf(stderr, "Gpl3dInit: called (interp=%p, place_db=%p, dbsta=%p)\n",
  static_cast<void*>(interp), static_cast<void*>(place_db), static_cast<void*>(dbsta));

// 保存全局引用（用于 TCL 命令访问）
g_place_db = place_db;
g_logger = logger;

// 注册 TCL 命令（使用全局变量，在 run() 时设置）
if (interp) {
  // 注册 timing_iteration 命令
  Tcl_CreateObjCommand(interp,
    "gpl3d::td::timing_iteration",
    timing_iteration_cmd,
    nullptr,
    nullptr);

  // 注册权重应用命令
  Tcl_CreateObjCommand(interp,
    "gpl3d::td::apply_net_weights",
    apply_net_weights_cmd,
    nullptr,
    nullptr);
}

// 你已有的其它命令注册...
// Tcl_CreateObjCommand(interp, "gpl3d::import_place_db", ...);
// Tcl_CreateObjCommand(interp, "gpl3d::placer3d_run", ...);
}

void Placer3d::run()
{
  logger_->info(utl::GPL3D, 0, "Begin running ePlace3D...");

  // Step1: 用 parser 把 OpenDB 转换到 PlaceDB（使用局部变量，确保每次运行都是干净的状态）
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
  
  // Space Placement 阶段不需要 TimingManager，所以先进行 Space Placement
  double targetDensity = 0.8;
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

  // 在 Tier Placement 之前创建 TimingManager（Tier Placement 需要它）
  // 同时将 pdb 复制到成员变量 db 以保持生命周期
  db = std::move(pdb);
  
  gpl3d::td::NetWeightScheduler::Params sched_p; // 默认即可
  gpl3d::td::TimingManager::Config cfg;          // 只做 setup
  timing_ = std::make_unique<gpl3d::td::TimingManager>(&db, sta_, logger_, sched_p, cfg);
  
  // 保存全局引用（用于 TCL 命令访问）
  g_timing_manager = timing_.get();
  g_place_db = &db;
  g_timing_oracle = timing_->getTimingOracle();  // 设置 TimingOracle 引用

  gpl3d::td::RCTreeBuilder3D::TechParams tech;
  tech.RperUm.assign(std::max(2, tierCount), 0.08);
  tech.CperUm.assign(std::max(2, tierCount), 0.20e-15);
  tech.Rtsv = 0.03;
  tech.Ctsv = 20e-15;
  timing_->setTechParams(tech);

  TierPlacer tp(&db, std::move(partitionedPosition), 0.8);
  tp.setTiming(timing_.get(), /*k_timing=*/5);  // 改为每5个iteration运行一次timing分析

  tp.place();

  time_end(&GPtime);

  printf("\nGlobal Placement time: %.2f seconds\n\n", GPtime);


  logger_->info(utl::GPL3D, 0, "ePlace3D finished.");
  
  // 注意：不要在这里清理全局指针，因为 TCL 脚本可能还需要使用它们
  // 全局指针会在 Placer3d 对象销毁时自动失效（通过 timing_ 的析构）
}

}  // namespace gpl3d
