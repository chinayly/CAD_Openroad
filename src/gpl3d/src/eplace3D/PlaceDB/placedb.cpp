#include <random>

#include "placedb.h"
#include "global.h"

void PlaceDB::addModule(string name, float width, float height, ModuleMoveType moveType, bool isNI, bool isTSV, bool isTerminal)
{
    Module* mod = new Module(name, width, height, moveType, isNI);
    assert(commonRowHeight > EPS); //! potential float precision problem!!
    if (isTSV)
    {
        mod->isTSV = true;
        mod->height = TSVsize; // set the height of TSV
        mod->width = TSVsize;  // set the width of TSV
    }
    else
    {
        mod->isTSV = false;
    }
    if (isTerminal)
    {
        mod->isTerminal = true;
        mod->moveType = FIXED; // set the move type of terminal to FIXED
    }
    else
    {
        mod->isTerminal = false;
    }
    mod->isMacro = false;
    if (float_greater(height, commonRowHeight))
    {
        mod->isMacro = true;
    }
    else if (float_greater(commonRowHeight, height) && (moveType != FIXED))
    {
        if (name.find("TSV") == std::string::npos){
            printf("Cell %s height ERROR: %f, commonRowHeight: %f\n", name.c_str(), height,commonRowHeight);
            exit(-1);
        }
        
    }
    dbModules.emplace_back(mod);
    moduleMap[name] = mod;
}

void PlaceDB::addNet(string name, vector<Pin*> pins)
{
    Net* n = new Net(name,pins);
    dbNets.push_back(n);
}

Pin* PlaceDB::addPin(Module *masterModule, float xOffset, float yOffset,PinDirection direction)
{
    dbPins.push_back(new Pin(masterModule, POS_2D(xOffset,yOffset), direction));
    return dbPins.back();
}

void PlaceDB::allocateModuleMemory(int n)
{
    dbModules.resize(n);
}

void PlaceDB::allocateNetMemory(int n)
{
    dbNets.reserve(n);
}

void PlaceDB::allocatePinMemory(int n) // difference between resize and reserve: with resize we can use index to index an element in a vector after allocation and before the element was instantiated
{
    dbPins.reserve(n); //! reserve instead of resize
}

Module *PlaceDB::getModuleFromName(string name)
{
    map<string, Module *>::const_iterator ite = moduleMap.find(name);
    if (ite == moduleMap.end())
    {
        return NULL;
    }
    return ite->second;
}

double PlaceDB::getCoreRegionArea2D()
{
    double bottom = dbSiteRows.front().bottom;
    double top = dbSiteRows.back().bottom + dbSiteRows.back().height;
    double left = dbSiteRows.front().start.x;
    double right = dbSiteRows.front().end.x;

    double totalRowArea = 0;
    float curRowArea = 0;
    for (SiteRow curRow : dbSiteRows)
    {
        left = min(left, curRow.start.x);
        right = max(right, curRow.end.x);
        // printf( "right= %g\n", m_coreRgn.right );
        curRowArea = (curRow.end.x - curRow.start.x) * curRow.height;
        totalRowArea += curRowArea;
    }
    return totalRowArea;
}

void PlaceDB::convertTo3d(std::size_t numTiers)
{
    //! define TSV size
    double realTSVSize = 3000; // 2000nm, 2um
    TSVsize = 12.0 / 1400.0 * realTSVSize; // 根据tech file 换算， 45nm工艺下， 12的行高意味着1400nm
    TSVsize = floor(TSVsize);
    TSVsize = 1 * commonRowHeight; //默认行高的计算面积
    // TSVsize = 0; 
    printf("TSV size: %f\n",TSVsize);

    double coreRegionArea = getCoreRegionArea2D();
    std::size_t numRows = dbSiteRows.size();
    //
    printf("\n======Convert to 3D database======\n");
    printf("before:\n");
    printf("numRows:%li\tcommonRowHeight:%f\n",numRows,commonRowHeight);
    printf("coreRegionArea:%f\n",coreRegionArea);
    // To keep the area, row length and row_num should be 1/sqrt(numTiers) 
    double areaExpandFactor = 1.0; //! about 1.2x area.   :to expand the place area for potential TSVs
    if(gArg.CheckExist("expandFactor"))
    {
        gArg.GetDouble("expandFactor",&areaExpandFactor);
    }
    printf("areaExpandFactor:%f\n",areaExpandFactor);
    double coreRegionAreaPerTier = coreRegionArea / numTiers * areaExpandFactor * areaExpandFactor;
    std::size_t numRowsPerTier = static_cast<std::size_t>(ceil(numRows / sqrt(numTiers) * areaExpandFactor));
    double rowWidth = ceil(coreRegionAreaPerTier / numRowsPerTier / commonRowHeight);
    // this assumes the siteWidth of all rows are the same
    double siteWidth = dbSiteRows.back().step;
    printf("after:\n");
    printf("numTiers:%li\n",numTiers);
    printf("numRowsPerTier:%li\tcommonRowHeight:%f\n",numRowsPerTier,commonRowHeight);
    printf("rowWidth:%f,siteWidth:%f\n",rowWidth,siteWidth);

    for(std::size_t curTierId = 0; curTierId < numTiers; curTierId++)
    {
        Tier curTier(0,0,0,commonRowHeight);
        for(std::size_t curRowId = 0; curRowId < numRowsPerTier; curRowId++)
        {
            SiteRow curRow(curRowId*commonRowHeight,commonRowHeight,siteWidth);
            curRow.start = POS_2D(0,curRowId*commonRowHeight);
            curRow.end = POS_2D(rowWidth,curRowId*commonRowHeight);
            curTier.addRow(curRow);
        }
        curTier.setCoreRegion();
        dbTiers.push_back(curTier);
    }
}

double PlaceDB::totalModuleArea()
{
    double totalArea = 0;
    for(auto m:dbModules)
    {
        totalArea += m->getArea(); 
    }
    return totalArea;
}

double PlaceDB::totalTierArea()
{
    double totalArea = 0;
    for(auto& t:dbTiers)
    {
        totalArea += t.getArea();
    }
    return totalArea;
}

// void PlaceDB::removeBlockedSite() // update intervals
// {
//     //! ignore terminals that are outside the core region
//     //! overlap between macros should be eliminated first! (macro legalization)

//     // 1. count all terminal and macros
//     vector<CRect> obstacles;
//     obstacles.clear();
//     for (Module *curTerminal : dbTerminals)
//     {
//         if (float_greater(coreRegion.ur.y, curTerminal->getLL_2D().y) && float_less(coreRegion.ll.y, curTerminal->getUR_2D().y)) // overlap in y direction
//         {
//             if (float_greater(coreRegion.ur.x, curTerminal->getLL_2D().x) && float_less(coreRegion.ll.x, curTerminal->getUR_2D().x)) // overlap in x direction
//             {
//                 CRect newObstacle;
//                 newObstacle.ll = curTerminal->getLL_2D();
//                 newObstacle.ur = curTerminal->getUR_2D();
//                 obstacles.push_back(newObstacle);
//             }
//         }
//     }

//     for (Module *curNode : dbNodes)
//     {
//         if (curNode->isMacro)
//         {
//             CRect newObstacle;
//             newObstacle.ll = curNode->getLL_2D();
//             newObstacle.ur = curNode->getUR_2D();
//             obstacles.push_back(newObstacle);
//         }
//     }
//     //! sort obstacles by x coordinate first
//     sort(obstacles.begin(), obstacles.end(), [=](CRect a, CRect b)
//          {
//             if (!float_equal(a.ll.x , b.ll.x))
//             {
//                 return float_less(a.ll.x ,b.ll.x);
                
//             }
//             else if (!float_equal(a.ll.y , b.ll.y))
//             {
//                 return float_less(a.ll.y ,b.ll.y);
//             }
//             else
//             {
//                 // terminals/macros overlap!!
//                 cerr<<"TERMINALS/MACROS OVERLAP!\n";
//                 exit(0);
//             } });

//     // 2. remove blocked sites and update intervals
//     double siteStep = dbSiteRows.front().step; //! assume step for all rows are identical! so it's ok to use front()

//     for (CRect curObstacle : obstacles)
//     {
//         vector<SiteRow>::iterator iteBeginRow, iteEndRow;
//         // find the begin row and the end row (of all rows that are blocked by curObstacle)
//         //!!!! important assumption here: dbSiteRows is sorted by bottom coordinate in an increasing order!
//         for (iteBeginRow = dbSiteRows.begin(); iteBeginRow < dbSiteRows.end(); iteBeginRow++)
//         {
//             if (iteBeginRow->bottom + iteBeginRow->height > curObstacle.ll.y)
//             {
//                 break;
//             }
//         }

//         for (iteEndRow = iteBeginRow; iteEndRow < dbSiteRows.end(); iteEndRow++)
//         {
//             if (iteEndRow->bottom + iteEndRow->height >= curObstacle.ur.y)
//             {
//                 break;
//             }
//         }

//         if (iteEndRow == dbSiteRows.end())
//         {
//             iteEndRow--;
//         }
//         assert(iteBeginRow != dbSiteRows.end());

//         Interval tempInterval;

//         for (vector<SiteRow>::iterator curRow = iteBeginRow; curRow <= iteEndRow; curRow++)
//         {
//             for (int i = 0; i < (signed)curRow->intervals.size(); i++)
//             {
//                 tempInterval = curRow->intervals[i];

//                 if (tempInterval.start >= curObstacle.ur.x || tempInterval.end <= curObstacle.ll.x) // screen unnecessary checks
//                 {
//                     continue;
//                 }

//                 if (tempInterval.start >= curObstacle.ll.x && tempInterval.end <= curObstacle.ur.x) // fully blocked
//                 {
//                     //    ---
//                     // MMMMMMMMM
//                     curRow->intervals.erase(vector<Interval>::iterator(&(curRow->intervals[i])));
//                 }
//                 else if (tempInterval.end > curObstacle.ur.x && tempInterval.start >= curObstacle.ll.x)
//                 {
//                     // ------      -----
//                     // MMM      MMMMM
//                     curRow->intervals[i].start = curObstacle.ur.x;
//                 }
//                 else if (tempInterval.start < curObstacle.ll.x && tempInterval.end <= curObstacle.ur.x)
//                 {
//                     // ---------       -----
//                     //     MMMMM          MMMMM
//                     curRow->intervals[i].end = curObstacle.ll.x;
//                 }
//                 else if (tempInterval.start < curObstacle.ll.x && tempInterval.end > curObstacle.ur.x)
//                 {
//                     // -----------
//                     //    MMMM
//                     curRow->intervals[i].end = curObstacle.ll.x;
//                     curRow->intervals.insert(vector<Interval>::iterator(&(curRow->intervals[i + 1])), Interval(curObstacle.ur.x, tempInterval.end));
//                 }
//                 else
//                 {
//                     printf("Warning: Module Romoving Error\n");
//                     // exit(-1);
//                 }
//             }
//         }
//     }

//     //! 3. align intervals to sites after updating intervals, see ntuplace: FixFreeSiteBySiteStep(). Here we need to update the end and start of a site row, so end.x-start.x is an positive integer multiple of site step(site width)
//     for (auto curRowIter = dbSiteRows.begin(); curRowIter != dbSiteRows.end(); curRowIter++)
//     {
//         //? should start.x and end.x be integers too??? check ntuplace

//         for (auto curIntervalIter = curRowIter->intervals.begin(); curIntervalIter != curRowIter->intervals.end();)
//         {
//             double intervalWidth = curIntervalIter->end - curIntervalIter->start;
//             // subRowWidth might be less than 0 when:
//             //    ------
//             //   OOOOO
//             if (float_less(intervalWidth, siteStep)) // assume iter->step > 0!
//             {
//                 curIntervalIter = curRowIter->intervals.erase(curIntervalIter);
//             }
//             else
//             {
//                 double newLeft = ceil((curIntervalIter->start - coreRegion.ll.x) / siteStep) * siteStep + coreRegion.ll.x;
//                 double newRight = floor((curIntervalIter->end - coreRegion.ll.x) / siteStep) * siteStep + coreRegion.ll.x;
//                 double newIntervalWidth = newRight - newLeft;
//                 // assert(newIntervalWidth >= siteStep);
//                 if (float_greater(newIntervalWidth, 0.0))
//                 {
//                     curIntervalIter->start = newLeft;
//                     curIntervalIter->end = newRight;
//                     curIntervalIter++;
//                 }
//                 else
//                 {
//                     curIntervalIter = curRowIter->intervals.erase(curIntervalIter);
//                     if (float_less(newIntervalWidth, 0.0))
//                     {
//                         // newIntervalWidth should >= 0.0
//                         cerr << "sub row new width < 0 when it should not\n";
//                         exit(0);
//                     }
//                 }
//             }
//         }
//     }
// }

// void PlaceDB::showDBInfo()
// {
//     double cellArea = 0;
//     double macroArea = 0;
//     double fixedArea = 0;
//     double fixedAreaInCore = 0;
//     double movableArea = 0;
//     int macroCount = 0;
//     int cellCount = 0;
//     int terminalCount = dbTerminals.size();
//     int netCount = dbNets.size();
//     int pinCount = dbPins.size();
//     int maxNetDegree = INT32_MIN;
//     for (Module& curNode : dbNodes)
//     {
//         if (curNode.isMacro)
//         {
//             macroArea += curNode.getArea();
//             macroCount++;
//         }
//         else
//         {
//             cellArea += curNode.getArea();
//             cellCount++;
//         }
//     }
//     movableArea = macroArea + cellArea;
//     for (Module *curTerminal : dbTerminals)
//     {
//         fixedArea += curTerminal->getArea();
//         fixedAreaInCore += getOverlapArea_2D(coreRegion.ll, coreRegion.ur, curTerminal->getLL_2D(), curTerminal->getUR_2D()); // notice that here we do not consider that some rows might have shorter width than the others
//     }
//     int pin2 = 0, pin3 = 0, pin10 = 0, pin100 = 0;
//     for (Net *curNet : dbNets)
//     {
//         int curPinCount = curNet->getPinCount();
//         if (curPinCount > maxNetDegree)
//         {
//             maxNetDegree = curPinCount;
//         }
//         if (curPinCount == 2)
//             pin2++;
//         else if (curPinCount < 10)
//             pin3++;
//         else if (curPinCount < 100)
//             pin10++;
//         else
//             pin100++;
//     }

//     printf("\n<<<< DATABASE SUMMARIES >>>>\n\n");
//     printf("         Core region: ");
//     coreRegion.Print();
//     printf("   Row Height/Number: %.0f / %d (site step %f)\n", commonRowHeight, dbSiteRows.size(), dbSiteRows[0].step);
//     printf("           Core Area: %.0f (%g)\n", coreArea, coreArea);
//     printf("           Cell Area: %.0f (%.2f%%)\n", cellArea, 100.0 * cellArea / coreArea);
//     if (macroCount > 0)
//     {
//         printf("          Macro Area: %.0f (%.2f%%)\n", macroArea, 100.0 * macroArea / coreArea);
//         printf("  Macro/(Macro+Cell): %.2f%%\n", 100.0 * macroArea / (macroArea + cellArea));
//     }
//     printf("        Movable Area: %.0f (%.2f%%)\n", movableArea, 100.0 * movableArea / coreArea);
//     if (terminalCount > 0)
//     {
//         printf("          Fixed Area: %.0f (%.2f%%)\n", fixedArea, 100.0 * fixedArea / coreArea);
//         printf("  Fixed Area in Core: %.0f (%.2f%%)\n", fixedAreaInCore, 100.0 * fixedAreaInCore / coreArea);
//     }
//     // printf( "   (Macro+Cell)/Core: %.2f%%\n", 100.0*(macroArea+cellArea)/coreArea );
//     printf("     Placement Util.: %.2f%% (=move/freeSites)\n", 100.0 * movableArea / (coreArea - fixedAreaInCore));
//     printf("        Core Density: %.2f%% (=usedArea/core)\n", 100.0 * (movableArea + fixedAreaInCore) / coreArea);
//     // printf( "           Site Area: %.0f (%.0f)", totalSiteArea, coreArea-fixedAreaInCore );
//     printf("              Cell #: %d (=%dk)\n", cellCount, (cellCount / 1000));
//     printf("            Object #: %d (=%dk) (fixed: %d) (macro: %d)\n", macroCount + cellCount + terminalCount, (macroCount + cellCount + terminalCount) / 1000, terminalCount, macroCount);
//     if (macroCount < 20)
//     {
//         for (Module *curNode : dbNodes)
//             if (curNode->isMacro)
//                 printf(" Macro: %s\n", curNode->name);
//     }
//     printf("               Net #: %d (=%dk)\n", netCount, netCount / 1000);
//     printf("               Max net degree=: %d\n", maxNetDegree);
//     printf("                  Pin 2 (%d) 3-10 (%d) 11-100 (%d) 100- (%d)\n", pin2, pin3, pin10, pin100);
//     printf("               Pin #: %d\n", pinCount);

//     // printf( "               Pin #: %d (in: %d  out: %d  undefined: %d)\n", pinNum, inPinNum, outPinNum, undefPinNum );
//     // double HPWL = calcHPWL();
//     // printf("     Pin-to-Pin HPWL: %.0f (%g)\n", HPWL, HPWL);
// }

// void PlaceDB::showRows()
// {
//     for (auto curRowIter = dbSiteRows.begin(); curRowIter != dbSiteRows.end(); curRowIter++)
//     {
//         cout << "\n=====DB ROW SPACE ===\n";

//         for (auto iter = curRowIter->intervals.begin(); iter != curRowIter->intervals.end(); iter++)
//         {
//             // modified by Jin 20070727
//             printf("[%.10f,%.10f] ", iter->start, iter->getLength());
//             // cout<<" ["<<iter->first<<","<<iter->second<<"] ";
//             // modified by Jin 20070727
//         }
//         cout << '\n';
//     }
// }

// void PlaceDB::outputBookShelf(string suffix, bool plOnly)
// {
//     string outputFilePath;
//     string benchmarkName;
//     gArg.GetString("benchmarkName", &benchmarkName);
//     if (!gArg.GetString("outputPath", &outputFilePath))
//     {
//         outputFilePath = "./" + benchmarkName + "/";
//     }

//     gArg.Override("outputSuffix", suffix);

//     if (!plOnly)
//     {
//         outputAUX();
//         outputNodes();
//         outputNets();
//         outputSCL();
//     }

//     outputPL();
// }

// void PlaceDB::outputAUX()
// {
//     string outputFilePath;
//     gArg.GetString("outputPath", &outputFilePath);

//     string benchmarkName;
//     gArg.GetString("benchmarkName", &benchmarkName);

//     string suffix;
//     gArg.GetString("outputSuffix", &suffix);

//     outputFilePath += benchmarkName;
//     outputFilePath += "-" + suffix + ".aux";

//     cout << "Output AUX file:" << outputFilePath << endl;

//     gArg.Override("outputAUX", outputFilePath);

//     ofstream out(outputFilePath);
//     if (!out)
//     {
//         cerr << "Cannot open output file\n";
//         return;
//     }

//     out << "RowBasedPlacement : "
//         << benchmarkName << "-" + suffix + ".nodes "
//         << benchmarkName << "-" + suffix + ".nets "
//         << benchmarkName << "-" + suffix + ".wts "
//         << benchmarkName << "-" + suffix + ".pl "
//         << benchmarkName << "-" + suffix + ".scl \n\n";
// }

// void PlaceDB::outputNodes()
// {

//     string outputFilePath;
//     gArg.GetString("outputPath", &outputFilePath);

//     string benchmarkName;
//     gArg.GetString("benchmarkName", &benchmarkName);

//     string suffix;
//     gArg.GetString("outputSuffix", &suffix);

//     outputFilePath += benchmarkName;
//     outputFilePath += "-" + suffix + ".nodes";

//     cout << "Output Nodes file:" << outputFilePath << endl;

//     FILE *out;
//     out = fopen(outputFilePath.c_str(), "w");
//     if (!out)
//     {
//         cerr << "Cannot open output file\n";
//         return;
//     }

//     fprintf(out, "UCLA nodes 1.0\n\n");
//     fprintf(out, "NumNodes : %d\n", moduleCount);
//     fprintf(out, "NumTerminals : %d\n\n", dbTerminals.size());

//     // non-terminal
//     for (Module *curNode : dbNodes)
//     {
//         double w = curNode->getWidth();
//         double h = curNode->getHeight();

//         fprintf(out, " %30s %10.0f %10.0f\n",
//                 curNode->name.c_str(),
//                 w,
//                 h);
//     }

//     // terminal
//     for (Module *curTerminal : dbTerminals)
//     {
//         double w = curTerminal->getWidth();
//         double h = curTerminal->getHeight();

//         if (curTerminal->isNI)
//         {
//             fprintf(out, " %10s %10.0f %10.0f terminal_NI\n",
//                     curTerminal->name.c_str(),
//                     w,
//                     h);
//         }
//         else
//         {
//             fprintf(out, " %10s %10.0f %10.0f terminal\n",
//                     curTerminal->name.c_str(),
//                     w,
//                     h);
//         }
//     }
//     fprintf(out, "\n\n");
//     fclose(out);
// }

// void PlaceDB::outputPL()
// {
//     string outputFilePath;
//     gArg.GetString("outputPath", &outputFilePath);

//     string benchmarkName;
//     gArg.GetString("benchmarkName", &benchmarkName);

//     string suffix;
//     gArg.GetString("outputSuffix", &suffix);

//     outputFilePath += benchmarkName;
//     outputFilePath += "-" + suffix + ".pl";

//     gArg.Override("outputPL", outputFilePath);

//     cout << "Output PL file:" << outputFilePath << endl;

//     // out "pl"
//     FILE *out = fopen(outputFilePath.c_str(), "w");
//     fprintf(out, "UCLA pl 1.0\n\n");

//     char *orientN = "N";

//     for (Module *curNode : dbNodes)
//     {
//         fprintf(out, "%s\t%.0f\t%.0f : %s",
//                 curNode->name.c_str(),
//                 curNode->getLL_2D().x,
//                 curNode->getLL_2D().y,
//                 orientN);
//         fprintf(out, "\n");
//     }
//     for (Module *curTerminal : dbTerminals)
//     {
//         fprintf(out, "%s\t%.0f\t%.0f : %s",
//                 curTerminal->name.c_str(),
//                 curTerminal->getLL_2D().x,
//                 curTerminal->getLL_2D().y,
//                 orientN);

//         if (curTerminal->isNI)
//             fprintf(out, " /FIXED_NI\n");
//         else
//             fprintf(out, " /FIXED\n");
//     }
//     fprintf(out, "\n\n");

//     fclose(out);
// }

// void PlaceDB::outputNets()
// {
//     string outputFilePath;
//     gArg.GetString("outputPath", &outputFilePath);

//     string benchmarkName;
//     gArg.GetString("benchmarkName", &benchmarkName);

//     string suffix;
//     gArg.GetString("outputSuffix", &suffix);

//     outputFilePath += benchmarkName;
//     outputFilePath += "-" + suffix + ".nets";

//     cout << "Output Nets file:" << outputFilePath << endl;

//     FILE *out;
//     out = fopen(outputFilePath.c_str(), "w");
//     if (!out)
//     {
//         cerr << "Cannot open output file\n";
//         return;
//     }

//     fprintf(out, "UCLA nets 1.0\n\n");
//     fprintf(out, "NumNets : %d\n", dbNets.size());
//     fprintf(out, "NumPins : %d\n", dbPins.size());
//     for (Net curNet : dbNets)
//     {
//         fprintf(out, "NetDegree : %d\n", curNet.netPins.size());
//         for (Pin *curPin : curNet.netPins)
//         {
//             fprintf(out, " %10s B : %.2f %.2f\n",

//                     curPin->module->name.c_str(),
//                     curPin->offset.x,
//                     curPin->offset.y);
//         }
//     }
//     fprintf(out, "\n");
//     fclose(out);
// }

// void PlaceDB::outputSCL()
// {
//     string outputFilePath;
//     gArg.GetString("outputPath", &outputFilePath);

//     string benchmarkName;
//     gArg.GetString("benchmarkName", &benchmarkName);

//     string suffix;
//     gArg.GetString("outputSuffix", &suffix);

//     outputFilePath += benchmarkName;
//     outputFilePath += "-" + suffix + ".scl";

//     cout << "Output SCL file:" << outputFilePath << endl;

//     FILE *out;
//     out = fopen(outputFilePath.c_str(), "w");
//     if (!out)
//     {
//         cerr << "Cannot open output file\n";
//         return;
//     }

//     fprintf(out, "UCLA scl 1.0\n");
//     fprintf(out, "# Created       :\n");
//     fprintf(out, "# User          :\n\n");
//     fprintf(out, "NumRows : %d\n\n", dbSiteRows.size());

//     char *ori[2] = {"N", "Y"};
//     for (SiteRow curRow : dbSiteRows)
//     {
//         double step = curRow.step;
//         if (step == 0)
//             step = 1.0;
//         fprintf(out, "CoreRow Horizontal\n");
//         fprintf(out, " Coordinate    : %8.0f\n", curRow.bottom);
//         fprintf(out, " Height        : %8.0f\n", curRow.height);
//         fprintf(out, " Sitewidth     : %8.0f\n", step);
//         fprintf(out, " Sitespacing   : %8.0f\n", step);
//         fprintf(out, " Siteorient    : 1\n"); //%s\n", ori[i % 2] );
//         fprintf(out, " Sitesymmetry  : 1\n");
//         fprintf(out, " SubrowOrigin  : %8.0f Numsites : %8.0f\n",
//                 curRow.start.x, (curRow.end.x - curRow.start.x) / curRow.step);

//         fprintf(out, "End\n");
//     }
//     fprintf(out, "\n");
//     fclose(out);
// }


// CRect PlaceDB::getOptimialRegion(Module *module)
// {
//     // see the description of the 'optimal region' in the paper: An efficient and effective detailed placement algorithm
//     //? Return a rectangle which has integer width and height. And its height is an integer multiple of the row height while its width align with row site
//     //? is it necessary to align?
//     vector<float> Xs;
//     vector<float> Ys;

//     for (Net *curNet : module->nets)
//     {
//         double maxX = -DOUBLE_MAX;
//         double minX = DOUBLE_MAX;

//         double maxY = -DOUBLE_MAX;
//         double minY = DOUBLE_MAX;

//         double curX;
//         double curY;

//         POS_3D curPos;
//         double HPWL;

//         for (Pin *curPin : curNet->netPins)
//         {
//             if (curPin->module == module)
//             {
//                 continue;
//             }
//             curPos = curPin->absolutePos;
//             curX = curPos.x;
//             curY = curPos.y;

//             minX = min(minX, curX);
//             maxX = max(maxX, curX);
//             minY = min(minY, curY);
//             maxY = max(maxY, curY);
//         }
//         Xs.push_back(minX);
//         Xs.push_back(maxX);
//         Ys.push_back(minY);
//         Ys.push_back(maxY);
//     }

//     // find medians of Xs and Ys.
//     // ! The size of Xs and Ys should be even.
//     float left, right, bottom, up;

//     // left = getKth(Xs, Xs.size() / 2 - 1); // left and right boundary of the optimal region
//     // right = getKth(Xs, Xs.size() / 2);

//     // bottom = getKth(Ys, Ys.size() / 2 - 1);
//     // up = getKth(Ys, Ys.size() / 2);

//     // or

//     sort(Xs.begin(), Xs.end());
//     sort(Ys.begin(), Ys.end());

//     left = Xs[Xs.size() / 2 - 1]; // left and right boundary of the optimal region
//     right = Xs[Xs.size() / 2];

//     bottom = Ys[Ys.size() / 2 - 1];
//     up = Ys[Ys.size() / 2];

//     // create CRect and return

//     CRect result;
//     result.ll.x = left;
//     result.ll.y = bottom;
//     result.ur.x = right;
//     result.ur.y = up;
//     return result;
// }

// bool PlaceDB::isConnected(Module *module1, Module *module2)
// {
//     for (Net *module1Net : module1->nets)
//     {
//         for (Net *module2Net : module2->nets)
//         {
//             if (module1Net == module2Net)
//             {
//                 return true;
//             }
//         }
//     }
//     return false;
// }
