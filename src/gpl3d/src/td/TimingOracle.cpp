// td/TimingOracle.cpp — use sta::MinMax::max()/min()
#include "td/TimingOracle.h"
#include "utl/Logger.h"

#include "db_sta/dbSta.hh"
#include "sta/Sta.hh"
#include "sta/MinMax.hh"
#include "sta/Units.hh"
#include "sta/Network.hh"
#include "placedb.h"

#include <cmath>
#include <limits>
#include <algorithm>
#include "tcl.h"  // 注意用双引号

namespace gpl3d::td {

TimingOracle::TimingOracle(PlaceDB* pdb, sta::dbSta* sta, utl::Logger* logger)
  : pdb_(pdb), sta_(sta), logger_(logger)
{}

static double to_ns(double v_seconds)
{
  if (!std::isfinite(v_seconds)) return 0.0;
  return v_seconds * 1e9;
}

StaSummary TimingOracle::updateAndReport(bool compute_hold, int iteration)
{
  StaSummary out{};
  out.WNS_setup = 0.0;
  out.TNS_setup = 0.0;
  out.WNS_hold  = 0.0;
  out.TNS_hold  = 0.0;

  if (!sta_) {
    if (logger_) logger_->warn(utl::GPL3D, 0, "TimingOracle: dbSta is null.");
    return out;
  }
  sta::Sta* s = sta_->sta();
  if (!s) {
    if (logger_) logger_->warn(utl::GPL3D, 0, "TimingOracle: sta()->sta() is null.");
    return out;
  }

  // 更新一次时序（Tcl侧需先 set_wire_rc / estimate_parasitics -placement）
  // ★ 关键：由于寄生参数已更新，需要强制重新计算所有时序
  // 先使延迟和到达时间失效，然后强制更新
  s->delaysInvalid();
  s->arrivalsInvalid();
  s->updateTiming(true /*force full update*/);

  // ---- setup (max) ----
  {
    const sta::MinMax* max_m = sta::MinMax::max();     // ★ 关键修改
    sta::Slack wns_s = s->worstSlack(max_m);
    sta::Slack tns_s = s->totalNegativeSlack(max_m);
    out.WNS_setup = to_ns(static_cast<double>(wns_s));
    out.TNS_setup = to_ns(static_cast<double>(tns_s));
  }

  // ---- hold (min)（可选）----
  if (compute_hold) {
    const sta::MinMax* min_m = sta::MinMax::min();     // ★ 关键修改
    sta::Slack wns_s = s->worstSlack(min_m);
    sta::Slack tns_s = s->totalNegativeSlack(min_m);
    out.WNS_hold = to_ns(static_cast<double>(wns_s));
    out.TNS_hold = to_ns(static_cast<double>(tns_s));
  }

  auto sane = [&](double v)->bool {
    return std::isfinite(v) && std::fabs(v) < 1e12; // ns 量级保护
  };
  if (!sane(out.WNS_setup) || !sane(out.TNS_setup) ||
      !sane(out.WNS_hold)  || !sane(out.TNS_hold)) {
    if (logger_) {
      logger_->warn(utl::GPL3D, 0,
        "TimingOracle: abnormal STA numbers detected. "
        "Check set_wire_rc / estimate_parasitics -placement / set_propagated_clock.");
    }
    if (!sane(out.WNS_setup)) out.WNS_setup = 0;
    if (!sane(out.TNS_setup)) out.TNS_setup = 0;
    if (!sane(out.WNS_hold))  out.WNS_hold  = 0;
    if (!sane(out.TNS_hold))  out.TNS_hold  = 0;
  }

  if (logger_) {
    if (compute_hold) {
      logger_->info(utl::GPL3D, 0,
        "STA summary: WNS(setup)={:.3f} ns, TNS(setup)={:.3f} ns; "
        "WNS(hold)={:.3f} ns, TNS(hold)={:.3f} ns.",
        out.WNS_setup, out.TNS_setup, out.WNS_hold, out.TNS_hold);
    } else {
      logger_->info(utl::GPL3D, 0,
        "STA summary: WNS(setup)={:.3f} ns, TNS(setup)={:.3f} ns.",
        out.WNS_setup, out.TNS_setup);
    }
  }

  // 计算每个 net 的 criticality 和 slack（用于AI训练数据标注）
  // 如果 WNS 异常，跳过 criticality 计算（避免使用异常值）
  net_crit_.clear();
  net_slack_map_.clear();
  if (sane(out.WNS_setup)) {
    computeNetCriticalities(s, out.WNS_setup, iteration);
  } else {
    if (logger_) {
      logger_->warn(utl::GPL3D, 0,
        "TimingOracle: WNS is abnormal ({} ns), skipping criticality computation.",
        out.WNS_setup);
    }
  }

  return out;
}

void TimingOracle::computeNetCriticalities(sta::Sta* s, double wns_setup, int iteration)
{
  if (!pdb_ || !s) return;

  sta::Network* network = s->network();
  if (!network) return;

  // 检查 WNS 是否异常
  auto sane = [](double v)->bool {
    return std::isfinite(v) && std::fabs(v) < 1e12; // ns 量级保护
  };
  
  const double MAX_VALID_SLACK_NS = 1e6;  // 1ms，足够大的正常值
  
  // 如果 WNS 本身异常，直接返回（不应该发生，因为调用前已检查）
  if (!sane(wns_setup) || std::fabs(wns_setup) > MAX_VALID_SLACK_NS) {
    if (logger_) {
      logger_->warn(utl::GPL3D, 0,
        "TimingOracle::computeNetCriticalities: WNS is abnormal ({} ns), skipping.",
        wns_setup);
    }
    return;
  }

  const sta::MinMax* max_m = sta::MinMax::max();
  const double wns_abs = std::fabs(wns_setup);
  const double denom = wns_abs + 1e-9;  // CRIT_EPS

  int nets_with_slack = 0;

  // 遍历 PlaceDB 中的所有 nets
  for (Net* pdb_net : pdb_->dbNets) {
    if (!pdb_net) continue;

    // 在 STA Network 中查找对应的 net
    const char* net_name = pdb_net->name.c_str();
    sta::Net* sta_net = network->findNet(net_name);
    if (!sta_net) continue;

    // 过滤：跳过 power/ground nets
    if (network->isPower(sta_net) || network->isGround(sta_net)) {
      continue;
    }

    // 过滤：检查 net 是否有 driver 和 load（孤立 cell 的 net 可能没有）
    // 注意：drivers() 返回的 PinSet* 由 Network 管理，不应手动释放
    sta::PinSet* drivers = network->drivers(sta_net);
    bool has_driver = (drivers && !drivers->empty());
    bool has_load = false;
    
    // 检查是否有 load pin
    std::unique_ptr<sta::NetConnectedPinIterator> pin_iter_check{
        network->connectedPinIterator(sta_net)};
    while (pin_iter_check->hasNext()) {
      const sta::Pin* pin = pin_iter_check->next();
      if (network->isLoad(pin)) {
        has_load = true;
        break;
      }
    }
    
    // 跳过没有 driver 或没有 load 的 net（可能是孤立 cell）
    if (!has_driver || !has_load) {
      continue;
    }

    // 获取该 net 的最差 slack（遍历所有连接到该 net 的 pins）
    double worst_slack = std::numeric_limits<double>::max();
    bool found_slack = false;

    std::unique_ptr<sta::NetConnectedPinIterator> pin_iter{
        network->connectedPinIterator(sta_net)};
    while (pin_iter->hasNext()) {
      const sta::Pin* pin = pin_iter->next();
      sta::Slack slack = s->pinSlack(pin, max_m);
      if (slack != sta::INF) {
        double slack_ns = to_ns(static_cast<double>(slack));
        // 过滤异常值：如果 slack 绝对值过大，认为是无效的（可能是孤立 cell 导致的）
        if (std::fabs(slack_ns) < MAX_VALID_SLACK_NS) {
          if (slack_ns < worst_slack) {
            worst_slack = slack_ns;
            found_slack = true;
          }
        }
      }
    }

    // 存储slack值到Net对象和映射表（用于AI训练数据标注）
    if (found_slack) {
      pdb_net->slack = worst_slack;
      pdb_net->iteration = iteration;
      pdb_net->slack_valid = true;
      net_slack_map_[pdb_net] = worst_slack;
      ++nets_with_slack;
    } else {
      // 如果没有找到有效的slack，标记为无效
      pdb_net->slack_valid = false;
    }

    // 计算 criticality: 如果 slack < 0，则 criticality = (-slack) / (|WNS| + eps)
    if (found_slack && worst_slack < 0.0) {
      double crit = (-worst_slack) / denom;
      crit = std::min(crit, 1.0);  // 限制在 [0, 1]
      net_crit_[pdb_net] = crit;
    } else {
      // slack >= 0 的 net，criticality = 0（不存储，默认就是 0）
    }
  }

  if (logger_) {
    logger_->info(utl::GPL3D, 0,
      "Computed criticalities for {} nets, labeled slack for {} nets (WNS={:.3f} ns, iteration={}).",
      net_crit_.size(), nets_with_slack, wns_setup, iteration);
  }
}

double TimingOracle::getNetSlack(const ::Net* net) const
{
  auto it = net_slack_map_.find(net);
  if (it != net_slack_map_.end()) {
    return it->second;
  }
  return 0.0;  // 默认返回0（表示没有slack信息或slack >= 0）
}

// ---------------- Tcl hook ----------------
static int timing_iteration_cmd(ClientData client_data, Tcl_Interp* tcl, int objc, Tcl_Obj* const objv[])
{
  auto* self = reinterpret_cast<TimingOracle*>(client_data);
  if (!self) {
    Tcl_SetResult(tcl, const_cast<char*>("TimingOracle=null"), TCL_STATIC);
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

  StaSummary sum = self->updateAndReport(do_hold);

  Tcl_Obj* dict = Tcl_NewDictObj();
  Tcl_DictObjPut(tcl, dict, Tcl_NewStringObj("WNS_setup",-1), Tcl_NewDoubleObj(sum.WNS_setup));
  Tcl_DictObjPut(tcl, dict, Tcl_NewStringObj("TNS_setup",-1), Tcl_NewDoubleObj(sum.TNS_setup));
  Tcl_DictObjPut(tcl, dict, Tcl_NewStringObj("WNS_hold",-1),  Tcl_NewDoubleObj(sum.WNS_hold));
  Tcl_DictObjPut(tcl, dict, Tcl_NewStringObj("TNS_hold",-1),  Tcl_NewDoubleObj(sum.TNS_hold));
  Tcl_SetObjResult(tcl, dict);
  return TCL_OK;
}

void TimingOracle::registerTcl(Tcl_Interp* interp)
{
  if (!interp) return;
  Tcl_CreateObjCommand(interp,
    "gpl3d::td::timing_iteration",
    timing_iteration_cmd,
    reinterpret_cast<ClientData>(this),
    nullptr);
}

} // namespace gpl3d::td
