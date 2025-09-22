#include <cstdio>
#include <vector>
#include <ranges>

#include "global.h"
#include "plot.h"


int main(int argc, char* argv[])
{
    if(argc != 2)
    {
        printf("usage: <plotpath>\n");
        exit(-1);
    } 

    std::string folder = std::string(argv[1]);
    std::string name("test.bmp");
    std::tuple region = std::make_tuple(0,0,100,100);

    std::vector<std::tuple<size_t,POS_2D,POS_2D,const unsigned char*>> data;
    for(size_t i = 0; i < 20; i++)
    {
        double centerX = uniformDistribution(20,90);
        double centerY = uniformDistribution(20,90);
        double width = uniformDistribution(0,10);
        double height = uniformDistribution(0,10);
        size_t layer = (size_t) uniformDistribution(0,4);
        data.push_back(std::make_tuple( layer,
                                        POS_2D{centerX-width/2,centerY-height/2},
                                        POS_2D{centerX+width/2,centerY+height/2},
                                        Plot::Blue));
    }
    Plot::plotFigure(
        folder,
        name,
        data | std::ranges::views::transform(
            [](const auto& t){
                auto [tierId,ll,ur,color] = t;
                return make_tuple(tierId,ll.x,ll.y,ur.x,ur.y,color);
            }
        ),
        region,
        (size_t)4,
        std::make_pair(500,500)
    );
    return 0;
}