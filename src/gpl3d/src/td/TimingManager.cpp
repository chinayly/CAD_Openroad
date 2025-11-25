#include "td/TimingManager.h"

// 在实现文件里包含完整定义
#include "td/ParasiticsInjector.h"
#include "td/TimingOracle.h"
#include "td/RCTreeBuilder3D.h"
#include "td/NetWeightScheduler.h"
#include "td/TSVNetMapper.h"

#include "ord/OpenRoad.hh"
#include "db_sta/dbSta.hh"
#include "sta/Sta.hh"
#include "gpl3d/parser_odb.h"
#include "odb/db.h"
#include <tcl.h>

namespace gpl3d::td {

    TimingManager::TimingManager(PlaceDB* pdb, sta::dbSta* sta, utl::Logger* logger,
        const NetWeightScheduler::Params& sched_params)
: pdb_(pdb), ord_(nullptr), sta_(sta), logger_(logger)
{
rc_.reset(new RCTreeBuilder3D(pdb_, logger_));
pinj_.reset(new ParasiticsInjector(sta_, logger_));  // ★ 用 dbSta*
toracle_.reset(new TimingOracle(pdb_, sta_, logger_));
sched_.reset(new NetWeightScheduler(logger_, sched_params));
tsv_mapper_.reset(new TSVNetMapper());
cfg_ = Config{};
}

TimingManager::TimingManager(PlaceDB* pdb, sta::dbSta* sta, utl::Logger* logger,
    const NetWeightScheduler::Params& sched_params,
    const Config& cfg)
: pdb_(pdb), ord_(nullptr), sta_(sta), logger_(logger), cfg_(cfg)
{
rc_.reset(new RCTreeBuilder3D(pdb_, logger_));
pinj_.reset(new ParasiticsInjector(sta_, logger_));
toracle_.reset(new TimingOracle(pdb_, sta_, logger_));
sched_.reset(new NetWeightScheduler(logger_, sched_params));
tsv_mapper_.reset(new TSVNetMapper());
}

TimingManager::~TimingManager() = default;

bool TimingManager::timingIteration(const ModulePosMap& module_pos, int iteration)
{
  if (logger_) {
    logger_->info(utl::GPL3D, 0, "Timing iteration: build RC (3D/TSV) … (iteration={})", iteration);
  }

  // 更新 TSV 映射器（基于当前布局位置）
  tsv_mapper_->buildFromPlaceDB(pdb_, module_pos);

  // 更新 RC Tree（基于当前布局位置）
  rc_->updateFromPlacement(module_pos);

  // ★ 关键修复：在运行 STA 之前，先同步位置到 OpenROAD 数据库
  // 因为 STA 的 updateTiming() 会从 OpenROAD 数据库读取位置
  syncPositionsToOpenROAD(module_pos);
  
  // ★ 关键修复：更新寄生参数（基于新位置）
  // 对于没有 TSV 的 net，STA 需要使用基于新位置的寄生参数
  // 注意：这必须在 TSV 注入之前调用，因为 estimate_parasitics 会清除旧的寄生参数
  updateParasitics();
  
  // ★ 调试信息：打印一些模块位置变化（用于验证同步是否成功）
  if (logger_ && iteration >= 0 && iteration < 3) {
    int sample_count = 0;
    for (const auto& kv : module_pos) {
      if (sample_count++ >= 3) break;
      Module* m = kv.first;
      const ModulePosition& pos = kv.second;
      logger_->info(utl::GPL3D, 0, "  Sample module '{}': pos=({:.2f}, {:.2f}), tier={}",
                    m->name, pos.position.x, pos.position.y, pos.tierId);
    }
  }

  // 注入 TSV 寄生参数到 STA（使用新的接口，基于原始 net 和 Steiner tree）
  // 需要获取 odb::dbDatabase，从 ord_ 或 OpenRoad::openRoad() 获取
  odb::dbDatabase* odb_db = nullptr;
  if (ord_) {
    odb_db = ord_->getDb();
  } else {
    // 如果 ord_ 是 nullptr，尝试从全局 OpenRoad 实例获取
    ord::OpenRoad* openroad = ord::OpenRoad::openRoad();
    if (openroad) {
      odb_db = openroad->getDb();
    }
  }
  
  if (!odb_db) {
    if (logger_) {
      logger_->warn(utl::GPL3D, 0, "TimingManager: cannot get odb::dbDatabase, skipping TSV injection.");
    }
  } else {
    const double totalC = pinj_->overlayTSV(*tsv_mapper_, pdb_, odb_db, module_pos);
    if (logger_) {
      logger_->info(utl::GPL3D, 0, "Injected TSV R/C into STA parasitics (total = {:.3f} fF).", totalC * 1e15);
    }
  }

  // 运行 STA 并计算 criticality，同时标注slack数据（用于AI训练）
  const auto s = toracle_->updateAndReport(/*compute_hold=*/cfg_.compute_hold, iteration);
  summary_ = s;

  // 从 TimingOracle 获取 criticality 并更新权重
  const auto& crit = toracle_->netCriticalities();
  const bool need_rebuild = sched_->computeWeights(crit, net_w_);

  if (logger_ && !net_w_.empty()) {
    double wmin = 1e100, wmax = 0.0;
    for (const auto& kv : net_w_) {
      if (kv.second < wmin) wmin = kv.second;
      if (kv.second > wmax) wmax = kv.second;
    }
    logger_->info(utl::GPL3D, 0, "TD weights updated: |nets|={}, w∈[{:.3f},{:.3f}].",
                  static_cast<int>(net_w_.size()), wmin, wmax);
  }

  return need_rebuild;
}

double TimingManager::weightOf(const ::Net* n) const
{
  auto it = net_w_.find(n);
  return (it == net_w_.end()) ? 1.0 : it->second;
}

void TimingManager::setTechParams(const RCTreeBuilder3D::TechParams& t)
{
  if (rc_) rc_->setTechParams(t);
}

void TimingManager::clearWeights()
{
  net_w_.clear();
  if (logger_) {
    logger_->info(utl::GPL3D, 0, "TimingManager: cleared all net weights (reset to 1.0 by default).");
  }
}

void TimingManager::setWeightsFromDict(const std::unordered_map<std::string, double>& net_weights, PlaceDB* pdb)
{
  if (!pdb) {
    if (logger_) {
      logger_->warn(utl::GPL3D, 0, "TimingManager::setWeightsFromDict: PlaceDB is null.");
    }
    return;
  }

  size_t found_count = 0;
  for (const auto& kv : net_weights) {
    const std::string& net_name = kv.first;
    const double weight = kv.second;

    // 在 PlaceDB 中查找对应的 Net
    Net* net = nullptr;
    for (Net* n : pdb->dbNets) {
      if (n->name == net_name) {
        net = n;
        break;
      }
    }

    if (net) {
      net_w_[net] = weight;
      found_count++;
    } else {
      if (logger_) {
        logger_->warn(utl::GPL3D, 0, "TimingManager::setWeightsFromDict: net '{}' not found in PlaceDB.", net_name);
      }
    }
  }

  if (logger_) {
    logger_->info(utl::GPL3D, 0, "TimingManager: set weights from external dict: {}/{} nets found and updated.",
                  found_count, net_weights.size());
  }
}

// 同步模块位置到 OpenROAD 数据库（私有辅助函数）
void TimingManager::syncPositionsToOpenROAD(const ModulePosMap& module_pos)
{
  // 获取 OpenROAD 实例和数据库
  odb::dbDatabase* odb_db = nullptr;
  if (ord_) {
    odb_db = ord_->getDb();
  } else {
    ord::OpenRoad* openroad = ord::OpenRoad::openRoad();
    if (openroad) {
      odb_db = openroad->getDb();
    }
  }
  
  if (!odb_db) {
    if (logger_) {
      logger_->warn(utl::GPL3D, 0, "TimingManager: cannot get odb::dbDatabase, skipping position sync.");
    }
    return;
  }
  
  // 获取 parser 的 module-inst 映射
  parser::OpenRoadParser* parser = parser::getOpenRoadParser();
  if (!parser) {
    if (logger_) {
      logger_->warn(utl::GPL3D, 0, "TimingManager: cannot get OpenRoadParser, skipping position sync.");
    }
    return;
  }
  
  const auto& module_inst_map = parser->moduleInstMap();
  
  // 获取 DBU 转换因子
  odb::dbBlock* blk = odb_db->getChip()->getBlock();
  if (!blk) {
    if (logger_) {
      logger_->warn(utl::GPL3D, 0, "TimingManager: cannot get block, skipping position sync.");
    }
    return;
  }
  const int dbu_per_micron = blk->getDbUnitsPerMicron();
  
  // 同步每个模块的位置
  int synced_count = 0;
  int fixed_count = 0;
  int not_found_count = 0;
  for (const auto& kv : module_pos) {
    Module* m = kv.first;
    const ModulePosition& pos = kv.second;
    
    auto it = module_inst_map.find(m);
    if (it == module_inst_map.end()) {
      not_found_count++;
      continue;  // 可能是 filler 或其他不在 OpenROAD 中的模块
    }
    
    odb::dbInst* inst = it->second;
    if (!inst) {
      continue;
    }
    
    // 跳过固定模块
    if (inst->getPlacementStatus().isFixed()) {
      fixed_count++;
      continue;
    }
    
    // 获取新位置（中心坐标，单位：微米）
    double cx_um = pos.position.x;
    double cy_um = pos.position.y;
    
    // 转换为 DBU（左下角坐标）
    int llx_dbu = static_cast<int>((cx_um - m->width / 2.0) * dbu_per_micron);
    int lly_dbu = static_cast<int>((cy_um - m->height / 2.0) * dbu_per_micron);
    
    // 更新 OpenROAD 数据库中的位置
    inst->setLocation(llx_dbu, lly_dbu);
    inst->setPlacementStatus(odb::dbPlacementStatus::PLACED);
    synced_count++;
  }
  
  if (logger_) {
    logger_->info(utl::GPL3D, 0, 
                  "TimingManager: synced {} module positions to OpenROAD database (fixed: {}, not_found: {}).",
                  synced_count, fixed_count, not_found_count);
  }
}

// 更新寄生参数（基于新位置）
void TimingManager::updateParasitics()
{
  // 通过 TCL 命令调用 estimate_parasitics -placement
  // 这样可以避免依赖 Resizer 的头文件
  Tcl_Interp* interp = nullptr;
  if (ord_) {
    interp = ord_->tclInterp();
  } else {
    ord::OpenRoad* openroad = ord::OpenRoad::openRoad();
    if (openroad) {
      interp = openroad->tclInterp();
    }
  }
  
  if (interp) {
    // 调用 TCL 命令 estimate_parasitics -placement
    // 这会基于 OpenROAD 数据库中的新位置重新估计寄生参数
    int result = Tcl_Eval(interp, "estimate_parasitics -placement");
    if (result == TCL_OK) {
      if (logger_) {
        logger_->info(utl::GPL3D, 0, "TimingManager: updated parasitics via TCL command (based on new positions).");
      }
    } else {
      if (logger_) {
        logger_->warn(utl::GPL3D, 0, "TimingManager: failed to update parasitics via TCL: {}", Tcl_GetStringResult(interp));
      }
    }
  } else {
    if (logger_) {
      logger_->warn(utl::GPL3D, 0, "TimingManager: cannot get TCL interp, skipping parasitics update.");
    }
  }
}

} // namespace gpl3d::td
