#pragma once
#include "placedb.h"
namespace odb {
class dbDatabase;
class dbInst;

}  // namespace odb
namespace sta {
class dbSta;
class dbNetwork;
}  // namespace sta

namespace utl {
class Logger;
}

namespace gpl3d {

class Placer3d
{
 public:
  Placer3d() = default;
  ~Placer3d() = default;

  PlaceDB& getDB() { return db; }
  void init(odb::dbDatabase* db,
            sta::dbNetwork* db_network,
            sta::dbSta* sta,
            utl::Logger* logger);
  void run();

 private:
  odb::dbDatabase* db_;
  PlaceDB db;
  sta::dbNetwork* db_network_ = nullptr;
  sta::dbSta* sta_ = nullptr;
  utl::Logger* logger_ = nullptr;
};

}  // namespace gpl3d