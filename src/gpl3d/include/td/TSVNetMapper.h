#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "placedb.h"

namespace gpl3d::td {

/// TSV 信息：记录 TSV 模块及其连接的上下两层子 net
struct TSVInfo {
  Module* tsv_module {nullptr};      // TSV 模块
  Net* lower_net {nullptr};          // 下层子 net（_DN）
  Net* upper_net {nullptr};          // 上层子 net（_UP）
  int tier_from {0};                 // TSV 起始层
  int tier_to {0};                   // TSV 目标层
  double tsv_x {0.0};                // TSV 的 X 坐标
  double tsv_y {0.0};                // TSV 的 Y 坐标
};

/// 管理原始 net 到子 net 和 TSV 的映射关系
class TSVNetMapper {
public:
  TSVNetMapper() = default;

  /// 从 PlaceDB 构建映射关系（在 SpacePlacer 分割 net 后调用）
  void buildFromPlaceDB(PlaceDB* pdb, const std::unordered_map<Module*, ModulePosition>& module_pos);

  /// 根据子 net 名称获取原始 net 名称
  std::string getOriginalNetName(const std::string& subnet_name) const;

  /// 根据原始 net 名称获取所有相关的 TSV 信息
  const std::vector<TSVInfo>& getTSVInfos(const std::string& original_net_name) const;

  /// 检查 net 名称是否是子 net（包含 _TSV_ 标记）
  static bool isSubnet(const std::string& net_name);

  /// 从子 net 名称提取原始 net 名称（通过解析 _TSV_ 标记）
  static std::string extractOriginalNetName(const std::string& subnet_name);

private:
  // 原始 net 名称 -> TSV 信息列表
  std::unordered_map<std::string, std::vector<TSVInfo>> original_net_to_tsvs_;
  
  // 子 net 名称 -> 原始 net 名称（快速查找）
  std::unordered_map<std::string, std::string> subnet_to_original_;
  
  // 空向量（用于返回空结果）
  static const std::vector<TSVInfo> empty_tsv_list_;
};

} // namespace gpl3d::td

