#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <functional>

#include "utl/Logger.h"
#include "placedb.h"
#include "td/RCTreeBuilder3D.h"
#include "td/TSVNetMapper.h"

// 仅前向声明 OpenROAD 入口，避免在头文件中暴露 STA 类型
namespace ord { class OpenRoad; }
namespace odb { class dbDatabase; class dbNet; }
namespace stt { struct Tree; struct Branch; }
namespace sta { 
  class dbSta; 
  class Net;
  class Network;
  class Parasitics;
  class ParasiticAnalysisPt;
  class Parasitic;
  class ParasiticNode;
}
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
                           const ::std::string& wire_rc_layer_or_rule = "");

  /// 叠加 TSV（3D）的寄生参数到各个 net 的 Steiner tree 中。
  /// 需要提供 TSVNetMapper 来映射原始 net 和 TSV 信息。
  /// 对于包含 TSV 的原始 net：
  ///   1. 删除 OpenROAD 估计的寄生参数
  ///   2. 分别构建上下两层子 net 的 Steiner tree
  ///   3. 找到两个 Steiner tree 的 bounding box 最相邻的两个端点
  ///   4. 在这两个端点之间插入 TSV（创建中间节点，添加 R 和 C）
  ///   5. 将整个 RC 树构建到 OpenSTA 的寄生网络中
  /// 返回：叠加的 TSV 总电容（法拉）
  double overlayTSV(const TSVNetMapper& tsv_mapper, PlaceDB* pdb, 
                    odb::dbDatabase* odb_db,
                    const std::unordered_map<Module*, ModulePosition>& module_pos);

private:
  /// 为包含 TSV 的原始 net 构建完整的 RC 树（包括上下两层子 net 的 Steiner tree 和 TSV）
  /// 1. 删除 OpenROAD 估计的寄生参数
  /// 2. 分别构建上下两层子 net 的 Steiner tree
  /// 3. 找到两个 Steiner tree 的 bounding box 最相邻的两个端点
  /// 4. 在这两个端点之间插入 TSV
  /// 5. 将整个 RC 树构建到 OpenSTA 的寄生网络中
  bool buildRCTreeWithTSV(sta::Parasitics* parasitics,
                           sta::Network* network,
                           sta::Net* original_net,
                           const std::string& original_net_name,
                           const std::vector<TSVInfo>& tsv_infos,
                           PlaceDB* pdb,
                           odb::dbDatabase* odb_db,
                           const std::unordered_map<Module*, ModulePosition>& module_pos,
                           const sta::ParasiticAnalysisPt* ap);

  /// 为子 net 构建 Steiner tree（使用 OpenROAD 的 SteinerTreeBuilder）
  stt::Tree buildSteinerTreeForSubnet(const Net* subnet,
                                       PlaceDB* pdb,
                                       odb::dbDatabase* odb_db,
                                       const std::unordered_map<Module*, ModulePosition>& module_pos,
                                       int& drvr_index);

  /// 计算 Steiner tree 的 bounding box
  void computeSteinerTreeBBox(const stt::Tree& tree,
                               const std::vector<int>& x,
                               const std::vector<int>& y,
                               int& x_min, int& y_min,
                               int& x_max, int& y_max);

  /// 找到两个 bounding box 最相邻的两个端点
  void findClosestEndpoints(const stt::Tree& tree1,
                            const std::vector<int>& x1,
                            const std::vector<int>& y1,
                            const stt::Tree& tree2,
                            const std::vector<int>& x2,
                            const std::vector<int>& y2,
                            int& closest_x1, int& closest_y1,
                            int& closest_x2, int& closest_y2);

  /// 将 Steiner tree 构建到 OpenSTA 的寄生网络中
  /// 返回：位置 (x, y) -> 寄生节点的映射，用于后续查找节点
  ::std::unordered_map<::std::pair<int, int>, sta::ParasiticNode*> 
  buildParasiticNetworkFromSteinerTree(sta::Parasitics* parasitics,
                                       sta::Network* network,
                                       sta::Parasitic* parasitic_net,
                                       const stt::Tree& tree,
                                       const std::vector<int>& x,
                                       const std::vector<int>& y,
                                       int drvr_index,
                                       const Net* subnet,
                                       PlaceDB* pdb,
                                       const std::unordered_map<Module*, ModulePosition>& module_pos,
                                       double RperUm, double CperUm);

  /// 在寄生网络的两个节点之间插入 TSV（创建中间节点，添加 R 和 C）
  void insertTSVBetweenNodes(sta::Parasitics* parasitics,
                               sta::Network* network,
                               sta::Parasitic* parasitic_net,
                               sta::ParasiticNode* node1,
                               sta::ParasiticNode* node2,
                               double tsv_r, double tsv_c,
                               int tsv_x, int tsv_y);

  /// 从 PlaceDB 的 Net 获取 pin 位置（转换为 DBU 单位）
  void getSubnetPinPositions(const Net* subnet,
                              PlaceDB* pdb,
                              odb::dbDatabase* odb_db,
                              const std::unordered_map<Module*, ModulePosition>& module_pos,
                              std::vector<int>& x,
                              std::vector<int>& y,
                              std::vector<const Pin*>& pins);

private:
  ord::OpenRoad* ord_   {nullptr};
  sta::dbSta*    sta_{nullptr};
  utl::Logger*   logger_{nullptr};
  
  // TSV 技术参数
  double Rtsv_ {0.03};      // 30 mΩ
  double Ctsv_ {20e-15};   // 20 fF
  double RperUm_ {0.08};   // 0.08 Ω/μm
  double CperUm_ {0.20e-15}; // 0.20 fF/μm
  
  // 记录每个 net 在每个 analysis point 上已构建的 RC 树，避免重复构建
  ::std::unordered_map<const sta::Net*, ::std::unordered_map<const sta::ParasiticAnalysisPt*, bool>> built_rc_trees_;

}; // class ParasiticsInjector

} // namespace gpl3d::td
