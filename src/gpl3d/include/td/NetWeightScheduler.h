#pragma once

#include <unordered_map>
#include <algorithm>
#include <limits>
#include <cmath>
#include "utl/Logger.h"

// 你们项目中的 Net 在 eplace3D/PlaceDB 下是全局作用域类型（无命名空间）。
// 这里只做前向声明，避免引入大型头文件依赖。
class Net;

namespace gpl3d::td {

/// NetWeightScheduler
/// 将 per-net 关键度(0~1) 平滑映射为线长权重乘子 w_n：
///   w_n ← clip_[wmin,wmax]( EMA( w_old , 1 + alpha * (crit_n)^gamma ) )
/// 并返回：平均相对变化量是否超过阈值（用于外环触发 rebuildPreconditioner）。
///
/// 设计目标：
/// - 连续可导（权重做乘子，只影响一阶/二阶缩放，不改变导数形式）
/// - 数值稳定（EMA 平滑、限幅、相对变化阈值）
/// - 与项目数据结构解耦（仅依赖 Net* 作为 key）
class NetWeightScheduler {
public:
  struct Params {
    double alpha = 3.0;              // 关键度=1 时，目标权重=1+alpha
    double gamma = 2.0;              // 非线性强调高关键度
    double ema   = 0.7;              // EMA 平滑系数（越大越保守）
    double wmin  = 0.5;              // 权重下限，防止“掐死”某些网
    double wmax  = 8.0;              // 权重上限，防止发散
    double change_threshold = 0.05;   // 平均相对改变量阈值（例：5%）→ 触发重建预条件器
    bool   decay_inactive_to_one = true; // 本轮未出现的网向 1.0 衰减
    double eps = 1e-12;              // 数值稳定用
  };

  explicit NetWeightScheduler(utl::Logger* logger);
  NetWeightScheduler(utl::Logger* logger, const Params& p);

  /// 核心接口
  /// @param crit  本轮 net → criticality ∈[0,1]（未出现的网视作 0）
  /// @param net_w 原地更新：net → weight 乘子（不存在的网默认从 1.0 起）
  /// @return      平均相对变化量是否超过阈值（用于外环触发 rebuildPreconditioner）
  bool computeWeights(const std::unordered_map<const ::Net*, double>& crit,
    std::unordered_map<const ::Net*, double>& net_w);

  const Params& params() const { return p_; }
  void setParams(const Params& p) { p_ = p; }

private:
  template <typename T>
  static T clamp(T v, T lo, T hi) { return std::max(lo, std::min(v, hi)); }

  static double getOr(const std::unordered_map<const ::Net*, double>& m,
                      const ::Net* k, double defv)
  {
    const auto it = m.find(k);
    return it == m.end() ? defv : it->second;
  }

  /// 更健壮的相对变化：|a-b| / max(|a|, |b|, eps)
  static double safeRelChange(double a, double b, double eps) {
    const double denom = std::max({std::fabs(a), std::fabs(b), eps});
    return std::fabs(a - b) / denom;
  }

  utl::Logger* logger_; // 外部传入的 OpenROAD 日志器
  Params p_;
};

} // namespace gpl3d::td
