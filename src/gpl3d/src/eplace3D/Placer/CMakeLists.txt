find_package(OpenMP REQUIRED)

add_library(Placer spaceplacer.cpp tierplacer.cpp mesh.cpp)
target_link_libraries(
    Placer PUBLIC PlaceDB PlaceCommon Optimization FFT Plot CTS OpenMP::OpenMP_CXX gpl3d_td
)
target_include_directories(Placer
  PUBLIC
  /home/TritonPart/TD_3D/src/gpl3d/include/td
    
)

# add_executable(MeshTest mesh_test.cpp)
# target_link_libraries(MeshTest PRIVATE Placer)

# add_executable(SpacePlacerTest spaceplacer_test.cpp)
# target_link_libraries(SpacePlacerTest PRIVATE Placer Parser)

# add_executable(PlacerTest placer_test.cpp)
# target_link_libraries(PlacerTest PRIVATE Placer Parser)
