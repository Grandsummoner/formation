/* Formation host unit test: MIDI CC and program change in, the pans, tap tempo, the settings record. cc -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen tests/unit_new.c */
#define main host_main_unused
#include "../firmware/src/formation.c"
#undef main
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok   %s\n", msg); } while (0)
int main(void)
{
    uint32_t i;
    int32_t pk;
    static int32_t out[2 * HALF_FRAMES];
    preset_init(&cur); weights();
    /* control changes: 0..127 over 0..255 */
    cc_in(8, 127);  CHECK(cur.morph_pos == 255, "CC 8: the crossfade at 127 is B");
    cc_in(8, 0);    CHECK(cur.morph_pos == 0, "CC 8: 0 is A");
    cc_in(12, 64);  CHECK(cur.density >= 128 && cur.density <= 130, "CC 12: density");
    cc_in(22, 127); CHECK(cur.sc[0].p[2] == 255, "CC 22: scene A LEGATO");
    cc_in(28, 127); CHECK(cur.sc[1].p[2] == 255, "CC 28: scene B LEGATO");
    cc_in(71, 100); CHECK(cur.sc[1].timbre > 190 && cur.sc[1].timbre < 205, "CC 71: timbre B");
    cc_in(85, 127); CHECK(cur.sc[0].rsend == 255, "CC 85: reverb send A (each voice has its own)");
    cc_in(87, 127); CHECK(cur.sc[0].rsize == 255 && cur.sc[1].rsize == 255, "CC 87: reverb size, both voices");
    cc_in(90, 0);   CHECK(fxs.pana == 0, "CC 90: pan A");
    CHECK(cc_in(3, 10) == 0, "an unassigned CC changes nothing");
    /* program change */
    bank_ok = 1;                                                    /* (the bank is set by hand here: pc_in opens it, and must not load over it) */
    { preset_t t = cur; t.used |= 1u; t.density = 77; slot_put(3, MODEL_FORMATION, &t, sizeof t); } running = 0;
    pc_in(3); CHECK(cur.density == 77, "program change 3 recalls preset 4 (stopped: at once)");
    pc_in(9); CHECK(cur.density == 77, "a program change to an empty slot does nothing");
    /* pans: a scene hard left sounds only on the left */
    preset_init(&cur); fxs.pana = 0; fxs.panb = 255; cur.morph_pos = 0; cur.sc[0].rsend = cur.sc[1].rsend = 0; cur.sc[0].dsend = 0; cur.sc[0].dfb = cur.sc[1].dfb = 0; weights();
    memset(sl, 0, sizeof sl); voice_on(0, 60);
    { int32_t pl = 0, pr = 0; uint32_t b;
      for (b = 0; b < 40; b++) { memset(out, 0, sizeof out); app_audio(out, HALF_FRAMES);
          for (i = 0; i < HALF_FRAMES; i++) { int32_t l = out[2 * i], r = out[2 * i + 1]; if (l < 0) l = -l; if (r < 0) r = -r; if (l > pl) pl = l; if (r > pr) pr = r; } }
      CHECK(pl > 500 && pr == 0, "PAN A hard left: sound on the left only"); }
    fxs.pana = 128; weights(); memset(sl, 0, sizeof sl); voice_on(0, 60);
    { int32_t pl = 0, pr = 0; uint32_t b;
      for (b = 0; b < 40; b++) { memset(out, 0, sizeof out); app_audio(out, HALF_FRAMES);
          for (i = 0; i < HALF_FRAMES; i++) { int32_t l = out[2 * i], r = out[2 * i + 1]; if (l < 0) l = -l; if (r < 0) r = -r; if (l > pl) pl = l; if (r > pr) pr = r; } }
      CHECK(pl > 500 && pr > 500 && (pl - pr < 40 && pr - pl < 40), "PAN A in the middle: the same on both sides"); }
    CHECK(meter_l > 0 && meter_r > 0, "the meter sees the peaks");
    /* tap tempo: taps 500 ms apart make 120 BPM, and a pause starts again */
    preset_init(&cur);
    fm1_ms = 10000; tap_tempo(); fm1_ms = 10500; tap_tempo(); fm1_ms = 11000; tap_tempo(); fm1_ms = 11500; tap_tempo();
    CHECK(bpm_get(&cur) == 120, "tap tempo: four taps 500 ms apart make 120 BPM");
    fm1_ms = 14000; tap_tempo(); fm1_ms = 14400; tap_tempo();
    CHECK(bpm_get(&cur) == 150, "tap tempo: after a pause it starts again (400 ms = 150 BPM)");
    /* the settings bytes of the global settings go round */
    { uint8_t f[8]; leg_dly[0] = 1; leg_dly[1] = 77; leg_dly[2] = 33; fxs.hint = 1; fxs.recm = 1; fxs.sync = 1; fxs.chord = 3; fxs.voic = 2; fxs.amode = 2; fxs.abars = 5; fxs.ashp = 1; fxs.csrc = 2;
      fxs.cmode = 2; fxs.meter = 0; fxs.ccin = 0; fxs.pcin = 1; fxs.pana = 11; fxs.panb = 222;
      fx_save(f);
      leg_dly[0] = leg_dly[1] = leg_dly[2] = fxs.hint = fxs.recm = fxs.sync = fxs.chord = fxs.voic = fxs.amode = fxs.abars = fxs.ashp = fxs.csrc = fxs.cmode = fxs.meter = fxs.ccin = fxs.pcin = fxs.pana = fxs.panb = 0;
      fx_load(f, 8);
      CHECK(leg_dly[0] == 1 && leg_dly[1] == 77 && leg_dly[2] == 33 && fxs.hint == 1 && fxs.recm == 1 && fxs.sync == 1 && fxs.chord == 3 && fxs.voic == 2 && fxs.amode == 2 && fxs.abars == 5 && fxs.ashp == 1 && fxs.csrc == 2, "the settings bytes: the old delay, help, REC, sync, chord, AUTO, clock go round");
      CHECK(fxs.cmode == 2 && fxs.meter == 0 && fxs.ccin == 0 && fxs.pcin == 1 && fxs.pana == 11 && fxs.panb == 222, "the settings bytes: colour, meter, CC / program in, pans go round"); }
    return fails ? 1 : (printf("ALL OK\n"), 0);
}
