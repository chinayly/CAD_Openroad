#ifndef CTSDB_H
#define CTSDB_H
#include "CTSglobal.h"
#include "CTSobjects.h"
#include "global.h"
#include "objects.h"
#include "placedb.h"
class CTSDB
{
public:
    CTSDB()
    {
        init();
    }
    vector<Sink> dbSinks;
    vector<Blockage> dbBlockages;
    Point_2D clockSource;

    void init()
    {
        clockSource.SetZero();
        dbSinks.clear();
        dbBlockages.clear();
    }
    PlaceDB* pdb;

    void initWithPlaceDB(PlaceDB*);
    void setSinkLocationWithPlaceDB(PlaceDB*);


    void initWithTierFFs(const vector<vector<Module*>>& ffNodesPerTier, 
                        const unordered_map<Module*, ModulePosition>& modulePosition,
                        size_t tierId);
    
    void setSinkLocationWithTierPlacer(const vector<vector<Module*>>& ffNodesPerTier,
                                      const unordered_map<Module*, ModulePosition>& modulePosition,
                                      size_t tierId);
    

    void showCTSdbInfo();
};
#endif