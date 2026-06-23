cmake_minimum_required(VERSION 3.21)
project(ads1299_studio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort PrintSupport Test)

# Vendored third-party
add_library(qcustomplot STATIC third_party/qcustomplot/qcustomplot.cpp)
target_include_directories(qcustomplot PUBLIC third_party/qcustomplot)
target_link_libraries(qcustomplot PUBLIC Qt6::Widgets Qt6::PrintSupport)

add_library(edflib STATIC third_party/edflib/edflib.c)
target_include_directories(edflib PUBLIC third_party/edflib)

add_library(kissfft STATIC third_party/kissfft/kiss_fft.c third_party/kissfft/kiss_fftr.c)
target_include_directories(kissfft PUBLIC third_party/kissfft)

# Core library (grows over the plan)
# Use GLOB; if no sources yet, define as INTERFACE to avoid "no sources" error.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/app/*.cpp)
if(CORE_SOURCES)
    add_library(studio_core STATIC ${CORE_SOURCES})
    target_include_directories(studio_core PUBLIC src)
    target_link_libraries(studio_core PUBLIC Qt6::Core Qt6::SerialPort edflib kissfft)
else()
    # No core sources yet — create a header-only INTERFACE target so downstream
    # targets can link against studio_core without error. Remove this branch
    # once src/core/ or src/app/ gains its first .cpp file (Task 2+).
    add_library(studio_core INTERFACE)
    target_include_directories(studio_core INTERFACE src)
    target_link_libraries(studio_core INTERFACE Qt6::Core Qt6::SerialPort edflib kissfft)
endif()

# UI sources
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp)
qt_add_executable(ads1299-studio src/main.cpp ${UI_SOURCES} resources/resources.qrc)
target_link_libraries(ads1299-studio PRIVATE studio_core qcustomplot Qt6::Widgets)

enable_testing()
add_subdirectory(tests)
