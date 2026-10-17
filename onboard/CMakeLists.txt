cmake_minimum_required(VERSION 3.15)

include(proj.conf)

# Stamped into the first RAM diagnostic record.
string(TIMESTAMP STAGE2_BUILD "0x%Y%m%d" UTC)
add_definitions(-DSTAGE2_BUILD=${STAGE2_BUILD})

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

set(XZ_DIR ${BL_SDK_BASE}/components/lzma/xz)
target_sources(app PRIVATE jtag.c gowin.c link.c ramstage.c
  ${XZ_DIR}/xz_crc32.c ${XZ_DIR}/xz_dec_lzma2.c ${XZ_DIR}/xz_dec_stream.c)
target_include_directories(app PRIVATE ${XZ_DIR})
target_compile_definitions(app PRIVATE XZ_DEC_PREALLOC)
sdk_set_main_file(main.c)

project(stage2)
