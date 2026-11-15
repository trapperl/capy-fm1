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

Wren 0.2 has only been run in the simulator. Do the first install yourself, with the computer at hand, before the
FM-1 is wrapped up as a present.

## Before you hand it over

- Turn **MASTER** to the middle.
- With headphones, hold **HOME** for three seconds: the screen says QUIET ON and everything is half as loud. It
  stays that way after switching off. The same again turns it off.
- The loudest the mix can get is held under a soft ceiling, so many notes at once don't crackle.

## The first five minutes

1. Switch it on. It says PIANO and waits. Nothing plays by itself.
2. Play a key: the screen says its letter. Play C, E and G together: it says C MAJOR.
3. Turn **PRESETS**: another instrument, with its name in its own colour.
4. Turn **KNOB 1**: dark to bright. Turn **KNOB 4**: the echo comes on (the FX button lights). Press **FX**: off.
5. Press **SEQ**: a band plays. The screen says which chord it is on and which comes next, and the keys that fit
   glow. Play along. **SEQ** again for the next band; after the fourth it is off.
6. Press **REC**, play a few notes, press **REC** again: it goes round. Turn **PRESETS** to BASS, press **REC**,
   play along, press **REC**: two layers.
7. Press **SEL**: the games. **HOME**: back to piano, everything off. The recording is still there: **PLAY**.

## Playing: the details

The tables in the [README](../README.md#playing) are complete. A few details:

- **Letters and chords.** The screen names what she holds. It knows major, minor, diminished, augmented and sus
  chords and the three common sevenths, in any order of the notes. **GLO** swaps the letters for the sound's wave
  and back; the choice is kept.
- **The knobs and their buttons.** ENV, LFO and FX each switch one thing on and off; the knob next in line
  (KNOB 2, 3, 4) sets how much. Turning a knob switches its thing on, so a knob never does nothing.
- **The band** never starts by itself and plays quietly under her. It stays in C, so the white keys fit; the glowing
  keys are the notes of the chord it is on. POP goes C, G, A minor, F; BLUES is a twelve-bar blues; WALTZ is in
  three; LULLABY has no drums.
- **The recording** holds about 750 notes in as many layers as she likes. Each note remembers its instrument.
  It is kept in time with SPEED: turn ALGORITHM and the recording, the band and the arp all go faster or slower
  together. With the band on, a new recording is trimmed to a whole number of beats.
- **The arp is recorded too**: what goes into the recording is what sounded.
- **SAVE** stops the sound for a moment (the FM-1 has to be quiet while it writes), then says SAVED. One recording
  is kept. At power-on it is there again: PLAY.
- **EDIT** needs a full second's hold to erase everything; a tap only takes the last layer away.

## The games: the details

- **The pond.** HOME and switching on show the capybaras' pond for three seconds (any key skips it); they blink, and the ones in the water bob. There is one
  baby to begin with; every three levels gained, over all the games together, another one moves in, up to five.
  The screen says so when it happens.
- Every game starts at level 1 and moves up after five right answers; the level is kept over power-off. Nothing
  moves a level down.
- A wrong answer brings the same question again. Twice wrong and it helps: the right keys light (FIND IT, CHORDS),
  the notes light as they are played again (HIGH LOW), or the tune gets a note shorter (COPY ME).
- The questions are made up as it goes, so there is no last one.
- In the games REC, PLAY, SAVE, EDIT, ARP and SEQ say "HOME: BACK TO PLAYING" and do nothing, and OCT- and OCT+ are
  the answer buttons of HIGH LOW; the octave stays in the middle so the letters on the screen match the keys.
- The tunes: Twinkle Twinkle, Mary Had a Little Lamb, Ode to Joy, Frère Jacques, Row Your Boat, Old MacDonald,
  Jingle Bells, London Bridge, Happy Birthday. All of them are on the white keys.

## MIDI

Notes played on the keys go out over USB on channel 1. Notes coming in over USB or the TRS jack play the
instrument that is showing, and are recorded like the keys. The FM-1 also shows up as a USB audio input, so a
computer or phone can record her.

## What it does not do

No demo, no characters, no colours or shapes standing in for notes, no hidden pages. One hold exists for grown-ups
(QUIET). That is on purpose.
