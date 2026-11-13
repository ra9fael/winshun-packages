cmake_minimum_required(VERSION 3.21)
project(ra9fael_packages VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ra9fael.packages must be built as Windows x64: plugin.json names packages.exe as runtime.program and the backends are Windows-only")
endif()

# The shared-host Qt contract (docs/plugins.md) requires MSVC, Release, and a
# dynamic CRT. The plugin must not ship its own Qt DLLs, so every Qt target comes
# from the host's installation.
if(NOT MSVC)
    message(FATAL_ERROR "the shared Qt contract requires MSVC")
endif()

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
add_compile_options(/utf-8 /W4 /permissive-)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:MultiThreadedDebugDLL>")

# ---- shared code -------------------------------------------------------------
# The pure core carries no OS headers so it also compiles for the L1 tests, which
# run on any C++17 compiler without Qt.
set(CORE_SOURCES
    src/core/identity.cpp
    src/core/version.cpp
    src/core/model.cpp
    src/core/plan.cpp
    src/core/queue.cpp
    src/core/redact.cpp
    src/core/json_lite.cpp
    src/core/cache.cpp
    src/core/theme_tokens.cpp
)

set(ADAPTER_SOURCES
    src/adapters/winget_table.cpp
    src/adapters/winget_adapter.cpp
    src/adapters/scoop_adapter.cpp
)

# ---- the plugin -------------------------------------------------------------
qt_add_executable(packages
    src/main.cpp
    src/platform/process.cpp
    src/ui/window.cpp
    src/ui/qml_window.cpp
    src/ui/models.cpp
    src/ui/theme_resolver.cpp
    ${CORE_SOURCES}
    ${ADAPTER_SOURCES}
    packages.rc
    plugin-icons.qrc
)
target_include_directories(packages PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(packages PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    advapi32 shell32 ole32
)

# plugin.json names the exe as runtime.program, so it is emitted next to the
# manifest, like the other plugins in this workspace.
set(PACKAGES_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(packages PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PACKAGES_RUNTIME_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(packages PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${PACKAGES_RUNTIME_DIR}")
endforeach()

# The QML module keeps its source-relative aliases (src/ui/qml/...), which is what
# the scene URL in qml_window.cpp points at.
set_source_files_properties(src/ui/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(packages
    URI Packages
    VERSION 1.0
    QML_FILES
        src/ui/qml/Main.qml
        src/ui/qml/Theme.qml
        src/ui/qml/RowDelegate.qml
        src/ui/qml/DetailPane.qml
        src/ui/qml/ReviewDialog.qml
        src/ui/qml/TasksPage.qml
        src/ui/qml/ThemedButton.qml
        src/ui/qml/ThemedField.qml
        src/ui/qml/ThemedCheckBox.qml
        src/ui/qml/RowMenu.qml
)

# ---- L1 tests ---------------------------------------------------------------
# Pure logic, no host, no backend. These also build with a non-MSVC compiler, so
# the same cases can run on a Linux CI job.
enable_testing()

add_executable(packages_core_tests tests/core_tests.cpp ${CORE_SOURCES})
target_include_directories(packages_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME packages_core COMMAND packages_core_tests)

add_executable(packages_theme_tests tests/theme_tokens_tests.cpp src/core/theme_tokens.cpp)
target_include_directories(packages_theme_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME packages_theme COMMAND packages_theme_tests)

# The process layer is the only place that starts a child. Its pipe handling is
# what turned a fast `winget --version` into "failed (exit 0)", so it gets a real
# child to run rather than a fixture.
add_executable(packages_process_tests tests/process_tests.cpp src/platform/process.cpp)
target_include_directories(packages_process_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME packages_process COMMAND packages_process_tests)

add_executable(packages_fixture_tests
    tests/fixture_tests.cpp
    src/platform/process.cpp
    ${CORE_SOURCES}
    ${ADAPTER_SOURCES}
)
target_include_directories(packages_fixture_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME packages_fixtures COMMAND packages_fixture_tests)
set_tests_properties(packages_fixtures PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(packages_scoop_tests
    tests/scoop_tests.cpp
    src/platform/process.cpp
    ${CORE_SOURCES}
    ${ADAPTER_SOURCES}
)
target_include_directories(packages_scoop_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME packages_scoop COMMAND packages_scoop_tests)
set_tests_properties(packages_scoop PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

foreach(test_target packages_core_tests packages_theme_tests packages_process_tests packages_fixture_tests packages_scoop_tests)
    set_target_properties(${test_target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(${test_target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/tests")
    endforeach()
endforeach()

# ---- L0 static validation ---------------------------------------------------
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME packages_static
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/validate.py --quick)
    set_tests_properties(packages_static PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# ---- development tool: render the real scene to a PNG ----------------------
# Not packaged: it exists so the delivered window can be compared with the
# confirmed mockup without a desktop session. It loads the .qml files straight
# from the source tree, so it needs no QML module of its own (two modules cannot
# share one output directory) and it always shows the current scene.
option(PACKAGES_BUILD_GRAB_TOOL "Build the offscreen window capture tool" OFF)
if(PACKAGES_BUILD_GRAB_TOOL)
    qt_add_executable(packages_grab_window
        tools/grab_window.cpp
        src/platform/process.cpp
        src/ui/window.cpp
        src/ui/models.cpp
        src/ui/theme_resolver.cpp
        ${CORE_SOURCES}
        ${ADAPTER_SOURCES}
    )
    target_include_directories(packages_grab_window PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(packages_grab_window PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 advapi32 shell32 ole32)
endif()

# Diagnostic: prints what locateScoop() resolves in this process's environment.
# Not part of the plugin; used to check the Scoop root search against a real
# installation and against a simulated elevated environment.
option(PACKAGES_BUILD_PROBE_TOOL "Build the Scoop root probe tool" OFF)
if(PACKAGES_BUILD_PROBE_TOOL)
    add_executable(packages_probe_scoop
        tools/probe_scoop.cpp
        src/platform/process.cpp
        ${ADAPTER_SOURCES}
        ${CORE_SOURCES}
    )
    target_include_directories(packages_probe_scoop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(packages_probe_scoop PRIVATE advapi32 shell32 ole32)
endif()

# The development harness: the real window with no plugin host. Not shipped and not
# packaged; it links the same controller, models, adapters and QML scene, so what it
# shows is what the plugin shows, but opening it costs one command instead of a
# build, a package, an install and a host restart.
option(PACKAGES_BUILD_DEV_TOOL "Build the development harness (opens the window unhosted)" OFF)
if(PACKAGES_BUILD_DEV_TOOL)
    qt_add_executable(packages_dev
        tools/dev_main.cpp
        tools/look_fixture.cpp
        src/platform/process.cpp
        src/ui/window.cpp
        src/ui/qml_window.cpp
        src/ui/models.cpp
        src/ui/theme_resolver.cpp
        ${CORE_SOURCES}
        ${ADAPTER_SOURCES}
    )
    target_include_directories(packages_dev PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(packages_dev PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 advapi32 shell32 ole32)
    set_target_properties(packages_dev PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    # The harness reads the scene from src/ui/qml (that is what makes --watch and
    # edit-without-rebuild work), so the path is compiled in as its default. It is
    # a development tool: a moved checkout means re-running cmake, which is exactly
    # the coupling a shipped plugin must not have and does not have.
    target_compile_definitions(packages_dev PRIVATE
        PACKAGES_DEV_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/ui/qml")
endif()
