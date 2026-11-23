# fbDOOM (optional)

Builds [fbDOOM](https://github.com/maximevince/fbDOOM) from a local checkout
and adds a DOOM tile to the HMI launcher. Source is not vendored here.

## Configuration

Set the path to your local checkout with `make menuconfig` → Games → fbdoom →
*Local fbDOOM checkout*. It is stored in your local `.config` only; the
committed SD defconfig leaves it empty. In `.config`:

```text
CONFIG_PACKAGE_fbdoom=y
CONFIG_FBDOOM_SOURCE="/path/to/fbDOOM"
```

An empty path builds nothing: no `/usr/bin/fbdoom` and no launcher tile.
The checkout is copied into the build directory and patched there; it is not
modified. WAD files in the checkout root (for example `DOOM.WAD`) are installed
lowercased to `/usr/share/games/doom/`. Changing the path rebuilds `hz-ui`.

## Board patches

| Patch | Change |
| --- | --- |
| `001-evdev-keyboard.patch` | Keyboard input through evdev (the board has no text console) |
| `002-framebuffer-pan-alpha.patch` | Correct framebuffer offset and opaque colours |
| `003-iwad-dir.patch` | Look for WAD files in `/usr/share/games/doom` |
| `004-touch-exit.patch` | Hold the top-left screen corner for 2 s to quit |
| `005-optional-keyboard.patch` | Start without a keyboard; one plugged in later works |

Built without SDL, so there is no sound.

## Use

Tap DOOM on Home and play with a USB keyboard (it can be plugged in later).
The HMI pauses, including the parking-sensor view, until DOOM exits.
Backspace, Quit Game from the menu, or holding the top-left screen corner for
2 seconds returns to the HMI. Arrow keys move,
Ctrl fires, Space opens doors. Settings and saves go to `/mnt/.fdoom.tar/`
(fixed upstream).

fbDOOM is GPL-2.0-or-later (id Software / Chocolate Doom sources). The WAD
must be one you are entitled to use.
