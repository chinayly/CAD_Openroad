#include "gpl3d/parser_odb.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "global.h"
#include "objects.h"
#include "odb/db.h"
#include "placedb.h"

using namespace odb;
using std::string;

namespace {

// dbu → μm 的换算
inline double mic(dbBlock* blk, int64_t dbu_val)
{
  return static_cast<double>(dbu_val)
         / static_cast<double>(blk->getDbUnitsPerMicron());
}

}  // unnamed namespace

namespace parser {

/* ---------- 行方向 ---------- */
bool OpenRoadParser::isSouthOrientation(const dbOrientType& orient)
{
  auto v = orient.getValue();
  return v == dbOrientType::R180 || v == dbOrientType::MX
         || v == dbOrientType::MXR90;
}

/* ---------- 顶层入口 ---------- */
void OpenRoadParser::importOpenRoadData(dbDatabase* db, PlaceDB& pdb)
{
  parseSiteRows(db, pdb);
  // buildTierFromDie(db, pdb);
  parseModules(db, pdb);
  parseNets(db, pdb);
}

/* ---------- SiteRow ---------- */
void OpenRoadParser::parseSiteRows(dbDatabase* db, PlaceDB& pdb)
{
  auto* blk = db->getChip()->getBlock();
  for (dbRow* row : blk->getRows()) {
    double bottom = mic(blk, row->getOrigin().y());
    double height = mic(blk, row->getSite()->getHeight());
    double step = mic(blk, row->getSpacing());

    double start_x = mic(blk, row->getOrigin().x());
    double end_x
        = start_x + mic(blk, row->getSite()->getWidth()) * row->getSiteCount();

    SiteRow sr(bottom, height, step);
    sr.start = POS_2D(start_x, bottom);
    sr.end = POS_2D(end_x, bottom);
    sr.orientation = isSouthOrientation(row->getOrient()) ? OR_S : OR_N;
    sr.intervals.emplace_back(static_cast<float>(start_x),
                              static_cast<float>(end_x));

    pdb.dbSiteRows.push_back(sr);
  }
  if (!pdb.dbSiteRows.empty())
    pdb.commonRowHeight = pdb.dbSiteRows.front().height;
}

/* ---------- Module & Pin ---------- */
void OpenRoadParser::parseModules(dbDatabase* db, PlaceDB& pdb)
{
  auto* blk = db->getChip()->getBlock();
  const double dbu2mic = 1.0 / blk->getDbUnitsPerMicron();

  for (dbInst* inst : blk->getInsts()) {
    dbBox* bbox = inst->getBBox();
    if (!bbox)
      continue;

    double w = (bbox->xMax() - bbox->xMin()) * dbu2mic;
    double h = (bbox->yMax() - bbox->yMin()) * dbu2mic;
    double llx = inst->getLocation().x() * dbu2mic;
    double lly = inst->getLocation().y() * dbu2mic;
    double cx = llx + w / 2.0;
    double cy = lly + h / 2.0;

    dbPlacementStatus status = inst->getPlacementStatus();
    ModuleMoveType mv = FREE;
    mv = status.isFixed() ? FIXED : FREE;
    pdb.addModule(inst->getName(), w, h, mv, false);
    Module* mod = pdb.getModuleFromName(inst->getName());
    mod->setInitialPosition(ModulePosition(0, cx, cy));

    module_inst_map_[mod] = inst;

    for (dbITerm* it : inst->getITerms()) {
      dbMTerm* mt = it->getMTerm();
      if (!mt)
        continue;
      dbMPin* mp = nullptr;
      auto mpins = mt->getMPins();
      if (!mpins.empty())
        mp = *mpins.begin();
      if (!mp)
        continue;
      dbBox* pb = nullptr;
      if (!mp->getGeometry().empty())
        pb = *mp->getGeometry().begin();
      if (!pb)
        continue;

      int64_t pcx = (pb->xMin() + pb->xMax()) / 2;
      int64_t pcy = (pb->yMin() + pb->yMax()) / 2;

      float offx = static_cast<float>(pcx * dbu2mic - cx);
      float offy = static_cast<float>(pcy * dbu2mic - cy);

      PinDirection dir = PIN_DIRECTION_UNDEFINED;
      dbIoType io = mt->getIoType();
      switch (io.getValue()) {
        case dbIoType::INPUT:
          dir = PIN_DIRECTION_IN;
          break;
        case dbIoType::OUTPUT:
          dir = PIN_DIRECTION_OUT;
          break;
        default:
          break;
      }
      Pin* pin = pdb.addPin(mod, offx, offy, dir);
      mod->addPin(pin);
      iterm_pin_map_[it] = pin;
    }
  }
  int cnt_fixed = 0, cnt_free = 0;
  for (auto* m : pdb.dbModules) {
    if (!m)
      continue;
    (m->moveType == FIXED) ? cnt_fixed++ : cnt_free++;
  }
  std::cout << "[Summary] Modules: FIXED=" << cnt_fixed << ", FREE=" << cnt_free
            << std::endl;
}

/* ---------- Net ---------- */
void OpenRoadParser::parseNets(dbDatabase* db, PlaceDB& pdb)
{
  dbBlock* block = db->getChip()->getBlock();
  for (dbNet* net : block->getNets()) {
    std::vector<Pin*> netPins;

    for (dbITerm* iterm : net->getITerms()) {
      auto it = iterm_pin_map_.find(iterm);
      if (it != iterm_pin_map_.end())
        netPins.push_back(it->second);
    }

    for (dbBTerm* bt : net->getBTerms()) {
      dbBox* box = nullptr;
      for (dbBPin* bpin : bt->getBPins()) {
        if (!bpin->getBoxes().empty()) {
          box = *bpin->getBoxes().begin();
          break;
        }
      }
      if (!box)
        continue;

      int64_t cx = (box->xMin() + box->xMax()) / 2;
      int64_t cy = (box->yMin() + box->yMax()) / 2;
      float offx = float(cx);
      float offy = float(cy);

      PinDirection dir = PIN_DIRECTION_OUT;
      dbIoType bio = bt->getIoType();
      switch (bio.getValue()) {
        case dbIoType::INPUT:
          dir = PIN_DIRECTION_IN;
          break;
        case dbIoType::OUTPUT:
          dir = PIN_DIRECTION_OUT;
          break;
        default:
          break;
      }
      Pin* pin = pdb.addPin(nullptr, offx, offy, dir);
      netPins.push_back(pin);
    }
    pdb.maxNetDegree = 0;
    pdb.addNet(net->getName(), netPins);
    if (netPins.size() > pdb.maxNetDegree)
      pdb.maxNetDegree = netPins.size();
  }
}

static OpenRoadParser global_parser;
OpenRoadParser* getOpenRoadParser()
{
  return &global_parser;
}

void OpenRoadParser::buildTierFromDie(odb::dbDatabase* db, PlaceDB& pdb)
{
  auto* blk = db->getChip()->getBlock();
  if (!blk) {
    std::cerr << "[Error] No dbBlock found in database." << std::endl;
    return;
  }

  // getDieArea() 返回的是 Rect
  odb::Rect die = blk->getDieArea();

  double lx = mic(blk, die.xMin());
  double ly = mic(blk, die.yMin());
  double ux = mic(blk, die.xMax());
  double uy = mic(blk, die.yMax());

  double substrate_thickness = 10.0;          // μm，可根据工艺设置
  size_t num_metal_layers = 10;               // 金属层数量，可根据 LEF/Lib 设
  double interconnect_layer_thickness = 1.0;  // μm
  double row_height = pdb.commonRowHeight > 0 ? pdb.commonRowHeight : 1.0;

  // 构造单层 Tier，后续可以扩展成多层
  Tier tier(substrate_thickness,
            num_metal_layers,
            interconnect_layer_thickness,
            row_height);
  tier.coreRegion.ll = POS_2D(lx, ly);
  tier.coreRegion.ur = POS_2D(ux, uy);

  pdb.dbTiers.push_back(tier);

  std::cout << "[buildTierFromDie] Added Tier 0 with coreRegion: "
            << "ll=(" << lx << "," << ly << "), "
            << "ur=(" << ux << "," << uy << ")" << std::endl;
}

}  // namespace parser
