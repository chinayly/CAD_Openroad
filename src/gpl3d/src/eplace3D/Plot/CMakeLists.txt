add_library(Plot plottiers.cpp)
# find_package(pybind11 REQUIRED)  # 需要 pybind11
find_package(X11 REQUIRED)

# target_compile_definitions(Plot PRIVATE -DPLOT_SCRIPT_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")

# target_link_libraries(Plot PUBLIC pybind11::embed)  # 链接 pybind11
target_link_libraries(Plot PUBLIC ${X11_LIBRARIES})
target_include_directories(Plot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/CImg)
target_include_directories(Plot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# add_executable(test_plot test.cpp)  # 生成 test_plot 可执行文件
# target_link_libraries(test_plot PRIVATE Plot)  # 链接 pybind11

add_executable(PlotTierTest plottiers_test.cpp)
target_link_libraries(PlotTierTest PRIVATE Plot PlaceCommon)
