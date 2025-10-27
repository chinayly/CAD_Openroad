// td/TimingOracle.cpp — use sta::MinMax::max()/min()
#include "td/TimingOracle.h"
#include "utl/Logger.h"

#include "db_sta/dbSta.hh"
#include "sta/Sta.hh"
#include "sta/MinMax.hh"
#include "sta/Units.hh"

#include <cmath>
#include <limits>
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

StaSummary TimingOracle::updateAndReport(bool compute_hold)
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
  s->updateTiming(false /*force*/);

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

  net_crit_.clear();
  return out;
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
