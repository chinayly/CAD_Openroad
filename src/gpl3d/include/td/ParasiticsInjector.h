#pragma once

#include <string>
#include <unordered_map>

#include "utl/Logger.h"
#include "placedb.h"
#include "td/RCTreeBuilder3D.h"

// 仅前向声明 OpenROAD 入口，避免在头文件中暴露 STA 类型
namespace ord { class OpenRoad; }
namespace sta { class dbSta; }
namespace utl { class Logger; }
namespace gpl3d::td {

/// 将（外部已估计的）2D 寄生 与 我们的 TSV(3D) 叠加到 STA 寄生网络。
class ParasiticsInjector {
public:
explicit ParasiticsInjector(ord::OpenRoad* ord, utl::Logger* logger);
  explicit ParasiticsInjector(sta::dbSta* sta, utl::Logger* logger);
  /// 占位：不在 C++ 里直接跑 Tcl（你的环境没有 evalTclString）。
  /// 请在外部 Tcl flow 中执行：
  ///   set_wire_rc <rule/layer>
  ///   estimate_parasitics -placement
  /// 本函数返回 false（表示未在此处估计 2D 寄生）。
  bool estimatePlacementRC(bool do_set_wire_rc = false,
                           const std::string& wire_rc_layer_or_rule = "");

  /// 叠加 TSV（3D）的等效地电容到各个 net。
  /// 返回：叠加的 TSV 总电容（法拉）
  double overlayTSV(const RCTreeBuilder3D& rc3d);

private:
  ord::OpenRoad* ord_   {nullptr};
  sta::dbSta*    sta_{nullptr};
  utl::Logger*   logger_{nullptr};
};

} // namespace gpl3d::td
