
%{
#include "gpl3d/Placer3d.h"
#include "ord/OpenRoad.hh"
#include "gpl3d/parser_odb.h"
namespace ord{
    OpenRoad* getOpenRoad();
    gpl3d::Placer3d* getPlacer3d();
}
namespace parser{
    OpenRoadParser* getOpenRoadParser();
}
using ord::getPlacer3d;
using parser::getOpenRoadParser;
using ord::getOpenRoad;

%}

%inline 
%{

void import_place_db()
    {
        odb::dbDatabase* db = getOpenRoad()->getDb(); 
        PlaceDB& pdb = getPlacer3d()->getDB();
        getOpenRoadParser()->importOpenRoadData(db, pdb);
    }

    void placer3d_run()
    {
        getPlacer3d()->run();
    }
    
%}

