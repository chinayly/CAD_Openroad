#pragma once
#include <unordered_map>

class Net;
class PlaceDB;

namespace utl { class Logger; }
namespace sta { 
  class dbSta; 
  class Sta;
}

#include "td/Timingtype.h"  // 定义 StaSummary
#include "tcl.h"            // 新增：用于注册 Tcl 命令（双引号）

namespace gpl3d::td {

class TimingOracle {
public:
  // 统一使用 dbSta* 版本
  TimingOracle(PlaceDB* pdb, sta::dbSta* sta, utl::Logger* logger);

  // 只在声明处给默认参数
  // iteration: 当前iteration编号，用于标注slack数据（-1表示不记录iteration）
  StaSummary updateAndReport(bool compute_hold = false, int iteration = -1);

  const std::unordered_map<const ::Net*, double>& netCriticalities() const {
    return net_crit_;
  }

  // 新增：获取指定net的slack
  double getNetSlack(const ::Net* net) const;
  
  // 新增：获取所有net的slack映射
  const std::unordered_map<const ::Net*, double>& netSlacks() const {
    return net_slack_map_;
  }

  // 新增：把 TimingOracle 暴露为 Tcl 命令：gpl3d::td::timing_iteration
  // 由模块初始化处调用一次：oracle->registerTcl(interp);
  void registerTcl(Tcl_Interp* interp);

 private:
  // 计算每个 net 的 criticality（从 STA 提取 slack）
  // iteration: 当前iteration编号，用于标注slack数据
  void computeNetCriticalities(sta::Sta* s, double wns_setup, int iteration = -1);
  PlaceDB*     pdb_{nullptr};
  sta::dbSta*  sta_{nullptr};   // 持有 dbSta 指针
  utl::Logger* logger_{nullptr};

  std::unordered_map<const ::Net*, double> net_crit_;
  std::unordered_map<const ::Net*, double> net_slack_map_;  // 存储net->slack映射
};

} // namespace gpl3d::td
