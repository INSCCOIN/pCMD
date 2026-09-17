# pCMD

**Dual-pane commander for SharkDeck / WalnutPi OS.**

`pCMD` is a small Norton Commander–style file manager written in C + ncurses. It is the C port of [sCMD](https://github.com/INSCCOIN/SharkDeck), built for the 3.5″ 480×320 SharkDeck terminal instead of a desktop window manager.

Left pane starts in the current directory. Right pane starts in `/home/working/SharkDeck/SharkDeck` when that path exists, otherwise `$HOME`.

```
pCMD needs a tty
```

If stdout is not a terminal, it refuses to start.

## Why C

sCMD is Python. On a WalnutPi Zero 2W (Allwinner H618, 1 GB RAM, Debian-based WalnutPi image) that costs startup time and RAM every time you open a file manager.

pCMD is one binary:

- no interpreter
- ncurses UI sized for 480×320
- copy / move / delete implemented in-process (no `cp`/`mv`/`rm` spawn except for the user menu)
- same key language as sCMD so muscle memory transfers

## Features

- Two independent panes with cursor, scroll, and sort
- Directories first, then files
- Sort by name, size, or date
- Hidden files toggle
- Substring filter (`/`)
- Multi-mark (space), up to 256 marks
- Copy / move to the other pane, or copy-in-place with a new name (`c`)
- Recursive directory copy and delete
- Rename, mkdir, chmod `+x`, symlink into the other pane
- Built-in text viewer (first 400 lines)
- External editor (`$EDITOR`, default `nano`)
- Preview the current file in the inactive pane (`p`)
- Jump list and user command menu from `~/.pcmd.*` (falls back to `~/.scmd.*`)
- Free-space line from `df -h .`
- Norton-style confirm prompts (`y` to confirm)

Limits baked into the binary:

| Limit | Value |
| --- | --- |
| Entries per pane | 1024 |
| Marks | 256 |
| Name length | 255 |
| Path length | 1023 |
| Viewer lines | 400 |
| Menu / jump rows | 32 |

## Requirements

On the official SharkDeck WalnutPi Zero 2W image (Debian, aarch64):

```bash
sudo apt update
sudo apt install -y build-essential libncurses-dev
```

`gcc`, `make` (optional), and ncurses headers are enough. No extra runtime beyond `libncurses`.

## Build

```bash
gcc -O2 -std=c11 -o pcmd pcmd.c -lncurses
```

Install somewhere on `PATH`:

```bash
sudo install -m 0755 pcmd /usr/local/bin/pcmd
```

Or keep it next to the rest of the deck tools:

```bash
mkdir -p /home/working/SharkDeck/bin
install -m 0755 pcmd /home/working/SharkDeck/bin/pcmd
```

## Run

```bash
pcmd                  # left = cwd, right = SharkDeck tree or $HOME
pcmd /tmp             # left starts in /tmp
pcmd ~
```

Must be run from a real tty (on-device console, `fbterm`/`kmscon`, or SSH).

Bind it from **sKEY** if you want a color key to open the commander. Example: leave red as F1 (help inside pCMD), and launch the binary from a shell alias or a menu entry rather than fighting `keyd`.

## Screen layout

```
/home/working          name          | /home/working/SharkDeck/SharkDeck name
 *notes.txt            12K           |  apps/                       <DIR>
  pcmd.c               48K           |  README.md                    8K
                                     |
notes.txt 09-17 00:12 *1 free 12G/29G
pCMD
F1? F3 view F4 ed F5 cp F6 mv F7 md F8 rm F9 menu / j s p x l
```

- Header: path, optional `/filter`, current sort
- List: `*` for marked rows, directories in yellow, cursor in cyan
- Status: name, mtime, mark count, disk free
- Message bar: last action or error
- Footer: compact keymap

`p` replaces the inactive pane with a text preview of the current file.

## Keys

### SharkDeck color row

The KeebDeck Basic top row is F1–F6. Inside pCMD those map as:

| Key | Color | Action |
| --- | --- | --- |
| F1 | red | Help |
| F2 | orange | Rename |
| F3 | yellow | View file / enter directory |
| F4 | green | Edit (`$EDITOR` or `nano`) |
| F5 | blue | Copy selection → other pane |
| F6 | purple | Move selection → other pane |

F7–F10 are the rest of the Norton row if your keymap exposes them (or use the letter shortcuts).

### Navigation

| Key | Action |
| --- | --- |
| Tab | Other pane |
| Up / Down | Move cursor |
| PgUp / PgDn | Page |
| Home / End | First / last entry |
| Enter | Open directory, or view file |
| Backspace | Parent directory |
| Space | Toggle mark and step down |
| q / F10 | Quit |

### Pane

| Key | Action |
| --- | --- |
| h | Toggle hidden files |
| / | Filter (substring, case-insensitive) |
| s | Cycle sort: name → size → date |
| j | Jump menu |
| p | Toggle preview in the other pane |

### File ops

| Key | Action |
| --- | --- |
| c | Copy inside the same pane (prompts for name) |
| l | Symlink selection into the other pane |
| x | `chmod +x` on selection |
| F2 | Rename |
| F3 | View |
| F4 | Edit |
| F5 | Copy → other pane |
| F6 | Move → other pane |
| F7 | mkdir |
| F8 | Delete (confirms) |
| F9 | User menu |

Selection is every marked entry, or the current row if nothing is marked. `..` is never marked or deleted.

Copy across filesystems falls back to copy-then-remove when `rename(2)` fails. Overwrite asks first.

## Config

Both files are `label|value` lines. `#` comments and blank lines are ignored. If `~/.pcmd.*` is missing, pCMD reads the matching `~/.scmd.*` file so existing sCMD config keeps working.

### `~/.pcmd.jumps`

```
home|/home/working
deck|/home/working/SharkDeck/SharkDeck
tmp|/tmp
root|/
sd|/mnt
```

`j` opens this list and jumps the **active** pane. With no file at all, the built-in list is `home`, `work` (`/home/working`), `tmp`, `root`.

### `~/.pcmd.menu`

```
htop|htop
disk|df -h
edit this|nano '%f'
term here|cd '%p' && bash
copy name|echo '%n'
```

F9 runs the chosen line after expanding:

| Token | Expands to |
| --- | --- |
| `%p` | Active pane path |
| `%d` | Other pane path |
| `%f` | Current file path (or pane path) |
| `%n` | Current entry name |

The command is run with ncurses suspended. Press Enter when it finishes to return to pCMD.

### Editor

```bash
export EDITOR=nano          # default if unset
export EDITOR=vi
export EDITOR=micro
```

Put that in `~/.bashrc` on the deck.

## sKEY / launcher notes

pCMD does not install its own key daemon and does not want `keyd`. Use **sKEY** for the color row.

Suggested split that does not steal F1 from help:

- Red stays F1 (pCMD help when pCMD is focused)
- Blue / F5 is copy *inside* pCMD; bind “open pCMD” on a shell alias or a menu entry instead of F5 if you already use blue to open `/home/working/SharkDeck/SharkDeck`
- Purple / F6 is move *inside* pCMD; keep `htop` on F9 menu or a jump rather than colliding with move

A typical alias:

```bash
alias fm='pcmd /home/working /home/working/SharkDeck/SharkDeck'
```

(The binary only takes the left path on the command line; the right pane is still the built-in SharkDeck default.)

## Colors

ncurses pairs used when the tty has color:

| Pair | Use |
| --- | --- |
| White on blue | Path header, status, prompts |
| Black on cyan | Active cursor |
| Yellow on black | Directories / preview |
| White on red | Errors |

On the 480×320 panel this is the same blue-header look as classic Midnight Commander, scaled to two narrow columns.

## Files

```
pcmd.c      source
README.md   this file
```

Single translation unit. No extra headers.

## Compatibility

Written against:

- SharkDeck Gen 1 — WalnutPi Zero 2W, Allwinner H618, 1 GB RAM
- 3.5″ 480×320 color panel
- Solder Party KeebDeck Basic (color F1–F6)
- Official WalnutPi Zero 2W image from [sharkdeck.dev/firmware](https://sharkdeck.dev/firmware)
- Docs: [sharkdeck.dev/docs](https://sharkdeck.dev/docs) (rev 4, 2026-08-19)

It is plain POSIX + ncurses, so it also builds on any Linux box with a tty. The default right-pane path and the 480-wide layout are what make it a SharkDeck app.

## License

Use and modify freely for personal use ONLY
