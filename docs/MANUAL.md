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

The sound is the FM-1's own: 27 instruments per scene, each through a delay and a small stereo reverb of its own (and an envelope you can reshape), each scene placed in the stereo
field by its PAN. The first eleven are small and cheap: ANALOG, FM, SUPERSAW, PULSE, E PIANO, BELL, PLUCK, ORGAN, BASS, PAD,
BRASS. The next sixteen share one synth engine (oscillators, a resonant two-pole low-pass that the level envelope opens, the
timbre knob moving the cutoff) and are voiced after the character of classic synths, not copies of them: PROPHET 5 (two saws,
mid resonance), MINIMOOG (two saws and a sub square, a resonant sweep: a fat bass), JUNO PAD (saw, drifting pulse, sub), SYNC LEAD
(hard sync with a sweeping slave), VIRUS SAW (seven detuned saws), SH-101 (saw, pulse, sub), ACID 303 (a saw into a very resonant
filter with a fast sweep), STRINGS (five wide saws, slow in), CS-80 BRASS (three saws with a swelling filter), DX BASS (FM 2:1 with
a sub), CLAVINET (a narrow pulse, plucked), VIBES (soft FM), PWM PAD (two pulses whose widths move against each other), BREATH
(noise through a resonance at the pitch), HOOVER (three saws diving in from above) and WOBBLE BASS (two saws with the filter
wobbling in eighth notes of the BPM). They cost about as much as FM; the reverb on top costs more than four voices, so watch the
DSP figure (it shows in the footer from 60 %, in amber, and red from 80 %).

## Controls

### The screen

* **Performance screen** (the default): the globe (its rings in the colours of the theme), the eight probability towers, the A-to-B bar, tempo, density and swing.
* **Values are on the pages.** Nothing takes over the screen: turn a knob on the performance screen and the RHYTHM page
  opens, with the value you are turning lit; it closes by itself about two seconds after you stop. Buttons and the other
  knobs (PLAY, MODE, the crossfade, a saved slot ...) show a one-line message in place of the footer's first line, in the
  colour of the scene it is about.
* **Pages**: the buttons HOME, FX, ENV, LFO and GLO open a page of two halves, four values each. **Knobs 1 to 4** turn the four values of the
  lit half (the one with the bright frame). **SELECT** goes through the halves, and so the pages, of the group you are in, and round again at the end;
  tiny **dots** at the top right show where you are (they fill up to the page you are on, all of them on the last). Press the same
  button again, or HOME, to go back to the performance screen; a page you leave alone for **10 seconds** closes by itself. SELECT works as the menus
  (below) only on the performance screen.
* **Colour.** In the RICH and WILD colours each scene takes the colour of its instrument (each of the 27 has its own hue; two alike are told apart), the towers, the frames, the globe and the crossfade take the colour the morph is at between A and B, and every theme is brighter and more saturated. SOFT keeps one colour. MONO and MATRIX keep their own two scene colours.
* **Motion.** A new page slides in and brightens; a value you turn flashes white and settles; each note sends a ripple out of the globe and the globe swells with it; the boot logo gathers particles into a ring round the name. After a while with nothing touched (IDLE SCREEN in SYSTEM 2: off, 30 seconds, 1 minute or 3 minutes) the screen becomes a **formation**: 36 particles that spring into a shape (a ring, two rings, a spiral, a lattice, a wave, three rings), change shape every seven seconds, scatter when a note sounds and form again. Any touch brings the screen back.
* **Beginner text** (SYSTEM 2 menu, HELP TEXT): while it is on, each knob you turn brings a short note on what it does, in the bottom third of
  the screen, for four seconds.
* **LED flashes**: the LED of what an action was about blinks for under a second, as the plugin flashes its buttons. A
  save makes the slot's key and SAVE blink fast; a recall makes them blink slowly (a recall waits for the end of the measure
  while it plays); a copy or an init flashes the two scene keys (or the one) and SAVE; a randomise flashes the scene's key
  and EDIT.
* **The globe** is a mesh of 62 nodes after Navy Arp 2's: the rings in the theme's colours, each ring's nodes glowing with one of the six LFOs.
  While it plays, four coloured triangles hop from node to node, and every note makes one flare and hop.
* **Level meter**: two small bars at the right of the globe show the left and right level (METER on the MIDI IN LOOK page). The
  DSP load shows in the footer once it is over 60 %.
* **Colour**: COLOUR (GLO page, MIDI IN LOOK) has three settings: SOFT (one hue: the theme's ramp, the scenes by brightness),
  RICH (the scenes, the page groups, the towers and the globe in the theme's colours) and WILD (RICH, with each tower, each ring and
  each flash in a colour of its own). Each theme has a colour for scene A, one for scene B (cells of a scene are drawn in its colour), one for each
  group of pages (HOME, FX, LFO, GLO), and a colour range for the towers (low to high levels). Six themes: NAVY, MONO,
  MATRIX, SUNSET, AURORA, NEON (GLO page, TEMPO KEY, knob 4; or the SYSTEM menu).

| Button | Its pages (top half / bottom half) |
| --- | --- |
| **HOME** | GENERATE: entropy, harmony, chaos, morph / RHYTHM: rest, density, swing, legato. **Hold HOME for a second: the DRUM pages** (below); hold it again, or press it, to go back. |
| **DRUM** (hold HOME) | KIT: DRUMS (on / off), LEVEL, TONE (the brightness of the hats and the brush), DECAY / FEEL: SHDW A, SPCE A, SHDW B, SPCE B (below). Then TUNING: TUNE (the kick and tom, -12 to +12 semitones from the key), FOLLOW, SEND (into the two reverbs) and DRUM CH (the MIDI channel, 10 as General MIDI has it). **The first six lower keys are soft pads** here: kick, tom, brush, rim, hat, open hat. |
| **FX** | Each voice has its own chain. A VOICE: instrument, timbre, level / A DELAY: send, time (1/16, 1/8, 1/8 dotted, 1/4), feedback, tone; then A REVERB: send, size, bright; then the same three for B; the last page ends with MIX: PAN A and PAN B (L to R, C in the middle). **The lower keys are punch-ins here** (below). |
| **ENV** | A ENVELOPE / B ENVELOPE: ATTACK, DECAY, SUSTAIN, RELEASE of each voice. Each starts as its instrument's own envelope and is relative to it: the knob moves it up and down (each 20 steps double or halve a time; the sustain is a level), and the page shows the time in ms or s. A new instrument starts again from its own. |
| **LFO** | Six pages, one for each LFO (MORPH, REST, LEGATO, ENTROPY, HARMONY, CHAOS): on top the wave, drawn with a dot where the LFO is now; below its SHAPE (SINE, TRI, SAW, RAMP, SQUARE, RANDOM), RATE (OFF, then 4 BAR, 2 BAR, 1 BAR, 1/2, 1/4, 1/8, 1/16, 1/32 of the tempo), DEPTH and PHASE (where in its wave it starts). The LFOs start from their phase each time PLAY is pressed. |
| **GLO** | TEMPO KEY: BPM (1 to 480), step division, transpose, theme / SCALE MIDI: root, scale, MIDI channel of A and of B; then CHORDS: POLY (chords from Harmony), CHORD (STACK, SUS, QUART, ADD9), SPREAD (AUTO, CLOSE, OPEN) / SYNC: SYNC (the MIDI sync out: OFF, START for start and stop only, or CLOCK for the 24-a-beat clock as well) and CLOCK (where the tempo comes from: INT, EXT or AUTO); then AUTO A-B: AUTO (OFF, ONCE, PING, SAW), BARS (1, 2, 4, 8, 16, 32 or 64) and SHAPE (LINEAR or EASE) / MIDI IN LOOK: CC IN and PROG (on or off), COLOUR (SOFT, RICH, WILD) and METER (on or off). |

**The drum voice.** A gentle kit of six, synthesised (no samples): a tuned KICK and TOM, a soft BRUSH snare, a RIM click, a closed HAT and an OPEN hat. It is an accompaniment that follows the melody and keeps out of its way:
after each step the drums look at what the melody did, and answer it. The kick is the anchor, on the first and the middle of the measure, tuned to the key (the tonic; with FOLLOW on, the fifth on the middle when the melody has left the tonic chord).
A soft brush falls on the backbeat and on a falling leap of the melody; a tuned tom, the melody's own note an octave down, answers a rising leap; a rim speaks in the gap just after a note; hats fill the gaps; an open hat comes at the end of a phrase or after a big jump.
**SHDW** (shadow, one for each scene, blended by the A-B crossfade) is how much they answer the melody's events; **SPCE** (space, the same) is how much room they leave: the busier the melody is, the fewer the hits, and with a lot of SPACE they play almost only in its gaps.
They cannot drown it: the drum level has a ceiling (the top of the knob is about 0.6 of one note of a voice), the kit ducks automatically under the melody (at least 3 dB, deeper under chords), every hit is soft (velocity 100 at most), and they go out on their own MIDI channel as General MIDI drums (kick 36, tom 45, brush 38, rim 37, hat 42, open hat 46).
Everything is part of the preset (a preset saved before v1.8 has the drums off, so that it sounds as it did). CC 94, 95 and 96 move the level, the shadow and the space.

**Punch-ins.** While an FX page is open the 16 lower keys are momentary effects on the voice the page is about (both on MIX), for as long as you hold them:
THROW (all of it into the delay), REPEAT (long feedback), DLY 1/16, DLY 1/4, BLOOM (reverb send up), HALL (big reverb), DARK, AIR (the reverb's tone),
CLOSE and OPEN (the timbre), DUCK (quieter), KILL (silent), LONG and SHORT (the release longer or shorter), SLOW (a slow attack for new notes) and FLIP (the pan to the other side).
Let go and everything is as it was. (On the SAVE and RECALL screens the keys are slots, as ever.)

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
| **PLAY** | Start / stop the clock. **Hold it for a second: PANIC.** The screen asks for OCT- and OCT+ together within four seconds; then everything stops: the sequencer, all notes (MIDI all-notes-off and all-sound-off on both channels as well), the echoes and the reverb, FREEZE, REC and the chain. Anything else, and nothing happens. |
| **ARP** | ARP mode: notes come from the keys you hold (or are latched), or from the scale and root if none are held. **Long press: LATCH on / off.** |
| **SEQ** | SEQ mode: loops eight stored notes (set them with a step key plus a lower key). |
| **SEL** | **FREEZE**: locks the pitch pool and all the levels as they are now, rhythm and pitch together. Press again to release. (POLY is a cell on the GLO page, CHORDS; tapping the tempo is in the SYSTEM 2 menu.) |
| **REC** | **Capture** one measure of what plays. Press: armed (it blinks); at the next measure it records eight steps (lit while it does); then the notes that sounded become the SEQ pattern of both scenes (a step that stayed silent is set to 0, one that sounded to 255) and the mode is SEQ. With REC KEEPS (SYSTEM 2) on +MOTION it also keeps where the six parameters and the density stood at each step, and SEQ plays that motion back (until a preset is recalled or a new capture is made). Press while armed or recording: cancel. **Long press: undo** the last capture. |
| **EDIT** | **Randomise** the edited scene (step levels, rest, legato, morph, entropy, harmony, chaos). **Long press: AUTO ROLL**, a new roll at the start of every measure (lit while on, RND in the header). |
| **OCT- / OCT+** | Octave, -3 to +3. |
| **HOME, FX, ENV, LFO, GLO** | Pages, as above. |
| **SAVE** | Short press: save mode (blinks). Pick a slot 1-16 with a lower key; SAVE again cancels. **Long press:** recall mode (stays lit): pick a slot and it starts at the end of the measure; long press again to leave. The screen then shows the 16 slots with their numbers: a filled square is a saved preset, an outline an empty slot, the bright one the last used. In the COPY INIT menu SAVE does the action; in SYSTEM 2 it taps the tempo. |

**Clock: master or follower.** CLOCK on the CHORD SYNC page says who keeps the time. **INT**: Formation is the master: its BPM sets the tempo, SYNC
sends the clock and Start / Stop out, and a clock that comes in is ignored. **EXT**: Formation follows the 24-a-beat MIDI clock that comes in on
USB or Bluetooth: nothing plays until a Start (or PLAY, then the next ticks); Start runs it from the first step, Continue carries on, Stop stops
it; the steps happen on the clock's ticks (with the swing as a delay of the odd ones), AUTO A-B counts its bars in the clock's beats, and the BPM
shown, the delay and the LFOs go by the tempo of the clock (the BPM knob does nothing then). The footer shows EXT. **AUTO**: it follows a clock while
one is coming in, and when it has been gone for 300 ms it carries on by itself at the tempo the clock had. While Formation follows it sends no clock
or Start / Stop out (so a clock can go round a ring of devices without echoing). That is why there is a switch: Formation both sends and takes MIDI, and
only one of the two can be the master. Over Bluetooth a clock arrives in bursts, a connection interval apart, so the steps wobble by that much: use USB for a
tight sync. The clock and transport also come in on the TRS jack (below).

**MIDI in.** Formation takes MIDI on USB, Bluetooth and the TRS jack (a MIDI Type A jack on the FM-1: notes, control changes, program
changes, clock and start / stop; the jack's driver is Optimist's, which came from Melodee, where it was checked on an FM-1). Notes
play like the keys. With CC IN on, control changes on any channel move parameters (0 to 127 over the range):

| CC | What | CC | What |
| --- | --- | --- | --- |
| 8 | crossfade A to B | 70 / 71 | timbre A / B |
| 12 | density | 72 / 73 | level A / B |
| 13 | swing | 74 / 75 | delay send A / B |
| 20 to 25 | scene A: MORPH, REST, LEGATO, ENTROPY, HARMONY, CHAOS | 85 / 86 | reverb send A / B |
| 26 to 31 | scene B: the same six | 87 / 88 | reverb size / bright |
|  |  | 90 / 91 | pan A / B |

With PROG on, a program change from 0 to 15 recalls that preset (at the end of the measure while it plays; at once when stopped).
A CC on the crossfade works with AUTO A-B like the knob does: the trip carries on from where it was put.

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

### Knobs

| Knob | What |
| --- | --- |
| **Knobs 1-4** | The four values of the lit half, or the RHYTHM values on the performance screen (above). |
| **ALGORITHM** | The A-to-B crossfade, on every screen. With AUTO A-B on, turning it moves the crossfade by hand and the automatic trip carries on from where you leave it. |
| **PRESETS** | The edited scene's voice: MIDI only, then the 27 instruments in the order above. |
| **SELECT** | On a page: goes through its halves and pages. On the performance screen: the menus, in this order: SCALE and ROOT, COPY and INIT, CHAIN, SYSTEM, SYSTEM 2 (they go back after 2.5 seconds without a touch). |
| **MASTER** | Volume. |

### COPY and INIT menu (SELECT)

Knob 1 picks the action: copy A to B, copy B to A, init the edited scene, init everything (the working state), **INIT SLOT** (knob 2 picks the slot, 1 to 16) or **INIT ALL SLOTS**. **SAVE** does the first four; the last two empty saved presets, so you **hold SAVE** for them.

### CHAIN menu (SELECT)

A song of up to eight steps, each a preset slot and a number of measures. Knob 1 picks the step, knob 2 its slot (turn left past slot 1 to empty the step), knob 3 its measures (1, 2, 4, 8 or 16), and knob 4 switches the chain ON or OFF.
While it is on and the sequencer plays, each new measure is counted and, when a step's measures are up, the next step's preset is recalled; after the last step it starts again from the first, and a chain with nothing in it switches itself off.
It starts from step 1 each time you press PLAY. The steps are kept with your working state; ON is not (it is off at power-on). CHN shows in the header while it is on.

### SYSTEM menu

Theme (NAVY, MONO, MATRIX, SUNSET, AURORA, NEON), the MIDI channels of scene A and B, BLUETOOTH, and the DSP load: how much of the
audio time the sound takes. Keep it under about 50 %; if it is higher, use fewer chords or the lighter voices.

### SYSTEM 2 menu

HELP TEXT (knob 1: the beginner text, on or off), REC KEEPS (knob 2: NOTES, or +MOTION), IDLE SCREEN (knob 3: OFF, 30 SEC, 1 MIN, 3 MIN) and TAP TEMPO: press SAVE in time, from the last four taps (a pause of two seconds starts again).

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
* The plugin's Rate and Octaves LFOs are not here (six LFOs: Morph, Rest, Legato, Entropy, Harmony, Chaos; each with a shape, a rate from 4 bars to 1/32, a depth and a phase).
* Chords come from the scale, as in the plugin; the held notes are the arp's pool.
* USB and Bluetooth MIDI notes (any channel) are accepted like keys. **MIDI sync out**: PLAY sends MIDI Start and
  stopping sends Stop; with SYNC at CLOCK it also sends the 24-a-beat clock at the BPM, to USB and Bluetooth (GLO page).
* Presets are kept in their own flash sector, your working state is saved by itself when the sound is quiet.
* Each voice has its own effects: a mono delay (send, time, feedback and tone: the DELAY half of its FX pages, up to about 0.74 s, so the
  1/4 delay is held at 0.74 s below 81 BPM) and a stereo reverb (send, size, bright: its REVERB half), both part of the preset, with the voice's
  envelope (ENV) and each LFO's shape and phase. The voice and its delay sit where its PAN puts them, the reverb is stereo.
* Each reverb uses a few percent of the chip's time while it runs (it stops by itself about four seconds after its voice's sound
  ends, and when its send is 0): watch DSP LOAD in the SYSTEM menu. E PIANO, BELL and ORGAN cost the most of the
  voices (about as much as FM).
* **Storage (v1.9).** Each saved slot now carries a model tag (the three-model release builds on it), and the settings record keeps a working state for each model. Presets and settings from every earlier version are read and converted automatically. A v1.0 to v1.8 preset becomes a FORMATION preset.
* Presets and settings saved by v1.0 to v1.8 are read and converted when you recall them: the one reverb and the one delay of v1.6 and before
  become the same settings on both voices, ENV is the instrument's own and the LFOs are sines at phase 0, as they were. The first preset you save
  afterwards writes the new layout.
