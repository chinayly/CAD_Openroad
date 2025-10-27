#pragma once
#include <unordered_map>

class Net;
class PlaceDB;

namespace utl { class Logger; }
namespace sta { class dbSta; }

#include "td/Timingtype.h"  // 定义 StaSummary
#include "tcl.h"            // 新增：用于注册 Tcl 命令（双引号）

namespace gpl3d::td {

class TimingOracle {
public:
  // 统一使用 dbSta* 版本
  TimingOracle(PlaceDB* pdb, sta::dbSta* sta, utl::Logger* logger);

  // 只在声明处给默认参数
  StaSummary updateAndReport(bool compute_hold = false);

  const std::unordered_map<const ::Net*, double>& netCriticalities() const {
    return net_crit_;
  }

  // 新增：把 TimingOracle 暴露为 Tcl 命令：gpl3d::td::timing_iteration
  // 由模块初始化处调用一次：oracle->registerTcl(interp);
  void registerTcl(Tcl_Interp* interp);

private:
  PlaceDB*     pdb_{nullptr};
  sta::dbSta*  sta_{nullptr};   // 持有 dbSta 指针
  utl::Logger* logger_{nullptr};

  std::unordered_map<const ::Net*, double> net_crit_;
};

} // namespace gpl3d::td
