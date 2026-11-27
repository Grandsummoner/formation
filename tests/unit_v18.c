/* Formation host unit test, v1.8: the drum voice (it follows the melody and stays under it), the new preset layout and the older ones read.
 * cc -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen tests/unit_v18.c */
#define main host_main_unused
#include "../firmware/src/formation.c"
#undef main
static preset_t sp(uint32_t k) { preset_t t; memcpy(&t, bank.s[k].data, sizeof t); return t; }          /* the FORMATION preset in a slot */
static void sp_put(uint32_t k, const preset_t *t) { slot_put(k, MODEL_FORMATION, t, sizeof *t); }
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok   %s\n", msg); } while (0)

static int32_t blk[64][2 * HALF_FRAMES];
static void render(uint32_t blocks)             /* blocks of the output into blk (the loudest sample of each, per block, is what the checks use) */
{
    uint32_t b;
    for (b = 0; b < blocks; b++) { memset(blk[b], 0, sizeof blk[b]); app_audio(blk[b], HALF_FRAMES); }
}
static int32_t peak_of(uint32_t blocks)
{
    uint32_t b, i;
    int32_t p = 0;
    for (b = 0; b < blocks; b++) for (i = 0; i < 2u * HALF_FRAMES; i++) { int32_t v = blk[b][i] < 0 ? -blk[b][i] : blk[b][i]; if (v > p) p = v; }
    return p;
}
static void quiet(void)
{
    memset(sl, 0, sizeof sl); memset(drm, 0, sizeof drm); mel_env = 0; memset(rv_d, 0, sizeof rv_d); memset(ap_d, 0, sizeof ap_d); memset(dly, 0, sizeof dly);
    rv_run[0] = rv_run[1] = 0; rr[0] = rr[1] = 0; dr_act = 0;
}
static void clear_hits(void) { memset(dr_hits, 0, sizeof dr_hits); dr_vmax = 0; }
static uint32_t total_hits(uint32_t from) { uint32_t i, n = 0; for (i = from; i < NDR; i++) n += dr_hits[i]; return n; }
static void melody_setup(void)                  /* the arp plays a rising scale on every step: the melody is always sounding */
{
    uint32_t i;
    preset_init(&cur);
    for (i = 0; i < 8u; i++) { cur.sc[0].fader[i] = cur.sc[1].fader[i] = 255; }
    cur.sc[0].p[1] = cur.sc[1].p[1] = 0; cur.sc[0].p[3] = cur.sc[1].p[3] = 127; cur.sc[0].p[2] = cur.sc[1].p[2] = 128; cur.sc[0].p[5] = cur.sc[1].p[5] = 0;
    cur.density = 128; held_n = 0; latch = 0; frozen = 0; mute_mask = 0; pend_slot = -1; rec_state = 0; chain_on = 0; mot_on = 0;
    memset(eff, 0, sizeof eff); eff[3] = 127; eff[2] = 128; cstep = 7; running = 0;
    dm_have = 0; dm_act = 0; dm_gap = 0;
}
static void run_steps(uint32_t n) { uint32_t i; for (i = 0; i < n; i++) { eff[1] = 0; eff[5] = 0; do_step(); } }

int main(void)
{
    uint32_t i;
    /* ---- the layouts */
    CHECK(sizeof(preset_t) == sizeof(preset17_t) + 11 && sizeof(preset17_t) == 106 && sizeof(preset16_t) == 80, "the preset layouts: 117 B now, 106 B in v1.7, 80 B before");
    CHECK(sizeof(prebank_t) <= 4096u && sizeof(saved6_t) <= 512u, "the bank and the settings record fit");
    preset_init(&cur);
    CHECK(cur.dr_on == 1 && cur.dr_level == 150 && cur.dr_follow == 1 && cur.dr_shadow[0] == 140 && cur.dr_space[1] == 150 && cur.dr_tune == 128, "a new preset: the drums are on, gently");
    {
        preset17_t o;
        preset_t n;
        memset(&o, 0, sizeof o);
        o.sc[0].inst = 4; o.sc[1].env[2] = 77; o.lfo_sp[3] = 0x2A; o.density = 99; o.used = 1; o.scale = 6;
        preset_from17(&n, &o);
        CHECK(n.sc[0].inst == 4 && n.sc[1].env[2] == 77 && n.lfo_sp[3] == 0x2A && n.density == 99 && n.scale == 6 && (n.used & 1u), "a v1.7 preset: every byte comes over");
        CHECK(n.dr_on == 0 && n.dr_level == 150 && n.dr_space[0] == 150, "a v1.7 preset: the drums are off (it sounds as it did), their settings the defaults");
    }
    {                                                                           /* a PNV3 bank is converted in place */
        static preset17_t src[16];
        uint32_t k;
        memset(&bank, 0, sizeof bank);
        for (k = 0; k < 16u; k++) { memset(&src[k], 0, sizeof src[k]); src[k].density = (uint8_t)(20 + k); src[k].sc[1].rsize = (uint8_t)(50 + k); src[k].used = 1; }
        bank.magic = PRE_MAGIC3;
        memcpy((uint8_t *)&bank + 8, src, sizeof src);
        bank_conv(0);
        for (k = 0; k < 16u; k++) { preset_t t = sp(k); if (t.density != 20 + k || t.sc[1].rsize != 50 + k || bank.s[k].model != MODEL_FORMATION || t.dr_on) break; }
        CHECK(k == 16u && bank.magic == PRE_MAGIC, "a PNV3 bank: all sixteen slots converted in place, drums off");
        bank_ok = 1;
        { preset_t t = sp(2); t.dr_on = 1; t.dr_shadow[1] = 33; t.used |= 1u; sp_put(2, &t); }
        bank_save();
        memset(&bank, 0, sizeof bank); bank_ok = 0;
        CHECK(bank_load() == 0 && sp(2).dr_shadow[1] == 33 && sp(2).dr_on == 1 && sp(5).density == 25, "a PNV5 bank saves and loads");
        bank_ok = 0;
    }
    {                                                                           /* the v1.7 settings record: read and converted */
        saved5_t o5;
        memset(&o5, 0, sizeof o5);
        o5.ver = 1; o5.theme = 3; o5.ch0 = 2; o5.ch1 = 5; o5.slot = 7; o5.cur.density = 66; o5.cur.sc[0].env[0] = 140; o5.chain[1] = 0x85;
        host_store[0] = 0;
        settings_save(&o5, sizeof o5);
        memset(chain, 0, sizeof chain);
        app_init();
        CHECK(cur.density == 66 && cur.sc[0].env[0] == 140 && glob.theme == 3 && glob.ch[1] == 5 && glob.slot == 7 && chain[1] == 0x85, "a v1.7 settings record is read");
        CHECK(cur.dr_on == 0 && glob.ch[2] == 9, "a v1.7 settings record: drums off, on channel 10 if you turn them on");
        host_store[0] = 0;
        preset_init(&cur); cur.dr_shadow[1] = 77; glob.ch[2] = 3; cur.dr_tune = 131;
        memset(&sv, 0, sizeof sv); sv.f = cur; sv.ver = 1; sv.ch2 = glob.ch[2]; fx_save(sv.fx);
        settings_save(&sv, sizeof sv);
        cur.dr_shadow[1] = 0; glob.ch[2] = 0; cur.dr_tune = 0;
        app_init();
        CHECK(cur.dr_shadow[1] == 77 && glob.ch[2] == 3 && cur.dr_tune == 131, "the v1.8 settings record goes round (drums, their channel)");
        host_store[0] = 0;
    }

    /* ---- they follow the melody */
    {
        uint32_t k, bad = 0, kicks = 0;
        melody_setup(); cur.root = 7; cur.dr_follow = 1; clear_hits();
        for (i = 0; i < 400u; i++) {
            uint32_t before = dr_hits[DR_KICK];
            eff[1] = 0; do_step();
            if (dr_hits[DR_KICK] != before) { kicks++; if (cstep != 0u && cstep != 4u) bad++; }
        }
        CHECK(kicks > 40 && bad == 0, "kick: only on the first and the middle of the measure");
        CHECK(dr_last[DR_KICK] == 43u || dr_last[DR_KICK] == 50u, "kick: tuned to the key (G here: the tonic, or the fifth)");
        melody_setup(); cur.root = 0; cur.dr_follow = 0; clear_hits(); k = 0;
        for (i = 0; i < 300u; i++) { eff[1] = 0; do_step(); if (dr_last[DR_KICK] != 36u && dr_hits[DR_KICK]) k++; }
        CHECK(dr_hits[DR_KICK] > 20 && k == 0, "kick: FOLLOW off, it stays on the tonic");
    }
    {
        uint32_t busy_hits, lots, gaps;
        melody_setup(); cur.dr_space[0] = cur.dr_space[1] = 0; clear_hits(); run_steps(800); lots = total_hits(2);
        melody_setup(); cur.dr_space[0] = cur.dr_space[1] = 255; clear_hits(); run_steps(800); busy_hits = total_hits(2);
        CHECK(busy_hits * 2 < lots, "SPACE: a busy melody leaves much less for the drums when SPACE is up");
        melody_setup(); cur.density = 0; cur.dr_space[0] = cur.dr_space[1] = 255; clear_hits(); run_steps(800); gaps = total_hits(4);
        CHECK(gaps > busy_hits, "gaps: when the melody rests the hats and rim fill in, even with SPACE up");
        melody_setup(); cur.dr_on = 0; clear_hits(); run_steps(200);
        CHECK(total_hits(0) == 0, "drums off: nothing is hit");
        melody_setup(); cur.dr_shadow[0] = cur.dr_shadow[1] = 0; cur.dr_space[0] = cur.dr_space[1] = 0; clear_hits(); run_steps(800); lots = dr_hits[DR_BRUSH];
        melody_setup(); cur.dr_shadow[0] = cur.dr_shadow[1] = 255; cur.dr_space[0] = cur.dr_space[1] = 0; clear_hits(); run_steps(800);
        CHECK(dr_hits[DR_BRUSH] > lots + 20, "SHADOW: the more of it, the more the drums answer the melody (a brush on each falling leap)");
    }
    {                                                                           /* a rising leap: a tom, tuned to the melody */
        uint8_t pool[4] = {48, 60, 52, 72};
        uint32_t seen = 0, ok = 1;
        melody_setup(); memcpy(held, pool, 4); held_n = 4; cur.dr_shadow[0] = cur.dr_shadow[1] = 255; cur.dr_space[0] = cur.dr_space[1] = 0; clear_hits();
        for (i = 0; i < 400u; i++) {
            uint32_t before = dr_hits[DR_TOM];
            eff[1] = 0; do_step();
            if (dr_hits[DR_TOM] != before) { seen++; if (dr_last[DR_TOM] != (uint32_t)ds_raw - 12u) ok = 0; }
        }
        CHECK(seen > 10 && ok, "tom: on rising leaps, on the melody's note an octave down");
    }
    {
        melody_setup(); cur.dr_level = 255; cur.dr_shadow[0] = cur.dr_shadow[1] = 255; clear_hits(); run_steps(600);
        CHECK(dr_vmax <= 100, "the hits are soft: none louder than velocity 100");
        CHECK(total_hits(0) > 100, "(and they do play)");
    }
    {
        uint32_t n = 0;
        melody_setup(); clear_hits(); running = 1;
        for (i = 0; i < 40u; i++) { eff[1] = 0; do_step(); }
        for (i = 0; i < 32u; i++) if (poff[i].on && poff[i].sc == 2) n++;
        CHECK(n > 0, "drums send MIDI notes on their own channel, with a note off to follow");
        offs_run(0, 1);
        for (i = 0, n = 0; i < 32u; i++) if (poff[i].on) n++;
        CHECK(n == 0, "(and the note offs go out)");
        running = 0;
    }

    /* ---- they stay under the melody */
    {
        int32_t mel, drums_alone, with_both, drums_under;
        static int32_t keep[8][2 * HALF_FRAMES];
        uint32_t b;
        preset_init(&cur); cur.morph_pos = 0; cur.sc[0].rsend = cur.sc[1].rsend = 0; cur.sc[0].dsend = 0; cur.dr_send = 0; cur.dr_on = 1; cur.dr_level = 255; weights();
        quiet(); voice_on(0, 60); render(30); mel = peak_of(30);
        quiet(); drum_hit(DR_KICK, 100, 36); render(30); drums_alone = peak_of(30);
        CHECK(drums_alone < mel, "level: one soft kick, the drum level at its top, is quieter than one note");
        quiet(); voice_on(0, 60); drum_hit(DR_KICK, 100, 36); drum_hit(DR_HAT, 100, 0); render(8);
        for (b = 0; b < 8u; b++) memcpy(keep[b], blk[b], sizeof keep[b]);
        with_both = peak_of(8);
        dr_gain = 0; quiet(); voice_on(0, 60); render(8);                       /* the same, without the drums: the difference is the drums */
        drums_under = 0;
        for (b = 0; b < 8u; b++) for (i = 0; i < 2u * HALF_FRAMES; i++) { int32_t d = keep[b][i] - blk[b][i]; if (d < 0) d = -d; if (d > drums_under) drums_under = d; }
        CHECK(drums_under < mel, "level: a kick and a hat together, on a sounding note, are still quieter than the note");
        (void)with_both;
        weights();
    }
    {
        int32_t alone, under;
        uint32_t b;
        static int32_t keep[8][2 * HALF_FRAMES];
        preset_init(&cur); cur.morph_pos = 0; cur.sc[0].rsend = cur.sc[1].rsend = 0; cur.sc[0].dsend = 0; cur.dr_send = 0; cur.dr_level = 255; weights();
        quiet(); drum_hit(DR_KICK, 100, 36); render(8); alone = peak_of(8);
        quiet(); voice_on(0, 60); render(40); drum_hit(DR_KICK, 100, 36); render(8);
        for (b = 0; b < 8u; b++) memcpy(keep[b], blk[b], sizeof keep[b]);
        dr_gain = 0; quiet(); voice_on(0, 60); render(40); drum_hit(DR_KICK, 100, 36); render(8);
        under = 0;
        for (b = 0; b < 8u; b++) for (i = 0; i < 2u * HALF_FRAMES; i++) { int32_t d = keep[b][i] - blk[b][i]; if (d < 0) d = -d; if (d > under) under = d; }
        CHECK(under * 10 < alone * 7, "ducking: the same kick is at least 3 dB quieter while the melody sounds");
        weights();
    }
    {
        quiet(); preset_init(&cur); cur.dr_level = 255; weights();
        CHECK(dr_gain > 0 && dr_gain <= (255 * DR_CAP) >> 8, "the drum level has a ceiling");
        fm1_ms = 90000; do_panic(); weights();
        CHECK(dr_gain == 0, "PANIC shuts the drums too");
        fm1_ms = 92000; weights();
        CHECK(dr_gain > 0, "(and they are back after)");
    }

    /* ---- the controls */
    {
        cell_t c = {K_DTUNE, 0}, sh = {K_DSHD, 1};
        char name[12], val[16];
        int32_t bar;
        uint32_t scn;
        preset_init(&cur);
        cell_adj(c, 1); cell_adj(c, 1); cell_adj(c, 1);
        cell_text(c, name, val, &bar, &scn);
        CHECK(strcmp(val, "+3") == 0, "TUNE goes in semitones (+3)");
        cell_adj(c, -1); cell_adj(c, -1); cell_adj(c, -1); cell_adj(c, -1);
        cell_text(c, name, val, &bar, &scn);
        CHECK(strcmp(val, "-1") == 0, "TUNE goes in semitones (-1)");
        cell_text(sh, name, val, &bar, &scn);
        CHECK(scn == 2 && strcmp(name, "SHDW B") == 0, "SHADOW of scene B is in B's colour");
        glob.ch[2] = 9; cell_adj((cell_t){K_DCH, 0}, 1); CHECK(glob.ch[2] == 10, "the drum channel moves");
        glob.ch[2] = 9;
        cc_in(94, 127); cc_in(95, 0); cc_in(96, 127);
        CHECK(cur.dr_level == 255 && cur.dr_shadow[0] == 0 && cur.dr_shadow[1] == 0 && cur.dr_space[0] == 255, "CC 94 / 95 / 96: the drums' level, shadow, space");
        CHECK(pages_in(G_DRUM) == 2 && HALF[group_first(G_DRUM)].group == G_DRUM, "the drum pages: two of them");
    }
    return fails ? 1 : (printf("ALL OK\n"), 0);
}
