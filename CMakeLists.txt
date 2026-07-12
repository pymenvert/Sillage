cmake_minimum_required(VERSION 3.24)

project(sillage
    VERSION 0.1.0
    DESCRIPTION "Multi-LiDAR people tracking engine for immersive spaces"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

add_subdirectory(engine)
