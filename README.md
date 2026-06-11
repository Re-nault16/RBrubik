# RBrubik

RBrubik is a small terminal app that renders a rotating 3D Rubik's cube using ANSI escape codes.

The cube automatically alternates between 20 seconds of scrambling and 20 seconds of solving, with animated sticker transitions.

The renderer uses a centered canvas with a fixed maximum size: large terminal windows do not enlarge the cube, and the app avoids redrawing unnecessary terminal space. In small windows, the canvas adapts to the available room.

It is designed for Arch ARM on aarch64, but it also works on Linux x86_64. It only requires a C compiler, `make`, and a terminal with truecolor and Unicode support.

## Requirements

- C99-compatible C compiler, such as `gcc` or `clang`
- `make`
- terminal with ANSI truecolor and Unicode support

## Installation

```sh
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
- `Ctrl-C`: quit

To uninstall it:

```sh
sudo make uninstall
```

## License

MIT.
