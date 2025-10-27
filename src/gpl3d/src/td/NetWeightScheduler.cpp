#include "td/NetWeightScheduler.h"

namespace gpl3d::td {

  NetWeightScheduler::NetWeightScheduler(utl::Logger* logger)
  : logger_(logger), p_{} // 使用 Params 的默认值
{}

NetWeightScheduler::NetWeightScheduler(utl::Logger* logger, const Params& p)
  : logger_(logger), p_(p)
{
}

bool NetWeightScheduler::computeWeights(
    const std::unordered_map<const ::Net*, double>& crit,
    std::unordered_map<const ::Net*, double>& net_w)
{
  double sum_rel_change = 0.0;
  size_t cnt = 0, up_cnt = 0, down_cnt = 0;
  double min_w = std::numeric_limits<double>::infinity();
  double max_w = 0.0;

  // 1) 处理本轮出现 criticality 的网
  for (const auto& kv : crit) {
    const ::Net* n = kv.first;
    double c_raw   = kv.second;

    // clamp 到 [0,1]，过滤异常值
    if (!(c_raw >= 0.0 && c_raw <= 1.0)) {
      c_raw = clamp(c_raw, 0.0, 1.0);
    }
    const double c = c_raw;

    const double w_old    = getOr(net_w, n, 1.0);
    const double w_target = 1.0 + p_.alpha * std::pow(c, p_.gamma);
    double w_new          = p_.ema * w_old + (1.0 - p_.ema) * w_target;
    w_new                 = clamp(w_new, p_.wmin, p_.wmax);

    const double rel = safeRelChange(w_new, w_old, p_.eps);
    sum_rel_change += rel;
    cnt += 1;
    if (w_new > w_old) ++up_cnt;
    else if (w_new < w_old) ++down_cnt;

    net_w[n] = w_new;
    if (w_new < min_w) min_w = w_new;
    if (w_new > max_w) max_w = w_new;
  }

  // 2) 对于本轮未出现的网：可选向 1.0 回落（避免长期残留过大/过小权重）
  if (p_.decay_inactive_to_one) {
    for (auto& kv : net_w) {
      const ::Net* n = kv.first;
      double& w_old  = kv.second;
      if (crit.find(n) != crit.end()) {
        continue; // 已在步骤1处理
      }

      const double w_target = 1.0;
      double w_new          = p_.ema * w_old + (1.0 - p_.ema) * w_target;
      w_new                 = clamp(w_new, p_.wmin, p_.wmax);

      const double rel = safeRelChange(w_new, w_old, p_.eps);
      sum_rel_change += rel;
      cnt += 1;
      if (w_new > w_old) ++up_cnt;
      else if (w_new < w_old) ++down_cnt;

      w_old = w_new;
      if (w_new < min_w) min_w = w_new;
      if (w_new > max_w) max_w = w_new;
    }
  }

  const double avg_rel_change = (cnt ? (sum_rel_change / static_cast<double>(cnt)) : 0.0);

  if (logger_) {
    // 你们已有在 gpl3d 中使用的日志类别：utl::GPL3D（见 Placer3d.cpp）
    // 如果你们工程自定义了别的类别，也可改成相应类别。
    logger_->info(utl::GPL3D, 0,
      "TD weight update: nets={}, avgΔ={:.3f} (thr={:.3f}), up={}, down={}, "
      "w∈[{:.3f},{:.3f}], α={:.2f}, γ={:.2f}, ema={:.2f}",
      cnt, avg_rel_change, p_.change_threshold, up_cnt, down_cnt,
      (cnt ? min_w : 1.0), (cnt ? max_w : 1.0),
      p_.alpha, p_.gamma, p_.ema);
  }

  // true → 外环调用 rebuildPreconditioner()
  return avg_rel_change > p_.change_threshold;
}

} // namespace gpl3d::td
