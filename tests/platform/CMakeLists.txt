cmake_minimum_required(VERSION 3.21)
project(WindowsPlatformTests LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The Windows platform module tests require Windows.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Network Test)
qt_standard_project_setup(REQUIRES 6.11)

qt_add_executable(tst_windows_platform_controller
    tst_windows_platform_controller.cpp
    ../../src/platform/windows/windows_platform_controller.h
    ../../src/platform/windows/windows_platform_controller.cpp
)
target_link_libraries(tst_windows_platform_controller PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Test user32 shell32 advapi32
)
target_compile_definitions(tst_windows_platform_controller PRIVATE CLIPBOARDHISTORY_TESTING)
include(CTest)
add_test(NAME windows_platform_controller COMMAND tst_windows_platform_controller)
