#include <cstdio>
#include <ranges>
#include <assert.h>
#include <string>
#include <sstream>

#include "placer.h"
#include "fft.h"
#include "opt.h"
#include "plot.h"
/*
    All the modules are treated as nodes in 3d placer
    because there's no 3d benchmark.
*/
SpacePlacer::SpacePlacer(PlaceDB *db, double targetDensity)
    : db(db), targetDensity(targetDensity)
{
    fillers.clear();
    freeNodes.clear();
    terminals.clear();
    clkFFs.clear();
    module3DPosition.clear();
    wirelengthGradient.clear();
    densityGradient.clear();
}

// void SpacePlacer::plotCurrentPlacement(std::string figName)
// {
//     std::cout << "=== 绘制当前布局 ===" << std::endl;
//     std::vector<std::vector<double>> module_data;
//     std::vector<std::vector<double>> filler_data;
//     for (Module *module : db->dbModules) // 遍历所有模块
//     {
//         if (!module)
//             continue;

//         // 获取模块的位置信息
//         POS_3D pos = module3DPosition[module];

//         // 记录模块的 (x, y, z, width, height, depth)
//         module_data.push_back({
//             pos.x, pos.y, pos.z,                                 // 模块的起始位置
//             module->width, module->height, 0.1 * moduleThickness // 3D Placer 假设厚度为 1
//         });
//     }
//     for (Module &filler : fillers) // 遍历所有模块
//     {
//         // 获取模块的位置信息
//         POS_3D pos = module3DPosition[&filler];

//         // 记录模块的 (x, y, z, width, height, depth)
//         filler_data.push_back({
//             pos.x, pos.y, pos.z,                               // 模块的起始位置
//             filler.width, filler.height, 0.1 * moduleThickness // 3D Placer 假设厚度为 1
//         });
//     }
//     double core_x = coreCube.getWidth();
//     double core_y = coreCube.getHeight();
//     double core_z = coreCube.getDepth();
//     std::string output_dir = "../result/";
//     if (gArg.CheckExist("outputDir"))
//     {
//         gArg.GetString("outputDir", &output_dir);
//     }

//     Plotter::drawModules(module_data, filler_data, core_x, core_y, core_z, output_dir + figName);

//     std::cout << "=== 布局绘制完成，结果已保存至 " << output_dir + figName << " ===" << std::endl;
// }

void SpacePlacer::placeInitialization()
{
    printf("=== Start SpacePlacer Initialization ===\n");
    setCoreCube();
    double totalModuleArea = 0;
    vector<int> moduleTypeCount;
    // int numTerminalandFF = 0;
    moduleTypeCount.resize(5, 0); // 0: macro, 1: TSV, 2: terminal, 3:standard cell
    for(auto m:db->dbModules)
    {
        totalModuleArea += m->getArea();
        if (m->isMacro)
        {
            moduleTypeCount[0]++;
            if (m->isFF)
            {
                moduleTypeCount[3]++;
            }
        }
        else if (m->isTSV)
        {
            moduleTypeCount[1]++;
        }
        else if (m->isTerminal)
        {
            moduleTypeCount[2]++; 
            if (m->isFF)
            {
                // printf("modulename:%s is a flipflop\n", m->name.c_str());
                // numTerminalandFF++;
                moduleTypeCount[3]++;
            }
        }
        else if (m->isFF)
        {
            moduleTypeCount[3]++;
        }
        else
        {
            moduleTypeCount[4]++;
        }
    }
    printf("Total module area:%f\n", totalModuleArea);
    printf("Macro count:%d, TSV count:%d, Terminals: %d, Flipflops: %d, Standard cell count:%d\n",
           moduleTypeCount[0], moduleTypeCount[1], moduleTypeCount[2], moduleTypeCount[3], moduleTypeCount[4]);
    // printf("numTerminalandFF:%d\n", numTerminalandFF);
    
    fillerInitialization();
    nodeInitialization();
    binInitialization();
    gradientInitialization();
    penaltyFactorInitialization();
    printf("=== Finish SpacePlacer Initialization ===\n");
}

void SpacePlacer::penaltyFactorInitialization()
{
    updateDensityGradient();
    updateWirelengthGradient();
    double numerator = 0;
    double denominator = 0;
    for (auto m : freeNodes)
    {
        const VECTOR_3D &wg = wirelengthGradient[m];
        const VECTOR_3D &dg = densityGradient[m];
        numerator += (abs(wg.x) + abs(wg.y) + abs(wg.z));
        denominator += (abs(dg.x) + abs(dg.y) + abs(dg.z));
    }
    lambda = numerator / denominator;
    printf("Initial penalty factor:%lf\n\n", lambda);
}

///

bool SpacePlacer::stopCondition()
{
    return globalDensityOverflow <=0.1;  //! space placer density overflow threshold   0.03 for ibm01
}

void SpacePlacer::updatePenaltyFactor(double lastHpwl, double curHpwl)
{
    if (gArg.CheckExist("noDen"))
    {
        lambda = 0.0;
        return;
    }
    double multiplier = pow(MULTIPLIER_BASE, -(curHpwl - lastHpwl) / HPWL_REF + 1);
    if (multiplier > MULTIPLIER_UPPERBOUND)
    {
        multiplier = MULTIPLIER_UPPERBOUND;
    }
    else if (multiplier < MULTIPLIER_LOWERBOUND)
    {
        multiplier = MULTIPLIER_LOWERBOUND;
    }
    lambda *= multiplier;
}

void SpacePlacer::place()
{
    auto moveInside = [&](vector<double *> &params)
    {
        for (size_t idx = 0; idx < params.size(); idx++)
        {
            size_t nodeId = idx / 3;
            size_t axis = idx % 3;
            switch (axis)
            {
            case 0:
                *params[idx] = std::clamp(*params[idx],
                                          coreCube.llf.x + freeNodes[nodeId]->width / 2,
                                          coreCube.urb.x - freeNodes[nodeId]->width / 2);
                break;
            case 1:
                *params[idx] = std::clamp(*params[idx],
                                          coreCube.llf.y + freeNodes[nodeId]->height / 2,
                                          coreCube.urb.y - freeNodes[nodeId]->height / 2);
                break;
            case 2:
                *params[idx] = std::clamp(*params[idx],
                                          coreCube.llf.z,
                                          coreCube.urb.z - moduleThickness);
                break;
            default:
                printf("ERROR: axis index should be less than 3 (%d)", axis);
            }
        }
    };
    NesterovOptimizer opt(this, moveInside);
    double hpwl = totalHPWL();
    size_t iterCount = 0;
    printf("=== Start place optimization ===\n");
    while (!stopCondition() && iterCount <= 500)
    {
        printf("iter:%d\n", iterCount);
        printf("penalty factor:%.16f\n", lambda);
        printf("total hpwl:%f\n", hpwl);
        printf("density overflow:%f\n", globalDensityOverflow);
        if (iterCount % 10 == 0 && gArg.CheckExist("Plot"))
        {

            string figName = "GlobalPlacement_iter=" + to_string(iterCount) + ".png";
            // plotCurrentPlacement(figName);
        }

        opt.step();
        printf("\n");
        double newHpwl = totalHPWL();
        updatePenaltyFactor(hpwl, newHpwl);
        hpwl = newHpwl;
        iterCount++;
    }
    printf("sp-place clk aware time:%f\n", clkawareTime);
    printf("=== Finish place optimization ===\n");
}

void SpacePlacer::setCoreCube()
{
    double left = DOUBLE_MAX, right = -DOUBLE_MAX; // boundary in x direction
    double bottom = DOUBLE_MAX, up = -DOUBLE_MAX;  // boundary in y direction
    for (auto &tier : db->dbTiers)
    {
        if (tier.coreRegion.ll.x < left)
        {
            left = tier.coreRegion.ll.x;
        }
        if (tier.coreRegion.ll.y < bottom)
        {
            bottom = tier.coreRegion.ll.y;
        }
        if (tier.coreRegion.ur.x > right)
        {
            right = tier.coreRegion.ur.x;
        }
        if (tier.coreRegion.ur.y > up)
        {
            up = tier.coreRegion.ur.y;
        }
    }
    double front = 0;
    double back = std::min(right - left, up - bottom);
    moduleThickness = back / db->dbTiers.size();
    wirelengthScaleZ = 257 / moduleThickness;
    printf("WirelengthScaleZ: %f\n", wirelengthScaleZ);
    coreCube.llf = POS_3D{left, bottom, front};
    coreCube.urb = POS_3D{right, up, back};
    std::cout << "CoreCube:" << std::endl;
    std::cout << "llf pos: " << coreCube.llf << std::endl;
    std::cout << "urb pos: " << coreCube.urb << std::endl
              << std::endl;
}

vector<double *> SpacePlacer::getParams()
{
    vector<double *> params;
    params.reserve(freeNodes.size() * 3);
    for (auto node : freeNodes)
    {
        // the order should not be changed
        params.push_back(&module3DPosition[node].x);
        params.push_back(&module3DPosition[node].y);
        params.push_back(&module3DPosition[node].z);
    }
    return params;
}

vector<double> SpacePlacer::getGradient()
{
    double dentime = 0.0;
    double wiretime = 0.0;
    time_start(&dentime);
    updateDensityGradient();
    time_end(&dentime);
    time_start(&wiretime);
    updateWirelengthGradient();
    time_end(&wiretime);        
    printf("Density Gradient Time: %f, Wirelength Gradient Time: %f\n", dentime, wiretime);

    vector<double> gradient;
    gradient.reserve(freeNodes.size() * 3);
    for (auto node : freeNodes)
    {
        // preconditioning
        // the preconditioner in 3d eplace no longer needs connectedNetNum, see ePlace-3D (13)
        float connectedNetNum = node->modulePins.size();
        float charge = node->getArea() * moduleThickness;
        float preconditioner = 1 / max(1.0, (lambda * charge)); //+ connectedNetNum
        // the order should not be changed
        gradient.push_back(preconditioner * (wirelengthGradient[node].x + lambda * densityGradient[node].x));
        gradient.push_back(preconditioner * (wirelengthGradient[node].y + lambda * densityGradient[node].y));
        gradient.push_back(preconditioner * (wirelengthGradient[node].z + lambda * densityGradient[node].z));
    }
    return gradient;
}

/*
    initialization
*/
void SpacePlacer::gradientInitialization()
{
    std::vector<std::pair<Module*, VECTOR_3D>> wlPairs, denPairs;
    wlPairs.resize(freeNodes.size());
    denPairs.resize(freeNodes.size());

    #pragma omp parallel for
    for (size_t i = 0; i < freeNodes.size(); ++i)
    {
        Module* m = freeNodes[i];
        wlPairs[i] = {m, VECTOR_3D()};
        denPairs[i] = {m, VECTOR_3D()};
    }

    for (size_t i = 0; i < freeNodes.size(); ++i)
    {
        wirelengthGradient[wlPairs[i].first] = wlPairs[i].second;
        densityGradient[denPairs[i].first] = denPairs[i].second;
    }
    // for (auto m : freeNodes)
    // {
    //     wirelengthGradient[m] = VECTOR_3D();
    //     densityGradient[m] = VECTOR_3D();
    // }
    printf(" Gradient Initialization Finished\n");
}

void SpacePlacer::nodeInitialization()
{
    //  all modules are set to free in 3D placement
    //  i.e. no terminals
    size_t n = db->dbModules.size();
    std::vector<double> areas(n);
    std::vector<POS_3D> positions(n);

    #pragma omp parallel for
    for (size_t i = 0; i < n; ++i)
    {
        Module *m = db->dbModules[i];
        areas[i] = m->getArea();
        POS_3D initialPos = coreCube.getCenter();
        initialPos.z -= 0.5 * moduleThickness;
        positions[i] = initialPos;
    }

    freeNodesArea = 0;
    freeNodes.clear();
    for (size_t i = 0; i < n; ++i)
    {
        Module *m = db->dbModules[i];
        freeNodesArea += areas[i];
        freeNodes.push_back(m);
        module3DPosition[m] = positions[i];
    }

    
    // freeNodesArea = 0;
    // for (Module *m : db->dbModules)
    // {
    //     freeNodesArea += m->getArea();
    //     freeNodes.push_back(m);
    //     POS_3D initialPos = coreCube.getCenter();
    //     // initialPos.z = coreCube.llf.z; 
    //     initialPos.z -= 0.5*moduleThickness; //!初始化所有模块的z坐标在整个3d布局空间的中间位置
    //     module3DPosition[m] = initialPos;
    // }

    for (Module &f : fillers)
    {
        freeNodesArea += f.getArea();
        freeNodes.push_back(&f);
        module3DPosition[&f] = POS_3D{uniformDistribution(coreCube.llf.x, coreCube.urb.x),
                                      uniformDistribution(coreCube.llf.y, coreCube.urb.y),
                                      uniformDistribution(coreCube.llf.z, coreCube.urb.z - moduleThickness)};
    }
    printf("Free Nodes Initialization Finished, freeNodesArea: %f\n", freeNodesArea);
}

double SpacePlacer::computeAvgCellVolume()
{
    double totalVolume = 0;
    int standardCellCount = 0;

    #pragma omp parallel for reduction(+:totalVolume,standardCellCount)
    for (size_t i = 0; i < db->dbModules.size(); ++i)
    {
        Module *module = db->dbModules[i];
        if (!module->isMacro)
        {
            double width = module->width;
            double height = module->height;
            double thickness = moduleThickness;
            totalVolume += width * height * thickness;
            standardCellCount++;
        }
    }
    // for (Module *module : db->dbModules) // 遍历所有模块
    // {
    //     if (!module->isMacro) // 只计算标准单元
    //     {
    //         double width = module->width;
    //         double height = module->height;
    //         double thickness = moduleThickness; // 3D 厚度始终为 1

    //         totalVolume += width * height * thickness;
    //         standardCellCount++;
    //     }
    // }

    if (standardCellCount == 0)
    {
        printf("Warning: No standard cells found. Using default average volume.\n");
        return 1.0;
    }

    return totalVolume / standardCellCount;
}

void SpacePlacer::storeBins(int bx,int by, int bz, double minX, double minY, double minZ, double binWidth, double binHeight, double binThickness)
{

    bins.clear();
    bins.resize(bx, std::vector<std::vector<Bin3D>>(by, std::vector<Bin3D>(bz)));

    for (int x = 0; x < bx; ++x)
    {
        for (int y = 0; y < by; ++y)
        {
            for (int z = 0; z < bz; ++z)
            {
                bins[x][y][z].llf = POS_3D{
                    minX + x * binWidth,
                    minY + y * binHeight,
                    minZ + z * binThickness};

                bins[x][y][z].urb = POS_3D{
                    bins[x][y][z].llf.x + binWidth,
                    bins[x][y][z].llf.y + binHeight,
                    bins[x][y][z].llf.z + binThickness};

                bins[x][y][z].center = POS_3D{
                    (bins[x][y][z].llf.x + bins[x][y][z].urb.x) / 2.0,
                    (bins[x][y][z].llf.y + bins[x][y][z].urb.y) / 2.0,
                    (bins[x][y][z].llf.z + bins[x][y][z].urb.z) / 2.0};

                bins[x][y][z].width = binWidth;
                bins[x][y][z].height = binHeight;
                bins[x][y][z].thickness = binThickness;
                bins[x][y][z].volume = binWidth * binHeight * binThickness;
            }
        }
    }

    printf("Stored %d x %d x %d bins\n", bx, by, bz);
}

int SpacePlacer::nearestPowerOfTwo(int num)
{
    if (num < 1)
        return 1;
    int power = 1;
    while (power * 2 <= num)
    {
        power *= 2;
    }
    return power;
}

void SpacePlacer::binInitialization()
{
    bins.clear();

    if (db->dbModules.empty())
    {
        printf("Error: No modules found in database.\n");
        return;
    }

    double moduleWidth = coreCube.getWidth();
    double moduleHeight = coreCube.getHeight();
    double moduleDepth = coreCube.getDepth();

    double avgCellVolume = computeAvgCellVolume();
    double volumeR = coreCube.getVolume();
    int totalBins = volumeR / (avgCellVolume * targetDensity);
    printf("total bins: %d, avgCellVolume: %f, targetDensity: %f, volumeR: %f\n",
           totalBins, avgCellVolume, targetDensity, volumeR);   

    //如果想减少z方向bin数量为xy方向的一半， 则totalbins*2再开三次方，下面的bz/2
    int m3D = std::cbrt(totalBins); 
    m3D = nearestPowerOfTwo(std::max(4, std::min(m3D, 1024))); // 保证 `m3D` 为 `2^n`

    int bx = m3D;
    int by = m3D;
    int bz = m3D; 
    double binWidth = moduleWidth / bx;
    double binHeight = moduleHeight / by;
    double binThickness = moduleDepth / bz;

    storeBins(bx,by,bz,  coreCube.llf.x, coreCube.llf.y, coreCube.llf.z, binWidth, binHeight, binThickness);

    printf("Bins Updated Based on CoreCube: %d (X) * %d (Y) * %d (Z) = %d total bins\n",
           bx, by, bz, bx * by * bz);
}

void SpacePlacer::fillerInitialization()
{
    // set the filler dimension to the average of all std cells
    double totalStandardCellArea = 0;
    double totalMacroArea = 0;
    int numMacros = 0;
    size_t numStandardCells = 0;
    for (Module *module : db->dbModules)
    {
        if (!module->isMacro)
        {
            totalStandardCellArea += module->getArea();
            numStandardCells++;
        }
        else
        {
            totalMacroArea += module->getArea();
            numMacros++;
        }
    }
    double avgFillerArea = totalStandardCellArea / numStandardCells;
    double fillerHeight = db->commonRowHeight;
    double fillerWidth = avgFillerArea / fillerHeight;

    // determine filler numbers
    double totalTierArea = db->totalTierArea();
    double totalModuleArea = db->totalModuleArea();
    // no terminals

    double macroRatio = totalMacroArea / totalTierArea;
    macroRatio = std::round(macroRatio * 100.0) / 100.0; // 保留两位小数
    partitionMacroRatioConstraint = macroRatio + 0.05; // set a loose constraint
    printf("macro count:%d, totalMacroArea:%f, totalTierArea:%f\n", numMacros, totalMacroArea, totalTierArea);
    printf("macroratio: %.2f     ratio constraint: %.2f\n", macroRatio, partitionMacroRatioConstraint);
    



    double totalWhiteSpaceArea = totalTierArea;
    double totalFillerArea = targetDensity * totalWhiteSpaceArea - totalModuleArea;
    size_t fillerNum = static_cast<size_t>(floor(totalFillerArea / avgFillerArea));
    fillers.reserve(fillerNum);
    for (size_t i = 0; i < fillerNum; i++)
    {
        string fillerName = "f" + to_string(i);
        Module curFiller(fillerName, fillerWidth, fillerHeight, FREE, false);
        fillers.push_back(curFiller);
    }
    printf("filler:\n");
    printf("totalTierArea:%f\n", totalTierArea);
    printf("totalFillerArea:%f\tfillerNum:%d\n\n", totalFillerArea, fillerNum);
    printf("fillerWidth:%f\tfillerHeight:%f\n\n", fillerWidth, fillerHeight);
}

/*
    wirelength related
*/

void zeroGradient(unordered_map<Module *, VECTOR_3D> &g)
{
    for (auto &[k, v] : g)
    {
        v.SetZero();
    }
}

template <std::ranges::range R>
    requires std::same_as<std::ranges::range_value_t<R>, POS_3D>
std::pair<POS_3D, POS_3D> getBoundingBox(const R &pos)
{
    POS_3D XYZmin(DOUBLE_MAX, DOUBLE_MAX, DOUBLE_MAX);
    POS_3D XYZmax(-DOUBLE_MAX, -DOUBLE_MAX, -DOUBLE_MAX);
    for (auto p : pos)
    {
        if (p.x > XYZmax.x)
        {
            XYZmax.x = p.x;
        }
        if (p.x < XYZmin.x)
        {
            XYZmin.x = p.x;
        }
        if (p.y > XYZmax.y)
        {
            XYZmax.y = p.y;
        }
        if (p.y < XYZmin.y)
        {
            XYZmin.y = p.y;
        }
        if (p.z > XYZmax.z)
        {
            XYZmax.z = p.z;
        }
        if (p.z < XYZmin.z)
        {
            XYZmin.z = p.z;
        }
    }
    return std::make_pair(XYZmin, XYZmax);
}

double SpacePlacer::getNetHPWL(const Net &n)
{
    if (n.netPins.empty())
        return 0.0f;
    auto [bbmax, bbmin] = getBoundingBox(
        n.netPins | std::ranges::views::transform([&](Pin *p)
                                                  {
        const POS_3D& modPos = module3DPosition[p->module];
        POS_3D pinPos;
        pinPos.x = p->offset.x + modPos.x;
        pinPos.y = p->offset.y + modPos.y;
        pinPos.z = modPos.z; 
        return pinPos; }));
    return abs(bbmax.x - bbmin.x) + abs(bbmax.y - bbmin.y) + wirelengthScaleZ * abs(bbmax.z - bbmin.z);
}

void SpacePlacer::addClkNetGradient()
{
    vector<Pin*> pseudoClkPins;
    for(auto m: db->dbFFs)
    {
        pseudoClkPins.push_back(new Pin{m, POS_2D{0, 0},PinDirection::PIN_DIRECTION_IN}); // create a pseudo pin for each FF
    }
    // get clock net
    Net pseudoClkNet("pseudo_clk", pseudoClkPins);

    // prepare cache
    vector<WAWirelengthGradientCache> cache(pseudoClkPins.size());
    addWAWirelengthGradientOneNet(pseudoClkNet, cache);
    for(auto pin: pseudoClkPins)
    {
        delete pin; // delete the pseudo pin
    }
}

void SpacePlacer::addClkNetGradientMassCenter()
{
    // calculate the mass center of all FFs
    POS_3D massCenter(0, 0, 0);
    for (auto m : db->dbFFs)
    {
        const POS_3D &modPos = module3DPosition[m];
        massCenter.x += modPos.x;
        massCenter.y += modPos.y;
        massCenter.z += modPos.z;
    }
    massCenter.x /= db->dbFFs.size();
    massCenter.y /= db->dbFFs.size();
    massCenter.z /= db->dbFFs.size();

    // set the gradient to the mass center
    auto sign = [](double x) -> double
    {
        if (x > 0)
            return 1.0;
        else if (x < 0)
            return -1.0;
        else
            return 0.0;
    };
    for (auto m : db->dbFFs)
    {
        const POS_3D &modPos = module3DPosition[m];
        auto curSinkGradient = VECTOR_3D{
            sign(modPos.x - massCenter.x),
            sign(modPos.y - massCenter.y),
            sign(modPos.z - massCenter.z)
        };
        wirelengthGradient[m] += curSinkGradient;
    }
}


/*
    the cache is used to avoid extra vector allocation
*/
void SpacePlacer::addWAWirelengthGradientOneNet(const Net &n, vector<WAWirelengthGradientCache> &cache)
{
    cache.resize(n.netPins.size());
    // calculate pin position
    for (size_t i = 0; i < n.netPins.size(); i++)
    {
        const POS_3D &modPos = module3DPosition[n.netPins[i]->module];
        cache[i].pinPos.x = n.netPins[i]->offset.x + modPos.x;
        cache[i].pinPos.y = n.netPins[i]->offset.y + modPos.y;
        cache[i].pinPos.z = modPos.z;
    }
    auto [bbmin, bbmax] = getBoundingBox(cache | std::ranges::views::transform([](WAWirelengthGradientCache &c)
                                                                               { return c.pinPos; }));

    // calculate necessary values to assemble the gradient
    VECTOR_3D numeratorPositive, numeratorNegative;
    VECTOR_3D denominatorPositive, denominatorNegative;
    numeratorPositive.SetZero();
    numeratorNegative.SetZero();
    denominatorPositive.SetZero();
    denominatorNegative.SetZero();
    for (size_t i = 0; i < n.netPins.size(); i++)
    {
        const POS_3D &curPinPos = cache[i].pinPos;
        VECTOR_3D &curExpPositive = cache[i].expPositive;
        VECTOR_3D &curExpNegative = cache[i].expNegative;
        VECTOR_3D expMax; // (Xi-Xmax)/gamma in WA model (X/Y/Z)
        VECTOR_3D expMin; // (Xmin-Xi)/gamma in WA model (X/Y/Z)
        expMax.x = (curPinPos.x - bbmax.x) * invertedGamma.x;
        expMin.x = (bbmin.x - curPinPos.x) * invertedGamma.x;
        expMax.y = (curPinPos.y - bbmax.y) * invertedGamma.y;
        expMin.y = (bbmin.y - curPinPos.y) * invertedGamma.y;
        expMax.z = (curPinPos.z - bbmax.z) * invertedGamma.z;
        expMin.z = (bbmin.z - curPinPos.z) * invertedGamma.z;

        curExpPositive.x = fastExp(expMax.x);
        numeratorPositive.x += curPinPos.x * curExpPositive.x;
        denominatorPositive.x += curExpPositive.x;
        curExpNegative.x = fastExp(expMin.x);
        numeratorNegative.x += curPinPos.x * curExpNegative.x;
        denominatorNegative.x += curExpNegative.x;

        curExpPositive.y = fastExp(expMax.y);
        numeratorPositive.y += curPinPos.y * curExpPositive.y;
        denominatorPositive.y += curExpPositive.y;
        curExpNegative.y = fastExp(expMin.y);
        numeratorNegative.y += curPinPos.y * curExpNegative.y;
        denominatorNegative.y += curExpNegative.y;

        curExpPositive.z = fastExp(expMax.z);
        numeratorPositive.z += curPinPos.z * curExpPositive.z;
        denominatorPositive.z += curExpPositive.z;
        curExpNegative.z = fastExp(expMin.z);
        numeratorNegative.z += curPinPos.z * curExpNegative.z;
        denominatorNegative.z += curExpNegative.z;
    }

    // assemble the gradient
    for (size_t i = 0; i < n.netPins.size(); i++)
    {
        VECTOR_3D &curWirelengthGradient = wirelengthGradient[n.netPins[i]->module];
        const VECTOR_3D curPinVec(cache[i].pinPos.x, cache[i].pinPos.y, cache[i].pinPos.z);
        const VECTOR_3D &curExpPositive = cache[i].expPositive;
        const VECTOR_3D &curExpNegative = cache[i].expNegative;
        VECTOR_3D &&curExpPositiveDivideGamma = curExpPositive * invertedGamma;
        VECTOR_3D &&curExpNegativeDivideGamma = curExpNegative * invertedGamma;

        VECTOR_3D &&curPinGradientPositiveTerm = VECTOR_3D{1.0,1.0,wirelengthScaleZ} *
            ((curExpPositive + curExpPositiveDivideGamma * curPinVec) * denominatorPositive - curExpPositiveDivideGamma * numeratorPositive) / (denominatorPositive * denominatorPositive);
        VECTOR_3D &&curPinGradientNegativeTerm = VECTOR_3D{1.0,1.0,wirelengthScaleZ} *
            ((curExpNegative - curExpNegativeDivideGamma * curPinVec) * denominatorNegative + curExpNegativeDivideGamma * numeratorNegative) / (denominatorNegative * denominatorNegative);
        curWirelengthGradient += (curPinGradientPositiveTerm - curPinGradientNegativeTerm);
    }
}

void SpacePlacer::addLSEWirelengthGradientOneNet(const Net &n)
{
    printf("LSE model not implemented!\n");
    exit(-1);
}

void SpacePlacer::updateWirelengthGradient()
{
    // first set the gradient to zero
    zeroGradient(wirelengthGradient);
    if (gArg.CheckExist("LSE"))
    {
        for (auto net : db->dbNets)
        {
            addLSEWirelengthGradientOneNet(*net);
        }
    }
    else
    {
        // default to WA model
        updateDensityOverflow();
        // calculate inverted gamma
        invertedGamma = VECTOR_3D{1.0 / 8, 1.0 / 8, 1.0 / 8};
        invertedGamma.x /= bins[0][0][0].width;
        invertedGamma.y /= bins[0][0][0].height;
        invertedGamma.z /= bins[0][0][0].thickness;
        if (globalDensityOverflow > 1.0)
        {
            invertedGamma *= 0.1;
        }
        else if (globalDensityOverflow < 0.1)
        {
            invertedGamma *= 10;
        }
        else
        {
            double exp = 1.0 / pow(10.0, (globalDensityOverflow - 0.1) * 20 / 9.0 - 1.0);
            invertedGamma *= exp;
        }
        vector<WAWirelengthGradientCache> cache(db->maxNetDegree);
        for (auto net : db->dbNets)
        {
            addWAWirelengthGradientOneNet(*net, cache);
        }
        // #pragma omp parallel for schedule(dynamic)
        // for (size_t i = 0; i < db->dbNets.size(); ++i)
        // {
        //     vector<WAWirelengthGradientCache> localCache(db->maxNetDegree);
        //     addWAWirelengthGradientOneNet(*db->dbNets[i], localCache);
        // }
        if (gArg.CheckExist("pseudoClkNet"))
        {
            double clktime = 0.0;
            time_start(&clktime);
            addClkNetGradient();
            time_end(&clktime);
            clkawareTime += clktime;
        }
        if (gArg.CheckExist("clockaware") && gArg.CheckExist("pseudoClkMassCenter"))
        {
            addClkNetGradientMassCenter();
        }
    }
}

double SpacePlacer::totalHPWL()
{
    double hpwl = 0;
    for (size_t i = 0; i < db->dbNets.size(); ++i)
    {
        hpwl += getNetHPWL(*db->dbNets[i]);
    }
    return hpwl;
}

/*
    density related
*/

double getOverlapVolume(const POS_3D &llf1, const POS_3D &urb1,
                        const POS_3D &llf2, const POS_3D &urb2)
{
    double dx = std::max(0.0, std::min(urb1.x, urb2.x) - std::max(llf1.x, llf2.x));
    double dy = std::max(0.0, std::min(urb1.y, urb2.y) - std::max(llf1.y, llf2.y));
    double dz = std::max(0.0, std::min(urb1.z, urb2.z) - std::max(llf1.z, llf2.z));

    return dx * dy * dz; // 体积 = 宽 * 高 * 深
}

void SpacePlacer::updateBinDensity()
{
    for (int x = 0; x < bins.size(); ++x)
    {
        for (int y = 0; y < bins[x].size(); ++y)
        {
            for (int z = 0; z < bins[x][y].size(); ++z)
            {
                bins[x][y][z].nodeDensity = 0;
            }
        }
    }
    #pragma omp parallel for
    for (Module *node : freeNodes)
    {
        POS_3D nodeCenter = {
            module3DPosition[node].x,
            module3DPosition[node].y,
            module3DPosition[node].z + moduleThickness / 2.0};

        double nodeVolume = node->width * node->height * moduleThickness;

        double binWidth = bins[0][0][0].width;
        double binHeight = bins[0][0][0].height;
        double binThickness = bins[0][0][0].thickness;

        VECTOR_3D localSmoothLengthScale; // see ePlace paper page 15 or RePlace opt.cpp line 1460
        localSmoothLengthScale.x = 1.0;
        localSmoothLengthScale.y = 1.0;
        localSmoothLengthScale.z = 1.0;

        CRect3D rectForCurNode;
        //! beware: local smooth on x and y dimension, also needed here!
        //! binStart and binEnd should be calculated with inflated cell width and height, see replace charge.cpp line 408
        POS_3D cellPosition = module3DPosition[node];
        POS_3D &&cellCenter = cellPosition + POS_3D{0, 0, moduleThickness / 2.0};
        rectForCurNode.llf = cellPosition - POS_3D{node->width / 2.0, node->height / 2.0, 0};
        rectForCurNode.urb = cellPosition + POS_3D{node->width / 2.0, node->height / 2.0, moduleThickness};

        // if (!curNode->isMacro)
        // {
        //! local smooth: not only for std cells because there may be small macros, like in MMS bigblue3
        if (float_less(node->width, binWidth))
        {
            localSmoothLengthScale.x = node->width / binWidth;
            rectForCurNode.llf.x = cellCenter.x - 0.5 * binWidth;
            rectForCurNode.urb.x = cellCenter.x + 0.5 * binWidth;
        }
        if (float_less(node->height, binHeight))
        {
            localSmoothLengthScale.y = node->height / binHeight;
            rectForCurNode.llf.y = cellCenter.y - 0.5 * binHeight;
            rectForCurNode.urb.y = cellCenter.y + 0.5 * binHeight;
        }
        if (float_less(moduleThickness, binThickness))
        {
            localSmoothLengthScale.z = moduleThickness / binThickness;
            rectForCurNode.llf.z = cellCenter.z - 0.5 * binThickness;
            rectForCurNode.urb.z = cellCenter.z + 0.5 * binThickness;
        }

        int binStartX = std::max(0, (int)std::floor((nodeCenter.x - node->width / 2.0 - coreCube.llf.x) / binWidth));
        int binEndX = std::min((int)bins.size() - 1, (int)std::floor((nodeCenter.x + node->width / 2.0 - coreCube.llf.x) / binWidth));

        int binStartY = std::max(0, (int)std::floor((nodeCenter.y - node->height / 2.0 - coreCube.llf.y) / binHeight));
        int binEndY = std::min((int)bins[0].size() - 1, (int)std::floor((nodeCenter.y + node->height / 2.0 - coreCube.llf.y) / binHeight));

        int binStartZ = std::max(0, (int)std::floor((nodeCenter.z - moduleThickness / 2.0 - coreCube.llf.z) / binThickness));
        int binEndZ = std::min((int)bins[0][0].size() - 1, (int)std::floor((nodeCenter.z + moduleThickness / 2.0 - coreCube.llf.z) / binThickness));

        for (int x = binStartX; x <= binEndX; ++x)
        {
            for (int y = binStartY; y <= binEndY; ++y)
            {
                for (int z = binStartZ; z <= binEndZ; ++z)
                {
                    // Bin3D &bin = bins[x][y][z];
                    double scale = localSmoothLengthScale.x * localSmoothLengthScale.y * localSmoothLengthScale.z;
                    double overlapVolume = scale * getOverlapVolume(bins[x][y][z].llf, bins[x][y][z].urb, rectForCurNode.llf, rectForCurNode.urb);
                    #pragma omp atomic
                    bins[x][y][z].nodeDensity += overlapVolume;
                }
            }
        }
    }
}

// double getOverlapVolume(const POS_3D &llf1, const POS_3D &urb1, const POS_3D &llf2, const POS_3D &urb2)
// {
//     double overlapX = getOverlap(llf1.x,urb1.x,llf2.x,urb2.x);
//     double overlapY = getOverlap(llf1.y,urb1.y,llf2.y,urb2.y);
//     double overlapZ = getOverlap(llf1.z,urb1.z,llf2.z,urb2.z);
//     return overlapX * overlapY * overlapZ;
// }

void SpacePlacer::updateDensityGradient()
{
    double bintime = 0.0;
    double ffttime = 0.0;
    time_start(&bintime);
    updateBinDensity();
    time_end(&bintime);
    time_start(&ffttime);
    double binWidth = bins[0][0][0].width;
    double binHeight = bins[0][0][0].height;
    double binThickness = bins[0][0][0].thickness;
    size_t bindimX = bins.size();
    size_t bindimY = bins[0].size();
    size_t bindimZ = bins[0][0].size();
    replace::FFT_3D fft(bindimX, bindimY, bindimZ, binWidth, binHeight, binThickness);
    double binVolume = bins[0][0][0].getVolume();
    for (size_t i = 0; i < bindimX; i++)
    {
        for (size_t j = 0; j < bindimY; j++)
        {
            for (size_t k = 0; k < bindimZ; k++)
            {
                double rho = bins[i][j][k].baseDensity + bins[i][j][k].nodeDensity + bins[i][j][k].terminalDensity;
                rho /= binVolume;
                fft.updateDensity(i, j, k, rho);
            }
        }
    }
    fft.doFFT();
    for (size_t i = 0; i < bindimX; i++)
    {
        for (size_t j = 0; j < bindimY; j++)
        {
            for (size_t k = 0; k < bindimZ; k++)
            {
                auto [ex, ey, ez] = fft.getElectroForce(i, j, k);
                bins[i][j][k].E.x = ex;
                bins[i][j][k].E.y = ey;
                bins[i][j][k].E.z = ez;
                auto phi = fft.getElectroPhi(i, j, k);
                bins[i][j][k].phi = phi;
            }
        }
    }
    time_end(&ffttime);
    
    // update density gradient for free nodes
    zeroGradient(densityGradient);
    double localsmoothtime = 0.0;
    time_start(&localsmoothtime);
    // #pragma 
    #pragma omp parallel for default(shared)
    for (size_t nodeIdx = 0; nodeIdx < freeNodes.size(); nodeIdx++)
    {
        Module *curNode = freeNodes[nodeIdx];

        VECTOR_3D localSmoothLengthScale; // see ePlace paper page 15 or RePlace opt.cpp line 1460
        localSmoothLengthScale.x = 1.0;
        localSmoothLengthScale.y = 1.0;
        localSmoothLengthScale.z = 1.0;

        CRect3D rectForCurNode;

        //! beware: local smooth on x and y dimension, also needed here!
        //! binStart and binEnd should be calculated with inflated cell width and height, see replace charge.cpp line 408
        POS_3D cellPosition = module3DPosition[curNode];
        POS_3D &&cellCenter = cellPosition + POS_3D{0, 0, moduleThickness / 2.0};
        rectForCurNode.llf = cellPosition - POS_3D{curNode->width / 2.0, curNode->height / 2.0, 0};
        rectForCurNode.urb = cellPosition + POS_3D{curNode->width / 2.0, curNode->height / 2.0, moduleThickness};

        // if (!curNode->isMacro)
        // {
        //! local smooth: not only for std cells because there may be small macros, like in MMS bigblue3
        if (float_less(curNode->width, binWidth))
        {
            localSmoothLengthScale.x = curNode->width / binWidth;
            rectForCurNode.llf.x = cellCenter.x - 0.5 * binWidth;
            rectForCurNode.urb.x = cellCenter.x + 0.5 * binWidth;
        }
        if (float_less(curNode->height, binHeight))
        {
            localSmoothLengthScale.y = curNode->height / binHeight;
            rectForCurNode.llf.y = cellCenter.y - 0.5 * binHeight;
            rectForCurNode.urb.y = cellCenter.y + 0.5 * binHeight;
        }
        if (float_less(moduleThickness, binThickness))
        {
            localSmoothLengthScale.z = moduleThickness / binThickness;
            rectForCurNode.llf.z = cellCenter.z - 0.5 * binThickness;
            rectForCurNode.urb.z = cellCenter.z + 0.5 * binThickness;
        }
        // }

        int binStartX = std::max(0, (int)std::floor((cellCenter.x - curNode->width / 2.0 - coreCube.llf.x) / binWidth));
        int binEndX = std::min((int)bins.size() - 1, (int)std::floor((cellCenter.x + curNode->width / 2.0 - coreCube.llf.x) / binWidth));

        int binStartY = std::max(0, (int)std::floor((cellCenter.y - curNode->height / 2.0 - coreCube.llf.y) / binHeight));
        int binEndY = std::min((int)bins[0].size() - 1, (int)std::floor((cellCenter.y + curNode->height / 2.0 - coreCube.llf.y) / binHeight));

        int binStartZ = std::max(0, (int)std::floor((cellCenter.z - moduleThickness / 2.0 - coreCube.llf.z) / binThickness));
        int binEndZ = std::min((int)bins[0][0].size() - 1, (int)std::floor((cellCenter.z + moduleThickness / 2.0 - coreCube.llf.z) / binThickness));

        assert(binStartX >= 0);
        assert(binEndX >= 0);
        assert(binStartY >= 0);
        assert(binEndY >= 0);
        assert(binStartZ >= 0);
        assert(binEndZ >= 0);

        // if (binEndIdx.y >= binDimension.y)
        // {
        //     binEndIdx.y = binDimension.y - 1;
        // }

        // if (binEndIdx.x >= binDimension.x)
        // {
        //     binEndIdx.x = binDimension.x - 1;
        // }

        //! beware: local smooth
        for (int x = binStartX; x <= binEndX; ++x)
        {
            for (int y = binStartY; y <= binEndY; ++y)
            {
                for (int z = binStartZ; z <= binEndZ; ++z)
                {
                    double scale = localSmoothLengthScale.x * localSmoothLengthScale.y * localSmoothLengthScale.z;
                    double overlapVolume = scale * getOverlapVolume(bins[x][y][z].llf, bins[x][y][z].urb, rectForCurNode.llf, rectForCurNode.urb);
                    VECTOR_3D &curNodeDensityGradient = densityGradient[curNode];
                    curNodeDensityGradient -= bins[x][y][z].E * overlapVolume;
                }
            }
        }
    }
    time_end(&localsmoothtime);
    // printf("Update Bin Density Time: %f, FFT Time: %f， localsmooth Time: %f \n", bintime, ffttime, localsmoothtime);
}

void SpacePlacer::updateDensityOverflow()
{
    double globalOverflowVolume = 0;
    double binVolume = bins[0][0][0].getVolume();
    size_t bx = bins.size();
    size_t by = bx > 0 ? bins[0].size() : 0;
    size_t bz = (bx > 0 && by > 0) ? bins[0][0].size() : 0;

    #pragma omp parallel for collapse(3) reduction(+:globalOverflowVolume)
    for (size_t x = 0; x < bx; ++x)
        for (size_t y = 0; y < by; ++y)
            for (size_t z = 0; z < bz; ++z)
            {
                double binOverflowVolum = std::max(0.0, bins[x][y][z].baseDensity + bins[x][y][z].nodeDensity - targetDensity * binVolume);
                globalOverflowVolume += binOverflowVolum;
            }
    globalDensityOverflow = globalOverflowVolume / (freeNodesArea * moduleThickness);
}

/*
    partition
*/
void SpacePlacer::InitialPartition(std::unordered_map<Module *, ModulePosition> &partitionedPosition, bool isFirstCall)
{
    // 计算每层的最大可用面积
    int numLayers = db->dbTiers.size();
    std::vector<double> maxAreaPerLayer(numLayers);
    TSVCountPerTier.resize(numLayers, 0);  

    if (isFirstCall)
    {
        // 如果是第一次调用，
        printf("first pre partition\n");
        for (int i = 0; i < numLayers; ++i)
        {
            // 预设每层的最大面积为，第一次层还要考虑固定pin的面积，高层要考虑TSV
            maxAreaPerLayer[i] = 0.85 * db->dbTiers[i].getArea(); //设为85是因为2d-benchmark的初始面积占比是80%
            if (i==0)
            {
                maxAreaPerLayer[i] = 0.85 * db->dbTiers[i].getArea(); 
            }
        }
    }
    else
    {
        // 不是第一次调用，则根据TSV数量预留空间。
        //! 第一层还要考虑固定pin的面积，高层要考虑TSV
        printf("===real partition===\n");
        for (int i = 0; i < numLayers; ++i)
        {
            double totalTSVarea = ((double)TSVCountPerTier[i]) * db->TSVsize * db->TSVsize; // TSV面积
            printf("TSVCountPerTier[%d]: %d, total TSV area: %f\n", i, TSVCountPerTier[i], totalTSVarea);
            maxAreaPerLayer[i] = 0.95 * db->dbTiers[i].getArea() - totalTSVarea; 
        }
    }
    // printf("Layer %d max area: %f\n", i, maxAreaPerLayer[i]);
    

    // 初始化每层的已使用面积
    std::vector<double> layerAreaUsed(numLayers, 0.0);
    std::vector<double> layerMacroAreaUsed(numLayers, 0.0);

    // 按模块面积从大到小排序
    std::vector<Module *> sortedModules = db->dbModules;
    std::sort(sortedModules.begin(), sortedModules.end(), [](Module *a, Module *b)
              { return a->area > b->area; });

    // 用于记录每层的占用空间，二维布尔数组表示是否被占用
    // std::vector<std::vector<bool>> occupied(numLayers);
    // for (int i = 0; i < numLayers; ++i)
    // {
    //     int width = db->dbTiers[i].coreRegion.getWidth();
    //     int height = db->dbTiers[i].coreRegion.getHeight();
    //     occupied[i].resize(width * height, false); // 初始化为未占用
    // }

    // 遍历所有模块，尝试放置到合适的层
    const auto distance = [](double l, double r, double p)
    {
        if (p < l)
            return l - p;
        else if (p > r)
            return p - r;
        else
            return 0.0;
    };

    for (Module *module : sortedModules)
    {
        double moduleWidth = module->width;
        double moduleHeight = module->height;
        double moduleArea = module->getArea();
        POS_3D pos3D = module3DPosition[module];
        double z = pos3D.z;

        bool placed = false;

        // 按距离 `z` 由近到远排序层，层的选择顺序从接近模块位置的层开始
        std::vector<int> layerOrder(numLayers);
        double thicknessScaleFactor =  1.0 -  1 / (double) numLayers;
        std::iota(layerOrder.begin(), layerOrder.end(), 0);
        std::sort(layerOrder.begin(), layerOrder.end(), [&](int a, int b)
                  { return  distance(a * thicknessScaleFactor * moduleThickness + coreCube.llf.z, (a + 1) * thicknessScaleFactor * moduleThickness + coreCube.llf.z, z) <
                             distance(b * thicknessScaleFactor * moduleThickness + coreCube.llf.z, (b + 1) * thicknessScaleFactor * moduleThickness + coreCube.llf.z, z); });

                  //   { return std::abs(z - coreCube.llf.z - a * (moduleThickness * thicknessScaleFactor)) < std::abs(z - coreCube.llf.z - b * thicknessScaleFactor * moduleThickness); });
        
        
        // 遍历排序后的层，选择最近且未超载的层
        for (int layer : layerOrder)
        {
            // 检查该层是否有足够的剩余空间
            if (layerAreaUsed[layer] + moduleArea <= maxAreaPerLayer[layer] &&
                moduleWidth <= db->dbTiers[layer].coreRegion.getWidth() &&
                moduleHeight <= db->dbTiers[layer].coreRegion.getHeight())
            {

                // 尝试找到一个空闲位置来放置模块
                // bool canPlace = false;
                // for (int x = 0; x < db->dbTiers[layer].coreRegion.getWidth() - moduleWidth; ++x) {
                //     for (int y = 0; y < db->dbTiers[layer].coreRegion.getHeight() - moduleHeight; ++y) {
                //         bool areaOccupied = false;
                //         for (int i = x; i < x + moduleWidth; ++i) {
                //             for (int j = y; j < y + moduleHeight; ++j) {
                //                 if (occupied[layer][i * db->dbTiers[layer].coreRegion.getHeight() + j]) {
                //                     areaOccupied = true;
                //                     break;
                //                 }
                //             }
                //             if (areaOccupied) break;
                //         }

                //         // 如果该区域未被占用，则可以放置模块
                //         if (!areaOccupied) {
                //             canPlace = true;

                //             // 更新已占用区域
                //             for (int i = x; i < x + moduleWidth; ++i) {
                //                 for (int j = y; j < y + moduleHeight; ++j) {
                //                     occupied[layer][i * db->dbTiers[layer].coreRegion.getHeight() + j] = true;
                //                 }
                //             }
                // 更新模块位置
                
                if (module->isMacro &&
                    layerMacroAreaUsed[layer] + moduleArea <= partitionMacroRatioConstraint * db->dbTiers[layer].coreRegion.getArea() )
                {
                    // printf("laymacroareaused[%d]: %f, module area: %f, max macro area: %f\n", layer, layerMacroAreaUsed[layer], moduleArea, partitionMacroRatioConstraint * db->dbTiers[layer].coreRegion.getArea());
                    layerMacroAreaUsed[layer] += moduleArea; // 更新该层的宏块已使用面积
                }
                else if (module->isMacro &&
                         layerMacroAreaUsed[layer] + moduleArea > partitionMacroRatioConstraint * db->dbTiers[layer].coreRegion.getArea())
                {
                    // 如果是宏块且超出宏块面积限制，则跳过该层
                    printf("Layer %d macro area limit exceeded, cannot place macro %s. macro area used: %f, macro area: %f\n", layer, module->name.c_str(), layerMacroAreaUsed[layer], moduleArea);
                    continue;
                }
                
                partitionedPosition[module] = ModulePosition(layer, pos3D.x, pos3D.y);
                layerAreaUsed[layer] += moduleArea; // 更新该层的已使用面积
                placed = true;
                break;
            }
        }
        if (!placed)
        {
            printf("Module %s could not be placed in any layer\n", module->name.c_str());
        }
    }
    for ( int i = 0; i < numLayers; ++i )
    {
        printf("Layer %d max area: %f, used area: %f\n", i, maxAreaPerLayer[i], layerAreaUsed[i]);
    }
}

void SpacePlacer::preCalculateTSV(
    std::unordered_map<Module *, ModulePosition> &partitionedPosition)
{
    // const int numLayers = db->dbTiers.size();
    // std::vector<Net *> newNets;
    // newNets.reserve(db->dbNets.size() * 2);
    // int tsvCount = 0;
    // TSVCountPerTier.resize(numLayers, 0);  
    // printf("\n");
    // for (Net *origNet : db->dbNets)
    // {
    //     std::vector<std::vector<Pin *>> layerPins(numLayers);
    //     for (Pin *pin : origNet->netPins)
    //     {
    //         Module *m = pin->module;
    //         auto it = partitionedPosition.find(m);
    //         if (m && it != partitionedPosition.end())
    //         {
    //             int tier = it->second.tierId;
    //             layerPins[tier].push_back(pin);
    //         }
    //     }
    //     // 找到最底和最高被用到的层
    //     int first = -1, last = -1;
    //     for (int i = 0; i < numLayers; ++i)
    //     {
    //         if (!layerPins[i].empty())
    //         {
    //             if (first < 0)
    //                 first = i;
    //             last = i;
    //         }
    //     }

    //     if (first == last)
    //     {
    //         newNets.push_back(new Net(*origNet));
    //         continue;
    //     }

    //     // Module *previousTSV = nullptr;
    //     // 跨层 在每一对 (k,k+1) 之间加 TSV，模块放在 k+1 层
    //     for (int k = first; k < last; ++k)
    //     {
    //         tsvCount++;
    //         TSVCountPerTier[k + 1]++; // 记录每层的 TSV 数量    
    //     }
    // }
    
    const int numLayers = db->dbTiers.size();
    std::vector<int> TSVCountPerTierLocal(numLayers, 0);
    int tsvCount = 0;

    #pragma omp parallel
    {
        std::vector<int> localTSVCountPerTier(numLayers, 0);
        int localTSVCount = 0;

        #pragma omp for
        for (size_t netIdx = 0; netIdx < db->dbNets.size(); ++netIdx)
        {
            Net *origNet = db->dbNets[netIdx];
            std::vector<std::vector<Pin *>> layerPins(numLayers);
            for (Pin *pin : origNet->netPins)
            {
                Module *m = pin->module;
                auto it = partitionedPosition.find(m);
                if (m && it != partitionedPosition.end())
                {
                    int tier = it->second.tierId;
                    layerPins[tier].push_back(pin);
                }
            }
            int first = -1, last = -1;
            for (int i = 0; i < numLayers; ++i)
            {
                if (!layerPins[i].empty())
                {
                    if (first < 0)
                        first = i;
                    last = i;
                }
            }
            if (first == last)
                continue;
            for (int k = first; k < last; ++k)
            {
                localTSVCount++;
                localTSVCountPerTier[k + 1]++;
            }
        }

        // 归约到全局
        #pragma omp critical
        {
            tsvCount += localTSVCount;
            for (int i = 0; i < numLayers; ++i)
                TSVCountPerTierLocal[i] += localTSVCountPerTier[i];
        }
    }

    TSVCountPerTier = TSVCountPerTierLocal;


    //===
    printf("pre TSV count per tier: ");
    for (int i = 0; i < numLayers; ++i)
    {
        printf("%d, ", TSVCountPerTier[i]);
    }
    printf("\n");
    printf("pre TSV count: %d\n", tsvCount);
    double TSVArea = pow(db->TSVsize, 2); // 假设 TSV 占用一个正方形区域
    printf("TSV area: %f\n", TSVArea);
    printf("Total TSV area: %f\n\n", (double)tsvCount * TSVArea);

}


void SpacePlacer::UpdatePartitionDatabase(
    std::unordered_map<Module *, ModulePosition> &partitionedPosition)
{
    const int numLayers = db->dbTiers.size();
    std::vector<Net *> newNets;
    newNets.reserve(db->dbNets.size() * 2);
    int tsvCount = 0;
    TSVCountPerTier.assign(numLayers, 0);  
    printf("\n");
    for (Net *origNet : db->dbNets)
    {
        std::vector<std::vector<Pin *>> layerPins(numLayers);
        for (Pin *pin : origNet->netPins)
        {
            Module *m = pin->module;
            auto it = partitionedPosition.find(m);
            if (m && it != partitionedPosition.end())
            {
                int tier = it->second.tierId;
                layerPins[tier].push_back(pin);
            }
        }
        // 找到最底和最高被用到的层
        int first = -1, last = -1;
        for (int i = 0; i < numLayers; ++i)
        {
            if (!layerPins[i].empty())
            {
                if (first < 0)
                    first = i;
                last = i;
            }
        }

        if (first == last)
        {
            newNets.push_back(new Net(*origNet));
            continue;
        }

        Module *previousTSV = nullptr;
        // 跨层 在每一对 (k,k+1) 之间加 TSV，模块放在 k+1 层
        for (int k = first; k < last; ++k)
        {
            std::string base = origNet->name + "_TSV_" + std::to_string(k) + "_" + std::to_string(k + 1);
            Module *tsv = new Module( //! new tsv
                base,
                db->TSVsize,
                db->TSVsize,
                ModuleMoveType::TSV,
                false, false,  true, false);
            tsvCount++;
            TSVCountPerTier[k + 1]++; // 记录每层的 TSV 数量    
            partitionedPosition[tsv] = ModulePosition(k+1, 0.0f, 0.0f);
            db->dbModules.push_back(tsv);

            if (!layerPins[k].empty())
            {
                Net *down = new Net(base + "_DN", layerPins[k]);
                down->addPin(new Pin(tsv, POS_2D{0, 0}, PIN_DIRECTION_OUT));
                newNets.push_back(down);
            }

            if (!layerPins[k + 1].empty())
            {
                Net *up = new Net(base + "_UP", vector<Pin *>());
                up->addPin(new Pin(tsv, POS_2D{0, 0}, PIN_DIRECTION_IN));
                for (Pin *p : layerPins[k + 1])
                    up->addPin(p);
                newNets.push_back(up);
            }

            if (previousTSV)
            {
                std::string linkName = base + "_LINK_" + std::to_string(k - 1) + "_" + std::to_string(k);
                Net *link = new Net(base + "_LINK", vector<Pin *>());
                link->addPin(new Pin(previousTSV, POS_2D{0, 0}, PIN_DIRECTION_OUT));
                link->addPin(new Pin(tsv, POS_2D{0, 0}, PIN_DIRECTION_IN));
                newNets.push_back(link);
            }

            previousTSV = tsv;
        }
    }
    printf("TSV count: %d\n", tsvCount);
    printf("TSV count per tier: ");
    for (int i = 0; i < numLayers; ++i)
    {
        printf("%d, ", TSVCountPerTier[i]);
    }
    printf("\n");
    db->dbNets.clear();
    db->dbNets.insert(db->dbNets.end(), newNets.begin(), newNets.end());
}

// void SpacePlacer::UpdatePartitionDatabase(
//     std::unordered_map<Module *, ModulePosition> &partitionedPosition)
// {
//     const int numLayers = db->dbTiers.size();

//     // 全局收集
//     std::vector<Net *> newNets;
//     std::vector<Module *> newTSVs;
//     std::vector<int> TSVCountPerTierLocal(numLayers, 0);
//     int tsvCount = 0;

//     // 用于合并本地 partitionedPosition
//     std::vector<std::vector<std::pair<Module*, ModulePosition>>> localPartitionedPositions(omp_get_max_threads());

//     #pragma omp parallel
//     {
//         std::vector<Net *> threadNewNets;
//         std::vector<Module *> threadNewTSVs;
//         std::vector<int> threadTSVCountPerTier(numLayers, 0);
//         int threadTSVCount = 0;
//         int tid = omp_get_thread_num();

//         #pragma omp for nowait
//         for (size_t netIdx = 0; netIdx < db->dbNets.size(); ++netIdx)
//         {
//             Net *origNet = db->dbNets[netIdx];
//             std::vector<std::vector<Pin *>> layerPins(numLayers);
//             for (Pin *pin : origNet->netPins)
//             {
//                 Module *m = pin->module;
//                 auto it = partitionedPosition.find(m);
//                 if (m && it != partitionedPosition.end())
//                 {
//                     int tier = it->second.tierId;
//                     layerPins[tier].push_back(pin);
//                 }
//             }
//             int first = -1, last = -1;
//             for (int i = 0; i < numLayers; ++i)
//             {
//                 if (!layerPins[i].empty())
//                 {
//                     if (first < 0)
//                         first = i;
//                     last = i;
//                 }
//             }

//             if (first == last)
//             {
//                 threadNewNets.push_back(new Net(*origNet));
//                 continue;
//             }

//             Module *previousTSV = nullptr;
//             for (int k = first; k < last; ++k)
//             {
//                 std::string base = origNet->name + "_TSV_" + std::to_string(k) + "_" + std::to_string(k + 1);
//                 Module *tsv = new Module(
//                     base,
//                     db->TSVsize,
//                     db->TSVsize,
//                     ModuleMoveType::TSV,
//                     false, false, true, false);
//                 threadTSVCount++;
//                 threadTSVCountPerTier[k + 1]++;
//                 localPartitionedPositions[tid].emplace_back(tsv, ModulePosition(k+1, 0.0f, 0.0f));
//                 threadNewTSVs.push_back(tsv);

//                 if (!layerPins[k].empty())
//                 {
//                     Net *down = new Net(base + "_DN", layerPins[k]);
//                     down->addPin(new Pin(tsv, POS_2D{0, 0}, PIN_DIRECTION_OUT));
//                     threadNewNets.push_back(down);
//                 }

//                 if (!layerPins[k + 1].empty())
//                 {
//                     Net *up = new Net(base + "_UP", vector<Pin *>());
//                     up->addPin(new Pin(tsv, POS_2D{0, 0}, PIN_DIRECTION_IN));
//                     for (Pin *p : layerPins[k + 1])
//                         up->addPin(p);
//                     threadNewNets.push_back(up);
//                 }

//                 if (previousTSV)
//                 {
//                     Net *link = new Net(base + "_LINK", vector<Pin *>());
//                     link->addPin(new Pin(previousTSV, POS_2D{0, 0}, PIN_DIRECTION_OUT));
//                     link->addPin(new Pin(tsv, POS_2D{0, 0}, PIN_DIRECTION_IN));
//                     threadNewNets.push_back(link);
//                 }

//                 previousTSV = tsv;
//             }
//         }

//         // 合并到全局
//         #pragma omp critical
//         {
//             newNets.insert(newNets.end(), threadNewNets.begin(), threadNewNets.end());
//             newTSVs.insert(newTSVs.end(), threadNewTSVs.begin(), threadNewTSVs.end());
//             tsvCount += threadTSVCount;
//             for (int i = 0; i < numLayers; ++i)
//                 TSVCountPerTierLocal[i] += threadTSVCountPerTier[i];
//         }
//     }

//     // 合并本地 partitionedPosition
//     for (const auto& localPairs : localPartitionedPositions)
//         for (const auto& p : localPairs)
//             partitionedPosition[p.first] = p.second;

//     TSVCountPerTier = TSVCountPerTierLocal;
//     db->dbModules.insert(db->dbModules.end(), newTSVs.begin(), newTSVs.end());
//     db->dbNets.clear();
//     db->dbNets.insert(db->dbNets.end(), newNets.begin(), newNets.end());

//     printf("TSV count: %d\n", tsvCount);
//     printf("TSV count per tier: ");
//     for (int i = 0; i < numLayers; ++i)
//     {
//         printf("%d, ", TSVCountPerTier[i]);
//     }
//     printf("\n");
// }


// void SpacePlacer::SetTSVPosition(std::unordered_map<Module *, ModulePosition> &partitionedPosition)
// {
//     // 遍历所有模块，找到所有 TSV 模块
//     for (auto &entry : partitionedPosition)
//     {
//         Module *module = entry.first;
//         ModulePosition position = entry.second;

//         // 只处理 TSV 模块
//         if (module->moveType == ModuleMoveType::TSV)
//         {
//             // 获取该 TSV 所在的层
//             int layer = position.tierId;

//             // 计算重心位置
//             double sumX = 0.0, sumY = 0.0;
//             double totalArea = 0.0; // 计算总面积（加权）
//             int connectedModulesCount = 0;

//             // 遍历所有与该 TSV 连接的 Net
//             for (Net *net : db->dbNets)
//             {
//                 // 查找该 Net 中所有连接到 TSV 的 Pin
//                 for (Pin *pin : net->netPins)
//                 {
//                     if (pin->module == module)
//                     { // 找到 TSV 连接的 Pin
//                         // 遍历所有连接到该 TSV 的其他模块
//                         for (Pin *connectedPin : net->netPins)
//                         {
//                             if (connectedPin->module != module) // 忽略 TSV 自己
//                             {
//                                 Module *connectedModule = connectedPin->module;

//                                 // 获取连接模块的坐标和面积
//                                 ModulePosition connectedPosition = partitionedPosition[connectedModule];
//                                 double moduleArea = connectedModule->getArea();

//                                 // 累加连接模块的坐标，按面积加权
//                                 sumX += connectedPosition.position.x * moduleArea;
//                                 sumY += connectedPosition.position.y * moduleArea;
//                                 totalArea += moduleArea;
//                                 connectedModulesCount++;
//                             }
//                         }
//                     }
//                 }
//             }

//             // 计算重心位置，如果有连接模块
//             if (connectedModulesCount > 0 && totalArea > 0)
//             {
//                 double avgX = sumX / totalArea;
//                 double avgY = sumY / totalArea;

//                 // 更新 TSV 的位置，设置为计算出的重心位置
//                 partitionedPosition[module] = ModulePosition(layer, avgX, avgY);
//             }
//         }
//     }
// }

void SpacePlacer::SetTSVPosition(std::unordered_map<Module *, ModulePosition> &partitionedPosition)
{
    std::vector<std::pair<Module*, ModulePosition>> tsvUpdates;

    #pragma omp parallel
    {
        std::vector<std::pair<Module*, ModulePosition>> localUpdates;

        #pragma omp for nowait
        for (size_t idx = 0; idx < partitionedPosition.size(); ++idx)
        {
            auto it = std::next(partitionedPosition.begin(), idx);
            Module *module = it->first;
            ModulePosition position = it->second;

            if (module->moveType == ModuleMoveType::TSV)
            {
                int layer = position.tierId;
                double sumX = 0.0, sumY = 0.0, totalArea = 0.0;
                int connectedModulesCount = 0;

                for (Net *net : db->dbNets)
                {
                    for (Pin *pin : net->netPins)
                    {
                        if (pin->module == module)
                        {
                            for (Pin *connectedPin : net->netPins)
                            {
                                if (connectedPin->module != module)
                                {
                                    Module *connectedModule = connectedPin->module;
                                    ModulePosition connectedPosition = partitionedPosition[connectedModule];
                                    double moduleArea = connectedModule->getArea();
                                    sumX += connectedPosition.position.x * moduleArea;
                                    sumY += connectedPosition.position.y * moduleArea;
                                    totalArea += moduleArea;
                                    connectedModulesCount++;
                                }
                            }
                        }
                    }
                }
                if (connectedModulesCount > 0 && totalArea > 0)
                {
                    double avgX = sumX / totalArea;
                    double avgY = sumY / totalArea;
                    localUpdates.emplace_back(module, ModulePosition(layer, avgX, avgY));
                }
            }
        }

        #pragma omp critical
        tsvUpdates.insert(tsvUpdates.end(), localUpdates.begin(), localUpdates.end());
    }

    for (auto &p : tsvUpdates)
        partitionedPosition[p.first] = p.second;
}

void SpacePlacer::writePlaceInfo()
{
    //for hypergraph partition project
    string folder = "./";
    if(gArg.CheckExist("outputDir"))
    {
        gArg.GetString("outputDir",&folder);
    }
    std::string filefolder = folder + "forPart";
    if (!std::filesystem::exists(filefolder))
    {
        std::filesystem::create_directories(filefolder);
    }
    std::string inputfilename = filefolder + "/placeinfo";

    // printf("write placeinfo to %s\n", inputfilename.c_str());
    // 如果文件已存在则直接返回
    if (false)  //std::filesystem::exists(inputfilename)
    {
        printf("File %s already exists, skip writing.\n", inputfilename.c_str());
    }
    else{
        FILE *fp = fopen(inputfilename.c_str(), "w");
        if (!fp) {
            printf("Cannot open file %s for writing!\n", inputfilename.c_str());
            return;
        }

        // 统计单元、网、pin数量
        int number_of_cells = db->dbModules.size();
        int number_of_nets = db->dbNets.size();
        int number_of_pins = 0;
        for (auto net : db->dbNets)
            number_of_pins += net->netPins.size();

        // 头部
        fprintf(fp, "%d\n%d\n%d\n", number_of_cells, number_of_nets, number_of_pins);

        // 网定义部分
        for (auto net : db->dbNets)
        {
            int net_weight = 1;
            int number_of_cells_in_net = net->netPins.size();

            // 统计该 net 连接到的 module 中有多少个属于 dbFFs
            int ff_count = 0;
            int macro_count = 0;
            for (auto pin : net->netPins) {
                if (pin->module->isFF) {  //std::find(db->dbFFs.begin(), db->dbFFs.end(), pin->module) != db->dbFFs.end()
                    ff_count++;
                }
                if (pin->module->isMacro){
                    macro_count++;
                }
            }
            // net_weight += ff_count;
            // net_weight += (   max(macro_count-1 , 1)    );



            // bool hasFF = false;
            // for (auto pin : net->netPins) {
            //     if (std::find(db->dbFFs.begin(), db->dbFFs.end(), pin->module) != db->dbFFs.end()) {
            //         hasFF = true;
            //         break;
            //     }
            // }
            // if (hasFF) net_weight = (number_of_cells_in_net + 2) / 3;

            // net_weight = (number_of_cells_in_net + 2) / 3; // 等于 number_of_cells_in_net 除以 3 后向上取整
            fprintf(fp, "%d %d", 1, number_of_cells_in_net);
            for (auto pin : net->netPins)
            {
                // 单元ID为db->dbModules中的索引
                auto it = std::find(db->dbModules.begin(), db->dbModules.end(), pin->module);
                int cell_id = (it != db->dbModules.end()) ? std::distance(db->dbModules.begin(), it) : -1;
                fprintf(fp, " %d", cell_id);
            }
            fprintf(fp, "\n");
        }

        // 单元权重部分
        for (auto module : db->dbModules)
        {
            fprintf(fp, "%.0f\n", module->getArea());
        }

        fclose(fp);
        printf("Placeinfo written to %s\n", inputfilename.c_str());
    }
    
}


void SpacePlacer::writePartitionedPosition(
    std::unordered_map<Module *, ModulePosition> &partitionedPosition)
{
    string folder = "./";
    if(gArg.CheckExist("outputDir"))
    {
        gArg.GetString("outputDir",&folder);
    }
    std::string filefolder = folder + "forPart";
    if (!std::filesystem::exists(filefolder))
    {
        std::filesystem::create_directories(filefolder);
    }

    std::string infofilename = filefolder + "/" + "placeinfo.part";

    // // 如果文件已存在则清空
    // if (std::filesystem::exists(infofilename)) {
    //     printf("File %s already exists");
    // }

    FILE *fp = fopen(infofilename.c_str(), "w");
    if (!fp) {
        printf("Cannot open file %s for writing!\n", infofilename.c_str());
        return;
    }

    int numLayers = db->dbTiers.size();
    double offratio = 0.01;// 分区大小偏移比例
    if (gArg.CheckExist("partoffratio"))
    {
        gArg.GetDouble("partoffratio", &offratio);
        printf("Using user-defined partition size offset ratio: %f\n", offratio);
    }
    double totalArea = 0.0;
    // 按分区统计单元编号
    std::vector<std::vector<int>> cellsInPartition(numLayers);
    for (size_t i = 0; i < db->dbModules.size(); ++i)
    {
        Module *m = db->dbModules[i];
        auto it = partitionedPosition.find(m);
        if (it != partitionedPosition.end())
        {
            int tierId = it->second.tierId;
            cellsInPartition[tierId].push_back(i);
        }
        totalArea += m->getArea();
    }

    for (int partId = 0; partId < numLayers; ++partId)
    {
        int maxCellNum = cellsInPartition[partId].size();
        double curPartSize = 0.0;
        for (int idx : cellsInPartition[partId])
            curPartSize += db->dbModules[idx]->getArea();
        
        double minratio = (1.0 / numLayers) - offratio;
        double maxratio = (1.0 / numLayers) + offratio;
        // printf("minratio: %f\n", minratio);
        double minPartSize = minratio * totalArea ; // 可根据实际需求调整
        double maxPartSize = maxratio * totalArea ; // 可根据实际需求调整
        // printf("maxratio: %f/n", (1.0 / numLayers) + offratio);

        // 第一行：分区号 最大单元数 最小分区大小 当前分区大小 最大分区大小
        fprintf(fp, "%d %d %.0f %.0f %.0f\n", partId, maxCellNum, minPartSize, curPartSize, maxPartSize);

        // 第二行：单元列表
        for (int idx : cellsInPartition[partId])
            fprintf(fp, "%d ", idx);
        fprintf(fp, "\n");
    }

    fclose(fp);
    printf("Partition results written to %s\n", infofilename.c_str());
}


void SpacePlacer::readPartitionedPosition(
    std::unordered_map<Module *, ModulePosition> &partitionedPosition)
{
    string folder = "./";
    if(gArg.CheckExist("outputDir"))
    {
        gArg.GetString("outputDir",&folder);
    }
    std::string filefolder = folder + "forPart";
    std::string infofilename = filefolder + "/" + "placeinfo.part." + std::to_string(db->dbTiers.size()) ;

    FILE *fp = fopen(infofilename.c_str(), "r");
    if (!fp) {
        printf("Cannot open file %s for reading!\n", infofilename.c_str());
        return;
    }

    int numLayers = db->dbTiers.size();
    for (int partId = 0; partId < numLayers; ++partId)
    {
        int maxCellNum;
        double minPartSize, curPartSize, maxPartSize;

        // 读取第一行
        if (fscanf(fp, "%*d %d %lf %lf %lf", &maxCellNum, &minPartSize, &curPartSize, &maxPartSize) != 4) {
            printf("Error reading partition header for partition %d\n", partId);
            break;
        }
        printf("maxcellNum: %d, minPartSize: %.0f, curPartSize: %.0f, maxPartSize: %.0f\n",
               maxCellNum, minPartSize, curPartSize, maxPartSize);
        
        // 读取第二行（单元列表）
        std::vector<int> cellIdxList;
        cellIdxList.reserve(maxCellNum);

        int readCount = 0;
        while (readCount < maxCellNum) {
            int cellIdx;
            int ret = fscanf(fp, "%d", &cellIdx);
            if (ret == 1) {
                cellIdxList.push_back(cellIdx);
                readCount++;
            } else {
                // 读到行尾或出错
                break;
            }
        }
        // 跳过行尾剩余内容
        fscanf(fp, "%*[^\n]");
        fgetc(fp); // 跳过换行符

        // 处理单元列表
        for (int cellIdx : cellIdxList) {
            if (cellIdx >= 0 && cellIdx < db->dbModules.size()) {
                Module *m = db->dbModules[cellIdx];
                POS_3D pos3D = module3DPosition[m];
                partitionedPosition[m] = ModulePosition(partId, pos3D.x, pos3D.y);
            } else {
                printf("Invalid cell index %d in partition %d\n", cellIdx, partId);
            }
        }
    }

    fclose(fp);
    printf("Partitioned position read from %s\n", infofilename.c_str());
}


void SpacePlacer::partitionTool(int numLayers, int seed)
{
    string folder = "./";
    if(gArg.CheckExist("outputDir"))
    {
        gArg.GetString("outputDir",&folder);
    }
    std::string filefolder = folder + "forPart";
    if (!std::filesystem::exists(filefolder))
    {
        std::filesystem::create_directories(filefolder);
    }

    std::string infofilename = filefolder + "/" + "placeinfo";


    string partitionToolPath;
    partitionToolPath = "/home/TritonPart/TD_3D/src/gpl3d/third_party/hypergraphpartition/src/";

    // string partMethod = "fms"; // "pfm" , "plm" , "sa"
    // string partExe = "ad_" + partMethod + ".x";
    // string cmd = partitionToolPath + partMethod + "/" + partExe + " "
    //              + infofilename + " "
    //              + std::to_string(numLayers) + " "
    //              + std::to_string(1) ; 


    string partMethod = "plm"; // "pfm" , "plm" , "sa"
    string partExe = "ad_" + partMethod + ".x";
    string cmd = partitionToolPath + partMethod + "/" + partExe + " "
                 + infofilename + " "
                 + std::to_string(numLayers) + " 1 3 "
                 + std::to_string(seed) ; 




    cout << RED << "Running partitioner: " << cmd << RESET_COLOR << endl;
    system(cmd.c_str());

}


unordered_map<Module *, ModulePosition> SpacePlacer::getPartitionedPosition()
{
    // TSVsize = 0.5 * db->commonRowHeight; 
    // 创建 moduleVector
    std::unordered_map<Module *, ModulePosition> partitionedPosition;
    writePlaceInfo();

    //simple分层
    // 进行初步分层
    // InitialPartition(partitionedPosition, true);

    // // 预计算 TSV
    // preCalculateTSV(partitionedPosition);
    // 根据tsv数量重新划分层
    InitialPartition(partitionedPosition, false);
    //simple分层 end

    printf("=====start partition=====\n");
    
    //输出当前预初始分区结果
    writePartitionedPosition(partitionedPosition);

    // 调用 分区工具
    int seed = 123456;
    if (gArg.CheckExist("partseed"))
    {
        gArg.GetInt("partseed", &seed);
        printf("Using user-defined partition seed: %d\n", seed);
    }
    partitionTool(db->dbTiers.size(), seed);

    // 读取分区结果
    readPartitionedPosition(partitionedPosition);



    // 进行网络划分，并创建 TSV
    double partTime;
    time_start(&partTime);
    UpdatePartitionDatabase(partitionedPosition);
    time_end(&partTime);
    printf("Partition Time: %f seconds\n", partTime);

    // 计算 TSV 位置
    SetTSVPosition(partitionedPosition);

    // 输出 net 数量
    printf("Net count after partition: %zu\n", db->dbNets.size());

    printf("=====end partition=====\n");

    return partitionedPosition;
}
