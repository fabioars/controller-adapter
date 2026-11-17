cmake_minimum_required(VERSION 3.16)
set(CMAKE_C_STANDARD 11)

set(BLUEPAD32_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bluepad32)
if(NOT EXISTS ${BLUEPAD32_ROOT}/external/btstack/port/esp32)
    message(FATAL_ERROR "Third-party code missing. Run ./scripts/setup.sh (or make setup) first.")
endif()
set(ENV{BTSTACK_ROOT} ${BLUEPAD32_ROOT}/external/btstack)
set(EXTRA_COMPONENT_DIRS ${BLUEPAD32_ROOT}/src/components ${CMAKE_CURRENT_SOURCE_DIR}/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include($ENV{BTSTACK_ROOT}/port/esp32/components/btstack/cmake/BTstack_project.cmake)

project(ps3_xbox_adapter)
