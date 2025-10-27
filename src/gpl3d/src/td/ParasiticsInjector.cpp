#include "td/ParasiticsInjector.h"

// —— 只在实现文件里包含 OpenROAD / OpenSTA 头，避免头文件传播 —— //
#include "db_sta/dbSta.hh"  // dbSta（OpenROAD里 getSta() 的具体类型）
#include "ord/OpenRoad.hh"
#include "sta/Corner.hh"      // Corner
#include "sta/MinMax.hh"      // MinMax::max()/min()
#include "sta/Network.hh"     // Network / Net
#include "sta/Parasitics.hh"  // Parasitics / Parasitic / ParasiticNode
#include "sta/Sta.hh"         // Sta 基类接口

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

// 基于原名/raw 与顶层实例名，生成一组可能的 STA 可识别候选
static std::vector<std::string> genCandidates(const std::string& raw,
                                              const char* top_inst_name)
{
  std::vector<std::string> cand;
  cand.reserve(8);

  const std::string cleaned = sanitizeNetName(raw);
  const bool has_top = (top_inst_name && *top_inst_name);

  // a) cleaned 原样
  cand.push_back(cleaned);

  // b) 叶子名（去掉层次路径，只保留最后一个 '/' 之后）
  auto last_slash = cleaned.rfind('/');
  if (last_slash != std::string::npos && last_slash + 1 < cleaned.size()) {
    cand.push_back(cleaned.substr(last_slash + 1));
  }

  // c) 顶层前缀 + cleaned
  if (has_top) {
    cand.push_back(std::string(top_inst_name) + "/" + cleaned);
  }
  // d) 顶层前缀 + 叶子
  if (has_top && last_slash != std::string::npos
      && last_slash + 1 < cleaned.size()) {
    cand.push_back(std::string(top_inst_name) + "/"
                   + cleaned.substr(last_slash + 1));
  }

  // e) 把层次分隔符 '/' 改成 '|'（有的读入风格会这样）
  {
    auto to_bar = [&](std::string s) {
      for (char& ch : s)
        if (ch == '/')
          ch = '|';
      return s;
    };
    cand.push_back(to_bar(cleaned));
    if (has_top)
      cand.push_back(to_bar(std::string(top_inst_name) + "/" + cleaned));
    if (last_slash != std::string::npos && last_slash + 1 < cleaned.size()) {
      cand.push_back(to_bar(cleaned.substr(last_slash + 1)));
      if (has_top)
        cand.push_back(to_bar(std::string(top_inst_name) + "/"
                              + cleaned.substr(last_slash + 1)));
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
    const std::string& /*wire_rc_layer_or_rule*/)
{
  if (logger_) {
    logger_->warn(utl::GPL3D,
                  0,
                  "estimatePlacementRC: skipped here (run 'set_wire_rc' & "
                  "'estimate_parasitics -placement' in Tcl).");
  }
  return false;
}

// 把 TSV 的等效 C（法拉）叠加到每条 net 的寄生网络（以“节点地电容”增量方式）
double ParasiticsInjector::overlayTSV(const RCTreeBuilder3D& rc3d)
{
  // 1) OpenSTA 句柄
  sta::dbSta* sta = sta_ ? sta_ : (ord_ ? ord_->getSta() : nullptr);
  if (!sta)
    return 0.0;

  sta::Parasitics* parasitics = sta->parasitics();
  sta::Network* network = sta->network();
  if (!parasitics || !network)
    return 0.0;

  // 确保 Corner 上有分析点（OpenSTA 里需要先开启）
  sta->setParasiticAnalysisPts(true);

  // 我们同时对 max/min 两个视图叠加（若你只想 setup，可仅用 Max）
  const sta::MinMax* views[2] = {sta::MinMax::max(), sta::MinMax::min()};

  double totalC = 0.0;
  int nets_touched = 0;

  // 2) 对每个 corner、每个视图的 analysis point 注入 TSV 电容
  for (sta::Corner* corner : *sta->corners()) {
    for (const sta::MinMax* mm : views) {
      const sta::ParasiticAnalysisPt* ap = corner->findParasiticAnalysisPt(mm);
      if (ap == nullptr)
        continue;  // 该视图没开就跳过

      for (const auto& kv : rc3d.all()) {
        const ::Net* n = kv.first;
        const RCNetModel& mdl = kv.second;
        if (mdl.tsvs.empty())
          continue;

        // 通过名字找到 STA 的 net（要求 PlaceDB::Net::name 与 STA/DB 一致）
        const char* top_name = nullptr;
        if (network && network->topInstance()) {
          top_name = network->name(network->topInstance());
        }

        // —— 生成候选名并逐个尝试 —— //
        const std::string& net_name = n->name;
        sta::Net* snet = nullptr;

        auto candidates = genCandidates(net_name, top_name);
        for (const auto& c : candidates) {
          snet = network->findNet(c.c_str());
          if (snet)
            break;
        }

        if (!snet) {
          if (logger_) {
            // 打印前 1~2 个候选，别刷屏
            std::string c0 = candidates.size() > 0 ? candidates[0] : "";
            std::string c1 = candidates.size() > 1 ? candidates[1] : "";
            logger_->warn(utl::GPL3D,
                          0,
                          "overlayTSV: STA net not found. orig='{}' try0='{}' "
                          "try1='{}' top='{}'",
                          net_name,
                          c0,
                          c1,
                          (top_name ? top_name : "(null)"));
          }
          continue;
        }

        // 聚合 TSV 电容（法拉）
        double addC = 0.0;
        for (size_t i = 0; i < mdl.tsvs.size(); ++i) {
          const RCTsvSeg& t = mdl.tsvs[i];
          if (t.C > 0.0)
            addC += t.C;
        }
        if (addC <= 0.0)
          continue;

        // 3) 确保该 net 有寄生网络（注意：三参版本：net, includes_pin_caps,
        // ap）
        sta::Parasitic* pnet = parasitics->findParasiticNetwork(snet, ap);
        if (!pnet) {
          pnet = parasitics->makeParasiticNetwork(snet,
                                                  /*includes_pin_caps=*/true,
                                                  ap);
        }

        // 4) 取“根”节点并对它增量加地电容（单节点电容语义即地电容）
        //    用 (net, id=0) 作为根节点标识；多次调用会复用同一节点。
        sta::ParasiticNode* root
            = parasitics->ensureParasiticNode(pnet, snet, /*id=*/0, network);
        parasitics->incrCap(root, static_cast<float>(addC));

        totalC += addC;
        ++nets_touched;

        if (logger_) {
          const char* nname = network->name(snet);
          logger_->info(utl::GPL3D,
                        0,
                        "overlayTSV: [{} {}] net {} add C = {:.3f} fF",
                        (corner ? corner->name() : "corner"),
                        (mm == sta::MinMax::max() ? "max" : "min"),
                        (nname ? nname : "(null)"),
                        addC * 1e15);
        }
      }  // nets
    }  // views (min/max)
  }  // corners

  if (logger_) {
    logger_->info(utl::GPL3D,
                  0,
                  "overlayTSV done: nets={}, total TSV C = {:.3f} fF (min/max "
                  "for all corners)",
                  nets_touched,
                  totalC * 1e15);
  }
  return totalC;
}

}  // namespace gpl3d::td
