// —— 只在实现文件里包含 OpenROAD / OpenSTA 头，避免头文件传播 —— //
#include "db_sta/dbSta.hh"  // dbSta（OpenROAD里 getSta() 的具体类型）
#include "ord/OpenRoad.hh"
#include "sta/Corner.hh"      // Corner
#include "sta/MinMax.hh"      // MinMax::max()/min()
#include "sta/Network.hh"     // Network / Net
#include "sta/Parasitics.hh"  // Parasitics / Parasitic / ParasiticNode
#include "sta/Sta.hh"         // Sta 基类接口
#include "stt/SteinerTreeBuilder.h"  // Steiner tree builder
#include "odb/db.h"           // OpenDB types
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <limits>
#include <functional>

// 注意：placedb.h 包含 global.h，其中有 using namespace std;
// 为了避免命名空间污染，我们在全局命名空间内包含它
#include "td/ParasiticsInjector.h"

// Hash function for std::pair<int, int> (needed for unordered_map)
namespace std {
template<>
struct hash<pair<int, int>> {
  size_t operator()(const pair<int, int>& p) const {
    return hash<int>()(p.first) ^ (hash<int>()(p.second) << 1);
  }
};
}

// 在全局命名空间内定义辅助函数，避免 std 命名空间解析问题
namespace {

// 去掉 TSV 派生后缀；兼容 "__TSV_" 或 "_TSV_"；顺便把 "\[" "\]" 还原成 "[" "]"
static std::string sanitizeNetName(const std::string& raw)
{
  std::string s = raw;

  // 1) 去掉 TSV 派生尾缀：从最后一个 "_TSV_" 起把后面全砍掉
  size_t pos = s.rfind("_TSV_");
  if (pos != std::string::npos) {
    s.erase(pos);
  }

  // 2) 去掉对 [] 的转义
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()
        && (s[i + 1] == '[' || s[i + 1] == ']')) {
      continue;  // skip backslash
    }
    out.push_back(s[i]);
  }
  return out;
}

// 添加转义字符到方括号（用于 STA 查找）
static std::string escapeBrackets(const std::string& s)
{
  std::string out;
  out.reserve(s.size() + 10);  // 预留一些空间
  for (char ch : s) {
    if (ch == '[' || ch == ']') {
      out += '\\';
    }
    out += ch;
  }
  return out;
}

// 基于原名/raw 与顶层实例名，生成一组可能的 STA 可识别候选
static std::vector<std::string> genCandidates(const std::string& raw,
                                              const char* top_inst_name)
{
  std::vector<std::string> cand;
  cand.reserve(16);  // 增加容量，因为现在会有更多候选

  const std::string cleaned = sanitizeNetName(raw);
  const bool has_top = (top_inst_name && *top_inst_name);

  // 生成候选列表的函数
  auto addCandidates = [&](const std::string& base_name) {
    // 1) 原样（不带转义）
    cand.push_back(base_name);
    
    // 2) 带转义的方括号（如果包含方括号）
    if (base_name.find('[') != std::string::npos || 
        base_name.find(']') != std::string::npos) {
      cand.push_back(escapeBrackets(base_name));
    }
  };

  // a) cleaned 原样（及转义版本）
  addCandidates(cleaned);

  // b) 叶子名（去掉层次路径，只保留最后一个 '/' 之后）
  auto last_slash = cleaned.rfind('/');
  if (last_slash != std::string::npos && last_slash + 1 < cleaned.size()) {
    std::string leaf = cleaned.substr(last_slash + 1);
    addCandidates(leaf);
  }

  // c) 顶层前缀 + cleaned
  if (has_top) {
    std::string with_top = std::string(top_inst_name) + "/" + cleaned;
    addCandidates(with_top);
  }
  
  // d) 顶层前缀 + 叶子
  if (has_top && last_slash != std::string::npos
      && last_slash + 1 < cleaned.size()) {
    std::string top_leaf = std::string(top_inst_name) + "/"
                           + cleaned.substr(last_slash + 1);
    addCandidates(top_leaf);
  }

  // e) 把层次分隔符 '/' 改成 '|'（有的读入风格会这样）
  {
    auto to_bar = [&](const std::string& s) {
      std::string result = s;
      for (char& ch : result)
        if (ch == '/')
          ch = '|';
      return result;
    };
    addCandidates(to_bar(cleaned));
    if (has_top) {
      addCandidates(to_bar(std::string(top_inst_name) + "/" + cleaned));
    }
    if (last_slash != std::string::npos && last_slash + 1 < cleaned.size()) {
      addCandidates(to_bar(cleaned.substr(last_slash + 1)));
      if (has_top) {
        addCandidates(to_bar(std::string(top_inst_name) + "/"
                              + cleaned.substr(last_slash + 1)));
      }
    }
  }

  // 去重（简单 O(n^2) 即可）
  std::vector<std::string> uniq;
  uniq.reserve(cand.size());
  for (auto& c : cand) {
    bool dup = false;
    for (auto& u : uniq)
      if (u == c) {
        dup = true;
        break;
      }
    if (!dup && !c.empty())
      uniq.push_back(std::move(c));
  }
  return uniq;
}

}  // anonymous namespace
namespace gpl3d::td {

ParasiticsInjector::ParasiticsInjector(sta::dbSta* sta, utl::Logger* logger)
    : ord_(nullptr), sta_(sta), logger_(logger)
{
}

// 建议仍在 Tcl flow 里做 2D 估计（这里不直接跑 Tcl）
bool ParasiticsInjector::estimatePlacementRC(
    bool /*do_set_wire_rc*/,
    const ::std::string& /*wire_rc_layer_or_rule*/)
{
  if (logger_) {
    logger_->warn(::utl::GPL3D,
                  0,
                  "estimatePlacementRC: skipped here (run 'set_wire_rc' & "
                  "'estimate_parasitics -placement' in Tcl).");
  }
  return false;
}

// 把 TSV 的寄生参数叠加到原始 net 的 Steiner tree 中
double ParasiticsInjector::overlayTSV(const TSVNetMapper& tsv_mapper, PlaceDB* pdb,
                                       odb::dbDatabase* odb_db,
                                       const std::unordered_map<Module*, ModulePosition>& module_pos)
{
  // 1) OpenSTA 句柄
  sta::dbSta* sta = sta_ ? sta_ : (ord_ ? ord_->getSta() : nullptr);
  if (!sta)
    return 0.0;

  sta::Parasitics* parasitics = sta->parasitics();
  sta::Network* network = sta->network();
  if (!parasitics || !network || !odb_db)
    return 0.0;

  // 确保 Corner 上有分析点（OpenSTA 里需要先开启）
  sta->setParasiticAnalysisPts(true);

  // 我们同时对 max/min 两个视图叠加（若你只想 setup，可仅用 Max）
  const sta::MinMax* views[2] = {sta::MinMax::max(), sta::MinMax::min()};

  // 清空缓存，因为布局位置可能已经改变，需要重新构建 RC 树
  built_rc_trees_.clear();

  double totalC = 0.0;
  int nets_rebuilt = 0;
  // 用于跟踪已经统计过 TSV 电容的原始 net（避免在 min/max 视图中重复统计）
  ::std::unordered_set<const sta::Net*> nets_counted_for_cap;

  // 2) 遍历所有原始 net，找到相关的 TSV 并注入
  // 从 PlaceDB 中收集所有唯一的原始 net 名称
  ::std::unordered_set<::std::string> original_net_names;
  for (Net* net : pdb->dbNets) {
    if (!net) continue;
    if (!net->originalNetName.empty() && TSVNetMapper::isSubnet(net->name)) {
      original_net_names.insert(net->originalNetName);
    }
  }
  
  if (logger_) {
    logger_->info(::utl::GPL3D, 0,
      "overlayTSV: found {} unique original nets with TSVs", original_net_names.size());
  }

  // 3) 对每个 corner、每个视图的 analysis point 注入 TSV
  for (sta::Corner* corner : *sta->corners()) {
    for (const sta::MinMax* mm : views) {
      const sta::ParasiticAnalysisPt* ap = corner->findParasiticAnalysisPt(mm);
      if (ap == nullptr)
        continue;  // 该视图没开就跳过

      // 4) 对每个原始 net，查找相关的 TSV 并注入
      for (const ::std::string& original_net_name : original_net_names) {
        const auto& tsv_infos = tsv_mapper.getTSVInfos(original_net_name);
        if (tsv_infos.empty()) {
          if (logger_ && logger_->debugCheck(::utl::GPL3D, "tsv", 1)) {
            logger_->warn(::utl::GPL3D, 0,
              "overlayTSV: no TSV info for original net '{}'", original_net_name);
          }
          continue;
        }

        // 在 OpenSTA 中找到原始 net
        const char* top_name = nullptr;
        if (network && network->topInstance()) {
          top_name = network->name(network->topInstance());
        }

        sta::Net* original_net = nullptr;
        auto candidates = genCandidates(original_net_name, top_name);
        for (const auto& c : candidates) {
          original_net = network->findNet(c.c_str());
          if (original_net)
            break;
        }

        if (!original_net) {
          if (logger_) {
            logger_->warn(::utl::GPL3D, 0,
              "overlayTSV: original net '{}' not found in STA", original_net_name);
          }
          continue;
        }

        // 过滤：跳过 power/ground nets
        if (network->isPower(original_net) || network->isGround(original_net)) {
          continue;
        }

        // 检查是否已经构建过 RC 树（避免重复）
        if (built_rc_trees_[original_net][ap]) {
          continue;
        }

        // 5) 为包含 TSV 的原始 net 构建完整的 RC 树
        // 添加调试信息：检查问题 net 是否被识别为包含 TSV
        bool is_problem_net = (original_net_name.find("_0582_") != std::string::npos ||
                               original_net_name.find("fe_cmd_fifo.mem_1r1w.r_v_i") != std::string::npos);
        if (is_problem_net && logger_) {
          logger_->warn(::utl::GPL3D, 0,
            "overlayTSV: processing potential problem net '{}' with {} TSV(s)",
            original_net_name, tsv_infos.size());
        }
        
        if (buildRCTreeWithTSV(parasitics, network, original_net, original_net_name,
                               tsv_infos, pdb, odb_db, module_pos, ap)) {
          built_rc_trees_[original_net][ap] = true;
          // 计算总 TSV 电容（每个 TSV 只计算一次，避免在 min/max 视图中重复统计）
          // 每个 TSV 连接相邻的两层（k 到 k+1），所以每个 TSV 的电容就是 Ctsv_
          if (nets_counted_for_cap.find(original_net) == nets_counted_for_cap.end()) {
            // 每个 TSV 只连接相邻的两层，所以每个 TSV 的电容就是 Ctsv_
            totalC += Ctsv_ * tsv_infos.size();
            nets_counted_for_cap.insert(original_net);
          }
          ++nets_rebuilt;
          if (is_problem_net && logger_) {
            logger_->warn(::utl::GPL3D, 0,
              "overlayTSV: successfully built RC tree for problem net '{}'",
              original_net_name);
          }
        } else {
          // buildRCTreeWithTSV 失败：寄生参数已被删除，需要重新估计
          // 注意：由于我们已经删除了寄生参数，如果重建失败，该 net 将没有寄生参数
          // 这可能导致延迟计算异常。理想情况下应该重新估计，但这需要 wireload 信息
          if (logger_) {
            logger_->warn(::utl::GPL3D, 0,
              "overlayTSV: buildRCTreeWithTSV failed for net '{}' (parasitics deleted, may cause timing issues)",
              original_net_name);
          }
          if (is_problem_net && logger_) {
            logger_->warn(::utl::GPL3D, 0,
              "overlayTSV: PROBLEM NET '{}' build failed! This may cause abnormal WNS.",
              original_net_name);
          }
        }
      }  // original nets
    }  // views (min/max)
  }  // corners

  if (logger_) {
    logger_->info(::utl::GPL3D,
                  0,
                  "overlayTSV done: nets_rebuilt={}, total TSV C = {:.3f} fF (min/max "
                  "for all corners)",
                  nets_rebuilt,
                  totalC * 1e15);
  }
  return totalC;
}

// 为包含 TSV 的原始 net 构建完整的 RC 树
bool ParasiticsInjector::buildRCTreeWithTSV(
    sta::Parasitics* parasitics,
    sta::Network* network,
    sta::Net* original_net,
    const std::string& original_net_name,
    const std::vector<TSVInfo>& tsv_infos,
    PlaceDB* pdb,
    odb::dbDatabase* odb_db,
    const std::unordered_map<Module*, ModulePosition>& module_pos,
    const sta::ParasiticAnalysisPt* ap)
{
  bool is_problem_net = (original_net_name.find("_0582_") != std::string::npos ||
                         original_net_name.find("fe_cmd_fifo.mem_1r1w.r_v_i") != std::string::npos);
  
  if (!parasitics || !network || !original_net || tsv_infos.empty() || !pdb || !odb_db) {
    if (is_problem_net && logger_) {
      logger_->warn(::utl::GPL3D, 0,
        "buildRCTreeWithTSV: invalid parameters for problem net '{}'",
        original_net_name);
    }
    return false;
  }

  // 1. 删除 OpenROAD 估计的寄生参数
  if (is_problem_net && logger_) {
    logger_->warn(::utl::GPL3D, 0,
      "buildRCTreeWithTSV: deleting parasitics for problem net '{}'",
      original_net_name);
  }
  parasitics->deleteParasiticNetworks(original_net);

  // 2. 创建新的寄生网络
  sta::Parasitic* pnet = parasitics->makeParasiticNetwork(original_net,
                                                          /*includes_pin_caps=*/true,
                                                          ap);
  if (!pnet) {
    if (is_problem_net && logger_) {
      logger_->warn(::utl::GPL3D, 0,
        "buildRCTreeWithTSV: failed to create parasitic network for problem net '{}'",
        original_net_name);
    }
    return false;
  }

  // 3. 获取 SteinerTreeBuilder（从 OpenRoad 获取）
  stt::SteinerTreeBuilder* stt_builder = nullptr;
  if (ord_) {
    stt_builder = ord_->getSteinerTreeBuilder();
  } else {
    // 如果 ord_ 是 nullptr，尝试从全局 OpenRoad 实例获取
    ord::OpenRoad* openroad = ord::OpenRoad::openRoad();
    if (openroad) {
      stt_builder = openroad->getSteinerTreeBuilder();
    }
  }
  if (!stt_builder) {
    if (logger_) {
      logger_->warn(::utl::GPL3D, 0,
        "buildRCTreeWithTSV: SteinerTreeBuilder not available for net '{}'",
        original_net_name);
    }
    if (is_problem_net && logger_) {
      logger_->warn(::utl::GPL3D, 0,
        "buildRCTreeWithTSV: PROBLEM NET '{}' failed at SteinerTreeBuilder step!",
        original_net_name);
    }
    return false;
  }

  // 4. 对每个 TSV，分别构建上下两层子 net 的 Steiner tree
  // 然后找到最相邻的端点并插入 TSV
  // 注意：这里简化实现，假设每个原始 net 只有一个 TSV
  // 如果有多个 TSV，需要更复杂的处理
  if (tsv_infos.size() > 1 && logger_) {
    logger_->warn(::utl::GPL3D, 0,
      "buildRCTreeWithTSV: net '{}' has {} TSVs, using first one only",
      original_net_name, tsv_infos.size());
  }

  const TSVInfo& tsv_info = tsv_infos[0];
  if (!tsv_info.lower_net || !tsv_info.upper_net) {
    if (logger_) {
      logger_->warn(::utl::GPL3D, 0,
        "buildRCTreeWithTSV: TSV info incomplete for net '{}' (lower_net={}, upper_net={})",
        original_net_name,
        tsv_info.lower_net ? tsv_info.lower_net->name.c_str() : "null",
        tsv_info.upper_net ? tsv_info.upper_net->name.c_str() : "null");
    }
    if (is_problem_net && logger_) {
      logger_->warn(::utl::GPL3D, 0,
        "buildRCTreeWithTSV: PROBLEM NET '{}' failed at TSV info check!",
        original_net_name);
    }
    return false;
  }

  // 5. 构建下层子 net 的 Steiner tree
  int drvr_index_lower = 0;
  stt::Tree tree_lower = buildSteinerTreeForSubnet(tsv_info.lower_net, pdb, odb_db,
                                                    module_pos, drvr_index_lower);
  if (tree_lower.branch.empty()) {
    if (logger_) {
      logger_->warn(::utl::GPL3D, 0,
        "buildRCTreeWithTSV: failed to build Steiner tree for lower net '{}'",
        tsv_info.lower_net ? tsv_info.lower_net->name.c_str() : "null");
    }
    return false;
  }
  
  // 6. 构建上层子 net 的 Steiner tree
  int drvr_index_upper = 0;
  stt::Tree tree_upper = buildSteinerTreeForSubnet(tsv_info.upper_net, pdb, odb_db,
                                                    module_pos, drvr_index_upper);
  if (tree_upper.branch.empty()) {
    if (logger_) {
      logger_->warn(::utl::GPL3D, 0,
        "buildRCTreeWithTSV: failed to build Steiner tree for upper net '{}'",
        tsv_info.upper_net ? tsv_info.upper_net->name.c_str() : "null");
    }
    return false;
  }

  // 7. 获取 pin 位置（用于计算 bounding box）
  std::vector<int> x_lower, y_lower, x_upper, y_upper;
  std::vector<const Pin*> pins_lower, pins_upper;
  getSubnetPinPositions(tsv_info.lower_net, pdb, odb_db, module_pos,
                        x_lower, y_lower, pins_lower);
  getSubnetPinPositions(tsv_info.upper_net, pdb, odb_db, module_pos,
                        x_upper, y_upper, pins_upper);

  // 8. 找到两个 Steiner tree 的 bounding box 最相邻的两个端点
  int closest_x1 = 0, closest_y1 = 0, closest_x2 = 0, closest_y2 = 0;
  findClosestEndpoints(tree_lower, x_lower, y_lower,
                       tree_upper, x_upper, y_upper,
                       closest_x1, closest_y1, closest_x2, closest_y2);

  // 9. 将下层 Steiner tree 构建到寄生网络，并获取位置到节点的映射
  auto lower_pos_to_node = buildParasiticNetworkFromSteinerTree(
      parasitics, network, pnet,
      tree_lower, x_lower, y_lower, drvr_index_lower,
      tsv_info.lower_net, pdb, module_pos,
      RperUm_, CperUm_);

  // 10. 将上层 Steiner tree 构建到寄生网络，并获取位置到节点的映射
  auto upper_pos_to_node = buildParasiticNetworkFromSteinerTree(
      parasitics, network, pnet,
      tree_upper, x_upper, y_upper, drvr_index_upper,
      tsv_info.upper_net, pdb, module_pos,
      RperUm_, CperUm_);

  // 11. 在两个最相邻的端点之间插入 TSV
  // 首先找到这两个端点对应的寄生节点
  sta::ParasiticNode* node1 = nullptr;
  sta::ParasiticNode* node2 = nullptr;
  
  // 在下层 tree 的位置映射中找到 closest_x1, closest_y1 对应的节点
  auto it1 = lower_pos_to_node.find({closest_x1, closest_y1});
  if (it1 != lower_pos_to_node.end()) {
    node1 = it1->second;
  } else {
    // 如果精确匹配失败，尝试找最接近的节点（允许小的误差）
    for (const auto& kv : lower_pos_to_node) {
      int dx = ::std::abs(kv.first.first - closest_x1);
      int dy = ::std::abs(kv.first.second - closest_y1);
      if (dx < 10 && dy < 10) {  // 允许 10 DBU 的误差
        node1 = kv.second;
        break;
      }
    }
  }
  
  // 在上层 tree 的位置映射中找到 closest_x2, closest_y2 对应的节点
  auto it2 = upper_pos_to_node.find({closest_x2, closest_y2});
  if (it2 != upper_pos_to_node.end()) {
    node2 = it2->second;
  } else {
    // 如果精确匹配失败，尝试找最接近的节点（允许小的误差）
    for (const auto& kv : upper_pos_to_node) {
      int dx = ::std::abs(kv.first.first - closest_x2);
      int dy = ::std::abs(kv.first.second - closest_y2);
      if (dx < 10 && dy < 10) {  // 允许 10 DBU 的误差
        node2 = kv.second;
        break;
      }
    }
  }
  
  // 12. 计算 TSV 参数
  // 每个 TSV 连接相邻的两层（k 到 k+1），所以每个 TSV 的 R 和 C 就是 Rtsv_ 和 Ctsv_
  // tier_to - tier_from 应该总是 1，不需要乘以这个值
  double tsv_r = Rtsv_;
  double tsv_c = Ctsv_;
  
  // 13. 插入 TSV（如果找到了两个节点）
  if (node1 && node2) {
    int tsv_x = (closest_x1 + closest_x2) / 2;
    int tsv_y = (closest_y1 + closest_y2) / 2;
    insertTSVBetweenNodes(parasitics, network, pnet, node1, node2,
                         tsv_r, tsv_c, tsv_x, tsv_y);
  } else {
    if (logger_) {
      logger_->warn(::utl::GPL3D, 0,
        "buildRCTreeWithTSV: cannot find nodes for TSV insertion in net '{}' (node1={}, node2={})",
        original_net_name, node1 ? "found" : "null", node2 ? "found" : "null");
    }
    // 即使找不到 TSV 插入点，RC 树已经构建完成，仍然返回 true
    // 因为上下两层子 net 的 Steiner tree 已经构建到寄生网络中了
  }

  // 14. 验证寄生网络是否有效（检查是否有电阻和电容）
  // 如果 RC 树为空，说明构建失败，需要恢复原始寄生参数
  sta::ParasiticResistorSeq resistors = parasitics->resistors(pnet);
  if (resistors.empty()) {
    if (logger_) {
      logger_->warn(::utl::GPL3D, 0,
        "buildRCTreeWithTSV: parasitic network for net '{}' has no resistors, build may have failed",
        original_net_name);
    }
    // 删除空的寄生网络，让 OpenROAD 重新估计
    parasitics->deleteParasiticNetworks(original_net);
    return false;
  }

  return true;
}

// 为子 net 构建 Steiner tree（使用 OpenROAD 的 SteinerTreeBuilder）
stt::Tree ParasiticsInjector::buildSteinerTreeForSubnet(
    const Net* subnet,
    PlaceDB* pdb,
    odb::dbDatabase* odb_db,
    const std::unordered_map<Module*, ModulePosition>& module_pos,
    int& drvr_index)
{
  // 获取 pin 位置
  std::vector<int> x, y;
  std::vector<const Pin*> pins;
  getSubnetPinPositions(subnet, pdb, odb_db, module_pos, x, y, pins);

  if (x.size() < 2) {
    // 如果 pin 数量少于 2，返回空 tree
    return stt::Tree{0, 0, {}};
  }

  // 找到 driver pin（第一个 output pin）
  drvr_index = 0;
  for (size_t i = 0; i < pins.size(); ++i) {
    if (pins[i] && pins[i]->direction == PIN_DIRECTION_OUT) {
      drvr_index = static_cast<int>(i);
      break;
    }
  }

  // 获取 SteinerTreeBuilder
  stt::SteinerTreeBuilder* stt_builder = nullptr;
  if (ord_) {
    stt_builder = ord_->getSteinerTreeBuilder();
  } else {
    // 如果 ord_ 是 nullptr，尝试从全局 OpenRoad 实例获取
    ord::OpenRoad* openroad = ord::OpenRoad::openRoad();
    if (openroad) {
      stt_builder = openroad->getSteinerTreeBuilder();
    }
  }
  if (!stt_builder) {
    return stt::Tree{0, 0, {}};
  }

  // 构建 Steiner tree
  return stt_builder->makeSteinerTree(x, y, drvr_index);
}

// 计算 Steiner tree 的 bounding box
void ParasiticsInjector::computeSteinerTreeBBox(
    const stt::Tree& tree,
    const std::vector<int>& x,
    const std::vector<int>& y,
    int& x_min, int& y_min,
    int& x_max, int& y_max)
{
  if (x.empty() || y.empty()) {
    x_min = y_min = x_max = y_max = 0;
    return;
  }

  x_min = x_max = x[0];
  y_min = y_max = y[0];

  for (size_t i = 1; i < x.size(); ++i) {
    if (x[i] < x_min) x_min = x[i];
    if (x[i] > x_max) x_max = x[i];
    if (y[i] < y_min) y_min = y[i];
    if (y[i] > y_max) y_max = y[i];
  }

  // 也考虑 Steiner tree 的分支点
  for (const auto& branch : tree.branch) {
    if (branch.x < x_min) x_min = branch.x;
    if (branch.x > x_max) x_max = branch.x;
    if (branch.y < y_min) y_min = branch.y;
    if (branch.y > y_max) y_max = branch.y;
  }
}

// 找到两个 bounding box 最相邻的两个端点
void ParasiticsInjector::findClosestEndpoints(
    const stt::Tree& tree1,
    const std::vector<int>& x1,
    const std::vector<int>& y1,
    const stt::Tree& tree2,
    const std::vector<int>& x2,
    const std::vector<int>& y2,
    int& closest_x1, int& closest_y1,
    int& closest_x2, int& closest_y2)
{
  // 计算两个 tree 的 bounding box
  int x1_min, y1_min, x1_max, y1_max;
  int x2_min, y2_min, x2_max, y2_max;
  computeSteinerTreeBBox(tree1, x1, y1, x1_min, y1_min, x1_max, y1_max);
  computeSteinerTreeBBox(tree2, x2, y2, x2_min, y2_min, x2_max, y2_max);

  // 找到两个 bounding box 最相邻的两个端点
  // 策略：找到两个 bounding box 之间距离最近的两个点
  double min_dist = std::numeric_limits<double>::max();
  closest_x1 = closest_y1 = closest_x2 = closest_y2 = 0;

  // 遍历 tree1 的所有点（包括 pin 和 Steiner 点）
  std::vector<std::pair<int, int>> points1;
  for (size_t i = 0; i < x1.size(); ++i) {
    points1.push_back({x1[i], y1[i]});
  }
  for (const auto& branch : tree1.branch) {
    points1.push_back({branch.x, branch.y});
  }

  // 遍历 tree2 的所有点
  std::vector<std::pair<int, int>> points2;
  for (size_t i = 0; i < x2.size(); ++i) {
    points2.push_back({x2[i], y2[i]});
  }
  for (const auto& branch : tree2.branch) {
    points2.push_back({branch.x, branch.y});
  }

  // 找到距离最近的两个点
  for (const auto& p1 : points1) {
    for (const auto& p2 : points2) {
      double dx = static_cast<double>(p1.first - p2.first);
      double dy = static_cast<double>(p1.second - p2.second);
      double dist = std::sqrt(dx * dx + dy * dy);
      if (dist < min_dist) {
        min_dist = dist;
        closest_x1 = p1.first;
        closest_y1 = p1.second;
        closest_x2 = p2.first;
        closest_y2 = p2.second;
      }
    }
  }
}

// 将 Steiner tree 构建到 OpenSTA 的寄生网络中
::std::unordered_map<::std::pair<int, int>, sta::ParasiticNode*> 
ParasiticsInjector::buildParasiticNetworkFromSteinerTree(
    sta::Parasitics* parasitics,
    sta::Network* network,
    sta::Parasitic* parasitic_net,
    const stt::Tree& tree,
    const std::vector<int>& x,
    const std::vector<int>& y,
    int drvr_index,
    const Net* subnet,
    PlaceDB* pdb,
    const std::unordered_map<Module*, ModulePosition>& module_pos,
    double RperUm, double CperUm)
{
  ::std::unordered_map<::std::pair<int, int>, sta::ParasiticNode*> pos_to_node_map;
  
  if (!parasitics || !network || !parasitic_net || !subnet || x.size() != y.size())
    return pos_to_node_map;

  const sta::Net* sta_net = parasitics->net(parasitic_net);
  if (!sta_net)
    return pos_to_node_map;

  // 1. 创建节点映射：位置 (x,y) -> 寄生节点
  // 使用位置作为键，确保相同位置的节点只创建一次
  ::std::unordered_map<::std::pair<int, int>, sta::ParasiticNode*> pos_to_node;
  ::std::unordered_map<int, sta::ParasiticNode*> branch_to_node;  // 分支索引 -> 节点（用于快速查找）

  // 2. 为每个分支点创建寄生节点（按位置去重）
  for (int i = 0; i < static_cast<int>(tree.branch.size()); ++i) {
    const stt::Branch& branch = tree.branch[i];
    ::std::pair<int, int> pos = {branch.x, branch.y};
    
    // 如果这个位置还没有节点，创建一个
    if (pos_to_node.find(pos) == pos_to_node.end()) {
      sta::ParasiticNode* node = nullptr;
      
      // 对于前 deg 个分支（pin 节点），尝试连接到 STA pin
      if (i < tree.deg && i < static_cast<int>(x.size()) && i < static_cast<int>(subnet->netPins.size())) {
        // 使用 pin 索引作为 id
        node = parasitics->ensureParasiticNode(parasitic_net, sta_net, i, network);
      } else {
        // 对于 Steiner 点，使用分支索引作为 id
        node = parasitics->ensureParasiticNode(parasitic_net, sta_net, i, network);
      }
      
      pos_to_node[pos] = node;
      branch_to_node[i] = node;
    } else {
      // 如果位置已存在，复用节点
      branch_to_node[i] = pos_to_node[pos];
    }
  }

  // 3. 遍历所有分支，创建电阻和电容
  // 使用集合跟踪已创建的边，避免重复
  ::std::unordered_set<::std::pair<int, int>> created_edges;
  size_t resistor_id = 1;
  for (int i = 0; i < static_cast<int>(tree.branch.size()); ++i) {
    const stt::Branch& branch = tree.branch[i];
    int neighbor_idx = branch.n;
    
    // 跳过自环（neighbor == self）
    if (neighbor_idx == i)
      continue;

    // 获取两个节点
    sta::ParasiticNode* n1 = branch_to_node[i];
    sta::ParasiticNode* n2 = branch_to_node[neighbor_idx];
    
    if (!n1 || !n2) {
      if (logger_ && logger_->debugCheck(::utl::GPL3D, "tsv", 2)) {
        logger_->warn(::utl::GPL3D, 0,
          "buildParasiticNetworkFromSteinerTree: missing node for branch {} -> {}",
          i, neighbor_idx);
      }
      continue;
    }
    
    // 检查是否已经创建了这条边（避免重复）
    // 使用较小的索引作为第一个元素，确保无向边的唯一表示
    int idx1 = (i < neighbor_idx) ? i : neighbor_idx;
    int idx2 = (i < neighbor_idx) ? neighbor_idx : i;
    ::std::pair<int, int> edge_key = {idx1, idx2};
    
    if (created_edges.find(edge_key) != created_edges.end()) {
      continue;  // 边已创建，跳过
    }
    created_edges.insert(edge_key);

    // 计算分支长度（Manhattan 距离，单位：DBU）
    int dx = ::std::abs(branch.x - tree.branch[neighbor_idx].x);
    int dy = ::std::abs(branch.y - tree.branch[neighbor_idx].y);
    int wire_length_dbu = dx + dy;

    if (wire_length_dbu == 0) {
      // 零长度分支（可能是同一个位置），使用很小的电阻保持连通性
      parasitics->makeResistor(parasitic_net, resistor_id++, 1.0e-3, n1, n2);
      continue;
    }

    // 计算 RC 参数
    // 将 DBU 转换为微米
    odb::dbBlock* block = nullptr;
    if (ord_) {
      odb::dbDatabase* odb_db = ord_->getDb();
      if (odb_db && odb_db->getChip()) {
        block = odb_db->getChip()->getBlock();
      }
    }
    
    double length_um = 0.0;
    if (block) {
      const int dbu_per_micron = block->getDbUnitsPerMicron();
      length_um = static_cast<double>(wire_length_dbu) / static_cast<double>(dbu_per_micron);
    } else {
      // 如果没有 block，假设 DBU = 1000 per micron（常见值）
      length_um = static_cast<double>(wire_length_dbu) / 1000.0;
    }

    // 计算电阻和电容
    // RperUm 和 CperUm 的单位是 Ω/μm 和 F/μm
    double res = length_um * RperUm;  // 欧姆
    double cap = length_um * CperUm;  // 法拉

    // 创建 Pi 模型：将电容的一半放在起点，一半放在终点，中间是电阻
    parasitics->incrCap(n1, static_cast<float>(cap * 0.5));
    parasitics->makeResistor(parasitic_net, resistor_id++, static_cast<float>(res), n1, n2);
    parasitics->incrCap(n2, static_cast<float>(cap * 0.5));
  }

  // 4. 记录位置到节点的映射（用于后续 TSV 插入）
  for (const auto& kv : pos_to_node) {
    pos_to_node_map[kv.first] = kv.second;
  }
  
  return pos_to_node_map;
}

// 在寄生网络的两个节点之间插入 TSV（创建中间节点，添加 R 和 C）
void ParasiticsInjector::insertTSVBetweenNodes(
    sta::Parasitics* parasitics,
    sta::Network* network,
    sta::Parasitic* parasitic_net,
    sta::ParasiticNode* node1,
    sta::ParasiticNode* node2,
    double tsv_r, double tsv_c,
    int tsv_x, int tsv_y)
{
  if (!parasitics || !network || !parasitic_net || !node1 || !node2)
    return;

  // 创建 TSV 中间节点（使用唯一的 id）
  static size_t tsv_node_id_counter = 10000;  // 使用大数字避免冲突
  const sta::Net* net = parasitics->net(node1, network);
  sta::ParasiticNode* tsv_node = parasitics->ensureParasiticNode(
      parasitic_net, 
      net, 
      static_cast<int>(tsv_node_id_counter++),
      network);

  // 将 TSV 电容的一半添加到 TSV 节点（另一半平摊到相邻节点）
  parasitics->incrCap(tsv_node, static_cast<float>(tsv_c * 0.5));
  parasitics->incrCap(node1, static_cast<float>(tsv_c * 0.25));
  parasitics->incrCap(node2, static_cast<float>(tsv_c * 0.25));

  // 创建电阻：node1 -> tsv_node -> node2
  parasitics->makeResistor(parasitic_net, 
                          static_cast<size_t>(tsv_node_id_counter++),
                          static_cast<float>(tsv_r * 0.5),
                          node1, tsv_node);

  parasitics->makeResistor(parasitic_net, 
                          static_cast<size_t>(tsv_node_id_counter++),
                          static_cast<float>(tsv_r * 0.5),
                          tsv_node, node2);
}

// 从 PlaceDB 的 Net 获取 pin 位置（转换为 DBU 单位）
void ParasiticsInjector::getSubnetPinPositions(
    const Net* subnet,
    PlaceDB* pdb,
    odb::dbDatabase* odb_db,
    const std::unordered_map<Module*, ModulePosition>& module_pos,
    std::vector<int>& x,
    std::vector<int>& y,
    std::vector<const Pin*>& pins)
{
  x.clear();
  y.clear();
  pins.clear();

  if (!subnet || !pdb || !odb_db)
    return;

  odb::dbBlock* block = odb_db->getChip()->getBlock();
  if (!block)
    return;

  const int dbu_per_micron = block->getDbUnitsPerMicron();

  for (Pin* pin : subnet->netPins) {
    if (!pin || !pin->module)
      continue;

    auto pos_it = module_pos.find(pin->module);
    if (pos_it == module_pos.end())
      continue;

    const ModulePosition& mpos = pos_it->second;
    double pin_x_um = mpos.position.x + pin->offset.x;
    double pin_y_um = mpos.position.y + pin->offset.y;

    // 转换为 DBU
    int pin_x_dbu = static_cast<int>(pin_x_um * dbu_per_micron);
    int pin_y_dbu = static_cast<int>(pin_y_um * dbu_per_micron);

    x.push_back(pin_x_dbu);
    y.push_back(pin_y_dbu);
    pins.push_back(pin);
  }
}

}  // namespace gpl3d::td
