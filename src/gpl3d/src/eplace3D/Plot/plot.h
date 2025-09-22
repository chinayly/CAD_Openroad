#ifndef PLOT_H
#define PLOT_H

#include <vector>
#include <string>
#include <cstddef>
#include <ranges>
// #include <pybind11/embed.h>
// #include <pybind11/stl.h>
// #include <pybind11/numpy.h>
// namespace py = pybind11;

// class Plotter
// {
// public:
//     static void drawModules(const std::vector<std::vector<double>> &modules,
//                             const std::vector<std::vector<double>> &fillers,
//                             double core_x, double core_y, double core_z,
//                             const std::string &output_path);
// };

#include <filesystem>
#include "CImg.h"
namespace Plot
{
    using namespace cimg_library;
    namespace fs = std::filesystem;
    
    const size_t spacing = 10;
    const float opacity = 0.7;
    const size_t textSize = 100;

    const unsigned char Blue[] = {120, 200, 255},
                        White[] = {255, 255, 255},
                        Black[] = {0, 0, 0},
                        Green[] = {0, 150, 0},
                        LightGreen[] = {144, 238, 144},
                        Orange[] = {255, 140, 0},
                        Purple[] = {128, 0, 128},
                        Red[] = {255, 0, 0};
    
    CImg<unsigned char> mergeSubFigs(const std::vector<CImg<unsigned char>>& subFigs);
    CImg<unsigned char> mergeSubFigsVertical(const std::vector<CImg<unsigned char>>& subFigs);
    std::vector<CImg<unsigned char>> createSubFigs(std::pair<size_t,size_t> figSize, size_t num);

    /*
        R is the information about the cell
        order : tierId,llx,lly,urx,ury,color
        region is the placement region
        order : llx,lly,urx,ury
    */
    template <std::ranges::range R>
        requires std::same_as<std::ranges::range_value_t<R>, std::tuple<size_t,double,double,double,double,const unsigned char*>>
    void plotFigure(
        std::string folder,
        std::string name,
        const R& rectangles,
        std::tuple<double,double,double,double> region,
        size_t tiers,
        std::pair<size_t, size_t> subFigSize
    )
    {
        // preprocess
        if(!folder.ends_with('/'))
        {
            folder += "/";
        }
        if(!fs::exists(folder))
        {
            fs::create_directories(folder);
        }
        std::string filename = folder + name;
        // Check if file exists and remove it
        if (fs::exists(filename)) {
            fs::remove(filename);
        }
        std::vector<CImg<unsigned char>>&& subFigs = createSubFigs(subFigSize,tiers);

        auto transform2FigPos = [=](double x, double y){
            const auto [regionllx,regionlly,regionurx,regionury] = region;
            const double scaleX = (regionurx - regionllx) / subFigSize.first;
            const double scaleY = (regionury - regionlly) / subFigSize.second;
            return std::make_pair((size_t)((x-regionllx) / scaleX), (size_t)((y-regionlly) / scaleY));
        };
        for(const auto& rec:rectangles)
        {
            auto [tierId,llx,lly,urx,ury,color] = rec;
            auto [llxFig,llyFig] = transform2FigPos(llx, lly);
            auto [urxFig,uryFig] = transform2FigPos(urx, ury);
            subFigs[tierId].draw_rectangle(llxFig, llyFig, urxFig, uryFig, color);
        }
        CImg<unsigned char>&& mainFig = mergeSubFigsVertical(subFigs);
        mainFig.save_bmp(filename.c_str());
    }
}

#endif
