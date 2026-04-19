cmake_minimum_required(VERSION 3.20)

project(onebit_canvas LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(onebit_canvas INTERFACE)
target_include_directories(onebit_canvas INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(onebit_canvas_tests
    tests/test_onebit_canvas.cpp
)

target_link_libraries(onebit_canvas_tests PRIVATE onebit_canvas)
target_include_directories(onebit_canvas_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

enable_testing()
add_test(NAME onebit_canvas_tests COMMAND onebit_canvas_tests)

set(ONEBIT_CANVAS_DEFAULT_CAIRO_PREFIX
    "C:/Users/maggc/workspace/kicad-source-mirror/build/msvc-win64-release/vcpkg_installed/x64-windows")
set(ONEBIT_CANVAS_CAIRO_PREFIX "${ONEBIT_CANVAS_DEFAULT_CAIRO_PREFIX}" CACHE PATH
    "Prefix containing cairo headers, libs, and runtime DLLs for pixel-compare tests")

if(EXISTS "${ONEBIT_CANVAS_CAIRO_PREFIX}/include/cairo/cairo.h" AND
   EXISTS "${ONEBIT_CANVAS_CAIRO_PREFIX}/lib/cairo.lib")
    add_executable(onebit_canvas_cairo_compare_tests
        tests/test_cairo_compare.cpp
    )

    target_link_libraries(onebit_canvas_cairo_compare_tests PRIVATE onebit_canvas)
    target_include_directories(onebit_canvas_cairo_compare_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${ONEBIT_CANVAS_CAIRO_PREFIX}/include/cairo
    )
    target_link_libraries(onebit_canvas_cairo_compare_tests PRIVATE
        ${ONEBIT_CANVAS_CAIRO_PREFIX}/lib/cairo.lib
    )

    add_test(NAME onebit_canvas_cairo_compare_tests COMMAND onebit_canvas_cairo_compare_tests)
    set_tests_properties(onebit_canvas_cairo_compare_tests PROPERTIES
        ENVIRONMENT "PATH=${ONEBIT_CANVAS_CAIRO_PREFIX}/bin;$ENV{PATH}")
endif()
