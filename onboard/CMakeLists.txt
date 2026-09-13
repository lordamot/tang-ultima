cmake_minimum_required(VERSION 3.15)

include(proj.conf)

# stamped into the LOG sector's first record, so a read-back says which
# build wrote it
string(TIMESTAMP STAGE2_BUILD "0x%Y%m%d" UTC)
add_definitions(-DSTAGE2_BUILD=${STAGE2_BUILD})

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

target_sources(app PRIVATE jtag.c gowin.c link.c)
sdk_set_main_file(main.c)

project(stage2)
