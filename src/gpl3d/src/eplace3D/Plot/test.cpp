#include <iostream>
#include "plot.h"

int main()
{
    std::cout << "=== 测试 Plotter: C++ 调用 Python 绘图 ===" << std::endl;

    // 生成测试数据：10 个随机 module
    std::vector<std::vector<double>> test_modules = {
        {1, 1, 1, 2, 2, 2}, // 模块1 (x, y, z, width, height, depth)
        {3, 3, 2, 2, 2, 3}, // 模块2
        {5, 5, 3, 3, 3, 2}, // 模块3
        {2, 6, 1, 2, 2, 4}, // 模块4
        {7, 2, 5, 3, 3, 3}, // 模块5
        {4, 7, 4, 2, 2, 2}, // 模块6
        {6, 6, 2, 2, 2, 2}, // 模块7
        {8, 8, 3, 3, 3, 3}, // 模块8
        {9, 1, 2, 2, 2, 2}, // 模块9
        {1, 9, 5, 3, 3, 2}  // 模块10
    };

    // 设置 coreCube 的尺寸 (假设总布局空间为 10x10x10)
    double core_x = 10.0;
    double core_y = 10.0;
    double core_z = 10.0;

    // 调用 Plotter 进行测试
    std::string output_dir = "../result";
    Plotter::drawModules(test_modules, core_x, core_y, core_z, output_dir);

    std::cout << "=== 测试完成 ===" << std::endl;
    return 0;
}
