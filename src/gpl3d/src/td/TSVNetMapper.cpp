#include "td/TSVNetMapper.h"
#include <algorithm>
#include <regex>

namespace gpl3d::td {

const std::vector<TSVInfo> TSVNetMapper::empty_tsv_list_;

void TSVNetMapper::buildFromPlaceDB(PlaceDB* pdb, 
                                     const std::unordered_map<Module*, ModulePosition>& module_pos) {
  original_net_to_tsvs_.clear();
  subnet_to_original_.clear();

  if (!pdb) return;

  // 遍历所有 net，识别子 net 和 TSV
  for (Net* net : pdb->dbNets) {
    if (!net) continue;

    // 如果 net 有原始名称且与当前名称不同，说明是子 net
    if (!net->originalNetName.empty() && net->originalNetName != net->name) {
      subnet_to_original_[net->name] = net->originalNetName;
    }
  }

  // 遍历所有模块，找到 TSV 模块
  for (Module* mod : pdb->dbModules) {
    if (!mod || !mod->isTSV) continue;

    // 从 TSV 模块名称提取信息：origNetName_TSV_k_k+1
    std::string tsv_name = mod->name;
    size_t tsv_pos = tsv_name.find("_TSV_");
    if (tsv_pos == std::string::npos) continue;

    std::string original_net_name = tsv_name.substr(0, tsv_pos);
    
    // 提取层号
    size_t layer_pos = tsv_pos + 5; // "_TSV_" 的长度
    size_t underscore_pos = tsv_name.find("_", layer_pos);
    if (underscore_pos == std::string::npos) continue;

    int tier_from = std::stoi(tsv_name.substr(layer_pos, underscore_pos - layer_pos));
    int tier_to = std::stoi(tsv_name.substr(underscore_pos + 1));

    // 获取 TSV 位置
    auto pos_it = module_pos.find(mod);
    double tsv_x = 0.0, tsv_y = 0.0;
    if (pos_it != module_pos.end()) {
      tsv_x = pos_it->second.position.x;
      tsv_y = pos_it->second.position.y;
    }

    // 查找相关的子 net
    Net* lower_net = nullptr;
    Net* upper_net = nullptr;
    
    std::string base_name = tsv_name;
    std::string lower_name = base_name + "_DN";
    std::string upper_name = base_name + "_UP";

    for (Net* net : pdb->dbNets) {
      if (!net) continue;
      if (net->name == lower_name) {
        lower_net = net;
      } else if (net->name == upper_name) {
        upper_net = net;
      }
    }

    // 创建 TSV 信息
    TSVInfo info;
    info.tsv_module = mod;
    info.lower_net = lower_net;
    info.upper_net = upper_net;
    info.tier_from = tier_from;
    info.tier_to = tier_to;
    info.tsv_x = tsv_x;
    info.tsv_y = tsv_y;

    original_net_to_tsvs_[original_net_name].push_back(info);
  }
}

std::string TSVNetMapper::getOriginalNetName(const std::string& subnet_name) const {
  auto it = subnet_to_original_.find(subnet_name);
  if (it != subnet_to_original_.end()) {
    return it->second;
  }
  // 如果不在映射中，尝试从名称提取
  return extractOriginalNetName(subnet_name);
}

const std::vector<TSVInfo>& TSVNetMapper::getTSVInfos(const std::string& original_net_name) const {
  auto it = original_net_to_tsvs_.find(original_net_name);
  if (it != original_net_to_tsvs_.end()) {
    return it->second;
  }
  return empty_tsv_list_;
}

bool TSVNetMapper::isSubnet(const std::string& net_name) {
  return net_name.find("_TSV_") != std::string::npos;
}

std::string TSVNetMapper::extractOriginalNetName(const std::string& subnet_name) {
  size_t tsv_pos = subnet_name.find("_TSV_");
  if (tsv_pos != std::string::npos) {
    return subnet_name.substr(0, tsv_pos);
  }
  return subnet_name;  // 如果没有找到 _TSV_，返回原名称
}

} // namespace gpl3d::td

