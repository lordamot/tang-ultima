# Tang Ultima: прошивка с нуля под Windows

Команды ниже выполняются в **PowerShell из корня репозитория**. Нужны
BLFlashCube (его консольная программа **BLFlashCommand.exe**) и Gowin
Programmer 1.9.9 (**programmer_cli.exe**). GUI `BLFlashCube.exe` и
`programmer.exe` для этих команд не используются. Оставьте рядом с EXE
остальные файлы их дистрибутивов; установите USB-драйверы из комплектов.

Прошиваются три устройства, причём собственный BL616 платы получает
**два** файла:

| Устройство | Файл относительно корня репозитория | Адрес в его flash |
| --- | --- | --- |
| BL616 на Tang Nano 20K, FPGA Partner | `bin\onboard\bl616_fpga_partner_20kNano.bin` | `0x000000` |
| Тот же BL616, наша вторая ступень | `bin\onboard\stage2.bin` | `0x040000` |
| Внешняя flash ПЛИС, начальное ядро УКНЦ | `bin\uknc.fs` | `0x000000` |
| Отдельный BL616 на M0S Dock | `bin\bl616.bin` | `0x000000` |

Готовая вторая ступень — **`bin\onboard\stage2.bin`**. При пересборке
команда `make onboard-fw` создаёт `build\onboard\stage2.bin`; перед
прошивкой обновите поставляемую копию в `bin\onboard\stage2.bin`.
`bin\onboard\fpga_companion_nano20k.bin` — другая прошивка, из upstream,
и нашей второй ступенью не является. Прошивка дока `bin\bl616.bin` также
не заменяет `stage2.bin`.

## 1. Подготовить команды и конфигурации

Укажите свои пути к EXE. Путь `C:\tang-ultima` замените путём к клону;
дальнейшие пути к образам остаются относительно его корня. Для INI
используются абсолютные пути, вычисленные из этих относительных путей,
чтобы загрузчик не искал образ в каталоге BLFlashCube. Для совместимости
с кодировкой INI используйте путь к клону без кириллицы.

```powershell
Set-Location 'C:\tang-ultima'
$BL = 'C:\BLFlashCube\BLFlashCommand.exe'
$Gowin = 'C:\Gowin\Programmer\bin\programmer_cli.exe'
$Repo = (Get-Location).Path

New-Item -ItemType Directory -Force 'build\windows' | Out-Null
New-Item -ItemType Directory -Force 'bin\onboard\backup' | Out-Null

$Images = @(
    @('partner', 'bin\onboard\bl616_fpga_partner_20kNano.bin', '0x000000'),
    @('stage2', 'bin\onboard\stage2.bin', '0x040000'),
    @('dock', 'bin\bl616.bin', '0x000000')
)
foreach ($Image in $Images) {
    $ImagePath = (Resolve-Path -LiteralPath $Image[1] -ErrorAction Stop).Path
    @"
[cfg]
erase = 1
skip_mode = 0x0, 0x0
boot2_isp_mode = 0

[FW]
filedir = $ImagePath
address = $($Image[2])
"@ | Set-Content -Encoding ASCII -LiteralPath "build\windows\$($Image[0]).ini"
}
```

Получатся три INI: `build\windows\partner.ini`, `stage2.ini`, `dock.ini`.
`erase = 1` стирает область записываемого файла. **Не заменяйте на
`erase = 2`**: это стирание всего чипа, которое уничтожит соседнюю прошивку.
Настройки соответствуют целям прошивки в `Makefile`.

## 2. Собственный BL616 платы Tang: резервная копия и efuse

Отключите M0S Dock. Подключайте к PC только Tang Nano 20K, без питания
через другие соединения. На обесточенной Tang зажмите **UPDATE**, подключите
USB-C к PC, отпустите UPDATE. Это ROM-загрузчик BL616, а не режим
программирования ПЛИС. В диспетчере устройств найдите его COM-порт
(`Bouffalo CDC DEMO`, USB VID:PID `ffff:0005`). Подставьте свой номер:

```powershell
$TangPort = 'COM7'
$BackupStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$Backup = "$Repo\bin\onboard\backup\tang-windows-$BackupStamp.bin"
$Efuse = "$Repo\bin\onboard\backup\efuse-windows-$BackupStamp.bin"

& $BL --interface=uart --baudrate=2000000 --port=$TangPort --chipname=bl616 --cpu_id= --flash --read --start=0x0 --len=0x400000 --file="$Backup"
```

Дождитесь успешного чтения. Здесь сохраняются все 4 МБ flash собственного
BL616 этой платы. Файл сохраните отдельно; копия чужой платы его не заменяет.
Перед следующим вызовом при необходимости снова войдите в ROM-загрузчик
через UPDATE и проверьте номер COM-порта.

```powershell
& $BL --interface=uart --baudrate=2000000 --port=$TangPort --chipname=bl616 --cpu_id= --efuse --read --start=0x0 --len=0x200 --file="$Efuse"
```

Проверить режим шифрования можно проектным декодером, если установлен Python:

```powershell
py -3 tools\efuse_bl616.py "$Efuse"
```

Без Python посмотрите начало дампа:

```powershell
Format-Hex -Path $Efuse | Select-Object -First 4
```

В двоичном дампе `EF_CFG_0` — первые четыре байта, little-endian;
`ef_sf_aes_mode` — младшие два бита первого байта. Для платы, описанной
в документации проекта, слово равно `0x00000431` (байты `31 04 00 00`),
режим AES равен 1. Декодер также умеет читать текстовый hex-дамп.

**Продолжайте с FPGA Partner только при `FUSED` / ненулевом AES mode.**
Partner зашифрован и на чипе без соответствующего efuse не запускается.
При `NOT FUSED`, пустом или сомнительном дампе эту последовательность
останавливают: для такой платы готового варианта Partner в проекте нет.
Здесь efuse только читается, записывать его для этой инструкции не нужно.
Файл `bin\onboard\backup\efuse.bin` — сохранённый дамп платы автора,
а не файл для прошивки: проверяйте собственную плату. Наша `stage2.bin`
записывается без шифрования по адресу `0x040000` за Partner; зашифрованный
Partner по адресу 0 использует уже настроенный на заводе BL616.

## 3. Собственный BL616 платы Tang: Partner и stage 2

Tang по-прежнему единственная плата на USB PC, в ROM-загрузчике через
UPDATE. Сначала Partner в начало flash:

```powershell
& $BL --interface=uart --baudrate=2000000 --port=$TangPort --chipname=bl616 --cpu_id= --config="$Repo\build\windows\partner.ini"
```

После сообщения об успешной записи отключите USB, снова подключите с
зажатой UPDATE, отпустите её. При изменении COM-порта обновите `$TangPort`.
Теперь **наша** вторая ступень по адресу `0x040000`:

```powershell
& $BL --interface=uart --baudrate=2000000 --port=$TangPort --chipname=bl616 --cpu_id= --config="$Repo\build\windows\stage2.ini"
```

После успешной записи отключите и подключите Tang **без UPDATE**.
На PC должен запуститься USB-JTAG-программатор Partner с VID:PID
`0403:6010`. Вторая ступень при питании от PC не запускается — так
устроен Partner. Она понадобится при автономном питании.

## 4. ПЛИС Tang: начальное ядро во внешнюю flash

Tang подключена к PC без UPDATE, док отключён. Закройте GUI Programmer,
BLFlashCube и терминалы, которые могли занять устройство. Проверьте CLI:

```powershell
& $Gowin --help
& $Gowin --scan-cables
& $Gowin --scan --cable-index 1 --channel 0 --frequency 2.5MHz
```

`--cable-index 1` — `Gowin USB Cable(FT2CH)`, канал 0 — JTAG.
Это режим FT2232, который эмулирует Partner. При отсутствии кабеля
проверьте драйвер из комплекта Programmer; COM-порт из предыдущего
раздела для JTAG не используется. Должна определиться ПЛИС семейства
GW2A(R)-18(C), IDCODE `0x0000081b`.

Записать УКНЦ по адресу 0 и проверить запись:

```powershell
& $Gowin --device GW2AR-18C --run 13 --cable-index 1 --channel 0 --frequency 2.5MHz --spiaddr 0x000000 --fsFile "$Repo\bin\uknc.fs"
```

`--run 13` — **exFlash Erase,Program,Verify in bscan**. Это внешняя
flash ПЛИС, не flash BL616. В справке своей поставки 1.9.9 проверьте это
название операции и наличие `GW2AR-18C`. Если поставка перечисляет только
`GW2AR-18`, используйте это имя в `--device`.

Для Gowin здесь используется **`.fs`**. Файл `bin\uknc.bin` — тот же
битстрим, упакованный для openFPGALoader и установки из OSD; не подставляйте
его вместо `.fs` в этот вызов. `--run 2` загружает только SRAM и для
сохранения ядра после выключения не подходит.

Для другого начального ядра выполните **одну** такую же команду с заменой
аргумента `--fsFile`:

| Машина | Файл для Gowin | Соответствующий файл для SD-карты |
| --- | --- | --- |
| УКНЦ | `bin\uknc.fs` | `bin\uknc.bin` |
| ПК8000 | `bin\pk8000.fs` | `bin\pk8000.bin` |
| Корвет | `bin\korvet.fs` | `bin\korvet.bin` |
| ZS-256 | `bin\zs256.fs` | `bin\zs256.bin` |
| БК-0011М | `bin\bk.fs` | `bin\bk.bin` |

Во flash находится одно начальное ядро по адресу 0; остальные выбираются
с карты. После успешных записи и проверки полностью выключите и включите
питание Tang. При ошибке проверки не считайте прошивку завершённой.

## 5. Отдельный BL616: M0S Dock

Отключите Tang от PC и питания. Подключите к PC только M0S Dock.
Зажмите на доке **BOOT**, нажмите и отпустите **RESET**, отпустите BOOT.
Найдите COM-порт загрузчика дока; он может отличаться от порта Tang:

```powershell
$DockPort = 'COM8'
& $BL --interface=uart --baudrate=2000000 --port=$DockPort --chipname=bl616 --cpu_id= --config="$Repo\build\windows\dock.ini"
```

Это записывает **`bin\bl616.bin` по адресу 0 отдельного BL616**.
`build\fw\bl616.bin` — результат локальной сборки; в этой инструкции
используется именно поставляемый файл из `bin`. После успешной записи
перезапустите док без BOOT и отключите его от PC.

## 6. Карта и первый запуск

На SD-карту FAT32 скопируйте все пять `bin\<имя>.bin` в каталог
`/cores/`: `uknc.bin`, `pk8000.bin`, `korvet.bin`, `zs256.bin`, `bk.bin`.
В `/ultima.ini` укажите начальное ядро, например для команды выше:

```ini
core=uknc
```

Файлы каждой машины лежат в её каталоге: `/uknc/`, `/pk8000/`,
`/korvet/`, `/zs256/`, `/bk/`. Для ZS-256 обязательны
`/zs256/zs256.rom` и `/zs256/gs105a.rom`; для БК — пакет AZBK с
`/bk/AZ.INI` и указанными в нём ROM. Подробнее — [README](README.md#sd-карта).

Соедините платы по распиновке проекта, вставьте карту и клавиатуру,
подайте питание **от блока питания, без USB-подключения к PC**.
Начальное ядро загрузится из flash ПЛИС. F12 → Core выбирает другое
ядро с карты; Save to flash сохраняет его как начальное. Пока идёт
Save to flash и показано «Do not switch off!», питание не отключать.
При PC-питании Partner остаётся программатором и переключение недоступно.

## Восстановление собственного BL616 Tang из своей копии

Если Partner не стартует, ROM-загрузчик остаётся доступен: отключите
док, подключите Tang к PC с зажатой UPDATE. Укажите **свой** сохранённый
дамп и актуальный COM-порт. Сначала создайте INI:

```powershell
$TangPort = 'COM7'
$Backup = (Resolve-Path 'bin\onboard\backup\tang-windows-YYYYMMDD-HHMMSS.bin').Path
@"
[cfg]
erase = 1
skip_mode = 0x0, 0x0
boot2_isp_mode = 0

[FW]
filedir = $Backup
address = 0x000000
"@ | Set-Content -Encoding ASCII 'build\windows\restore.ini'

& $BL --interface=uart --baudrate=2000000 --port=$TangPort --chipname=bl616 --cpu_id= --config="$Repo\build\windows\restore.ini"
```

После успешной записи переподключите без UPDATE. Восстанавливается flash
BL616; flash ПЛИС и прошивка дока этой командой не затрагиваются.

## Основания и границы проверки

Адреса, INI и параметры BLFlashCommand сверены с `Makefile`,
[описанием onboard](.claude/docs/onboard.md) и справкой загрузчика.
Операция Gowin и параметры CLI сверены со справкой установленной здесь
версии 1.9.11.03 и
[руководством Gowin Programmer, раздел 4](https://cdn.gowinsemi.com.cn/SUG502E.pdf).
Применение `.fs` для внешней flash описано также в
[инструкции Sipeed для Nano 20K](https://wiki.sipeed.com/hardware/en/tang/tang-nano-20k/example/led.html#Burn-into-Flash).
**Эти Windows-команды с Programmer 1.9.9 на плате в рамках подготовки
инструкции не выполнялись.** Проверенная на плате история проекта относится
к Linux-инструментам и указана в документации onboard.
