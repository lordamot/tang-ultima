cmake_minimum_required(VERSION 3.15)

include(proj.conf)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_companion.py" --paths
  OUTPUT_VARIABLE COMPANION_PATHS OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE COMPANION_RESULT)
if(NOT COMPANION_RESULT EQUAL 0)
  message(FATAL_ERROR "Cannot read companion profile paths")
endif()
string(REPLACE "\n" ";" COMPANION_JSON "${COMPANION_PATHS}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${COMPANION_JSON} "${CMAKE_CURRENT_SOURCE_DIR}/../config/companion.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_companion.py")
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_companion.py"
  RESULT_VARIABLE COMPANION_RESULT)
if(NOT COMPANION_RESULT EQUAL 0)
  message(FATAL_ERROR "Cannot generate companion JSON defaults")
endif()

add_definitions(-DU8X8_WITH_USER_PTR)

# The firmware's version is the first line of ../VERSION, shown at the
# right of the OSD's caption (menu.c).  Read here rather than passed
# through make, so the quoting survives; a change to the file re-runs
# cmake.  Without the file the caption shows no version.
set(CORE_VERSION "")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION)
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION CORE_VERSION LIMIT_COUNT 1)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION)
endif()
add_definitions(-DCORE_VERSION="${CORE_VERSION}")

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

sdk_add_include_directories(. u8g2/csrc)

target_sources(app PRIVATE usb_host.c hidparser.c spi.c osd_u8g2.c menu.c sdc.c sysctrl.c bt_ble.c
                           companion.c ultima.c flashwr.c coreload.c rt11sav.c bas.c extrom.c romload.c azbk.c bk.c evo.c vsd.c)

file(GLOB COMPONENT_SRCS u8g2/csrc/*.c  u8g2/sys/bitmap/common/*.c ft2232d_emulator/*.c)
target_sources(app PRIVATE ${COMPONENT_SRCS})

sdk_add_include_directories("./exported" "./ft2232d_emulator/include")

sdk_set_main_file(main.c)

project(misterynano_fw)
