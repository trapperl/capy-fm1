# Capy: the manual

For the grown-up who hands it over. The child needs none of this.

## Install

Installing firmware is at your own risk. So far Capy has been installed on one FM-1, from Windows, over another
community firmware. Installing it over M-VAVE's own firmware has not been tried yet.

1. Download this repository as a [ZIP file](https://github.com/trapperl/capy-fm1/archive/refs/heads/main.zip) and
   unpack it. You get a folder `capy-fm1-main` with the firmware, `capy.fwsc`, and its installer inside.
2. Install [Python 3](https://www.python.org/downloads/). On Windows, tick "Add python.exe to PATH" in its
   installer.
3. Open a terminal in the `capy-fm1-main` folder. On Windows: open the folder in Explorer, type `cmd` in the
   address bar and press Enter.
4. Type this and press Enter. It installs the two Python packages the installer needs:

   ```
   pip install mido python-rtmidi
   ```

5. Connect the FM-1 to the computer with a USB **data** cable and switch it on.
6. Type this and press Enter:

   ```
   python tools/fm1_install.py capy.fwsc
   ```

   It shows which FM-1 it found and asks "Install?". Type `y` and press Enter. Don't unplug the FM-1 while it
   writes. After a minute or two the FM-1 restarts and shows Capy's pond.

**Going back to M-VAVE's firmware:** [The Firmware Hub](https://thefirmwarehub.com), in Chrome or Edge, has a
"Back to official V15" button for the FM-1. That has not been tried from Capy yet.

## If it goes wrong

- **The installer says "FM-1 not found":** the cable may only charge (use one that carries data), the FM-1 may be
  off, or another program may be using it.
- **The installer ends with "the FM-1 did not come back":** the firmware was written. Switch the FM-1 off and on.
  If the computer still doesn't see it, see the Windows point below.
- **Capy crashes:** a red screen shows where in the program, then the FM-1 restarts. If it crashes twice while starting, it goes
  to the USB rescue by itself.
- **It won't start:** hold **OCT-** while switching on. That is the USB rescue: no sound, only the updater. Install
  Capy again, or another firmware, from there.
- **Windows no longer sees the FM-1 after an install:** it kept its setup for the firmware that was on it before.
  In Device Manager, under "Sound, video and game controllers", uninstall the FM-1's entry (it carries a warning
  sign), then choose Action, Scan for hardware changes.
- **Not even the rescue starts:** the chip's own boot ROM can still be reached over USB with a small RP2040 dongle
  ([FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter)) or the tools listed under "Firmware update
  and recovery" in [awesome-fm-1](https://github.com/cicloid/awesome-fm-1).

Install it and try it yourself before the FM-1 is wrapped up as a present.

## Before you hand it over

- Turn **MASTER** to the middle.
- With headphones, hold **HOME** for three seconds: the screen says QUIET ON and everything is half as loud. It
  stays that way after switching off. The same again turns it off.
- The loudest the mix can get is held under a soft ceiling, so many notes at once don't crackle.

## The first five minutes

1. Switch it on. It shows the capybaras' pond. Press any key: the pond closes and the screen says PIANO.
2. Play a key: the screen says its letter. Play C, E and G together: it says C MAJOR.
3. Turn **PRESETS**: another instrument, with its name in its own colour.
4. Turn **KNOB 4**: dark to bright. Turn **KNOB 1**: the echo comes on (the FX button lights). Press **FX**: off.
5. Press **SEQ**: a metronome clicks. Press **SEQ** again: the first band's drums play. Press a low key (the ones
   marked orange on the screen): the bass and the chords join in on that key's chord, and the keys that fit glow.
   Another low key, another chord. Each further press of **SEQ** is the next band; after the fourth it is off.
6. Press **REC**, play a few notes, press **REC** again: it goes round. Turn **PRESETS** to BASS, press **REC**,
   play along, press **REC**: two layers.
7. Press **SEL**: the games. **HOME**: the pond again; any key, and it is back at the piano with everything off.
   The recording is still there: **PLAY**.

## Playing: the details

The tables in the [README](../README.md#playing) are complete. A few details:

- **Letters and chords.** The screen names the keys that are held. It knows major, minor, diminished, augmented and sus
  chords and the three common sevenths, in any order of the notes. **GLO** swaps the letters for the sound's wave
  and back; the choice is kept.
- **The knobs and their buttons.** FX, ENV and LFO each switch one thing on and off; the knob above each
  (KNOB 1, 2, 3) sets how much, and the screen shows them in the same order: ECHO, LENGTH, WOBBLE, then TONE (KNOB 4). Turning a knob switches its thing on, so a knob never does nothing.
- **The band.** SEQ steps through CLICK (a metronome), then POP, BLUES, WALTZ and LULLABY, then off. A band begins
  with its drums alone (LULLABY has none: it waits). The lowest octave of keys leads it, F to E: each white key is a chord in C (C, D minor,
  E minor, F, G, A minor; B plays G), kept until another is picked, and the band answers at once. Of several low
  keys held, the lowest leads, so a C chord played down there is C. The keys play their own notes as always. It
  stays in C, so the white keys fit; the glowing keys are the notes of the chord it is on. BLUES walks its bass;
  WALTZ is in three.
- **The recording** holds about 750 notes in any number of layers. Each note remembers its instrument.
  It is kept in time with SPEED: turn ALGORITHM and the recording, the band and the arp all go faster or slower
  together. With the band on, a new recording is trimmed to a whole number of beats.
- **The arp is recorded too**: what goes into the recording is what sounded.
- **Four songs.** SELECT turns through SONG 1 to SONG 4; the screen says which one REC, PLAY, SAVE and EDIT
  mean. Turning to another song keeps the one that was showing (the sound stops for a moment while it is written), so
  the knob never loses a recording. Switching off does: **SAVE** first.
- **SAVE** stops the sound for a moment (the FM-1 has to be quiet while it writes), then says SONG 2 SAVED. At
  power-on the song that was showing is there again: PLAY.
- **EDIT** needs a full second's hold to erase everything; a tap only takes the last layer away.

## The games: the details

- **The nap.** When nothing has been touched and nothing has sounded for two minutes, the pond comes back with the
  capybara asleep (ZZZ). Any key, button or knob wakes it, and the instrument, the knobs and the song are as they
  were. It does not happen in the games, with the click or a band on, or while a recording plays.
- **The pond.** HOME and switching on show the capybaras' pond until a key is played, a button pressed or a knob turned; the
  buttons OCT- and OCT+ don't close it, they step back and forth through its six scenes (calm, the jump into the
  water, the ride on her mother's back, the wren, the nap, the yuzu bath), which also follow each other by
  themselves; each visit starts with the next one. The other babies blink, and the ones in the water bob. There is one
  baby to begin with; every three levels gained, over all the games together, another one moves in, up to five.
  The screen says so when it happens.
- Every game starts at level 1 and moves up after five right answers; the level is kept over power-off. Nothing
  moves a level down.
- A wrong answer brings the same question again. Twice wrong and it helps: the right keys light (FIND IT, CHORDS),
  the notes light as they are played again (HIGH LOW), the tune gets a note shorter (COPY ME), only the right key
  stays lit (GUESS IT), or each drum's key lights as its turn comes (DRUM IT).
- The questions are made up as it goes, so there is no last one.
- In the games SEQ switches the click on and off (the rhythms of TAP IT and DRUM IT are heard without it, and JOIN IN keeps its
  own drums), and DRUMS and ANIMALS are left out of the instruments. In DRUM IT the keys are the drum kit whatever
  the instrument: C the kick, D the snare, E the clap, in every octave; the order of the drums counts, and roughly
  their timing. REC, PLAY, SAVE, EDIT and ARP say "HOME: BACK TO PLAYING" and do nothing, and OCT- and OCT+ are
  the answer buttons of HIGH LOW; the octave stays in the middle so the letters on the screen match the keys.
- The tunes: Twinkle Twinkle, Mary Had a Little Lamb, Ode to Joy, Frère Jacques, Row Your Boat, Old MacDonald,
  Jingle Bells, London Bridge, Happy Birthday, Hot Cross Buns, Rain Rain Go Away, Au Clair de la Lune, Lightly Row,
  Yankee Doodle, Itsy Bitsy Spider, Pop Goes the Weasel, Alouette, Long Long Ago, Good King Wenceslas and Brahms'
  Lullaby. After the last note of a tune the screen says LISTEN and Capy plays the whole tune back with the band;
  SEL skips that and goes on to the next tune. All of them are on the white keys.

## MIDI

Notes played on the keys go out over USB on channel 1. Notes coming in over USB or the TRS jack play the
instrument that is showing, and are recorded like the keys. The FM-1 also shows up as a USB audio input, so a
computer or phone can record the playing.

## What it does not do

No demo songs, no colours or shapes standing in for notes, no hidden pages. One hold exists for grown-ups
(QUIET). That is on purpose.
