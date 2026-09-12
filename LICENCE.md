# MIT License

Copyright (c) 2026 Sergei Lemeshev and contributors (Tang Ultima)
Copyright (c) 2026 Sergei Lemeshev and contributors (Korvet Nano and
PK8000 Nano: the firmware lineage, the method, the tools)
Copyright (c) 2024-2026 Alexey Gurov, Sergei Lemeshev and contributors
(the parts taken from UKNC Nano: the MCU link, the HDMI encoder, the
tools and the firmware)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

# Лицензия MIT

Copyright (c) 2026 Сергей Лемешев и участники (Korvet Nano)
Copyright (c) 2026 Сергей Лемешев и участники (PK8000 Nano: метод,
расписание SDRAM, инструменты)
Copyright (c) 2024-2026 Алексей Гуров, Сергей Лемешев и участники
(части, взятые из UKNC Nano: связь с МК, HDMI-кодер, инструменты и прошивка)

Данная лицензия разрешает лицам, получившим копию данного программного
обеспечения и сопутствующей документации (в дальнейшем именуемыми
«Программное обеспечение»), безвозмездно использовать Программное
обеспечение без ограничений, включая неограниченное право на использование,
копирование, изменение, слияние, публикацию, распространение,
сублицензирование и/или продажу копий Программного обеспечения, а также
лицам, которым предоставляется данное Программное обеспечение, при
соблюдении следующих условий:

Указанное выше уведомление об авторском праве и данные условия должны быть
включены во все копии или значимые части данного Программного обеспечения.

ДАННОЕ ПРОГРАММНОЕ ОБЕСПЕЧЕНИЕ ПРЕДОСТАВЛЯЕТСЯ «КАК ЕСТЬ», БЕЗ КАКИХ-ЛИБО
ГАРАНТИЙ, ЯВНО ВЫРАЖЕННЫХ ИЛИ ПОДРАЗУМЕВАЕМЫХ, ВКЛЮЧАЯ ГАРАНТИИ ТОВАРНОЙ
ПРИГОДНОСТИ, СООТВЕТСТВИЯ ПО ЕГО КОНКРЕТНОМУ НАЗНАЧЕНИЮ И ОТСУТСТВИЯ
НАРУШЕНИЙ, НО НЕ ОГРАНИЧИВАЯСЬ ИМИ. НИ В КАКОМ СЛУЧАЕ АВТОРЫ ИЛИ
ПРАВООБЛАДАТЕЛИ НЕ НЕСУТ ОТВЕТСТВЕННОСТИ ПО КАКИМ-ЛИБО ИСКАМ, ЗА УЩЕРБ ИЛИ
ПО ИНЫМ ТРЕБОВАНИЯМ, В ТОМ ЧИСЛЕ ПРИ ДЕЙСТВИИ КОНТРАКТА, ДЕЛИКТЕ ИЛИ ИНОЙ
СИТУАЦИИ, ВОЗНИКШИМ ИЗ-ЗА ИСПОЛЬЗОВАНИЯ ПРОГРАММНОГО ОБЕСПЕЧЕНИЯ ИЛИ ИНЫХ
ДЕЙСТВИЙ С ПРОГРАММНЫМ ОБЕСПЕЧЕНИЕМ.

В случае расхождений между текстами английская версия имеет приоритет.

---

# Third-party notices

- `tang/src/korvet/vm80a.v` - the КР580ВМ80А core, copyright 2014-2018
  1801BM1@gmail.com, Creative Commons Attribution 3.0
  (`tang/src/korvet/vm80a-LICENSE.md`).  Used verbatim.
- `tang/src/korvet/tv80_*.v` - the Z80 core, copyright 2004 Guy
  Hutchison, based on Daniel Wallner's T80, MIT
  (`tang/src/korvet/tv80-LICENSE.md`).  One line changed in
  `tv80_core.v`, marked.
- `tang/src/korvet/wd1793.sv`, `ym2149.sv` - MiSTer's, by MikeJ and
  Sorgelig, as PK8000 Nano and UKNC Nano carried them.
- `tang/src/mister/`, `mnano/` - MiSTeryNano by Till Harbaum, as carried
  by UKNC Nano and PK8000 Nano; `mnano/u8g2` is olikraus's u8g2 (BSD).
- `mnano/extrom.c` reimplements the Korvet-EXTROM controller's program
  (forth32, Alexander Stepanov, and Sergey Erokhin's emulator patch,
  GPL v2) as the protocol it is; `tang/rom/stage1.rom` and the copy of
  it in `mnano/extrom.c` are that project's phase-1 loader, GPL v2, and
  come from its `SD_ROOT`.
- `tang/src/korvet/memmap.v` is generated from the PLM D31's equations
  as Славик drew them for the 2022 board (`infosource/ПЛМ_v1.zip`),
  which are the machine's own fuse map.
- `tang/rom/korvet20.rom`, `korvet11.rom`, `korvet2.fnt` are the
  Корвет's own firmware and character generator, of their original
  authors (МГУ / the Baku works, 1986-1988); `tang/rom/mapper.mem` is
  Emu80's (Viktor Pykhonin, GPL v3) table of that firmware's hardware.
