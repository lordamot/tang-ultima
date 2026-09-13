# Stage 2 for the on-board BL616; built by the top-level `make onboard-fw`,
# which passes the toolchain and the SDK.

SDK_DEMO_PATH ?= .
BL_SDK_BASE ?= $(SDK_DEMO_PATH)/../tools/bouffalo_sdk

export BL_SDK_BASE

CHIP ?= bl616
BOARD ?= bl616dk
CROSS_COMPILE ?= riscv64-unknown-elf-

include $(BL_SDK_BASE)/project.build
