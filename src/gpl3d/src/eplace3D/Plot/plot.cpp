#include <iostream>
#include <unistd.h>
#include "plot.h"

static py::scoped_interpreter guard{};
void Plotter::drawModules(const std::vector<std::vector<double>> &modules,
                          const std::vector<std::vector<double>> &fillers,
                          double core_x, double core_y, double core_z,
                          const std::string &output_path) //
{
    try
    {
        py::module_ sys = py::module_::import("sys");

        std::string plot_path = PLOT_SCRIPT_DIR;
        sys.attr("path").attr("append")(plot_path);

        py::module_ py_draw = py::module_::import("plotConst");

        py_draw.attr("draw_3d_modules")(modules, fillers, core_x, core_y, core_z, output_path);
    }
    catch (py::error_already_set &e)
    {
        std::cerr << "Python 执行错误: " << e.what() << std::endl;
    }
}
