add_library(Parser parser.cpp)

target_link_libraries(Parser PUBLIC PlaceDB PlaceCommon)
target_include_directories(Parser INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(ParserTest parser_test.cpp)
target_link_libraries(ParserTest PRIVATE Parser)

# add_executable(
#     ParserTest 
#     parser_test.cpp
#     parser.cpp
#     ${PROJECT_SOURCE_DIR}/PlaceCommon/arghandler.cpp
#     ${PROJECT_SOURCE_DIR}/PlaceCommon/objects.cpp
#     ${PROJECT_SOURCE_DIR}/PlaceDB/placedb.cpp

# )
# INCLUDE_DIRECTORIES (
# 	${PROJECT_SOURCE_DIR}/Parser
# 	${PROJECT_SOURCE_DIR}/PlaceCommon
# 	${PROJECT_SOURCE_DIR}/PlaceDB
# )