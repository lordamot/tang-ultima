//
// korvet.h
//
// USB HID to Корвет (ПК8020) translation table
//
// The machine's keyboard is two matrices the i8080 scans through the
// address bus: the main one, 8 rows of 8, and a second of 3 rows for
// the cursor and function keys (Emu80's KorvetKeyboard, which the core's
// ports.v implements).  A code here is row*8 + column + 1, 1..64 for the
// main matrix and 65..88 for the second; 0 means the key does not exist
// on the machine (MISS).  The core takes the code as a press and
// 0x80 | code as the release, the generic MiSTeryNano way (usb_host.c's
// kbd_tx), and sets or clears the matrix bit itself.
//
//   main   row 0: @ A B C D E F G        row 4: 0 1 2 3 4 5 6 7
//          row 1: H I J K L M N O        row 5: 8 9 : ; , - . /
//          row 2: P Q R S T U V W        row 6: ВК(Enter) СТРН(Clear) СТОП ВЗ(Ins) ИЗ(Del) ЗБ(Bksp) ТАБ Space
//          row 3: X Y Z [ \ ] ^ _        row 7: РГ(Shift) АЛФ ГРФ ПРФ(Esc) СЕЛ УПР(Ctrl) ФИКС(Caps) РГ
//   second row 0: page-home home-up-left down end-down-right left МЕНЮ right home
//          row 1: up end - - - - page-end -
//          row 2: F1 F2 F3 F4 F5
//

#ifndef MISS
#define MISS          (0)
#endif
#define KV(row,col)   ((row)*8+(col)+1)
#define KX(row,col)   (64+(row)*8+(col)+1)

static const unsigned char keymap_korvet[] = {
  MISS,         // 00: NoEvent
  MISS,         // 01: Overrun Error
  MISS,         // 02: POST fail
  MISS,         // 03: ErrorUndefined

  // characters
  KV(0,1), // 04: a
  KV(0,2), // 05: b
  KV(0,3), // 06: c
  KV(0,4), // 07: d
  KV(0,5), // 08: e
  KV(0,6), // 09: f
  KV(0,7), // 0a: g
  KV(1,0), // 0b: h
  KV(1,1), // 0c: i
  KV(1,2), // 0d: j
  KV(1,3), // 0e: k
  KV(1,4), // 0f: l
  KV(1,5), // 10: m
  KV(1,6), // 11: n
  KV(1,7), // 12: o
  KV(2,0), // 13: p
  KV(2,1), // 14: q
  KV(2,2), // 15: r
  KV(2,3), // 16: s
  KV(2,4), // 17: t
  KV(2,5), // 18: u
  KV(2,6), // 19: v
  KV(2,7), // 1a: w
  KV(3,0), // 1b: x
  KV(3,1), // 1c: y
  KV(3,2), // 1d: z

  // top number key row
  KV(4,1), // 1e: 1
  KV(4,2), // 1f: 2
  KV(4,3), // 20: 3
  KV(4,4), // 21: 4
  KV(4,5), // 22: 5
  KV(4,6), // 23: 6
  KV(4,7), // 24: 7
  KV(5,0), // 25: 8
  KV(5,1), // 26: 9
  KV(4,0), // 27: 0

  // other keys
  KV(6,0), // 28: return - ВК
  KV(7,3), // 29: esc - ПРФ
  KV(6,5), // 2a: backspace - ЗБ
  KV(6,6), // 2b: tab - ТАБ
  KV(6,7), // 2c: space

  KV(5,5), // 2d: -
  KV(3,6), // 2e: = -> ^
  KV(3,3), // 2f: [
  KV(3,5), // 30: ]
  KV(3,4), // 31: backslash
  KV(3,4), // 32: EUR-1 (the ISO keyboard's key next to Enter) -> backslash
  KV(5,3), // 33: ;
  KV(5,2), // 34: ' -> :
  KV(0,0), // 35: `(~) -> @
  KV(5,4), // 36: ,
  KV(5,6), // 37: .
  KV(5,7), // 38: /
  KV(7,6), // 39: caps lock -> ФИКС

  // function keys
  KX(2,0), // 3a: F1
  KX(2,1), // 3b: F2
  KX(2,2), // 3c: F3
  KX(2,3), // 3d: F4
  KX(2,4), // 3e: F5
  KV(3,7), // 3f: F6 -> _
  KV(6,2), // 40: F7 -> СТОП
  KV(7,6), // 41: F8 -> ФИКС
  KV(7,4), // 42: F9 -> СЕЛ
  KV(7,2), // 43: F10 -> ГРФ
  KV(7,1), // 44: F11 -> АЛФ
  MISS,    // 45: F12 - the OSD key, never sent to the core

  MISS,    // 46: PrtScr
  MISS,    // 47: Scroll Lock
  KV(6,2), // 48: Pause -> СТОП
  KV(6,3), // 49: Insert -> ВЗ
  KX(0,7), // 4a: Home
  KX(0,0), // 4b: PageUp -> page-home
  KV(6,4), // 4c: Delete -> ИЗ
  KX(1,1), // 4d: End
  KX(1,6), // 4e: PageDown -> page-end

  // cursor keys
  KX(0,6), // 4f: right
  KX(0,4), // 50: left
  KX(0,2), // 51: down
  KX(1,0), // 52: up

  MISS,    // 53: Num Lock

  // keypad
  KV(6,4), // 54: KP / -> ИЗ
  KV(6,3), // 55: KP * -> ВЗ
  KV(6,1), // 56: KP - -> СТРН
  MISS,    // 57: KP +
  KV(6,0), // 58: KP Enter -> ВК
  KX(0,7), // 59: KP 1 -> home
  KX(0,2), // 5a: KP 2 -> down
  KX(1,1), // 5b: KP 3 -> end
  KX(0,4), // 5c: KP 4 -> left
  KX(0,5), // 5d: KP 5 -> МЕНЮ
  KX(0,6), // 5e: KP 6 -> right
  KX(0,1), // 5f: KP 7 -> home-up-left
  KX(1,0), // 60: KP 8 -> up
  KX(0,3), // 61: KP 9 -> end-down-right
  KX(0,0), // 62: KP 0 -> page-home
  KX(1,6), // 63: KP . -> page-end
  MISS     // 64: EUR-2
};

static const unsigned char modifier_korvet[] = {
  /* usb modifer bits:
     0     1      2    3    4     5      6    7
     LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
  */

  KV(7,5), // ctrl - УПР
  KV(7,0), // lshift - РГ
  KV(7,2), // alt - ГРФ
  MISS,    // lgui
  KV(7,5), // ctrl (right) - УПР
  KV(7,7), // rshift - РГ (the right one)
  KV(7,1), // alt (right) - АЛФ
  MISS     // rgui
};
