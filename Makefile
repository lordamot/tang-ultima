# Tang Ultima - three Soviet machines in one Tang Nano 20K, switched from
# the OSD.
#
# The machines are the sibling repositories, built here out of their trees:
#
#   ../tang-uknc     МС0511 УКНЦ      slot 0  0x000000  (the power-on core)
#   ../tang-pk8000   ПК8000 Сура      slot 1  0x100000
#   ../tang-korvet   ПК8020 Корвет    slot 2  0x200000
#
# Each bitstream's header names the next slot (Gowin MultiBoot), the core
# pulses RECONFIG_N on the firmware's SYS command 9, and the FPGA loads
# that slot.  The firmware (mnano/) is one binary that knows all three
# and hops the ring until the core the card asks for is running.
# .claude/docs/ has the account; `make help` this list.
#
#   make toolchain     fetch the toolchain into tools/  (~8 GB, once)
#   make cores         build the three bitstreams -> bin/<core>.fs, bin/ultima.bin
#   make core-uknc     one of them (also core-pk8000, core-korvet)
#   make image         bin/ultima.bin from bin/*.fs (checks the ring)
#   make fw            build the BL616 firmware -> build/fw/bl616.bin
#   make menu-test     the OSD menu on the host, all three cores' forms as PNG
#   make lint          Verilator over each core, in its own tree
#   make flash-image   openFPGALoader bin/ultima.bin into the flash (one operation)
#   make flash-core-uknc   one slot only (also flash-core-pk8000, -korvet)
#   make flash-mcu     flash the firmware over UART (COMX=/dev/ttyACM0)
#   make clean         remove build/
#
# LOADING_RATE=<MHz> passes -loading_rate to Gowin for all three cores
# (the MSPI clock the FPGA reads the flash at; the default 2.5 makes a
# core switch about 3 s a hop).  Untested on a board: leave it alone
# unless you mean to try it.

ROOT     := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
TOOLS    := $(ROOT)/tools
OSS      := $(TOOLS)/oss-cad-suite
BUILD    := $(ROOT)/build

GOWIN    := $(TOOLS)/gowin
GWSH     := $(GOWIN)/bin/gw_sh
# gw_sh ships its own Qt and libstdc++; tools/fetch.sh shadows the stale
# ones and these three variables do the rest (UKNC Nano's build.md).
GWENV    := LD_LIBRARY_PATH="$(GOWIN)/lib:$(GOWIN)/bin" \
            QT_QPA_PLATFORM=offscreen \
            QT_PLUGIN_PATH="$(GOWIN)/plugins/qt"
OFL      := $(OSS)/bin/openFPGALoader
BLFLASH  := $(TOOLS)/bouffalo_sdk/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu
PYTHON   := python3

#-----------------------------------------------------------------------
# The cores: where each lives, what Gowin calls its project, which slot
# it takes and which slot its header points at.  The ring is the order
# of CORES; mnano/ultima.c has the same three in the same order.
#-----------------------------------------------------------------------
CORES        := uknc pk8000 korvet
SLOT_SIZE    := 0x100000

DIR_uknc     := $(ROOT)/../tang-uknc
NAME_uknc    := test003
ADDR_uknc    := 0x000000
NEXT_uknc    := 0x100000

DIR_pk8000   := $(ROOT)/../tang-pk8000
NAME_pk8000  := pk8000
ADDR_pk8000  := 0x100000
NEXT_pk8000  := 0x200000

DIR_korvet   := $(ROOT)/../tang-korvet
NAME_korvet  := korvet
ADDR_korvet  := 0x200000
NEXT_korvet  := 0x000000

LOADING_RATE ?=
TCLOPTS      := $(if $(LOADING_RATE),--loading-rate $(LOADING_RATE),)

COMX     ?= /dev/ttyACM0
BAUDRATE ?= 2000000
FW_BIN   ?= bin/bl616.bin

# The BL616 firmware: -DM0S_DOCK=1 picks the SPI pinout in mnano/spi.c
# that matches the board's wiring; CONFIG_BT_STACK_CLI=0 drops a BLE
# shell that will not build against this SDK.
FW_BOARD := bl616dk -DCMAKE_C_FLAGS=-DM0S_DOCK=1 -DCONFIG_BT_STACK_CLI=0
FW_OUT   := mnano/build/build_out/misterynano_fw_bl616.bin

.PHONY: all help toolchain cores image fw menu-test lint clean \
        flash-image flash-mcu \
        $(foreach c,$(CORES),core-$(c) flash-core-$(c) lint-$(c))

all: cores fw

help:
	@sed -n '2,32p' $(firstword $(MAKEFILE_LIST)) | sed 's/^# \?//'

toolchain:
	$(TOOLS)/fetch.sh

#-----------------------------------------------------------------------
# One core: its own tools/gowin_tcl.py writes the Tcl with absolute
# paths and the next slot's address, gw_sh runs it here (it writes impl/
# under its cwd), its own tools/timing_check.py gates the result, and
# the .fs is copied to bin/.  The sibling's tree is read, never written.
#-----------------------------------------------------------------------
define CORE_RULES
core-$(1): bin/$(1).fs

bin/$(1).fs: $$(BUILD)/cores/$(1)/impl/pnr/$$(NAME_$(1)).fs
	@$$(PYTHON) $$(DIR_$(1))/tools/timing_check.py $$(BUILD)/cores/$(1)/impl/pnr || { \
	  echo "$(1): NOT copied to bin/: the layout fails its timing gate" >&2; exit 1; }
	@mkdir -p bin
	@rm -f $$@ && cp $$< $$@ && chmod u+w $$@
	@grep -a "^//MultiBootSPIAddr" $$@
	@grep -iE "Timing Constraints|Logic|Register|BSRAM|PLL" \
	    $$(BUILD)/cores/$(1)/impl/pnr/$$(NAME_$(1)).rpt.txt 2>/dev/null | head -8 || true

$$(BUILD)/cores/$(1)/impl/pnr/$$(NAME_$(1)).fs: FORCE
	@test -x $$(GWSH) || { echo "gowin missing - run: make toolchain" >&2; exit 1; }
	@test -d $$(DIR_$(1))/tang || { echo "$$(DIR_$(1)) not found - the sibling repository is expected beside this one" >&2; exit 1; }
	@mkdir -p $$(BUILD)/cores/$(1)
	$$(PYTHON) $$(DIR_$(1))/tools/gowin_tcl.py --abs --multiboot-addr $$(NEXT_$(1)) $$(TCLOPTS) \
	    > $$(BUILD)/cores/$(1)/build.tcl
	cd $$(BUILD)/cores/$(1) && $$(GWENV) $$(GWSH) build.tcl

flash-core-$(1): bin/$(1).fs
	$$(OFL) -b tangnano20k -f -o $$(ADDR_$(1)) bin/$(1).fs

lint-$(1):
	$$(MAKE) -C $$(DIR_$(1)) lint
endef
$(foreach c,$(CORES),$(eval $(call CORE_RULES,$(c))))

FORCE:

cores: $(foreach c,$(CORES),bin/$(c).fs) image

lint: $(foreach c,$(CORES),lint-$(c))

#-----------------------------------------------------------------------
# The flash image: the three at their slots, one file, one flash
# operation (the Tang's USB bridge takes one openFPGALoader run per
# replug - build.md).  mkimage.py refuses a ring that does not close.
#-----------------------------------------------------------------------
image: bin/ultima.bin

bin/ultima.bin: $(foreach c,$(CORES),bin/$(c).fs) $(TOOLS)/mkimage.py
	$(PYTHON) $(TOOLS)/mkimage.py $@ $(SLOT_SIZE) \
	    $(foreach c,$(CORES),$(ADDR_$(c)):$(NEXT_$(c)):bin/$(c).fs)

#-----------------------------------------------------------------------
# MCU firmware
#-----------------------------------------------------------------------
fw:
	@test -d $(TOOLS)/bouffalo_sdk || { \
	  echo "bouffalo_sdk missing - run: make toolchain" >&2; exit 1; }
	@test -x $(TOOLS)/toolchain_gcc_t-head_linux/bin/riscv64-unknown-elf-gcc || { \
	  echo "riscv toolchain missing - run: make toolchain" >&2; exit 1; }
	$(MAKE) -C mnano \
	  CROSS_COMPILE=$(TOOLS)/toolchain_gcc_t-head_linux/bin/riscv64-unknown-elf- \
	  BL_SDK_BASE=$(TOOLS)/bouffalo_sdk \
	  BOARD='$(FW_BOARD)' \
	  PATH="$(TOOLS)/cmake/bin:$(TOOLS)/bin:$$PATH"
	@mkdir -p $(BUILD)/fw bin
	@cp $(FW_OUT) $(BUILD)/fw/bl616.bin
	@echo
	@echo "firmware: build/fw/bl616.bin"
	@ls -l $(BUILD)/fw/bl616.bin

# The OSD menu on the host (mnano/menu_test.c): menu.c with its SDL host
# switch, u8g2 drawing into a bitmap, FatFs with no card.  Walks every
# form of every core and leaves each screen under build/menu/ as text
# and PNG.
FATFS_SRC := $(TOOLS)/bouffalo_sdk/components/fs/fatfs
MENU_TEST_SRC := mnano/menu_test.c mnano/menu.c mnano/ultima.c \
  $(wildcard mnano/u8g2/csrc/*.c) mnano/u8g2/sys/bitmap/common/u8x8_d_bitmap.c \
  $(FATFS_SRC)/ff.c $(FATFS_SRC)/ffunicode.c

menu-test: $(MENU_TEST_SRC) mnano/menu.h mnano/ultima.h VERSION
	@test -d $(FATFS_SRC) || { echo "bouffalo_sdk missing - run: make toolchain" >&2; exit 1; }
	@mkdir -p $(BUILD)/menu
	rm -f $(BUILD)/menu/*.txt $(BUILD)/menu/*.png
	@$(CC) -O1 -w -DSDL -DCORE_VERSION='"$(shell head -1 VERSION)"' \
	  -Imnano -Imnano/u8g2/csrc -I$(FATFS_SRC) -o $(BUILD)/menu/menu_test $(MENU_TEST_SRC)
	$(BUILD)/menu/menu_test $(BUILD)/menu
	$(PYTHON) $(TOOLS)/osd_png.py $(BUILD)/menu/*.txt

#-----------------------------------------------------------------------
# Flashing.  The FPGA: replug, flash, power-cycle - in that order (the
# on-board bridge takes one USB reset per replug, and `-f` does not
# reliably reconfigure the chip).  The BL616: hold BOOT, tap RESET,
# release BOOT, then say which port that put on the host.
#-----------------------------------------------------------------------
flash-image: bin/ultima.bin
	$(OFL) -b tangnano20k -f --file-type bin -o 0 bin/ultima.bin

flash-mcu:
	@test -x $(BLFLASH) || { \
	  echo "bouffalo_sdk missing - run: make toolchain" >&2; exit 1; }
	@test -f $(FW_BIN) || { \
	  echo "no firmware at $(FW_BIN)" >&2; exit 1; }
	@test -w $(COMX) || { \
	  echo "cannot write $(COMX) - is the board in boot mode, and are" >&2; \
	  echo "you in the 'dialout' group?  See .claude/docs/build.md." >&2; \
	  exit 1; }
	@mkdir -p $(BUILD)/flash
	@printf '[cfg]\nerase = 1\nskip_mode = 0x0, 0x0\nboot2_isp_mode = 0\n\n[FW]\nfiledir = %s\naddress = 0x000000\n' \
	  "$(abspath $(FW_BIN))" > $(BUILD)/flash/bl616.ini
	@echo "flashing $(abspath $(FW_BIN)) -> $(COMX)"
	$(BLFLASH) --interface=uart --baudrate=$(BAUDRATE) --port=$(COMX) \
	  --chipname=bl616 --config=$(BUILD)/flash/bl616.ini

clean:
	rm -rf $(BUILD) mnano/build mnano/build_out
