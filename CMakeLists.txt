cmake_minimum_required(VERSION 3.16)
project(CalcTabdd VERSION 0.2.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(CTest)
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets)

add_library(calctabdd_core STATIC
    src/expression_engine.cpp src/expression_engine.h
    src/calculation_history.cpp src/calculation_history.h
    src/calculation_export.cpp src/calculation_export.h
    src/calculation_catalog.cpp src/calculation_catalog.h
    src/calculator_help.cpp src/calculator_help.h
    src/formula_completion.cpp src/formula_completion.h
    src/calculator_page.cpp src/calculator_page.h
    src/plugin_controller.cpp src/plugin_controller.h src/ndd_plugin_api.h)
target_include_directories(calctabdd_core PUBLIC src)
target_link_libraries(calctabdd_core PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets)
target_compile_definitions(calctabdd_core PUBLIC CALCTABDD_VERSION="${PROJECT_VERSION}")
add_library(calctabdd SHARED src/plugin_exports.cpp)
target_link_libraries(calctabdd PRIVATE calctabdd_core)
set_target_properties(calctabdd PROPERTIES
    OUTPUT_NAME calctabdd
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin")
foreach(target calctabdd_core calctabdd)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W4)
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
install(TARGETS calctabdd RUNTIME DESTINATION plugin LIBRARY DESTINATION plugin)

if(BUILD_TESTING)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Test)
    function(calc_test name)
        add_executable(${name} tests/${name}.cpp)
        target_link_libraries(${name} PRIVATE calctabdd_core Qt5::Test)
        if(MSVC)
            target_compile_options(${name} PRIVATE /utf-8 /W4)
        endif()
        add_test(NAME ${name} COMMAND ${name} -platform offscreen
            -o ${name}.txt,txt -o -,txt)
        set_tests_properties(${name} PROPERTIES TIMEOUT 120)
        if(WIN32)
            file(TO_CMAKE_PATH "$ENV{SystemRoot}/Fonts" font_directory)
            set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_FONTDIR=${font_directory}")
        endif()
    endfunction()
    calc_test(engine_tests)
    calc_test(page_tests)
    set(CALCTABDD_HOST_SOURCE_DIR "" CACHE PATH "Pinned notepad-- source; required for native integration tests")
    set(CALCTABDD_QSCINTILLA_LIBRARY "" CACHE FILEPATH "Optional prebuilt host QScintilla static library")
    if(CALCTABDD_HOST_SOURCE_DIR)
        include(cmake/HostQScintilla.cmake)
        calc_test(plugin_tests)
        target_link_libraries(plugin_tests PRIVATE host_qscintilla)
        target_include_directories(plugin_tests PRIVATE
            "${CALCTABDD_HOST_SOURCE_DIR}/src/include"
            "${CALCTABDD_HOST_SOURCE_DIR}/src")
        target_compile_definitions(plugin_tests PRIVATE CALCTABDD_PLUGIN_PATH="$<TARGET_FILE:calctabdd>")
        add_dependencies(plugin_tests calctabdd)
    else()
        message(STATUS "Native QScintilla integration tests NOT ENABLED; set CALCTABDD_HOST_SOURCE_DIR")
    endif()
endif()
