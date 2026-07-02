cmake_minimum_required(VERSION 3.21)
project(ads1299_studio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort PrintSupport Test OpenGLWidgets OpenGL)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# Vendored third-party
add_library(qcustomplot STATIC third_party/qcustomplot/qcustomplot.cpp)
target_include_directories(qcustomplot PUBLIC third_party/qcustomplot)
target_link_libraries(qcustomplot PUBLIC Qt6::Widgets Qt6::PrintSupport)

add_library(edflib STATIC third_party/edflib/edflib.c)
target_include_directories(edflib PUBLIC third_party/edflib)
set_target_properties(edflib PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

add_library(kissfft STATIC third_party/kissfft/kiss_fft.c third_party/kissfft/kiss_fftr.c)
target_include_directories(kissfft PUBLIC third_party/kissfft)
set_target_properties(kissfft PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

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

# Wire libusb-1.0 into studio_core (PUBLIC so test targets and exe also get it)
target_include_directories(studio_core PUBLIC ${LIBUSB_INCLUDE_DIRS})
target_link_libraries(studio_core PUBLIC ${LIBUSB_LINK_LIBRARIES})
target_link_directories(studio_core PUBLIC ${LIBUSB_LIBRARY_DIRS})

# UI library — shared by the app executable and GUI tests (e.g. MainWindow
# control-gating tests), so UI classes compile once.
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp)
add_library(studio_ui STATIC ${UI_SOURCES})
target_include_directories(studio_ui PUBLIC src)
target_link_libraries(studio_ui PUBLIC studio_core qcustomplot Qt6::Widgets Qt6::OpenGLWidgets Qt6::OpenGL)

qt_add_executable(ads1299-studio src/main.cpp resources/resources.qrc)
target_link_libraries(ads1299-studio PRIVATE studio_ui)

enable_testing()
add_subdirectory(tests)
