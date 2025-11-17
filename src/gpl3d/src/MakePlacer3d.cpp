
#include "gpl3d/MakePlacer3d.h"

#include "ord/OpenRoad.hh"
#include "gpl3d/Placer3d.h"
#include "utl/decode.h"

namespace gpl3d{
    // Tcl files encoded into strings.
    extern const char* gpl3d_tcl_inits[];
} // namespace gpl3d

extern "C" {
    extern int Gpl3d_Init(Tcl_Interp* interp);
}

// Forward declare PlaceDB and the native initializer so we can call the
// C++ registration helper that lives in placer3d.cpp (gpl3d::Gpl3dInit).
class PlaceDB;
namespace gpl3d { void Gpl3dInit(ord::OpenRoad* openroad, PlaceDB* place_db, 
    ::sta::dbSta* dbsta, ::utl::Logger* logger); }

namespace ord{

gpl3d::Placer3d* makePlacer3d()
{
    return new gpl3d::Placer3d();
}

void initPlacer3d(OpenRoad* openroad)
{
    Tcl_Interp* tcl_interp = openroad->tclInterp();
    Gpl3d_Init(tcl_interp);
    utl::evalTclInit(tcl_interp, gpl3d::gpl3d_tcl_inits);

    gpl3d::Placer3d* kernel = openroad->getPlacer3d();

    kernel->init(openroad->getDb(),
                 openroad->getDbNetwork(),
                 openroad->getSta(),
                 openroad->getLogger());

    // Ensure native C++ initialization that registers runtime Tcl hooks
    // (e.g. gpl3d::td::timing_iteration) is invoked. Some registration
    // lives in `gpl3d::Gpl3dInit` (see placer3d.cpp); call it here so that
    // TimingOracle::registerTcl gets a valid Tcl_Interp and is not missed.
    // PlaceDB isn't available at this stage, pass nullptr — TimingOracle
    // handles a null PlaceDB pointer for registration purposes.
    // Call native initializer that registers additional C++ hooks (e.g.
    // TimingOracle Tcl bindings). Pass nullptr for PlaceDB which will be
    // set up later when actual placement runs.
    gpl3d::Gpl3dInit(openroad, nullptr, openroad->getSta(), openroad->getLogger());
}

void deletePlacer3d(gpl3d::Placer3d* placer3d)
{
    delete placer3d;
}

} //namespace ord