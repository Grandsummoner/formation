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

## Controls

### The screen

* **Performance screen** (the default): the globe, the eight probability towers, the A-to-B bar, tempo, density and swing.
* **Touch a value and it takes over the screen**: its name, the value at double size and a bar. It goes back by itself
  about 0.8 seconds after you stop turning (or let go of the button).
* **Pages**: some buttons open a page of two halves, four values each. **Knobs 1 to 4** turn the four values of the lit
  half (the one with the bright frame). Press the same button again to move to the next half, then the next page, and
  after the last one back to the performance screen.

| Button | Its pages (top half / bottom half) |
| --- | --- |
| **HOME** | GENERATE: entropy, harmony, chaos, morph / RHYTHM: rest, density, swing, legato |
| **FX** | VOICE A: instrument, timbre, delay, level / VOICE B: the same for scene B |
| **LFO** | speed and depth of the six LFOs, two parameters a half: MORPH REST / LEGATO ENTROPY / HARMONY CHAOS |
| **GLO** | TEMPO KEY: BPM, step division, transpose, theme / SCALE MIDI: root, scale, MIDI channel of A and of B |

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
| **ALGORITHM** | The A-to-B crossfade, on every screen. |
| **PRESETS** | The edited scene's voice: MIDI only, ANALOG, FM, SUPERSAW, PULSE. |
| **SELECT** | Menus: SCALE and ROOT, COPY and INIT, SYSTEM (they go back after 2.5 seconds without a touch). |
| **MASTER** | Volume. |

### COPY and INIT menu (SELECT)

Knob 1 picks the action (copy A to B, copy B to A, init the edited scene, init everything); **SAVE** does it.

### SYSTEM menu

Theme (NAVY, MONOCHROME, MATRIX), the MIDI channels of scene A and B, and the DSP load: how much of the audio time the
sound takes. Keep it under about 50 %; if it is higher, use fewer chords or the lighter voices.

## Notes on what differs from the plugin

* **Morph** has no effect in the plugin's note engine; here it smears each step's probability into the next step's.
* **Legato** sets the note length here (10 % to 95 % of a step); in the plugin it reduces the rests.
* The plugin's Rate and Octaves LFOs are not here (six LFOs: Morph, Rest, Legato, Entropy, Harmony, Chaos).
* Chords come from the scale, as in the plugin; the held notes are the arp's pool.
* USB MIDI notes (any channel) are accepted like keys. The firmware does not send MIDI clock yet.
* Presets are kept in their own flash sector, your working state is saved by itself when the sound is quiet.
* No reverb; one delay (a dotted eighth), fed by each scene's send.
