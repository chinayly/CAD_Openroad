#pragma once

#include <unordered_map>
#include <vector>
#include <tuple>
#include <cstddef>
#include <utility>
#include <limits>
#include <cmath>
#include "utl/Logger.h"

// 你们的 PlaceDB/Net/Pin/Module/ModulePosition/POS_2D 在 eplace3D/PlaceDB 下
// 工程其他文件就是直接 include 这些短名头文件的（见 tierplacer.cpp）
#include "placedb.h"

namespace gpl3d::td {

/// 2D 线段（仅在不开启 OpenROAD 估计时才会使用；默认我们不生成 2D 段）
struct RCWireSeg {
  const Pin*   u {nullptr};
  const Pin*   v {nullptr};
  int          tier {0};
  double       length_um {0.0};
  double       R {0.0};
  double       C {0.0};
};

/// TSV 段（我们在本方案中 **主要产出它** 给 ParasiticsInjector 叠加）
struct RCTsvSeg {
  const Pin*   u {nullptr};   // 仅作参考标注（注入时可由 ParasiticsInjector 选择 driver/sink）
  const Pin*   v {nullptr};
  int          z_from {0};
  int          z_to   {0};
  int          count  {0};    // 穿越的 TSV 数，一般 = |z_to - z_from|
  double       R {0.0};
  double       C {0.0};
};

/// 针对一条 net 的简化 RC 模型
/// - 在“OpenROAD 2D 估计 + TSV 叠加”模式下：`wires` 通常为空，`tsvs` 填满
/// - elmore_hint 仅做轻量排序/日志，不参与时序计算
struct RCNetModel {
  std::vector<RCWireSeg> wires;  // 默认为空（2D 交给 OpenROAD）
  std::vector<RCTsvSeg>  tsvs;   // 我们产出并叠加
  double elmore_hint {0.0};
};

/// 轻量 3D RC 构建器：
/// - 输入：PlaceDB（拓扑）、当前 Module → ModulePosition（含 position/tierId）
/// - 输出：每条跨层 net 的 TSV 信息；2D 线段默认不构建（由 OpenROAD 估计）
/// - 若 fallback 模式（未使用 OpenROAD 估计）则可同时生成 2D wires（可选开关）
class RCTreeBuilder3D {
public:
  struct TechParams {
    // 每层线参（当 fallback 生成 2D wires 时使用），R' [Ω/um], C' [F/um]
    std::vector<double> RperUm;
    std::vector<double> CperUm;
    // TSV 等效参数
    double Rtsv = 0.03;     // 30 mΩ
    double Ctsv = 20e-15;   // 20 fF
  };

  RCTreeBuilder3D(PlaceDB* pdb, utl::Logger* logger);

  void setTechParams(const TechParams& t);
  /// 使用 OpenROAD 估计 2D（默认 true）：我们只构建 TSV；false 则也可构建 2D 线段
  void setUseOpenROAD2DEstimate(bool v) { use_openroad_2d_ = v; }
  bool useOpenROAD2DEstimate() const { return use_openroad_2d_; }

  /// 用外层维护的 modulePosition（Module* -> ModulePosition{position,tierId}）更新 RC 模型
  void updateFromPlacement(const std::unordered_map<Module*, ModulePosition>& module_pos);

  /// 查询某条 net 的 RC 模型（若不存在则返回空指针）
  const RCNetModel* model(const Net* n) const;

  /// 获取全部 net 的 RC 模型（供 ParasiticsInjector 扫描注入 TSV）
  const std::unordered_map<const Net*, RCNetModel>& all() const { return models_; }

  /// 统计：跨层 net 的数量（便于日志观测）
  std::size_t numCrossLayerNets() const { return cross_layer_nets_; }

private:
  // 内部构建
  void buildOneNet_(const Net* n, const std::unordered_map<Module*, ModulePosition>& mp);
  void detectTSVAndOptional2D_(const std::vector<std::tuple<const Pin*, double,double,int>>& pin_xyz,
                               RCNetModel& out) const;
  void fillRC_(RCNetModel& out) const;

  static inline double manhattan(double dx, double dy) { return std::fabs(dx) + std::fabs(dy); }

private:
  PlaceDB* pdb_;
  utl::Logger* logger_;
  TechParams tech_;

  // 是否让 OpenROAD 估计 2D（默认 true），我们只做 TSV
  bool use_openroad_2d_ { true };

  // 缓存
  std::unordered_map<const Net*, RCNetModel> models_;
  std::size_t cross_layer_nets_ {0};
};

} // namespace gpl3d::td
