//
// bk.h
//
// USB HID to БК-0011М keyboard translation.
//
// The machine's keyboard delivers one 7-bit КОИ-7 code a key into
// 177662 (the core's keyboard.v), with a flag for the АР2 chord that
// sends the interrupt through vector 274 instead of 60, and it keeps
// the РУС/ЛАТ and СТР (lower case) states itself.  All of that is done
// here, on the MCU, from the USB report: bk.c's kbd_tx_bk() takes a key
// up or down with the modifier byte and sends the core two bytes - the
// code and the flags - through hid.v's keyboard event.
//
// The tables give a key's code for the plain and the shifted key in
// ЛАТ mode; letters are adjusted for РУС and СТР as the machine does it
// (MiSTer's BK0011M keyboard.sv is the reference: a lower-case ASCII
// letter minus (rus ^ caps), an upper-case one plus it, where rus is
// 0x20 in ЛАТ and 0 in РУС), and with СУ (control) the letter's low five
// bits go.  A value with bit 7 set is a code with АР2 implied (ПОВТ,
// ГРАФ, ИНДСУ, БЛОК РЕД, ШАГ, -!->), and the values above BK_ACTION are
// not codes but actions: the СТОП key (a radial interrupt), СБР (a
// reset), CAPS LOCK (the СТР toggle), and the two hotkeys of the AZBK
// (АР2+ЛАТ: legacy 512/256 switch, АР2+РУС: palette reset).
//
//   Esc КТ  F1 ПОВТ  F2 ВС  F3 ГРАФ  F5 -!->  F6 ИНДСУ  F7 БЛОК РЕД
//   F8 ШАГ  F9 СБР  F10 СТОП  F11 reset (СБР key)  F12 the OSD
//   Ins |-->  Del |<--  Home/PgUp/End/PgDn the diagonal arrows
//   Shift+Enter УСТ ТАБ  Shift+Tab СБР ТАБ  Left Ctrl РУС  Win ЛАТ
//   Right Ctrl СУ  Alt АР2  Caps Lock СТР
//

#ifndef BK_H
#define BK_H

#define BK_AR2        0x80         // a code with АР2 implied
#define BK_ACTION     0xF0         // from here up: not a code
#define BK_A_STOP     0xF0         // СТОП
#define BK_A_RESET    0xF1         // СБР (the reset key)
#define BK_A_CAPS     0xF2         // СТР toggle
#define BK_A_RUS      0xF3         // РУС: code 016 and the state
#define BK_A_LAT      0xF4         // ЛАТ: code 017 and the state
#define BK_NONE       0x00

// the flags byte of a keyboard event (keyboard.v)
#define BKF_RELEASE   0x01
#define BKF_AR2       0x02
#define BKF_STOP      0x04
#define BKF_RESET     0x08
#define BKF_HOTKEY    0x10

void kbd_tx_bk(spi_t *spi, unsigned char hid, unsigned char modifiers, char pressed);

#endif // BK_H
