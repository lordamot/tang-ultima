# Tang Ultima - three Soviet machines in one Tang Nano 20K, switched from
# the OSD.
#
# The machines are the sibling repositories, built here out of their trees:
#
#   ../tang-uknc     МС0511 УКНЦ
#   ../tang-pk8000   ПК8000 Сура
#   ../tang-korvet   ПК8020 Корвет
#
# The flash holds ONE bitstream, at flash address 0, and that is the
# machine the board is - power-up always loads address 0.  All three live
# on the SD card as packed bitstreams, /cores/<name>.bin, and the OSD's
# Core form writes the wanted one into address 0 through the FPGA's own
# MSPI pins (mnano/flashwr.c, mister/flashwr.v); the board is then
# power-cycled into it.  MultiBoot and its ring are gone: RECONFIG_N
# cannot be pulsed from inside this FPGA.  .claude/docs/ has the account.
#
#   make toolchain     fetch the toolchain into tools/  (~8 GB, once)
#   make cores         build all three -> bin/<core>.fs and bin/<core>.bin
#   make core-uknc     one of them (also core-pk8000, core-korvet)
#   make card          say which files to copy onto the SD card
#   make fw            build the BL616 firmware -> build/fw/bl616.bin
#   make menu-test     the OSD menu on the host, all three cores' forms as PNG
#   make lint          Verilator over each core, in its own tree
#   make flash-image   openFPGALoader the default core into flash address 0
#   make flash-core-uknc   any single core into address 0 (also -pk8000, -korvet)
#   make flash-mcu     flash the firmware over UART (COMX=/dev/ttyACM0)
#   make clean         remove build/
#
# DEFAULT_CORE=<name> is what `make flash-image` puts at address 0 (uknc).
# LOADING_RATE=<MHz> passes -loading_rate to Gowin for all three cores -
# the MSPI clock the FPGA reads the flash at, Gowin's default being 2.5,
# which is about 3 s to configure.  Untested on a board: leave it alone
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
# The cores: where each lives and what Gowin calls its project.
#
# There are no slots any more.  The flash holds ONE bitstream, at address
# 0, and that is the machine the board is; the three live on the SD card
# as packed bitstreams and the OSD writes the wanted one to address 0
# (mnano/flashwr.c, .claude/docs/coreswitch.md).  MultiBoot is gone
# because RECONFIG_N cannot be pulsed from inside this FPGA - see
# .claude/docs/progress.md - so the ring, the jump addresses and the
# three-slot image went with it.
#-----------------------------------------------------------------------
CORES        := uknc pk8000 korvet
# what `make flash-image` puts at address 0, i.e. what a fresh board is
DEFAULT_CORE ?= uknc

DIR_uknc     := $(ROOT)/../tang-uknc
NAME_uknc    := test003

DIR_pk8000   := $(ROOT)/../tang-pk8000
NAME_pk8000  := pk8000

DIR_korvet   := $(ROOT)/../tang-korvet
NAME_korvet  := korvet

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

.PHONY: all help toolchain cores card fw menu-test lint clean \
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
	@grep -iE "Timing Constraints|Logic|Register|BSRAM|PLL" \
	    $$(BUILD)/cores/$(1)/impl/pnr/$$(NAME_$(1)).rpt.txt 2>/dev/null | head -8 || true

# the packed form, which is what goes on the card and what the OSD writes
# into the flash - byte for byte Gowin's own .bin
bin/$(1).bin: bin/$(1).fs $$(TOOLS)/mkimage.py
	$$(PYTHON) $$(TOOLS)/mkimage.py --fs2bin bin/$(1).fs $$@

$$(BUILD)/cores/$(1)/impl/pnr/$$(NAME_$(1)).fs: FORCE
	@test -x $$(GWSH) || { echo "gowin missing - run: make toolchain" >&2; exit 1; }
	@test -d $$(DIR_$(1))/tang || { echo "$$(DIR_$(1)) not found - the sibling repository is expected beside this one" >&2; exit 1; }
	@mkdir -p $$(BUILD)/cores/$(1)
	$$(PYTHON) $$(DIR_$(1))/tools/gowin_tcl.py --abs $$(TCLOPTS) \
	    > $$(BUILD)/cores/$(1)/build.tcl
	cd $$(BUILD)/cores/$(1) && $$(GWENV) $$(GWSH) build.tcl

# there is one slot, address 0, so this is the same write for every core
flash-core-$(1): bin/$(1).bin
	$$(OFL) -b tangnano20k -f --file-type bin -o 0 bin/$(1).bin

lint-$(1):
	$$(MAKE) -C $$(DIR_$(1)) lint
endef
$(foreach c,$(CORES),$(eval $(call CORE_RULES,$(c))))

FORCE:

cores: $(foreach c,$(CORES),bin/$(c).fs bin/$(c).bin)

lint: $(foreach c,$(CORES),lint-$(c))

#-----------------------------------------------------------------------
# The card.  /sd/cores/<name>.bin is where the OSD looks for a machine to
# install, so these three files are what makes the switch work at all -
# without them the Core form can only report that the file is missing.
#-----------------------------------------------------------------------
card: $(foreach c,$(CORES),bin/$(c).bin)
	@echo
	@echo "copy these into /cores/ on the SD card:"
	@ls -l $(foreach c,$(CORES),bin/$(c).bin)
	@echo
	@echo "  SD:/cores/uknc.bin  SD:/cores/pk8000.bin  SD:/cores/korvet.bin"
	@echo
	@echo "and SD:/ultima.ini records which one is in the flash (the OSD writes it)."

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
# The first flash: one core at address 0, which is all the flash holds.
# After this the board never needs openFPGALoader again unless an install
# is interrupted - the OSD writes address 0 itself from the card.
flash-image: bin/$(DEFAULT_CORE).bin
	$(OFL) -b tangnano20k -f --file-type bin -o 0 bin/$(DEFAULT_CORE).bin

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
