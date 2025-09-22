#include "mmm.h"

void MMM::initSinkNodes()
{
    int index = 0;
    for (Sink curSink : ctsdb->dbSinks)
    {
        assert(index == curSink.id);

        MMMNode *curRoot = new MMMNode(curSink);
        curRoot->id = index;
        curRoot->loadCapacitance = curSink.capacitance;

        sinks.emplace_back(curRoot);
        index++;
    }
}

MMMNode *MMM::MMMmain(vector<MMMNode *> currentSinks, int cutDirection)
{
    Point_2D centerOfMass = Point_2D();

    int currentSinkCount = currentSinks.size();

    // cout << "cur sink count: " << currentSinkCount << endl;
    // cout << "cutDirection: " << cutDirection << endl;
    if (currentSinkCount <= 1)
    {
        if (currentSinkCount == 1) // reach bottom
        {
            return currentSinks[0];
        }
        else
        {
            return NULL;
        }
    }
    // if (currentSinkCount == 3)
    // {
    //     for (MMMNode* curSink : currentSinks)
    //     {
    //         cout << *curSink << endl;
    //         cout << ctsdb->pdb->dbFFs[curSink->id]->name << endl;
    //     }
    // }
    vector<double> xORyCoordinates; // stores x OR y coordinates, then we will find the median of this vector

    for (MMMNode *curSink : currentSinks)
    {
        if (cutDirection)
        {
            xORyCoordinates.push_back(curSink->x);
        }
        else
        {
            xORyCoordinates.push_back(curSink->y);
        }
        centerOfMass.x += curSink->x;
        centerOfMass.y += curSink->y;
    }
    centerOfMass.x /= double(currentSinkCount);
    centerOfMass.y /= double(currentSinkCount);

    // todo: efficiently get median
    double xORyMedian = getMedian(xORyCoordinates, 0, xORyCoordinates.size() - 1);

    // if (currentSinkCount == 3)
    // {
    //     cout << "Median is " << xORyMedian << endl
    //          << endl;
    // }

    // todo: get SA and SB by partitioning the currentSinks by xORyMedian
    vector<MMMNode *> SA;
    vector<MMMNode *> SB;

    int SAcount = 0;

    for (MMMNode *curSink : currentSinks)
    {
        if (cutDirection)
        {
            if (double_lessorequal(curSink->x, xORyMedian) && SAcount < (currentSinkCount + 1) / 2)
            {
                SA.push_back(curSink);
                SAcount++;
            }
            else
            {
                SB.push_back(curSink);
            }
        }
        else
        {
            if (double_lessorequal(curSink->y, xORyMedian) && SAcount < (currentSinkCount + 1) / 2)
            {
                SA.push_back(curSink);
                SAcount++;
            }
            else
            {
                SB.push_back(curSink);
            }
        }
    }
    // cout << "Partitioned:" << SA.size() << " " << SB.size() << endl;
    assert(SA.size() + SB.size() == currentSinkCount);
    // todo: determine cutDirection
    // ! for now: simple logic: x-y-x-y.....
    int cutDirectionOfSA = 1 - cutDirection;
    int cutDirectionOfSB = 1 - cutDirection;

    MMMNode *centerMassOfSA = MMMmain(SA, cutDirectionOfSA);
    MMMNode *centerMassOfSB = MMMmain(SB, cutDirectionOfSB);

    MMMNode *currentCenterOfMass = new MMMNode(centerOfMass); //! this gurantees that the root of the MMMNode tree is the last MMMNode in the vector mergeNodes

    currentCenterOfMass->id = mergeNodes.size() + sinks.size(); // sinks.size() should be constant

    mergeNodes.push_back(currentCenterOfMass);

    currentCenterOfMass->leftChild = centerMassOfSA;
    currentCenterOfMass->rightChild = centerMassOfSB;

    //! major bug: currentCentorOfMass->accumulatedArborealGradient==0!!!!
    //! can't accumulate gradient during tree construction, need a top-down

    assert(currentCenterOfMass->accumulatedArborealGradient.x == 0);
    assert(currentCenterOfMass->accumulatedArborealGradient.y == 0);

    if (centerMassOfSA)
    {
        centerMassOfSA->parent = currentCenterOfMass;
    }
    if (centerMassOfSB)
    {
        centerMassOfSB->parent = currentCenterOfMass;
    }

    return currentCenterOfMass;
}

void MMM::drawSolution()
{
    string plotPath;
    string benchmarkName;
    if (!gArg.GetString("plotPath", &plotPath))
    {
        plotPath = "./";
    }
    gArg.GetString("benchmarkName", &benchmarkName);

    string outFilePath = plotPath + benchmarkName + "_MMMsolution.plt";
    ofstream outfile(outFilePath.c_str(), ios::out);

    outfile << " " << endl;
    outfile << "set terminal png size 4000,4000" << endl;
    outfile << "set output "
            << "\"" << plotPath << benchmarkName + "_MMMsolution"
            << ".png\"" << endl;
    // outfile << "set multiplot layout 1, 2" << endl;
    outfile << "set size ratio -1" << endl;
    outfile << "set nokey" << endl
            << endl;

    // for(int i=0; i<cell_list_top.size(); i++){
    //     outfile << "set label " << i + 2 << " \"" << cell_list_top[i]->get_name() << "\" at " << cell_list_top[i]->get_posX() + cell_list_top[i]->get_width() / 2 << "," << cell_list_top[i]->get_posY() + cell_list_top[i]->get_height() / 2 << " center front" << endl;
    // }
    // outfile << "set xrange [0:" << _pChip->get_width() << "]" << endl;
    // outfile << "set yrange [0:" << _pChip->get_height() << "]" << endl;
    // outfile << "plot[:][:] '-' w l lt 3 lw 2, '-' with filledcurves closed fc \"grey90\" fs border lc \"red\", '-' with filledcurves closed fc \"yellow\" fs border lc \"black\", '-' w l lt 1" << endl << endl;

    outfile << "plot[:][:]  '-' w l lt 3 lw 2, '-' w p pt 7 ps 1, '-' w p pt 5 ps 2, '-' w l lt 4 lw 2, " << endl
            << endl;

    outfile << "# TREE" << endl;
    std::function<void(MMMNode *)> traceToSource = [&](MMMNode *curNode)
    {
        if (curNode->parent == NULL)
        { // reached source
            return;
        }
        auto &nxtNode = curNode->parent;
        plotLinePLT(outfile, curNode->x, curNode->y, nxtNode->x, nxtNode->y);
        traceToSource(nxtNode);
    };
    for (MMMNode *curNode : sinks)
    {
        traceToSource(curNode);
    }
    outfile << "EOF" << endl;

    outfile << "# Sinks" << endl;
    for (MMMNode *curNode : sinks)
    {
        plotLinePLT(outfile, curNode->x, curNode->y, curNode->x, curNode->y);
    }
    outfile << "EOF" << endl;

    MMMNode *root = mergeNodes.back();
    assert(!root->parent);

    outfile << "# Source" << endl;
    plotLinePLT(outfile, root->x, root->y, root->x, root->y);
    outfile << "EOF" << endl;

    // outfile << "# Blockages" << endl;
    // for (Blockage block : this->ctsdb->dbBlockages)
    // {
    //     plotBoxPLT(outfile, block.ll.x, block.ll.y, block.ur.x, block.ll.y, block.ur.x, block.ur.y, block.ll.x, block.ur.y); // counter clock-wise
    // }
    // outfile << "EOF" << endl;

    // outfile << "pause -1 'Press any key to close.'" << endl;
    outfile.close();

    system(("gnuplot " + outFilePath).c_str());

    cout << BLUE << "[Router]" << RESET_COLOR << " - Visualize the solution graph(MMM) in \'" << outFilePath << "\'.\n";
}

int MMM::countLevel(MMMNode *node)
{
    if (!node)
    {
        return 0;
    }
    return max(countLevel(node->leftChild), countLevel(node->rightChild)) + 1;
}

void MMM::accumulateGradients()
{
    MMMNode *root = mergeNodes.back();
    std::function<void(MMMNode *)> preOrderTraversal = [&](MMMNode *curNode)
    {
        if (curNode)
        {
            if (curNode->parent)
            {
                curNode->level = curNode->parent->level + 1;
                
                double levelWeight = calculateLevelWeight(curNode->level, globalDensityOverflow, totalLevelCount);
                double wireWeight =0;
                // double wireWeight = calculateWireWeight( POS_2D(curNode->x, curNode->y), POS_2D(curNode->parent->x, curNode->parent->y),  POS_2D(root->x, root->y),  globalDensityOverflow  );
                // VECTOR_2D curLevelWeight = calculateArborealGradientFor2PinNet_WA(curNode, curNode->parent);//!1
                VECTOR_2D curLevelWeight = calcWAWirelengthGradientOneNet({POS_2D(curNode->x, curNode->y), POS_2D(curNode->parent->x, curNode->parent->y)}, invertedGamma)[0]; //!返回两个点的坐标，其中的第一个点是当前节点，第二个点是父节点
                
                double factor =  1.0;

                // secb.II: add clock wire level weight
                if (gArg.CheckExist("clkwirelevel"))
                {
                    double curDist = abs(curNode->x - curNode->parent->x) + abs(curNode->y - curNode->parent->y);
                    double leftDist = abs(curNode->parent->leftChild->x - curNode->parent->x) + abs(curNode->parent->leftChild->y - curNode->parent->y);
                    double rightDist = abs(curNode->parent->rightChild->x - curNode->parent->x) + abs(curNode->parent->rightChild->y - curNode->parent->y);
                    double brotherMaxDist = max(leftDist, rightDist);
                    // brotherMaxDist = brotherMaxDist == 0 ? 1 : brotherMaxDist; // avoid division by zero

                    if (curDist == brotherMaxDist)
                    {
                        factor = 1.2; 
                    }
                }
                
                // printf("factor=%f, curDist=%f, brotherMaxDist=%f, leftDist=%f, rightDist=%f\n", factor, curDist, brotherMaxDist, leftDist, rightDist);
                

                curLevelWeight.x *= (levelWeight * factor);
                curLevelWeight.y *= (levelWeight * factor);
                // printf("curLevelWeight=(%f, %f), levelWeight=%f, factor=%f\n", curLevelWeight.x, curLevelWeight.y, levelWeight, factor);
                // curLevelWeight.x *= (0.5 *levelWeight + 0.5 * wireWeight); 
                // curLevelWeight.y *= (0.5 *levelWeight + 0.5 * wireWeight);
                
                curNode->accumulatedArborealGradient = curNode->parent->accumulatedArborealGradient + curLevelWeight;
                curNode->accumulatedPreconditioner = curNode->parent->accumulatedPreconditioner + 2 * levelWeight;
            }

            preOrderTraversal(curNode->leftChild);
            preOrderTraversal(curNode->rightChild);
        }
    };
    // MMMNode *root = mergeNodes.back();
    // root->level = 1;
    root->level = 0; //! level start from 0;
    preOrderTraversal(root);
}

void MMM::updateSinkLocation()
{
    int index = 0;
    for (Sink curSink : ctsdb->dbSinks)
    {
        assert(sinks[index]->id == curSink.id);
        sinks[index]->x = curSink.x;
        sinks[index]->y = curSink.y;
        index++;
    }
}

void MMM::doMMM()
{
    // clear mergeNodes(release memory) and update sinks(update location before redo MMM)
    // cout << "merge node count: " << mergeNodes.size()<<endl;
    clockWirelength = 0.0;
    MMMNode *root = MMMmain(sinks, 0);
    assert(root = mergeNodes.back());
    totalLevelCount = countLevel(root);
    // calMaxToSourceDistance(root);
    accumulateGradients();
    clearTree();
}

void MMM::clearTree()
{
    std::function<void(MMMNode *)> postOrderTraversal_sink = [&](MMMNode *curNode)
    {
        int curId = curNode->id;
        if (curNode->leftChild != NULL && curNode->rightChild != NULL)
        {
            postOrderTraversal_sink(curNode->leftChild);
            postOrderTraversal_sink(curNode->rightChild);
            delete curNode;
            return;
        }
    };
    assert(mergeNodes.back());
    postOrderTraversal_sink(mergeNodes.back());
    mergeNodes.clear();
}

void MMM::buildTree()
{
    MMMNode *root = MMMmain(sinks, 0);
    assert(root = mergeNodes.back());
}

int partition(vector<double> &L, int l, int r)
{
    int i, num = l;
    for (i = l + 1; i <= r; i++)
    {
        if (double_less(L[i], L[l]))
        {
            std::swap(L[i], L[++num]);
        }
    }
    std::swap(L[l], L[num]);
    return num;
}

double getMedian(vector<double> &L, int l, int r)
{
    int mid = (l + r) / 2;
    while (1)
    {
        int pos = partition(L, l, r);
        if (pos == mid)
            break;
        else if (pos > mid)
            r = pos - 1;
        else
            l = pos + 1;
    }
    return L[mid];
}

// VECTOR_2D MMM::calculateArborealGradientFor2PinNet_WA(MMMNode *lowerLevelNode, MMMNode *higherLevelNode)
// { // need double check and test to see if this works as I want
//     Net pseudo2PinNet = Net(0);

//     Module pseudoModuleForLowerLevelNode = Module(0, "lower", 0.0, 0.0, false, false);
//     Module pseudoModuleForHigherLevelNode = Module(1, "higher", 0.0, 0.0, false, false);

//     pseudoModuleForLowerLevelNode.setCenter_2D_pseudo(lowerLevelNode->x, lowerLevelNode->y, 0.0);
//     pseudoModuleForHigherLevelNode.setCenter_2D_pseudo(higherLevelNode->x, higherLevelNode->y, 0.0);

//     Pin pseudoPinForLowerLevelNode = Pin(&pseudoModuleForLowerLevelNode, &pseudo2PinNet, 0.0, 0.0);
//     Pin pseudoPinForHigherLevelNode = Pin(&pseudoModuleForHigherLevelNode, &pseudo2PinNet, 0.0, 0.0);

//     pseudoModuleForLowerLevelNode.addPin(&pseudoPinForLowerLevelNode);
//     pseudoModuleForHigherLevelNode.addPin(&pseudoPinForHigherLevelNode);

//     pseudo2PinNet.addPin(&pseudoPinForLowerLevelNode);
//     pseudo2PinNet.addPin(&pseudoPinForHigherLevelNode);

//     pseudo2PinNet.calcBoundPin();
//     double WAwirelength = pseudo2PinNet.calcWirelengthWA_2D(invertedGamma);
//     VECTOR_2D gradient;
//     gradient = pseudo2PinNet.getWirelengthGradientWA_2D(invertedGamma, &pseudoPinForLowerLevelNode);

//     // double levelWeight = calculateLevelWeight(lowerLevelNode->level, globalDensityOverflow, totalLevelCount);
//     // // double levelWeight = 1; //?this works better for MMM?
//     // gradient.x *= levelWeight;
//     // gradient.y *= levelWeight;
//     return gradient; // todo : times level weight!
// }

// double MMM::calculateLevelWeight(int level)
// {
//     // see equation 8 and 9 in the paper "clock aware low power placement"
//     if (!(level > 0))
//     {
//         cout << "wrong level: " << level << endl;
//     }
//     assert(level > 0);
//     double p = 10.35;
//     double q = -1.68;
//     double sigma;
//     sigma = p - (1 / fastExp(globalDensityOverflow + q)); //? potential DIV 0 error here?
//     sigma = min(sigma, 5.0);
//     sigma = max(sigma, 0.1);
//     double li = 1.8 / double(level); //? this is not mentioned in the paper, what should we do about this?
//     double levelWeight;
//     levelWeight = (1 / (sqrt(2.0 * PI) * sigma)) * fastExp((-1.0) * ((li * li) / (2.0 * sigma * sigma)));
//     return levelWeight;
// }