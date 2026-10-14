cmake_minimum_required(VERSION 3.21)

project(ClipboardHistory
    VERSION 0.1.0
    DESCRIPTION "A local-first clipboard history manager"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Quick QuickControls2 QuickDialogs2 Sql)
if(WIN32)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Network)
endif()

if(WIN32 AND Qt6_VERSION VERSION_GREATER_EQUAL 6.13)
    message(FATAL_ERROR "Qt 6.12 is the last Qt release with official Windows 10 support. Use a Qt 6.11 or 6.12 kit for Windows 10 builds.")
endif()

qt_standard_project_setup(REQUIRES 6.11)

if(WIN32)
    qt_add_executable(ClipboardHistory WIN32
        src/app/main.cpp
    )
else()
    qt_add_executable(ClipboardHistory
        src/app/main.cpp
    )
endif()

qt_add_qml_module(ClipboardHistory
    URI ClipboardHistory
    VERSION 1.0
    QML_FILES
        src/ui/Main.qml
        src/ui/ThemePalette.js
)

target_sources(ClipboardHistory
    PRIVATE
        src/core/settings/app_paths.h
        src/core/settings/app_paths.cpp
        src/core/settings/app_settings.cpp
        src/core/logging/PrivacyLogger.cpp
        src/core/history/history_repository.h
        src/core/history/history_repository.cpp
        src/core/history/image_store.h
        src/core/history/image_store.cpp
        src/core/history/history_service.h
        src/core/history/history_service.cpp
        src/core/history/history_worker.h
        src/core/history/history_worker.cpp
        src/core/history/history_task_queue.h
        src/core/history/history_task_queue.cpp
        src/core/history/clipboard_capture.h
        src/core/history/clipboard_capture.cpp
        src/core/history/history_model.h
        src/core/history/history_model.cpp
        src/core/import/legacy_importer.h
        src/core/import/legacy_importer.cpp
)
target_link_libraries(ClipboardHistory
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickDialogs2
        Qt6::Sql
)
if(WIN32)
    target_sources(ClipboardHistory PRIVATE
        src/platform/windows/windows_platform_controller.h
        src/platform/windows/windows_platform_controller.cpp
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc"
        @ONLY
    )
    target_sources(ClipboardHistory PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
    target_include_directories(ClipboardHistory PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(ClipboardHistory PRIVATE Qt6::Network user32 shell32 advapi32)
endif()

target_sources(ClipboardHistory PRIVATE resources/app_icons.qrc)

add_custom_command(TARGET ClipboardHistory POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/resources/custom-theme.json"
        "-DDESTINATION_FILE=$<TARGET_FILE_DIR:ClipboardHistory>/custom-theme.json"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_theme_if_missing.cmake"
    VERBATIM
)

set_target_properties(ClipboardHistory PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    AUTORCC ON
)
