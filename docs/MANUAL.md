# Formation manual

Formation is a probabilistic arpeggiator and step sequencer with two **scenes**, A and B, that you crossfade with
Knob 4. Each scene has eight step probabilities and its own voice; both send MIDI out over USB and play the FM-1's own
sound. It is a port of the Navy Arp 2 plugin.

The same firmware also runs in your browser (**emulator.html** on the site that has the installer): every knob, button and
key is on the page, so you can try an idea before you install it.

## The idea

Eight steps run in a loop at the BPM. At each step the engine rolls a die against that step's level (the two scenes'
levels are crossfaded), then against **Rest**. If the step sounds, it plays a note, maybe a chord (**Harmony**, with
POLY on), maybe an octave jump (**Chaos**). **Entropy** changes the direction the steps run (forward, ping-pong, random
walk, reverse, skipping). **Legato** sets how long notes last. **Density** raises or lowers all the probabilities.
**Swing** delays every other step. **Morph** smears each step's level into the next one.

Scene A plays on MIDI channel 1 and scene B on channel 2 (GLO page); the crossfade moves note velocity (and the
sound) from A to B with an equal-power curve.

The sound is the FM-1's own: eleven instruments (ANALOG, FM, SUPERSAW, PULSE, E PIANO, BELL, PLUCK, ORGAN, BASS, PAD,
BRASS) per scene, through a delay and a small stereo reverb.

## Controls

### The screen

* **Performance screen** (the default): the globe (its rings in the colours of the theme), the eight probability towers, the A-to-B bar, tempo, density and swing.
* **Values are on the pages.** Nothing takes over the screen: turn a knob on the performance screen and the RHYTHM page
  opens, with the value you are turning lit; it closes by itself about two seconds after you stop. Buttons and the other
  knobs (PLAY, MODE, the crossfade, a saved slot ...) show a one-line message in place of the footer's first line, in the
  colour of the scene it is about.
* **Pages**: some buttons open a page of two halves, four values each. **Knobs 1 to 4** turn the four values of the lit
  half (the one with the bright frame). Press the same button again to move to the next half, then the next page, and
  after the last one back to the performance screen.
* **LED flashes**: the LED of what an action was about blinks for under a second, as the plugin flashes its buttons. A
  save makes the slot's key and SAVE blink fast; a recall makes them blink slowly (a recall waits for the end of the measure
  while it plays); a copy or an init flashes the two scene keys (or the one) and SAVE; a randomise flashes the scene's key
  and EDIT.
* **Colour**: each theme has a colour for scene A, one for scene B (cells of a scene are drawn in its colour), one for each
  group of pages (HOME, FX, LFO, GLO), and a colour range for the towers (low to high levels). Six themes: NAVY, MONO,
  MATRIX, SUNSET, AURORA, NEON (GLO page, TEMPO KEY, knob 4; or the SYSTEM menu).

| Button | Its pages (top half / bottom half) |
| --- | --- |
| **HOME** | GENERATE: entropy, harmony, chaos, morph / RHYTHM: rest, density, swing, legato |
| **FX** | VOICE A: instrument, timbre, delay, level / VOICE B: the same for scene B; then REVERB: send of A, send of B, size, bright / DELAY: time (1/16, 1/8, 1/8 dotted, 1/4), feedback, tone |
| **LFO** | speed and depth of the six LFOs, two parameters a half: MORPH REST / LEGATO ENTROPY / HARMONY CHAOS |
| **GLO** | TEMPO KEY: BPM (1 to 480), step division, transpose, theme / SCALE MIDI: root, scale, MIDI channel of A and of B; then CHORD SYNC: CHORD (STACK, SUS, QUART, ADD9), SPREAD (AUTO, CLOSE, OPEN) and SYNC (the MIDI sync out: OFF, START for start and stop only, or CLOCK for the 24-a-beat clock as well) and CLOCK (where the tempo comes from: INT, EXT or AUTO). Knobs 1 to 4 turn them; then AUTO A-B: AUTO (OFF, ONCE, PING, SAW), BARS (1, 2, 4, 8, 16, 32 or 64) and SHAPE (LINEAR or EASE). |

On the performance screen the knobs turn the RHYTHM values: **Knob 1** rest (or, with a step key held, that step's level),
**Knob 2** density, **Knob 3** swing, **Knob 4** legato. Turn a knob fast and it takes bigger steps.

### Keys (the 27 capsule keys)

| Keys | What |
| --- | --- |
| **Lower 16 (white keys)** | The keyboard. Notes you hold are the arp's pool (with LATCH they stay after you let go). With the clock stopped they just play. On the SAVE and RECALL screens they are slots 1-16. |
| **Upper keys 1-8** | The steps. Hold one and turn **Knob 1** on the performance screen to set that step's probability (hold several to set them together). In SEQ mode: hold a step key and tap a lower key to set that step's note. |
| **Upper keys 9, 10** | Which scene the knobs edit: A or B. |
| **Upper key 11** | MUTE. Hold it and tap step keys to mute or unmute steps; tap it alone to mute or unmute all. |

### Buttons

| Button | What |
| --- | --- |
| **PLAY** | Start / stop the clock. |
| **ARP** | ARP mode: notes come from the keys you hold (or are latched), or from the scale and root if none are held. **Long press: LATCH on / off.** |
| **SEQ** | SEQ mode: loops eight stored notes (set them with a step key plus a lower key). |
| **SEL** | POLY on / off (chords from Harmony). |

**Clock: master or follower.** CLOCK on the CHORD SYNC page says who keeps the time. **INT**: Formation is the master: its BPM sets the tempo, SYNC
sends the clock and Start / Stop out, and a clock that comes in is ignored. **EXT**: Formation follows the 24-a-beat MIDI clock that comes in on
USB or Bluetooth: nothing plays until a Start (or PLAY, then the next ticks); Start runs it from the first step, Continue carries on, Stop stops
it; the steps happen on the clock's ticks (with the swing as a delay of the odd ones), AUTO A-B counts its bars in the clock's beats, and the BPM
shown, the delay and the LFOs go by the tempo of the clock (the BPM knob does nothing then). The footer shows EXT. **AUTO**: it follows a clock while
one is coming in, and when it has been gone for 300 ms it carries on by itself at the tempo the clock had. While Formation follows it sends no clock
or Start / Stop out (so a clock can go round a ring of devices without echoing). That is why there is a switch: Formation both sends and takes MIDI, and
only one of the two can be the master. Over Bluetooth a clock arrives in bursts, a connection interval apart, so the steps wobble by that much: use USB for a
tight sync. The TRS jack is not read by the firmware yet.

**Tempo.** BPM goes from 1 to 480 (the knob speeds up when you turn it fast). At 1 BPM with quarter-note steps one step takes a
minute; at 480 BPM with 32nd-note steps one step takes 16 ms. The extremes are for slow evolution and for rolls, with these
limits: at the top with the fastest steps and big chords, USB and above all Bluetooth carry less than the notes (Bluetooth drops
what it cannot send, USB is fine to well past 200 notes a second).

**AUTO A-B.** The crossfader moves by itself, locked to the step clock and so to the MIDI clock Formation sends: one trip from A to
B takes 1, 2, 4, 8, 16, 32 or 64 bars (a bar is 4 beats, whatever the step division). ONCE goes to the far end and rests (each
PLAY makes a new trip), PING goes to and fro, SAW goes from A to B and starts again from A. SHAPE EASE slows the movement near the
ends. A hand on the ALGORITHM knob, or a recalled preset, sets the position and the trip carries on from there. FREEZE holds it, and it
moves only while the sequencer plays.

**Chords.** With POLY on, each note that plays brings a chord built on it, from the degrees of the current scale (so the chord
is always the scale's own: a triad on each note of C MAJOR is C, Dm, Em, F, G, Am, B dim). How many notes joins in with
HARMONY: a quarter of the way up gives the note and its fifth, half way a triad, three quarters a seventh chord.
CHORD (GLO page, CHORD SYNC) picks the kind: STACK is stacked thirds (triads and sevenths), SUS swaps the third for the
fourth, QUART stacks fourths, ADD9 is root, third, fifth and ninth. SPREAD is the voicing: CLOSE keeps the notes together,
OPEN lifts the third and the seventh an octave (a wider, softer chord), AUTO opens the chords low down (below E3), where
close thirds are muddy. CHAOS moves the whole chord an octave, never a single note out of it.
| **REC** | FREEZE: locks the pitch pool and all the levels as they are now, rhythm and pitch together. Press again to release. |
| **EDIT** | **Randomise** the edited scene (step levels, rest, legato, morph, entropy, harmony, chaos). **Long press: AUTO ROLL**, a new roll at the start of every measure (lit while on, RND in the header). |
| **OCT- / OCT+** | Octave, -3 to +3. |
| **HOME, FX, LFO, GLO** | Pages, as above. |
| **SAVE** | Short press: save mode (blinks). Pick a slot 1-16 with a lower key; SAVE again cancels. **Long press:** recall mode (stays lit): pick a slot and it starts at the end of the measure; long press again to leave. Filled slots are lit. |
| ENV | Not used yet. |

### Knobs

| Knob | What |
| --- | --- |
| **Knobs 1-4** | The four values of the lit half, or the RHYTHM values on the performance screen (above). |
| **ALGORITHM** | The A-to-B crossfade, on every screen. With AUTO A-B on, turning it moves the crossfade by hand and the automatic trip carries on from where you leave it. |
| **PRESETS** | The edited scene's voice: MIDI only, ANALOG, FM, SUPERSAW, PULSE, E PIANO, BELL, PLUCK, ORGAN, BASS, PAD, BRASS. |
| **SELECT** | Menus: SCALE and ROOT, COPY and INIT, SYSTEM (they go back after 2.5 seconds without a touch). |
| **MASTER** | Volume. |

### COPY and INIT menu (SELECT)

Knob 1 picks the action (copy A to B, copy B to A, init the edited scene, init everything); **SAVE** does it.

### SYSTEM menu

Theme (NAVY, MONO, MATRIX, SUNSET, AURORA, NEON), the MIDI channels of scene A and B, BLUETOOTH, and the DSP load: how much of the
audio time the sound takes. Keep it under about 50 %; if it is higher, use fewer chords or the lighter voices.

**BLUETOOTH** (knob 4: right is on, left is off) sends everything Formation sends over USB over Bluetooth LE MIDI as
well, and takes MIDI notes in from a connected device like USB's. It is **off at every power-on**. Switched on, the FM-1
advertises as `Formation XXXX`; connect to it from a computer, a tablet or a Bluetooth MIDI adapter that acts as the host.
The link has to be **paired** (Just Works: there is no passkey, you only accept), and MIDI does not flow until it is.
The FM-1 asks for pairing as soon as a device connects; on Windows use Settings, Bluetooth and devices, Add device; on
a Mac, Audio MIDI Setup, MIDI Studio, the Bluetooth icon, Connect. The row reads WAITING while it advertises, LINKED once
a device has connected but the MIDI is not on yet (it is pairing, or it has not asked for the MIDI), and READY plus a
count (the packets queued for it) once it listens; the performance screen shows BT beside the theme while it does.

* One paired device is kept across power-offs; pairing another replaces it. If a device that was paired before no longer
  connects (the FM-1 was reset, or another device took the place), remove Formation from that device's Bluetooth
  list and pair again.

* The first time it is switched on it starts the radio and keeps a copy of the radio's factory values and an address
  in the settings. NO CAL means the radio's factory values could not be found (they come from the stock firmware's
  store, which Formation only reads): the radio stays off, and installing the stock firmware once puts them back.
* Starting the radio takes a moment, so switch Bluetooth on before you play, not during.
* Bluetooth uses some of the chip's time. Watch DSP LOAD with a device connected.

## Notes on what differs from the plugin

* **Morph** has no effect in the plugin's note engine; here it smears each step's probability into the next step's.
* **Legato** sets the note length here (10 % to 95 % of a step); in the plugin it reduces the rests.
* The plugin's Rate and Octaves LFOs are not here (six LFOs: Morph, Rest, Legato, Entropy, Harmony, Chaos).
* Chords come from the scale, as in the plugin; the held notes are the arp's pool.
* USB and Bluetooth MIDI notes (any channel) are accepted like keys. **MIDI sync out**: PLAY sends MIDI Start and
  stopping sends Stop; with SYNC at CLOCK it also sends the 24-a-beat clock at the BPM, to USB and Bluetooth (GLO page).
* Presets are kept in their own flash sector, your working state is saved by itself when the sound is quiet.
* The effects are a mono delay (time, feedback and tone on the FX page, DELAY half; each scene's send is on its VOICE
  page) and a stereo reverb (REVERB half: a send for each scene, size, bright). Reverb and delay settings: the reverb's
  four values are part of a preset; the delay's three and the sync setting are kept with your working state.
* The reverb uses a few percent of the chip's time while it runs (it stops by itself about four seconds after the sound
  ends, and when both sends are 0): watch DSP LOAD in the SYSTEM menu. E PIANO, BELL and ORGAN cost the most of the
  voices (about as much as FM).
* A preset saved before the reverb existed gets the reverb's default settings when you recall it.
