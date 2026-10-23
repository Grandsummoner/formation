# Formation manual

Formation is a probabilistic arpeggiator and step sequencer with two **scenes**, A and B, that you crossfade with
Knob 4. Each scene has eight step probabilities and its own voice; both send MIDI out over USB and play the FM-1's own
sound. It is a port of the Navy Arp 2 plugin.

## The idea

Eight steps run in a loop at the BPM. At each step the engine rolls a die against that step's level (the two scenes'
levels are crossfaded), then against **Rest**. If the step sounds, it plays a note, maybe a chord (**Harmony**, with
POLY on), maybe an octave jump (**Chaos**). **Entropy** changes the direction the steps run (forward, ping-pong, random
walk, reverse, skipping). **Legato** sets how long notes last. **Density** raises or lowers all the probabilities.
**Swing** delays every other step. **Morph** smears each step's level into the next one.

Scene A plays on MIDI channel 1 and scene B on channel 2 (SYSTEM page); the crossfade moves note velocity (and the
sound) from A to B with an equal-power curve.

## Controls

### Keys (the 27 capsule keys)

| Keys | What |
| --- | --- |
| **Lower 16 (white keys)** | The keyboard. Notes you hold are the arp's pool (with LATCH they stay after you let go). With the clock stopped they just play. On the SAVE and RECALL screens they are slots 1-16. |
| **Upper keys 1-8** | The steps. Hold one and turn **Knob 1** to set that step's probability (hold several to set them together). In SEQ mode: hold a step key and tap a lower key to set that step's note. |
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
| **OCT- / OCT+** | Octave, -3 to +3. |
| **FX** | The FX page (for the edited scene): timbre, delay send, level. |
| **LFO** | LFO page 1 (Morph, Rest, Legato), page 2 (Entropy, Harmony, Chaos), off. |
| **GLO** | Global page: BPM, step division, transpose. |
| **HOME** | Back to the performance screen. |
| **SAVE** | Short press: save mode (blinks). Pick a slot 1-16 with a lower key; SAVE again cancels. **Long press:** recall mode (stays lit): pick a slot and it starts at the end of the measure; long press again to leave. Filled slots are lit. |
| ENV, EDIT | Not used yet. |

### Knobs

| Knob | Performance screen | Other screens |
| --- | --- | --- |
| **Knob 1** | The level of the held step key(s) | FX: timbre · LFO: which row · GLO: BPM · SCALE: root · RANDOM: first parameter |
| **Knob 2** | Density | FX: delay send · LFO: speed (off, 1/4, 1/8, 1/16, 1/32 of a beat) · GLO: step division · SCALE: scale · RANDOM: second |
| **Knob 3** | Swing | FX: level · LFO: depth · GLO: transpose · RANDOM: third |
| **Knob 4** | A to B crossfade | (everywhere) |
| **SELECT** | Menu: SCALE and ROOT, COPY and INIT, SYSTEM | |
| **ALGORITHM** | RANDOM pages: 1 = Morph, Rest, Legato; 2 = Entropy, Harmony, Chaos | |
| **PRESETS** | The edited scene's voice: MIDI only, ANALOG, FM, SUPERSAW, PULSE | |
| **MASTER** | Volume | |

The menu and RANDOM screens go back to the performance screen after 2.5 seconds without a touch. The FX, LFO and GLO
pages stay until you press their button again or HOME.

### Dice

On a RANDOM page, the upper keys 1-5 roll: 1 MELO (step levels), 2 ARTI (rest and legato), 3 TIME (division and octave),
4 NAVY (morph, entropy, harmony, chaos), 5 all.

### COPY and INIT page (SELECT)

Knob 1 picks the action (copy A to B, copy B to A, init the edited scene, init everything); **SAVE** does it.

### SYSTEM page

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
