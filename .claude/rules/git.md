# Git Usage

## Branch Model

| Branch | Purpose                                         |
|--------|-------------------------------------------------|
| `main` | ready working solution                          |
| `feat/<name>` | Feature branches (short kebab-case name) |

## Workflow

Work onto mine branch - allowed.
Never push.
Always ask before commit something.

This repository builds the five sibling repositories (`../tang-uknc`,
`../tang-pk8000`, `../tang-korvet`, `../tang-zs256`, `../tang-bk-epta`)
and the reconfig support lives in their trees.  A change there is a commit THERE, under their own
`.claude/rules/git.md`, and asked for separately.

## Commit Messages

Short imperative subject line, no period. Examples:

```
hold the interrupt task across a core switch
document the flash layout
```

- Keep subject under 72 characters
- No ticket/issue prefix required
- English only

## What Not to Commit

- `/.idea/`, `/.vscode/` - editor state
- `/tools/` - the fetched toolchain, ~8 GB, restored by `make toolchain`;
  the scripts in it are force-added (`git add -f tools/*.py tools/*.sh
  tools/*.patch`) and a new script has to be too
- `/build/`, `/mnano/build/` - build products, the cores' PnR output
  among them (each core's `impl/pnr/` is under `build/cores/<core>/`;
  the siblings keep their own on record, this repository does not)

## What *is* committed on purpose

- **`bin/uknc.fs`, `bin/pk8000.fs`, `bin/korvet.fs`, `bin/zs256.fs`,
  `bin/bk.fs`** - the cores as Gowin writes them, and **`bin/uknc.bin`,
  `bin/pk8000.bin`, `bin/korvet.bin`, `bin/zs256.bin`, `bin/bk.bin`** -
  the same packed, which is what goes on the SD card
  and what the flash holds; plus **`bin/bl616.bin`**, the firmware.  So no
  toolchain is needed to use the board.  Rebuilt from the tree: `make
  cores` copies each `.fs` to `bin/` once its timing gate passes and packs
  the `.bin`; `make fw` leaves the firmware in `build/fw/` and it is
  copied on by hand.

  `bin/ultima.bin` is gone - it was the three-slot MultiBoot image, and
  there are no slots any more.
- **`VERSION`** - read into the OSD's caption by `mnano/CMakeLists.txt`.

## Working tree noise

The tree may show a long list of `mode change 100755 => 100644` entries
under `mnano/u8g2/`.  That is a checkout artefact inherited through the
siblings from UKNC Nano, not work.  Use `git diff --summary` to tell it
from a real edit.
