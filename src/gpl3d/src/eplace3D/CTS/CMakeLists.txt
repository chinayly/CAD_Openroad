add_library(CTS ctsdb.cpp mmm.cpp DME.cpp CTSobjects.cpp topology.cpp)

target_include_directories(CTS INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(CTS PRIVATE PlaceCommon PlaceDB)