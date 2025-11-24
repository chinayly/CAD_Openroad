#include <cstdio>
#include <ranges>

#include "placer.h"
#include "fft.h"
#include "plot.h"
// 添加CTS相关头文件
#include "ctsdb.h"
#include "mmm.h"
#include "topology.h"
#include "DME.h"
#include "td/TimingManager.h"

TierPlacer::TierPlacer(PlaceDB *db, unordered_map<Module *, ModulePosition> &&position, double targetDensity)
    : targetDensity(targetDensity), modulePosition(position), db(db)
{
    numTiers = db->dbTiers.size();
    printf("\n\n\n=== Start TierPlacer Initialization ===\n");
    setCoreRegion();
    fillerInitialization();
    nodeInitialization();
    meshInitialization();
    gradientInitialization();
    penaltyFactorInitialization();
    // 如果指定了优化时钟树
    if (gArg.CheckExist("clockaware")) {
        clockOptimizationEnabled = true;
    }
    printf("=== Finish TierPlacer Initialization ===");
}

void TierPlacer::plotCurrentPlacement(string name)
{
    const unsigned int imgWidth = 960;
    const unsigned int imgHeight = (unsigned int)(imgWidth * (coreRegion.getHeight() / coreRegion.getWidth()));
    string folder = "./";
    if(gArg.CheckExist("outputDir"))
    {
        gArg.GetString("outputDir",&folder);
    }
    // Workaround since ranges::concat_view is only supported in C++26
    vector<tuple<size_t, double, double, double, double, const unsigned char *>> rectangles;
    rectangles.reserve(freeNodes.size());
    for (auto m : db->dbModules)
    {
        const ModulePosition &pos = modulePosition[m];
        const unsigned char* color;
        if (m->isMacro) {
            color = Plot::Blue;  // Blue for macros
        } else if (m->isTSV) {
            color = Plot::Orange;  // Orange for TSVs
        } 
        else if (m->isFF) {
            color = Plot::Purple;  // Purple for flip-flops
        } 
        // else if (m->isTerminal) {
        //     color = Plot::Red;  // Red for fixed modules
        // }
        else {
            color = Plot::LightGreen;  // Light green for standard cells
        }
        if (m->width <= 0.0)
        {continue;}
        rectangles.push_back(
            make_tuple(
                pos.tierId,
                pos.position.x - m->width / 2,
                pos.position.y - m->height / 2,
                pos.position.x + m->width / 2,
                pos.position.y + m->height / 2,
                color
            ));
    }
    if (gArg.CheckExist("plotFiller"))
    {
        for (auto &f : fillers)
        {
            const ModulePosition &pos = modulePosition[&f];
            rectangles.push_back(
                make_tuple(
                    pos.tierId,
                    pos.position.x - f.width / 2,
                    pos.position.y - f.height / 2,
                    pos.position.x + f.width / 2,
                    pos.position.y + f.height / 2,
                    Plot::Green));
        }
    }
    Plot::plotFigure(
        folder,
        name,
        rectangles | ranges::views::all,
        make_tuple(coreRegion.ll.x, coreRegion.ll.y, coreRegion.ur.x, coreRegion.ur.y),
        numTiers,
        make_pair(imgWidth, imgHeight));
}

vector<double *> TierPlacer::getParams()
{
    vector<double *> params;
    params.reserve(freeNodes.size() * 2);
    for (auto m : freeNodes)
    {
        params.push_back(&modulePosition[m].position.x);
        params.push_back(&modulePosition[m].position.y);
    }
    return params;
}


void TierPlacer::setTiming(gpl3d::td::TimingManager* tm, int k_timing)
{
    timing_   = tm;
    k_timing_ = (k_timing > 0 ? k_timing : 15);
}
// calculate the total gradient with preconditioning
vector<double> TierPlacer::getGradient()
{
    updateDensityGradient();
    updateWirelengthGradient();
    // 如果启用了时钟优化，则更新FF梯度
    if (clockOptimizationEnabled) {
        double clkGradTime= 0.0;
        time_start(&clkGradTime);
        updateClockGradient();
        time_end(&clkGradTime);
        clkawareTime += clkGradTime;
    }
    // 计算时钟权重eta，参考eplace.cpp的计算方式
    double eta = 6.0 * fastExp(10.0 * (0.1 - globalDensityOverflow));
    vector<double> gradient;
    gradient.reserve(freeNodes.size() * 2);
    for (auto m : freeNodes)
    {
        // if (stopConditionPerTier[modulePosition[m].tierId])
        // {
        //     gradient.push_back(0);
        //     gradient.push_back(0);
        //     continue;
        // }
        //preconditioning
        double connectedNetNum = m->modulePins.size();
        double charge = m->getArea();
        double preconditioner = 1 / std::max(1.0, (connectedNetNum + lambda * charge));
        double gradx = preconditioner * (wirelengthGradient[m].x + lambda * densityGradient[m].x);
        double grady = preconditioner * (wirelengthGradient[m].y + lambda * densityGradient[m].y);
        
        //for clock optimization, add clock gradient
        double clktime = 0.0;
        time_start(&clktime);
        if (clockOptimizationEnabled && m->isFF && globalDensityOverflow < 0.8)
        {
            preconditioner = 1 / max(1.0, (connectedNetNum + lambda * charge + eta * m->clockGradientPreconditioner));
            gradx = preconditioner * (wirelengthGradient[m].x + lambda * densityGradient[m].x + eta * clockGradient[m].x);
            grady = preconditioner * (wirelengthGradient[m].y + lambda * densityGradient[m].y + eta * clockGradient[m].y);
        }
        time_end(&clktime);
        clkawareTime += clktime;
        gradient.push_back(gradx);
        gradient.push_back(grady);
    }
    return gradient;
}



void zeroGradient(unordered_map<Module *, VECTOR_2D> &g)
{
    for (auto &[k, v] : g)
    {
        v.SetZero();
    }
}

void TierPlacer::updatePenaltyFactor(double curHpwl, double lastHpwl)
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

// template <std::ranges::range R>
//     requires std::same_as<std::ranges::range_value_t<R>, POS_2D>
// std::pair<POS_2D, POS_2D> getBoundingBox(const R &pos)
// {
//     POS_2D XYmin(DOUBLE_MAX, DOUBLE_MAX);
//     POS_2D XYmax(-DOUBLE_MAX, -DOUBLE_MAX);
//     for (auto p : pos)
//     {
//         if (p.x > XYmax.x)
//         {
//             XYmax.x = p.x;
//         }
//         if (p.x < XYmin.x)
//         {
//             XYmin.x = p.x;
//         }
//         if (p.y > XYmax.y)
//         {
//             XYmax.y = p.y;
//         }
//         if (p.y < XYmin.y)
//         {
//             XYmin.y = p.y;
//         }
//     }
//     return std::make_pair(XYmin, XYmax);
// }

double TierPlacer::totalHPWL()
{
    double hpwl = 0;
    for (Net *net : db->dbNets)
    {
        const double w = timing_ ? timing_->weightOf(net) : 1.0;  // ★ 权重乘子
        hpwl += w * getNetHPWL(*net);
    }
    return hpwl;
}

double TierPlacer::getNetHPWL(const Net &n)
{
    if (n.netPins.empty())
    {
        return 0.0f;
    }
    auto [bbmax, bbmin] = getBoundingBox(
        n.netPins | std::ranges::views::transform([&](Pin *p)
                                                  {
        const POS_2D& modPos = modulePosition[p->module].position;
        POS_2D pinPos;
        pinPos = p->offset + modPos;
        return pinPos; }));
    return abs(bbmax.x - bbmin.x) + abs(bbmax.y - bbmin.y);
}

void TierPlacer::updateWirelengthGradient()
{
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
        invertedGamma = VECTOR_2D{1.0 / 8, 1.0 / 8};
        invertedGamma.x /= meshPerTier[0].getBinWidth();
        invertedGamma.y /= meshPerTier[0].getBinHeight();
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

        // ★ 这里定义 cache，并在本作用域内使用
        std::vector<WAWirelengthGradientCache> cache(db->maxNetDegree);

        for (auto net : db->dbNets)
        {
            const double w = timing_ ? timing_->weightOf(net) : 1.0; // ★ 时序权重
            addWAWirelengthGradientOneNet(*net, cache, w);           // ★ 改为 3 参版本
        }
    }
}


void TierPlacer::addWAWirelengthGradientOneNet(const Net &n,
    vector<WAWirelengthGradientCache> &cache,
    double net_weight)    
{
    cache.resize(n.netPins.size());
    // calculate pin position
    for (size_t i = 0; i < n.netPins.size(); i++)
    {
        const ModulePosition &modPos = modulePosition[n.netPins[i]->module];
        cache[i].pinPos = n.netPins[i]->offset + modPos.position;
    }
    auto [bbmin, bbmax] = getBoundingBox(cache | std::ranges::views::transform([](WAWirelengthGradientCache &c)
                                                                               { return c.pinPos; }));

    // calculate necessary values to assemble the gradient
    VECTOR_2D numeratorPositive, numeratorNegative;
    VECTOR_2D denominatorPositive, denominatorNegative;
    numeratorPositive.SetZero();
    numeratorNegative.SetZero();
    denominatorPositive.SetZero();
    denominatorNegative.SetZero();
    for (size_t i = 0; i < n.netPins.size(); i++)
    {
        const POS_2D &curPinPos = cache[i].pinPos;
        VECTOR_2D &curExpPositive = cache[i].expPositive;
        VECTOR_2D &curExpNegative = cache[i].expNegative;
        VECTOR_2D expMax; // (Xi-Xmax)/gamma in WA model (X/Y/Z)
        VECTOR_2D expMin; // (Xmin-Xi)/gamma in WA model (X/Y/Z)
        expMax.x = (curPinPos.x - bbmax.x) * invertedGamma.x;
        expMin.x = (bbmin.x - curPinPos.x) * invertedGamma.x;
        expMax.y = (curPinPos.y - bbmax.y) * invertedGamma.y;
        expMin.y = (bbmin.y - curPinPos.y) * invertedGamma.y;

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
    }

    // assemble the gradient
    for (size_t i = 0; i < n.netPins.size(); i++)
    {
        VECTOR_2D &curWirelengthGradient = wirelengthGradient[n.netPins[i]->module];
        const VECTOR_2D curPinVec(cache[i].pinPos.x, cache[i].pinPos.y);
        const VECTOR_2D &curExpPositive = cache[i].expPositive;
        const VECTOR_2D &curExpNegative = cache[i].expNegative;
        VECTOR_2D &&curExpPositiveDivideGamma = curExpPositive * invertedGamma;
        VECTOR_2D &&curExpNegativeDivideGamma = curExpNegative * invertedGamma;

        VECTOR_2D &&curPinGradientPositiveTerm =
            ((curExpPositive + curExpPositiveDivideGamma * curPinVec) * denominatorPositive - curExpPositiveDivideGamma * numeratorPositive) / (denominatorPositive * denominatorPositive);
        VECTOR_2D &&curPinGradientNegativeTerm =
            ((curExpNegative - curExpNegativeDivideGamma * curPinVec) * denominatorNegative + curExpNegativeDivideGamma * numeratorNegative) / (denominatorNegative * denominatorNegative);
            curWirelengthGradient += (curPinGradientPositiveTerm - curPinGradientNegativeTerm) * net_weight;

    }
}

void TierPlacer::addWAClkWirelengthGradientOneNet(const Net &n, vector<WAClkWirelengthGradientCache> &cache)
{
    // printf("check");
    cache.resize(n.netPins.size());
    // calculate pin position
    for (size_t i = 0; i < n.netPins.size(); i++)
    {
        const ModulePosition &modPos = modulePosition[n.netPins[i]->module];
        cache[i].pinPos = n.netPins[i]->offset + modPos.position;
    }
    auto [bbmin, bbmax] = getBoundingBox(cache | std::ranges::views::transform([](WAClkWirelengthGradientCache &c)
                                                                               { return c.pinPos; }));

    // calculate necessary values to assemble the gradient
    VECTOR_2D numeratorPositive, numeratorNegative;
    VECTOR_2D denominatorPositive, denominatorNegative;
    numeratorPositive.SetZero();
    numeratorNegative.SetZero();
    denominatorPositive.SetZero();
    denominatorNegative.SetZero();
    for (size_t i = 0; i < n.netPins.size(); i++)
    {
        const POS_2D &curPinPos = cache[i].pinPos;
        VECTOR_2D &curExpPositive = cache[i].expPositive;
        VECTOR_2D &curExpNegative = cache[i].expNegative;
        VECTOR_2D expMax; // (Xi-Xmax)/gamma in WA model (X/Y/Z)
        VECTOR_2D expMin; // (Xmin-Xi)/gamma in WA model (X/Y/Z)
        expMax.x = (curPinPos.x - bbmax.x) * invertedGamma.x;
        expMin.x = (bbmin.x - curPinPos.x) * invertedGamma.x;
        expMax.y = (curPinPos.y - bbmax.y) * invertedGamma.y;
        expMin.y = (bbmin.y - curPinPos.y) * invertedGamma.y;

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
    }

    // assemble the gradient
    for (size_t i = 0; i < n.netPins.size(); i++)
    {
        VECTOR_2D &curWirelengthGradient = clockGradient[n.netPins[i]->module];
        VECTOR_2D &curclocknetGradient = clocknetGradient[n.netPins[i]->module];
        const VECTOR_2D curPinVec(cache[i].pinPos.x, cache[i].pinPos.y);
        const VECTOR_2D &curExpPositive = cache[i].expPositive;
        const VECTOR_2D &curExpNegative = cache[i].expNegative;
        VECTOR_2D &&curExpPositiveDivideGamma = curExpPositive * invertedGamma;
        VECTOR_2D &&curExpNegativeDivideGamma = curExpNegative * invertedGamma;

        VECTOR_2D &&curPinGradientPositiveTerm =
            ((curExpPositive + curExpPositiveDivideGamma * curPinVec) * denominatorPositive - curExpPositiveDivideGamma * numeratorPositive) / (denominatorPositive * denominatorPositive);
        VECTOR_2D &&curPinGradientNegativeTerm =
            ((curExpNegative - curExpNegativeDivideGamma * curPinVec) * denominatorNegative + curExpNegativeDivideGamma * numeratorNegative) / (denominatorNegative * denominatorNegative);
        
        curclocknetGradient = (curPinGradientPositiveTerm - curPinGradientNegativeTerm);

        
        if (!gArg.CheckExist("MMM") && !gArg.CheckExist("DME"))
        {
            curWirelengthGradient = curclocknetGradient ; // 如果没有使用MMM或DME模型，则直接使用clocknetGradient
            continue;
        }
        curWirelengthGradient = (curWirelengthGradient * 0.5 + curclocknetGradient * 0.5)  * 2;
        
        
        
    }
}


void TierPlacer::addLSEWirelengthGradientOneNet(const Net &n)
{
    printf("LSE model not implemented!\n");
    exit(-1);
}

void TierPlacer::updateDensityOverflow()
{
    double totalOverflowArea = 0;
    densityOverflowPerTier.resize(numTiers);
    for (size_t tierId = 0; tierId < numTiers; tierId++)
    {
        double binArea = meshPerTier[tierId].getBinArea();
        // why can't I capture this->targetDensity???
        double tDensity = targetDensity;
        double tierOverflowArea = meshPerTier[tierId].accumulateModuleDensity(
            [binArea, tDensity](double density, double acc)
            {
                return acc + std::max(0.0, density - binArea * tDensity);
            },
            0);
        totalOverflowArea += tierOverflowArea;
        densityOverflowPerTier[tierId] = tierOverflowArea / movableNodesAreaPerTier[tierId]; //! calculate density overflow per tier to check whether stop further opt steps 
        if (densityOverflowPerTier[tierId] < 0.1)
        {
            stopConditionPerTier[tierId] = true;
        }
    }
    globalDensityOverflow = totalOverflowArea /
                            std::accumulate(movableNodesAreaPerTier.cbegin(), movableNodesAreaPerTier.cend(), 0);
}

void TierPlacer::updateDensityGradient()
{
    zeroGradient(densityGradient);
    #pragma omp parallel for
    for (size_t i = 0; i < numTiers; i++)
    {
        meshPerTier[i].clearDensity();
        double binWidth = meshPerTier[i].getBinWidth();
        double binHeight = meshPerTier[i].getBinHeight();
        // update density
        for (auto curNode : freeNodes) //db->dbModules
        {
            const ModulePosition &curNodePos = modulePosition[curNode];
            if (curNodePos.tierId == i)
            {
                CRect rectForCurNode;
                double scaleX = 1, scaleY = 1;
                const POS_2D &cellCenter = curNodePos.position;
                rectForCurNode.ll = cellCenter - POS_2D{curNode->width / 2, curNode->height / 2};
                rectForCurNode.ur = cellCenter + POS_2D{curNode->width / 2, curNode->height / 2};
                if (curNode->width < binWidth)
                {
                    scaleX = curNode->width / binWidth;
                    rectForCurNode.ll.x = cellCenter.x - 0.5 * binWidth;
                    rectForCurNode.ur.x = cellCenter.x + 0.5 * binWidth;
                }
                if (curNode->height < binHeight)
                {
                    scaleY = curNode->height / binHeight;
                    rectForCurNode.ll.y = cellCenter.y - 0.5 * binHeight;
                    rectForCurNode.ur.y = cellCenter.y + 0.5 * binHeight;
                }
                meshPerTier[i].addModuleDensity(rectForCurNode, scaleX * scaleY);
            }
        }
        // for (auto curFiller : fillers)
        // {
        //     const ModulePosition &curFillerPos = modulePosition[&curFiller];
        //     if (curFillerPos.tierId == i)
        //     {
        //         CRect rectForCurFiller;
        //         double scaleX = 1, scaleY = 1;
        //         const POS_2D &cellCenter = curFillerPos.position;
        //         rectForCurFiller.ll = cellCenter - POS_2D{curFiller.width / 2, curFiller.height / 2};
        //         rectForCurFiller.ur = cellCenter + POS_2D{curFiller.width / 2, curFiller.height / 2};
        //         if (curFiller.width < binWidth)
        //         {
        //             scaleX = curFiller.width / binWidth;
        //             rectForCurFiller.ll.x = cellCenter.x - 0.5 * binWidth;
        //             rectForCurFiller.ur.x = cellCenter.x + 0.5 * binWidth;
        //         }
        //         if (curFiller.height < binHeight)
        //         {
        //             scaleY = curFiller.height / binHeight;
        //             rectForCurFiller.ll.y = cellCenter.y - 0.5 * binHeight;
        //             rectForCurFiller.ur.y = cellCenter.y + 0.5 * binHeight;
        //         }
        //         meshPerTier[i].addFillerDensity(rectForCurFiller, scaleX * scaleY);
        //     }
        // }
        // calculate e field
        meshPerTier[i].calcElectricField();
        // update density gradient
        #pragma omp parallel for default(shared)
        for (size_t nodeIdx = 0; nodeIdx < freeNodes.size(); nodeIdx++)
        {
            Module *curNode = freeNodes[nodeIdx];
            const ModulePosition &curNodePos = modulePosition[curNode];
            if (curNodePos.tierId == i)
            {
                VECTOR_2D &curNodeDensityGradient = densityGradient[curNode];
                const POS_2D &cellCenter = curNodePos.position;
                CRect rectForCurNode;
                double scaleX = 1, scaleY = 1;
                rectForCurNode.ll = cellCenter - POS_2D{curNode->width / 2, curNode->height / 2};
                rectForCurNode.ur = cellCenter + POS_2D{curNode->width / 2, curNode->height / 2};
                if (curNode->width < binWidth)
                {
                    scaleX = curNode->width / binWidth;
                    rectForCurNode.ll.x = cellCenter.x - 0.5 * binWidth;
                    rectForCurNode.ur.x = cellCenter.x + 0.5 * binWidth;
                }
                if (curNode->height < binHeight)
                {
                    scaleY = curNode->height / binHeight;
                    rectForCurNode.ll.y = cellCenter.y - 0.5 * binHeight;
                    rectForCurNode.ur.y = cellCenter.y + 0.5 * binHeight;
                }
                curNodeDensityGradient -= meshPerTier[i].getElectricForce(rectForCurNode) * scaleX * scaleY;
            }
        }
    }
}

bool TierPlacer::stopCondition()
{
    return globalDensityOverflow < 0.1f;
}

void TierPlacer::place()
{
    moduleTypeCount.resize(5, 0);
    for (auto m : db->dbModules)
    {
        if (m->isMacro) {
            moduleTypeCount[0] ++;
            if (m->isFF) {
                moduleTypeCount[3] ++; 
            }
        } 
        else if (m->isTSV) {
            moduleTypeCount[1] ++;
        } 
        else if (m->isTerminal) {
            moduleTypeCount[2] ++;
            if (m->isFF) {
                moduleTypeCount[3] ++; 
            }
        }
        else if (m->isFF) {
            moduleTypeCount[3] ++;
        }
        else {
            moduleTypeCount[4] ++;
        }
    }
    printf("\nModule type count: Macros: %d, TSVs: %d, Terminals: %d, FFs: %d, Standard Cells: %d\n",
           moduleTypeCount[0], moduleTypeCount[1], moduleTypeCount[2], moduleTypeCount[3], moduleTypeCount[4]);
    stopConditionPerTier.resize(numTiers, false);
    bestGlobalDensityOverflow = 1;
    
    // 收集每层的所有FF节点
    ffNodesPerTier.resize(numTiers);
    for (auto ff : db->dbFFs)
    {
        assert(modulePosition.contains(ff));
        size_t tierId = modulePosition[ff].tierId;
        ffNodesPerTier[tierId].push_back(ff);
    }
    for (size_t tierId = 0; tierId < numTiers; tierId++)
    {
        printf("Tier %zu: %zu FFs\n", tierId, ffNodesPerTier[tierId].size());
    }


    auto moveInside = [&](vector<double *> &params)
    {
        for (size_t idx = 0; idx < params.size(); idx++)
        {
            size_t nodeId = idx / 2;
            size_t axis = idx % 2;
            switch (axis)
            {
            case 0:
                *params[idx] = std::clamp(*params[idx],
                                          coreRegion.ll.x + freeNodes[nodeId]->width / 2,
                                          coreRegion.ur.x - freeNodes[nodeId]->width / 2);
                break;
            case 1:
                *params[idx] = std::clamp(*params[idx],
                                          coreRegion.ll.y + freeNodes[nodeId]->height / 2,
                                          coreRegion.ur.y - freeNodes[nodeId]->height / 2);
                break;
            default:
                printf("ERROR: axis index should be less than 2 (%d)", axis);
            }
        }
    };
    NesterovOptimizer opt(this, moveInside);
    opt.initialize();
    if (timing_) {
        bool changed = timing_->timingIteration(modulePosition);
        (void)changed; // 暂时不触发预条件器重建
    }
    double hpwl = totalHPWL();
    int iterCount = 0;
    printf("=== Start place optimization ===\n");
    printf("total hpwl:%f\n", hpwl);
    for (size_t tierId = 0; tierId < numTiers; tierId++)
    {
        printf("Tier:%d\tDensityOverflow:%f\n", tierId, densityOverflowPerTier[tierId]);
        printf("stopCondition:%s\n", stopConditionPerTier[tierId]? "true" : "false");
    }
    printf("total density overflow:%f\n", globalDensityOverflow);
    
    while (iterCount <= 500) // 
    {
        
        opt.step();
        if (timing_ && (iterCount > 0) && (iterCount % k_timing_ == 0)) {
            // 传递iteration编号，用于标注slack数据（用于AI训练）
            bool need_repc = timing_->timingIteration(modulePosition, iterCount);
            // 如果以后有预条件器对象，可按 need_repc 决定是否重建
            // if (need_repc) rebuildPreconditioner();
        }
        printf("\n");
        double newHpwl = totalHPWL();
        updatePenaltyFactor(hpwl, newHpwl);
        hpwl = newHpwl;
        

        printf("iter:%d\n", iterCount);
        printf("penalty factor:%.15f\n", lambda);
        printf("total hpwl:%f\n", hpwl);
        for (size_t tierId = 0; tierId < numTiers; tierId++)
        {
            printf("Tier:%d\tDensityOverflow:%f,  stopCondition:%s\n", tierId, densityOverflowPerTier[tierId], stopConditionPerTier[tierId]? "true" : "false");
        }
            
        if (globalDensityOverflow < bestGlobalDensityOverflow)
        {
            bestGlobalDensityOverflow = globalDensityOverflow;
            bestIteration = iterCount;
        }
        printf("total density overflow:%f. Best Density overflow:%f  at iteration %d\n", globalDensityOverflow, bestGlobalDensityOverflow, bestIteration);
        if (iterCount % 20 == 0)
        {
            string figName = "TierPlacement_iter=" + to_string(iterCount) + ".bmp";
            plotCurrentPlacement(figName);
        }
        if (std::all_of(stopConditionPerTier.begin(), stopConditionPerTier.end(), [](bool condition){ return condition; }))
        {
            printf("Stop at iteration %d.\n", iterCount);
            finishTierPlacer(iterCount);
            break;
        }
        if ((globalDensityOverflow < 0.1f) && (lambda >200)  )
        {
            printf("Stop at iteration %d due to global density overflow < 0.1 and penalty factor >200.\n", iterCount);
            finishTierPlacer(iterCount);
            break;
        }
        if ((lambda >400)  ) // 300 two tier;  600 multitier
        {
            printf("Stop at iteration %d due to  penalty factor too big.\n", iterCount);
            finishTierPlacer(iterCount);
            break;
        }


        iterCount++;
    }
}

/*
    initialization
*/
void TierPlacer::setCoreRegion()
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
    coreRegion.ll = POS_2D{left, bottom};
    coreRegion.ur = POS_2D{right, up};
    std::cout << "CoreRegion:" << std::endl;
    std::cout << "ll pos: " << coreRegion.ll << std::endl;
    std::cout << "ur pos: " << coreRegion.ur << std::endl
              << std::endl;
}

void TierPlacer::fillerInitialization()
{
    // calculate the total movable & terminal nodes area
    movableNodesAreaPerTier.resize(numTiers);
    movableNodesNumberPerTier.resize(numTiers);
    terminalAreaPerTier.resize(numTiers);
    fillerNumPerTier.resize(numTiers);
    TSVCountPerTier.resize(numTiers);
    TSVareaPerTier.resize(numTiers);
    std::fill(TSVareaPerTier.begin(), TSVareaPerTier.end(), 0.0);
    std::fill(TSVCountPerTier.begin(), TSVCountPerTier.end(), 0);
    std::fill(movableNodesAreaPerTier.begin(), movableNodesAreaPerTier.end(), 0.0);
    std::fill(movableNodesNumberPerTier.begin(), movableNodesNumberPerTier.end(), 0);
    std::fill(terminalAreaPerTier.begin(), terminalAreaPerTier.end(), 0.0);
    printf("Number of modules to process: %zu\n", db->dbModules.size());

    double totalStandardCellArea = 0;
    size_t numStandardCells = 0;
    double totalMacroArea =0;
    vector<double>  macroAreaPerTier(numTiers, 0.0);
    vector<int> macroCountPerTier(numTiers, 0); 
    // size_t numMacro =0;

    double totalModuleArea = 0;
    for (auto m : db->dbModules)
    {
        if ( (!m->isMacro) && (m->moveType != TSV)  )
        {
            totalStandardCellArea += m->getArea();
            numStandardCells++;
        }
        else if (m->isMacro) 
        {
            totalMacroArea += m->getArea();
            macroCountPerTier[modulePosition[m].tierId]++;
            macroAreaPerTier[modulePosition[m].tierId] += m->getArea();
        }
        // if(m->moveType == TSV)
        // {
        //     printf("area of the module: %f\n", m->getArea());
        //     assert(abs(m->getArea()) < EPS);
        // }
        totalModuleArea += m->getArea();
        movableNodesAreaPerTier[modulePosition[m].tierId] += m->getArea();
        movableNodesNumberPerTier[modulePosition[m].tierId]++;
        if (m->moveType == TSV)
        {
            TSVCountPerTier[modulePosition[m].tierId]++;
            TSVareaPerTier[modulePosition[m].tierId] += m->getArea();
        }
        // no terminals at this moment
    }
    double avgFillerArea = totalStandardCellArea / numStandardCells;
    double fillerHeight = db->commonRowHeight;
    double fillerWidth = avgFillerArea / fillerHeight;

    printf("totalModuleArea:%f\n\n", totalModuleArea);
    // for (auto area : movableNodesAreaPerTier)
    // {
    //     printf("movableNodesArea:%f\n", area);
    // }
    printf("filler:\n");
    printf("fillerWidth:%f\tfillerHeight:%f\n", fillerWidth, fillerHeight);
    // determine filler numbers per tier
    fillers.clear();
    for (size_t tierIdx = 0; tierIdx < numTiers; tierIdx++)
    {
        double tierArea = db->dbTiers[tierIdx].getArea();
        // printf
        double whiteSpaceArea = tierArea - terminalAreaPerTier[tierIdx];
        double tierFillerArea = targetDensity * whiteSpaceArea - movableNodesAreaPerTier[tierIdx];

        printf("Tier:%d; Tier area:%.0f\n", tierIdx, tierArea);
        printf("terminalArea: %.1f     whiteSpaceArea: %.0f  targetDensity: %f   moveableNodesNumber: %d        moveableNodesArea: %.0f   \n", 
                terminalAreaPerTier[tierIdx], whiteSpaceArea, targetDensity, movableNodesNumberPerTier[tierIdx], movableNodesAreaPerTier[tierIdx]);
        printf("macroCount:%d\tmacroArea:%.0f    macroRatio: %.2f\n", macroCountPerTier[tierIdx], macroAreaPerTier[tierIdx], macroAreaPerTier[tierIdx] / tierArea);
        printf("TSVCount:%d\tTSVarea:%.0f\n", TSVCountPerTier[tierIdx], TSVareaPerTier[tierIdx]);

        if (tierFillerArea < 0)
        {
            printf("Tier %zu has no filler area.\n", tierIdx);
            exit(-1);
        }
        size_t fillerNum = static_cast<size_t>(floor(tierFillerArea / avgFillerArea));
        for (size_t i = 0; i < fillerNum; i++)
        {
            string fillerName = "f" + to_string(tierIdx) + "_" + to_string(i);
            Module curFiller(fillerName, fillerWidth, fillerHeight, TIER_FIXED, false);
            fillers.push_back(curFiller);
        }
        fillerNumPerTier[tierIdx] = fillerNum;
        printf("TierFillerArea:%.0f\tfillerNum:%d\n\n", tierFillerArea, fillerNum);
    }
    printf("\n");
}

// set initial node position
void TierPlacer::nodeInitialization()
{
    if(gArg.CheckExist("TP0init"))
    {
        printf("In Tier Place, all modules will be initialized at the center of the core region.\n");
        for (auto m : db->dbModules)
        {
            assert(modulePosition.contains(m));
            //! initial position is set to the center of the core region.  or based on the result of eplace-3d
            modulePosition[m].position = coreRegion.getCenter();
            freeNodes.push_back(m);
        }
    }
    else
    {
        for (auto m : db->dbModules)
        {
            assert(modulePosition.contains(m));
            freeNodes.push_back(m);
        }
    }
    assert(fillers.size() == std::accumulate(fillerNumPerTier.cbegin(), fillerNumPerTier.cend(), 0));
    auto begin = fillers.begin();
    size_t tierIdx = 0;
    for (size_t curTierFillerNum : fillerNumPerTier)
    {
        for (auto curFiller = begin; curFiller < begin + curTierFillerNum; curFiller++)
        {
            Module *f = &*curFiller;
            freeNodes.push_back(f);
            modulePosition[f].tierId = tierIdx;
            modulePosition[f].position = POS_2D{
                uniformDistribution(coreRegion.ll.x, coreRegion.ur.x),
                uniformDistribution(coreRegion.ll.y, coreRegion.ur.y)};
        }
        tierIdx++;
        begin += curTierFillerNum;
    }
}

double TierPlacer::computeAvgCellArea()
{
    double totalArea = 0;
    int standardCellCount = 0;
    for (Module *module : db->dbModules) // 遍历所有模块
    {
        if (!module->isMacro) // 只计算标准单元
        {
            double width = module->width;
            double height = module->height;
            totalArea += width * height;
            standardCellCount++;
        }
    }
    if (standardCellCount == 0)
    {
        printf("Warning: No standard cells found. Using default average volume.\n");
        return 1.0;
    }
    return totalArea / standardCellCount;
}

int nearestPowerOfTwo(int num)
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

void TierPlacer::meshInitialization()
{
    printf("mesh:\n");
    meshPerTier.clear();
    double averageCellArea = computeAvgCellArea();
    for (size_t i = 0; i < numTiers; i++)
    {
        double tierArea = db->dbTiers[i].getArea();
        int totalBins = tierArea / (averageCellArea * targetDensity);
        int m2D = std::sqrt(totalBins);
        m2D = nearestPowerOfTwo(std::max(4, std::min(m2D, 1024))); // 保证 `m3D` 为 `2^n`
        meshPerTier.push_back(ElectricMesh2D(coreRegion.ll, coreRegion.ur, m2D, m2D));
        printf("tier:%d,mesh:%d*%d\n", i, m2D, m2D);
    }
    printf("\n");
}

void TierPlacer::penaltyFactorInitialization()
{
    updateDensityGradient();
    updateWirelengthGradient();
    double numerator = 0;
    double denominator = 0;
    for (auto m : freeNodes)
    {
        const VECTOR_2D &wg = wirelengthGradient[m];
        const VECTOR_2D &dg = densityGradient[m];
        numerator += (abs(wg.x) + abs(wg.y));
        denominator += (abs(dg.x) + abs(dg.y));
    }
    lambda = numerator / denominator; //! lambda init
    
    if (gArg.CheckExist("TP0init"))
    {
        lambda = lambda * 100.0; // if TP0init, lambda is set to a small value
    }
    printf("Initial penalty factor:%lf\n\n", lambda);
}

void TierPlacer::gradientInitialization()
{
    for (auto m : freeNodes)
    {
        wirelengthGradient[m] = VECTOR_2D();
        densityGradient[m] = VECTOR_2D();
        clockGradient[m] = VECTOR_2D();
        clockGradientLastTime[m] = VECTOR_2D();
        clocknetGradient[m] = VECTOR_2D();
        clockMassGradient[m] = VECTOR_2D();
    }
}


void TierPlacer::updateClockGradient()
{
    if (!clockOptimizationEnabled)
    {
        return;
    }
    
    // 检查是否在早期阶段跳过时钟优化
    if (globalDensityOverflow >= 0.8)
    {
        return;
    }

    // 按层分别处理FF节点

    for (size_t tierId = 0; tierId < numTiers; tierId++)
    {
        // printf("Calculating clock gradient for tier %zu with %zu FFs\n", tierId, ffNodesInTier.size());

        if (ffNodesPerTier[tierId].empty())
        {
            continue;
        }



        // 创建实例
        // CTSDB* ctsdb;
        // MMM* MMMcreator;
        // ZSTDMERouter* ctsRouter;

        CTSDB *ctsdb = new CTSDB();
        ctsdb->initWithTierFFs(ffNodesPerTier, modulePosition, tierId);

        MMM *mmmtree = new MMM(ctsdb);
        mmmtree->initSinkNodes();

        TreeTopology *topo = new TreeTopology(ctsdb);
        ZSTDMERouter *router = new ZSTDMERouter(ctsdb);
        router->setDelayModel(ELMORE_DELAY); // 使用Elmore延迟模型
        router->setTopology(topo);
        
        if ( gArg.CheckExist("simple") && (globalDensityOverflow >0.20)  )
        {
            printf("Densityoverflow: %f, usesimple,  ", densityOverflowPerTier[tierId]);
            addClkNetGradientMassCenter(ffNodesPerTier[tierId]);
            
            int index = 0;
            for (auto ff : ffNodesPerTier[tierId])
            {
                clockGradient[ff].x = clockGradientLastTime[ff].x * 0.3;
                clockGradient[ff].y = clockGradientLastTime[ff].y * 0.3;

                clockGradient[ff].x += 0.7 * clockMassGradient[ff].x;
                clockGradient[ff].y += 0.7 * clockMassGradient[ff].y;

                clockGradientLastTime[ff].x = clockMassGradient[ff].x;
                clockGradientLastTime[ff].y = clockMassGradient[ff].y;

                ff->clockGradientPreconditioner = 0;
                index++;
            }


        }


        if (gArg.CheckExist("MMM") || ( gArg.CheckExist("simple") && ( (0.14 < globalDensityOverflow)  &&  (globalDensityOverflow <= 0.20) )  ) )
        {
            printf("Densityoverflow: %f, useMMM, ", densityOverflowPerTier[tierId]);
            mmmtree->ctsdb->setSinkLocationWithTierPlacer(ffNodesPerTier, modulePosition, tierId);
            mmmtree->updateSinkLocation();
            mmmtree->setArborealGradientParameters(invertedGamma, globalDensityOverflow);
            // printf("MMM creator initialized with %zu sinks.", mmmtree->sinks.size());
            mmmtree->doMMM();

            int index = 0;
            for (MMMNode *curSink : mmmtree->sinks)
            {
                assert(index == curSink->id);

                Module* ff = ffNodesPerTier[tierId][index];
                
                clockGradient[ff].x = clockGradientLastTime[ff].x * 0.3;
                clockGradient[ff].y = clockGradientLastTime[ff].y * 0.3;

                clockGradient[ff].x += 0.7 * curSink->accumulatedArborealGradient.x;
                clockGradient[ff].y += 0.7 * curSink->accumulatedArborealGradient.y;

                clockGradientLastTime[ff].x = curSink->accumulatedArborealGradient.x;
                clockGradientLastTime[ff].y = curSink->accumulatedArborealGradient.y;

                ff->clockGradientPreconditioner = curSink->accumulatedPreconditioner;
                index++;
            }
            // printf("Clock gradient calculation for tier %zu completed using MMM.\n", tierId);
        }  
        else if (gArg.CheckExist("DME")   || ( gArg.CheckExist("simple") && (globalDensityOverflow <= 0.14 )  )  ) ///DME
        {
            printf("Densityoverflow: %f, useDME, ", densityOverflowPerTier[tierId]);
            router->ctsdb->setSinkLocationWithTierPlacer(ffNodesPerTier, modulePosition, tierId);
            router->setArborealGradientParameters(invertedGamma, globalDensityOverflow);
            // printf("ZSTDMERouter initialized with %zu sinks.\n", router->ctsdb->dbSinks.size());
            router->constructVirtualTree();

            int index = 0;
            for (Sink curSink : router->ctsdb->dbSinks)
            {
                assert(router->topology->globalTreeNodes[index]->id == index);
                assert(index == curSink.id);
                
                Module* ff = ffNodesPerTier[tierId][index];
                
                clockGradient[ff].x = clockGradientLastTime[ff].x * 0.3;
                clockGradient[ff].y = clockGradientLastTime[ff].y * 0.3;

                clockGradient[ff].x += 0.7 * router->topology->globalTreeNodes[index]->accumulatedArborealGradient.x;
                clockGradient[ff].y += 0.7 * router->topology->globalTreeNodes[index]->accumulatedArborealGradient.y;

                clockGradientLastTime[ff].x = router->topology->globalTreeNodes[index]->accumulatedArborealGradient.x;
                clockGradientLastTime[ff].y = router->topology->globalTreeNodes[index]->accumulatedArborealGradient.y;

                ff->clockGradientPreconditioner = router->topology->globalTreeNodes[index]->accumulatedPreconditioner;
                index++;
            }
            router->topology->clearTopology();
            // printf("Clock gradient calculation for tier %zu completed using DME.\n", tierId);
            // cout << "clear topo done\n";
        }

        if (gArg.CheckExist("TPpseudoClkNet"))
        {
            addClkNetGradient(ffNodesPerTier[tierId]);

            // for (size_t i = 0; i < ffNodesPerTier[tierId].size(); i++)
            // {
            //     Module *ff = ffNodesPerTier[tierId][i];
            //     if (!gArg.CheckExist("MMM") && !gArg.CheckExist("DME"))
            //     {
            //         clockGradient[ff].x =  clocknetGradient[ff].x;
            //         clockGradient[ff].y =  clocknetGradient[ff].y;
            //         continue;
            //     }
            //     clockGradient[ff].x = 2 * (0.5 * clockGradient[ff].x + 0.5 * clocknetGradient[ff].x);
            //     clockGradient[ff].y = 2 * (0.5 * clockGradient[ff].y + 0.5 * clocknetGradient[ff].y);
            // }
        }
        // 清理内存
        delete router;
        delete topo;
        delete mmmtree;
        delete ctsdb;


        
    }
}


void TierPlacer::outputBookShelfByTier(string suffix, bool plOnly)
{
    string benchmarkName;
    if (gArg.CheckExist("benchmarkName"))
    {
        gArg.GetString("benchmarkName", &benchmarkName);
    }
    else
    {
        printf("ERROR: benchmarkName is not set. Please set it using --benchmarkName.\n");
        // exit(-1);
    }
    

    string outputFilePath = "./temp/";
    if (gArg.CheckExist("outputDir"))
    {
        gArg.GetString("outputDir", &outputFilePath);
        outputFilePath += "forLGDP/";
        gArg.Override("BookshelfoutputDir", outputFilePath);
    }

    // 为每个tier分别输出
    clockWLPerTier.resize(numTiers, 0.0);
    for (size_t tierId = 0; tierId < numTiers; tierId++)
    {
        string tierSuffix = suffix + "_tier" + to_string(tierId);
        gArg.Override("outputSuffix", tierSuffix);
        printf("benchmarkName: %s, outputSuffix: %s, bookshelfoutputDir: %s\n",
               benchmarkName.c_str(), tierSuffix.c_str(), outputFilePath.c_str());
        
        // string cmd = "rm -rf " + outputFilePath;
        // system(cmd.c_str());

        string cmd = "mkdir -p " + outputFilePath;
        system(cmd.c_str());
        if (!plOnly)
        {
            outputAUXByTier(tierId);
            outputNodesByTier(tierId);
            outputNetsByTier(tierId);
            outputSCLByTier(tierId);
            outputtechByTier(tierId);
            outputFFsByTier(tierId);
        }

        outputPLByTier(tierId);

        // 统计GP后各层时钟树的线长
        double clkWL = clkWLPerTier(tierId);
        clockWLPerTier[tierId] = clkWL;
        printf("Clock wirelength for tier %zu: %.2f\n", tierId, clkWL);
    }
    // 输出总的时钟树线长
    double totalClockWL = std::accumulate(clockWLPerTier.begin(), clockWLPerTier.end(), 0.0);
    printf("\n\n\nBefore Legalization and DP: Total clock wirelength sum of all tiers: %.2f\n", totalClockWL);
}

void TierPlacer::outputPLByTier(size_t tierId)
{
    string outputFilePath;
    gArg.GetString("BookshelfoutputDir", &outputFilePath);

    string benchmarkName;
    gArg.GetString("benchmarkName", &benchmarkName);

    string suffix;
    gArg.GetString("outputSuffix", &suffix);

    outputFilePath += benchmarkName;
    outputFilePath += "-" + suffix + ".pl";

    cout << "Output PL file for tier " << tierId << ": " << outputFilePath << endl;

    FILE *out = fopen(outputFilePath.c_str(), "w");
    if (!out)
    {
        cerr << "Cannot open output file\n";
        return;
    }

    fprintf(out, "UCLA pl 1.0\n\n");
    // fprintf(out, "# Tier %zu placement\n\n", tierId);

    char *orientN = "N";

    // 输出当前层的所有模块
    for (Module *curNode : db->dbModules)
    {
        if (modulePosition[curNode].tierId == tierId)
        {
            const POS_2D& pos = modulePosition[curNode].position;
            fprintf(out, "%s\t%.0f\t%.0f : %s",
                    curNode->name.c_str(),
                    pos.x - curNode->width / 2,  // 转换为左下角坐标
                    pos.y - curNode->height / 2,
                    orientN);
            // fprintf(out, "\n");
            
            if (curNode->isTerminal)
            {
                if (curNode->isNI)
                    fprintf(out, "\n");
                else
                    fprintf(out, "\n");
            }
            else
            {
                fprintf(out, "\n");
            }
        }
    }

    // 输出当前层的filler
    // for (auto &f : fillers)
    // {
    //     if (modulePosition[&f].tierId == tierId)
    //     {
    //         const POS_2D& pos = modulePosition[&f].position;
    //         fprintf(out, "%s\t%.0f\t%.0f : %s\n",
    //                 f.name.c_str(),
    //                 pos.x - f.width / 2,
    //                 pos.y - f.height / 2,
    //                 orientN);
    //     }
    // }

    fprintf(out, "\n\n");
    fclose(out);
}

void TierPlacer::outputNodesByTier(size_t tierId)
{
    string outputFilePath;
    gArg.GetString("BookshelfoutputDir", &outputFilePath);

    string benchmarkName;
    gArg.GetString("benchmarkName", &benchmarkName);

    string suffix;
    gArg.GetString("outputSuffix", &suffix);

    outputFilePath += benchmarkName;
    outputFilePath += "-" + suffix + ".nodes";

    cout << "Output Nodes file for tier " << tierId << ": " << outputFilePath << endl;

    FILE *out = fopen(outputFilePath.c_str(), "w");
    if (!out)
    {
        cerr << "Cannot open output file\n";
        return;
    }

    // 统计当前层的模块数量
    int nodeCount = 0;
    int terminalCount = 0;
    
    for (Module *curNode : db->dbModules)
    {
        if (modulePosition[curNode].tierId == tierId)
        {
            nodeCount++;
            if (curNode->isTerminal)
                terminalCount++;
        }
    }
    
    // // 添加filler数量
    // for (auto &f : fillers)
    // {
    //     if (modulePosition[&f].tierId == tierId)
    //     {
    //         nodeCount++;
    //     }
    // }

    fprintf(out, "UCLA nodes 1.0\n\n");
    // fprintf(out, "# Tier %zu nodes\n", tierId);
    fprintf(out, "NumNodes : %d\n", nodeCount);
    fprintf(out, "NumTerminals : %d\n\n", terminalCount);

    // 输出当前层的模块
    for (Module *curNode : db->dbModules)
    {
        if (modulePosition[curNode].tierId == tierId)
        {
            double w = curNode->width;
            double h = curNode->height;

            if (curNode->isTerminal)
            {
                if (curNode->isNI)
                {
                    fprintf(out, " %30s %10.0f %10.0f terminal_NI\n",
                            curNode->name.c_str(), w, h);
                }
                else
                {
                    fprintf(out, " %30s %10.0f %10.0f terminal\n",
                            curNode->name.c_str(), w, h);
                }
            }
            else
            {
                fprintf(out, " %30s %10.0f %10.0f\n",
                        curNode->name.c_str(), w, h);
            }
        }
    }

    // // 输出当前层的filler
    // for (auto &f : fillers)
    // {
    //     if (modulePosition[&f].tierId == tierId)
    //     {
    //         fprintf(out, " %30s %10.0f %10.0f\n",
    //                 f.name.c_str(), f.width, f.height);
    //     }
    // }

    fprintf(out, "\n\n");
    fclose(out);
}

void TierPlacer::outputNetsByTier(size_t tierId)
{
    string outputFilePath;
    gArg.GetString("BookshelfoutputDir", &outputFilePath);

    string benchmarkName;
    gArg.GetString("benchmarkName", &benchmarkName);

    string suffix;
    gArg.GetString("outputSuffix", &suffix);

    outputFilePath += benchmarkName;
    outputFilePath += "-" + suffix + ".nets";

    cout << "Output Nets file for tier " << tierId << ": " << outputFilePath << endl;

    FILE *out = fopen(outputFilePath.c_str(), "w");
    if (!out)
    {
        cerr << "Cannot open output file\n";
        return;
    }

    // 收集当前层相关的网络
    vector<Net*> tierNets;
    int totalPins = 0;
    
    for (Net *curNet : db->dbNets)
    {
        bool hasModuleInTier = false;
        int pinsInTier = 0;
        
        for (Pin *curPin : curNet->netPins)
        {
            if (modulePosition[curPin->module].tierId == tierId)
            {
                hasModuleInTier = true;
                pinsInTier++;
            }
        }
        
        if (hasModuleInTier)
        {
            tierNets.push_back(curNet);
            totalPins += pinsInTier;
        }
    }

    fprintf(out, "UCLA nets 1.0\n\n");
    // fprintf(out, "# Tier %zu nets\n", tierId);
    fprintf(out, "NumNets : %zu\n", tierNets.size());
    fprintf(out, "NumPins : %d\n\n", totalPins);

    for (Net *curNet : tierNets)
    {
        // 只输出当前层的pins
        vector<Pin*> tierPins;
        for (Pin *curPin : curNet->netPins)
        {
            if (modulePosition[curPin->module].tierId == tierId)
            {
                tierPins.push_back(curPin);
            }
            // assert(tierPins.size() == totalPins);
        }
        
        if (!tierPins.empty())
        {
            fprintf(out, "NetDegree : %zu %s\n", tierPins.size(), curNet->name.c_str());
            for (Pin *curPin : tierPins)
            {
                fprintf(out, " %30s B : %.2f %.2f\n",
                        curPin->module->name.c_str(),
                        curPin->offset.x,
                        curPin->offset.y);
            }
        }
    }
    
    fprintf(out, "\n");
    fclose(out);
}

void TierPlacer::outputSCLByTier(size_t tierId)
{
    string outputFilePath;
    gArg.GetString("BookshelfoutputDir", &outputFilePath);

    string benchmarkName;
    gArg.GetString("benchmarkName", &benchmarkName);

    string suffix;
    gArg.GetString("outputSuffix", &suffix);

    outputFilePath += benchmarkName;
    outputFilePath += "-" + suffix + ".scl";

    cout << "Output SCL file for tier " << tierId << ": " << outputFilePath << endl;

    FILE *out = fopen(outputFilePath.c_str(), "w");
    if (!out)
    {
        cerr << "Cannot open output file\n";
        return;
    }

    fprintf(out, "UCLA scl 1.0\n");
    fprintf(out, "# Created for tier %zu\n", tierId);
    fprintf(out, "# User          :\n\n");

    // 使用当前层的布局区域信息
    const Tier& currentTier = db->dbTiers[tierId];
    
    
    fprintf(out, "NumRows : %d\n\n", currentTier.siteRows.size());
    for (SiteRow curRow : currentTier.siteRows)
    {
        double step = curRow.step;
        if (step == 0)
            step = 1.0;
        fprintf(out, "CoreRow Horizontal\n");
        fprintf(out, " Coordinate    : %8.0f\n", curRow.bottom);
        fprintf(out, " Height        : %8.0f\n", curRow.height);
        fprintf(out, " Sitewidth     : %8.0f\n", step);
        fprintf(out, " Sitespacing   : %8.0f\n", step);
        fprintf(out, " Siteorient    : 1\n");
        fprintf(out, " Sitesymmetry  : 1\n");
        fprintf(out, " SubrowOrigin  : %8.0f Numsites : %8.0f\n",
                curRow.start.x, (curRow.end.x - curRow.start.x) / curRow.step);

        fprintf(out, "End\n");
    }
    fprintf(out, "\n");
    fclose(out);
}

void TierPlacer::outputAUXByTier(size_t tierId)
{
    string outputFilePath;
    gArg.GetString("BookshelfoutputDir", &outputFilePath);

    string benchmarkName;
    gArg.GetString("benchmarkName", &benchmarkName);

    string suffix;
    gArg.GetString("outputSuffix", &suffix);

    string outputAUXFilePath = outputFilePath;
    outputAUXFilePath += benchmarkName;
    outputAUXFilePath += "-" + suffix + ".aux";

    cout << "Output AUX file for tier " << tierId << ": " << outputAUXFilePath << endl;

    ofstream out(outputAUXFilePath);
    if (!out)
    {
        cerr << "Cannot open output file\n";
        return;
    }

    out << "RowBasedPlacement : "
        <<  benchmarkName + "-" + suffix + ".nodes "
        <<  benchmarkName + "-" + suffix + ".nets "
        <<  benchmarkName + "-" + suffix + ".wts "
        <<  benchmarkName + "-" + suffix + ".pl "
        <<  benchmarkName + "-" + suffix + ".scl "
        <<  benchmarkName + "-" + suffix + ".tech "
        <<  benchmarkName + "-" + suffix + ".ffs \n\n";
    
    // out << "# Tier " << tierId << " placement files\n";
}

void TierPlacer::outputtechByTier(size_t tierId)
{
    string outputFilePath;
    gArg.GetString("BookshelfoutputDir", &outputFilePath);

    string benchmarkName;
    gArg.GetString("benchmarkName", &benchmarkName);

    string suffix;
    gArg.GetString("outputSuffix", &suffix);

    outputFilePath += benchmarkName;
    outputFilePath += "-" + suffix + ".tech";

    cout << "Output TECH file for tier " << tierId << ": " << outputFilePath << endl;

    FILE *out = fopen(outputFilePath.c_str(), "w");
    if (!out)
    {
        cerr << "Cannot open output file\n";
        return;
    }

    fprintf(out, "UMICH tech 1.0\nUnitTechScail : 12 1400");
    
    
    fclose(out);
}

void TierPlacer::outputFFsByTier(size_t tierId)
{
    string outputFilePath;
    gArg.GetString("BookshelfoutputDir", &outputFilePath);

    string benchmarkName;
    gArg.GetString("benchmarkName", &benchmarkName);

    string suffix;
    gArg.GetString("outputSuffix", &suffix);

    outputFilePath += benchmarkName;
    outputFilePath += "-" + suffix + ".ffs";

    cout << "Output FFS file for tier " << tierId << ": " << outputFilePath << endl;

    FILE *out = fopen(outputFilePath.c_str(), "w");
    if (!out)
    {
        cerr << "Cannot open output file\n";
        return;
    }

    fprintf(out, "UCLA ffs 1.0\n\n");
    
    fprintf(out, "NumFFs : %zu\n", ffNodesPerTier[tierId].size());

    // 输出当前层的所有FFs
    for (Module *curNode : ffNodesPerTier[tierId])
    {
        
        fprintf(out, "%s\n", curNode->name.c_str());
    }
    fclose(out);
}


void TierPlacer::finishTierPlacer(int iterCount)
{
    string figName = "TierPlacement_iter=" + to_string(iterCount) + ".bmp";
    plotCurrentPlacement(figName);

    //write back positions to db 写回位置到db
    for (Module *m : db->dbModules)
    {
        assert(modulePosition.contains(m));
        m->currentPos = modulePosition[m];
        // ModulePosition(modulePosition[m].tierId, 
        //                                 modulePosition[m].position.x, 
        //                                 modulePosition[m].position.y);
    }

    printf("\n\n\nTP-place clk aware time: %.2f \n\n\n", clkawareTime);



    // 输出最终的布局结果bookshelf format
    // outputBookShelfByTier("lgdp", false);
        
}

double TierPlacer::clkWLPerTier(int tierId)
{
    CTSDB *ctsdb = new CTSDB();
    ctsdb->initWithTierFFs(ffNodesPerTier, modulePosition, tierId);
    TreeTopology *topo = new TreeTopology(ctsdb);
    ZSTDMERouter *router = new ZSTDMERouter(ctsdb);
    router->setDelayModel(ELMORE_DELAY); // 使用Elmore延迟模型
    router->setTopology(topo);
    router->ctsdb->setSinkLocationWithTierPlacer(ffNodesPerTier, modulePosition, tierId);
    topo->clearTopology();
    topo->buildTreeUsingNearestNeighborGraph_BucketDecomposition();
    router->topDown();
    // cout << "\nbefore legalization: \n";
    double clkWL = router->buildSolution();
    return clkWL;
}


void TierPlacer::addClkNetGradient(const vector<Module*>& ffNodesCurTier)
{
    const vector<Module*>& tierFFs = ffNodesCurTier;
    vector<Pin*> pseudoClkPins;
    for(auto m: tierFFs)
    {
        pseudoClkPins.push_back(new Pin{m, POS_2D{0, 0},PinDirection::PIN_DIRECTION_IN}); // create a pseudo pin for each FF
    }
    // get clock net
    Net pseudoClkNet("pseudo_clk", pseudoClkPins);

    // prepare cache
    vector<WAClkWirelengthGradientCache> cache(pseudoClkPins.size());
    addWAClkWirelengthGradientOneNet(pseudoClkNet, cache);

    for(auto pin: pseudoClkPins)
    {
        delete pin; // delete the pseudo pin
    }
}

void TierPlacer::addClkNetGradientMassCenter(const vector<Module*>& ffNodesCurTier)
{
    const vector<Module*>& tierFFs = ffNodesCurTier;
    // printf("FF number: %zu\n", tierFFs.size());
    // calculate the mass center of all FFs
    POS_2D massCenter(0, 0);
    for (auto m : tierFFs)
    {
        const ModulePosition &modPos = modulePosition[m];
        massCenter.x += modPos.getX();
        massCenter.y += modPos.getY();
    }
    massCenter.x /= db->dbFFs.size();
    massCenter.y /= db->dbFFs.size();

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
    for (auto m : tierFFs)
    {
        const ModulePosition &modPos = modulePosition[m];
        auto curSinkGradient = VECTOR_2D{
            sign(modPos.getX() - massCenter.x),
            sign(modPos.getY() - massCenter.y)
        };
        clockMassGradient[m] = curSinkGradient;
    }
}


// void SpacePlacer::addClkNetGradientMassCenter()
// {
//     // calculate the mass center of all FFs
//     POS_3D massCenter(0, 0, 0);
//     for (auto m : db->dbFFs)
//     {
//         const POS_3D &modPos = module3DPosition[m];
//         massCenter.x += modPos.x;
//         massCenter.y += modPos.y;
//         massCenter.z += modPos.z;
//     }
//     massCenter.x /= db->dbFFs.size();
//     massCenter.y /= db->dbFFs.size();
//     massCenter.z /= db->dbFFs.size();

//     // set the gradient to the mass center
//     auto sign = [](double x) -> double
//     {
//         if (x > 0)
//             return 1.0;
//         else if (x < 0)
//             return -1.0;
//         else
//             return 0.0;
//     };
//     for (auto m : db->dbFFs)
//     {
//         const POS_3D &modPos = module3DPosition[m];
//         auto curSinkGradient = VECTOR_3D{
//             sign(modPos.x - massCenter.x),
//             sign(modPos.y - massCenter.y),
//             sign(modPos.z - massCenter.z)
//         };
//         wirelengthGradient[m] += curSinkGradient;
//     }
// }