#include <cmath>

#include "plot.h"
#include <cassert>

using namespace std;
namespace Plot{

    vector<CImg<unsigned char>> createSubFigs(pair<size_t,size_t> figSize, size_t num)
    {
        vector<CImg<unsigned char>> subFigs;
        for(auto i = 0; i < num; i++)
        {
            subFigs.push_back(CImg<unsigned char>(figSize.first,figSize.second,1,3,255));
        }
        return subFigs;
    }

    CImg<unsigned char> mergeSubFigs(const vector<CImg<unsigned char>>& subFigs)
    {
        size_t subFigNum = subFigs.size();
        assert(subFigNum > 0);
        size_t subFigWidth = subFigs[0].width();
        size_t subFigHeight = subFigs[0].height();
        size_t numRows = (size_t)floor(sqrt(subFigNum));
        size_t numCols = (subFigNum - 1) / numRows + 1;
        CImg<unsigned char> mainFig(
            numRows * subFigWidth + (numRows + 1) * spacing,
            numCols * subFigHeight + (numCols + 1) * spacing,
            1,3,255
        );
        for(size_t i = 0; i < numRows; i++)
        {
            for(size_t j = 0; j < numCols; j++)
            {
                size_t figId = i * numCols + j;
                if(figId >= subFigNum)
                {
                    return mainFig;
                }
                string figLabel = "Fig " + to_string(figId);
                size_t startX = spacing + i * (subFigWidth + spacing);
                size_t startY = spacing + j * (subFigHeight + spacing);
                mainFig.draw_image(startX, startY, subFigs[figId]);
                mainFig.draw_text(startX + subFigWidth / 10, startY + subFigHeight / 10, figLabel.c_str(), Black, NULL, 1, textSize);
            }
        } 
        return mainFig;
    }

    CImg<unsigned char> mergeSubFigsVertical(const vector<CImg<unsigned char>>& subFigs)
{
    size_t subFigNum = subFigs.size();
    assert(subFigNum > 0);
    size_t subFigWidth = subFigs[0].width();
    size_t subFigHeight = subFigs[0].height();

    // 一列排列：主图宽度=单个子图宽度+两侧间隔，高度=所有子图高度+间隔
    CImg<unsigned char> mainFig(
        subFigWidth + 2 * spacing,
        subFigNum * subFigHeight + (subFigNum + 1) * spacing,
        1, 3, 255
    );

    for (size_t i = 0; i < subFigNum; ++i)
    {
        string figLabel = "Tier " + to_string(subFigNum - 1 - i);

        size_t startX = spacing;
        size_t startY = spacing + i * (subFigHeight + spacing);
        mainFig.draw_image(startX, startY, subFigs[subFigNum - 1 - i]); // 从下往上绘制
        mainFig.draw_text(startX + subFigWidth / 8, startY + subFigHeight / 8, figLabel.c_str(), Black, NULL, 1, textSize);
    }
    return mainFig;
}
}