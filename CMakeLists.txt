cmake_minimum_required(VERSION 3.16)
project(HelloWorld VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform-aware default Qt prefix hints (can be overridden by CMAKE_PREFIX_PATH or Qt6_DIR)
if(NOT DEFINED Qt6_DIR)
    if(NOT DEFINED CMAKE_PREFIX_PATH)
        if(APPLE)
            # Prioritize qtbase (core) then full qt to match Homebrew's linked plugins
            list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qtbase/lib/cmake" "/opt/homebrew/opt/qt/lib/cmake" "/usr/local/opt/qt/lib/cmake")
        elseif(WIN32)
            # On Windows, try to detect a Qt installation under C:/Qt and append likely cmake folders.
            set(_qt_root "C:/Qt")
            if(EXISTS "${_qt_root}")
                file(GLOB QT_ROOT_DIRS LIST_DIRECTORIES true "${_qt_root}/*")
                foreach(_d IN LISTS QT_ROOT_DIRS)
                    if(IS_DIRECTORY "${_d}")
                        foreach(_sub IN ITEMS "" "/msvc2019_64" "/msvc2022_64" "/msvc2017_64" "/mingw81_64" "/mingw64")
                            set(_cmake_dir "${_d}${_sub}/lib/cmake")
                            if(EXISTS "${_cmake_dir}")
                                list(APPEND CMAKE_PREFIX_PATH "${_cmake_dir}")
                                message(STATUS "Appending Qt candidate to CMAKE_PREFIX_PATH: ${_cmake_dir}")
                            endif()
                        endforeach()
                    endif()
                endforeach()
            else()
                message(STATUS "Qt root ${_qt_root} not found; please set Qt6_DIR or CMAKE_PREFIX_PATH to your Qt installation.")
            endif()
        endif()
    endif()
endif()

# If Qt6_DIR still not defined, try to locate Qt6CoreConfig.cmake under C:/Qt and set Qt6_DIR
if(WIN32 AND NOT DEFINED Qt6_DIR)
    set(_found_qt_dir "")
    # common locations: C:/Qt/<version>/<compiler>/lib/cmake/Qt6Core
    file(GLOB_RECURSE _qt_core_configs LIST_DIRECTORIES false "C:/Qt/*/lib/cmake/Qt6Core/Qt6CoreConfig.cmake" "C:/Qt/*/*/lib/cmake/Qt6Core/Qt6CoreConfig.cmake")
    if(_qt_core_configs)
        list(GET _qt_core_configs 0 _first_qtconfig)
        get_filename_component(_found_qt_dir "${_first_qtconfig}" DIRECTORY)
        set(Qt6_DIR "${_found_qt_dir}")
        message(STATUS "Auto-detected Qt6_DIR = ${Qt6_DIR}")
    else()
        message(STATUS "No Qt6Config found automatically under C:/Qt. You can set -DQt6_DIR=C:/Qt/<version>/<compiler>/lib/cmake/Qt6Core")
    endif()
endif()

# If the user has a known Qt installation path, prefer that (useful when automatic detection fails)
if(WIN32 AND NOT DEFINED Qt6_DIR)
    # Use the user's provided Qt path unconditionally when Qt6_DIR is not set.
    set(Qt6_DIR "C:/Qt/6.10.1/msvc2022_64/lib/cmake/Qt6Core")
    message(STATUS "Using explicit Qt6_DIR: ${Qt6_DIR}")
endif()

# Allow user to pass Qt6_DIR or CMAKE_PREFIX_PATH on command line or via CMake presets
if(DEFINED Qt6_DIR)
    message(STATUS "Using Qt6_DIR hint: ${Qt6_DIR}")
endif()
message(STATUS "CMAKE_PREFIX_PATH = ${CMAKE_PREFIX_PATH}")

# Find Qt6 (required)
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)
message(STATUS "Found Qt6: ${Qt6_DIR}")

# Find OpenMP (required for CPU multi-threading)
# On macOS with Apple Clang, we need to point to Homebrew's libomp
if(APPLE)
    # First attempt: check if it's already in the path or has a root hint
    find_package(OpenMP)
    
    # If not found, try the common Homebrew location
    if(NOT OpenMP_FOUND)
        set(OpenMP_ROOT "/opt/homebrew/opt/libomp")
        find_package(OpenMP REQUIRED)
    endif()
else()
    find_package(OpenMP REQUIRED)
endif()

if(OpenMP_CXX_FOUND)
    message(STATUS "Found OpenMP: ${OpenMP_CXX_FLAGS}")
endif()

# Enable CMake automoc/autouic/autorcc conveniences
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# NOTE: Eigen3 dependency removed. The project uses an internal linear algebra library.

# Create executable
add_executable(${PROJECT_NAME} MACOSX_BUNDLE main.cpp)

# Link Qt and OpenMP libraries
target_link_libraries(${PROJECT_NAME} PRIVATE 
    Qt6::Core 
    Qt6::Widgets
    OpenMP::OpenMP_CXX
)

# macOS specific: Ensure system frameworks like AGL are found
if(APPLE)
    # Search for the AGL stub specifically to satisfy legacy Qt requirements
    find_file(AGL_TBD NAMES AGL.tbd PATHS 
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/AGL.framework"
        "/Library/Developer/CommandLineTools/SDKs/MacOSX14.4.sdk/System/Library/Frameworks/AGL.framework"
        "/Library/Developer/CommandLineTools/SDKs/MacOSX13.3.sdk/System/Library/Frameworks/AGL.framework"
        "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/AGL.framework"
    )
    
    if(AGL_TBD)
        message(STATUS "Found AGL stub: ${AGL_TBD}")
        # Get the directory containing AGL.framework
        get_filename_component(AGL_FRAMEWORK_DIR "${AGL_TBD}" DIRECTORY)
        get_filename_component(AGL_SDK_FRAMEWORKS_PATH "${AGL_FRAMEWORK_DIR}" DIRECTORY)
        
        # Link the stub and add the search path for the framework
        target_link_libraries(${PROJECT_NAME} PRIVATE ${AGL_TBD})
        target_link_options(${PROJECT_NAME} PRIVATE "-F${AGL_SDK_FRAMEWORKS_PATH}")
    else()
        # Fallback to searching as framework if stub is not found
        find_library(AGL_LIBRARY AGL)
        if(AGL_LIBRARY)
            target_link_libraries(${PROJECT_NAME} PRIVATE ${AGL_LIBRARY})
        endif()
    endif()
endif()

# Install rule
install(TARGETS ${PROJECT_NAME} 
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
