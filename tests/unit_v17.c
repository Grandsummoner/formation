/* Formation host unit test, v1.7: the old saves read as before, ENV, LFO shapes, a delay and a reverb for each voice, the punch-ins, REC's capture,
 * the CHAIN, the panic, the pages. cc -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen tests/unit_v17.c */
#define main host_main_unused
#include "../firmware/src/formation.c"
#undef main
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok   %s\n", msg); } while (0)

static int32_t peak_after(uint32_t blocks_skip, uint32_t blocks)       /* the loudest sample over some blocks, after skipping some */
{
    static int32_t out[2 * HALF_FRAMES];
    uint32_t b, i;
    int32_t pk = 0;
    for (b = 0; b < blocks_skip + blocks; b++) {
        memset(out, 0, sizeof out);
        app_audio(out, HALF_FRAMES);
        if (b >= blocks_skip)
            for (i = 0; i < 2u * HALF_FRAMES; i++) { int32_t v = out[i] < 0 ? -out[i] : out[i]; if (v > pk) pk = v; }
    }
    return pk;
}
static void quiet(void)
{
    uint32_t i;
    memset(sl, 0, sizeof sl);
    memset(dly, 0, sizeof dly); memset(rv_d, 0, sizeof rv_d); memset(ap_d, 0, sizeof ap_d);
    memset(dly_lp, 0, sizeof dly_lp); memset(rv_lp, 0, sizeof rv_lp); rv_run[0] = rv_run[1] = 0;
    for (i = 0; i < 2u; i++) rr[i] = 0;
}

int main(void)
{
    uint32_t i;
    /* ---- the old layouts read, and are converted */
    {
        preset16_t o;
        preset_t n;
        memset(&o, 0, sizeof o);
        o.sc[0].inst = 5; o.sc[0].timbre = 11; o.sc[0].dsend = 22; o.sc[0].level = 33; o.sc[0].fader[3] = 99; o.sc[0].seq[2] = 61;
        o.sc[1].inst = 9; o.sc[1].level = 44;
        o.lfo_rate[2] = 3; o.lfo_depth[2] = 70; o.density = 111; o.bpm = 200; o.used = 1 | (1u << 1); o.scale = 4; o.root = 7; o.poly = 1;
        o.rv[0] = 10; o.rv[1] = 20; o.rv[2] = 30; o.rv[3] = 40;
        leg_dly[0] = 3; leg_dly[1] = 55; leg_dly[2] = 66;
        preset_from16(&n, &o);
        CHECK(n.sc[0].inst == 5 && n.sc[0].timbre == 11 && n.sc[0].dsend == 22 && n.sc[0].level == 33 && n.sc[0].fader[3] == 99 && n.sc[0].seq[2] == 61 && n.sc[1].inst == 9 && n.sc[1].level == 44, "a v1.6 preset: the scenes come over");
        CHECK(n.sc[0].rsend == 10 && n.sc[1].rsend == 20 && n.sc[0].rsize == 30 && n.sc[1].rsize == 30 && n.sc[0].rtone == 40 && n.sc[1].rtone == 40, "a v1.6 preset: the one reverb becomes one for each voice");
        CHECK(n.sc[0].dtime == 3 && n.sc[0].dfb == 55 && n.sc[1].dtone == 66, "a v1.6 preset: the one delay becomes one for each voice");
        CHECK(n.sc[0].env[0] == 128 && n.sc[1].env[3] == 128 && n.lfo_sp[0] == 0, "a v1.6 preset: ENV is the instrument's own, the LFOs are sines at phase 0");
        CHECK(n.lfo_rate[2] == 3 && n.lfo_depth[2] == 70 && n.density == 111 && bpm_get(&n) == 200 + 256 && n.scale == 4 && n.root == 7 && n.poly == 1 && (n.used & 1u), "a v1.6 preset: the rest comes over (the tempo's high bits too)");
        memset(&o, 0, sizeof o);
        preset_from16(&n, &o);
        CHECK(n.sc[0].rsend == 40 && n.sc[0].rsize == 170 && n.sc[0].rtone == 160, "a preset from before the reverb gets its defaults");
        leg_dly[0] = 2; leg_dly[1] = 100; leg_dly[2] = 255;
    }
    {                                                                           /* a whole PNV2 bank, converted in place */
        static preset16_t src[16];
        uint32_t k;
        memset(&bank, 0, sizeof bank);
        for (k = 0; k < 16u; k++) { memset(&src[k], 0, sizeof src[k]); src[k].density = (uint8_t)(10 + k); src[k].sc[1].level = (uint8_t)(100 + k); src[k].used = 1; src[k].rv[0] = (uint8_t)(1 + k); src[k].rv[2] = 77; }
        bank.magic = PRE_MAGIC2;
        memcpy((uint8_t *)&bank + 8, src, sizeof src);
        bank_conv(1);
        for (k = 0; k < 16u; k++)
            if (bank.p[k].density != 10 + k || bank.p[k].sc[1].level != 100 + k || !(bank.p[k].used & 1u) || bank.p[k].sc[0].rsend != 1 + k || bank.p[k].sc[0].rsize != 77) break;
        CHECK(k == 16u && bank.magic == PRE_MAGIC, "a PNV2 bank: all sixteen slots converted in place");
        CHECK(sizeof(prebank_t) <= 4096u, "the new bank fits its flash sector");
        CHECK(sizeof(saved6_t) <= 512u, "the settings record fits the settings sector's buffer");
    }
    {                                                                           /* the working state of v1.6 in the settings: app_init reads it */
        saved4_t o4;
        memset(&o4, 0, sizeof o4);
        o4.s.ver = 1; o4.s.theme = 2; o4.s.ch0 = 3; o4.s.ch1 = 4; o4.s.slot = 5;
        o4.s.cur.density = 99; o4.s.cur.sc[0].inst = 7; o4.s.cur.rv[1] = 66; o4.s.cur.rv[0] = 1; o4.s.cur.rv[2] = 5; o4.s.cur.bpm = 90;
        o4.fx[0] = 1 | (2 << 2); o4.fx[1] = 77; o4.fx[2] = 33; o4.fx[3] = 2; o4.fx[5] = 40; o4.fx[6] = 200;
        host_store[0] = 0;
        settings_save(&o4, sizeof o4);
        cur.density = 0;
        app_init();
        CHECK(cur.density == 99 && cur.sc[0].inst == 7 && cur.sc[1].rsend == 66 && glob.theme == 2 && glob.ch[0] == 3 && glob.ch[1] == 4 && glob.slot == 5, "a v1.6 settings record: the working state is read and converted");
        CHECK(cur.sc[0].dtime == 1 && cur.sc[0].dfb == 77 && cur.sc[1].dtone == 33 && fxs.pana == 40 && fxs.panb == 200 && fxs.sync == 2, "a v1.6 settings record: the delay moves into the voices, the pans stay");
        host_store[0] = 0;
    }

    /* ---- ENV */
    CHECK(env_atk(INST_ANALOG, 128) == 240 && env_dec(INST_ANALOG, 128) == 13230 && env_rel(INST_ANALOG, 128) == 6615, "ENV at 128 is the instrument's own");
    CHECK(env_atk(INST_ANALOG, 148) >= 478 && env_atk(INST_ANALOG, 148) <= 482, "ENV: 20 steps up doubles the attack");
    CHECK(env_rel(INST_ANALOG, 108) >= 3300 && env_rel(INST_ANALOG, 108) <= 3320, "ENV: 20 steps down halves the release");
    CHECK(env_atk(INST_PAD, 255) == ATK_MAX && env_atk(INST_PLUCK, 0) == 8u && env_rel(INST_BELL, 255) == REL_MAX, "ENV: the extremes are held at their limits");
    CHECK(env_sus(INST_ANALOG, 128) == 178 && env_sus(INST_ANALOG, 0) == 0 && env_sus(INST_ANALOG, 255) == 255, "ENV: the sustain is a level, 128 the instrument's own");
    {
        uint32_t base_atk;
        preset_init(&cur); quiet(); weights();
        voice_on(0, 60);
        base_atk = sl[0].eatk;
        cur.sc[0].env[0] = 148; quiet(); voice_on(0, 60);
        CHECK(sl[0].eatk > base_atk * 19 / 10 && sl[0].eatk < base_atk * 21 / 10, "ENV: a note takes its voice's attack");
        cur.sc[0].env[0] = 128; cur.sc[0].inst = INST_BELL;
        {   cell_t c = {K_INST, 0}; cur.sc[0].env[1] = 200; cur.sc[0].inst = INST_SAW; cell_adj(c, 1); }
        CHECK(cur.sc[0].env[1] == 128 && cur.sc[0].env[0] == 128, "ENV: a new instrument starts from its own envelope again");
    }
    {                                                                           /* the envelope is heard: a short release dies sooner than a long one */
        int32_t a, b;
        preset_init(&cur); cur.sc[0].inst = INST_ANALOG; cur.sc[0].rsend = cur.sc[1].rsend = 0; cur.sc[0].dsend = cur.sc[1].dsend = 0; cur.morph_pos = 0; weights();
        cur.sc[0].env[3] = 80; quiet(); weights(); voice_on(0, 60); peak_after(0, 20); voice_off(0, 60); a = peak_after(10, 4);
        cur.sc[0].env[3] = 180; quiet(); weights(); voice_on(0, 60); peak_after(0, 20); voice_off(0, 60); b = peak_after(10, 4);
        CHECK(b > a * 3 / 2, "ENV: a long release is still ringing when a short one is gone");
    }

    /* ---- LFO */
    CHECK(lfo_wave(0, 0, 0) == 0 && lfo_wave(1, 0, 0) == 0 && lfo_wave(1, 0x40000000u, 0) >= 32766 && lfo_wave(1, 0xC0000000u, 0) <= -32766, "LFO: sine and triangle start at zero, the triangle peaks at a quarter");
    CHECK(lfo_wave(2, 0, 0) <= -32766 && lfo_wave(2, 0xFFFF0000u, 0) >= 32766 && lfo_wave(3, 0, 0) >= 32766 && lfo_wave(3, 0xFFFF0000u, 0) <= -32766, "LFO: saw rises, ramp falls");
    CHECK(lfo_wave(4, 0, 0) == 32767 && lfo_wave(4, 0x80000000u, 0) == -32767 && lfo_wave(5, 0, 1234) == 1234, "LFO: square, and random holds its value");
    CHECK(lfo_period(1000000u, 1) == 1000000u && lfo_period(1000000u, 4) == 125000u && lfo_period(1000000u, 5) == 2000000u && lfo_period(1000000u, 6) == 4000000u && lfo_period(1000000u, 8) == 16000000u, "LFO: the rates are a beat down to 1/32 and up to 4 bars");
    {
        uint32_t p;
        for (p = 0; p < 9u; p++) { uint32_t q, ok = 0; for (q = 0; q < 9u; q++) if (LFO_POS[q] == p) ok++; if (ok != 1u) break; }
        CHECK(p == 9u, "LFO: the rate knob reaches every rate exactly once");
    }
    {
        int32_t hi = 0, lo = 0;
        preset_init(&cur); bpm_set(&cur, 120); cur.lfo_rate[1] = 1; cur.lfo_depth[1] = 255; cur.lfo_sp[1] = 4;            /* REST: a square, once a beat */
        lfo_reset();
        for (i = 0; i < 400u; i++) { update_eff(24000u * 5u); if (eff[1] > hi) hi = eff[1]; }       /* 5 ms steps, 2 s */
        lo = 255; lfo_reset();
        for (i = 0; i < 400u; i++) { update_eff(24000u * 5u); if (eff[1] < lo) lo = eff[1]; }
        CHECK(hi >= cur.sc[0].p[1] + 100 && lo == 0, "LFO: a square at full depth swings the parameter both ways");
        cur.lfo_sp[1] = 0; lfo_reset(); update_eff(1u); CHECK(lfo_out[1] == 0, "LFO: reset puts it back at its start");
        cur.lfo_sp[1] = (uint8_t)(16u << 3) | 1u;                              /* a triangle, half a cycle on */
        lfo_reset(); update_eff(1u); CHECK(lfo_out[1] < 100 && lfo_out[1] > -100, "LFO: PHASE offsets the start (the triangle half a cycle on is at zero)");
        cur.lfo_sp[1] = (uint8_t)(8u << 3) | 1u;                               /* a quarter on: the peak */
        lfo_reset(); update_eff(1u); CHECK(lfo_out[1] > 32000, "LFO: PHASE a quarter on puts the triangle at its peak");
        cur.lfo_sp[1] = 5; cur.lfo_rate[1] = 4; { int32_t a = 0, b = 0; lfo_reset();
            for (i = 0; i < 2000u; i++) { update_eff(24000u * 2u); if (i == 600u) a = lfo_out[1]; if (i == 1500u) b = lfo_out[1]; }
            CHECK(a != b, "LFO: RANDOM picks new values"); }
        cur.lfo_rate[1] = 0; update_eff(24000u); CHECK(lfo_out[1] == 0, "LFO: rate OFF is silent");
    }

    /* ---- a delay and a reverb for each voice */
    {
        int32_t with, without;
        preset_init(&cur); cur.morph_pos = 128; cur.sc[0].rsend = cur.sc[1].rsend = 0; cur.sc[1].dsend = 0; cur.sc[0].dsend = 200; cur.sc[0].dfb = 0; cur.sc[0].dtime = 3; weights();               /* a beat: the echo comes long after the dry sound has died */
        quiet(); voice_on(0, 60); peak_after(0, 12); voice_off(0, 60); with = peak_after(110, 60);
        cur.sc[0].dsend = 0; weights(); quiet(); voice_on(0, 60); peak_after(0, 12); voice_off(0, 60); without = peak_after(110, 60);
        CHECK(with > without * 3 / 2, "delay: voice A's own delay returns an echo after the note");
    }
    {
        int32_t a, b;
        preset_init(&cur); cur.morph_pos = 0; cur.sc[0].rsend = 0; cur.sc[1].rsend = 255; cur.sc[0].dsend = cur.sc[1].dsend = 0; weights();
        quiet(); voice_on(0, 60); peak_after(0, 12); voice_off(0, 60); a = peak_after(60, 20);          /* A sounds, B's reverb is on but A has none: no tail from A */
        cur.sc[0].rsend = 255; weights(); quiet(); voice_on(0, 60); peak_after(0, 12); voice_off(0, 60); b = peak_after(60, 20);
        CHECK(b > a * 2 && b > 50, "reverb: a voice's tail needs its own send");
        CHECK(rv_s[0] == 255 && rv_s[1] == 255, "reverb: each voice has its own send");
    }
    {
        preset_init(&cur); cur.sc[0].dtime = 0; cur.sc[1].dtime = 3; cur.sc[0].rsize = 10; cur.sc[1].rsize = 250; bpm_set(&cur, 100); weights();
        CHECK(dly_len_v[0] != dly_len_v[1] && dly_len_v[0] == (int32_t)(DT_K[0] / 100u) && rv_g[0] != rv_g[1], "each voice's delay time and reverb size are its own");
        bpm_set(&cur, 1); weights();
        CHECK(dly_len_v[1] <= (int32_t)DLN - 8, "the delay time stays inside its line at slow tempos");
    }

    /* ---- the punch-ins */
    {
        preset_init(&cur); cur.sc[0].dsend = 10; cur.sc[1].dsend = 10; cur.sc[0].level = 100; cur.sc[1].level = 100; cur.sc[0].timbre = 50; weights();
        pi_mask = 1; pi_voice = 0; weights();
        CHECK(ds_v[0] == 255 && ds_v[1] == 10, "punch-in THROW: only the voice the page shows");
        pi_voice = 2; weights();
        CHECK(ds_v[0] == 255 && ds_v[1] == 255, "punch-in THROW: on the MIX page both");
        pi_mask = 0; weights();
        CHECK(ds_v[0] == 10 && ds_v[1] == 10, "punch-in: let go, it is as it was");
        pi_mask = 1u << 11; pi_voice = 1; weights();
        CHECK(gain_v[1] == 0 && gain_v[0] != 0, "punch-in KILL: that voice goes quiet");
        pi_mask = 1u << 9; pi_voice = 0; weights(); voice_on(0, 60);
        CHECK(sl[0].tim == 255, "punch-in OPEN: the timbre opens");
        pi_mask = 0; weights();
        CHECK(sl[0].tim == 50, "punch-in OPEN: and closes back when let go (the notes still sounding follow)");
        pi_mask = 1u << 12; pi_voice = 0; weights();
        CHECK(sl[0].erel == env_rel(cur.sc[0].inst, 128) * 8u, "punch-in LONG: the release eight times as long");
        pi_mask = 0; pi_voice = 2; weights();
    }

    /* ---- REC: a capture */
    {
        uint32_t n;
        preset_init(&cur); cur.sc[0].p[1] = cur.sc[1].p[1] = 0; cur.sc[0].p[3] = cur.sc[1].p[3] = 127; cur.sc[0].p[2] = 128; bpm_set(&cur, 120);
        for (i = 0; i < 8u; i++) { cur.sc[0].fader[i] = cur.sc[1].fader[i] = 255; }
        cur.sc[0].fader[2] = cur.sc[1].fader[2] = 0;                              /* step 3 never sounds */
        cur.density = 128; fxs.recm = 0; mot_on = 0; frozen = 0; held_n = 0; latch = 0; mute_mask = 0; pend_slot = -1; chain_on = 0;
        memset(eff, 0, sizeof eff); eff[3] = 127; eff[2] = 128;
        update_eff(0); running = 0; cstep = 7;
        rec_state = 1;
        for (n = 0; n < 12u; n++) { eff[1] = 0; do_step(); }
        CHECK(rec_state == 0 && cur.mode == MODE_SEQ, "REC: after one measure the capture is in and the mode is SEQ");
        CHECK(cur.sc[0].fader[2] == 0 && cur.sc[1].fader[2] == 0 && cur.sc[0].fader[0] == 255 && cur.sc[1].fader[5] == 255, "REC: a step that did not sound is off, the ones that did are on");
        CHECK(cur.sc[0].seq[0] == 48u + cur.root + SCALE[cur.scale][0] && cur.sc[1].seq[4] == 48u + cur.root + SCALE[cur.scale][4], "REC: the notes that sounded are the pattern, in both scenes");
        CHECK(rec_done == 1, "REC: it says it is done");
        rec_restore();
        CHECK(cur.mode == MODE_ARP && cur.sc[0].fader[2] == 0 && cur.sc[0].fader[0] == 255, "REC: a long press puts the pattern back (the levels are the ones before)");
        rec_done = 0;
    }
    {                                                                           /* with motion */
        uint32_t n;
        preset_init(&cur); for (i = 0; i < 8u; i++) cur.sc[0].fader[i] = cur.sc[1].fader[i] = 255;
        fxs.recm = 1; mot_on = 0; cur.mode = MODE_ARP; rec_state = 1; cstep = 7; cur.density = 200;
        for (n = 0; n < 12u; n++) { eff[2] = (uint8_t)(100 + n * 10); eff[1] = 0; do_step(); }
        CHECK(mot_on == 1 && rec_mot[3][6] == 200, "REC +MOTION: the parameters at each step are kept");
        CHECK(rec_mot[1][2] != rec_mot[5][2], "REC +MOTION: they differ from step to step as they moved");
        { uint8_t was = effv(2, 5); eff[2] = 7; CHECK(effv(2, 5) == was && was == rec_mot[5][2], "REC +MOTION: SEQ plays the recorded motion, not the live value"); }
        recall_apply(-1); mot_on = 1; bank.p[0].used = 1; recall_apply(0); CHECK(mot_on == 0, "REC +MOTION: a recalled preset brings no old motion");
        fxs.recm = 0; rec_done = 0;
    }

    /* ---- CHAIN */
    {
        uint32_t n, seen[4] = {0, 0, 0, 0};
        preset_init(&cur);
        memset(&bank, 0, sizeof bank); bank_ok = 1;
        for (i = 0; i < 4u; i++) { bank.p[i] = cur; bank.p[i].used = 1; bank.p[i].density = (uint8_t)(31 + i); }
        memset(chain, 0, sizeof chain);
        chain[0] = 0x80u | (0u << 4) | 0u;                                       /* slot 1, 1 measure */
        chain[1] = 0x80u | (1u << 4) | 2u;                                       /* slot 3, 2 measures */
        chain[2] = 0x80u | (0u << 4) | 1u;                                       /* slot 2, 1 measure */
        cur.mode = MODE_ARP; mute_mask = 0xFFu; pend_slot = -1; rec_state = 0; cstep = 7; chain_on = 1; chain_pos = 255; chain_left = 0;
        for (n = 0; n < 8u * 4u; n++) {
            do_step();
            if (cstep == 0u) {                                                   /* a measure starts here: which preset is it? */
                uint32_t d = cur.density;
                if (d >= 31 && d <= 34) seen[d - 31]++;
            }
        }
        CHECK(seen[0] >= 1 && seen[2] == 2 && seen[1] >= 1, "CHAIN: slot 1 for a measure, slot 3 for two, then slot 2");
        CHECK(chain_on, "CHAIN: it goes round again");
        chain_on = 0; mute_mask = 0;
        memset(chain, 0, sizeof chain); chain_on = 1; chain_pos = 255; chain_left = 0; mute_mask = 0xFF; for (n = 0; n < 9u; n++) do_step();
        CHECK(chain_on == 0, "CHAIN: an empty chain switches itself off");
        mute_mask = 0;
    }

    /* ---- the panic */
    {
        preset_init(&cur); running = 0; transport(1); held_n = 3; latch = 1;
        fm1_ms = 50000;
        do_panic();
        CHECK(!running && held_n == 0 && !latch && (int32_t)(kill_until - fm1_ms) > 0, "PANIC: stopped, the keys let go, the tail is shut");
        weights();
        CHECK(rv_s[0] == 0 && dly_fb_v[0] == 0, "PANIC: no reverb, no echoes while it lasts");
        fm1_ms = 53000; weights();
        CHECK(rv_s[0] == cur.sc[0].rsend, "PANIC: and then everything is back");
    }

    /* ---- the pages */
    {
        int32_t h, n = 0, seen_wave = 0, wraps = 0;
        CHECK(pages_in(G_HOME) == 1 && pages_in(G_FX) == 4 && pages_in(G_ENV) == 1 && pages_in(G_LFO) == 6 && pages_in(G_GLO) == 3, "pages: the number in each group");
        h = group_first(G_LFO);
        for (i = 0; i < 20u; i++) { h = half_step(h, 1); if (HALF[h].flags & 1u) seen_wave = 1; if (h == group_first(G_LFO) + 1) wraps++; }
        CHECK(!seen_wave && wraps >= 3, "pages: SELECT skips the wave on top and goes round the LFO pages");
        h = group_first(G_FX); h = half_step(h, -1);
        CHECK(h == group_last(G_FX), "pages: SELECT back from the first half comes to the last");
        for (h = 0; h < NHALF; h++) {
            uint32_t k;
            for (k = 0; k < 4u; k++) {
                cell_t c = HALF[h].c[k];
                char name[12], val[16];
                int32_t bar;
                uint32_t scn;
                if (c.kind == K_NONE || c.kind == K_WAVE) continue;
                cell_text(c, name, val, &bar, &scn);
                if (!name[0] || !val[0] || !help_text(c)[0]) n++;
            }
        }
        CHECK(n == 0, "pages: every cell has a name, a value and a beginner text");
        n = 0;
        for (h = 0; h < NHALF; h++) {
            uint32_t k;
            for (k = 0; k < 4u; k++) {
                cell_t c = HALF[h].c[k];
                char name[12], val[16];
                int32_t bar;
                uint32_t scn;
                if (c.kind == K_NONE || c.kind == K_WAVE || c.kind == K_PANA || c.kind == K_PANB || c.kind == K_P || (c.kind >= K_LSHP && c.kind <= K_LPHS) || half_voice(h) == 2u) continue;
                cell_text(c, name, val, &bar, &scn);
                if (scn != (c.arg & 1u) + 1u) n++;
            }
        }
        CHECK(n == 0, "pages: the cells of a voice are in that voice's colour");
        cur_half = group_first(G_FX); edit_sc = 1; cur_half = -1; goto_group(G_FX);
        CHECK(HALF[cur_half].c[0].kind == K_INST && (HALF[cur_half].c[0].arg & 1) == 1, "pages: FX opens on the voice being edited");
        goto_group(G_FX);
        CHECK(cur_half == -1, "pages: the same button again goes home");
        edit_sc = 0;
        CHECK(half_voice(group_first(G_FX)) == 0 && half_voice(group_last(G_FX)) == 2, "pages: the punch-ins act on the voice of the page (both on MIX)");
    }

    /* ---- the settings record round trip */
    {
        preset_init(&cur); cur.sc[1].env[2] = 33; cur.lfo_sp[4] = 0x2B; cur.sc[0].dtime = 3;
        chain[3] = 0x93; fxs.hint = 1;
        host_store[0] = 0;
        memset(&sv, 0, sizeof sv);
        sv.cur = cur; sv.ver = 1; fx_save(sv.fx); memcpy(sv.chain, chain, CHAIN_N);
        settings_save(&sv, sizeof sv);
        cur.sc[1].env[2] = 0; cur.lfo_sp[4] = 0; chain[3] = 0; fxs.hint = 0;
        app_init();
        CHECK(cur.sc[1].env[2] == 33 && cur.lfo_sp[4] == 0x2B && cur.sc[0].dtime == 3 && chain[3] == 0x93 && fxs.hint == 1, "the v1.7 settings record goes round (ENV, LFO shape, delay, chain, help)");
        host_store[0] = 0;
    }
    return fails ? 1 : (printf("ALL OK\n"), 0);
}
