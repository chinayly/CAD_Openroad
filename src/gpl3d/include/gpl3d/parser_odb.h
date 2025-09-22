#ifndef GPL3D_PARSER_ODB_H_
#define GPL3D_PARSER_ODB_H_

#include <string>
#include <unordered_map>
#include <vector>

namespace odb {
class dbDatabase;
class dbInst;
class dbITerm;
class dbOrientType;
}  // namespace odb

class PlaceDB;
class Module;
class Pin;

namespace parser {

class OpenRoadParser
{
 public:
  void importOpenRoadData(odb::dbDatabase* db, PlaceDB& placeDB);

  const std::unordered_map<Module*, odb::dbInst*>& moduleInstMap() const
  {
    return module_inst_map_;
  }

 private:
  // 读取三大类对象
  void parseSiteRows(odb::dbDatabase* db, PlaceDB& placeDB);
  void parseModules(odb::dbDatabase* db, PlaceDB& placeDB);
  void parseNets(odb::dbDatabase* db, PlaceDB& placeDB);
  void buildTierFromDie(odb::dbDatabase* db, PlaceDB& placeDB);
  // 工具
  static bool isSouthOrientation(const odb::dbOrientType& orient);

  /* -------- 缓存 -------- */
  std::unordered_map<odb::dbITerm*, Pin*> iterm_pin_map_;
  std::unordered_map<Module*, odb::dbInst*> module_inst_map_;
};
OpenRoadParser* getOpenRoadParser();

}  // namespace parser
#endif  // GPL3D_PARSER_ODB_H_
