/* SPDX-License-Identifier: GPL-3.0-only */
/* Formation for the M-VAVE FM-1: a port of Navy Arp 2 (Grandsummoner), the dual-scene probabilistic arpeggiator /
 * step sequencer, to the FM-1 with its own sound engines, on Dinghy / Felucca's platform (platform.c).
 *
 * Integers only (the chip has no FPU and no libgcc): see docs/RULES.md.
 * Layout of this file:  1 tables and helpers   2 the sound (audio ISR)   3 MIDI and notes   4 the engine
 *                       5 storage   6 the screen   7 the controls and main loop
 * The control map is in docs/MANUAL.md. */
#define APP_NAME "FORMATION"
#include "platform.c"

/* ================================================================== 1. tables and helpers */
static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (a refined parabola) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static int32_t cosine(uint32_t ph) { return sine(ph + 0x40000000u); }

static uint32_t rng_s = 0x2F6E2B1u;
static uint32_t rnd32(void) { uint32_t x = rng_s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng_s = x; return x; }
static uint32_t rnd8(void) { return rnd32() >> 24; }

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static const uint8_t SCALE[10][8] = {           /* from Navy Arp 2: eight offsets, the last an octave (or more) up */
    {0, 2, 4, 5, 7, 9, 11, 12}, {0, 2, 3, 5, 7, 8, 10, 12}, {0, 3, 5, 7, 10, 12, 15, 17}, {0, 2, 4, 7, 9, 12, 14, 16},
    {0, 2, 3, 5, 7, 9, 10, 12}, {0, 1, 3, 5, 7, 8, 10, 12}, {0, 2, 4, 6, 7, 9, 11, 12}, {0, 2, 4, 5, 7, 9, 10, 12},
    {0, 2, 3, 5, 7, 8, 11, 12}, {0, 2, 3, 5, 7, 9, 11, 12}};
static const char *const SCALE_NAME[10] = {"MAJOR", "MINOR", "PENT MIN", "PENT MAJ", "DORIAN", "PHRYGIAN", "LYDIAN",
                                           "MIXOLYD", "HARM MIN", "MEL MIN"};
static const char *const ROOT_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const INST_NAME[5] = {"MIDI ONLY", "ANALOG", "FM", "SUPERSAW", "PULSE"};
static const char *const DIV_NAME[4] = {"1/4", "1/8", "1/16", "1/32"};
static const char *const LFO_RATE[5] = {"OFF", "1/4", "1/8", "1/16", "1/32"};
static const char *const PNAME[6] = {"MORPH", "REST", "LEGATO", "ENTROPY", "HARMONY", "CHAOS"};

static const uint32_t INC_C9[12] = {            /* MIDI 120..131 (C9..B9); lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};

/* ---- a scene, a preset (the whole state), what is kept */
typedef struct {
    uint8_t fader[8];                           /* step probabilities, 0..255 */
    uint8_t p[6];                               /* MORPH REST LEGATO ENTROPY(127 = 0) HARMONY CHAOS, 0..255 */
    uint8_t inst, timbre, dsend, level;         /* the scene's voice */
    uint8_t seq[8];                             /* SEQ mode: the notes of the pattern */
} scene_t;                                      /* 26 B */
typedef struct {
    scene_t sc[2];
    uint8_t lfo_rate[6], lfo_depth[6];
    uint8_t morph_pos, density, swing, bpm, div, scale, root, poly, mode;
    int8_t oct, tr;
    uint8_t used, pad[4];
} preset_t;                                     /* 80 B */
enum { MODE_ARP = 0, MODE_SEQ = 1 };
enum { INST_MIDI = 0, INST_ANALOG, INST_FM, INST_SAW, INST_PULSE };

static preset_t cur;                            /* the state you play */
static struct { uint8_t ver, theme, ch[2], slot; } glob = {1, 0, {0, 1}, 0};

static void scene_init(scene_t *s, int b)
{
    static const uint8_t FB[8] = {255, 160, 200, 120, 255, 160, 200, 120};
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        s->fader[i] = b ? FB[i] : 255;
        s->seq[i] = (uint8_t)(48u + SCALE[1][i]);
    }
    s->p[0] = 0; s->p[1] = 26; s->p[2] = b ? 220 : 128; s->p[3] = 127; s->p[4] = b ? 160 : 0; s->p[5] = 0;
    s->inst = b ? INST_SAW : INST_ANALOG;
    s->timbre = b ? 70 : 128;
    s->dsend = 60;
    s->level = b ? 150 : 170;
}
static void preset_init(preset_t *p)
{
    memset(p, 0, sizeof *p);
    scene_init(&p->sc[0], 0);
    scene_init(&p->sc[1], 1);
    p->density = 128; p->bpm = 80; p->div = 2; p->scale = 1; p->root = 0; p->mode = MODE_ARP;
}

/* ================================================================== 2. the sound (audio ISR) */
/* Two scene voices of four notes each. A slot is written by the main loop (inc, inst, tim, lvl, then gate and serial
 * last) and rendered, with its envelope, by the ISR. Instruments are small: analog saw, 2-op FM, 3-saw, pulse,
 * each through a one-pole low-pass (the timbre). A mono delay (dotted eighth) is fed by each scene's send. */
#define NSL 8u
typedef struct {
    volatile uint32_t inc, serial;
    volatile int32_t lvl;
    volatile uint8_t gate, inst, tim;
    uint32_t seen, ph, ph2, ph3;                /* ISR only from here */
    int32_t amp, lp, rel;
    uint8_t st;                                 /* 0 idle, 1 attack, 2 decay / sustain, 3 release */
} slot_t;
static slot_t sl[NSL];
static volatile int32_t gain_a, gain_b, ds_a, ds_b, dly_len;
static volatile uint8_t busy;
static volatile uint32_t dsp_max;               /* the longest app_audio block since the last look, in 24 MHz ticks */
static int32_t bufa[HALF_FRAMES], bufb[HALF_FRAMES];
static int16_t dly[65536] __attribute__((section(".pool")));
static uint32_t dly_w;

static void render_slot(slot_t *s, int32_t *buf, uint32_t n)
{
    uint32_t i, inc = s->inc, ph = s->ph, ph2 = s->ph2, ph3 = s->ph3, width;
    int32_t amp = s->amp, lp = s->lp, lvlq = s->lvl << 16, susq, atk, dec, k, ix;
    uint8_t st = s->st, inst = s->inst;
    if (s->serial != s->seen) {                 /* a new note: attack from where the level is */
        s->seen = s->serial;
        st = 1;
        s->rel = 0;
    }
    if (!s->gate && (st == 1 || st == 2)) {     /* released */
        st = 3;
        s->rel = amp / 6615 + 1;
    }
    if (!st) return;
    susq = (lvlq / 10) * 7;
    atk = lvlq / 240 + 1;
    dec = (lvlq - susq) / 13230 + 1;
    k = 1500 + (int32_t)s->tim * 105;
    ix = ((int32_t)s->tim + 1) * 256;
    width = 644245094u + (uint32_t)s->tim * 11790106u;
    for (i = 0; i < n; i++) {
        int32_t w;
        if (st == 1) { amp += atk; if (amp >= lvlq) { amp = lvlq; st = 2; } }
        else if (st == 2) { if (amp > susq) { amp -= dec; if (amp < susq) amp = susq; } }
        else { amp -= s->rel; if (amp <= 0) { amp = 0; st = 0; break; } }
        ph += inc;
        switch (inst) {
        case INST_FM:
            ph2 += (inc >> 1) * 7u;
            w = sine(ph + (uint32_t)(sine(ph2) * ix)) >> 1;
            break;
        case INST_SAW:
            ph2 += inc + (inc >> 8);
            ph3 += inc - (inc >> 8);
            w = (int32_t)(((ph >> 17) + (ph2 >> 17) + (ph3 >> 17)) / 3u) - 16384;
            break;
        case INST_PULSE:
            w = ph < width ? 12000 : -12000;
            break;
        default:
            w = (int32_t)(ph >> 17) - 16384;
            break;
        }
        lp += ((w - lp) * k) >> 15;
        buf[i] += (lp * (amp >> 16)) >> 15;
    }
    s->ph = ph; s->ph2 = ph2; s->ph3 = ph3; s->amp = amp; s->lp = lp; s->st = st;
}

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t v, i, any = 0, t0 = fm1_ticks();
    int32_t ga = gain_a, gb = gain_b, da = ds_a, db = ds_b, len = dly_len;
    memset(bufa, 0, sizeof bufa);
    memset(bufb, 0, sizeof bufb);
    for (v = 0; v < NSL; v++) {
        render_slot(&sl[v], v < 4u ? bufa : bufb, n);
        any |= sl[v].st;
    }
    busy = (uint8_t)(any != 0u);
    for (i = 0; i < n; i++) {
        int32_t a = (bufa[i] * ga) >> 7, b = (bufb[i] * gb) >> 7, d, in, s;
        d = dly[(dly_w - (uint32_t)len) & 0xFFFFu];
        in = ((a * da + b * db) >> 8) + ((d * 100) >> 8);
        dly[dly_w & 0xFFFFu] = (int16_t)clampi(in, -32767, 32767);
        dly_w++;
        s = a + b + (d >> 1);
        out[2u * i] += s;
        out[2u * i + 1u] += s;
    }
    t0 = fm1_ticks() - t0;
    if (t0 > dsp_max) dsp_max = t0;
}

/* ================================================================== 3. MIDI and notes (main loop) */
static uint32_t slot_note[NSL], rr[2];
static uint32_t wa, wb;                         /* the scene weights now: 100 * cos / sin of the morph */

static void weights(void)
{
    uint32_t ph = (uint32_t)cur.morph_pos * 4210752u;
    wb = (uint32_t)((sine(ph) * 100) >> 15);
    wa = (uint32_t)((cosine(ph) * 100) >> 15);
    if (wa > 100u) wa = 100u;
    if (wb > 100u) wb = 100u;
    gain_a = (int32_t)((wa * ((uint32_t)cur.sc[0].level + 1u)) >> 8);
    gain_b = (int32_t)((wb * ((uint32_t)cur.sc[1].level + 1u)) >> 8);
    ds_a = cur.sc[0].dsend;
    ds_b = cur.sc[1].dsend;
}

static void voice_on(uint32_t sc, uint32_t note)
{
    uint32_t idx = sc * 4u + (rr[sc]++ & 3u);
    slot_t *s = &sl[idx];
    if (cur.sc[sc].inst == INST_MIDI || note > 127u) return;
    s->inc = INC_C9[note % 12u] >> (10u - note / 12u);
    s->inst = cur.sc[sc].inst;
    s->tim = cur.sc[sc].timbre;
    s->lvl = 8000;
    slot_note[idx] = note;
    s->gate = 1;
    s->serial++;
}
static void voice_off(uint32_t sc, uint32_t note)
{
    uint32_t i;
    for (i = sc * 4u; i < sc * 4u + 4u; i++)
        if (slot_note[i] == note && sl[i].gate)
            sl[i].gate = 0;
}
static void voices_off(void)
{
    uint32_t i;
    for (i = 0; i < NSL; i++) sl[i].gate = 0;
}

typedef struct { uint32_t due; uint8_t note, sc, on; } poff_t;
static poff_t poff[32];

static void send_off(uint32_t sc, uint32_t note)
{
    midi_send(0x80u | glob.ch[sc], note, 0);
    voice_off(sc, note);
}
static void note_on(uint32_t sc, uint32_t note, uint32_t gate_ticks)
{
    uint32_t w = sc ? wb : wa, i;
    if (!w) return;
    for (i = 0; i < 32u; i++)                   /* the same note still sounding: stop it first */
        if (poff[i].on && poff[i].note == note && poff[i].sc == sc) { send_off(sc, note); poff[i].on = 0; }
    midi_send(0x90u | glob.ch[sc], note, w);
    voice_on(sc, note);
    if (gate_ticks)
        for (i = 0; i < 32u; i++)
            if (!poff[i].on) {
                poff[i].on = 1; poff[i].note = (uint8_t)note; poff[i].sc = (uint8_t)sc;
                poff[i].due = fm1_ticks() + gate_ticks;
                break;
            }
}
static void offs_run(uint32_t now, int force)
{
    uint32_t i;
    for (i = 0; i < 32u; i++)
        if (poff[i].on && (force || (int32_t)(now - poff[i].due) >= 0)) {
            poff[i].on = 0;
            send_off(poff[i].sc, poff[i].note);
        }
}

/* ================================================================== 4. the engine */
static void recall_apply(int32_t slot);
static void roll(void);
static uint8_t auto_roll;
static uint8_t running, frozen, latch, first_of_chord = 1, fwd = 1, cstep = 7;
static uint8_t held[12], held_n, latchn[12], latch_len, frz_pool[12], frz_n;
static uint8_t frz_p[8], frz_e[6];              /* what Freeze locks: the step levels and the six parameters */
static uint8_t mute_mask;
static int32_t pend_slot = -1;
static uint8_t eff[6];                          /* the six parameters now: morphed between the scenes, plus the LFOs */
static uint32_t lfo_ph[6], next_t, last_t, step_hit;
static uint8_t trail[8];
static uint32_t lfo_rem;
static uint32_t hit_ticks;                      /* when a note last sounded: the screen flashes on it */

static uint32_t step_ticks(void)                /* one step, in 24 MHz ticks (60e6 us / (bpm * 2^div) * 24) */
{
    uint32_t bpm = cur.bpm < 40u ? 40u : cur.bpm > 240u ? 240u : cur.bpm;
    return (60000000u / (bpm << cur.div)) * 24u;
}

static void update_eff(uint32_t dt)             /* dt: ticks since the last call */
{
    uint32_t p, mp = cur.morph_pos, beat_us = 60000000u / (cur.bpm < 40u ? 40u : cur.bpm), us;
    dt += lfo_rem;
    us = dt / 24u;
    lfo_rem = dt - us * 24u;
    for (p = 0; p < 6u; p++) {
        int32_t base = (int32_t)(((uint32_t)cur.sc[0].p[p] * (255u - mp) + (uint32_t)cur.sc[1].p[p] * mp + 127u) / 255u);
        uint32_t r = cur.lfo_rate[p];
        if (r && cur.lfo_depth[p]) {
            uint32_t period = beat_us >> (r - 1u);          /* us per cycle: 1, 1/2, 1/4, 1/8 beat */
            lfo_ph[p] += (4294967295u / (period ? period : 1u)) * us;
            base += (((sine(lfo_ph[p]) * (int32_t)cur.lfo_depth[p]) >> 15) * 127) >> 8;
        }
        eff[p] = (uint8_t)clampi(base, p == 2u ? 26 : 0, 255);
    }
}

static void pool_add(uint8_t *l, uint8_t *n, uint32_t note)
{
    uint32_t i, j;
    for (i = 0; i < *n; i++) if (l[i] == note) return;
    if (*n >= 12u) return;
    for (i = 0; i < *n && l[i] < note; i++) ;
    for (j = *n; j > i; j--) l[j] = l[j - 1u];
    l[i] = (uint8_t)note;
    (*n)++;
}
static void pool_del(uint8_t *l, uint8_t *n, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < *n; i++)
        if (l[i] == note) {
            for (; i + 1u < *n; i++) l[i] = l[i + 1u];
            (*n)--;
            return;
        }
}

static void do_step(void)
{
    int32_t e = (int32_t)(frozen ? frz_e[3] : eff[3]) - 127;
    uint32_t ls = cstep, p, mp = cur.morph_pos, rest, legato, harm, chaos, gate_t, raw, ob, k, n, i;
    const uint8_t *pool;
    uint8_t pn, pl[4];
    if (e >= -13 && e <= 13) ls = (ls + 1u) & 7u;                                   /* entropy: the play direction */
    else if (e > 13 && e <= 64) { if (fwd) { ls++; if (ls >= 7u) { ls = 7u; fwd = 0; } } else { ls--; if (!ls || ls > 7u) { ls = 0; fwd = 1; } } }
    else if (e > 64) { uint32_t r = rnd8(); if (r < 179u) ls = (ls + 1u) & 7u; else if (r < 230u) ls = (ls + 7u) & 7u; }
    else if (e < -13 && e >= -64) ls = (ls + 7u) & 7u;
    else ls = rnd8() < 51u ? (ls + 2u) & 7u : (ls + 1u) & 7u;
    cstep = (uint8_t)ls;
    if (ls == 0u && auto_roll && !frozen) roll();                                   /* auto re-roll: each measure */
    if (ls == 0u && pend_slot >= 0) {                                               /* a recall waits for the end of the measure */
        recall_apply(pend_slot);
        pend_slot = -1;
    }
    /* the step's probability: the scenes' faders, the Morph smear, then the density */
    if (frozen) p = frz_p[ls];
    else {
        uint32_t a = ((uint32_t)cur.sc[0].fader[ls] * (255u - mp) + (uint32_t)cur.sc[1].fader[ls] * mp + 127u) / 255u;
        uint32_t b = ((uint32_t)cur.sc[0].fader[(ls + 1u) & 7u] * (255u - mp) + (uint32_t)cur.sc[1].fader[(ls + 1u) & 7u] * mp + 127u) / 255u;
        uint32_t m = eff[0];
        p = (a * (255u - m) + b * m + 127u) / 255u;
    }
    if (cur.density < 128u) p = p * cur.density / 128u;
    else if (cur.density > 128u) p = p + ((255u - p) * (cur.density - 128u)) / 127u;
    if (p > 255u) p = 255u;
    rest = frozen ? frz_e[1] : eff[1];
    legato = frozen ? frz_e[2] : eff[2];
    harm = frozen ? frz_e[4] : eff[4];
    chaos = frozen ? frz_e[5] : eff[5];
    if (legato >= 204u) rest = rest * (255u - legato) / 51u;
    gate_t = (step_ticks() / 100u) * (10u + legato * 85u / 255u);
    if ((mute_mask >> ls) & 1u) return;
    if (!(rnd8() < p || p == 255u) || rnd8() < rest) return;
    /* the pitch */
    pool = frozen ? frz_pool : latch ? latchn : held;
    pn = frozen ? frz_n : latch ? latch_len : held_n;
    if (cur.mode == MODE_SEQ) raw = cur.sc[mp >= 128u].seq[ls];
    else if (pn) raw = pool[ls % pn];
    else raw = 48u + cur.root + SCALE[cur.scale][ls];
    ob = raw >= cur.root ? ((raw - cur.root) / 12u) * 12u + cur.root : cur.root;
    n = 1;
    pl[0] = (uint8_t)raw;
    if (cur.poly) {
        n = harm > 63u ? (harm < 128u ? 2u : harm < 192u ? 3u : 4u) : 1u;
        for (k = 1; k < n; k++) pl[k] = (uint8_t)(ob + SCALE[cur.scale][(ls + k * 2u) & 7u]);
    }
    for (k = 0; k < n; k++) {
        int32_t pitch = (int32_t)pl[k] + cur.oct * 12 + cur.tr;
        if (chaos > 51u && rnd8() < chaos) pitch += (rnd32() & 0x100u) ? 12 : -12;
        pitch = clampi(pitch, 0, 127);
        for (i = 0; i < 2u; i++) note_on(i, (uint32_t)pitch, gate_t);
    }
    hit_ticks = fm1_ticks();
    step_hit = ls;
    trail[ls] = 255;
}

static void transport(int on)
{
    if (on && !running) {
        running = 1;
        cstep = 7; fwd = 1;
        next_t = fm1_ticks();
        last_t = next_t;
    } else if (!on && running) {
        running = 0;
        offs_run(0, 1);
        voices_off();
    }
}

static void freeze_set(int on)
{
    uint32_t i, mp = cur.morph_pos;
    if (on && !frozen) {
        const uint8_t *pool = latch ? latchn : held;
        frz_n = latch ? latch_len : held_n;
        for (i = 0; i < 12u; i++) frz_pool[i] = pool[i];
        for (i = 0; i < 6u; i++) frz_e[i] = eff[i];
        for (i = 0; i < 8u; i++)
            frz_p[i] = (uint8_t)(((uint32_t)cur.sc[0].fader[i] * (255u - mp) + (uint32_t)cur.sc[1].fader[i] * mp + 127u) / 255u);
        frozen = 1;
    } else if (!on) frozen = 0;
}

static void key_note(uint32_t note, int down)   /* a key (or MIDI in) goes down or up */
{
    uint32_t i;
    if (down) {
        if (frozen) { if (!running) for (i = 0; i < 2u; i++) note_on(i, note, 0); return; }
        pool_add(held, &held_n, note);
        if (latch) {
            if (first_of_chord) { latch_len = 0; first_of_chord = 0; }
            pool_add(latchn, &latch_len, note);
        }
        if (!running) for (i = 0; i < 2u; i++) note_on(i, note, 0);
    } else {
        pool_del(held, &held_n, note);
        if (!held_n) first_of_chord = 1;
        if (!running) for (i = 0; i < 2u; i++) send_off(i, note);
    }
}

/* ================================================================== 5. storage */
#define PRE_MAGIC 0x32564E50u                   /* "PNV2" */
typedef struct { uint32_t magic, crc; preset_t p[16]; } prebank_t;
static prebank_t bank;
static uint8_t bank_ok;

static int bank_load(void)
{
#ifdef DINGHY_HOST
    if (!host_pre_ok) return -1;
    memcpy(&bank, host_pre, sizeof bank);
    return bank.magic == PRE_MAGIC ? 0 : -1;
#else
    if (!flash_ok || st_read(PRESETS_AT, &bank, sizeof bank) || bank.magic != PRE_MAGIC ||
        crc32((const uint8_t *)bank.p, sizeof bank.p) != bank.crc)
        return -1;
    return 0;
#endif
}
static int bank_save(void)
{
    bank.magic = PRE_MAGIC;
#ifdef DINGHY_HOST
    memcpy(host_pre, &bank, sizeof bank);
    host_pre_ok = 1;
    printf("%6u ms  presets saved\n", fm1_ms);
    return 0;
#else
    uint32_t took, hdr[2], i;
    int rc;
    if (!flash_ok) return -1;
    bank.crc = crc32((const uint8_t *)bank.p, sizeof bank.p);
    hdr[0] = bank.magic;
    hdr[1] = bank.crc;
    if ((rc = fl_erase4k(PRESETS_AT, &took)) != 0) return rc;
    for (i = 0; i < sizeof bank.p; i += 256u) {
        uint32_t k = sizeof bank.p - i < 256u ? (uint32_t)sizeof bank.p - i : 256u;
        if ((rc = fl_write(PRESETS_AT + 8u + i, (const uint8_t *)bank.p + i, k)) != 0) return rc;
    }
    return fl_write(PRESETS_AT, (const uint8_t *)hdr, sizeof hdr);   /* the header last: a cut save stays invalid */
#endif
}
static void bank_open(void)
{
    uint32_t i;
    if (bank_ok) return;
    if (bank_load()) {
        memset(&bank, 0, sizeof bank);
        for (i = 0; i < 16u; i++) bank.p[i].used = 0;
    }
    bank_ok = 1;
}
static void recall_apply(int32_t slot)          /* the engine calls it at the end of a measure */
{
    if (slot < 0 || slot > 15 || !bank.p[slot].used) return;
    cur = bank.p[slot];
    glob.slot = (uint8_t)slot;
}

/* ================================================================== 6. the screen */
typedef struct { const char *name; uint16_t c[5]; uint16_t lfo; } theme_t;
static const theme_t THEME[3] = {
    {"NAVY", {RGB(0, 36, 46), RGB(0, 84, 100), RGB(16, 140, 150), RGB(56, 200, 205), RGB(130, 255, 240)}, RGB(255, 170, 50)},
    {"MONOCHROME", {RGB(40, 40, 44), RGB(84, 84, 90), RGB(136, 136, 144), RGB(190, 190, 198), RGB(236, 236, 244)}, RGB(0, 220, 255)},
    {"MATRIX", {RGB(0, 36, 8), RGB(0, 90, 22), RGB(10, 150, 40), RGB(50, 214, 80), RGB(150, 255, 160)}, RGB(255, 0, 170)}};
#define C_A pal[3]
#define C_B (THEME[glob.theme % 3u].lfo)

static void theme_set(uint32_t t)
{
    uint32_t k;
    glob.theme = (uint8_t)(t % 3u);
    for (k = 0; k < 5u; k++) pal[k] = THEME[glob.theme].c[k];
}

static char *p_str(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *p_num(char *p, uint32_t v)
{
    char t[11];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    return p;
}
static char *p_sig(char *p, int32_t v) { if (v < 0) { *p++ = '-'; v = -v; } else *p++ = '+'; return p_num(p, (uint32_t)v); }
static char *p_pct(char *p, uint32_t v255) { p = p_num(p, (v255 * 100u + 127u) / 255u); *p++ = '%'; return p; }
static char *p_note(char *p, uint32_t n)
{
    p = p_str(p, ROOT_NAME[n % 12u]);
    return p_num(p, n / 12u - 1u);
}

/* a message that takes over the screen while a value is touched: its name, the value big, a bar */
static char hud_name[16], hud_val[16];
static uint32_t hud_until, ov_btn;
static int32_t hud_bar = -1;
static uint8_t hud_scn;                         /* 0 none, 1 scene A, 2 scene B: shown above the name */
static void hud(const char *name, const char *val, int32_t bar)
{
    char *p = p_str(hud_name, name); *p = 0;
    p = p_str(hud_val, val); *p = 0;
    hud_bar = bar;
    hud_scn = 0;
    hud_until = fm1_ms + 800u;
}
static void hud_btn(const char *name, const char *val, uint32_t bit)    /* stays while that button is held */
{
    hud(name, val, -1);
    ov_btn = 1u << bit;
}
static void hud_pct(const char *name, uint32_t v255)
{
    char t[8], *p = p_pct(t, v255);
    *p = 0;
    hud(name, t, (int32_t)v255);
}
static void hud_num(const char *name, int32_t v, int sign)
{
    char t[12], *p = sign ? p_sig(t, v) : p_num(t, (uint32_t)v);
    *p = 0;
    hud(name, t, -1);
}
static void hud_note(const char *name, uint32_t n)
{
    char t[8], *p = p_note(t, n);
    *p = 0;
    hud(name, t, -1);
}

/* ---- the globe: a wireframe sphere, rotating, breathing with the LFOs */
#define GR 9u                                   /* rings: 3 parallels, 6 meridians */
#define GP 16u                                  /* points a ring */
static int16_t gx[GR][GP], gy[GR][GP], gz[GR][GP];
static uint32_t gyaw;

static void globe_init(void)
{
    static const uint32_t LAT[3] = {0xE0000000u, 0u, 0x20000000u};
    uint32_t r, i;
    for (r = 0; r < 3u; r++)
        for (i = 0; i < GP; i++) {
            uint32_t th = i << 28;
            gx[r][i] = (int16_t)((cosine(LAT[r]) * cosine(th)) >> 15);
            gz[r][i] = (int16_t)((cosine(LAT[r]) * sine(th)) >> 15);
            gy[r][i] = (int16_t)sine(LAT[r]);
        }
    for (r = 3; r < GR; r++) {
        uint32_t lam = (r - 3u) * 357913941u;
        for (i = 0; i < GP; i++) {
            uint32_t th = i << 28;
            gx[r][i] = (int16_t)((sine(th) * cosine(lam)) >> 15);
            gz[r][i] = (int16_t)((sine(th) * sine(lam)) >> 15);
            gy[r][i] = (int16_t)cosine(th);
        }
    }
}

static void draw_globe(int32_t cx, int32_t cy)
{
    uint32_t r, i, lfo_on = 0, dsum = 0;
    int32_t radius = 27, cy_ = (int32_t)cosine(gyaw), sy_ = (int32_t)sine(gyaw), ct = 30791, st = 11207;
    for (r = 0; r < 6u; r++) if (cur.lfo_rate[r] && cur.lfo_depth[r]) { lfo_on = 1; dsum += cur.lfo_depth[r]; }
    if (lfo_on) radius += ((sine(lfo_ph[0] ? lfo_ph[0] : lfo_ph[3]) >> 12) * (int32_t)(dsum > 255u ? 255u : dsum)) >> 11;
    for (r = 0; r < GR; r++) {
        int32_t px = 0, py = 0;
        for (i = 0; i <= GP; i++) {
            uint32_t k = i & (GP - 1u);
            int32_t x = gx[r][k], y = gy[r][k], z = gz[r][k], x1, z1, y2, z2, sx, sy;
            x1 = (x * cy_ + z * sy_) >> 15;
            z1 = (-x * sy_ + z * cy_) >> 15;
            y2 = (y * ct - z1 * st) >> 15;
            z2 = (y * st + z1 * ct) >> 15;
            sx = cx + ((x1 * radius) >> 15);
            sy = cy - ((y2 * radius) >> 15);
            if (i) cv_line(px, py, sx, sy, z2 > 9000 ? pal[4] : z2 > -9000 ? pal[2] : pal[1]);
            px = sx; py = sy;
        }
    }
}

/* ---- the PERF screen: header, globe + HUD, probability towers, morph bar, footer */
static uint8_t edit_sc;                         /* the scene the knobs write to: 0 A, 1 B */
static uint32_t hold_step_mask;                 /* the step keys held now */

static void tower_values(uint8_t *out)
{
    uint32_t i, mp = cur.morph_pos;
    for (i = 0; i < 8u; i++) {
        uint32_t a = ((uint32_t)cur.sc[0].fader[i] * (255u - mp) + (uint32_t)cur.sc[1].fader[i] * mp + 127u) / 255u;
        out[i] = (uint8_t)a;
    }
}

static void draw_towers(void)
{
    uint8_t pv[8];
    uint32_t i;
    tower_values(pv);
    cv_begin(240, 64, C_BLACK);
    for (i = 0; i < 8u; i++) {
        int32_t x = 8 + (int32_t)i * 28, h = 2 + (int32_t)pv[i] * 54 / 255, th = 2 + (int32_t)trail[i] * 54 / 255;
        uint16_t col = (mute_mask >> i) & 1u ? pal[1] : (running && cstep == i) ? pal[4] : C_A;
        if (th > h) cv_rect(x, 60 - th, 24, th - h, pal[0]);                  /* the phosphor trail above */
        cv_rect(x, 60 - h, 24, h, col);
        if (running && cstep == i) cv_rect(x, 61, 24, 2, pal[4]);
        else cv_rect(x, 61, 24, 1, pal[1]);
        if (trail[i] > 6u) trail[i] = (uint8_t)(trail[i] - 6u); else trail[i] = 0;
        if (trail[i] < pv[i] && !(running && cstep == i)) trail[i] = pv[i];
    }
    cv_blit(0, 94);
}

static void draw_head(void)
{
    char s[40], *p = s;
    p = p_str(p, edit_sc ? "B " : "A ");
    p = p_str(p, cur.mode == MODE_ARP ? "ARP " : "SEQ ");
    p = p_num(p, cur.bpm);
    p = p_str(p, " BPM");
    if (latch) p = p_str(p, " LATCH");
    if (cur.poly) p = p_str(p, " POLY");
    if (frozen) p = p_str(p, " FRZ");
    if (auto_roll) p = p_str(p, " RND");
    *p = 0;
    draw_text_box(8, 2, 224, &FONT_S, s, frozen ? C_B : pal[4], 0);
}

static void draw_globe_hud(void)
{
    char s[24], *p = s;
    cv_begin(240, 64, C_BLACK);
    draw_globe(34, 32);
    p = p_str(p, ROOT_NAME[cur.root % 12u]);
    p = p_str(p, " ");
    p = p_str(p, SCALE_NAME[cur.scale % 10u]);
    *p = 0;
    cv_text(80, 4, &FONT_S, s, pal[3]);
    p = s;
    p = p_str(p, "A ");
    p = p_str(p, INST_NAME[cur.sc[0].inst % 5u]);
    *p = 0;
    cv_text(80, 24, &FONT_S, s, C_A);
    p = s;
    p = p_str(p, "B ");
    p = p_str(p, INST_NAME[cur.sc[1].inst % 5u]);
    *p = 0;
    cv_text(80, 44, &FONT_S, s, C_B);
    cv_blit(0, 26);
}

static void draw_morph(void)
{
    uint32_t i, x = 8u + (uint32_t)cur.morph_pos * 200u / 255u;
    static const char LET[6] = {'M', 'R', 'L', 'E', 'H', 'C'};
    char t[2] = {0, 0};
    cv_begin(240, 32, C_BLACK);
    cv_rect(8, 8, 224, 2, pal[1]);
    cv_rect((int32_t)x, 2, 16, 14, pal[4]);
    cv_text(8, 16, &FONT_S, "A", edit_sc ? pal[1] : C_A);
    cv_text(222, 16, &FONT_S, "B", edit_sc ? C_B : pal[1]);
    for (i = 0; i < 6u; i++) {
        uint8_t on = cur.lfo_rate[i] && cur.lfo_depth[i];
        t[0] = LET[i];
        cv_text(70 + (int32_t)i * 16, 16, &FONT_S, t, on ? C_B : pal[1]);
    }
    cv_blit(0, 164);
}

static void draw_foot(void)
{
    char s[40], *p = s;
    p = p_str(p, "OCT ");
    p = p_sig(p, cur.oct);
    p = p_str(p, "  ");
    p = p_str(p, THEME[glob.theme % 3u].name);
    *p = 0;
    draw_text_box(8, 204, 224, &FONT_S, s, pal[1], 0);
    p = s;
    p = p_str(p, "DEN ");
    p = p_pct(p, cur.density);
    p = p_str(p, "  SWG ");
    p = p_pct(p, cur.swing);
    *p = 0;
    draw_text_box(8, 222, 224, &FONT_S, s, pal[2], 0);
}

/* ---- double-size text for the overlay: the large font, every pixel a 2x2 block */
static int32_t cv_text2(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    uint16_t ramp[16];
    uint32_t r = c >> 11, g = (c >> 5) & 63u, b = c & 31u, a;
    for (a = 0; a < 16u; a++)
        ramp[a] = (uint16_t)(((r * a / 15u) << 11) | ((g * a / 15u) << 5) | (b * a / 15u));
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w = f->bw[gi], bpr = (w + 1u) / 2u;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[gy * bpr + gx / 2u];
                v = (gx & 1u) ? (v & 15u) : (v >> 4);
                if (v) {
                    int32_t px = x - 2 * (int32_t)f->pad + 2 * (int32_t)gx, py = y + 2 * (int32_t)gy;
                    cv_pset(px, py, ramp[v]); cv_pset(px + 1, py, ramp[v]);
                    cv_pset(px, py + 1, ramp[v]); cv_pset(px + 1, py + 1, ramp[v]);
                }
            }
        x += 2 * (int32_t)f->adv[gi];
    }
    return x;
}

/* ---- the cells: what one knob controls on a page, how it reads, how it turns */
enum { K_NONE, K_DENS, K_SWING, K_P, K_AB, K_INST, K_TIMB, K_DLY, K_LVL, K_LSPD, K_LDEP, K_BPM, K_STEP, K_TRANS,
       K_THEME, K_ROOT, K_SCALE, K_CHA, K_CHB };
typedef struct { uint8_t kind, arg; } cell_t;

static const char *const PSHORT[6] = {"MORPH", "REST", "LEGATO", "ENTROP", "HARMNY", "CHAOS"};
static const char *const LSHORT[6] = {"MOR", "RES", "LEG", "ENT", "HAR", "CHA"};
static const char *const INST_SHORT[5] = {"MIDI", "ANALOG", "FM", "SUPSAW", "PULSE"};
static const char *const SCALE_SHORT[10] = {"MAJOR", "MINOR", "PENTMN", "PENTMJ", "DORIAN", "PHRYG", "LYDIAN",
                                            "MIXOLY", "HARMMN", "MELMN"};
static const char *const THEME_SHORT[3] = {"NAVY", "MONO", "MATRIX"};

static void cpy(char *d, const char *s) { *p_str(d, s) = 0; }
static char *p_p100(char *p, uint32_t v255) { return p_num(p, (v255 * 100u + 127u) / 255u); }

static void cell_text(cell_t c, char *name, char *val, int32_t *bar, uint32_t *scn)   /* name[12], val[16] */
{
    scene_t *s = &cur.sc[edit_sc], *v = &cur.sc[c.arg & 1u];
    uint32_t a6 = c.arg % 6u;
    char *p;
    *bar = -1; *scn = 0; name[0] = 0; val[0] = 0;
    switch (c.kind) {
    case K_DENS: cpy(name, "DENS"); *p_p100(val, cur.density) = 0; *bar = cur.density; break;
    case K_SWING: cpy(name, "SWING"); *p_p100(val, cur.swing) = 0; *bar = cur.swing; break;
    case K_P:
        cpy(name, PSHORT[a6]);
        *scn = (uint32_t)edit_sc + 1u;
        if (a6 == 3u) *p_sig(val, ((int32_t)s->p[3] - 127) * 100 / 127) = 0;
        else *p_p100(val, s->p[a6]) = 0;
        *bar = s->p[a6];
        break;
    case K_AB: cpy(name, "A-B"); *p_p100(val, cur.morph_pos) = 0; *bar = cur.morph_pos; break;
    case K_INST: cpy(name, "INST"); cpy(val, INST_SHORT[v->inst % 5u]); *scn = (c.arg & 1u) + 1u; break;
    case K_TIMB: cpy(name, "TIMBRE"); *p_p100(val, v->timbre) = 0; *bar = v->timbre; *scn = (c.arg & 1u) + 1u; break;
    case K_DLY: cpy(name, "DELAY"); *p_p100(val, v->dsend) = 0; *bar = v->dsend; *scn = (c.arg & 1u) + 1u; break;
    case K_LVL: cpy(name, "LEVEL"); *p_p100(val, v->level) = 0; *bar = v->level; *scn = (c.arg & 1u) + 1u; break;
    case K_LSPD:
        p = p_str(name, LSHORT[a6]); cpy(p, " SP");
        cpy(val, LFO_RATE[cur.lfo_rate[a6] % 5u]);
        *bar = (int32_t)cur.lfo_rate[a6] * 255 / 4;
        break;
    case K_LDEP:
        p = p_str(name, LSHORT[a6]); cpy(p, " DP");
        *p_p100(val, cur.lfo_depth[a6]) = 0;
        *bar = cur.lfo_depth[a6];
        break;
    case K_BPM: cpy(name, "BPM"); *p_num(val, cur.bpm) = 0; break;
    case K_STEP: cpy(name, "STEP"); cpy(val, DIV_NAME[cur.div & 3u]); break;
    case K_TRANS: cpy(name, "TRANSP"); *p_sig(val, cur.tr) = 0; break;
    case K_THEME: cpy(name, "THEME"); cpy(val, THEME_SHORT[glob.theme % 3u]); break;
    case K_ROOT: cpy(name, "ROOT"); cpy(val, ROOT_NAME[cur.root % 12u]); break;
    case K_SCALE: cpy(name, "SCALE"); cpy(val, SCALE_SHORT[cur.scale % 10u]); break;
    case K_CHA: cpy(name, "CH A"); *p_num(val, (uint32_t)glob.ch[0] + 1u) = 0; break;
    case K_CHB: cpy(name, "CH B"); *p_num(val, (uint32_t)glob.ch[1] + 1u) = 0; break;
    default: break;
    }
}

static void adj(uint8_t *v, int32_t d, int32_t step) { *v = (uint8_t)clampi((int32_t)*v + d * step, 0, 255); }

static void cell_adj(cell_t c, int32_t d)       /* d: signed clicks, larger when the knob is turned fast */
{
    scene_t *s = &cur.sc[edit_sc], *v = &cur.sc[c.arg & 1u];
    int32_t sg = d > 0 ? 1 : -1;
    switch (c.kind) {
    case K_DENS: adj(&cur.density, d, 3); break;
    case K_SWING: adj(&cur.swing, d, 3); break;
    case K_P: adj(&s->p[c.arg % 6u], d, 3); break;
    case K_AB: adj(&cur.morph_pos, d, 3); break;
    case K_INST: v->inst = (uint8_t)clampi((int32_t)v->inst + sg, 0, 4); break;
    case K_TIMB: adj(&v->timbre, d, 3); break;
    case K_DLY: adj(&v->dsend, d, 3); break;
    case K_LVL: adj(&v->level, d, 3); break;
    case K_LSPD: cur.lfo_rate[c.arg % 6u] = (uint8_t)clampi((int32_t)cur.lfo_rate[c.arg % 6u] + sg, 0, 4); break;
    case K_LDEP: adj(&cur.lfo_depth[c.arg % 6u], d, 3); break;
    case K_BPM: cur.bpm = (uint8_t)clampi((int32_t)cur.bpm + d, 40, 240); break;
    case K_STEP: cur.div = (uint8_t)clampi((int32_t)cur.div + sg, 0, 3); break;
    case K_TRANS: cur.tr = (int8_t)clampi((int32_t)cur.tr + sg, -12, 12); break;
    case K_THEME: theme_set((uint32_t)((int32_t)glob.theme + 3 + sg) % 3u); break;
    case K_ROOT: cur.root = (uint8_t)clampi((int32_t)cur.root + sg, 0, 11); break;
    case K_SCALE: cur.scale = (uint8_t)clampi((int32_t)cur.scale + sg, 0, 9); break;
    case K_CHA: glob.ch[0] = (uint8_t)clampi((int32_t)glob.ch[0] + sg, 0, 15); break;
    case K_CHB: glob.ch[1] = (uint8_t)clampi((int32_t)glob.ch[1] + sg, 0, 15); break;
    default: break;
    }
}

static void cell_overlay(cell_t c)              /* the touched value takes over the screen */
{
    char n[12], v[16];
    int32_t bar;
    uint32_t scn;
    cell_text(c, n, v, &bar, &scn);
    hud(n, v, bar);
    hud_scn = (uint8_t)scn;
}

static uint32_t kn_t[8];
static int32_t kn_run[8];
static int32_t knob_delta(uint32_t id, int32_t e)   /* a fast turn takes bigger steps, a slow one stays fine */
{
    int32_t n = e < 0 ? -e : e, mult;
    uint32_t dt = fm1_ms - kn_t[id & 7u];
    kn_t[id & 7u] = fm1_ms;
    kn_run[id & 7u] = dt < 70u ? kn_run[id & 7u] + n : n;
    mult = kn_run[id & 7u] < 4 ? 1 : kn_run[id & 7u] < 10 ? 2 : kn_run[id & 7u] < 20 ? 4 : 8;
    return (e < 0 ? -n : n) * mult;
}

/* ---- pages: two halves of four cells; the knobs 1-4 turn the cells of the lit half; the buttons step through */
enum { G_HOME, G_FX, G_LFO, G_GLO };
static const char *const GROUP_NAME[4] = {"HOME", "FX", "LFO", "GLO"};
typedef struct { uint8_t group, pos; const char *tab; cell_t c[4]; } half_t;
static const half_t HALF[] = {
    {G_HOME, 0, "GENERATE", {{K_P, 3}, {K_P, 4}, {K_P, 5}, {K_P, 0}}},
    {G_HOME, 1, "RHYTHM", {{K_P, 1}, {K_DENS, 0}, {K_SWING, 0}, {K_P, 2}}},
    {G_FX, 0, "VOICE A", {{K_INST, 0}, {K_TIMB, 0}, {K_DLY, 0}, {K_LVL, 0}}},
    {G_FX, 1, "VOICE B", {{K_INST, 1}, {K_TIMB, 1}, {K_DLY, 1}, {K_LVL, 1}}},
    {G_LFO, 0, "MORPH REST", {{K_LSPD, 0}, {K_LDEP, 0}, {K_LSPD, 1}, {K_LDEP, 1}}},
    {G_LFO, 1, "LEGATO ENTROPY", {{K_LSPD, 2}, {K_LDEP, 2}, {K_LSPD, 3}, {K_LDEP, 3}}},
    {G_LFO, 0, "HARMONY CHAOS", {{K_LSPD, 4}, {K_LDEP, 4}, {K_LSPD, 5}, {K_LDEP, 5}}},
    {G_GLO, 0, "TEMPO KEY", {{K_BPM, 0}, {K_STEP, 0}, {K_TRANS, 0}, {K_THEME, 0}}},
    {G_GLO, 1, "SCALE MIDI", {{K_ROOT, 0}, {K_SCALE, 0}, {K_CHA, 0}, {K_CHB, 0}}},
};
#define NHALF ((int32_t)(sizeof HALF / sizeof HALF[0]))
static int32_t cur_half = -1;                   /* -1: the performance screen */

static void goto_group(uint32_t g)              /* its button: into the group, then half by half, then out */
{
    int32_t h = cur_half, i;
    if (h >= 0 && HALF[h].group == g) {
        h++;
        if (h >= NHALF || HALF[h].group != g) h = -1;
    } else {
        h = -1;
        for (i = 0; i < NHALF; i++)
            if (HALF[i].group == g) { h = i; break; }
    }
    cur_half = h;
}

/* ---- the other screens: the menus (SELECT knob) with rows of label, value, bar */
enum { V_PERF, V_SCALE, V_COPY, V_SYS, V_PAGE, V_SLOTS, V_OVL };
static uint8_t view, view_drawn = 255;
static uint8_t menu_pg, copy_act, save_mode, recall_mode, tr_view;
static uint32_t tr_t0, dsp_pct;
static char rows_cache[5][28];
static const char *const COPY_NAME[4] = {"COPY A TO B", "COPY B TO A", "INIT SCENE", "INIT ALL"};

typedef struct { char label[16], value[16]; int16_t bar; uint8_t hl; } row_t;
static const char *view_title(void)
{
    switch (view) {
    case V_SCALE: return "SCALE";
    case V_COPY: return "COPY INIT";
    default: return "SYSTEM";
    }
}
static void setrow(row_t *r, const char *l, int16_t bar, int hl) { char *p = p_str(r->label, l); *p = 0; r->bar = bar; r->hl = (uint8_t)hl; r->value[0] = 0; }
static void setval_s(row_t *r, const char *s) { char *p = p_str(r->value, s); *p = 0; }
static void setval_n(row_t *r, uint32_t v) { char *p = p_num(r->value, v); *p = 0; }

static uint32_t view_row(uint32_t i, row_t *r)  /* 0: no such row */
{
    switch (view) {
    case V_SCALE:
        if (i == 0) { setrow(r, "ROOT", -1, 0); setval_s(r, ROOT_NAME[cur.root % 12u]); return 1; }
        if (i == 1) { setrow(r, "SCALE", -1, 0); setval_s(r, SCALE_NAME[cur.scale % 10u]); return 1; }
        return 0;
    case V_COPY:
        if (i == 0) { setrow(r, COPY_NAME[copy_act & 3u], -1, 1); setval_s(r, "SAVE=GO"); return 1; }
        return 0;
    case V_SYS:
        if (i == 0) { setrow(r, "THEME", -1, 0); setval_s(r, THEME[glob.theme % 3u].name); return 1; }
        if (i == 1) { setrow(r, "OUT A CH", -1, 0); setval_n(r, (uint32_t)glob.ch[0] + 1u); return 1; }
        if (i == 2) { setrow(r, "OUT B CH", -1, 0); setval_n(r, (uint32_t)glob.ch[1] + 1u); return 1; }
        if (i == 3) { char *p; setrow(r, "DSP LOAD", (int16_t)clampi((int32_t)dsp_pct * 255 / 100, 0, 255), 0); p = p_num(r->value, dsp_pct); *p++ = '%'; *p = 0; return 1; }
        return 0;
    default:
        return 0;
    }
}

static void draw_row_strip(uint32_t y, const row_t *r)
{
    cv_begin(240, 24, C_BLACK);
    cv_text(12, 2, &FONT_S, r->label, r->hl ? pal[4] : pal[2]);
    cv_text(236 - text_w(&FONT_S, r->value), 2, &FONT_S, r->value, pal[4]);
    if (r->bar >= 0) {
        cv_rect(12, 20, 216, 2, pal[1]);
        cv_rect(12, 20, (int32_t)r->bar * 216 / 255, 2, r->hl ? pal[4] : pal[3]);
    }
    cv_blit(0, y);
}

static void draw_slots(void)                    /* the 16 preset slots: filled square = saved, outline = empty */
{
    uint32_t i;
    bank_open();
    cv_begin(240, 64, C_BLACK);
    for (i = 0; i < 16u; i++) {
        int32_t x = 10 + (int32_t)(i & 7u) * 28, y = 4 + (int32_t)(i >> 3) * 30;
        uint16_t c = i == glob.slot ? pal[4] : pal[3];
        if (bank.p[i].used) cv_rect(x, y, 24, 24, c);
        else { cv_rect(x, y, 24, 1, pal[1]); cv_rect(x, y + 23, 24, 1, pal[1]); cv_rect(x, y, 1, 24, pal[1]); cv_rect(x + 23, y, 1, 24, pal[1]); }
    }
    cv_blit(0, 92);
}

/* one panel of a page: four cells, the lit half framed bright */
static void draw_panel(int32_t hi, uint32_t y, int lit)
{
    uint32_t i;
    uint16_t fc = lit ? pal[4] : pal[1];
    cv_begin(240, 64, C_BLACK);
    cv_rect(2, 0, 236, lit ? 2 : 1, fc);
    cv_rect(2, 63, 236, lit ? 2 : 1, fc);
    cv_rect(2, 0, lit ? 2 : 1, 64, fc);
    cv_rect(237, 0, lit ? 2 : 1, 64, fc);
    if (hi >= 0)
        for (i = 0; i < 4u; i++) {
            char name[12], val[16];
            int32_t bar, x = 6 + (int32_t)i * 57, vy;
            uint32_t scn;
            const felucca_font_t *vf;
            if (HALF[hi].c[i].kind == K_NONE) continue;
            cell_text(HALF[hi].c[i], name, val, &bar, &scn);
            cv_text(x + 3, 4, &FONT_S, name, lit ? pal[3] : pal[1]);
            vf = text_w(&FONT_L, val) <= 52 ? &FONT_L : &FONT_S;
            vy = vf == &FONT_L ? 20 : 28;
            cv_text(x + (52 - text_w(vf, val)) / 2, vy, vf, val, lit ? pal[4] : pal[2]);
            if (bar >= 0) {
                cv_rect(x + 3, 56, 46, 3, pal[0]);
                cv_rect(x + 3, 56, bar * 46 / 255, 3, lit ? pal[4] : pal[2]);
            }
            if (i) cv_rect(x - 2, 10, 1, 44, pal[0]);
        }
    cv_blit(0, y);
}

static uint32_t sig_str(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}
static uint32_t panel_sig(int32_t hi, int lit)
{
    uint32_t i, h = lit ? 99u : 7u;
    if (hi < 0) return h + 1u;
    for (i = 0; i < 4u; i++) {
        char name[12], val[16];
        int32_t bar;
        uint32_t scn;
        cell_text(HALF[hi].c[i], name, val, &bar, &scn);
        h = h * 31u + sig_str(name) + sig_str(val) * 7u + (uint32_t)(bar + 1);
    }
    return h;
}

/* the overlay: scene tag, the name, the value at double size, a bar of 45 segments */
static void draw_ov_tag(void)
{
    const char *t = hud_scn == 1u ? "SCENE A" : "SCENE B";
    cv_begin(240, 16, C_BLACK);
    if (hud_scn) cv_text((240 - text_w(&FONT_S, t)) / 2, 0, &FONT_S, t, hud_scn == 1u ? C_A : C_B);
    cv_blit(0, 28);
}
static void draw_ov_name(void)
{
    cv_begin(240, 32, C_BLACK);
    cv_text((240 - text_w(&FONT_L, hud_name)) / 2, 0, &FONT_L, hud_name, pal[3]);
    cv_blit(0, 50);
}
static void draw_ov_val(void)
{
    int32_t w2 = text_w(&FONT_L, hud_val) * 2;
    cv_begin(240, 64, C_BLACK);
    if (w2 <= 232) cv_text2((240 - w2) / 2, 0, &FONT_L, hud_val, pal[4]);
    else cv_text((240 - text_w(&FONT_L, hud_val)) / 2, 16, &FONT_L, hud_val, pal[4]);
    cv_blit(0, 96);
}
static void draw_ov_bar(void)
{
    uint32_t i, lit = hud_bar >= 0 ? (uint32_t)hud_bar * 45u / 255u : 0u;
    cv_begin(240, 24, C_BLACK);
    if (hud_bar >= 0)
        for (i = 0; i < 45u; i++) cv_rect(8 + (int32_t)i * 5, 4, 4, 16, i < lit ? pal[4] : pal[0]);
    cv_blit(0, 176);
}

static uint32_t t_anim, t_rows, t_tower;
static char head_cache[8], pg_head[40];
static uint32_t foot_sig, morph_sig, slots_sig, pg_sig[2], ov_sig[3];

static uint32_t sig_of(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    while (n--) { h ^= *b++; h *= 16777619u; }
    return h;
}

/* one draw job a call: so a note due in the main loop never waits for more than one strip of the screen */
static void draw_tick(void)
{
    uint32_t i;
    if (view != view_drawn) {                   /* a new screen: clear it, the rest follows strip by strip */
        lcd_fill(0, 0, 240, 240, C_BLACK);
        view_drawn = view;
        for (i = 0; i < 5u; i++) rows_cache[i][0] = 1;
        head_cache[0] = 0x7F; pg_head[0] = 1;                 /* (a value no header has: it is drawn again) */
        foot_sig = 0; morph_sig = 0; slots_sig = 0; pg_sig[0] = pg_sig[1] = 0;
        ov_sig[0] = ov_sig[1] = ov_sig[2] = 0;
        t_anim = 0;
        if (view == V_SCALE || view == V_COPY || view == V_SYS) draw_text_box(0, 18, 240, &FONT_L, view_title(), pal[4], 1);
        return;
    }
    if (view == V_OVL) {
        uint32_t s0 = sig_str(hud_name) + hud_scn * 977u, s1 = sig_str(hud_val), s2 = (uint32_t)(hud_bar + 1) * 2654435761u + 1u;
        if (s0 != ov_sig[0]) { ov_sig[0] = s0; draw_ov_tag(); return; }
        if (s1 != ov_sig[1]) { ov_sig[1] = s1; draw_ov_name(); draw_ov_val(); return; }
        if (s2 != ov_sig[2]) { ov_sig[2] = s2; draw_ov_bar(); return; }
        return;
    }
    if (view == V_PERF || view == V_PAGE) {
        uint32_t m = sig_of(&cur.morph_pos, 1) ^ ((uint32_t)edit_sc << 8) ^ ((uint32_t)mute_mask << 12) ^ sig_of(cur.lfo_rate, 12) ^ ((uint32_t)glob.theme << 20);
        uint32_t f = sig_of(&cur.density, 2) ^ ((uint32_t)cur.oct << 16) ^ ((uint32_t)glob.theme << 24);
        if (view == V_PERF) {
            char hs[8];
            hs[0] = (char)(edit_sc + 1u); hs[1] = (char)(cur.mode + 1u); hs[2] = (char)(latch + 1u);
            hs[3] = (char)(cur.poly + 1u + 2u * auto_roll); hs[4] = (char)(frozen + 1u); hs[5] = (char)(cur.bpm);
            hs[6] = (char)(1u + glob.theme); hs[7] = 0;
            if (memcmp(head_cache, hs, 7) != 0) { memcpy(head_cache, hs, 8); draw_head(); return; }
        } else {
            int32_t lit = cur_half, top = lit - HALF[lit].pos;
            int32_t bot = (top + 1 < NHALF && HALF[top + 1].group == HALF[top].group && HALF[top + 1].pos == 1) ? top + 1 : -1;
            char hs[40], *p;
            uint32_t s0, s1;
            memset(hs, 0, sizeof hs);
            p = p_str(hs, GROUP_NAME[HALF[lit].group]);
            p = p_str(p, "  ");
            p_str(p, HALF[lit].tab);
            if (memcmp(pg_head, hs, sizeof hs) != 0) { memcpy(pg_head, hs, sizeof hs); draw_text_box(8, 2, 224, &FONT_S, hs, pal[4], 0); return; }
            s0 = panel_sig(top, HALF[lit].pos == 0);
            if (s0 != pg_sig[0]) { pg_sig[0] = s0; draw_panel(top, 22, HALF[lit].pos == 0); return; }
            s1 = panel_sig(bot, HALF[lit].pos == 1);
            if (s1 != pg_sig[1]) { pg_sig[1] = s1; draw_panel(bot, 92, HALF[lit].pos == 1); return; }
        }
        if (m != morph_sig) { morph_sig = m; draw_morph(); return; }
        if (f != foot_sig) { foot_sig = f; draw_foot(); return; }
        if (view == V_PERF) {
            if (fm1_ms - t_anim >= 70u) { t_anim = fm1_ms; gyaw += 0x01400000u; draw_globe_hud(); return; }
            if (fm1_ms - t_tower >= 45u) { t_tower = fm1_ms; draw_towers(); return; }
        }
        return;
    }
    if (view == V_SLOTS) {
        uint32_t m = (uint32_t)glob.slot ^ ((uint32_t)save_mode << 8);
        for (i = 0; i < 16u; i++) m = m * 31u + bank.p[i].used;
        if (m != slots_sig) { slots_sig = m; draw_slots(); return; }
        return;
    }
    if (fm1_ms - t_rows >= 15u) {
        for (i = 0; i < 5u; i++) {
            row_t r;
            char key[28], *p = key;
            memset(&r, 0, sizeof r);
            if (!view_row(i, &r)) { key[0] = 0; }
            else {
                p = p_str(p, r.label); *p++ = '|'; p = p_str(p, r.value); *p++ = '|'; *p++ = (char)(r.bar >> 3); *p++ = (char)r.hl; *p = 0;
            }
            if (memcmp(rows_cache[i], key, 27) != 0) {
                memcpy(rows_cache[i], key, sizeof key > 27u ? 27u : sizeof key);
                rows_cache[i][27] = 0;
                if (key[0]) draw_row_strip(70u + i * 30u, &r);
                else lcd_fill(0, 70u + i * 30u, 240, 24, C_BLACK);
                return;
            }
        }
        t_rows = fm1_ms;
    }
}

/* ================================================================== 7. the controls and the main loop */
static const uint8_t WHITE_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_KEY[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
#define KEY0 53u                                /* key 0 = F3 */

static void roll(void)                          /* randomise the scene the knobs edit */
{
    scene_t *s = &cur.sc[edit_sc];
    uint32_t i;
    for (i = 0; i < 8u; i++) s->fader[i] = (uint8_t)rnd8();
    s->p[0] = (uint8_t)rnd8();
    s->p[1] = (uint8_t)(rnd8() >> 1);
    s->p[2] = (uint8_t)(26u + rnd8() * 229u / 255u);
    s->p[3] = (uint8_t)(rnd8() % 255u);
    s->p[4] = (uint8_t)rnd8();
    s->p[5] = (uint8_t)(rnd8() >> 1);
}

static void do_copy(void)
{
    switch (copy_act & 3u) {
    case 0: cur.sc[1] = cur.sc[0]; hud("COPIED", "A TO B", -1); break;
    case 1: cur.sc[0] = cur.sc[1]; hud("COPIED", "B TO A", -1); break;
    case 2: scene_init(&cur.sc[edit_sc], (int)edit_sc); hud("INIT", edit_sc ? "SCENE B" : "SCENE A", -1); break;
    default: preset_init(&cur); hud("INIT", "ALL", -1); break;
    }
}

static void save_slot(uint32_t slot)
{
    bank_open();
    bank.p[slot] = cur;
    bank.p[slot].used = 1;
    glob.slot = (uint8_t)slot;
    bank_save();
}

static void select_view(void)
{
    uint8_t v = V_PERF;
    if (fm1_ms < hud_until) v = V_OVL;
    else if (save_mode || recall_mode) v = V_SLOTS;
    else if (tr_view) v = tr_view;
    else if (cur_half >= 0) v = V_PAGE;
    view = v;
}

typedef struct { preset_t cur; uint8_t ver, theme, ch0, ch1, slot; } saved_t;

static void leds(uint32_t lit_notes)
{
    uint32_t i, blink = (fm1_ms >> 8) & 1u, slots = save_mode || recall_mode;
    int32_t g = cur_half >= 0 ? (int32_t)HALF[cur_half].group : -1;
    fm1_led_key(BTN_ARP, cur.mode == MODE_ARP);
    fm1_led_key(BTN_SEQ, cur.mode == MODE_SEQ);
    fm1_led_key(BTN_SEL, cur.poly);
    fm1_led_key(BTN_REC, frozen);
    fm1_led_key(BTN_PLAY, running);
    fm1_led_key(BTN_EDIT, auto_roll);
    fm1_led_key(BTN_FX, g == G_FX);
    fm1_led_key(BTN_LFO, g == G_LFO);
    fm1_led_key(BTN_GLO, g == G_GLO);
    fm1_led_key(BTN_HOME, g < 0 || g == G_HOME);
    fm1_led_key(BTN_SAVE, save_mode ? blink : recall_mode);
    fm1_led_key(BTN_OCT_DOWN, cur.oct < 0);
    fm1_led_key(BTN_OCT_UP, cur.oct > 0);
    for (i = 0; i < 16u; i++) {
        int on = slots ? (bank.p[i].used != 0 && (i != glob.slot || blink)) : (int)((lit_notes >> WHITE_KEY[i]) & 1u);
        fm1_led_key(KEY_LED(WHITE_KEY[i]), on);
    }
    for (i = 0; i < 8u; i++) {
        int on = running && cstep == i ? 1 : (cur.sc[edit_sc].fader[i] > 127u) && !((mute_mask >> i) & 1u);
        fm1_led_key(KEY_LED(BLACK_KEY[i]), on);
    }
    fm1_led_key(KEY_LED(BLACK_KEY[8]), edit_sc == 0);
    fm1_led_key(KEY_LED(BLACK_KEY[9]), edit_sc == 1);
    fm1_led_key(KEY_LED(BLACK_KEY[10]), mute_mask != 0);
}

static saved_t sv;

static void app_init(void)
{
    preset_init(&cur);
    theme_set(0);
    globe_init();
    rng_s ^= fm1_ticks();
    if (!rng_s) rng_s = 1;
    if (settings_load(&sv, sizeof sv) == 0 && sv.ver == 1u) {
        cur = sv.cur; glob.theme = sv.theme % 3u; glob.ch[0] = sv.ch0 & 15u; glob.ch[1] = sv.ch1 & 15u; glob.slot = sv.slot & 15u;
        cur.bpm = (uint8_t)clampi(cur.bpm, 40, 240);
        cur.div &= 3u; cur.scale %= 10u; cur.root %= 12u; cur.oct = (int8_t)clampi(cur.oct, -3, 3);
        cur.tr = (int8_t)clampi(cur.tr, -12, 12);
        theme_set(glob.theme);
    }
    dly_len = 22000;
    weights();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 90, 240, &FONT_L, APP_NAME, pal[4], 1);
    draw_text_box(0, 130, 240, &FONT_S, "v" APP_VER, pal[2], 1);
}

static void app_step(void)                      /* one pass of the main loop (a browser calls it by itself) */
{
    static uint32_t prev, dirty, t_change, save_t0, save_down, save_long, arp_t0, arp_down, arp_long, edit_t0, edit_down,
                    edit_long, t_ui, mute_t0;
    static uint8_t liven[27], booted;
    uint32_t k, keys, down, up, rel, b, now, lit = 0, kk;
    uint8_t st, d1, d2;
    int32_t e0, e1, e2, e3, e4, e5, e6;
    uint32_t slots;
    platform_poll();
    if (fm1_ms < 1200u) return;                 /* the splash */
    if (!booted) { booted = 1; lcd_fill(0, 0, 240, 240, C_BLACK); last_t = fm1_ticks(); }
    now = fm1_ticks();

    /* --- the clock: before anything slow */
    update_eff(now - last_t);
    last_t = now;
    offs_run(now, 0);
    if (running && (int32_t)(now - next_t) >= 0) {
        uint32_t base = step_ticks(), sw = (base >> 8) * (((uint32_t)cur.swing * 115u) >> 8);
        do_step();
        next_t += (cstep & 1u) ? base - sw : base + sw;      /* (cstep is the step that just played: even steps are late) */
        if ((int32_t)(now - next_t) > (int32_t)(base * 4u)) next_t = now + base;
    }

    /* --- the keys */
    slots = save_mode || recall_mode;
    keys = fm1_in.notes;
    down = keys & ~prev;
    up = prev & ~keys;
    prev = keys;
    for (k = 0; k < 16u; k++) {
        uint32_t bit = 1u << WHITE_KEY[k], note = KEY0 + WHITE_KEY[k] + (uint32_t)((int32_t)cur.oct * 12 + cur.tr);
        if (down & bit) {
            if (slots) {
                if (save_mode) { save_slot(k); save_mode = 0; hud_num("SAVED SLOT", (int32_t)k + 1, 0); }
                else if (recall_mode) {
                    bank_open();
                    if (bank.p[k].used) {
                        if (running) pend_slot = (int32_t)k; else recall_apply((int32_t)k);
                        glob.slot = (uint8_t)k;
                    }
                }
                dirty = 1; t_change = fm1_ms;
            } else if (hold_step_mask && cur.mode == MODE_SEQ) {        /* SEQ: hold a step key, tap a note */
                for (kk = 0; kk < 8u; kk++)
                    if (hold_step_mask >> kk & 1u) {
                        cur.sc[edit_sc].seq[kk] = (uint8_t)clampi((int32_t)note, 0, 127);
                        note_on(edit_sc, note, 3600000u);
                        hud_note("STEP NOTE", note);
                        hud_scn = (uint8_t)(edit_sc + 1u);
                        dirty = 1; t_change = fm1_ms;
                    }
            } else {
                liven[k] = (uint8_t)clampi((int32_t)note, 0, 127);
                key_note(liven[k], 1);
            }
        }
        if (up & bit) {
            if (liven[k]) { key_note(liven[k], 0); liven[k] = 0; }
        }
    }
    for (k = 0; k < 11u; k++) {
        uint32_t bit = 1u << BLACK_KEY[k];
        if (down & bit) {
            if (k < 8u) hold_step_mask |= 1u << k;
            else if (k == 8u || k == 9u) { edit_sc = (uint8_t)(k - 8u); hud("EDIT", edit_sc ? "SCENE B" : "SCENE A", -1); hud_scn = (uint8_t)(edit_sc + 1u); }
            else { mute_t0 = fm1_ms; hold_step_mask |= 1u << 10; }
        }
        if (up & bit) {
            if (k < 8u) {
                if (hold_step_mask & (1u << 10)) { mute_mask ^= (uint8_t)(1u << k); dirty = 1; t_change = fm1_ms; }
                hold_step_mask &= ~(1u << k);
            } else if (k == 10u) {
                if (fm1_ms - mute_t0 < 500u && !(hold_step_mask & 0xFFu)) { mute_mask = mute_mask ? 0 : 0xFFu; hud("MUTE", mute_mask ? "ALL" : "OFF", -1); }
                hold_step_mask &= ~(1u << 10);
                dirty = 1; t_change = fm1_ms;
            }
        }
    }
    for (k = 0; k < 27u; k++) if (keys >> k & 1u) { lit |= 1u << k; }

    /* --- the buttons */
    b = fm1_input_edges(&rel);
    if (b & (1u << BTN_OCT_DOWN) && cur.oct > -3) { cur.oct--; dirty = 1; t_change = fm1_ms; hud_num("OCTAVE", cur.oct, 1); ov_btn = 1u << BTN_OCT_DOWN; }
    if (b & (1u << BTN_OCT_UP) && cur.oct < 3) { cur.oct++; dirty = 1; t_change = fm1_ms; hud_num("OCTAVE", cur.oct, 1); ov_btn = 1u << BTN_OCT_UP; }
    if (b & (1u << BTN_ARP)) { arp_down = 1; arp_t0 = fm1_ms; arp_long = 0; }
    if (arp_down && !arp_long && fm1_ms - arp_t0 > 600u) {                       /* long: Latch */
        arp_long = 1;
        latch = !latch;
        if (!latch) { latch_len = 0; first_of_chord = 1; }
        hud_btn("LATCH", latch ? "ON" : "OFF", BTN_ARP);
        dirty = 1; t_change = fm1_ms;
    }
    if (rel & (1u << BTN_ARP)) {
        if (!arp_long) { cur.mode = MODE_ARP; hud("MODE", "ARP", -1); dirty = 1; t_change = fm1_ms; }
        arp_down = 0;
    }
    if (b & (1u << BTN_SEQ)) { cur.mode = MODE_SEQ; hud_btn("MODE", "SEQ", BTN_SEQ); dirty = 1; t_change = fm1_ms; }
    if (b & (1u << BTN_SEL)) { cur.poly = !cur.poly; hud_btn("POLY", cur.poly ? "ON" : "OFF", BTN_SEL); dirty = 1; t_change = fm1_ms; }
    if (b & (1u << BTN_REC)) { freeze_set(!frozen); hud_btn("FREEZE", frozen ? "ON" : "OFF", BTN_REC); }
    if (b & (1u << BTN_PLAY)) { transport(!running); hud_btn(running ? "PLAY" : "STOP", running ? "RUNNING" : "STOPPED", BTN_PLAY); }
    if (b & (1u << BTN_EDIT)) { edit_down = 1; edit_t0 = fm1_ms; edit_long = 0; }
    if (edit_down && !edit_long && fm1_ms - edit_t0 > 600u) {                    /* long: auto re-roll each measure */
        edit_long = 1;
        auto_roll = !auto_roll;
        hud_btn("AUTO ROLL", auto_roll ? "ON" : "OFF", BTN_EDIT);
    }
    if (rel & (1u << BTN_EDIT)) {
        if (edit_down && !edit_long) { roll(); hud("RANDOMISED", edit_sc ? "SCENE B" : "SCENE A", -1); hud_scn = (uint8_t)(edit_sc + 1u); dirty = 1; t_change = fm1_ms; }
        edit_down = 0;
    }
    if (b & ((1u << BTN_FX) | (1u << BTN_LFO) | (1u << BTN_GLO) | (1u << BTN_HOME))) { tr_view = 0; menu_pg = 0; hud_until = 0; }
    if (b & (1u << BTN_FX)) goto_group(G_FX);
    if (b & (1u << BTN_LFO)) goto_group(G_LFO);
    if (b & (1u << BTN_GLO)) goto_group(G_GLO);
    if (b & (1u << BTN_HOME)) {
        save_mode = recall_mode = 0;
        if (cur_half >= 0 && HALF[cur_half].group != G_HOME) cur_half = -1;
        else goto_group(G_HOME);
    }
    if (b & (1u << BTN_SAVE)) {
        if (tr_view == V_COPY) { do_copy(); dirty = 1; t_change = fm1_ms; tr_t0 = fm1_ms; }
        else { save_down = 1; save_t0 = fm1_ms; save_long = 0; }
    }
    if (save_down && !save_long && fm1_ms - save_t0 > 600u) {                    /* long: recall mode, stays */
        save_long = 1;
        save_mode = 0;
        recall_mode = !recall_mode;
        bank_open();
    }
    if (rel & (1u << BTN_SAVE)) {
        if (save_down && !save_long) {
            if (save_mode) save_mode = 0;
            else if (!recall_mode) { save_mode = 1; bank_open(); }
        }
        save_down = 0;
    }
    if (ov_btn) {                                                               /* a button's message stays while it is held */
        if (fm1_in.buttons & ov_btn) hud_until = fm1_ms + 450u;
        else ov_btn = 0;
    }

    /* --- the encoders */
    e0 = fm1_enc_take(ENC_SELECT);
    e1 = fm1_enc_take(ENC_ALGORITHM);
    e2 = fm1_enc_take(ENC_KNOB1);
    e3 = fm1_enc_take(ENC_KNOB2);
    e4 = fm1_enc_take(ENC_KNOB3);
    e5 = fm1_enc_take(ENC_KNOB4);
    e6 = fm1_enc_take(ENC_PRESETS);
    if (e0) { menu_pg = (uint8_t)clampi((int32_t)menu_pg + (e0 > 0 ? 1 : -1), 0, 3); tr_view = menu_pg ? (uint8_t)(V_SCALE + menu_pg - 1u) : 0; tr_t0 = fm1_ms; }
    if (e1) { cell_t ab = {K_AB, 0}; cell_adj(ab, knob_delta(1, e1)); cell_overlay(ab); dirty = 1; t_change = fm1_ms; }      /* the A-to-B crossfade, on every screen */
    if (e6) {
        cell_t ins = {K_INST, 0};
        ins.arg = edit_sc;
        cell_adj(ins, e6);
        cell_overlay(ins);
        dirty = 1; t_change = fm1_ms;
    }
    if (e2 | e3 | e4 | e5) {
        int32_t ev[4], i;
        ev[0] = e2; ev[1] = e3; ev[2] = e4; ev[3] = e5;
        if (tr_view) {                                                          /* the menus */
            tr_t0 = fm1_ms;
            if (tr_view == V_SCALE) {
                if (e2) cur.root = (uint8_t)clampi((int32_t)cur.root + (e2 > 0 ? 1 : -1), 0, 11);
                if (e3) cur.scale = (uint8_t)clampi((int32_t)cur.scale + (e3 > 0 ? 1 : -1), 0, 9);
            } else if (tr_view == V_COPY) {
                if (e2) copy_act = (uint8_t)clampi((int32_t)copy_act + (e2 > 0 ? 1 : -1), 0, 3);
            } else {
                if (e2) { theme_set((uint32_t)((int32_t)glob.theme + 3 + (e2 > 0 ? 1 : -1)) % 3u); view_drawn = 255; }
                if (e3) glob.ch[0] = (uint8_t)clampi((int32_t)glob.ch[0] + (e3 > 0 ? 1 : -1), 0, 15);
                if (e4) glob.ch[1] = (uint8_t)clampi((int32_t)glob.ch[1] + (e4 > 0 ? 1 : -1), 0, 15);
            }
        } else {
            const half_t *h = &HALF[cur_half >= 0 ? cur_half : 1];            /* the performance screen turns the RHYTHM cells */
            for (i = 0; i < 4; i++) {
                cell_t c = h->c[i];
                if (!ev[i] || c.kind == K_NONE) continue;
                if (cur_half < 0 && i == 0 && (hold_step_mask & 0xFFu)) {      /* K1 with a step key held: that step's level */
                    uint32_t first = 8;
                    for (kk = 0; kk < 8u; kk++)
                        if (hold_step_mask >> kk & 1u) { adj(&cur.sc[edit_sc].fader[kk], knob_delta(2, ev[i]), 3); if (first > 7u) first = kk; }
                    hud_pct("STEP LEVEL", cur.sc[edit_sc].fader[first & 7u]);
                    hud_scn = (uint8_t)(edit_sc + 1u);
                } else {
                    cell_adj(c, c.kind == K_P || c.kind == K_DENS || c.kind == K_SWING || c.kind == K_LDEP || c.kind == K_TIMB || c.kind == K_DLY || c.kind == K_LVL || c.kind == K_BPM ? knob_delta((uint32_t)(2 + i), ev[i]) : ev[i]);
                    cell_overlay(c);
                }
            }
        }
        dirty = 1; t_change = fm1_ms;
    }

    /* --- MIDI in: notes play like the keys, any channel */
    while (midi_get(&st, &d1, &d2)) {
        if ((st & 0xF0u) == 0x90u && d2) key_note(d1, 1);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) key_note(d1, 0);
    }

    /* --- bookkeeping: the screens that leave by themselves, the sound's settings, the screen */
    if (tr_view && fm1_ms - tr_t0 > 2500u) { tr_view = 0; menu_pg = 0; }
    { static uint32_t t_dsp; if (fm1_ms - t_dsp >= 500u) { uint32_t m = dsp_max; dsp_max = 0; t_dsp = fm1_ms; dsp_pct = m / 1392u; } }   /* % of one 5.8 ms block = 139200 ticks; under 50 is comfortable (docs/RULES.md) */
    weights();
    { uint32_t bpm = cur.bpm < 40u ? 40u : cur.bpm; dly_len = (int32_t)clampi((int32_t)(1984500u / bpm), 1000, 65000); }
    select_view();
    if (fm1_ms - t_ui >= 12u) { t_ui = fm1_ms; leds(lit); }
    draw_tick();

    if (dirty && !busy && fm1_ms - t_change > 2000u) {                          /* save when silent: an erase stalls the sound */
        sv.cur = cur; sv.ver = 1; sv.theme = glob.theme; sv.ch0 = glob.ch[0]; sv.ch1 = glob.ch[1]; sv.slot = glob.slot;
        settings_save(&sv, sizeof sv);
        dirty = 0;
    }
}

static void app_main(void)
{
    app_init();
    for (;;) app_step();
}
