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

include(CTest)

option(CLIPBOARDHISTORY_BUILD_BENCHMARKS "Build isolated synthetic performance benchmarks" OFF)
if(CLIPBOARDHISTORY_BUILD_BENCHMARKS)
    qt_add_executable(clipboard_history_perf
        benchmarks/performance_runner.cpp
        src/core/history/history_service.h
        src/core/history/history_service.cpp
        src/core/history/history_task_queue.h
        src/core/history/history_task_queue.cpp
        src/core/history/history_worker.h
        src/core/history/history_worker.cpp
        src/core/history/history_repository.h
        src/core/history/history_repository.cpp
        src/core/history/image_store.h
        src/core/history/image_store.cpp
        src/core/import/legacy_importer.h
        src/core/import/legacy_importer.cpp
    )
    target_link_libraries(clipboard_history_perf PRIVATE Qt6::Core Qt6::Gui Qt6::Sql)
    target_compile_definitions(clipboard_history_perf PRIVATE
        CLIPBOARDHISTORY_BUILD_TYPE=\"${CMAKE_BUILD_TYPE}\"
    )
    if(WIN32)
        target_link_libraries(clipboard_history_perf PRIVATE Psapi)
    endif()
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Test)

    qt_add_executable(tst_history_repository
        tests/history/tst_history_repository.cpp
        src/core/history/history_repository.h
        src/core/history/history_repository.cpp
    )
    target_link_libraries(tst_history_repository PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
    add_test(NAME history_repository COMMAND tst_history_repository)

    qt_add_executable(tst_image_store
        tests/history/tst_image_store.cpp
        src/core/history/image_store.h
        src/core/history/image_store.cpp
    )
    target_link_libraries(tst_image_store PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME image_store COMMAND tst_image_store)

    qt_add_executable(tst_history_service
        tests/history/tst_history_service.cpp
        src/core/history/history_service.h
        src/core/history/history_service.cpp
        src/core/history/history_repository.h
        src/core/history/history_repository.cpp
        src/core/history/image_store.h
        src/core/history/image_store.cpp
        src/core/import/legacy_importer.h
        src/core/import/legacy_importer.cpp
    )
    target_link_libraries(tst_history_service PRIVATE Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test)
    add_test(NAME history_service COMMAND tst_history_service)

    qt_add_executable(tst_history_task_queue
        tests/history/tst_history_task_queue.cpp
        src/core/history/history_task_queue.h
        src/core/history/history_task_queue.cpp
        src/core/history/history_worker.h
        src/core/history/history_worker.cpp
        src/core/history/history_service.h
        src/core/history/history_service.cpp
        src/core/history/history_repository.h
        src/core/history/history_repository.cpp
        src/core/history/image_store.h
        src/core/history/image_store.cpp
        src/core/import/legacy_importer.h
        src/core/import/legacy_importer.cpp
    )
    target_link_libraries(tst_history_task_queue PRIVATE Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test)
    add_test(NAME history_task_queue COMMAND tst_history_task_queue)

    qt_add_executable(tst_history_model
        tests/history/tst_history_model.cpp
        src/core/history/history_model.h
        src/core/history/history_model.cpp
        src/core/history/history_task_queue.h
        src/core/history/history_task_queue.cpp
        src/core/history/history_worker.h
        src/core/history/history_worker.cpp
        src/core/history/history_service.h
        src/core/history/history_service.cpp
        src/core/history/history_repository.h
        src/core/history/history_repository.cpp
        src/core/history/image_store.h
        src/core/history/image_store.cpp
        src/core/import/legacy_importer.h
        src/core/import/legacy_importer.cpp
    )
    target_link_libraries(tst_history_model PRIVATE Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test)
    add_test(NAME history_model COMMAND tst_history_model)

    get_target_property(model_test_sources tst_history_model SOURCES)
    list(REMOVE_ITEM model_test_sources tests/history/tst_history_model.cpp)
    qt_add_executable(tst_qml_acceptance
        tests/acceptance/tst_qml_acceptance.cpp
        ${model_test_sources}
        resources/app_icons.qrc
    )
    set_target_properties(tst_qml_acceptance PROPERTIES AUTORCC ON)
    qt_add_qml_module(tst_qml_acceptance
        URI ClipboardHistoryAcceptance
        VERSION 1.0
        QML_FILES src/ui/Main.qml src/ui/ThemePalette.js
    )
    target_link_libraries(tst_qml_acceptance PRIVATE
        Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test Qt6::Quick
        Qt6::QuickControls2 Qt6::QuickDialogs2)
    target_compile_definitions(tst_qml_acceptance PRIVATE
        CLIPBOARDHISTORY_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        CLIPBOARDHISTORY_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
    if(WIN32)
        target_link_libraries(tst_qml_acceptance PRIVATE Psapi)
    endif()
    add_test(NAME qml_acceptance COMMAND tst_qml_acceptance)

    qt_add_executable(tst_clipboard_capture
        tests/history/tst_clipboard_capture.cpp
        src/core/history/clipboard_capture.h
        src/core/history/clipboard_capture.cpp
        src/core/history/history_task_queue.h
        src/core/history/history_task_queue.cpp
        src/core/history/history_worker.h
        src/core/history/history_worker.cpp
        src/core/history/history_service.h
        src/core/history/history_service.cpp
        src/core/history/history_repository.h
        src/core/history/history_repository.cpp
        src/core/history/image_store.h
        src/core/history/image_store.cpp
        src/core/import/legacy_importer.h
        src/core/import/legacy_importer.cpp
    )
    target_link_libraries(tst_clipboard_capture PRIVATE Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test)
    add_test(NAME clipboard_capture COMMAND tst_clipboard_capture)

    qt_add_executable(tst_app_settings
        tests/settings/tst_app_settings.cpp
        src/core/settings/app_paths.h
        src/core/settings/app_paths.cpp
        src/core/settings/app_settings.h
        src/core/settings/app_settings.cpp
    )
    target_link_libraries(tst_app_settings PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME app_settings COMMAND tst_app_settings)

    qt_add_executable(tst_privacy_logger
        tests/logging/tst_privacy_logger.cpp
        src/core/logging/PrivacyLogger.cpp
        src/core/settings/app_paths.h
        src/core/settings/app_paths.cpp
    )
    target_link_libraries(tst_privacy_logger PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME privacy_logger COMMAND tst_privacy_logger)

    qt_add_executable(tst_legacy_importer
        tests/import/tst_legacy_importer.cpp
        src/core/import/legacy_importer.h
        src/core/import/legacy_importer.cpp
        src/core/history/history_service.h
        src/core/history/history_service.cpp
        src/core/history/history_task_queue.h
        src/core/history/history_task_queue.cpp
        src/core/history/history_worker.h
        src/core/history/history_worker.cpp
        src/core/history/history_repository.h
        src/core/history/history_repository.cpp
        src/core/history/image_store.h
        src/core/history/image_store.cpp
    )
    target_link_libraries(tst_legacy_importer PRIVATE Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test)
    add_test(NAME legacy_importer COMMAND tst_legacy_importer)

    if(WIN32)
        qt_add_executable(tst_windows_platform_controller
            tests/platform/tst_windows_platform_controller.cpp
            src/platform/windows/windows_platform_controller.h
            src/platform/windows/windows_platform_controller.cpp
        )
        target_include_directories(tst_windows_platform_controller PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_link_libraries(tst_windows_platform_controller PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test user32 shell32 advapi32)
        target_compile_definitions(tst_windows_platform_controller PRIVATE CLIPBOARDHISTORY_TESTING)
        add_test(NAME windows_platform_controller COMMAND tst_windows_platform_controller)
    endif()

    if(WIN32)
        get_property(runtime_tests DIRECTORY PROPERTY TESTS)
        get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(test_path "$<TARGET_FILE_DIR:Qt6::Core>;${compiler_bin};$ENV{PATH}")
        string(REPLACE ";" "\\;" test_path "${test_path}")
        set_tests_properties(${runtime_tests} PROPERTIES ENVIRONMENT "PATH=${test_path}")
    endif()
endif()
