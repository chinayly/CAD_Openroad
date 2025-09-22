#ifndef PLACEDB_H
#define PLACEDB_H
#include "objects.h"

/*
    stores the following information
    1. modules
    2. nets
*/
class PlaceDB
{
public:
    PlaceDB()
    {
        dbModules.clear();
        dbPins.clear();
        dbNets.clear();
        dbTiers.clear();
        dbFFs.clear();
    };
    double commonRowHeight; // The row height of all tiers should be the same.
    size_t maxNetDegree;

    //! dbXxs: vector for storing Xxs
    vector<Module*> dbModules; // module include std cells and macros and terminals
    vector<Pin*> dbPins;
    vector<Net*> dbNets;
    vector<Tier> dbTiers;  
    vector<Module*>dbFFs; // flipflops
    
    vector<SiteRow> dbSiteRows; // this is used to calculate the coreRegion area of the benchmark
    double getCoreRegionArea2D();
    void convertTo3d(std::size_t numTiers);

    map<string, Module* > moduleMap; // map module name to module pointer(module include nodes and terminals)

    void addModule(string name, float width, float height, ModuleMoveType moveType, bool isNI, bool isTSV = false, bool isTerminal = false); 
    void addNet(string, vector<Pin*>);
    Pin* addPin(Module *, float, float, PinDirection);

    void allocateModuleMemory(int);
    void allocateNetMemory(int);
    void allocatePinMemory(int);

    Module *getModuleFromName(string);


    // for 3D operations
    double totalModuleArea();
    double totalTierArea();
    double TSVsize;

    // void removeBlockedSite(); // calculate intervals of siterows considering macros and terminals that block sites, see void RemoveFixedBlockSite() and void RemoveMacroSite() in ntuplace

    // void setChipRegion_2D();

    // void showDBInfo();
    // void showRows();

    // void outputBookShelf(string, bool);

    // int y2RowIndex(float);
    // bool isConnected(Module *, Module *);

    // void plotCurrentPlacement(string);
private:
      
    // void outputAUX();
    // void outputNodes();
    // void outputPL();
    // void outputNets();
    // void outputSCL();
};
#endif