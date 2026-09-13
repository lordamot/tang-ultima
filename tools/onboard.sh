#!/bin/sh
# What the Tang Nano 20K's on-board BL616 looks like from the host, read
# out of sysfs (no root, no lsusb -v).  Used by the Makefile's onboard-*
# and flash-mcu-onboard-* targets; .claude/docs/onboard.md is the account.
#
#   onboard.sh status       say what is on the bus and what it means
#   onboard.sh check-boot   exit 0 only if exactly ONE BL616 is in boot
#                           mode and it cannot be the M0S Dock
#
# The three faces of the chip:
#   0403:6010  "20K's FRIEND"        the factory firmware - FT2232 emulation,
#                                    openFPGALoader works
#   0403:6010  anything else         Sipeed's FPGA Partner (FT2232 emulation
#                                    plus a second stage at 0x40000)
#   349b:6160  "Bouffalo CDC DEMO"   the boot ROM's ISP: UPDATE was held at
#                                    power-up, OR the image at address 0 does
#                                    not run on this chip (wrong encryption)
# The M0S Dock's BL616 shows the same 349b:6160 in boot mode, and nothing
# at all when running (it is a USB host), so a CDC DEMO with the FRIEND
# still present is the dock, not the board.

set -e

scan() {
  for d in /sys/bus/usb/devices/*; do
    [ -f "$d/idVendor" ] || continue
    v=$(cat "$d/idVendor"); p=$(cat "$d/idProduct")
    case "$v:$p" in
      0403:6010|349b:6160)
        m=$(cat "$d/manufacturer" 2>/dev/null || echo "?")
        n=$(cat "$d/product" 2>/dev/null || echo "?")
        s=$(cat "$d/serial" 2>/dev/null || echo "-")
        printf '%s:%s\t%s\t%s\t%s\t%s\n' "$v" "$p" "$m" "$n" "$s" "${d##*/}"
        ;;
    esac
  done
}

friend_count()  { scan | grep -c "20K's FRIEND" || true; }
ftdi_count()    { scan | grep -c '^0403:6010' || true; }
cdc_count()     { scan | grep -c '^349b:6160' || true; }

case "${1:-status}" in
  status)
    echo "BL616s on the USB bus:"
    if [ -z "$(scan)" ]; then echo "  (none)"; else scan | sed 's/^/  /'; fi
    echo
    f=$(friend_count); t=$(ftdi_count); c=$(cdc_count)
    if [ "$f" -ge 1 ]; then
      echo "on-board BL616: FACTORY firmware (20K's FRIEND) - openFPGALoader works;"
      echo "  hold UPDATE (the small button by the HDMI socket) while plugging in"
      echo "  to reach the boot ROM for a flash-mcu-onboard-* target"
    elif [ "$t" -ge 1 ]; then
      echo "on-board BL616: an FT2232 that is not the FRIEND - the FPGA PARTNER,"
      echo "  presumably; openFPGALoader should still work"
    fi
    if [ "$c" -ge 1 ]; then
      echo "boot ROM (Bouffalo CDC DEMO) x$c: a BL616 is in ISP mode."
      if [ "$f" -ge 1 ] || [ "$t" -ge 1 ]; then
        echo "  The board's FT2232 is still there, so this one is the M0S DOCK."
      else
        echo "  Either UPDATE was held at power-up, or the image at address 0"
        echo "  does not run on this chip - see onboard.md, 'If it stays CDC DEMO'."
      fi
      ls /dev/ttyACM* 2>/dev/null | sed 's/^/  port: /'
    fi
    if [ "$f" = 0 ] && [ "$t" = 0 ] && [ "$c" = 0 ]; then
      echo "on-board BL616: NOT on the bus.  Running a firmware that is not a"
      echo "  USB device (a companion build), or unplugged.  Hold UPDATE while"
      echo "  plugging in and look again."
    fi
    ;;
  check-boot)
    c=$(cdc_count)
    if [ "$c" != 1 ]; then
      echo "need exactly ONE BL616 in boot mode (349b:6160 Bouffalo CDC DEMO), found $c" >&2
      echo "hold UPDATE (by the HDMI socket) while plugging the board in; unplug the dock" >&2
      exit 1
    fi
    if [ "$(ftdi_count)" -ge 1 ]; then
      echo "the board's FT2232 is still enumerated, so the CDC DEMO on the bus is the" >&2
      echo "M0S DOCK, not the on-board chip.  Refusing.  (make flash-mcu is the dock's.)" >&2
      exit 1
    fi
    ;;
  *)
    echo "usage: $0 status|check-boot" >&2; exit 2;;
esac
