# Tang Ultima - five Soviet machines in one Tang Nano 20K, switched from
# the OSD.
#
# The machines are the sibling repositories, built here out of their trees:
#
#   ../tang-uknc     МС0511 УКНЦ
#   ../tang-pk8000   ПК8000 Сура
#   ../tang-korvet   ПК8020 Корвет
#   ../tang-zs256    Scorpion ZS-256 Turbo+
#   ../tang-bk-epta  БК-0011М with an AZBK controller (BK Nano)
#
# The flash holds ONE bitstream, at flash address 0, and that is the
# machine the board is - power-up always loads address 0.  All five live
# on the SD card as packed bitstreams, /cores/<name>.bin, and the OSD's
# Core form writes the wanted one into address 0 through the FPGA's own
# MSPI pins (mnano/flashwr.c, mister/flashwr.v); the board is then
# power-cycled into it.  MultiBoot and its ring are gone: RECONFIG_N
# cannot be pulsed from inside this FPGA.  .claude/docs/ has the account.
#
#   make toolchain     fetch the toolchain into tools/  (~8 GB, once)
#   make cores         build all five -> bin/<core>.fs and bin/<core>.bin
#   make core-uknc     one of them (also core-pk8000, core-korvet, core-zs256, core-bk)
#   make card          say which files to copy onto the SD card
#   make fw            build the BL616 firmware -> build/fw/bl616.bin
#   make menu-test     the OSD menu on the host, all five cores' forms as PNG
#   make lint          Verilator over each core, in its own tree
#   make flash-image   openFPGALoader the default core into flash address 0
#   make flash-core-uknc   any single core into address 0 (also -pk8000, -korvet, -zs256, -bk)
#   make flash-mcu     flash the firmware over UART (COMX=/dev/ttyACM0)
#   make clean         remove build/
#
# The ON-BOARD BL616 - the board's own USB programmer, a separate chip
# from the dock's (.claude/docs/onboard.md; read it before touching it):
#   make onboard-status        what it enumerates as, and what that means
#   make onboard-fetch         the factory and Partner images from upstream
#                              into bin/onboard/, sha256-checked
#   make onboard-backup        read its flash into bin/onboard/backup/ - FIRST
#   make onboard-efuse         read the efuse: fused for encryption, or not
#   make flash-mcu-onboard-orig            factory firmware, plain variant
#   make flash-mcu-onboard-orig-encrypted  factory firmware, encrypted variant
#   make flash-mcu-onboard-ftdi    Sipeed's FPGA Partner: FT2232 + 2nd stage
#   make flash-mcu-onboard-stage2  a firmware at 0x40000 (STAGE2=<file>)
#   make flash-mcu-onboard-restore BACKUP=<file>  a backup back to address 0
#   make onboard-fw            build onboard/, stage 2 -> build/onboard/stage2.bin
#   make flash-mcu-onboard-core CORE=uknc   stage a core in the chip's flash
#                              at 0x100000 for stage 2 to load into the FPGA
#   make onboard-log           read back what stage 2 logged on its last run
# All of these need the chip in boot mode: hold UPDATE (the small button
# by the HDMI socket) while plugging the board in, and the dock unplugged.
#
# DEFAULT_CORE=<name> is what `make flash-image` puts at address 0 (uknc).
# LOADING_RATE=<MHz> passes -loading_rate to Gowin for all five cores -
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
# 0, and that is the machine the board is; the five live on the SD card
# as packed bitstreams and the OSD writes the wanted one to address 0
# (mnano/flashwr.c, .claude/docs/coreswitch.md).  MultiBoot is gone
# because RECONFIG_N cannot be pulsed from inside this FPGA - see
# .claude/docs/progress.md - so the ring, the jump addresses and the
# three-slot image went with it.
#-----------------------------------------------------------------------
CORES        := uknc pk8000 korvet zs256 bk
# what `make flash-image` puts at address 0, i.e. what a fresh board is
DEFAULT_CORE ?= uknc

DIR_uknc     := $(ROOT)/../tang-uknc
NAME_uknc    := test003

DIR_pk8000   := $(ROOT)/../tang-pk8000
NAME_pk8000  := pk8000

DIR_korvet   := $(ROOT)/../tang-korvet
NAME_korvet  := korvet

DIR_zs256    := $(ROOT)/../tang-zs256
NAME_zs256   := zs256

DIR_bk       := $(ROOT)/../tang-bk-epta
NAME_bk      := bk

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
        onboard-status onboard-fetch onboard-backup onboard-efuse \
        flash-mcu-onboard-orig flash-mcu-onboard-orig-encrypted \
        flash-mcu-onboard-ftdi flash-mcu-onboard-stage2 flash-mcu-onboard-restore \
        onboard-fw onboard-log flash-mcu-onboard-core \
        $(foreach c,$(CORES),core-$(c) flash-core-$(c) lint-$(c))

all: cores fw

help:
	@sed -n '2,48p' $(firstword $(MAKEFILE_LIST)) | sed 's/^# \?//'

toolchain:
	$(TOOLS)/fetch.sh

#-----------------------------------------------------------------------
# One core: its own tools/gowin_tcl.py writes the Tcl with absolute
# paths and the next slot's address, gw_sh runs it here (it writes impl/
# under its cwd), its own tools/timing_check.py gates the result, and
# the .fs is copied to bin/.  The sibling's tree is read, never written.
#-----------------------------------------------------------------------
define CORE_RULES
core-$(1): bin/$(1).fs bin/$(1).bin

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
# install, so these five files are what makes the switch work at all -
# without them the Core form can only report that the file is missing.
#-----------------------------------------------------------------------
card: $(foreach c,$(CORES),bin/$(c).bin)
	@echo
	@echo "copy these into /cores/ on the SD card:"
	@ls -l $(foreach c,$(CORES),bin/$(c).bin)
	@echo
	@echo "  SD:/cores/uknc.bin  SD:/cores/pk8000.bin  SD:/cores/korvet.bin  SD:/cores/zs256.bin  SD:/cores/bk.bin"
	@echo
	@echo "and SD:/ultima.ini records which one is in the flash (the OSD writes it)."
	@echo
	@echo "The ZS-256 has no ROM in its bitstream: SD:/zs256/zs256.rom (../tang-zs256/soft/rom/scorp294.rom,"
	@echo "or a 256 KB ProfROM) and SD:/zs256/gs105a.rom, else it runs zeros (romload.c)."
	@echo
	@echo "The BK has no ROM in its bitstream either: SD:/bk/ is MAXIOL's AZBK card package"
	@echo "(../tang-bk-epta/soft/azbk/: AZ.INI, ROM/, DISKS/ - its 'make card' stages it),"
	@echo "else it restarts for ever on empty memory (azbk.c reads AZ.INI at start)."

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
# -O0: this host's gcc 15.2 dies with an internal error at -O1 and above
# on ff.c and two u8g2 files (13 Sep 2026), and a test needs no optimiser
HOST_OPT  ?= -O0
MENU_TEST_SRC := mnano/menu_test.c mnano/menu.c mnano/ultima.c \
  $(wildcard mnano/u8g2/csrc/*.c) mnano/u8g2/sys/bitmap/common/u8x8_d_bitmap.c \
  $(FATFS_SRC)/ff.c $(FATFS_SRC)/ffunicode.c

menu-test: $(MENU_TEST_SRC) mnano/menu.h mnano/ultima.h VERSION
	@test -d $(FATFS_SRC) || { echo "bouffalo_sdk missing - run: make toolchain" >&2; exit 1; }
	@mkdir -p $(BUILD)/menu
	rm -f $(BUILD)/menu/*.txt $(BUILD)/menu/*.png
	@$(CC) $(HOST_OPT) -w -DSDL -DCORE_VERSION='"$(shell head -1 VERSION)"' \
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
	@test ! $(BUILD)/fw/bl616.bin -nt $(FW_BIN) || { \
	  echo "$(BUILD)/fw/bl616.bin is newer than $(FW_BIN) - copy it there, or" >&2; \
	  echo "FW_BIN=$(BUILD)/fw/bl616.bin.  (13 Sep 2026: the old one went on the dock.)" >&2; \
	  exit 1; }
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

#-----------------------------------------------------------------------
# The ON-BOARD BL616.  Not the dock's: this is the chip on the Tang Nano
# 20K itself, the one that is the board's USB-JTAG programmer.  Its
# factory firmware ("20K's FRIEND") emulates an FT2232; Sipeed's FPGA
# Partner does the same and also starts a second firmware at flash
# 0x40000 when no PC is attached - which is where a firmware built here
# would go, to load the FPGA's SRAM over the JTAG this chip owns (route
# D in coreswitch.md).  .claude/docs/onboard.md is the whole account,
# the recovery plan included.  Two things to know before running any of
# these:
#
#   - Whether the Partner runs at all depends on an efuse (flash
#     encryption) that Sipeed set on boards from about 2024 on.  On a
#     chip without it the encrypted Partner does not start and the
#     board sits in boot mode until a PLAIN image is written back.
#     `make onboard-efuse` reads the fuse; `make onboard-backup` keeps
#     this board's own factory image, which is the right variant by
#     construction.  Do both first.
#   - Nothing here can brick the chip: the ISP that these targets talk
#     to is in the BL616's mask ROM and UPDATE-at-power-up always
#     reaches it.  The FPGA boots from its own flash regardless.
#
# The images come from MiSTle-Dev/FPGA-Companion (the successor of the
# MiSTeryNano firmware this repository's mnano/ descends from), pinned
# to a commit and a release and checked against the sha256 recorded here.
#-----------------------------------------------------------------------
ONBOARD    := bin/onboard
FC_COMMIT  := 49ebb110045866493ce5802e46466096b4265874
FC_RELEASE := v1.4.29
FC_RAW     := https://raw.githubusercontent.com/MiSTle-Dev/FPGA-Companion/$(FC_COMMIT)/src/bl616
FC_DL      := https://github.com/MiSTle-Dev/FPGA-Companion/releases/download/$(FC_RELEASE)

# the factory firmware, plain and encrypted - one of them is what this
# board left the factory with
URL_friend_20k_bl616           := $(FC_RAW)/friend_20k/friend_20k_bl616.bin
SHA_friend_20k_bl616           := 8ed648878b4824d475239414667883db1eb4bbb78294f092591ea881c597f95b
URL_friend_20k_encrypted_bl616 := $(FC_RAW)/friend_20k/friend_20k_encrypted_bl616.bin
SHA_friend_20k_encrypted_bl616 := bc5504c05f00518a0568f5436989133dbdf990eccb66cb566950fad942b2df3a
# Sipeed's FPGA Partner for the Nano 20K (encrypted; identical to the
# release's bl616_fpga_partner_nano20k.bin)
URL_bl616_fpga_partner_20kNano := $(FC_RAW)/bl616_fpga_partner/bl616_fpga_partner_20kNano.bin
SHA_bl616_fpga_partner_20kNano := bc5b1f733a13562c898ff66b540038c116c3d77ca202d0afebd1e7af1c3b3203
# upstream's own second stage for the Partner, a known-good one to prove
# the 0x40000 mechanism with before a firmware from here goes there
URL_fpga_companion_nano20k     := $(FC_DL)/fpga_companion_nano20k.bin
SHA_fpga_companion_nano20k     := 323188e2e456483371db3eff0ace9427a27c840aee4b950bf5fae81cc806672d

ONBOARD_FILES := $(addprefix $(ONBOARD)/,friend_20k_bl616.bin \
  friend_20k_encrypted_bl616.bin bl616_fpga_partner_20kNano.bin \
  fpga_companion_nano20k.bin)

# what goes to 0x40000 behind the Partner: stage 2 from onboard/ (`make
# onboard-fw`); STAGE2=bin/onboard/fpga_companion_nano20k.bin is upstream's
STAGE2  ?= $(BUILD)/onboard/stage2.bin
# the core stage 2 loads into the FPGA, staged in the chip's flash
CORE    ?= uknc
STAGE_ADDR := 0x100000
LOG_ADDR   := 0x0FE000
# how much of the chip's flash onboard-backup reads: 1 MB covers address
# 0 and the whole second-stage region with room to spare
BACKUP_LEN ?= 0x100000

onboard-fetch: $(ONBOARD_FILES)

$(ONBOARD)/%.bin:
	@test -n "$(URL_$*)" || { echo "no upstream source known for $*" >&2; exit 1; }
	@mkdir -p $(ONBOARD)
	curl -fsSL -o $@.part "$(URL_$*)"
	@echo "$(SHA_$*)  $@.part" | sha256sum -c --quiet || { \
	  rm -f $@.part; echo "$*: sha256 does not match the Makefile - upstream changed, or the download did" >&2; exit 1; }
	@mv $@.part $@
	@ls -l $@

onboard-status:
	@sh $(TOOLS)/onboard.sh status

# the ISP session every onboard target shares: the tool, one BL616 in
# boot mode that is not the dock, and a writable port
define ONBOARD_SESSION
	@test -x $(BLFLASH) || { echo "bouffalo_sdk missing - run: make toolchain" >&2; exit 1; }
	@sh $(TOOLS)/onboard.sh check-boot
	@test -w $(COMX) || { echo "cannot write $(COMX) - dialout group?  (COMX=... to pick another port)" >&2; exit 1; }
	@mkdir -p $(BUILD)/onboard
endef

# a BL616 image starts with BFNP and has FCFG at 8 - refuse anything else
define ONBOARD_IMAGE_CHECK
	@$(PYTHON) -c 'import sys; d=open(sys.argv[1],"rb").read(16); sys.exit(0 if d[:4]==b"BFNP" and d[8:12]==b"FCFG" else 1)' "$(1)" \
	  || { echo "$(1) is not a BL616 boot image (no BFNP/FCFG header)" >&2; exit 1; }
endef

# $(call ONBOARD_WRITE,<erase 1|2>,<file>,<address>) - one image, one
# address, through an .ini like upstream's; erase 1 clears only the
# range the file covers, 2 the whole chip.  ONBOARD_WRITE_RAW is the
# same without the boot-image check, for data that is not a firmware
define ONBOARD_WRITE
	$(call ONBOARD_IMAGE_CHECK,$(2))
	$(call ONBOARD_WRITE_RAW,$(1),$(2),$(3))
endef

define ONBOARD_WRITE_RAW
	@printf '[cfg]\nerase = $(1)\nskip_mode = 0x0, 0x0\nboot2_isp_mode = 0\n\n[FW]\nfiledir = %s\naddress = $(3)\n' \
	  "$(abspath $(2))" > $(BUILD)/onboard/write.ini
	@echo
	@echo "  $(2)  ->  on-board BL616 flash address $(3)  (erase mode $(1), port $(COMX))"
	@echo
	$(BLFLASH) --interface=uart --baudrate=$(BAUDRATE) --port=$(COMX) \
	  --chipname=bl616 --cpu_id= --config=$(BUILD)/onboard/write.ini
	@echo
	@echo "done.  Unplug, plug in WITHOUT UPDATE, then: make onboard-status"
endef

# Step zero, before anything is written: this board's own factory
# image, which is the right encryption variant by construction, and the
# only backup that is certainly right.  Keep it; consider committing it.
onboard-backup:
	$(ONBOARD_SESSION)
	@mkdir -p $(ONBOARD)/backup
	$(BLFLASH) --interface=uart --baudrate=$(BAUDRATE) --port=$(COMX) \
	  --chipname=bl616 --cpu_id= --flash --read --start=0x0 --len=$(BACKUP_LEN) \
	  --file=$(abspath $(ONBOARD))/backup/bl616-$(shell date +%Y%m%d-%H%M%S).bin
	@ls -l $(ONBOARD)/backup/
	@f=$$(ls -t $(ONBOARD)/backup/bl616-*.bin | head -1); \
	  echo; echo "header of $$f:"; xxd -l 16 $$f; \
	  for k in friend_20k_bl616 friend_20k_encrypted_bl616 bl616_fpga_partner_20kNano; do \
	    if [ -f $(ONBOARD)/$$k.bin ] && cmp -s -n $$(stat -c %s $(ONBOARD)/$$k.bin) $$f $(ONBOARD)/$$k.bin; then \
	      echo "address 0 is byte-identical to upstream's $$k.bin"; fi; done

# the efuse: word 0 says whether only encrypted images run (fused) or
# only plain ones - tools/efuse_bl616.py decodes it
onboard-efuse:
	$(ONBOARD_SESSION)
	$(BLFLASH) --interface=uart --baudrate=$(BAUDRATE) --port=$(COMX) \
	  --chipname=bl616 --cpu_id= --efuse --read --start=0x0 --len=0x200 \
	  --file=$(abspath $(BUILD))/onboard/efuse.bin
	@mkdir -p $(ONBOARD)/backup && cp $(BUILD)/onboard/efuse.bin $(ONBOARD)/backup/efuse.bin
	$(PYTHON) $(TOOLS)/efuse_bl616.py $(BUILD)/onboard/efuse.bin

# the factory firmware back at address 0.  Plain for a chip without the
# encryption fuse (upstream: section erase), encrypted for one with it
# (upstream: chip erase - kept as upstream has it).  Wrong variant = the
# board stays in boot mode; flash the other one, nothing is lost.
flash-mcu-onboard-orig: $(ONBOARD)/friend_20k_bl616.bin
	$(ONBOARD_SESSION)
	$(call ONBOARD_WRITE,1,$<,0x000000)

flash-mcu-onboard-orig-encrypted: $(ONBOARD)/friend_20k_encrypted_bl616.bin
	$(ONBOARD_SESSION)
	$(call ONBOARD_WRITE,2,$<,0x000000)

# Sipeed's FPGA Partner at address 0: the FT2232 emulation, plus "start
# whatever is at 0x40000 when no PC is attached".  Section erase, so a
# second stage already there survives.  Encrypted: runs only on a fused
# chip - `make onboard-efuse` first.
flash-mcu-onboard-ftdi: $(ONBOARD)/bl616_fpga_partner_20kNano.bin
	$(ONBOARD_SESSION)
	$(call ONBOARD_WRITE,1,$<,0x000000)

# the second stage, behind the Partner.  Section erase: address 0 is
# not touched.  STAGE2=build/fw/bl616.bin once an internal build exists.
flash-mcu-onboard-stage2: $(STAGE2)
	$(ONBOARD_SESSION)
	$(call ONBOARD_WRITE,1,$(STAGE2),0x40000)

#-----------------------------------------------------------------------
# Stage 2 of our own (onboard/): loads a core staged in the chip's flash
# into the FPGA's SRAM over JTAG when the Partner starts it - i.e. when
# the board is powered from something that is not a PC.  onboard/stage.h
# and tools/mkstage.py share the layout.
#-----------------------------------------------------------------------
STAGE2_OUT := onboard/build/build_out/stage2_bl616.bin

onboard-fw:
	@test -d $(TOOLS)/bouffalo_sdk || { echo "bouffalo_sdk missing - run: make toolchain" >&2; exit 1; }
	$(MAKE) -C onboard \
	  CROSS_COMPILE=$(TOOLS)/toolchain_gcc_t-head_linux/bin/riscv64-unknown-elf- \
	  BL_SDK_BASE=$(TOOLS)/bouffalo_sdk \
	  PATH="$(TOOLS)/cmake/bin:$(TOOLS)/bin:$$PATH"
	@mkdir -p $(BUILD)/onboard
	@cp $(STAGE2_OUT) $(BUILD)/onboard/stage2.bin
	@echo
	@echo "stage 2: build/onboard/stage2.bin"
	@ls -l $(BUILD)/onboard/stage2.bin

# from the committed bin/<core>.bin as it is - not a prerequisite, or
# the FORCE rule above would rebuild the core every time
$(BUILD)/onboard/stage-%.bin: $(TOOLS)/mkstage.py
	@test -f bin/$*.bin || { echo "no bin/$*.bin - make core-$*" >&2; exit 1; }
	@mkdir -p $(BUILD)/onboard
	$(PYTHON) $(TOOLS)/mkstage.py bin/$*.bin $@

stage-%: $(BUILD)/onboard/stage-%.bin
	@:

# the core, staged: descriptor sector + bitstream at 0x100000
flash-mcu-onboard-core: $(BUILD)/onboard/stage-$(CORE).bin
	$(ONBOARD_SESSION)
	$(call ONBOARD_WRITE_RAW,1,$<,$(STAGE_ADDR))

# what stage 2 wrote on its last run
onboard-log:
	$(ONBOARD_SESSION)
	$(BLFLASH) --interface=uart --baudrate=$(BAUDRATE) --port=$(COMX) \
	  --chipname=bl616 --cpu_id= --flash --read --start=$(LOG_ADDR) --len=0x1000 \
	  --file=$(abspath $(BUILD))/onboard/log.bin
	@echo; echo "stage 2 log:"
	@$(PYTHON) $(TOOLS)/mkstage.py --log $(BUILD)/onboard/log.bin

# a backup back where it came from: BACKUP=bin/onboard/backup/<file>
flash-mcu-onboard-restore:
	@test -n "$(BACKUP)" -a -f "$(BACKUP)" || { \
	  echo "BACKUP=<file> - one of:" >&2; ls $(ONBOARD)/backup/bl616-*.bin 2>&1 >&2; exit 1; }
	$(ONBOARD_SESSION)
	$(call ONBOARD_WRITE,1,$(BACKUP),0x000000)

clean:
	rm -rf $(BUILD) mnano/build mnano/build_out onboard/build
