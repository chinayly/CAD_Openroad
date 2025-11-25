#pragma once
#include <memory>
#include <unordered_map>

#include "placedb.h"
#include "td/Timingtype.h"
#include "utl/Logger.h"

// 关键：需要这些头来拿到"完整类型"，因为我们在接口中用到了它们的嵌套类型
#include "td/NetWeightScheduler.h"  // 为了 NetWeightScheduler::Params
#include "td/RCTreeBuilder3D.h"     // 为了 RCTreeBuilder3D::TechParams
#include "td/TSVNetMapper.h"        // 为了 TSVNetMapper

namespace ord {
class OpenRoad;
}
namespace sta {
class dbSta;
}

namespace gpl3d::td {

class ParasiticsInjector;  // 在 .cpp 里包含其头
class TimingOracle;        // 在 .cpp 里包含其头

class TimingManager
{
 public:
  using ModulePosMap = std::unordered_map<Module*, ModulePosition>;

  struct Config
  {
    bool compute_hold = false;  // 是否同时统计 hold；默认只统计 setup
  };

  TimingManager(PlaceDB* pdb,
                ord::OpenRoad* ord,
                utl::Logger* logger,
                const NetWeightScheduler::Params& sched_params);

  TimingManager(PlaceDB* pdb,
                ord::OpenRoad* ord,
                utl::Logger* logger,
                const NetWeightScheduler::Params& sched_params,
                const Config& cfg);

  TimingManager(PlaceDB* pdb,
                sta::dbSta* sta,
                utl::Logger* logger,
                const NetWeightScheduler::Params& sched_params);
  TimingManager(PlaceDB* pdb,
                sta::dbSta* sta,
                utl::Logger* logger,
                const NetWeightScheduler::Params& sched_params,
                const Config& cfg);
  // 重点：析构放到 .cpp 里定义，确保此处不需要完整类型
  ~TimingManager();

  // 一次"时序迭代"：构建 3D RC → 注入 TSV → 跑 STA → 计算权重
  // iteration: 当前iteration编号，用于标注slack数据（-1表示不记录iteration）
  // 返回：是否需要 rebuildPreconditioner()
  bool timingIteration(const ModulePosMap& module_pos, int iteration = -1);

  // 线长/梯度乘子
  double weightOf(const ::Net* n) const;

  // 权重表（只读）
  const std::unordered_map<const ::Net*, double>& weights() const
  {
    return net_w_;
  }

  // 最近一次 STA 汇总
  const StaSummary& summary() const { return summary_; }

  // 配置
  void setConfig(const Config& c) { cfg_ = c; }
  const Config& config() const { return cfg_; }

  // 获取 TimingOracle（用于 TCL 命令）
  TimingOracle* getTimingOracle() const { return toracle_.get(); }

  // 透传 3D RC 工艺参数
  void setTechParams(const RCTreeBuilder3D::TechParams& t);

  // 清空权重
  void clearWeights();

  // 从外部（TCL脚本）设置权重
  // @param net_weights: net名称 -> 权重的映射
  void setWeightsFromDict(const std::unordered_map<std::string, double>& net_weights, PlaceDB* pdb);

 private:
  // 同步模块位置到 OpenROAD 数据库（私有辅助函数）
  void syncPositionsToOpenROAD(const ModulePosMap& module_pos);
  
  // 更新寄生参数（基于新位置）
  void updateParasitics();
  
  PlaceDB* pdb_{nullptr};
  ord::OpenRoad* ord_{nullptr};
  sta::dbSta*    sta_{nullptr}; 
  utl::Logger* logger_{nullptr};

  std::unique_ptr<RCTreeBuilder3D> rc_;
  std::unique_ptr<ParasiticsInjector> pinj_;
  std::unique_ptr<TimingOracle> toracle_;
  std::unique_ptr<NetWeightScheduler> sched_;
  std::unique_ptr<TSVNetMapper> tsv_mapper_;  // TSV 映射器

  std::unordered_map<const ::Net*, double> net_w_;
  StaSummary summary_{};  // ← 类型来自 TimingOracle.h
  Config cfg_{};
};

}  // namespace gpl3d::td
