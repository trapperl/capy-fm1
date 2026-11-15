# Wren

**A first real instrument for a child of five to seven, on the M-VAVE FM-1.**

Wren replaces the FM-1's firmware with a small keyboard instrument. What it holds to:

1. **She is the musician.** Nothing plays unless she plays it or switched it on: her own recording, and a band she
   starts and plays over. No demo songs.
2. **Real instruments and real names.** Piano, organ, bells, guitar, bass, strings, brass, flute; notes by their
   letters, chords by their names. What she learns here is true at any piano.
3. **Every button does what is printed on it.** There are two places, playing and the games, and HOME always goes
   back.
4. **The games don't run out.** They make their questions up, get a little harder after five right answers and
   remember their level. A wrong answer takes nothing away.

A capybara sits beside the instrument's name. She sings when notes sound, nods to the band, blinks, and gets a
yuzu on her head for a chord or a right answer.

<p>
<img src="docs/screen-hello.png" width="160" alt="Hello: the capybara with a yuzu on her head">
<img src="docs/screen-chord.png" width="160" alt="Playing: C, E and G held; the screen says C MAJOR, the keys carry their letters">
<img src="docs/screen-band.png" width="160" alt="The band: POP, the chord now is G, next A MINOR, the keys that fit are marked">
<img src="docs/screen-find.png" width="160" alt="FIND IT: find the note E">
<img src="docs/screen-chords.png" width="160" alt="CHORDS: hold the three notes of G MAJOR">
<img src="docs/screen-tap.png" width="160" alt="TAP IT: a rhythm as eight boxes">
</p>

> **Status: 0.2, not yet run on a real FM-1.** Everything here was checked in the host simulator
> (`tests/run_host.sh`), which runs the same code and writes the screen and the sound to files. The device package
> is built by GitHub Actions, where the build's own safety checks pass. Install the first build with a computer
> nearby and read [If it goes wrong](docs/MANUAL.md#if-it-goes-wrong) first. Installing firmware is at your own risk.

## Playing

| Control | What it does |
| --- | --- |
| Keys | Play. Twelve notes at once. The screen names what she holds: one note's letter, several notes' letters, or the chord ("C MAJOR", "A MINOR", "G7"). |
| **PRESETS** | The instrument: PIANO, ORGAN, BELLS, GUITAR, BASS, STRINGS, BRASS, FLUTE. |
| **OCT-**, **OCT+** | Lower, higher. |
| **KNOB 1** | TONE: dark to bright. |
| **KNOB 2**, **ENV** | LENGTH: how long notes ring. The knob sets how much, ENV switches it on and off. |
| **KNOB 3**, **LFO** | WOBBLE: vibrato. The knob sets how much, LFO switches it on and off. |
| **KNOB 4**, **FX** | ECHO. The knob sets how much, FX switches it on and off. |
| **GLO** | The note letters (on the screen's keys, and what she holds), or the sound's wave instead. |
| **ARP** | Held keys play one after the other. |
| **SEQ** | The band: each press the next one, POP, BLUES, WALTZ, LULLABY, then off. Drums, bass and chords in C. The screen says the chord now and the next one; the keys that fit glow, the others still play. |
| **ALGORITHM** | SPEED of the band, the arp and the recording (60 to 180). |
| **REC** | Record what you play. REC again and it goes round. REC while it plays adds a layer, with any instrument. |
| **PLAY** | The recording on and off. |
| **EDIT** | Tap: take the last layer away. Hold a second: erase the recording. |
| **SAVE** | Keep the recording for next time. |
| **SEL** | The games. |
| **HOME** | Back to the start: piano, everything off. The recording is kept. |
| **MASTER** | Volume. |
| **HOME held 3 s** | For grown-ups: QUIET, half the loudness, kept over power-off. |

## The games

**SEL** goes in, **SELECT** picks the game, **SEL** again asks for another question, **HOME** leaves. The
instrument, the knobs and the keys work as always.

| Game | What she does | How it grows |
| --- | --- | --- |
| **TUNES** | The next key of a tune is green and waits for her; the screen says its letter. She plays every note. | Nine traditional tunes, one after the other. |
| **FIND IT** | The screen shows a letter: find that key. A wrong key is named ("THAT WAS C"); after two, the right keys light. | C D E, then up to G, all white keys, the black keys. |
| **COPY ME** | It plays a few notes, she plays them back. Each time right, one note more. | From 2 notes up to 12, on more keys. |
| **HIGH LOW** | Two notes: was the second higher or lower? OCT+ or OCT-. | From an octave apart down to neighbouring keys. |
| **CHORDS** | The screen names a chord: hold its three notes. Stuck, and the keys light. | C F G, then the minor ones, then any key. |
| **TAP IT** | A drum plays a rhythm, shown as boxes: tap it back on any key. | Three slow hits up to six quick ones. |

More in the [manual](docs/MANUAL.md).

## Build it

Wren is built on [Dinghy](https://github.com/zvenson/dinghy), which carries
[Felucca](https://github.com/hugelton/Felucca)'s platform: the updater, the USB rescue, USB-MIDI, USB audio.

```sh
tests/run_host.sh tests/scripts/tour.txt    # no FM-1 needed: build/host/wren.wav and the screens as PNG
```

For the FM-1 itself you need Linux on x86-64, Python 3 with Pillow, JieLi's toolchain and its AC79 SDK:

```sh
pip3 install Pillow mido python-rtmidi
tools/get_toolchain.sh
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
./build.sh                                        # -> build/wren.fwsc
python3 tools/fm1_install.py build/wren.fwsc      # over USB; asks before it writes
```

Or let GitHub build it: every push to `main` runs `.github/workflows/release.yml` and keeps `wren.fwsc` as a build
artifact; a tag `v0.1` publishes a release.

The instrument is one file, [firmware/src/wren.c](firmware/src/wren.c). The platform's calls are in
[docs/API.md](docs/API.md), the chip's rules in [docs/RULES.md](docs/RULES.md).

## Licence

GPL-3.0-only, as Dinghy and Felucca. Credits and third-party parts: [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their owners. Wren is independent of M-VAVE, Hügelton Instruments and Dinghy.
