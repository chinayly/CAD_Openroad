add_library(FFT fft.cpp fftsg.cpp fftsg2d.cpp fftsg3d.cpp)

target_include_directories(FFT INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(FFTTest fft_test.cpp)
target_link_libraries(FFTTest PRIVATE FFT)

# ADD_EXECUTABLE(
#     fft_test
#     fft_test.cpp
#     ${PROJECT_SOURCE_DIR}/FFT/fft.cpp
#     ${PROJECT_SOURCE_DIR}/FFT/fftsg2d.cpp
#     ${PROJECT_SOURCE_DIR}/FFT/fftsg3d.cpp
#     ${PROJECT_SOURCE_DIR}/FFT/fftsg.cpp
# )
# INCLUDE_DIRECTORIES (
#     ${PROJECT_SOURCE_DIR}/FFT
# )



# target_link_libraries(fft_test "-lgomp -lpthread -lm -ldl -lX11")