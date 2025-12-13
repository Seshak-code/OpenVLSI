cmake_minimum_required(VERSION 3.16)
project(HelloWorld VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt packages
find_package(Qt6 COMPONENTS Core Widgets QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 COMPONENTS Core Widgets REQUIRED)
endif()

# Include directories
include_directories(/opt/homebrew/include/eigen3)

set(CMAKE_AUTOMOC ON)

# Create executable
add_executable(${PROJECT_NAME} main.cpp)

# Target include directories for Qt and Eigen
target_include_directories(${PROJECT_NAME} PRIVATE 
    /opt/homebrew/include/eigen3
)

# Link Qt libraries
if(Qt6_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt5::Core Qt5::Widgets)
endif()
