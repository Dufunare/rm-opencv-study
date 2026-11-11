cmake_minimum_required(VERSION 4.2)
project(rm_opencv_study VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Do not depend on system-wide OpenCV_DIR or manually copied OpenCV binaries.
find_package(OpenCV 4.14.0 EXACT REQUIRED COMPONENTS
    core
    imgproc
    imgcodecs
    highgui
    calib3d
)

add_executable(armor_demo src/main.cpp)
target_link_libraries(armor_demo PRIVATE ${OpenCV_LIBS})

if(MSVC)
    target_compile_options(armor_demo PRIVATE /W4 /permissive- /utf-8)
endif()

message(STATUS "Project OpenCV: ${OpenCV_VERSION}")
message(STATUS "Project compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "VS toolset: ${CMAKE_VS_PLATFORM_TOOLSET}")
message(STATUS "Windows SDK: ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
