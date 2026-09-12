//
// pk8000.h
//
// USB HID to PK8000 (Сура) translation table
//
// The machine's keyboard is a 10-row x 8-column matrix, scanned by the
// i8080 through the 8255 (Emu80's Pk8000Keyboard is the reference
// layout).  A code here is row*8 + column + 1, 1..80; 0 means the key
// does not exist on the machine (MISS).  The core takes the code as a
// press and 0x80 | code as the release, the generic MiSTeryNano way
// (usb_host.c's kbd_tx), and clears or sets the matrix bit itself, so
// any number of keys can be down at once.
//
//   row 0: 0 1 2 3 4 5 6 7
//   row 1: 8 9 , - . : ; /
//   row 2: [ \ ] ^ _ @ A B
//   row 3: C D E F G H I J
//   row 4: K L M N O P Q R
//   row 5: S T U V W X Y Z
//   row 6: SHIFT(РГ) CTRL(УПР) GRAPH(ГРФ) LANG(АЛФ) FIX F1 F2 F3
//   row 7: F4 F5 ESC TAB STOP BACKSPACE(ЗБ) SEL ENTER
//   row 8: SPACE CLEAR(СТРН) INS DEL LEFT UP DOWN RIGHT
//   row 9: HOME-UP-LEFT END-DOWN-RIGHT MENU HOME END PAGE-END PAGE-HOME -
//

#ifndef MISS
#define MISS          (0)
#endif
#define PK(row,col)   ((row)*8+(col)+1)

static const unsigned char keymap_pk8000[] = {
  MISS,         // 00: NoEvent
  MISS,         // 01: Overrun Error
  MISS,         // 02: POST fail
  MISS,         // 03: ErrorUndefined

  // characters
  PK(2,6), // 04: a
  PK(2,7), // 05: b
  PK(3,0), // 06: c
  PK(3,1), // 07: d
  PK(3,2), // 08: e
  PK(3,3), // 09: f
  PK(3,4), // 0a: g
  PK(3,5), // 0b: h
  PK(3,6), // 0c: i
  PK(3,7), // 0d: j
  PK(4,0), // 0e: k
  PK(4,1), // 0f: l
  PK(4,2), // 10: m
  PK(4,3), // 11: n
  PK(4,4), // 12: o
  PK(4,5), // 13: p
  PK(4,6), // 14: q
  PK(4,7), // 15: r
  PK(5,0), // 16: s
  PK(5,1), // 17: t
  PK(5,2), // 18: u
  PK(5,3), // 19: v
  PK(5,4), // 1a: w
  PK(5,5), // 1b: x
  PK(5,6), // 1c: y
  PK(5,7), // 1d: z

  // top number key row
  PK(0,1), // 1e: 1
  PK(0,2), // 1f: 2
  PK(0,3), // 20: 3
  PK(0,4), // 21: 4
  PK(0,5), // 22: 5
  PK(0,6), // 23: 6
  PK(0,7), // 24: 7
  PK(1,0), // 25: 8
  PK(1,1), // 26: 9
  PK(0,0), // 27: 0

  // other keys
  PK(7,7), // 28: return - ENTER (ВК)
  PK(7,2), // 29: esc
  PK(7,5), // 2a: backspace - ЗБ
  PK(7,3), // 2b: tab
  PK(8,0), // 2c: space

  PK(1,3), // 2d: -
  PK(2,3), // 2e: = -> ^
  PK(2,0), // 2f: [
  PK(2,2), // 30: ]
  PK(2,1), // 31: backslash
  PK(2,1), // 32: EUR-1 (the ISO keyboard's key next to Enter) -> backslash
  PK(1,6), // 33: ;
  PK(1,5), // 34: ' -> :
  PK(2,5), // 35: `(~) -> @
  PK(1,2), // 36: ,
  PK(1,4), // 37: .
  PK(1,7), // 38: /
  PK(6,4), // 39: caps lock -> FIX

  // function keys
  PK(6,5), // 3a: F1
  PK(6,6), // 3b: F2
  PK(6,7), // 3c: F3
  PK(7,0), // 3d: F4
  PK(7,1), // 3e: F5
  PK(2,4), // 3f: F6 -> _
  PK(7,4), // 40: F7 -> STOP
  PK(6,4), // 41: F8 -> FIX
  PK(6,3), // 42: F9 -> LANG (АЛФ)
  PK(6,2), // 43: F10 -> GRAPH (ГРФ)
  PK(7,6), // 44: F11 -> SEL
  MISS,    // 45: F12 - the OSD key, never sent to the core

  MISS,    // 46: PrtScr
  MISS,    // 47: Scroll Lock
  PK(7,4), // 48: Pause -> STOP
  PK(8,2), // 49: Insert -> INS
  PK(9,3), // 4a: Home -> HOME
  PK(9,6), // 4b: PageUp -> PAGE-HOME
  PK(8,3), // 4c: Delete -> DEL
  PK(9,4), // 4d: End -> END
  PK(9,5), // 4e: PageDown -> PAGE-END

  // cursor keys
  PK(8,7), // 4f: right
  PK(8,4), // 50: left
  PK(8,6), // 51: down
  PK(8,5), // 52: up

  MISS,    // 53: Num Lock

  // keypad
  PK(8,3), // 54: KP / -> DEL
  PK(8,2), // 55: KP * -> INS
  PK(8,1), // 56: KP - -> CLEAR (СТРН)
  MISS,    // 57: KP +
  PK(7,7), // 58: KP Enter -> ENTER
  PK(9,3), // 59: KP 1 -> HOME
  PK(8,6), // 5a: KP 2 -> down
  PK(9,1), // 5b: KP 3 -> END-DOWN-RIGHT
  PK(9,3), // 5c: KP 4 -> HOME
  PK(9,2), // 5d: KP 5 -> MENU
  PK(9,4), // 5e: KP 6 -> END
  PK(9,0), // 5f: KP 7 -> HOME-UP-LEFT
  PK(8,5), // 60: KP 8 -> up
  PK(9,1), // 61: KP 9 -> END-DOWN-RIGHT
  PK(9,5), // 62: KP 0 -> PAGE-END
  PK(9,5), // 63: KP . -> PAGE-END
  MISS     // 64: EUR-2
};

static const unsigned char modifier_pk8000[] = {
  /* usb modifer bits:
     0     1      2    3    4     5      6    7
     LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI
  */

  PK(6,1), // ctrl - CTRL (УПР)
  PK(6,0), // lshift - SHIFT (РГ)
  PK(6,2), // alt - GRAPH (ГРФ)
  MISS,    // lgui
  PK(6,1), // ctrl (right) - CTRL (УПР)
  PK(6,0), // rshift - SHIFT (РГ)
  PK(6,3), // alt (right) - LANG (АЛФ)
  MISS     // rgui
};
