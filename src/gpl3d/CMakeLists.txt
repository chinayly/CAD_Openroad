cmake_minimum_required(VERSION 3.16)

# 同时启用 C 和 C++
project(gpl3d LANGUAGES C CXX)

include("openroad")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1) 先把 ePlace3D 各子目录加进来，但不自动构建里面的测试可执行
add_subdirectory(src/eplace3D/PlaceCommon   )
add_subdirectory(src/eplace3D/PlaceDB       )
add_subdirectory(src/eplace3D/Placer        )
add_subdirectory(src/eplace3D/Optimization  )
add_subdirectory(src/eplace3D/FFT           )
add_subdirectory(src/eplace3D/Plot          )
#add_subdirectory(src/eplace3D/Eigen        )
add_subdirectory(src/eplace3D/CTS           )
<<<<<<< HEAD
=======
add_subdirectory(src/td)
>>>>>>> daaadde10f (wrong timing)


# 3) 我们自己的 gpl3d 核心库
add_library(gpl3d_lib
  src/odbStats.cpp
  src/placer3d.cpp
  src/parser_odb.cpp
)

# 4) 头文件路径（把 ePlace3D 里需要暴露给我们代码的目录 + 超图的 include 补齐）
target_include_directories(gpl3d_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eplace3D
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eplace3D/PlaceCommon
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eplace3D/PlaceDB
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eplace3D/Placer
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eplace3D/Optimization
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eplace3D/FFT
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eplace3D/Plot
    #${CMAKE_CURRENT_SOURCE_DIR}/src/eplace3D/Eigen
    ${CMAKE_CURRENT_SOURCE_DIR}/src/eplace3D/CTS
)

# 5) 依赖库（OpenROAD 提供的 odb/sta 等 + ePlace3D 子库）
find_package(Boost 1.80 REQUIRED)  # 你的工程之前用的是 Boost::boost 接口

target_link_libraries(gpl3d_lib
  PUBLIC
    odb
    OpenSTA
    utl_lib
    dbSta_lib
    par
    Boost::boost
    Placer
    PlaceDB
    PlaceCommon
    FFT
    Plot
    #Eigen
    CTS
    gpl3d_td
)

# 6) 生成 SWIG/Tcl 插件
swig_lib(
  NAME      gpl3d
  NAMESPACE gpl3d
  I_FILE    src/placer3d.i
  SCRIPTS   src/placer3d.tcl
)

target_sources(gpl3d PRIVATE src/MakePlacer3d.cpp)
target_include_directories(gpl3d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gpl3d PRIVATE gpl3d_lib)
