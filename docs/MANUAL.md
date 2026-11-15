# Wren: the manual

For the grown-up who hands it over. The child needs none of this.

## Install

1. Get `wren.fwsc`: from this repository's GitHub Actions run (the `wren-fwsc` artifact), from a release, or build it
   (see the [README](../README.md#build-it)).
2. Connect the FM-1 with a USB **data** cable and switch it on.
3. Either:
   - `pip3 install mido python-rtmidi`, then `python3 tools/fm1_install.py wren.fwsc`. It asks before it writes.
   - Or use a browser installer that takes a `.fwsc` file, such as the
     [FM-1 Firmware Hub](https://thefirmwarehub.com), in Chrome or Edge.
4. Don't unplug it while it writes. The FM-1 restarts into Wren.

**Going back:** install M-VAVE's own V15 package the same way, or with M-VAVE's M-UPGRADE app. Wren answers the
installers like the other community firmwares.

## If it goes wrong

- **Wren crashes:** a red screen shows where, then the FM-1 restarts. If it crashes twice while starting, it goes
  to the USB rescue by itself.
- **It won't start:** hold **OCT-** while switching on. That is the USB rescue: no sound, only the updater. Install
  another build or the stock firmware from there.
- **Not even that:** the chip's own boot ROM can still be reached over USB with a small RP2040 dongle
  ([FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter)) or the tools listed under "Firmware update
  and recovery" in [awesome-fm-1](https://github.com/cicloid/awesome-fm-1).

Wren 0.1 has only been run in the simulator. Do the first install yourself, with the computer at hand, before the
FM-1 is wrapped up as a present.

## Before you hand it over

- Turn **MASTER** to the middle.
- With headphones, hold **HOME** for three seconds: the screen says QUIET ON and everything is half as loud. It
  stays that way after switching off. The same again turns it off.
- The loudest the mix can get is held under a soft ceiling, so many notes at once don't crackle.

## The first five minutes

1. Switch it on. It says PIANO and waits. Nothing plays by itself.
2. Play the keys. The line above the keyboard is what the sound looks like.
3. Turn **PRESETS**: another instrument, with its name in its own colour.
4. Turn **KNOB 1**: dark to bright. Turn **KNOB 4**: the echo comes on (the FX button lights). Press **FX**: off.
5. Press **REC**, play a few notes, press **REC** again: it goes round. Turn **PRESETS** to BASS, press **REC**,
   play along, press **REC**: two layers.
6. Press **HOME**: back to piano, everything off. The recording is still there: **PLAY**.

## The controls

The table in the [README](../README.md#what-every-control-does) is complete. A few details:

- **The knobs and their buttons.** ENV, LFO and FX each switch one thing on and off; the knob next in line
  (KNOB 2, 3, 4) sets how much. Turning a knob switches its thing on, so a knob never does nothing.
- **LENGTH** makes ringing instruments (piano, bells, guitar) ring longer or shorter and gives the held ones
  (organ, strings, flute) a longer or shorter tail.
- **The recording** holds about 750 notes in as many layers as she likes. Each note remembers its instrument.
  It is kept in time with SPEED: turn ALGORITHM and the recording, the beat and the arp all go faster or slower
  together.
- **With the beat on (SEQ)**, a new recording is trimmed to a whole number of beats, so it stays in step with the
  drums.
- **The arp is recorded too**: what goes into the recording is what sounded.
- **SAVE** stops the recording for a moment (the sound has to be quiet while the FM-1 writes), then says SAVED.
  One recording is kept. At power-on it is there again: PLAY.
- **EDIT** needs a full second's hold to erase everything; a tap only takes the last layer away.

## Follow the lights

Press **SEL** (or turn **SELECT**): one key is lit, green on the screen. That is the next note of the tune. It
waits. She plays it, the next one lights. Other keys still sound: it is an instrument, not a test. At the end a
light runs along the keys and the tune starts again.

Turn **SELECT** for another tune: Twinkle Twinkle, Mary Had a Little Lamb, Ode to Joy, Frère Jacques, Row Your Boat,
Old MacDonald, Jingle Bells, London Bridge, Happy Birthday. All of them are on the white keys.

## MIDI

Notes played on the keys go out over USB on channel 1. Notes coming in over USB or the TRS jack play the
instrument that is showing, and are recorded like the keys. The FM-1 also shows up as a USB audio input, so a
computer or phone can record her.

## What it does not do

No demo, no auto-accompaniment, no characters, no games, no hidden pages. One hold exists for grown-ups (QUIET).
That is on purpose.
