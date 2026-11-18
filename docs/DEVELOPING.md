# Developing Capy

For people who want to change the firmware. To install Capy as it is, see the [README](../README.md#install).

Capy is built on [Dinghy](https://github.com/zvenson/dinghy), which carries
[Felucca](https://github.com/hugelton/Felucca)'s platform: the updater, the USB rescue, USB-MIDI and USB audio.
The instrument itself is one file, [firmware/src/capy.c](../firmware/src/capy.c). The platform's calls are in
[API.md](API.md), the chip's rules in [RULES.md](RULES.md).

## Build the firmware

This needs Linux on x86-64 (WSL on Windows works), Python 3, JieLi's toolchain and its AC79 SDK. In a terminal:

```sh
git clone https://github.com/trapperl/capy-fm1.git
cd capy-fm1
pip3 install Pillow mido python-rtmidi
tools/get_toolchain.sh
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
./build.sh
```

The result is `build/capy.fwsc`. Install it as the README describes, with `build/capy.fwsc` in place of
`capy.fwsc`. The `capy.fwsc` at the top of the repository is the package built from the code beside it.

## Test a change without an FM-1

There is no way to play Capy on a computer. There is a test tool: it compiles `capy.c` for the computer, feeds it a
script of key and button presses, and saves the sound and pictures of the screen, so a change can be checked
before it goes onto an FM-1.

```sh
tests/run_host.sh tests/scripts/tour.txt
```

This needs a C compiler and Python 3 with Pillow. The files appear in `build/host/`: `capy.wav` is the sound, and
each `capy-<milliseconds>.png` is the screen at a moment the script asked for one.

A script is plain text with one event on a line: the time in milliseconds, then what happens. For example
`900 keys 0x80` holds middle C from 900 ms on, `1200 keys 0` lets go, and `1500 shot` saves the screen. The
scripts are in [tests/scripts](../tests/scripts); `tour.txt` begins with the numbers of the buttons and knobs.

A second tool checks the games themselves:

```sh
tests/run_bot.sh
```

It plays every game with a player that knows the answers ([tests/bot.c](../tests/bot.c)) and fails if a game does
not reach its top level or stops asking. Then it runs eight sessions of random keys, buttons and knobs
([tests/stress.py](../tests/stress.py)) and fails if one crashes. Run it after changing a game.
