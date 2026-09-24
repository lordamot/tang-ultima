/*
  bk.c - the БК-0011М keyboard, out of USB HID reports.  See bk.h.
*/
#include <stdio.h>
#include "spi.h"
#include "bk.h"

// plain and shifted codes for HID keycodes 0..0x64, ЛАТ mode
static const unsigned char bk_plain[] = {
  BK_NONE, BK_NONE, BK_NONE, BK_NONE,                     // 00..03
  'a','b','c','d','e','f','g','h','i','j','k','l','m',    // 04..10
  'n','o','p','q','r','s','t','u','v','w','x','y','z',    // 11..1d
  '1','2','3','4','5','6','7','8','9','0',                // 1e..27
  012,        // 28 Enter          ВВОД
  003,        // 29 Esc            КТ
  030,        // 2a Backspace      ЗАБОЙ
  011,        // 2b Tab            ТАБ
  ' ',        // 2c Space
  '-',        // 2d -
  '=',        // 2e =
  '[',        // 2f [
  ']',        // 30 ]
  '\\',       // 31 backslash
  BK_NONE,    // 32 EUR-1
  ';',        // 33 ;
  '\'',       // 34 '
  '`',        // 35 `
  ',',        // 36 ,
  '.',        // 37 .
  '/',        // 38 /
  BK_A_CAPS,  // 39 Caps Lock      СТР
  BK_AR2|001, // 3a F1             ПОВТ
  023,        // 3b F2             ВС
  BK_AR2|025, // 3c F3             ГРАФ
  BK_NONE,    // 3d F4
  BK_AR2|031, // 3e F5             -!->
  BK_AR2|002, // 3f F6             ИНД СУ
  BK_AR2|004, // 40 F7             БЛОК РЕД
  BK_AR2|020, // 41 F8             ШАГ
  014,        // 42 F9             СБР (clear)
  BK_A_STOP,  // 43 F10            СТОП
  BK_A_RESET, // 44 F11            the reset key
  BK_NONE,    // 45 F12            the OSD's, never sent
  BK_NONE,    // 46 PrtScr
  BK_NONE,    // 47 Scroll Lock
  BK_A_STOP,  // 48 Pause          СТОП
  027,        // 49 Insert         |-->
  034,        // 4a Home           up-left
  035,        // 4b PageUp         up-right
  026,        // 4c Delete         |<--
  037,        // 4d End            down-left
  036,        // 4e PageDown       down-right
  031,        // 4f right
  010,        // 50 left
  033,        // 51 down
  032,        // 52 up
  BK_NONE,    // 53 Num Lock
  '/',        // 54 KP /
  '*',        // 55 KP *
  '-',        // 56 KP -
  '+',        // 57 KP +
  012,        // 58 KP Enter
  '1','2','3','4','5','6','7','8','9','0',                // 59..62
  '.',        // 63 KP .
  BK_NONE     // 64 EUR-2
};

static const unsigned char bk_shift[] = {
  BK_NONE, BK_NONE, BK_NONE, BK_NONE,
  'A','B','C','D','E','F','G','H','I','J','K','L','M',
  'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
  '!','@','#','$','%','^','&','*','(',')',
  015,        // Shift+Enter       УСТ ТАБ
  003,
  030,
  020,        // Shift+Tab         СБР ТАБ
  ' ',
  '_', '+', '{', '}', '|', BK_NONE, ':', '"', '~', '<', '>', '?',
  BK_A_CAPS,
  BK_AR2|001, 023, BK_AR2|025, BK_NONE, BK_AR2|031, BK_AR2|002, BK_AR2|004, BK_AR2|020, 014, BK_A_STOP, BK_A_RESET, BK_NONE,
  BK_NONE, BK_NONE, BK_A_STOP,
  027, 034, 035, 026, 037, 036, 031, 010, 033, 032,
  BK_NONE, '/', '*', '-', '+', 012,
  '1','2','3','4','5','6','7','8','9','0', '.', BK_NONE
};

static unsigned char rus  = 0x20;   // 0x20 in ЛАТ, 0 in РУС
static unsigned char caps = 0;      // 0x20 with СТР on

static void bk_send(spi_t *spi, unsigned char code, unsigned char flags) {
  printf("KBD BK: %03o flags %02x\r\n", code, flags);
  spi_begin(spi);
  spi_tx_u08(spi, SPI_TARGET_HID);
  spi_tx_u08(spi, SPI_HID_KEYBOARD);
  spi_tx_u08(spi, code);
  spi_tx_u08(spi, flags);
  spi_end(spi);
}

// modifier bits of the USB report: 0 LCtrl 1 LShift 2 LAlt 3 LGUI 4 RCtrl 5 RShift 6 RAlt 7 RGUI
void kbd_tx_bk(spi_t *spi, unsigned char hid, unsigned char mod, char pressed) {
  unsigned char flags = pressed ? 0 : BKF_RELEASE;
  int shift = mod & 0x22;
  int ctrl  = mod & 0x10;          // right control is СУ
  int ar2   = mod & 0x44;          // Alt is АР2

  // the modifier keys themselves come as 0xe0 + bit
  if(hid >= 0xe0) {
    int bit = hid - 0xe0;
    if(bit == 0) {                 // left control: РУС (with АР2: the palette hotkey)
      if(pressed) { if(ar2) bk_send(spi, 2, BKF_HOTKEY); else { rus = 0; bk_send(spi, 016, 0); } }
      else if(!ar2) bk_send(spi, 016, BKF_RELEASE);
    } else if(bit == 3 || bit == 7) {   // a Win key: ЛАТ (with АР2: the 512/256 hotkey)
      if(pressed) { if(ar2) bk_send(spi, 1, BKF_HOTKEY); else { rus = 0x20; bk_send(spi, 017, 0); } }
      else if(!ar2) bk_send(spi, 017, BKF_RELEASE);
    }
    return;
  }
  if(hid > 0x64) return;

  unsigned char v = shift ? bk_shift[hid] : bk_plain[hid];
  if(v == BK_NONE) return;

  if(v >= BK_ACTION) {
    switch(v) {
    case BK_A_STOP:  bk_send(spi, 0, BKF_STOP | flags); break;
    case BK_A_RESET: bk_send(spi, 0, BKF_RESET | flags); break;
    case BK_A_CAPS:  if(pressed) caps ^= 0x20; break;
    default: break;
    }
    return;
  }

  if(v & BK_AR2) { flags |= BKF_AR2; v &= 0x7f; }
  else if(ar2) flags |= BKF_AR2;

  // letters: the machine's case rules, then СУ
  if(v >= 'a' && v <= 'z')      v = v - (rus ^ caps);
  else if(v >= 'A' && v <= 'Z') v = v + (rus ^ caps);
  // the six Cyrillic letters the БК keeps on symbol keys (Ш [, Щ ], Ъ },
  // Э \, Ч ^, Ю @): in РУС the key gives the letter, as the БК's own
  // keyboard does, so [ ] \ Shift+6 Shift+2 are Ш Щ Э Ч Ю and Shift+] is
  // Ъ (0x7F, which no ASCII key sends otherwise).  Until 24 Sep 2026 the
  // letters needed Shift and Ъ could not be typed at all.
  if(!rus) switch(v) {
    case '[':  v = 0173; break;   // Ш
    case ']':  v = 0175; break;   // Щ
    case '\\': v = 0174; break;   // Э
    case '^':  v = 0176; break;   // Ч
    case '@':  v = 0140; break;   // Ю
    case '}':  v = 0177; break;   // Ъ
    default: break;
  }
  if(ctrl && ((v & 0x60) == 0x40 || (v & 0x60) == 0x60)) v &= 037;

  bk_send(spi, v, flags);
}
