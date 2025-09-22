add_library(PlaceDB objects.cpp placedb.cpp)

target_include_directories(PlaceDB INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PlaceDB PRIVATE PlaceCommon)