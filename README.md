<img width="853" height="510" alt="Screenshot 2026-06-12 alle 02 01 56" src="https://github.com/user-attachments/assets/a582b828-58cd-4f51-8305-3e09deb160e4" />
<img width="846" height="509" alt="Screenshot 2026-06-12 alle 02 02 23" src="https://github.com/user-attachments/assets/cdd3a872-3599-4f27-8da9-a10ba35f7fa4" />

# RBrubik

RBrubik is a small terminal app that renders a rotating 3D Rubik's cube using ANSI escape codes.

The cube automatically alternates between 20 seconds of scrambling and 20 seconds of solving, with animated sticker transitions.

The renderer uses a centered canvas with a fixed maximum size: large terminal windows do not enlarge the cube, and the app avoids redrawing unnecessary terminal space. In small windows, the canvas adapts to the available room.

It is designed for Arch ARM on aarch64, but it also works on Linux x86_64. It only requires a C compiler, `make`, and a terminal with truecolor and Unicode support.

## Latest Release

**v0.0.3 - Palette switch**

This release adds a palette switcher. Press `p` to alternate between the classic cube colors and a pastel palette with white sticker borders. The renderer keeps the v0.0.2 hotfix and limits redraws to the active cube rows to avoid palette-switch artifacts.

## Requirements

- C99-compatible C compiler, such as `gcc` or `clang`
- `make`
- terminal with ANSI truecolor and Unicode support

## Installation

```sh
git clone https://github.com/Re-nault16/RBrubik.git
cd RBrubik
make
sudo make install
```

The binary is installed as `/usr/bin/RBrubik`.

## Run

```sh
RBrubik
```

## Controls

- `q`: quit
- `p`: change palette
- `Ctrl-C`: quit

## Roadmap

- More palette options
- Additional cube customization controls
- Rendering and performance improvements

## Uninstall

```sh
sudo make uninstall
```

## License

MIT.
