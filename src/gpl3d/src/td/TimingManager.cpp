#include "td/TimingManager.h"

// 在实现文件里包含完整定义
#include "td/ParasiticsInjector.h"
#include "td/TimingOracle.h"
#include "td/RCTreeBuilder3D.h"
#include "td/NetWeightScheduler.h"

#include "ord/OpenRoad.hh"
#include "db_sta/dbSta.hh"
#include "sta/Sta.hh"

namespace gpl3d::td {

    TimingManager::TimingManager(PlaceDB* pdb, sta::dbSta* sta, utl::Logger* logger,
        const NetWeightScheduler::Params& sched_params)
: pdb_(pdb), ord_(nullptr), sta_(sta), logger_(logger)
{
rc_.reset(new RCTreeBuilder3D(pdb_, logger_));
pinj_.reset(new ParasiticsInjector(sta_, logger_));  // ★ 用 dbSta*
toracle_.reset(new TimingOracle(pdb_, sta_, logger_));
sched_.reset(new NetWeightScheduler(logger_, sched_params));
cfg_ = Config{};
}

TimingManager::TimingManager(PlaceDB* pdb, sta::dbSta* sta, utl::Logger* logger,
    const NetWeightScheduler::Params& sched_params,
    const Config& cfg)
: pdb_(pdb), ord_(nullptr), sta_(sta), logger_(logger), cfg_(cfg)
{
rc_.reset(new RCTreeBuilder3D(pdb_, logger_));
pinj_.reset(new ParasiticsInjector(sta_, logger_));
toracle_.reset(new TimingOracle(pdb_, sta_, logger_));
sched_.reset(new NetWeightScheduler(logger_, sched_params));
}

TimingManager::~TimingManager() = default;

bool TimingManager::timingIteration(const ModulePosMap& module_pos)
{
  if (logger_) {
    logger_->info(utl::GPL3D, 0, "Timing iteration: build RC (3D/TSV) …");
  }

  rc_->updateFromPlacement(module_pos);

  const double totalC = pinj_->overlayTSV(*rc_);
  if (logger_) {
    logger_->info(utl::GPL3D, 0, "Injected TSV C into STA parasitics (total = {:.3f} fF).", totalC * 1e15);
  }

  const auto s = toracle_->updateAndReport(/*compute_hold=*/cfg_.compute_hold);
  summary_ = s;

  const auto& crit = toracle_->netCriticalities();
  const bool need_rebuild = sched_->computeWeights(crit, net_w_);

  if (logger_ && !net_w_.empty()) {
    double wmin = 1e100, wmax = 0.0;
    for (const auto& kv : net_w_) {
      if (kv.second < wmin) wmin = kv.second;
      if (kv.second > wmax) wmax = kv.second;
    }
    logger_->info(utl::GPL3D, 0, "TD weights updated: |nets|={}, w∈[{:.3f},{:.3f}].",
                  static_cast<int>(net_w_.size()), wmin, wmax);
  }

  return need_rebuild;
}

double TimingManager::weightOf(const ::Net* n) const
{
  auto it = net_w_.find(n);
  return (it == net_w_.end()) ? 1.0 : it->second;
}

void TimingManager::setTechParams(const RCTreeBuilder3D::TechParams& t)
{
  if (rc_) rc_->setTechParams(t);
}

void TimingManager::clearWeights()
{
  net_w_.clear();
  if (logger_) {
    logger_->info(utl::GPL3D, 0, "TimingManager: cleared all net weights (reset to 1.0 by default).");
  }
}

} // namespace gpl3d::td
