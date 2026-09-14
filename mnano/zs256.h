//
// zs256.h
//
// USB HID to Scorpion ZS-256 (ZX Spectrum) translation table
//
// The machine's keyboard is the Spectrum's 8 x 5 matrix, read through
// port FE with the row on A15..A8 (the core's keyboard.v).  A code here
// is row * 5 + column + 1, 1..40; 41..80 is the same key with Caps Shift
// held for it and 81..120 with Symbol Shift, so that a PC key with no
// place on the machine - Backspace, the cursor keys, the punctuation -
// is one of the machine's two-key chords; 121 is Caps Shift with Symbol
// Shift (extended mode); 0 means the key does not exist on the machine
// (MISS).  The core takes the code as a press and 0x80 | code as the
// release, the generic MiSTeryNano way (usb_host.c's kbd_tx), and keeps
// the matrix itself.
//
//   row 0: CS Z X C V     row 4: 0 9 8 7 6
//   row 1: A  S D F G     row 5: P O I U Y
//   row 2: Q  W E R T     row 6: EN L K J H
//   row 3: 1  2 3 4 5     row 7: SP SS M N B
//
// Shift is Caps Shift, Ctrl and Alt are Symbol Shift, so the machine's
// own chords work from the PC keys (Shift+1 is EDIT, Ctrl+P is ", as on
// the machine's keyboard); the PC's unshifted punctuation keys give the
// symbols they carry (- = ; ' , . /) through Symbol Shift chords.
//

#ifndef MISS
#define MISS          (0)
#endif
#define ZK(row,col)   ((row)*5+(col)+1)
#define ZCS(row,col)  (40+ZK(row,col))
#define ZSS(row,col)  (80+ZK(row,col))
#define ZEXT          (121)

static const unsigned char keymap_zs256[] = {
  MISS,         // 00: NoEvent
  MISS,         // 01: Overrun Error
  MISS,         // 02: POST fail
  MISS,         // 03: ErrorUndefined

  // characters
  ZK(1,0),  // 04: a
  ZK(7,4),  // 05: b
  ZK(0,3),  // 06: c
  ZK(1,2),  // 07: d
  ZK(2,2),  // 08: e
  ZK(1,3),  // 09: f
  ZK(1,4),  // 0a: g
  ZK(6,4),  // 0b: h
  ZK(5,2),  // 0c: i
  ZK(6,3),  // 0d: j
  ZK(6,2),  // 0e: k
  ZK(6,1),  // 0f: l
  ZK(7,2),  // 10: m
  ZK(7,3),  // 11: n
  ZK(5,1),  // 12: o
  ZK(5,0),  // 13: p
  ZK(2,0),  // 14: q
  ZK(2,3),  // 15: r
  ZK(1,1),  // 16: s
  ZK(2,4),  // 17: t
  ZK(5,3),  // 18: u
  ZK(0,4),  // 19: v
  ZK(2,1),  // 1a: w
  ZK(0,2),  // 1b: x
  ZK(5,4),  // 1c: y
  ZK(0,1),  // 1d: z

  // top number key row
  ZK(3,0),  // 1e: 1
  ZK(3,1),  // 1f: 2
  ZK(3,2),  // 20: 3
  ZK(3,3),  // 21: 4
  ZK(3,4),  // 22: 5
  ZK(4,4),  // 23: 6
  ZK(4,3),  // 24: 7
  ZK(4,2),  // 25: 8
  ZK(4,1),  // 26: 9
  ZK(4,0),  // 27: 0

  // other keys
  ZK(6,0),  // 28: return
  ZCS(7,0), // 29: esc         -> BREAK (Caps Shift + Space)
  ZCS(4,0), // 2a: backspace   -> DELETE (Caps Shift + 0)
  ZEXT,     // 2b: tab         -> extended mode (Caps Shift + Symbol Shift)
  ZK(7,0),  // 2c: space
  ZSS(6,3), // 2d: -           -> Symbol Shift + J
  ZSS(6,1), // 2e: =           -> Symbol Shift + L
  MISS,     // 2f: [
  MISS,     // 30: ]
  MISS,     // 31: backslash
  MISS,     // 32: EUR-1
  ZSS(5,1), // 33: ;           -> Symbol Shift + O
  ZSS(4,3), // 34: '           -> Symbol Shift + 7
  MISS,     // 35: `
  ZSS(7,3), // 36: ,           -> Symbol Shift + N
  ZSS(7,2), // 37: .           -> Symbol Shift + M
  ZSS(0,4), // 38: /           -> Symbol Shift + V
  ZCS(3,1), // 39: caps lock   -> CAPS LOCK (Caps Shift + 2)

  // function keys
  MISS,     // 3a: F1
  MISS,     // 3b: F2
  MISS,     // 3c: F3
  MISS,     // 3d: F4
  MISS,     // 3e: F5
  MISS,     // 3f: F6
  MISS,     // 40: F7
  MISS,     // 41: F8
  MISS,     // 42: F9
  MISS,     // 43: F10
  MISS,     // 44: F11
  MISS,     // 45: F12        the OSD's, never sent

  MISS,     // 46: PrtScr
  MISS,     // 47: Scroll Lock
  ZCS(7,0), // 48: Pause      -> BREAK
  ZCS(4,1), // 49: Insert     -> GRAPHICS (Caps Shift + 9)
  ZCS(3,0), // 4a: Home       -> EDIT (Caps Shift + 1)
  ZCS(3,2), // 4b: PageUp     -> TRUE VIDEO (Caps Shift + 3)
  ZCS(4,0), // 4c: Delete     -> DELETE
  MISS,     // 4d: End
  ZCS(3,3), // 4e: PageDown   -> INV VIDEO (Caps Shift + 4)
  ZCS(4,2), // 4f: right      -> Caps Shift + 8
  ZCS(3,4), // 50: left       -> Caps Shift + 5
  ZCS(4,4), // 51: down       -> Caps Shift + 6
  ZCS(4,3), // 52: up         -> Caps Shift + 7

  // keypad
  MISS,     // 53: Num Lock
  ZSS(0,4), // 54: KP /       -> Symbol Shift + V
  ZSS(7,4), // 55: KP *       -> Symbol Shift + B
  ZSS(6,3), // 56: KP -       -> Symbol Shift + J
  ZSS(6,2), // 57: KP +       -> Symbol Shift + K
  ZK(6,0),  // 58: KP Enter
  ZK(3,0),  // 59: KP 1
  ZK(3,1),  // 5a: KP 2
  ZK(3,2),  // 5b: KP 3
  ZK(3,3),  // 5c: KP 4
  ZK(3,4),  // 5d: KP 5
  ZK(4,4),  // 5e: KP 6
  ZK(4,3),  // 5f: KP 7
  ZK(4,2),  // 60: KP 8
  ZK(4,1),  // 61: KP 9
  ZK(4,0),  // 62: KP 0
  ZSS(7,2), // 63: KP .       -> Symbol Shift + M
  MISS,     // 64: EUR-2
};

static const unsigned char modifier_zs256[] = {
  /* usb modifer bits:
     0     1     2    3    4     5     6    7
     LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI */

  ZK(7,1),  // ctrl   -> Symbol Shift
  ZK(0,0),  // lshift -> Caps Shift
  ZK(7,1),  // lalt   -> Symbol Shift
  MISS,     // lgui
  ZK(7,1),  // rctrl  -> Symbol Shift
  ZK(0,0),  // rshift -> Caps Shift
  ZK(7,1),  // ralt   -> Symbol Shift
  MISS      // rgui
};
