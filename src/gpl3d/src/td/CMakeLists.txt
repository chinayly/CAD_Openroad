# src/gpl3d/src/td/CMakeLists.txt

add_library(gpl3d_td
  TimingManager.cpp
  RCTreeBuilder3D.cpp
  ParasiticsInjector.cpp
  TimingOracle.cpp
  NetWeightScheduler.cpp
)

# 公开头文件根目录：让  #include "td/xxx.h"  能被找到
target_include_directories(gpl3d_td
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src/gpl3d/include/gpl3d   # ★ 这行必须有，才能 #include "td/..."
    ${PROJECT_SOURCE_DIR}/src/gpl3d/include/td         # （可选）若你也会用 #include "gpl3d/td/..."
    ${PROJECT_SOURCE_DIR}/include                   # （可选）你工程已有的公共 include


    ${PROJECT_SOURCE_DIR}/src/eplace3D
    ${PROJECT_SOURCE_DIR}/src/eplace3D/PlaceCommon
    ${PROJECT_SOURCE_DIR}/src/eplace3D/PlaceDB
    ${PROJECT_SOURCE_DIR}/src/eplace3D/Placer
    ${PROJECT_SOURCE_DIR}/src/eplace3D/Optimization
    ${PROJECT_SOURCE_DIR}/src/eplace3D/FFT
    ${PROJECT_SOURCE_DIR}/src/eplace3D/Plot
    ${PROJECT_SOURCE_DIR}/src/eplace3D/CTS
)
# 需要 C++17（如你工程统一标准不同，可改成 20/14）
target_compile_features(gpl3d_td PUBLIC cxx_std_17)

# 依赖 OpenROAD / OpenSTA 的 targets
# 这些 target 名在你的工程里通常是：ord, dbSta, sta, utl
target_link_libraries(gpl3d_td
  PUBLIC
    OpenSTA        # ✅ 库
    dbSta_lib
    utl_lib
    par
    PlaceDB
)
# 如果你的工程是静态链接并且需要位置无关代码，可取消注释
# set_target_properties(gpl3d_td PROPERTIES POSITION_INDEPENDENT_CODE ON)
