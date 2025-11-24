#include "td/RCTreeBuilder3D.h"

namespace gpl3d::td {

RCTreeBuilder3D::RCTreeBuilder3D(PlaceDB* pdb, utl::Logger* logger)
  : pdb_(pdb), logger_(logger)
{
  // 给出保守默认线参（当 fallback 构建 2D wires 时使用）
  std::size_t tiers = 4;
  try {
    if (pdb_ && !pdb_->dbTiers.empty()) {
      tiers = pdb_->dbTiers.size();
    }
  } catch (...) {
    // 某些构建环境下头文件被精简时，保护性处理
  }
  tech_.RperUm.assign(tiers, 0.08);      // 0.08 Ω/um
  tech_.CperUm.assign(tiers, 0.20e-15);  // 0.20 fF/um
  tech_.Rtsv = 0.03;                     // 30 mΩ
  tech_.Ctsv = 20e-15;                   // 20 fF
}

void RCTreeBuilder3D::setTechParams(const TechParams& t) {
  tech_ = t;
}

void RCTreeBuilder3D::updateFromPlacement(const std::unordered_map<Module*, ModulePosition>& module_pos)
{
  models_.clear();
  cross_layer_nets_ = 0;
  if (!pdb_) return;

  // 遍历全部 nets，构建 TSV（以及可选 2D）模型
  // 过滤：只处理有效的 nets（至少有 2 个 pin，且这些 pin 的 module 都有位置）
  for (Net* n : pdb_->dbNets) {
    if (!n || n->netPins.empty()) continue;
    
    // 检查 net 是否有足够的有效 pins（至少 2 个，且它们的 module 都有位置）
    int valid_pin_count = 0;
    for (Pin* p : n->netPins) {
      if (p && p->module && module_pos.find(p->module) != module_pos.end()) {
        valid_pin_count++;
      }
    }
    
    // 只有至少 2 个有效 pin 的 net 才构建模型
    if (valid_pin_count < 2) continue;
    
    buildOneNet_(n, module_pos);
  }

  if (logger_) {
    logger_->info(utl::GPL3D, 0,
      "RCTreeBuilder3D: built models for {} nets (cross-layer nets = {}).",
      models_.size(), cross_layer_nets_);
  }
}

const RCNetModel* RCTreeBuilder3D::model(const Net* n) const
{
  auto it = models_.find(n);
  return (it == models_.end()) ? nullptr : &it->second;
}

void RCTreeBuilder3D::buildOneNet_(const Net* n,
                                   const std::unordered_map<Module*, ModulePosition>& mp)
{
  // 收集 pin 的 (x,y,z)，其中 z=tierId
  std::vector<std::tuple<const Pin*, double,double,int>> pin_xyz;
  pin_xyz.reserve(n->netPins.size());

  for (Pin* p : n->netPins) {
    if (!p || !p->module) continue;

    auto it = mp.find(p->module);
    if (it == mp.end()) {
      // 找不到该模块的当前位置，跳过（或者可考虑用初始位置兜底）
      continue;
    }
    const ModulePosition& mpos = it->second;
    const double px = mpos.position.x + p->offset.x; // 你的工程风格：position + offset
    const double py = mpos.position.y + p->offset.y;
    const int    pz = static_cast<int>(mpos.tierId);

    pin_xyz.emplace_back(p, px, py, pz);
  }

  if (pin_xyz.size() < 2) return;

  RCNetModel model;
  detectTSVAndOptional2D_(pin_xyz, model);
  fillRC_(model);

  // 轻量 hint：Σ(R*C) 仅作日志/排序，不影响 STA
  double hint = 0.0;
  for (const auto& s : model.wires) hint += s.R * s.C;
  for (const auto& t : model.tsvs)  hint += t.R * t.C;
  model.elmore_hint = hint;

  // 若包含 TSV，统计一下
  if (!model.tsvs.empty()) ++cross_layer_nets_;

  models_.emplace(n, std::move(model));
}

/// 核心策略：
/// - 如果一个 net 的所有 pin 在同一层 → 无 TSV；且我们**不生成 2D 线段**（2D 交给 OpenROAD）
/// - 如果跨层：
///     * 生成一个 TSV 段，层差 count=|maxZ-minZ|，z_from=minZ，z_to=maxZ；
///     * **不生成 2D wires**（默认 use_openroad_2d_ = true）；
///     * 为了注入时定位参考，我们把 u/v 设为（minZ 层的任意 pin, maxZ 层的任意 pin）
/// - 可选回退：若 use_openroad_2d_ = false，则简单用“均分”策略把 XY 线长分到两侧层，生成 2D wires。
void RCTreeBuilder3D::detectTSVAndOptional2D_(
    const std::vector<std::tuple<const Pin*, double,double,int>>& pin_xyz,
    RCNetModel& out) const
{
  int minZ = std::numeric_limits<int>::max();
  int maxZ = std::numeric_limits<int>::min();
  int minZ_idx = -1, maxZ_idx = -1;

  double sum_xy_len = 0.0; // 仅用于 fallback wires 的估算

  for (int i = 0; i < (int)pin_xyz.size(); ++i) {
    const auto& [pi, xi, yi, zi] = pin_xyz[i];
    if (zi < minZ) { minZ = zi; minZ_idx = i; }
    if (zi > maxZ) { maxZ = zi; maxZ_idx = i; }
  }

  // 估一个简单的 XY 总长（同层间最小生成树成本的粗近似，用星形连到 minZ_idx）
  // 仅在 fallback wires 模式下会用到
  if (!use_openroad_2d_) {
    for (int i = 0; i < (int)pin_xyz.size(); ++i) if (i != minZ_idx) {
      const auto& [pi, xi, yi, zi] = pin_xyz[i];
      const auto& [p0, x0, y0, z0] = pin_xyz[minZ_idx];
      sum_xy_len += manhattan(xi - x0, yi - y0);
    }
  }

  if (minZ == maxZ) {
    // 无 TSV：默认不构建 2D wires（2D 交给 OpenROAD）
    if (!use_openroad_2d_) {
      // fallback：把 sum_xy_len 记在该层
      RCWireSeg s;
      s.u = std::get<0>(pin_xyz[minZ_idx]);
      s.v = std::get<0>(pin_xyz[minZ_idx]); // 自连仅作占位
      s.tier = minZ;
      s.length_um = sum_xy_len;
      out.wires.push_back(s);
    }
    return;
  }

  // 存在 TSV：做一个 net 级 TSV 段（跨越 |Δz|）
  {
    const auto& [pmin, xmin, ymin, zmin] = pin_xyz[minZ_idx];
    const auto& [pmax, xmax, ymax, zmax] = pin_xyz[maxZ_idx];
    RCTsvSeg t;
    t.u = pmin; t.v = pmax;
    t.z_from = zmin; t.z_to = zmax;
    t.count = std::abs(zmax - zmin);
    out.tsvs.push_back(t);
  }

  // 默认仍不生成 2D wires；fallback 模式下，XY 长度均分到两侧层
  if (!use_openroad_2d_) {
    RCWireSeg s1, s2;
    const auto& [pmin, xmin, ymin, zmin] = pin_xyz[minZ_idx];
    const auto& [pmax, xmax, ymax, zmax] = pin_xyz[maxZ_idx];
    s1.u = pmin; s1.v = pmax; s1.tier = zmin; s1.length_um = 0.5 * sum_xy_len;
    s2.u = pmin; s2.v = pmax; s2.tier = zmax; s2.length_um = 0.5 * sum_xy_len;
    out.wires.push_back(s1);
    out.wires.push_back(s2);
  }
}

void RCTreeBuilder3D::fillRC_(RCNetModel& out) const
{
  const int tiers = static_cast<int>(tech_.RperUm.size());

  // 2D 线段（仅在 fallback 时会有）
  for (auto& s : out.wires) {
    const int t = tiers ? std::min(std::max(0, s.tier), tiers - 1) : 0;
    const double Rprime = tech_.RperUm[t];
    const double Cprime = tech_.CperUm[t];
    s.R = Rprime * s.length_um;
    s.C = Cprime * s.length_um;
  }

  // TSV 段
  for (auto& t : out.tsvs) {
    const int cnt = std::max(1, t.count);
    t.R = tech_.Rtsv * cnt;
    t.C = tech_.Ctsv * cnt;
  }
}

} // namespace gpl3d::td
