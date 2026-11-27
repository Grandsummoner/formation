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
static const char *const INST_NAME[28] = {"MIDI ONLY", "ANALOG", "FM", "SUPERSAW", "PULSE", "E PIANO", "BELL", "PLUCK",
                                          "ORGAN", "BASS", "PAD", "BRASS", "PROPHET 5", "MINIMOOG", "JUNO PAD", "SYNC LEAD",
                                          "VIRUS SAW", "SH-101", "ACID 303", "STRINGS", "CS-80 BRASS", "DX BASS", "CLAVINET", "VIBES",
                                          "PWM PAD", "BREATH", "HOOVER", "WOBBLE BASS"};
static const char *const DIV_NAME[4] = {"1/4", "1/8", "1/16", "1/32"};
static const char *const LFO_RATE[9] = {"OFF", "1/4", "1/8", "1/16", "1/32", "1/2", "1 BAR", "2 BAR", "4 BAR"};   /* by the stored value: 1..4 are a beat, a half, a quarter, an eighth of a beat; 5..8 are 2, 4, 8, 16 beats */
static const uint8_t LFO_POS[9] = {0, 8, 7, 6, 5, 1, 2, 3, 4};              /* the order the rate knob goes through, slow to fast: the stored value at each place */
static const char *const LFO_SHAPE[6] = {"SINE", "TRI", "SAW", "RAMP", "SQUARE", "RANDOM"};
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
    uint8_t env[4];                             /* ATTACK DECAY SUSTAIN RELEASE, 128 = what the instrument has (the others are relative to it) */
    uint8_t dtime, dfb, dtone;                  /* this voice's delay: time (1/16 1/8 1/8. 1/4), feedback, tone (the send is dsend) */
    uint8_t rsend, rsize, rtone;                /* this voice's reverb: send, size, bright */
} scene_t;                                      /* 36 B */
typedef struct {
    scene_t sc[2];
    uint8_t lfo_rate[6], lfo_depth[6];
    uint8_t lfo_sp[6];                          /* each LFO's shape (bits 0..2) and phase (bits 3..7, in 32nds of a cycle) */
    uint8_t morph_pos, density, swing, bpm, div, scale, root, poly, mode;
    int8_t oct, tr;
    uint8_t used, rsv[4];
} preset17_t;                                   /* v1.7: 106 B (read, then converted: preset_t starts with the same bytes) */
typedef struct {
    scene_t sc[2];
    uint8_t lfo_rate[6], lfo_depth[6];
    uint8_t lfo_sp[6];                          /* each LFO's shape (bits 0..2) and phase (bits 3..7, in 32nds of a cycle) */
    uint8_t morph_pos, density, swing, bpm, div, scale, root, poly, mode;
    int8_t oct, tr;
    uint8_t used, rsv[4];
    uint8_t dr_on, dr_level, dr_tone, dr_decay, dr_tune, dr_send, dr_follow;      /* the drum voice: on, level, tone, decay, tune (128 plus the semitones, -12 .. +12), reverb send, kick follows the melody */
    uint8_t dr_shadow[2], dr_space[2];          /* how closely it shadows the melody, how much room it leaves: one of each for each scene (the morph blends them) */
} preset_t;                                     /* 117 B */
/* the presets and the settings of v1.0 .. v1.6 (a scene 26 B, a preset 80 B, one reverb for both scenes, one delay): read, then converted */
typedef struct { uint8_t fader[8], p[6], inst, timbre, dsend, level, seq[8]; } scene16_t;
typedef struct {
    scene16_t sc[2];
    uint8_t lfo_rate[6], lfo_depth[6];
    uint8_t morph_pos, density, swing, bpm, div, scale, root, poly, mode;
    int8_t oct, tr;
    uint8_t used, rv[4];                        /* rv: reverb send A, send B, size, tone (0 0 0 0: a preset from before the reverb) */
} preset16_t;
enum { MODE_ARP = 0, MODE_SEQ = 1 };
enum { INST_MIDI = 0, INST_ANALOG, INST_FM, INST_SAW, INST_PULSE, INST_EPIANO, INST_BELL, INST_PLUCK, INST_ORGAN, INST_BASS,
       INST_PAD, INST_BRASS, INST_PROPHET, INST_MOOG, INST_JUNO, INST_SYNC, INST_VIRUS, INST_SH101, INST_ACID, INST_STRINGS,
       INST_CS80, INST_DXBASS, INST_CLAV, INST_VIBES, INST_PWMPAD, INST_BREATH, INST_HOOVER, INST_WOBBLE, NINST };

/* what is not in a preset: the MIDI sync out, the clock, the look, the pans, the beginner text, what REC keeps */
static struct { uint8_t sync, chord, voic, amode, abars, ashp, csrc, cmode, meter, ccin, pcin, pana, panb, hint, recm, idle; } fxs =
    {2, 0, 0, 0, 2, 0, 0, 1, 1, 1, 1, 128, 128, 0, 0, 2};   /* csrc: the clock: 0 internal, 1 external, 2 auto */
/* sync: SYNC OUT 0 off, 1 start/stop, 2 + clock; chord, voic: the chord type and its voicing;
 * amode, abars, ashp: AUTO A-B (off / once / ping-pong / saw; 2^abars bars a trip; linear / ease); csrc: the clock (internal / external / auto);
 * cmode: the colours (0 SOFT, 1 RICH, 2 WILD); meter: the level meter on the performance screen; ccin, pcin: MIDI control change and program change in;
 * pana, panb: where each scene sits in the stereo field (128 = middle); hint: the beginner text (a note for each knob you touch);
 * recm: what REC captures (0 the notes, 1 the notes and the parameters' motion); idle: the screen that forms by itself after a while untouched (0 off, 1 30 s, 2 1 min, 3 3 min) */
static uint8_t leg_dly[3] = {2, 100, 255};     /* the delay of a v1.0 .. v1.6 record (time, feedback, tone): the presets of that time take it */
static const uint32_t DT_K[4] = {661500u, 1323000u, 1984500u, 2646000u};   /* 44100 * 60 * (1/4 1/2 3/4 1 beat): the delay in samples = K / bpm */

static preset_t cur;                            /* the state you play */
/* ---- the three models (v1.9). One runs at a time; each has its own state and its own presets. FORMATION's state is preset_t (above, as in v1.8); LANES and DRIFT have the same
 * sound part (core_t: the two voices, the tempo and scale, the drums) and their own data after it (a plain byte block here: the engines come with the next milestones, and
 * lay their lanes and dials over it, so that the saved layout does not change again). A saved slot is a model tag and the largest payload. */
enum { MODEL_NONE, MODEL_FORMATION, MODEL_LANES, MODEL_DRIFT };
typedef struct { uint8_t inst, timbre, dsend, level, env[4], dtime, dfb, dtone, rsend, rsize, rtone; } voice_t;           /* 14 B: a voice's sound and its delay and reverb */
typedef struct {
    voice_t v[2];
    uint8_t bpm, div, swing, scale, root, oct_, tr_, used, morph_pos;       /* the tempo (used holds its high bits, as in preset_t), the scale, the transpose */
    uint8_t dr_on, dr_level, dr_tone, dr_decay, dr_tune, dr_send;           /* the drum voice */
    uint8_t spare;
} core_t;                                       /* 44 B */
typedef struct { core_t c; uint8_t d[190]; } lanes_t;       /* 234 B */
typedef struct { core_t c; uint8_t d[100]; } drift_t;       /* 144 B */
#define PAY_B 234                               /* the largest payload of a saved slot */
typedef char chk_payload[(sizeof(preset_t) <= PAY_B && sizeof(lanes_t) <= PAY_B && sizeof(drift_t) <= PAY_B) ? 1 : -1];
typedef struct { uint8_t model; uint8_t data[PAY_B]; } pslot_t;     /* 235 B */
static lanes_t lanes_cur;
static drift_t drift_cur;

static struct { uint8_t ver, theme, ch[3], slot, model; } glob = {1, 0, {0, 1, 9}, 0, MODEL_FORMATION};      /* ch: MIDI out of A, of B, of the drums (channel 10) */

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
    s->env[0] = s->env[1] = s->env[2] = s->env[3] = 128;
    s->dtime = 2; s->dfb = 100; s->dtone = 255;
    s->rsend = 40; s->rsize = 170; s->rtone = 160;
}
/* the tempo: 1 .. 480 BPM. The low 8 bits are in bpm, the high 2 in used (bits 1 and 2; bit 0 is the slot's used flag), so a preset
 * saved when the tempo was 40 .. 240 in a byte reads as it did. */
#define BPM_MIN 1u
#define BPM_MAX 480u
static uint32_t bpm_get(const preset_t *p)
{
    uint32_t b = (uint32_t)p->bpm | (((uint32_t)p->used >> 1) & 3u) << 8;
    return b < BPM_MIN ? BPM_MIN : b > BPM_MAX ? BPM_MAX : b;
}
static void bpm_set(preset_t *p, int32_t v)
{
    uint32_t b = (uint32_t)clampi(v, (int32_t)BPM_MIN, (int32_t)BPM_MAX);
    p->bpm = (uint8_t)b;
    p->used = (uint8_t)((p->used & 1u) | ((b >> 8) << 1));
}

static void preset_init(preset_t *p)
{
    memset(p, 0, sizeof *p);
    scene_init(&p->sc[0], 0);
    scene_init(&p->sc[1], 1);
    p->density = 128; bpm_set(p, 80); p->div = 2; p->scale = 1; p->root = 0; p->mode = MODE_ARP;
    p->dr_on = 1; p->dr_level = 150; p->dr_tone = 110; p->dr_decay = 110; p->dr_tune = 128; p->dr_send = 40; p->dr_follow = 1;
    p->dr_shadow[0] = p->dr_shadow[1] = 140; p->dr_space[0] = p->dr_space[1] = 150;
}
static void core_init(core_t *c)                 /* the sound part of LANES and DRIFT: the same two voices and drums as a new FORMATION preset */
{
    preset_t p;
    uint32_t i;
    preset_init(&p);
    memset(c, 0, sizeof *c);
    for (i = 0; i < 2u; i++) {
        voice_t *v = &c->v[i];
        const scene_t *q = &p.sc[i];
        v->inst = q->inst; v->timbre = q->timbre; v->dsend = q->dsend; v->level = q->level;
        memcpy(v->env, q->env, 4);
        v->dtime = q->dtime; v->dfb = q->dfb; v->dtone = q->dtone; v->rsend = q->rsend; v->rsize = q->rsize; v->rtone = q->rtone;
    }
    c->bpm = p.bpm; c->div = p.div; c->swing = p.swing; c->scale = p.scale; c->root = p.root; c->oct_ = (uint8_t)p.oct; c->tr_ = (uint8_t)p.tr; c->used = p.used; c->morph_pos = p.morph_pos;
    c->dr_on = p.dr_on; c->dr_level = p.dr_level; c->dr_tone = p.dr_tone; c->dr_decay = p.dr_decay; c->dr_tune = p.dr_tune; c->dr_send = p.dr_send;
}
static void lanes_init(lanes_t *l) { memset(l, 0, sizeof *l); core_init(&l->c); }
static void drift_init(drift_t *d) { memset(d, 0, sizeof *d); core_init(&d->c); }
static void preset_from17(preset_t *d, const preset17_t *o)     /* a v1.7 preset: the same bytes, then the drums (off: it sounds as it did) */
{
    preset_init(d);
    memcpy(d, o, sizeof *o);
    d->dr_on = 0;
}
static void preset_from16(preset_t *d, const preset16_t *o)     /* a preset of v1.0 .. v1.6 in the new layout */
{
    uint32_t i;
    int legacy = !(o->rv[0] | o->rv[1] | o->rv[2] | o->rv[3]);   /* saved before the reverb existed: its defaults */
    preset_init(d);
    for (i = 0; i < 2u; i++) {
        scene_t *s = &d->sc[i];
        const scene16_t *q = &o->sc[i];
        memcpy(s->fader, q->fader, sizeof s->fader); memcpy(s->p, q->p, sizeof s->p); memcpy(s->seq, q->seq, sizeof s->seq);
        s->inst = q->inst; s->timbre = q->timbre; s->dsend = q->dsend; s->level = q->level;
        s->dtime = (uint8_t)(leg_dly[0] & 3u); s->dfb = leg_dly[1] > 230u ? 230u : leg_dly[1]; s->dtone = leg_dly[2];
        if (!legacy) { s->rsend = o->rv[i]; s->rsize = o->rv[2]; s->rtone = o->rv[3]; }
    }
    memcpy(d->lfo_rate, o->lfo_rate, sizeof d->lfo_rate); memcpy(d->lfo_depth, o->lfo_depth, sizeof d->lfo_depth);
    d->morph_pos = o->morph_pos; d->density = o->density; d->swing = o->swing; d->bpm = o->bpm; d->div = o->div; d->scale = o->scale;
    d->root = o->root; d->poly = o->poly; d->mode = o->mode; d->oct = o->oct; d->tr = o->tr; d->used = o->used;
    d->dr_on = 0;                                                               /* (a preset of that time sounds as it did) */
}

/* ================================================================== 2. the sound (audio ISR) */
/* Two scene voices of four notes each. A slot is written by the main loop (inc, inst, tim, lvl, then gate and serial
 * last) and rendered, with its envelope, by the ISR. Eleven instruments, all small: analog saw, 2-op FM, 3-saw, pulse,
 * electric piano, bell, pluck, organ, bass, pad, brass; each through a one-pole low-pass (the timbre), the plucked and
 * blown ones with the filter following the envelope. Each scene sends to a mono delay (time, feedback, tone) and to a
 * small stereo reverb: a four-line feedback network, two allpasses in front. */
#define NSL 8u
typedef struct {
    volatile uint32_t inc, serial;
    volatile int32_t lvl;
    volatile uint32_t eatk, edec, erel;         /* the envelope: attack, decay, release in samples (the main loop sets them: ENV of the scene, the instrument's own as the base) */
    volatile uint8_t gate, inst, tim, esus;     /* esus: the sustain level, 0..255 */
    uint32_t seen, ph, ph2, ph3, phx[4], age, rng;   /* ISR only from here */
    int32_t amp, lp, f2, rel;
    uint8_t st;                                 /* 0 idle, 1 attack, 2 decay / sustain, 3 release */
} slot_t;
static slot_t sl[NSL];
/* each voice (scene) has its own chain: the delay (send, time, feedback, tone), the reverb (send, size, bright), the pan; the main loop writes, the ISR reads */
static volatile int32_t gain_v[2], ds_v[2], dly_len_v[2] = {22000, 22000}, dly_fb_v[2], dly_k_v[2], rv_s[2], rv_g[2], rv_k[2], pan_l[2] = {255, 255}, pan_r[2] = {255, 255};
static volatile uint32_t meter_l, meter_r;      /* the loudest sample of each side since the screen last looked */
static volatile uint8_t busy;
static volatile uint32_t dsp_max;               /* the longest app_audio block since the last look, in 24 MHz ticks */
static int32_t bufa[HALF_FRAMES], bufb[HALF_FRAMES];
#define DLN 32768u                              /* a delay line: 0.74 s, one for each voice */
static int16_t dly[2][DLN] __attribute__((section(".pool")));
static uint32_t dly_w;
static int32_t dly_lp[2];

/* the envelope of each instrument, in samples at 44.1 kHz: attack, decay to the sustain, release; sustain in tenths */
static const struct { uint32_t atk, dec, rel; uint8_t sus; } ENV[NINST] = {
    {240, 13230, 6615, 7},   /* MIDI (not sounded) */
    {240, 13230, 6615, 7},   /* ANALOG */
    {240, 13230, 6615, 7},   /* FM */
    {240, 13230, 6615, 7},   /* SUPERSAW */
    {240, 13230, 6615, 7},   /* PULSE */
    {120, 44100, 8820, 3},   /* E PIANO: a second to sink to a third */
    {60, 88200, 44100, 0},   /* BELL: rings out over two seconds */
    {40, 20000, 4410, 0},    /* PLUCK: gone in half a second, the filter closing with it */
    {500, 1, 2205, 10},      /* ORGAN: on while the key is down */
    {200, 22050, 4410, 8},   /* BASS */
    {8820, 1, 22050, 10},    /* PAD: a fifth of a second in, half a second out */
    {2646, 13230, 4410, 9},  /* BRASS: swells in */
    {300, 20000, 8000, 6},   /* PROPHET 5 */
    {100, 9000, 4000, 3},    /* MINIMOOG: a bass that sweeps and sits */
    {600, 30000, 12000, 8},  /* JUNO PAD */
    {100, 25000, 6000, 5},   /* SYNC LEAD */
    {200, 15000, 8000, 8},   /* VIRUS SAW */
    {40, 12000, 3000, 2},    /* SH-101 */
    {60, 8000, 2500, 1},     /* ACID 303 */
    {9000, 1, 20000, 10},    /* STRINGS */
    {5000, 15000, 14000, 8}, /* CS-80 BRASS */
    {40, 15000, 3000, 3},    /* DX BASS */
    {30, 5000, 2500, 0},     /* CLAVINET */
    {40, 60000, 30000, 0},   /* VIBES */
    {4000, 1, 15000, 10},    /* PWM PAD */
    {3000, 20000, 8000, 8},  /* BREATH */
    {300, 25000, 6000, 7},   /* HOOVER */
    {120, 12000, 4000, 7}};  /* WOBBLE BASS */
/* the ENV page: each of the four values is a byte, 128 = what the instrument has, each step of 20 doubles or halves it (+- 6 octaves) */
static const uint16_t POW20[20] = {1024, 1060, 1097, 1135, 1175, 1217, 1260, 1304, 1350, 1397, 1446, 1497, 1550, 1604, 1660, 1719, 1779, 1842, 1906, 1973};   /* 1024 * 2^(k/20) */
static uint32_t env_scale(uint32_t def, uint32_t v, uint32_t lo, uint32_t hi)
{
    int32_t n = (int32_t)v - 128, o = n >= 0 ? n / 20 : -((-n + 19) / 20), k = n - o * 20;
    uint32_t x = (def * (uint32_t)POW20[k]) >> 10;
    if (o > 0) x = x > (hi >> o) ? hi : x << o;
    else if (o < 0) x >>= -o;
    return x < lo ? lo : x > hi ? hi : x;
}
#define ATK_MAX 88200u                          /* 2 s */
#define DEC_MAX 441000u                         /* 10 s */
#define REL_MAX 441000u
static uint32_t env_atk(uint32_t inst, uint32_t v) { return env_scale(ENV[inst % NINST].atk, v, 8u, ATK_MAX); }
static uint32_t env_dec(uint32_t inst, uint32_t v) { return env_scale(ENV[inst % NINST].dec, v, 1u, DEC_MAX); }
static uint32_t env_rel(uint32_t inst, uint32_t v) { return env_scale(ENV[inst % NINST].rel, v, 8u, REL_MAX); }
static uint32_t env_sus(uint32_t inst, uint32_t v) { return (uint32_t)clampi((int32_t)ENV[inst % NINST].sus * 255 / 10 + ((int32_t)v - 128) * 2, 0, 255); }
static const uint16_t IGAIN[NINST] = {256, 256, 256, 256, 256, 270, 210, 300, 290, 256, 330, 290,
                                      333, 460, 280, 256, 512, 560, 360, 512, 486, 256, 512, 205, 294, 400, 384, 282};   /* loudness of each, 256 = 1 (measured on the host: about the same level as ANALOG) */

/* the new instruments share one engine: oscillators (chosen in render_slot), a resonant two-pole low-pass (a state-variable filter) and the level
 * envelope: fc the cutoff (0..1023, quadratic into the filter's coefficient), fenv how far the level opens it, fq the resonance (0..255), kt how much
 * the pitch moves it, nof 1: no filter; the timbre knob always moves the cutoff */
typedef struct { uint16_t fc; uint8_t fenv, fq, kt, nof; } filt_t;
static const filt_t FILT[16] = {
    {420, 130, 60, 40, 0},      /* PROPHET 5 */
    {180, 220, 150, 30, 0},     /* MINIMOOG */
    {520, 60, 40, 30, 0},       /* JUNO PAD */
    {800, 40, 20, 20, 0},       /* SYNC LEAD */
    {700, 40, 30, 20, 0},       /* VIRUS SAW */
    {300, 180, 90, 30, 0},      /* SH-101 */
    {120, 230, 220, 30, 0},     /* ACID 303 */
    {450, 30, 10, 20, 0},       /* STRINGS */
    {250, 240, 70, 20, 0},      /* CS-80 BRASS */
    {900, 0, 0, 0, 1},          /* DX BASS */
    {250, 200, 70, 30, 0},      /* CLAVINET */
    {900, 0, 0, 0, 1},          /* VIBES */
    {500, 40, 20, 20, 0},       /* PWM PAD */
    {0, 0, 245, 0, 0},          /* BREATH: a resonance at the pitch */
    {600, 60, 90, 20, 0},       /* HOOVER */
    {260, 90, 130, 20, 0}};     /* WOBBLE BASS */
static volatile uint32_t wob_inc;                       /* the wobble bass's LFO: an eighth note, from the BPM (the main loop sets it) */

static void render_slot(slot_t *s, int32_t *buf, uint32_t n)
{
    uint32_t i, inc = s->inc, ph = s->ph, ph2 = s->ph2, ph3 = s->ph3, p4 = s->phx[0], p5 = s->phx[1], p6 = s->phx[2], p7 = s->phx[3], age = s->age, rng = s->rng, width, dt, dv;
    int32_t amp = s->amp, lp = s->lp, f2 = s->f2, lvlq = s->lvl << 16, susq, atk, dec, k, ix, cutb = 0, qv = 0;
    uint8_t st = s->st, inst = s->inst < NINST ? s->inst : 1;
    const filt_t *fp = inst >= INST_PROPHET ? &FILT[inst - INST_PROPHET] : &FILT[0];
    if (s->serial != s->seen) {                 /* a new note: attack from where the level is */
        s->seen = s->serial;
        st = 1;
        s->rel = 0;
        age = 0;
    }
    if (!s->gate && (st == 1 || st == 2)) {     /* released */
        st = 3;
        s->rel = amp / (s->erel ? (int32_t)s->erel : 1) + 1;
    }
    if (!st) return;
    susq = (lvlq >> 8) * (int32_t)s->esus;
    atk = lvlq / (s->eatk ? (int32_t)s->eatk : 1) + 1;
    dec = (lvlq - susq) / (s->edec ? (int32_t)s->edec : 1) + 1;
    k = 1500 + (int32_t)s->tim * 105;
    ix = ((int32_t)s->tim + 1) * 256;
    width = 644245094u + (uint32_t)s->tim * 11790106u;
    dt = inc >> 12;                                                         /* a detune of 1/4096 of the pitch */
    dv = (inc >> 1) / 8820u;                                                /* HOOVER: the dive, an octave's half over 200 ms */
    if (inst >= INST_PROPHET) {                                             /* the synth engine: where the filter starts */
        cutb = (int32_t)fp->fc + ((int32_t)s->tim << 1) + (((int32_t)fp->kt * (int32_t)(inc >> 21)) >> 5);
        qv = 12000 - (int32_t)fp->fq * 44;
    }
    for (i = 0; i < n; i++) {
        int32_t w, kk = k, a16;
        if (st == 1) { amp += atk; if (amp >= lvlq) { amp = lvlq; st = 2; } }
        else if (st == 2) {
            if (amp > susq) { amp -= dec; if (amp < susq) amp = susq; }
            if (amp <= 0) { amp = 0; st = 0; break; }                       /* a note that has sunk to nothing is over */
        }
        else { amp -= s->rel; if (amp <= 0) { amp = 0; st = 0; break; } }
        a16 = amp >> 16;                                                    /* 0..8000: the level now */
        if (age < 0x7FFFFFFFu) age++;
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
        case INST_EPIANO:                                                   /* FM 1:1, the index following the level */
            ph2 += inc;
            w = sine(ph + (uint32_t)(sine(ph2) * ((ix * a16) >> 13))) >> 1;
            break;
        case INST_BELL:                                                     /* FM 3.5:1 (a little off) and an octave partial */
            ph2 += (inc >> 1) * 7u + (inc >> 6);
            ph3 += inc << 1;
            w = (sine(ph + (uint32_t)(sine(ph2) * ((ix * a16) >> 13))) >> 1) + (sine(ph3) >> 3);
            break;
        case INST_PLUCK:                                                    /* a saw, the filter closing as it dies */
            w = (int32_t)(ph >> 17) - 16384;
            kk = 400 + ((k * a16) >> 13);
            break;
        case INST_ORGAN:                                                    /* three drawbars: 1, 2 and 4 */
            ph2 += inc << 1;
            ph3 += inc << 2;
            w = ((sine(ph) >> 1) + (sine(ph2) >> 2) + (sine(ph3) >> 3)) >> 1;
            break;
        case INST_BASS:                                                     /* a saw and a square an octave down */
            ph2 += inc >> 1;
            w = (((int32_t)(ph >> 17) - 16384 + (ph2 < 0x80000000u ? 7000 : -7000)) * 3) >> 2;
            kk = k >> 1;
            break;
        case INST_PAD:                                                      /* three saws, wide apart */
            ph2 += inc + (inc >> 7);
            ph3 += inc - (inc >> 7);
            w = (int32_t)(((ph >> 17) + (ph2 >> 17) + (ph3 >> 17)) / 3u) - 16384;
            break;
        case INST_BRASS:                                                    /* two saws, the filter opening with the level */
            ph2 += inc + (inc >> 9);
            w = (int32_t)(((ph >> 17) + (ph2 >> 17)) >> 1) - 16384;
            kk = 700 + ((k * a16) >> 13);
            break;
        case INST_PROPHET:                                                  /* two saws, a little apart */
            ph2 += inc + (inc >> 8);
            w = (((int32_t)(ph >> 17) - 16384) + ((int32_t)(ph2 >> 17) - 16384)) >> 1;
            break;
        case INST_MOOG:                                                     /* two saws and a square an octave down */
            ph2 += inc - (inc >> 9);
            ph3 += inc >> 1;
            w = ((((int32_t)(ph >> 17) - 16384) + ((int32_t)(ph2 >> 17) - 16384) + (ph3 < 0x80000000u ? 12000 : -12000)) * 3) >> 3;
            break;
        case INST_JUNO:                                                     /* a saw, a pulse whose width drifts, a sub */
            ph2 += inc + (inc >> 8);
            ph3 += inc >> 1;
            p4 += 58433u;
            w = (((int32_t)(ph >> 17) - 16384) >> 1) + ((ph2 < 0x80000000u + (uint32_t)(sine(p4) * 40000) ? 12000 : -12000) >> 1) + ((ph3 < 0x80000000u ? 12000 : -12000) >> 2);
            break;
        case INST_SYNC:                                                     /* a saw synced to another whose pitch falls with the level */
            if (ph < inc) ph2 = 0;
            ph2 += inc + (inc >> 8) * (uint32_t)((a16 * 3) >> 5);
            w = (int32_t)(ph2 >> 17) - 16384;
            break;
        case INST_VIRUS:                                                    /* seven saws spread out */
            ph2 += inc - dt * 51u; ph3 += inc - dt * 31u; p4 += inc - dt * 10u;
            p5 += inc + dt * 10u; p6 += inc + dt * 31u; p7 += inc + dt * 51u;
            w = (int32_t)(((ph >> 17) + (ph2 >> 17) + (ph3 >> 17) + (p4 >> 17) + (p5 >> 17) + (p6 >> 17) + (p7 >> 17)) / 7u) - 16384;
            break;
        case INST_SH101:                                                    /* a saw, a pulse, a square an octave down */
            ph3 += inc >> 1;
            w = (((int32_t)(ph >> 17) - 16384) >> 1) + ((ph < width ? 8000 : -8000) >> 1) + ((ph3 < 0x80000000u ? 8000 : -8000) >> 1);
            break;
        case INST_ACID:
            w = (int32_t)(ph >> 17) - 16384;
            break;
        case INST_STRINGS:                                                  /* five saws, wide */
            ph2 += inc + dt * 37u; ph3 += inc - dt * 37u; p4 += inc + dt * 18u; p5 += inc - dt * 18u;
            w = (int32_t)(((ph >> 17) + (ph2 >> 17) + (ph3 >> 17) + (p4 >> 17) + (p5 >> 17)) / 5u) - 16384;
            break;
        case INST_CS80:                                                     /* three saws, close */
            ph2 += inc + dt * 20u; ph3 += inc - dt * 20u;
            w = (int32_t)(((ph >> 17) + (ph2 >> 17) + (ph3 >> 17)) / 3u) - 16384;
            break;
        case INST_DXBASS:                                                   /* FM 2:1 with a sub */
            ph2 += inc << 1;
            ph3 += inc >> 1;
            w = (((sine(ph + (uint32_t)(sine(ph2) * ((ix * a16) >> 13))) >> 1) * 3) >> 2) + (sine(ph3) >> 3);
            break;
        case INST_CLAV:                                                     /* a narrow pulse */
            w = ph < 515396075u ? 12000 : -12000;
            break;
        case INST_VIBES:                                                    /* FM 4:1, a gentle index */
            ph2 += inc << 2;
            w = sine(ph + (uint32_t)(sine(ph2) * (((ix * a16) >> 13) >> 3))) >> 1;
            break;
        case INST_PWMPAD:                                                   /* two pulses whose widths move against each other */
            ph2 += inc + dt * 13u;
            p4 += 48000u;
            w = ((ph < 0x80000000u + (uint32_t)(sine(p4) * 30000) ? 12000 : -12000) + (ph2 < 0x80000000u - (uint32_t)(sine(p4) * 30000) ? 12000 : -12000)) >> 1;
            break;
        case INST_BREATH:                                                   /* noise through a resonance at the pitch */
            rng = rng * 1664525u + 1013904223u;
            w = (int32_t)(rng >> 17) - 16384;
            break;
        case INST_HOOVER:                                                   /* three saws, the pitch diving from above */
            { uint32_t ex = age < 8820u ? dv * (8820u - age) : 0u;
              ph += ex; ph2 += inc + ex + dt * 40u; ph3 += inc + ex - dt * 40u; }
            w = (int32_t)(((ph >> 17) + (ph2 >> 17) + (ph3 >> 17)) / 3u) - 16384;
            break;
        case INST_WOBBLE:                                                   /* two saws an octave apart, the filter wobbling at an eighth note */
            ph2 += inc >> 1;
            ph3 += wob_inc;
            w = (((int32_t)(ph >> 17) - 16384) + ((int32_t)(ph2 >> 17) - 16384)) >> 1;
            break;
        default:
            w = (int32_t)(ph >> 17) - 16384;
            break;
        }
        if (inst >= INST_PROPHET) {                                         /* the synth engine: through a two-pole low-pass (Chamberlin state variable) */
            int32_t out;
            if (fp->nof) out = w;
            else {
                int32_t cut = cutb + ((((int32_t)fp->fenv) * a16) >> 11), f, hi;
                if (inst == INST_WOBBLE) cut += (sine(ph3) * 380) >> 15;
                if (inst == INST_BREATH) f = (int32_t)(((inc >> 16) * 51472u) >> 16);   /* the pitch: 2 pi fc / fs in Q13 */
                else {
                    cut = clampi(cut, 0, 1023);
                    f = (cut * cut) >> 7;
                    if (f > 7600) f = 7600;
                }
                lp += (f * f2) >> 13;
                hi = w - lp - ((qv * f2) >> 13);
                f2 += (f * hi) >> 13;
                lp = clampi(lp, -65535, 65535);
                f2 = clampi(f2, -65535, 65535);
                out = inst == INST_BREATH ? f2 : lp;
            }
            buf[i] += (out * a16) >> 15;
        } else {
            lp += ((w - lp) * kk) >> 15;
            buf[i] += (lp * a16) >> 15;
        }
    }
    s->ph = ph; s->ph2 = ph2; s->ph3 = ph3; s->phx[0] = p4; s->phx[1] = p5; s->phx[2] = p6; s->phx[3] = p7; s->age = age; s->rng = rng;
    s->amp = amp; s->lp = lp; s->f2 = f2; s->st = st;
}

/* the reverb: two allpasses, then four delay lines (lengths about 25..40 ms, each damped by a one-pole low-pass)
 * fed back through a Householder matrix (I - 1/2 of all ones: it loses no energy, so the decay is the gain g and the
 * damping); the left output takes lines 0 and 2, the right 1 and 3 */
#define RVN 2048u
static int16_t rv_d[2][4][RVN] __attribute__((section(".pool")));      /* a reverb for each voice */
static int16_t ap_d[2][2][256] __attribute__((section(".pool")));
static const uint16_t RV_LEN[4] = {1171, 1307, 1523, 1789};
static const uint16_t AP_LEN[2] = {149, 239};
static uint32_t rv_w[2], ap_w[2], rv_run[2];
static int32_t rv_lp[2][4];

static void reverb(uint32_t vc, int32_t x, int32_t g, int32_t k, int32_t *l, int32_t *r)
{
    uint32_t w = rv_w[vc], aw = ap_w[vc], i;
    int32_t t[4], sm, d, u;
    d = ap_d[vc][0][(aw - AP_LEN[0]) & 255u];
    u = x - (d >> 1);
    ap_d[vc][0][aw & 255u] = (int16_t)clampi(u, -32767, 32767);
    x = d + (u >> 1);
    d = ap_d[vc][1][(aw - AP_LEN[1]) & 255u];
    u = x - (d >> 1);
    ap_d[vc][1][aw & 255u] = (int16_t)clampi(u, -32767, 32767);
    x = d + (u >> 1);
    ap_w[vc] = aw + 1u;
    for (i = 0; i < 4u; i++) {
        int32_t y = rv_d[vc][i][(w - RV_LEN[i]) & (RVN - 1u)];
        rv_lp[vc][i] += ((y - rv_lp[vc][i]) * k) >> 15;
        t[i] = rv_lp[vc][i];
    }
    sm = (t[0] + t[1] + t[2] + t[3]) >> 1;
    for (i = 0; i < 4u; i++) {
        int32_t f = (((t[i] - sm) >> 1) * (g >> 1)) >> 13;
        rv_d[vc][i][w & (RVN - 1u)] = (int16_t)clampi(x + f, -32767, 32767);
    }
    rv_w[vc] = w + 1u;
    *l = (t[0] + t[2]) >> 1;
    *r = (t[1] + t[3]) >> 1;
}

/* ---- the drum voice (v1.8): a gentle kit of six, synthesised: a tuned KICK and TOM (a sine with a short pitch drop), a soft BRUSH snare (band-passed noise and a quiet
 * body), a RIM click (two sines), a closed HAT and an OPEN hat (high-passed noise, soft). The main loop fires them (drum_hit); the ISR renders them, mixes them to the
 * middle and ducks them under the melody, so that they can never be louder than it. */
#define NDR 6
enum { DR_KICK, DR_TOM, DR_BRUSH, DR_RIM, DR_HAT, DR_OPEN };
static const uint8_t DR_GM[NDR] = {36, 45, 38, 37, 42, 46};                    /* the MIDI notes they send (General MIDI) */
static const char *const DR_NAME[NDR] = {"KICK", "TOM", "BRUSH", "RIM", "HAT", "OPEN"};
typedef struct {
    volatile uint32_t serial, inc, drop, lvl, ash, psh;                         /* the main loop: pitch step, pitch drop (the rim's second pitch), level, decay and pitch-drop shifts */
    uint32_t seen, ph, ph2, amp, penv;                                          /* the ISR: phases, the amplitude (Q24), the pitch envelope */
    int32_t lp, lp2;
} drm_t;
static drm_t drm[NDR];
static volatile int32_t dr_gain, dr_send;
static uint32_t dn_s = 0x2545F491u, dr_act;
static int32_t mel_env;                                                         /* the melody's level, followed (the drums duck under it) */
#define DK_SHIFT 4                                                              /* the follower, shifted down, is how far the drums duck (of 256) ... */
#define DK_MAX 200                                                              /* ... at most this far: they never go quite away */
#define DR_CAP 100                                                              /* the drum level knob at its top is this much of 256 (and the drums are rendered 8 times quieter first): a kit at full level is about 0.6 of one note of a voice */

static int32_t dr_noise(void)
{
    dn_s ^= dn_s << 13; dn_s ^= dn_s >> 17; dn_s ^= dn_s << 5;
    return (int32_t)(dn_s >> 16) - 32768;
}
static void render_drums(int32_t *out, uint32_t n)                              /* out must be zero; the drums are added, mono */
{
    uint32_t d, i, act = 0;
    for (d = 0; d < NDR; d++) {
        drm_t *s = &drm[d];
        uint32_t ser = s->serial;
        if (ser != s->seen) { s->seen = ser; s->amp = s->lvl; s->penv = 65535u; s->ph = s->ph2 = 0; s->lp = s->lp2 = 0; }
        if (s->amp < 4096u) continue;
        act = 1;
        for (i = 0; i < n; i++) {
            int32_t o, a = (int32_t)(s->amp >> 9), nz;
            switch (d) {
            case DR_KICK: case DR_TOM:
                s->ph += s->inc + ((s->drop >> 10) * (s->penv >> 6));          /* (drop x penv / 65536, without a 32-bit overflow) */
                s->penv -= s->penv >> s->psh;
                o = (sine(s->ph) * a) >> 15;
                break;
            case DR_BRUSH:
                nz = dr_noise();
                s->lp += (nz - s->lp) >> 2;
                s->lp2 += (s->lp - s->lp2) >> 4;
                s->ph += s->inc;
                o = ((((s->lp - s->lp2) << 1) + (sine(s->ph) >> 2)) * a) >> 15;
                break;
            case DR_RIM:
                s->ph += s->inc; s->ph2 += s->drop;
                o = (((sine(s->ph) + sine(s->ph2)) >> 1) * a) >> 15;
                break;
            default:                                                            /* HAT, OPEN */
                nz = dr_noise();
                s->lp += (nz - s->lp) >> s->psh;
                o = ((nz - s->lp) * a) >> 15;
                break;
            }
            s->amp -= s->amp >> s->ash;
            out[i] += o >> 3;
        }
    }
    dr_act = act;
}

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t v, i, any[2] = {0, 0}, t0 = fm1_ticks(), run[2], c;
    int32_t gv[2] = {gain_v[0], gain_v[1]}, dsv[2] = {ds_v[0], ds_v[1]}, len[2] = {dly_len_v[0], dly_len_v[1]}, fb[2] = {dly_fb_v[0], dly_fb_v[1]},
            dk[2] = {dly_k_v[0], dly_k_v[1]}, rs[2] = {rv_s[0], rv_s[1]}, rg[2] = {rv_g[0], rv_g[1]}, rk[2] = {rv_k[0], rv_k[1]},
            pl[2] = {pan_l[0], pan_l[1]}, pr[2] = {pan_r[0], pan_r[1]};
    uint32_t ml = 0, mr = 0;
    int32_t dgain = dr_gain, dsnd = dr_send, menv = mel_env;
    static int32_t bufd[HALF_FRAMES];
    memset(bufa, 0, sizeof bufa);
    memset(bufb, 0, sizeof bufb);
    memset(bufd, 0, sizeof bufd);
    render_drums(bufd, n);
    for (v = 0; v < NSL; v++) {
        render_slot(&sl[v], v < 4u ? bufa : bufb, n);
        any[v >> 2] |= sl[v].st;
    }
    busy = (uint8_t)((any[0] | any[1]) != 0u);
    for (c = 0; c < 2u; c++) {
        run[c] = rv_run[c];
        if ((rs[c] && any[c]) || (dr_act && dsnd)) run[c] = 700u;           /* the tail goes on for about four seconds */
        else if (run[c]) run[c]--;
        rv_run[c] = run[c];
    }
    for (i = 0; i < n; i++) {
        int32_t x[2], sl_ = 0, sr_ = 0, rl = 0, rr_ = 0, ol, or_, ax, dm, dg;
        x[0] = (bufa[i] * gv[0]) >> 7;
        x[1] = (bufb[i] * gv[1]) >> 7;
        ax = (x[0] < 0 ? -x[0] : x[0]) + (x[1] < 0 ? -x[1] : x[1]);
        if (ax > menv) menv += (ax - menv) >> 2; else menv -= menv >> 11;     /* the melody's level: quick up, about 50 ms down */
        dg = 256 - clampi(menv >> DK_SHIFT, 0, DK_MAX);
        dm = (((bufd[i] * dgain) >> 8) * dg) >> 8;                              /* the drums: their level, ducked under the melody */
        sl_ += dm; sr_ += dm;
        for (c = 0; c < 2u; c++) {
            int32_t dl, in, a, b;
            dl = dly[c][(dly_w - (uint32_t)len[c]) & (DLN - 1u)];
            dly_lp[c] += ((dl - dly_lp[c]) * dk[c]) >> 15;
            dl = dly_lp[c];
            in = ((x[c] * dsv[c]) >> 8) + ((dl * fb[c]) >> 8);
            dly[c][dly_w & (DLN - 1u)] = (int16_t)clampi(in, -32767, 32767);
            a = (x[c] * pl[c]) >> 8;                                        /* the voice where its PAN puts it, its delay with it (half as loud) */
            b = (x[c] * pr[c]) >> 8;
            sl_ += a + ((dl * pl[c]) >> 9);
            sr_ += b + ((dl * pr[c]) >> 9);
            if (run[c]) {
                int32_t ql, qr;
                reverb(c, ((x[c] * rs[c]) >> 8) + ((dm * dsnd) >> 9), rg[c], rk[c], &ql, &qr);
                rl += ql; rr_ += qr;
            }
        }
        dly_w++;
        ol = sl_ + rl; or_ = sr_ + rr_;
        out[2u * i] += ol;
        out[2u * i + 1u] += or_;
        if ((uint32_t)(ol < 0 ? -ol : ol) > ml) ml = (uint32_t)(ol < 0 ? -ol : ol);
        if ((uint32_t)(or_ < 0 ? -or_ : or_) > mr) mr = (uint32_t)(or_ < 0 ? -or_ : or_);
    }
    mel_env = menv;
    if (ml > meter_l) meter_l = ml;
    if (mr > meter_r) meter_r = mr;
    t0 = fm1_ticks() - t0;
    if (t0 > dsp_max) dsp_max = t0;
}
/* ================================================================== 3. MIDI and notes (main loop) */
static uint32_t slot_note[NSL], rr[2];
static uint32_t wa, wb;                         /* the scene weights now: 100 * cos / sin of the morph */

#define CHAIN_N 8
static uint32_t kill_until;                     /* PANIC: until then every note releases fast, the echoes and the reverb are shut */
static uint16_t pi_mask;                        /* the punch-in keys held now (bit k = lower key k), only while the FX pages are open */
static uint8_t pi_voice = 2;                    /* the voice they act on: 0 A, 1 B, 2 both (the voice the FX page shows) */

static void slot_env(uint32_t sc, slot_t *s)    /* a voice's envelope and timbre into a slot: at each note, and every pass (so a knob or a punch-in reaches the notes that sound) */
{
    const scene_t *q = &cur.sc[sc];
    uint32_t inst = q->inst % NINST, m = (pi_voice == 2u || pi_voice == sc) ? pi_mask : 0u;
    uint32_t ea = env_atk(inst, q->env[0]), ed = env_dec(inst, q->env[1]), er = env_rel(inst, q->env[3]);
    uint8_t tim = q->timbre;
    if (m & 0x1000u) er = er > REL_MAX >> 3 ? REL_MAX : er << 3;                     /* LONG: the release eight times longer */
    if (m & 0x2000u) { er >>= 3; if (er < 8u) er = 8u; }                              /* SHORT: an eighth of it */
    if (m & 0x4000u) ea = ea > ATK_MAX >> 4 ? ATK_MAX : ea << 4;                     /* SLOW: a swell, sixteen times the attack (new notes) */
    if ((int32_t)(kill_until - fm1_ms) > 0) er = 8u;
    if (m & 0x100u) tim = 0;                                                          /* CLOSE */
    if (m & 0x200u) tim = 255;                                                        /* OPEN */
    s->eatk = ea; s->edec = ed; s->erel = er; s->esus = (uint8_t)env_sus(inst, q->env[2]);
    s->tim = tim;
}

static void weights(void)
{
    uint32_t ph = (uint32_t)cur.morph_pos * 4210752u, sc, j, bpm = bpm_get(&cur);
    wb = (uint32_t)((sine(ph) * 100) >> 15);
    wa = (uint32_t)((cosine(ph) * 100) >> 15);
    if (wa > 100u) wa = 100u;
    if (wb > 100u) wb = 100u;
    for (sc = 0; sc < 2u; sc++) {                                                     /* each voice's own chain */
        const scene_t *q = &cur.sc[sc];
        uint32_t m = (pi_voice == 2u || pi_voice == sc) ? pi_mask : 0u, w = sc ? wb : wa;
        int32_t lvl = q->level, ds = q->dsend, fb = q->dfb, dt = q->dtime, dtn = q->dtone, rs = q->rsend, rz = q->rsize, rt = q->rtone, pn = sc ? fxs.panb : fxs.pana;
        if (m) {                                                                      /* the punch-ins: momentary, on the voice the FX page shows */
            if (m & 0x1u) ds = 255;                                                   /* THROW: all of it into the delay */
            if (m & 0x2u) { fb = 230; if (ds < 200) ds = 200; }                       /* REPEAT: the echoes hardly die */
            if (m & 0x4u) dt = 0;                                                     /* 1/16 */
            if (m & 0x8u) dt = 3;                                                     /* 1/4 */
            if (m & 0x10u) rs = 255;                                                  /* BLOOM */
            if (m & 0x20u) { if (rs < 220) rs = 220; rz = 255; }                      /* HALL */
            if (m & 0x40u) { rt = 0; if (rs < 180) rs = 180; }                        /* DARK */
            if (m & 0x80u) { rt = 255; if (rs < 180) rs = 180; }                      /* AIR */
            if (m & 0x400u) lvl >>= 2;                                                /* DUCK */
            if (m & 0x800u) lvl = 0;                                                  /* KILL */
            if (m & 0x8000u) pn = 255 - pn;                                           /* FLIP: the pan to the other side */
        }
        if ((int32_t)(kill_until - fm1_ms) > 0) { fb = 0; rs = 0; rz = 0; }               /* PANIC */
        gain_v[sc] = (int32_t)((w * ((uint32_t)lvl + 1u)) >> 8);
        ds_v[sc] = ds;
        dly_fb_v[sc] = fb > 230 ? 230 : fb;
        dly_k_v[sc] = 3000 + dtn * 113;
        dly_len_v[sc] = clampi((int32_t)(DT_K[dt & 3] / bpm), 1000, (int32_t)DLN - 8);
        rv_s[sc] = rs;
        rv_g[sc] = 19660 + rz * 42;                                                   /* the size: the feedback gain, 0.60 .. 0.93 */
        rv_k[sc] = 5000 + rt * 106;                                                   /* the tone: the damping low-pass, dark .. bright */
        pan_l[sc] = clampi(pn > 127 ? (255 - pn) * 2 : 255, 0, 255);                  /* a pan: the far side falls away, the near side stays full */
        pan_r[sc] = clampi(pn < 128 ? pn * 2 : 255, 0, 255);
        for (j = 0; j < 4u; j++) slot_env(sc, &sl[sc * 4u + j]);
    }
    dr_gain = (cur.dr_on && (int32_t)(kill_until - fm1_ms) <= 0) ? ((int32_t)cur.dr_level * DR_CAP) >> 8 : 0;     /* (PANIC shuts them too) */
    dr_send = cur.dr_send;
}

static void voice_on(uint32_t sc, uint32_t note)
{
    uint32_t idx = sc * 4u + (rr[sc]++ & 3u);
    slot_t *s = &sl[idx];
    if (cur.sc[sc].inst == INST_MIDI || cur.sc[sc].inst >= NINST || note > 127u) return;
    s->inc = INC_C9[note % 12u] >> (10u - note / 12u);
    s->inst = cur.sc[sc].inst;
    slot_env(sc, s);
    s->lvl = (8000 * (int32_t)IGAIN[cur.sc[sc].inst]) >> 8;
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
    if (sc < 2u) voice_off(sc, note);                                           /* (2 is the drums: nothing sounds on after the note) */
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

static uint32_t dr_hits[NDR], dr_last[NDR], dr_vmax;     /* (how many, the last pitch and the loudest hit: the tests look at them) */
static uint32_t step_ticks(void);
static void drum_hit(uint32_t lane, uint32_t vel, uint32_t note)   /* vel 1..127; note: the pitch of a KICK or TOM (MIDI), the others ignore it */
{
    drm_t *d = &drm[lane % NDR];
    uint32_t dec = cur.dr_decay, tone = cur.dr_tone, inc, i;
    int32_t tune = clampi((int32_t)cur.dr_tune - 128, -12, 12), nt;
    if (vel > 127u) vel = 127u;
    if (!vel) return;
    switch (lane) {
    case DR_KICK:
        nt = clampi((int32_t)note + tune, 24, 72);
        inc = INC_C9[nt % 12] >> (10u - (uint32_t)nt / 12u);
        d->inc = inc; d->drop = inc * 2u; d->psh = 8; d->ash = 10u + (dec >> 6);
        break;
    case DR_TOM:
        nt = clampi((int32_t)note + tune, 36, 84);
        inc = INC_C9[nt % 12] >> (10u - (uint32_t)nt / 12u);
        d->inc = inc; d->drop = inc / 2u; d->psh = 9; d->ash = 10u + (dec >> 6);
        break;
    case DR_BRUSH:
        d->inc = INC_C9[7] >> 6; d->ash = 10u + (dec >> 7);
        break;
    case DR_RIM:
        inc = INC_C9[7] >> 4; d->inc = inc; d->drop = inc * 27u / 10u; d->ash = 8;
        break;
    case DR_HAT:
        d->psh = tone > 170u ? 1u : tone > 85u ? 2u : 3u; d->ash = 8u + (dec >> 7);
        break;
    default:                                                                    /* OPEN */
        d->psh = tone > 170u ? 1u : tone > 85u ? 2u : 3u; d->ash = 11u + (dec >> 7);
        break;
    }
    d->lvl = vel * 131072u;
    d->serial++;
    dr_hits[lane % NDR]++; dr_last[lane % NDR] = note;
    if (vel > dr_vmax) dr_vmax = vel;
    midi_send(0x90u | glob.ch[2], DR_GM[lane % NDR], vel);
    for (i = 0; i < 32u; i++)
        if (!poff[i].on) {
            poff[i].on = 1; poff[i].note = DR_GM[lane % NDR]; poff[i].sc = 2;
            poff[i].due = fm1_ticks() + step_ticks() / 4u;
            break;
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
static uint32_t lfo_ph[6], next_t, last_t, step_hit, clk_next;
static uint8_t ds_sounded, ds_raw;              /* what the melody did on the step just decided (the drums follow it) */
static int32_t lfo_out[6], lfo_held[6];         /* each LFO's value now (-32767 .. 32767; 0 when it is off), and what RANDOM holds */
static int32_t auto_ph, auto_last = -1;       /* AUTO A-B: the position of the trip (0 = A .. 65535 = B), and the morph value it wrote last */
static int8_t auto_dir = 1;
static uint8_t auto_rest, auto_arming;       /* ONCE: it has arrived; armed (a new mode or PLAY): pick the way again */
static uint8_t trail[8];
static uint32_t lfo_rem;
static uint32_t hit_ticks;                      /* when a note last sounded */
static volatile uint32_t hit_n;                 /* how many notes have sounded: the globe flashes on each */

/* ---- REC: a capture of one measure. Armed, it starts at the next measure; for each of the eight steps it keeps whether a note sounded, and which (the chord's
 * root, before the octave and the transpose), and, with RECM on, where the parameters stood (the six, and the density). When the eight steps are in, the
 * notes go to the SEQ pattern of both scenes (a step that did not sound gets level 0, one that did gets 255), the mode becomes SEQ, and the motion, if any,
 * is played back by SEQ. The pattern as it was is kept so that a long press of REC can put it back. */
static uint8_t rec_state, rec_cnt, rec_gate, rec_done, rec_note[8], rec_mot[8][7], mot_on;      /* rec_state: 0 idle, 1 armed, 2 recording */
static struct { uint8_t fader[2][8], seq[2][8], mode, mot, ok; } rec_undo;
static void rec_finish(void)
{
    uint32_t i, k;
    memcpy(rec_undo.fader[0], cur.sc[0].fader, 8); memcpy(rec_undo.fader[1], cur.sc[1].fader, 8);
    memcpy(rec_undo.seq[0], cur.sc[0].seq, 8); memcpy(rec_undo.seq[1], cur.sc[1].seq, 8);
    rec_undo.mode = cur.mode; rec_undo.mot = mot_on; rec_undo.ok = 1;
    for (i = 0; i < 8u; i++)
        for (k = 0; k < 2u; k++) {
            if ((rec_gate >> i) & 1u) { cur.sc[k].seq[i] = rec_note[i]; cur.sc[k].fader[i] = 255; }
            else cur.sc[k].fader[i] = 0;
        }
    cur.mode = MODE_SEQ;
    mot_on = fxs.recm ? 1u : 0u;
    rec_state = 0;
    rec_done = 1;
}
static void rec_restore(void)
{
    if (!rec_undo.ok) return;
    memcpy(cur.sc[0].fader, rec_undo.fader[0], 8); memcpy(cur.sc[1].fader, rec_undo.fader[1], 8);
    memcpy(cur.sc[0].seq, rec_undo.seq[0], 8); memcpy(cur.sc[1].seq, rec_undo.seq[1], 8);
    cur.mode = rec_undo.mode; mot_on = rec_undo.mot; rec_undo.ok = 0;
}
static uint8_t effv(uint32_t k, uint32_t step)  /* a parameter now: what Freeze holds, or SEQ's recorded motion at that step, or the live value */
{
    if (frozen) return frz_e[k];
    if (mot_on && cur.mode == MODE_SEQ) return rec_mot[step & 7u][k];
    return eff[k];
}

/* ---- CHAIN (SELECT menus): up to eight steps, each a preset slot and a number of measures; while it is on and the sequencer plays, a new measure counts
 * and, when a step's bars are up, the next step's preset is recalled. Each step is a byte: bit 7 used, bits 4..6 the bars (0..4: 1, 2, 4, 8, 16), bits 0..3 the slot. */
static uint8_t chain[CHAIN_N], chain_on, chain_pos = 255, chain_left;
static void chain_measure(void);                /* (section 5) */
static void bank_open(void);
static void hud_num(const char *name, int32_t v, int sign);

static uint32_t step_ticks(void)                /* one step, in 24 MHz ticks (60e6 us / (bpm * 2^div) * 24) */
{
    uint32_t bpm = bpm_get(&cur);
    return (60000000u / (bpm << cur.div)) * 24u;
}

static uint32_t lfo_period(uint32_t beat_us, uint32_t r)   /* us a cycle */
{
    return r <= 4u ? beat_us >> (r - 1u) : beat_us << (r - 4u);
}
static int32_t lfo_wave(uint32_t sh, uint32_t ph, int32_t held)    /* the shape at a phase (a full cycle over 2^32): -32767 .. 32767 */
{
    int32_t t = (int32_t)(ph >> 16), v;
    switch (sh) {
    case 0: return sine(ph);
    case 1: v = t < 16384 ? 2 * t : t < 49152 ? 2 * (32768 - t) : 2 * (t - 65536); break;     /* triangle: up through zero at the start */
    case 2: v = t - 32768; break;                                                              /* saw: rising */
    case 3: v = 32767 - t; break;                                                              /* ramp: falling */
    case 4: v = t < 32768 ? 32767 : -32767; break;
    default: return held;
    }
    return clampi(v, -32767, 32767);
}
static void lfo_reset(void) { uint32_t p; for (p = 0; p < 6u; p++) { lfo_ph[p] = 0; lfo_held[p] = 0; } }   /* the LFOs start from their phase each time it plays */

static void update_eff(uint32_t dt)             /* dt: ticks since the last call */
{
    uint32_t p, mp = cur.morph_pos, beat_us = 60000000u / bpm_get(&cur), us;
    dt += lfo_rem;
    us = dt / 24u;
    lfo_rem = dt - us * 24u;
    for (p = 0; p < 6u; p++) {
        int32_t base = (int32_t)(((uint32_t)cur.sc[0].p[p] * (255u - mp) + (uint32_t)cur.sc[1].p[p] * mp + 127u) / 255u);
        uint32_t r = cur.lfo_rate[p], sh = cur.lfo_sp[p] & 7u;
        if (sh > 5u) sh = 0;
        lfo_out[p] = 0;
        if (r && r <= 8u) {
            uint32_t period = lfo_period(beat_us, r), old = lfo_ph[p];
            lfo_ph[p] += (4294967295u / (period ? period : 1u)) * us;
            if (sh == 5u && lfo_ph[p] < old) lfo_held[p] = (int32_t)(rnd32() >> 16) - 32768;      /* RANDOM: a new value each cycle */
            lfo_out[p] = lfo_wave(sh, lfo_ph[p] + ((uint32_t)(cur.lfo_sp[p] >> 3) << 27), lfo_held[p]);
            if (cur.lfo_depth[p]) base += (((lfo_out[p] * (int32_t)cur.lfo_depth[p]) >> 15) * 127) >> 8;
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

/* Chords are built on the note that plays, from the degrees of the scale, so they are the scale's own chords (a triad
 * from MAJOR is major or minor or diminished as the degree falls). The notes join in the order of CHORD: with POLY and
 * HARMONY up, 2 notes are the root and its fifth, 3 a triad, 4 a seventh (or a ninth, or stacked fourths). */
static const uint8_t NDEG[10] = {7, 7, 5, 5, 7, 7, 7, 7, 7, 7};                      /* notes in each scale before the octave */
static const uint8_t CHORD[4][4] = {{0, 4, 2, 6}, {0, 4, 3, 6}, {0, 3, 6, 9}, {0, 4, 2, 8}};   /* STACK (thirds), SUS, QUART (fourths), ADD9: degrees above the root */
static int32_t deg_semi(uint32_t sc, int32_t d)             /* the semitones above the root of scale degree d (negative: below) */
{
    int32_t n = NDEG[sc % 10u], o = d >= 0 ? d / n : -((-d + n - 1) / n);
    return o * 12 + (int32_t)SCALE[sc % 10u][d - o * n];
}
static int32_t semi_deg(uint32_t sc, int32_t semi)          /* the scale degree at or just below semi (semitones above the root) */
{
    int32_t n = NDEG[sc % 10u], o = semi >= 0 ? semi / 12 : -((-semi + 11) / 12), pc = semi - o * 12, i, b = 0;
    for (i = 0; i < n; i++) if ((int32_t)SCALE[sc % 10u][i] <= pc) b = i;
    return o * n + b;
}

static void chord_build(uint32_t raw, uint32_t n, uint8_t *pl)       /* the chord of n notes (1..4) on raw: pl[0] is raw */
{
    int32_t d0 = semi_deg(cur.scale, (int32_t)raw - (int32_t)cur.root);
    int open = fxs.voic == 2u || (fxs.voic == 0u && raw < 52u);                 /* OPEN: the third (and seventh) an octave up; AUTO: low down */
    uint32_t k;
    pl[0] = (uint8_t)raw;
    for (k = 1; k < n; k++) {
        int32_t pt = (int32_t)cur.root + deg_semi(cur.scale, d0 + (int32_t)CHORD[fxs.chord & 3u][k]);
        if (open && fxs.chord != 3u && n >= 3u && (k == 2u || k == 3u)) pt += 12;      /* (ADD9 stays as it is: lifting the third would put it next to the ninth) */
        pl[k] = (uint8_t)clampi(pt, 0, 127);
    }
}

static void do_step_inner(void)
{
    int32_t e = (int32_t)effv(3, cstep) - 127;
    uint32_t ls = cstep, p, mp = cur.morph_pos, rest, legato, harm, chaos, gate_t, raw, k, n, i;
    int32_t shift = 0;
    const uint8_t *pool;
    uint8_t pn, pl[4];
    if (e >= -13 && e <= 13) ls = (ls + 1u) & 7u;                                   /* entropy: the play direction */
    else if (e > 13 && e <= 64) { if (fwd) { ls++; if (ls >= 7u) { ls = 7u; fwd = 0; } } else { ls--; if (!ls || ls > 7u) { ls = 0; fwd = 1; } } }
    else if (e > 64) { uint32_t r = rnd8(); if (r < 179u) ls = (ls + 1u) & 7u; else if (r < 230u) ls = (ls + 7u) & 7u; }
    else if (e < -13 && e >= -64) ls = (ls + 7u) & 7u;
    else ls = rnd8() < 51u ? (ls + 2u) & 7u : (ls + 1u) & 7u;
    cstep = (uint8_t)ls;
    ds_sounded = 0;
    if (rec_state == 1u && ls == 0u) { rec_state = 2; rec_cnt = 0; rec_gate = 0; }  /* REC was armed: the capture starts with this measure */
    if (rec_state == 2u) {
        rec_cnt++;
        if (fxs.recm) { memcpy(rec_mot[ls], eff, 6); rec_mot[ls][6] = cur.density; }
    }
    if (ls == 0u && auto_roll && !frozen) roll();                                   /* auto re-roll: each measure */
    if (ls == 0u && chain_on) { chain_measure(); }                                  /* the chain counts the measure */
    if (ls == 0u && pend_slot >= 0) {                                               /* a recall waits for the end of the measure */
        recall_apply(pend_slot);
        pend_slot = -1;
    }
    /* the step's probability: the scenes' faders, the Morph smear, then the density */
    if (frozen) p = frz_p[ls];
    else {
        uint32_t a = ((uint32_t)cur.sc[0].fader[ls] * (255u - mp) + (uint32_t)cur.sc[1].fader[ls] * mp + 127u) / 255u;
        uint32_t b = ((uint32_t)cur.sc[0].fader[(ls + 1u) & 7u] * (255u - mp) + (uint32_t)cur.sc[1].fader[(ls + 1u) & 7u] * mp + 127u) / 255u;
        uint32_t m = effv(0, ls);
        p = (a * (255u - m) + b * m + 127u) / 255u;
    }
    { uint32_t dn = (mot_on && cur.mode == MODE_SEQ && !frozen) ? rec_mot[ls][6] : cur.density;
      if (dn < 128u) p = p * dn / 128u;
      else if (dn > 128u) p = p + ((255u - p) * (dn - 128u)) / 127u; }
    if (p > 255u) p = 255u;
    rest = effv(1, ls);
    legato = effv(2, ls);
    harm = effv(4, ls);
    chaos = effv(5, ls);
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
    n = cur.poly && harm > 63u ? (harm < 128u ? 2u : harm < 192u ? 3u : 4u) : 1u;   /* POLY: a chord on this note, in this scale */
    chord_build(raw, n, pl);
    if (chaos > 51u && rnd8() < chaos) shift = (rnd32() & 0x100u) ? 12 : -12;       /* chaos moves the whole chord an octave */
    for (k = 0; k < n; k++) {
        int32_t pitch = (int32_t)pl[k] + cur.oct * 12 + cur.tr + shift;
        pitch = clampi(pitch, 0, 127);
        for (i = 0; i < 2u; i++) note_on(i, (uint32_t)pitch, gate_t);
    }
    ds_sounded = 1; ds_raw = (uint8_t)raw;
    hit_ticks = fm1_ticks();
    hit_n++;                                    /* (the globe flashes on it) */
    step_hit = ls;
    trail[ls] = 255;
    if (rec_state == 2u) { rec_note[ls] = (uint8_t)raw; rec_gate |= (uint8_t)(1u << ls); }     /* REC keeps the note */
}
/* ---- the drums follow the melody. After each step they look at what it did: the pitch, the leap from the one before, whether it rested. SHADOW (per scene, blended
 * by the morph) is how much they answer its events (a soft brush on a falling leap, a tuned tom on a rising one, a rim in a gap just after a note, an open hat after
 * a big jump); SPACE is how much room they leave: the busier the melody, the fewer the hits, and with a lot of SPACE they play almost only in its gaps. The kick is the
 * anchor on the first and the middle of the measure, tuned to the key (the tonic; the fifth on the middle when the melody has left the tonic chord, with FOLLOW). */
static uint8_t dm_prev, dm_have, dm_act, dm_gap;
static uint32_t blend255(uint32_t a, uint32_t b, uint32_t m) { return (a * (255u - m) + b * m + 127u) / 255u; }
static int in_triad(uint32_t note)              /* the note is the tonic, third or fifth of the key (in any octave) */
{
    uint32_t pc = (note + 120u - (uint32_t)cur.root) % 12u;
    return pc == 0u || pc == 3u || pc == 4u || pc == 7u;
}
static uint32_t dvel(uint32_t base, uint32_t extra) { uint32_t v = base + (rnd8() & 15u) + extra; return v > 100u ? 100u : v; }
static void drum_step(void)
{
    uint32_t ls = cstep, shadow, space, room, interval = 0, p, up = 0;
    if (!cur.dr_on) { if (ds_sounded) { dm_prev = ds_raw; dm_have = 1; } return; }
    shadow = blend255(cur.dr_shadow[0], cur.dr_shadow[1], cur.morph_pos);
    space = blend255(cur.dr_space[0], cur.dr_space[1], cur.morph_pos);
    dm_act = (uint8_t)((dm_act * 3u + (ds_sounded ? 255u : 0u)) / 4u);          /* how busy the melody is */
    if (ds_sounded) {
        if (dm_have) { interval = ds_raw > dm_prev ? ds_raw - dm_prev : dm_prev - ds_raw; up = ds_raw > dm_prev; }
        dm_gap = 0;
    } else if (dm_gap < 255u) dm_gap++;
    room = 255u - ((uint32_t)dm_act * space >> 8);                               /* how much room the melody leaves them */
    if (ls == 0u || ls == 4u) {                                                  /* the kick: suppressed the least */
        p = (ls == 0u ? 215u : 120u) * (128u + room / 2u) / 255u;
        if (rnd8() < p) {
            uint32_t k = 36u + cur.root;
            if (ls == 4u && cur.dr_follow && dm_have && !in_triad(dm_prev)) k += 7u;
            drum_hit(DR_KICK, ls == 0u ? dvel(60, 14) : dvel(46, 10), k);
        }
    }
    if ((ls == 2u || ls == 6u) && rnd8() < 150u * room / 255u) drum_hit(DR_BRUSH, dvel(40, 8), 0);                          /* the soft backbeat */
    if (ds_sounded && interval >= 5u) {
        if (!up && rnd8() < (shadow * room / 255u) / 2u) drum_hit(DR_BRUSH, dvel(44, interval > 12u ? 24u : interval * 2u), 0);   /* a falling leap: a brush */
        if (up && rnd8() < (shadow * 3u / 4u) * (128u + room / 2u) / 255u)                                                  /* a rising leap: a tom, the note an octave down */
            drum_hit(DR_TOM, dvel(44, interval > 12u ? 24u : interval * 2u), (uint32_t)clampi((int32_t)ds_raw - 12, 36, 72));
    }
    if (!ds_sounded && dm_gap == 1u && rnd8() < (shadow * room / 255u) * 2u / 3u) drum_hit(DR_RIM, dvel(36, 0), 0);           /* the answer in the gap */
    if (!ds_sounded ? rnd8() < 150u * room / 255u : ((ls & 1u) && rnd8() < 70u * room / 255u)) drum_hit(DR_HAT, dvel(30, 0), 0);
    if ((!ds_sounded && ls == 7u && rnd8() < 110u * room / 255u) || (ds_sounded && interval >= 12u && rnd8() < shadow / 2u)) drum_hit(DR_OPEN, dvel(32, 0), 0);
    if (ds_sounded) { dm_prev = ds_raw; dm_have = 1; }
}
static void do_step(void)
{
    do_step_inner();
    drum_step();
    if (rec_state == 2u && rec_cnt >= 8u) rec_finish();                              /* eight steps are in */
}

/* AUTO A-B: the crossfader moves by itself, locked to the step clock: one trip from A to B takes 2^abars bars (4 beats each, whatever
 * the step division). ONCE goes to the far end and rests; PING goes to and fro; SAW goes A to B and starts again from A. A hand on the
 * crossfader, or a recalled preset, wins: the trip carries on from where it was put. It moves only while the sequencer runs. */
static void auto_arm(void) { auto_rest = 0; auto_last = -1; auto_arming = 1; }   /* the next step takes the position as it is and picks the way */
static void auto_step(void)
{
    uint32_t mode = fxs.amode, steps, inc, t;
    int32_t np;
    if (!mode || frozen) return;
    if ((int32_t)cur.morph_pos != auto_last) {                          /* a new position: carry on from there */
        auto_ph = (int32_t)(((uint32_t)cur.morph_pos << 16) / 255u);
        if (auto_ph > 65535) auto_ph = 65535;
        if (mode == 1u || auto_arming) auto_dir = auto_ph < 32768 ? 1 : -1;      /* ONCE, and a new start: towards the far end; a hand on PING keeps its way */
        auto_arming = 0;
        auto_rest = 0;
    }
    if (mode == 3u) auto_dir = 1;
    if (mode == 1u && auto_rest) { auto_last = (int32_t)cur.morph_pos; return; }
    steps = ((1u << fxs.abars) * 4u) << cur.div;                        /* steps in one trip: bars * 4 beats * steps a beat */
    inc = 65536u / steps;
    if (!inc) inc = 1;
    np = auto_ph + (int32_t)auto_dir * (int32_t)inc;
    if (np >= 65535) {
        if (mode == 3u) np -= 65535;
        else { np = 65535; if (mode == 1u) auto_rest = 1; else auto_dir = -1; }
    } else if (np <= 0) {
        np = 0;
        if (mode == 1u) auto_rest = 1; else auto_dir = 1;
    }
    auto_ph = np;
    t = (uint32_t)np;
    if (fxs.ashp) { uint32_t t2 = (t * t) >> 16; t = (t2 * (196608u - 2u * t)) >> 16; if (t > 65535u) t = 65535u; }   /* EASE: smoothstep */
    cur.morph_pos = (uint8_t)((t * 255u + 32768u) >> 16);
    auto_last = (int32_t)cur.morph_pos;
    weights();
}

/* ---- the external MIDI clock. With CLOCK at EXT (or AUTO, while a clock is coming in) the steps are driven by the 24-a-beat clock
 * that arrives on USB or Bluetooth, Start / Continue / Stop run the sequencer, and the BPM follows the clock (also what the delay and
 * the LFOs go by). The clock and transport bytes do not go to the ring midi_get reads: the interrupt that reads them (the app_midi_rt
 * hook) queues them here with the time they came, and ext_poll takes them in the main loop. While Formation follows, it sends no
 * clock or Start / Stop of its own. */
#ifndef RING_PUBLISH
#define RING_PUBLISH() ((void)0)                           /* (on the host and in the browser there is no interrupt to order against) */
#endif
static volatile uint32_t xc_t[32], xc_w, xc_r;
static volatile uint8_t xc_c[32];                       /* 0 a tick, 1 Start, 2 Continue, 3 Stop */
static uint32_t xc_last, xc_per, xc_wt0, xc_wn, xc_n, xc_stepno, xc_step_t;
static uint8_t xc_seen, xc_run, xc_pend, xc_follow, xc_cont;

static int app_midi_rt(uint32_t pkt)                    /* an interrupt (USB / Bluetooth input): 1 = taken, it does not go in the ring */
{
    uint32_t st = (pkt >> 8) & 0xFFu, c;
    if ((pkt & 15u) != 0xFu) return 0;
    if (st == 0xF8u) c = 0; else if (st == 0xFAu) c = 1; else if (st == 0xFBu) c = 2; else if (st == 0xFCu) c = 3; else return 0;
    if (xc_w - xc_r < 32u) {
        xc_t[xc_w & 31u] = fm1_ticks();
        xc_c[xc_w & 31u] = (uint8_t)c;
        RING_PUBLISH();
        xc_w++;
    }
    return 1;
}

static void transport(int on)
{
    if (on && !running) {
        running = 1;
        cstep = 7; fwd = 1;
        next_t = fm1_ticks();
        last_t = next_t;
        clk_next = next_t;
        auto_arm();                                         /* (ONCE makes a new trip each time it plays) */
        lfo_reset();
        if (xc_follow) { xc_run = 1; xc_pend = 0; if (!xc_cont) { xc_n = 0; xc_stepno = 0; } }   /* following: the steps come with the next ticks */
        if (fxs.sync && !xc_follow) midi_send(0xFAu, 0, 0);               /* MIDI Start */
    } else if (!on && running) {
        running = 0;
        offs_run(0, 1);
        voices_off();
        xc_run = 0; xc_pend = 0;
        if (fxs.sync && !xc_follow) midi_send(0xFCu, 0, 0);               /* MIDI Stop */
    }
}

static void ext_poll(uint32_t now)                      /* the main loop: the clock and transport that came in, who is the master, the tempo */
{
    uint32_t mode = fxs.csrc, tps = 24u >> (cur.div & 3u), to, follow;
    while (xc_r != xc_w) {
        uint32_t t = xc_t[xc_r & 31u], c = xc_c[xc_r & 31u];
        xc_r++;
        if (!mode) continue;                                                        /* INT: what comes in is ignored */
        if (c == 0) {                                                               /* a tick */
            to = xc_per * 4u > 7200000u ? xc_per * 4u : 7200000u;
            if (xc_seen && (int32_t)(t - xc_last) > (int32_t)to) xc_wn = 0;         /* it had gone: measure the tempo again */
            xc_last = t; xc_seen = 1;
            if (!xc_wn) xc_wt0 = t;                                                 /* the tempo: 24 ticks (a beat) apart, then low-passed */
            xc_wn++;
            if (xc_wn > 24u) {
                uint32_t np = (t - xc_wt0) / 24u;
                if (np >= 24000u && np < 24000000u) xc_per = xc_per ? (xc_per * 3u + np) / 4u : np;      /* a tick every 1 ms .. 1 s */
                xc_wt0 = t; xc_wn = 1;
            }
            if (xc_run && running) {
                if (xc_n % tps == 0) {                                              /* a step: on the beat grid, odd ones late by the swing */
                    uint32_t sl = xc_per * tps, sw = (sl >> 8) * (((uint32_t)cur.swing * 115u) >> 8);
                    if (xc_pend) { do_step(); auto_step(); }                        /* (the last one was still waiting: it goes now) */
                    xc_step_t = t + ((xc_stepno & 1u) ? sw : 0u);
                    xc_stepno++;
                    xc_pend = 1;
                }
                xc_n++;
            }
        } else {                                                                    /* Start, Continue, Stop: the clock is the master now */
            xc_follow = 1;
            xc_seen = 1; xc_last = t;
            if (c == 1u) {
                if (!running) transport(1);
                cstep = 7; fwd = 1; xc_n = 0; xc_stepno = 0; xc_pend = 0; xc_run = 1; xc_wn = 0; auto_arm();
            } else if (c == 2u) {
                if (!running) { uint8_t cs = cstep, fw = fwd; xc_cont = 1; transport(1); xc_cont = 0; cstep = cs; fwd = fw; }
                xc_run = 1;
            } else {
                xc_run = 0; xc_pend = 0;
                if (running) transport(0);
            }
        }
    }
    to = xc_per * 4u > 7200000u ? xc_per * 4u : 7200000u;                           /* a clock that has not ticked for 300 ms (or 4 ticks) is gone */
    follow = mode == 1u || (mode == 2u && xc_seen && (int32_t)(now - xc_last) < (int32_t)to);
    if (follow && !xc_follow) { xc_run = running; xc_n = 0; xc_stepno = 0; xc_pend = 0; }
    if (!follow && xc_follow) { xc_run = 0; xc_pend = 0; next_t = now; }            /* back on the internal clock, at the tempo it had */
    xc_follow = (uint8_t)follow;
    if (follow && xc_per) bpm_set(&cur, (int32_t)((60000000u + xc_per / 2u) / xc_per));   /* BPM = 60e6 / ticks a clock tick; the beat is 24 of them */
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
#define PRE_MAGIC 0x35564E50u                   /* "PNV5": v1.9, 16 slots of a model tag and 234 B after the 8-byte header */
#define PRE_MAGIC4 0x34564E50u                  /* "PNV4": v1.8, 16 presets of 117 B (the FORMATION preset as it is) */
#define PRE_MAGIC3 0x33564E50u                  /* "PNV3": v1.7, 16 presets of 106 B */
#define PRE_MAGIC2 0x32564E50u                  /* "PNV2": v1.0 .. v1.6, 16 presets of 80 B */
#define PRE16_BYTES (16u * sizeof(preset16_t))
#define PRE17_BYTES (16u * sizeof(preset17_t))
#define PRE18_BYTES (16u * sizeof(preset_t))
typedef struct { uint32_t magic, crc; pslot_t s[16]; } prebank_t;
static prebank_t bank;
static uint8_t bank_ok;

static int slot_used(uint32_t i) { return i < 16u && bank.s[i].model != MODEL_NONE; }
static void slot_put(uint32_t i, uint32_t model, const void *p, uint32_t n)       /* a slot: the model's tag, then its state */
{
    memset(&bank.s[i], 0, sizeof bank.s[i]);
    bank.s[i].model = (uint8_t)model;
    memcpy(bank.s[i].data, p, n);
}

static void bank_conv(int kind)                 /* bank holds an older bank (PNV2, PNV3 or PNV4): rewrite it in the new layout, last slot first (a new slot is longer, so it never lands on one not yet read) */
{
    const uint8_t *raw = (const uint8_t *)&bank;       /* kind: 0 v1.7, 1 v1.0-1.6, 2 v1.8 */
    int32_t i;
    for (i = 15; i >= 0; i--) {
        preset_t n;
        if (kind == 1) { preset16_t o; memcpy(&o, raw + 8u + (uint32_t)i * sizeof o, sizeof o); preset_from16(&n, &o); }
        else if (kind == 2) memcpy(&n, raw + 8u + (uint32_t)i * sizeof n, sizeof n);
        else { preset17_t o; memcpy(&o, raw + 8u + (uint32_t)i * sizeof o, sizeof o); preset_from17(&n, &o); }
        slot_put((uint32_t)i, (n.used & 1u) ? MODEL_FORMATION : MODEL_NONE, &n, sizeof n);
    }
    bank.magic = PRE_MAGIC;
}

static int bank_load(void)
{
#ifdef DINGHY_HOST
    if (!host_pre_ok) return -1;
    memcpy(&bank, host_pre, sizeof bank);
    if (bank.magic == PRE_MAGIC2) { bank_conv(1); return 0; }
    if (bank.magic == PRE_MAGIC3) { bank_conv(0); return 0; }
    if (bank.magic == PRE_MAGIC4) { bank_conv(2); return 0; }
    return bank.magic == PRE_MAGIC ? 0 : -1;
#else
    if (!flash_ok || st_read(PRESETS_AT, &bank, 8u)) return -1;
    if (bank.magic == PRE_MAGIC2 || bank.magic == PRE_MAGIC3 || bank.magic == PRE_MAGIC4) {          /* an older layout: read it, check it, convert it */
        uint32_t crc = bank.crc, kind = bank.magic == PRE_MAGIC2 ? 1u : bank.magic == PRE_MAGIC3 ? 0u : 2u,
                 n = kind == 1u ? PRE16_BYTES : kind == 0u ? PRE17_BYTES : PRE18_BYTES;
        if (st_read(PRESETS_AT, &bank, 8u + n) || crc32((const uint8_t *)&bank + 8u, n) != crc) return -1;
        bank_conv((int)kind);
        return 0;
    }
    if (bank.magic != PRE_MAGIC || st_read(PRESETS_AT, &bank, sizeof bank) || bank.magic != PRE_MAGIC ||
        crc32((const uint8_t *)bank.s, sizeof bank.s) != bank.crc)
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
    bank.crc = crc32((const uint8_t *)bank.s, sizeof bank.s);
    hdr[0] = bank.magic;
    hdr[1] = bank.crc;
    if ((rc = fl_erase4k(PRESETS_AT, &took)) != 0) return rc;
    for (i = 0; i < sizeof bank.s; i += 256u) {
        uint32_t k = sizeof bank.s - i < 256u ? (uint32_t)sizeof bank.s - i : 256u;
        if ((rc = fl_write(PRESETS_AT + 8u + i, (const uint8_t *)bank.s + i, k)) != 0) return rc;
    }
    return fl_write(PRESETS_AT, (const uint8_t *)hdr, sizeof hdr);   /* the header last: a cut save stays invalid */
#endif
}
static void bank_open(void)
{
    if (bank_ok) return;
    if (bank_load()) memset(&bank, 0, sizeof bank);
    bank_ok = 1;
}
static const uint8_t WHITE_KEY[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_KEY[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

/* ---- LED flashes: the LED of what an action was about blinks for 0.8 s, as the plugin flashes its buttons */
static uint32_t flash_until[41];
static uint8_t flash_ty[41];
static void flash(uint32_t id, uint32_t ty)     /* ty 1: a save (fast), 2: a recall (slow), 3: a copy, init or roll (fast, short) */
{
    if (id >= 41u) return;
    flash_until[id] = fm1_ms + 800u;
    flash_ty[id] = (uint8_t)ty;
}
static void led(uint32_t id, int on)            /* fm1_led_key, unless that LED is flashing */
{
    if (id < 41u && (int32_t)(flash_until[id] - fm1_ms) > 0) {
        uint32_t t = flash_until[id] - fm1_ms;
        on = flash_ty[id] == 2u ? (int)((t >> 7) & 1u) : flash_ty[id] == 3u ? (t > 400u && ((t >> 6) & 1u)) : (int)((t >> 6) & 1u);
    }
    fm1_led_key(id, on);
}

static void recall_apply(int32_t slot)          /* the engine calls it at the end of a measure */
{
    if (slot < 0 || slot > 15 || bank.s[slot].model != MODEL_FORMATION) return;               /* (a slot of another model: that model recalls it) */
    memcpy(&cur, bank.s[slot].data, sizeof cur);
    glob.slot = (uint8_t)slot;
    mot_on = 0;                                 /* (a capture's motion belongs to the pattern it came with) */
    flash(KEY_LED(WHITE_KEY[slot]), 2);
    flash(BTN_SAVE, 2);
}

static void chain_measure(void)                 /* a new measure while the chain is on: count it; when the step's bars are up, recall the next step's preset */
{
    uint32_t i, n, found = 0, code, slot;
    if (chain_left) chain_left--;
    if (chain_left) return;
    n = chain_pos == 255u ? 0u : ((uint32_t)chain_pos + 1u) % CHAIN_N;
    for (i = 0; i < CHAIN_N; i++, n = (n + 1u) % CHAIN_N)
        if (chain[n] & 0x80u) { found = 1; break; }
    if (!found) { chain_on = 0; return; }                                       /* nothing in it */
    chain_pos = (uint8_t)n;
    code = (chain[n] >> 4) & 7u;
    chain_left = (uint8_t)(1u << (code > 4u ? 4u : code));
    slot = chain[n] & 15u;
    bank_open();
    if (bank.s[slot].model == MODEL_FORMATION) { pend_slot = -1; recall_apply((int32_t)slot); }
    hud_num("CHAIN SLOT", (int32_t)slot + 1, 0);
}

/* ================================================================== 6. the screen */
/* a theme: the ramp of the chrome (dark to bright), the two scenes' colours, a colour for each group of pages (HOME, FX,
 * LFO, GLO), and the towers (low to high). The first three keep their places in the saved settings. */
enum { G_HOME, G_FX, G_LFO, G_GLO, G_ENV, G_DRUM };                  /* the groups of pages: each has a colour in the theme */
#define NTHEME 6u
typedef struct { const char *name; uint16_t c[5]; uint16_t a, b, g[4], t0, t1; } theme_t;
static const theme_t THEME[NTHEME] = {
    {"NAVY", {RGB(0, 36, 46), RGB(0, 84, 100), RGB(16, 140, 150), RGB(56, 200, 205), RGB(130, 255, 240)},
     RGB(56, 200, 205), RGB(255, 170, 50), {RGB(56, 200, 205), RGB(150, 120, 255), RGB(255, 170, 50), RGB(90, 220, 120)}, RGB(20, 90, 130), RGB(90, 240, 230)},
    {"MONO", {RGB(40, 40, 44), RGB(84, 84, 90), RGB(136, 136, 144), RGB(190, 190, 198), RGB(236, 236, 244)},
     RGB(190, 190, 198), RGB(0, 220, 255), {RGB(190, 190, 198), RGB(0, 220, 255), RGB(0, 220, 255), RGB(190, 190, 198)}, RGB(60, 60, 66), RGB(236, 236, 244)},
    {"MATRIX", {RGB(0, 36, 8), RGB(0, 90, 22), RGB(10, 150, 40), RGB(50, 214, 80), RGB(150, 255, 160)},
     RGB(50, 214, 80), RGB(255, 0, 170), {RGB(50, 214, 80), RGB(0, 220, 255), RGB(255, 0, 170), RGB(230, 255, 60)}, RGB(0, 90, 22), RGB(150, 255, 160)},
    {"SUNSET", {RGB(46, 12, 24), RGB(110, 30, 50), RGB(190, 70, 60), RGB(240, 130, 70), RGB(255, 220, 160)},
     RGB(255, 120, 90), RGB(255, 210, 80), {RGB(255, 120, 90), RGB(255, 90, 170), RGB(255, 210, 80), RGB(255, 160, 60)}, RGB(120, 30, 90), RGB(255, 200, 90)},
    {"AURORA", {RGB(8, 28, 36), RGB(16, 80, 90), RGB(30, 150, 130), RGB(90, 220, 150), RGB(200, 255, 210)},
     RGB(90, 230, 160), RGB(170, 120, 255), {RGB(90, 230, 160), RGB(170, 120, 255), RGB(255, 120, 200), RGB(80, 200, 255)}, RGB(30, 90, 160), RGB(120, 255, 170)},
    {"NEON", {RGB(20, 10, 40), RGB(60, 30, 110), RGB(120, 60, 190), RGB(200, 100, 255), RGB(245, 200, 255)},
     RGB(255, 60, 170), RGB(60, 230, 255), {RGB(255, 60, 170), RGB(60, 230, 255), RGB(255, 230, 60), RGB(170, 110, 255)}, RGB(90, 20, 160), RGB(255, 90, 190)}};
static theme_t tcur;                            /* the theme in use: a copy of THEME[] made brighter and more saturated (theme_set) */
#define TH tcur
#define THN (THEME[glob.theme % NTHEME].name)
static const uint8_t THEME_SAT[NTHEME] = {220, 0, 0, 215, 220, 240};      /* how saturated the instrument colours are in each theme (0: this theme keeps its own two scene colours) */
/* a colour for each instrument: its hue on the wheel (0 red, 43 yellow, 85 green, 128 cyan, 170 blue, 213 magenta) */
static const uint8_t INST_HUE[28] = {0, 32, 125, 205, 62, 5, 140, 70, 250, 175, 190, 36, 20, 15, 150, 225, 235, 95, 50, 160, 44, 110, 85, 118, 200, 100, 240, 180};
/* fxs.cmode: 0 SOFT (one hue: the theme's ramp, the scenes by brightness), 1 RICH (the scenes, the groups and the towers in the theme's colours),
 * 2 WILD (RICH, and each tower, each ring of the globe and each flash in its own colour) */
static uint16_t col_a, col_b, col_mix;          /* RICH and WILD: each scene's colour is its instrument's; col_mix is where the morph is between them */
static uint8_t col_key[4] = {255, 255, 255, 255};
#define C_A (fxs.cmode == 0u ? pal[3] : col_a)
#define C_B (fxs.cmode == 0u ? pal[4] : col_b)
#define GCOL0(grp) (TH.g[(grp) == G_ENV ? G_FX : (grp) == G_DRUM ? G_LFO : (grp) & 3u])
#define GCOL(grp) (fxs.cmode == 0u ? pal[3] : mixc(GCOL0(grp), col_mix, 70))        /* a group's colour, a little towards the morph's */

static uint16_t vivid(uint16_t c)               /* brighter and more saturated */
{
    int32_t r = (int32_t)(c >> 11) * 255 / 31, g = (int32_t)((c >> 5) & 63u) * 255 / 63, b = (int32_t)(c & 31u) * 255 / 31, y = (r + g + b) / 3;
    r = (y + (r - y) * 3 / 2) * 9 / 8; g = (y + (g - y) * 3 / 2) * 9 / 8; b = (y + (b - y) * 3 / 2) * 9 / 8;
    return RGB(clampi(r, 0, 255), clampi(g, 0, 255), clampi(b, 0, 255));
}
static uint16_t hsv565(uint32_t h, uint32_t s, uint32_t v)      /* h 0..255 round the wheel */
{
    uint32_t reg = (h & 255u) / 43u, rem = ((h & 255u) - reg * 43u) * 6u, p = (v * (255u - s)) >> 8, q = (v * (255u - ((s * rem) >> 8))) >> 8,
             t = (v * (255u - ((s * (255u - rem)) >> 8))) >> 8, r, g, b;
    switch (reg) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return RGB(r, g, b);
}
static void theme_set(uint32_t t)
{
    uint32_t k;
    glob.theme = (uint8_t)(t % NTHEME);
    tcur = THEME[glob.theme];
    for (k = 0; k < 5u; k++) tcur.c[k] = vivid(tcur.c[k]);
    tcur.a = vivid(tcur.a); tcur.b = vivid(tcur.b); tcur.t0 = vivid(tcur.t0); tcur.t1 = vivid(tcur.t1);
    for (k = 0; k < 4u; k++) tcur.g[k] = vivid(tcur.g[k]);
    for (k = 0; k < 5u; k++) pal[k] = tcur.c[k];
    col_key[3] = 255;                                                           /* (the scene colours are made again) */
}

static uint16_t mixc(uint16_t c0, uint16_t c1, uint32_t t)      /* t 0..255: c0 -> c1 */
{
    uint32_t r0 = c0 >> 11, g0 = (c0 >> 5) & 63u, b0 = c0 & 31u, r1 = c1 >> 11, g1 = (c1 >> 5) & 63u, b1 = c1 & 31u, u = 255u - t;
    return (uint16_t)((((r0 * u + r1 * t) / 255u) << 11) | (((g0 * u + g1 * t) / 255u) << 5) | ((b0 * u + b1 * t) / 255u));
}
static uint16_t dimc(uint16_t c, uint32_t t) { return mixc(C_BLACK, c, t); }       /* t 255: as it is, 0: black */
static void colours_update(void)                /* the scene colours: each its instrument's (where the theme has that), and the morph's between them */
{
    uint8_t k[4];
    k[0] = cur.sc[0].inst; k[1] = cur.sc[1].inst; k[2] = glob.theme; k[3] = fxs.cmode;
    if (memcmp(k, col_key, 4) != 0) {
        uint32_t sat = THEME_SAT[glob.theme % NTHEME], ia = k[0], ib = k[1], ha = INST_HUE[ia % 28u], hb = INST_HUE[ib % 28u];
        memcpy(col_key, k, 4);
        col_a = TH.a; col_b = TH.b;
        if (sat) {
            uint32_t d = (hb - ha) & 255u;
            if (ia && ia < NINST) col_a = hsv565(ha, sat, 255);
            if (ib && ib < NINST) {
                if (ia && ia < NINST && (d < 24u || d > 232u)) hb += 56u;           /* two alike: B is moved round the wheel, so that A and B can be told apart */
                col_b = hsv565(hb, sat, 255);
            }
        }
    }
    col_mix = mixc(col_a, col_b, cur.morph_pos);
}
static uint16_t hue6(uint32_t i)                /* six colours of the theme, one after the other (for WILD) */
{
    uint32_t k = i % 6u;
    return k < 4u ? TH.g[k] : k == 4u ? TH.a : TH.b;
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

/* a message that does not take over the screen: for a moment it replaces the footer's first line ("PLAY RUNNING",
 * "A-B 50%"), in the colour of the scene it is about. The values of the knobs are on the page halves instead. */
static char hud_name[16], hud_val[16];
static uint32_t dsp_pct;                        /* the audio code's load, in % (every half second) */
static uint32_t hud_until, ov_btn;
static uint8_t hud_scn;                         /* 0 none, 1 scene A, 2 scene B */
static void hud(const char *name, const char *val, int32_t bar)
{
    char *p = p_str(hud_name, name); *p = 0;
    p = p_str(hud_val, val); *p = 0;
    (void)bar;
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

/* ---- the globe, after Navy Arp 2: a mesh of 62 nodes (two poles and five rings of 12), rotating, breathing with the LFOs. The nodes of
 * each ring glow with one of the six LFOs. Four coloured triangles hop from node to node while it plays, flicker, and flare on every note. */
#define GN 62u
static int16_t gx[GN], gy[GN], gz[GN];
static uint32_t gyaw, hit_seen;
static uint32_t rip_t0[4];                      /* the ripples of the notes on the globe: when each began, and in whose colour */
static uint16_t rip_col[4];
static uint8_t idle_on, boot_done;               /* the formation screen is up; the boot logo has played */
static uint32_t touch_ms;                       /* the last touch of anything */
static struct { uint8_t v1, col, on; uint32_t t0, per, flare; } gtri[4];

static void globe_init(void)
{
    uint32_t r, i, n = 1;
    gx[0] = 0; gy[0] = 32767; gz[0] = 0;
    for (r = 1; r <= 5u; r++) {
        uint32_t pp = r * 357913941u;                                   /* the pitch: r / 12 of a cycle (a half turn is 6 / 12) */
        int32_t sp = sine(pp), cp = cosine(pp);
        for (i = 0; i < 12u; i++, n++) {
            uint32_t yw = i * 357913941u;
            gx[n] = (int16_t)((sp * cosine(yw)) >> 15);
            gy[n] = (int16_t)cp;
            gz[n] = (int16_t)((sp * sine(yw)) >> 15);
        }
    }
    gx[61] = 0; gy[61] = -32767; gz[61] = 0;
    for (i = 0; i < 4u; i++) { gtri[i].per = 533u + i * 300u; gtri[i].col = (uint8_t)i; gtri[i].on = (uint8_t)(i & 1u); gtri[i].v1 = (uint8_t)(i * 13u); }
}

static void cv_tri(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t c)      /* a filled triangle */
{
    int32_t ymin = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2), ymax = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2), y;
    int32_t X[6] = {x0, x1, x2, x0, 0, 0}, Y[6] = {y0, y1, y2, y0, 0, 0};
    for (y = ymin; y <= ymax; y++) {
        int32_t xa = 1 << 20, xb = -(1 << 20), e;
        for (e = 0; e < 3; e++) {
            int32_t ya = Y[e], yb = Y[e + 1], xa_ = X[e], xb_ = X[e + 1];
            if (ya == yb) { if (y == ya) { if (xa_ < xa) xa = xa_; if (xb_ < xa) xa = xb_; if (xa_ > xb) xb = xa_; if (xb_ > xb) xb = xb_; } continue; }
            if ((y < ya && y < yb) || (y > ya && y > yb)) continue;
            { int32_t x = xa_ + (y - ya) * (xb_ - xa_) / (yb - ya); if (x < xa) xa = x; if (x > xb) xb = x; }
        }
        if (xb >= xa) cv_rect(xa, y, xb - xa + 1, 1, c);
    }
}

static void draw_globe(int32_t cx, int32_t cy)
{
    uint32_t r, i, lfo_on = 0, dsum = 0, hit, now = fm1_ms;
    int32_t radius = 29, yc = (int32_t)cosine(gyaw), ys = (int32_t)sine(gyaw), ct = 30791, st = 11207;
    int32_t px[GN], py[GN], pz[GN];
    uint16_t rc[6];
    for (r = 0; r < 6u; r++) if (cur.lfo_rate[r] && cur.lfo_depth[r]) { lfo_on = 1; dsum += cur.lfo_depth[r]; }
    if (lfo_on) radius += (((lfo_out[0] ? lfo_out[0] : lfo_out[3]) >> 12) * (int32_t)(dsum > 255u ? 255u : dsum)) >> 11;
    for (r = 0; r < 6u; r++) rc[r] = fxs.cmode == 0u ? pal[3] : mixc(hue6(r), col_mix, 90);
    for (i = 0; i < GN; i++) {                                                          /* rotate, then a little perspective */
        int32_t x = gx[i], y = gy[i], z = gz[i], x1, z1, y2, z2, den, sc;
        x1 = (x * yc + z * ys) >> 15;
        z1 = (-x * ys + z * yc) >> 15;
        y2 = (y * ct - z1 * st) >> 15;
        z2 = (y * st + z1 * ct) >> 15;
        den = 32768 + ((z2 * 14894) >> 15);                                             /* 1 + z / 2.2 */
        sc = (int32_t)(1073741824u / (uint32_t)den);                                    /* 1 / that, in Q15 */
        px[i] = cx + ((((x1 * radius) >> 4) * (sc >> 4)) >> 22);
        py[i] = cy - ((((y2 * radius) >> 4) * (sc >> 4)) >> 22);
        pz[i] = z2;
    }
    hit = hit_n;                                                                        /* a note since the last drawing: a triangle flares and hops */
    if (hit != hit_seen) {
        uint32_t k = hit & 3u;
        hit_seen = hit;
        gtri[k].v1 = (uint8_t)(rnd32() % GN); gtri[k].on = 1; gtri[k].flare = now; gtri[k].t0 = now;
        gtri[k].col = (uint8_t)(rnd32() & 3u);
        rip_t0[k] = now; rip_col[k] = cur.morph_pos > 127u ? C_B : C_A;                   /* a ripple goes out, in the colour of the scene that is louder */
    }
    {   /* the globe swells with each note: up to 3 px, in a quarter of a second */
        uint32_t best = 1000u;
        for (i = 0; i < 4u; i++) { uint32_t fl = now - gtri[i].flare; if (fl < best) best = fl; }
        if (running && best < 250u) radius += (int32_t)((250u - best) * 3u / 250u);
    }
    for (i = 0; i < 4u; i++) {
        uint32_t fl = now - gtri[i].flare, br;
        if (running && now - gtri[i].t0 >= gtri[i].per) {                               /* each hops by itself, at its own pace */
            static const uint16_t PER[4] = {533, 1200, 1600, 8000};
            gtri[i].t0 = now; gtri[i].v1 = (uint8_t)(rnd32() % GN); gtri[i].per = PER[rnd32() & 3u];
            gtri[i].col = (uint8_t)(rnd32() & 3u); gtri[i].on = (uint8_t)(rnd32() % 100u < 55u);
        }
        if (!gtri[i].on || (!running && fl > 500u)) continue;
        br = 70u + (uint32_t)((sine(now * (4000u / (gtri[i].per / 100u + 1u) + 900u)) + 32768) >> 10);   /* a flicker: 70 .. 134 */
        if (fl < 400u) br += (400u - fl) * 120u / 400u;                                 /* the flare of a note */
        if (br > 255u) br = 255u;
        { uint32_t a = gtri[i].v1, b = (a + 1u) % GN, c = (a + 12u) % GN;
          cv_tri(px[a], py[a], px[b], py[b], px[c], py[c], dimc(fxs.cmode == 0u ? pal[3] : (fxs.cmode == 2u ? hue6(gtri[i].col) : TH.g[gtri[i].col]), br)); }
    }
    {   /* the edges: from the poles and between the rings, a ring's own colour, dimmer behind */
        uint32_t k, ring;
        #define EDGE(a, b, rg) do { int32_t zz = (pz[a] + pz[b]) >> 1; uint16_t c_ = rc[(rg) % 6u]; \
            cv_line(px[a], py[a], px[b], py[b], dimc(c_, zz > 9000 ? 200u : zz > -9000 ? 130u : 60u)); } while (0)
        for (k = 1; k <= 12u; k++) EDGE(0, k, 0u);
        for (ring = 1; ring <= 4u; ring++) {
            uint32_t o1 = 1u + (ring - 1u) * 12u, o2 = o1 + 12u;
            for (k = 0; k < 12u; k++) {
                EDGE(o1 + k, o1 + (k + 1u) % 12u, ring);
                EDGE(o1 + k, o2 + k, ring);
            }
        }
        for (k = 0; k < 12u; k++) { EDGE(49u + k, 49u + (k + 1u) % 12u, 5u); EDGE(49u + k, 61u, 5u); }
        #undef EDGE
    }
    for (i = 0; i < GN; i++) {                                                          /* the nodes: bright when their LFO is high, dim behind */
        uint32_t ringi = i == 0u || i == 61u ? 0u : 1u + (i - 1u) / 12u, al = 90u;
        if (cur.lfo_rate[ringi] && cur.lfo_depth[ringi] > 4u) {
            int32_t m = (lfo_out[ringi] * (int32_t)cur.lfo_depth[ringi]) >> 8;     /* -32640 .. 32640 */
            al = 90u + (uint32_t)((m + 32768) >> 8) * 153u / 256u;
        }
        if (pz[i] < -6000) al >>= 1;
        { uint16_t nc = fxs.cmode == 0u ? pal[4] : mixc(rc[ringi % 6u], pal[4], 140u);
          cv_rect(px[i] - 1, py[i] - 1, 2, 2, dimc(nc, al > 255u ? 255u : al)); }
    }
    for (i = 0; i < 4u; i++) {                                                          /* the ripples: a ring that grows from the middle and fades */
        uint32_t age = now - rip_t0[i], a;
        if (!rip_t0[i] || age > 700u) continue;
        for (a = 0; a < 40u; a++) {
            uint32_t ph = a * 107374182u;                                               /* 2^32 / 40 */
            int32_t rr_ = 6 + (int32_t)(age * 28u / 700u);
            cv_rect(cx + ((cosine(ph) * rr_) >> 15), cy + ((sine(ph) * rr_) >> 15), 2, 2, dimc(rip_col[i], 255u - age * 255u / 700u));
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
        uint16_t lo = fxs.cmode == 0u ? pal[1] : dimc(col_mix, 80), hi = fxs.cmode == 0u ? pal[3] : fxs.cmode == 2u ? hue6(i) : mixc(col_mix, C_WHITE, 70);     /* the tower's colours: low level .. high level (RICH: the morph's colour) */
        uint16_t col = (mute_mask >> i) & 1u ? pal[1] : (running && cstep == i) ? pal[4] : mixc(lo, hi, pv[i]);
        if (th > h) cv_rect(x, 60 - th, 24, th - h, dimc(mixc(lo, hi, trail[i]), 90));   /* the phosphor trail above */
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
    p = p_num(p, bpm_get(&cur));
    p = p_str(p, " BPM");
    if (latch) p = p_str(p, " LATCH");
    if (cur.poly) p = p_str(p, " POLY");
    if (frozen) p = p_str(p, " FRZ");
    if (rec_state) p = p_str(p, rec_state == 1u ? " ARM" : " REC");
    if (chain_on) p = p_str(p, " CHN");
    if (cur.dr_on) p = p_str(p, " DR");
    if (auto_roll) p = p_str(p, " RND");
    *p = 0;
    draw_text_box(8, 2, 224, &FONT_S, s, frozen ? pal[4] : edit_sc ? C_B : C_A, 0);
}

static uint32_t isqrt32(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}

static void draw_meter(void)                    /* two level bars at the right of the globe's area, with a fall-back */
{
    static uint32_t hold[2];
    uint32_t sd, k;
    for (sd = 0; sd < 2u; sd++) {
        uint32_t pk = sd ? meter_r : meter_l, h, y;
        if (sd) meter_r = 0; else meter_l = 0;
        h = isqrt32(pk > 32767u ? 32767u : pk) * 56u / 181u;                 /* about a square-root scale: 8000 is half the height */
        if (h > hold[sd]) hold[sd] = h; else hold[sd] = hold[sd] > 3u ? hold[sd] - 3u : 0u;
        for (y = 0; y < 56u; y += 2u) {
            uint16_t c = fxs.cmode == 0u ? pal[3] : (y < 34u ? RGB(60, 220, 100) : y < 46u ? RGB(255, 200, 60) : RGB(255, 70, 60));
            cv_rect(220 + (int32_t)sd * 10, 60 - (int32_t)y - 1, 7, 1, y < hold[sd] ? c : dimc(c, 28));
        }
        (void)k;
    }
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
    cv_text(80, 4, &FONT_S, s, GCOL(G_HOME));
    p = s;
    p = p_str(p, "A ");
    p = p_str(p, INST_NAME[cur.sc[0].inst % NINST]);
    *p = 0;
    cv_text(80, 24, &FONT_S, s, C_A);
    p = s;
    p = p_str(p, "B ");
    p = p_str(p, INST_NAME[cur.sc[1].inst % NINST]);
    *p = 0;
    cv_text(80, 44, &FONT_S, s, C_B);
    if (fxs.meter) draw_meter();
    cv_blit(0, 26);
}

static void draw_morph(void)
{
    uint32_t i, x = 8u + (uint32_t)cur.morph_pos * 200u / 255u;
    static const char LET[6] = {'M', 'R', 'L', 'E', 'H', 'C'};
    char t[2] = {0, 0};
    cv_begin(240, 32, C_BLACK);
    for (i = 0; i < 8u; i++) cv_rect(8 + (int32_t)i * 28, 8, 28, 2, dimc(mixc(C_A, C_B, i * 255u / 7u), 130));   /* the track: A's colour to B's */
    cv_rect((int32_t)x, 2, 16, 14, pal[4]);
    cv_text(8, 16, &FONT_S, "A", edit_sc ? pal[1] : C_A);
    cv_text(222, 16, &FONT_S, "B", edit_sc ? C_B : pal[1]);
    for (i = 0; i < 6u; i++) {
        uint8_t on = cur.lfo_rate[i] && cur.lfo_depth[i];
        t[0] = LET[i];
        cv_text(70 + (int32_t)i * 16, 16, &FONT_S, t, on ? GCOL(G_LFO) : pal[1]);
    }
    cv_blit(0, 164);
}

static void draw_foot(void)
{
    char s[40], *p = s;
    if (fm1_ms < hud_until) {                                  /* a message: in place of the first line */
        p = p_str(p, hud_name);
        *p++ = ' ';
        p = p_str(p, hud_val);
        *p = 0;
        draw_text_box(8, 204, 224, &FONT_S, s, hud_scn == 1u ? C_A : hud_scn == 2u ? C_B : pal[4], 0);
    } else {
        p = p_str(p, "OCT ");
        p = p_sig(p, cur.oct);
        p = p_str(p, "  ");
        p = p_str(p, THN);
        if (xc_follow) p = p_str(p, "  EXT");                 /* the steps follow an external MIDI clock */
        if (bt_state() == BT_READY) p = p_str(p, "  BT");    /* a Bluetooth central is connected and listening */
        *p = 0;
        draw_text_box(8, 204, 224, &FONT_S, s, GCOL(G_GLO), 0);
    }
    p = s;
    p = p_str(p, "DEN ");
    p = p_pct(p, cur.density);
    p = p_str(p, "  SWG ");
    p = p_pct(p, cur.swing);
    *p = 0;
    draw_text_box(8, 222, 144, &FONT_S, s, GCOL(G_HOME), 0);
    if (dsp_pct >= 60u && running) {                           /* the audio code is getting full: say so */
        p = s; p = p_str(p, "DSP "); p = p_num(p, dsp_pct > 99u ? 99u : dsp_pct); *p++ = '%'; *p = 0;
        draw_text_box(160, 222, 72, &FONT_S, s, dsp_pct >= 80u ? RGB(255, 70, 60) : RGB(255, 200, 60), 0);
    } else draw_text_box(160, 222, 72, &FONT_S, "", pal[0], 0);
}

/* ---- the cells: what one knob controls on a page, how it reads, how it turns */
enum { K_NONE, K_DENS, K_SWING, K_P, K_AB, K_INST, K_TIMB, K_DLY, K_LVL, K_LSPD, K_LDEP, K_BPM, K_STEP, K_TRANS,
       K_THEME, K_ROOT, K_SCALE, K_CHA, K_CHB, K_RSND, K_RSIZ, K_RTON, K_DTM, K_DFB, K_DTN, K_SYNC, K_CHRD, K_VOIC, K_AMODE, K_ABARS, K_ASHP, K_CSRC,
       K_CMODE, K_METER, K_CCIN, K_PCIN, K_PANA, K_PANB, K_LSHP, K_LPHS, K_ENVA, K_ENVD, K_ENVS, K_ENVR, K_POLY, K_WAVE,
       K_DON, K_DLVL, K_DTONE, K_DDEC, K_DSHD, K_DSPC, K_DTUNE, K_DFOL, K_DSND, K_DCH };
typedef struct { uint8_t kind, arg; } cell_t;

static const char *const PSHORT[6] = {"MORPH", "REST", "LEGATO", "ENTROP", "HARMNY", "CHAOS"};
static const char *const INST_SHORT[28] = {"MIDI", "ANALOG", "FM", "SUPSAW", "PULSE", "EPIANO", "BELL", "PLUCK", "ORGAN", "BASS", "PAD", "BRASS",
                                           "PRO-5", "MOOG", "JUNO", "SYNC", "VIRUS", "SH-101", "ACID", "STRING", "CS-80", "DXBASS", "CLAV", "VIBES",
                                           "PWM", "BREATH", "HOOVER", "WOBBLE"};
static const char *const SCALE_SHORT[10] = {"MAJOR", "MINOR", "PENTMN", "PENTMJ", "DORIAN", "PHRYG", "LYDIAN",
                                            "MIXOLY", "HARMMN", "MELMN"};
static const char *const DTM_NAME[4] = {"1/16", "1/8", "1/8.", "1/4"};
static const char *const SYNC_NAME[3] = {"OFF", "START", "CLOCK"};
static const char *const CHORD_NAME[4] = {"STACK", "SUS", "QUART", "ADD9"};
static const char *const VOIC_NAME[3] = {"AUTO", "CLOSE", "OPEN"};
static const char *const CSRC_NAME[3] = {"INT", "EXT", "AUTO"};
static const char *const CMODE_NAME[3] = {"SOFT", "RICH", "WILD"};
static const char *const OFFON_NAME[2] = {"OFF", "ON"};
static const char *const AMODE_NAME[4] = {"OFF", "ONCE", "PING", "SAW"};
static const char *const ASHP_NAME[2] = {"LINEAR", "EASE"};

static void cpy(char *d, const char *s) { *p_str(d, s) = 0; }
static char *p_p100(char *p, uint32_t v255) { return p_num(p, (v255 * 100u + 127u) / 255u); }

static void cell_text(cell_t c, char *name, char *val, int32_t *bar, uint32_t *scn)   /* name[12], val[16] */
{
    scene_t *s = &cur.sc[edit_sc], *v = &cur.sc[c.arg & 1u];
    uint32_t a6 = c.arg % 6u;
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
    case K_INST: cpy(name, "INST"); cpy(val, INST_SHORT[v->inst % NINST]); *scn = (c.arg & 1u) + 1u; break;
    case K_TIMB: cpy(name, "TIMBRE"); *p_p100(val, v->timbre) = 0; *bar = v->timbre; *scn = (c.arg & 1u) + 1u; break;
    case K_DLY: cpy(name, "DELAY"); *p_p100(val, v->dsend) = 0; *bar = v->dsend; *scn = (c.arg & 1u) + 1u; break;
    case K_LVL: cpy(name, "LEVEL"); *p_p100(val, v->level) = 0; *bar = v->level; *scn = (c.arg & 1u) + 1u; break;
    case K_LSPD: {
        uint32_t pos = 0, i;
        cpy(name, "RATE");
        cpy(val, LFO_RATE[cur.lfo_rate[a6] % 9u]);
        for (i = 0; i < 9u; i++) if (LFO_POS[i] == cur.lfo_rate[a6]) pos = i;
        *bar = (int32_t)pos * 255 / 8;
        break;
    }
    case K_LDEP:
        cpy(name, "DEPTH");
        *p_p100(val, cur.lfo_depth[a6]) = 0;
        *bar = cur.lfo_depth[a6];
        break;
    case K_BPM: cpy(name, "BPM"); *p_num(val, bpm_get(&cur)) = 0; break;
    case K_STEP: cpy(name, "STEP"); cpy(val, DIV_NAME[cur.div & 3u]); break;
    case K_TRANS: cpy(name, "TRANSP"); *p_sig(val, cur.tr) = 0; break;
    case K_THEME: cpy(name, "THEME"); cpy(val, THN); break;
    case K_ROOT: cpy(name, "ROOT"); cpy(val, ROOT_NAME[cur.root % 12u]); break;
    case K_SCALE: cpy(name, "SCALE"); cpy(val, SCALE_SHORT[cur.scale % 10u]); break;
    case K_CHA: cpy(name, "CH A"); *p_num(val, (uint32_t)glob.ch[0] + 1u) = 0; break;
    case K_CHB: cpy(name, "CH B"); *p_num(val, (uint32_t)glob.ch[1] + 1u) = 0; break;
    case K_RSND: cpy(name, "SEND"); *p_p100(val, v->rsend) = 0; *bar = v->rsend; *scn = (c.arg & 1u) + 1u; break;
    case K_RSIZ: cpy(name, "SIZE"); *p_p100(val, v->rsize) = 0; *bar = v->rsize; *scn = (c.arg & 1u) + 1u; break;
    case K_RTON: cpy(name, "BRIGHT"); *p_p100(val, v->rtone) = 0; *bar = v->rtone; *scn = (c.arg & 1u) + 1u; break;
    case K_DTM: cpy(name, "TIME"); cpy(val, DTM_NAME[v->dtime & 3u]); *scn = (c.arg & 1u) + 1u; break;
    case K_DFB: cpy(name, "FDBK"); *p_p100(val, v->dfb) = 0; *bar = v->dfb; *scn = (c.arg & 1u) + 1u; break;
    case K_DTN: cpy(name, "TONE"); *p_p100(val, v->dtone) = 0; *bar = v->dtone; *scn = (c.arg & 1u) + 1u; break;
    case K_LSHP: cpy(name, "SHAPE"); cpy(val, LFO_SHAPE[(cur.lfo_sp[a6] & 7u) % 6u]); break;
    case K_LPHS: cpy(name, "PHASE"); *p_num(val, ((uint32_t)(cur.lfo_sp[a6] >> 3) * 100u) / 32u) = 0; *bar = (int32_t)(cur.lfo_sp[a6] >> 3) * 255 / 31; break;
    case K_ENVA: case K_ENVD: case K_ENVR: {
        uint32_t i = v->inst, n = c.kind == K_ENVA ? env_atk(i, v->env[0]) : c.kind == K_ENVD ? env_dec(i, v->env[1]) : env_rel(i, v->env[3]);
        uint32_t ms = n * 10u / 441u;
        char *q = val;
        cpy(name, c.kind == K_ENVA ? "ATTACK" : c.kind == K_ENVD ? "DECAY" : "RELEASE");
        if (ms < 1000u) { q = p_num(q, ms); q = p_str(q, "MS"); }
        else { q = p_num(q, ms / 1000u); *q++ = '.'; q = p_num(q, (ms % 1000u) / 100u); *q++ = 'S'; }
        *q = 0;
        *bar = v->env[c.kind == K_ENVA ? 0 : c.kind == K_ENVD ? 1 : 3];
        *scn = (c.arg & 1u) + 1u;
        break;
    }
    case K_ENVS: cpy(name, "SUS"); *p_p100(val, env_sus(v->inst, v->env[2])) = 0; *bar = v->env[2]; *scn = (c.arg & 1u) + 1u; break;
    case K_POLY: cpy(name, "POLY"); cpy(val, OFFON_NAME[cur.poly & 1u]); break;
    case K_DON: cpy(name, "DRUMS"); cpy(val, OFFON_NAME[cur.dr_on & 1u]); break;
    case K_DLVL: cpy(name, "LEVEL"); *p_p100(val, cur.dr_level) = 0; *bar = cur.dr_level; break;
    case K_DTONE: cpy(name, "TONE"); *p_p100(val, cur.dr_tone) = 0; *bar = cur.dr_tone; break;
    case K_DDEC: cpy(name, "DECAY"); *p_p100(val, cur.dr_decay) = 0; *bar = cur.dr_decay; break;
    case K_DSHD: cpy(name, c.arg & 1u ? "SHDW B" : "SHDW A"); *p_p100(val, cur.dr_shadow[c.arg & 1u]) = 0; *bar = cur.dr_shadow[c.arg & 1u]; *scn = (c.arg & 1u) + 1u; break;
    case K_DSPC: cpy(name, c.arg & 1u ? "SPCE B" : "SPCE A"); *p_p100(val, cur.dr_space[c.arg & 1u]) = 0; *bar = cur.dr_space[c.arg & 1u]; *scn = (c.arg & 1u) + 1u; break;
    case K_DTUNE: {
        int32_t t = clampi((int32_t)cur.dr_tune - 128, -12, 12);
        char *q = val;
        cpy(name, "TUNE");
        if (t > 0) *q++ = '+'; else if (t < 0) { *q++ = '-'; t = -t; }
        q = p_num(q, (uint32_t)t); *q = 0;
        *bar = ((int32_t)clampi((int32_t)cur.dr_tune - 128, -12, 12) + 12) * 255 / 24;
        break;
    }
    case K_DFOL: cpy(name, "FOLLOW"); cpy(val, OFFON_NAME[cur.dr_follow & 1u]); break;
    case K_DSND: cpy(name, "SEND"); *p_p100(val, cur.dr_send) = 0; *bar = cur.dr_send; break;
    case K_DCH: cpy(name, "DRUM CH"); *p_num(val, (uint32_t)glob.ch[2] + 1u) = 0; break;
    case K_SYNC: cpy(name, "SYNC"); cpy(val, SYNC_NAME[fxs.sync % 3u]); break;
    case K_CHRD: cpy(name, "CHORD"); cpy(val, CHORD_NAME[fxs.chord & 3u]); break;
    case K_VOIC: cpy(name, "SPREAD"); cpy(val, VOIC_NAME[fxs.voic % 3u]); break;
    case K_CMODE: cpy(name, "COLOUR"); cpy(val, CMODE_NAME[fxs.cmode % 3u]); break;
    case K_METER: cpy(name, "METER"); cpy(val, OFFON_NAME[fxs.meter & 1u]); break;
    case K_CCIN: cpy(name, "CC IN"); cpy(val, OFFON_NAME[fxs.ccin & 1u]); break;
    case K_PCIN: cpy(name, "PROG"); cpy(val, OFFON_NAME[fxs.pcin & 1u]); break;
    case K_PANA: case K_PANB: {
        uint32_t pn = c.kind == K_PANA ? fxs.pana : fxs.panb;
        int32_t d = (int32_t)pn - 128;
        char *q = val;
        cpy(name, c.kind == K_PANA ? "PAN A" : "PAN B");
        if (d > -4 && d < 4) q = p_str(q, "C");
        else { q = p_str(q, d < 0 ? "L" : "R"); q = p_num(q, (uint32_t)((d < 0 ? -d : d) * 100 / 128)); }
        *q = 0;
        *bar = (int32_t)pn; *scn = c.kind == K_PANA ? 1 : 2;
        break;
    }
    case K_CSRC: cpy(name, "CLOCK"); cpy(val, CSRC_NAME[fxs.csrc % 3u]); break;
    case K_AMODE: cpy(name, "AUTO"); cpy(val, AMODE_NAME[fxs.amode & 3u]); break;
    case K_ABARS: cpy(name, "BARS"); *p_num(val, 1u << (fxs.abars % 7u)) = 0; break;
    case K_ASHP: cpy(name, "SHAPE"); cpy(val, ASHP_NAME[fxs.ashp & 1u]); break;
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
    case K_INST: {
        uint8_t was = v->inst;
        v->inst = (uint8_t)clampi((int32_t)v->inst + sg, 0, (int32_t)NINST - 1);
        if (v->inst != was) v->env[0] = v->env[1] = v->env[2] = v->env[3] = 128;      /* a new instrument starts from its own envelope */
        break;
    }
    case K_TIMB: adj(&v->timbre, d, 3); break;
    case K_DLY: adj(&v->dsend, d, 3); break;
    case K_LVL: adj(&v->level, d, 3); break;
    case K_LSPD: {
        int32_t pos = 0, i;
        for (i = 0; i < 9; i++) if (LFO_POS[i] == cur.lfo_rate[c.arg % 6u]) pos = i;
        cur.lfo_rate[c.arg % 6u] = LFO_POS[clampi(pos + sg, 0, 8)];
        break;
    }
    case K_LDEP: adj(&cur.lfo_depth[c.arg % 6u], d, 3); break;
    case K_BPM: bpm_set(&cur, (int32_t)bpm_get(&cur) + d); break;
    case K_STEP: cur.div = (uint8_t)clampi((int32_t)cur.div + sg, 0, 3); break;
    case K_TRANS: cur.tr = (int8_t)clampi((int32_t)cur.tr + sg, -12, 12); break;
    case K_THEME: theme_set((uint32_t)((int32_t)glob.theme + (int32_t)NTHEME + sg) % NTHEME); break;
    case K_ROOT: cur.root = (uint8_t)clampi((int32_t)cur.root + sg, 0, 11); break;
    case K_SCALE: cur.scale = (uint8_t)clampi((int32_t)cur.scale + sg, 0, 9); break;
    case K_CHA: glob.ch[0] = (uint8_t)clampi((int32_t)glob.ch[0] + sg, 0, 15); break;
    case K_CHB: glob.ch[1] = (uint8_t)clampi((int32_t)glob.ch[1] + sg, 0, 15); break;
    case K_RSND: adj(&v->rsend, d, 3); break;
    case K_RSIZ: adj(&v->rsize, d, 3); break;
    case K_RTON: adj(&v->rtone, d, 3); break;
    case K_DTM: v->dtime = (uint8_t)clampi((int32_t)v->dtime + sg, 0, 3); break;
    case K_DFB: v->dfb = (uint8_t)clampi((int32_t)v->dfb + d * 3, 0, 230); break;
    case K_DTN: adj(&v->dtone, d, 3); break;
    case K_LSHP: { uint32_t sh = (cur.lfo_sp[c.arg % 6u] & 7u) % 6u; sh = (uint32_t)clampi((int32_t)sh + sg, 0, 5); cur.lfo_sp[c.arg % 6u] = (uint8_t)((cur.lfo_sp[c.arg % 6u] & ~7u) | sh); break; }
    case K_LPHS: { uint8_t *q = &cur.lfo_sp[c.arg % 6u]; *q = (uint8_t)((*q & 7u) | ((uint32_t)clampi((int32_t)(*q >> 3) + sg, 0, 31) << 3)); break; }
    case K_ENVA: adj(&v->env[0], d, 3); break;
    case K_ENVD: adj(&v->env[1], d, 3); break;
    case K_ENVS: adj(&v->env[2], d, 3); break;
    case K_ENVR: adj(&v->env[3], d, 3); break;
    case K_POLY: cur.poly = sg > 0 ? 1u : 0u; break;
    case K_DON: cur.dr_on = sg > 0 ? 1u : 0u; break;
    case K_DLVL: adj(&cur.dr_level, d, 3); break;
    case K_DTONE: adj(&cur.dr_tone, d, 3); break;
    case K_DDEC: adj(&cur.dr_decay, d, 3); break;
    case K_DSHD: adj(&cur.dr_shadow[c.arg & 1u], d, 3); break;
    case K_DSPC: adj(&cur.dr_space[c.arg & 1u], d, 3); break;
    case K_DTUNE: cur.dr_tune = (uint8_t)(128 + clampi((int32_t)cur.dr_tune - 128 + sg, -12, 12)); break;
    case K_DFOL: cur.dr_follow = sg > 0 ? 1u : 0u; break;
    case K_DSND: adj(&cur.dr_send, d, 3); break;
    case K_DCH: glob.ch[2] = (uint8_t)clampi((int32_t)glob.ch[2] + sg, 0, 15); break;
    case K_SYNC: fxs.sync = (uint8_t)clampi((int32_t)fxs.sync + sg, 0, 2); break;
    case K_CHRD: fxs.chord = (uint8_t)clampi((int32_t)fxs.chord + sg, 0, 3); break;
    case K_VOIC: fxs.voic = (uint8_t)clampi((int32_t)fxs.voic + sg, 0, 2); break;
    case K_CMODE: fxs.cmode = (uint8_t)clampi((int32_t)fxs.cmode + sg, 0, 2); break;
    case K_METER: fxs.meter = sg > 0 ? 1u : 0u; break;
    case K_CCIN: fxs.ccin = sg > 0 ? 1u : 0u; break;
    case K_PCIN: fxs.pcin = sg > 0 ? 1u : 0u; break;
    case K_PANA: fxs.pana = (uint8_t)clampi((int32_t)fxs.pana + d * 3, 0, 255); break;
    case K_PANB: fxs.panb = (uint8_t)clampi((int32_t)fxs.panb + d * 3, 0, 255); break;
    case K_CSRC: fxs.csrc = (uint8_t)clampi((int32_t)fxs.csrc + sg, 0, 2); break;
    case K_AMODE: fxs.amode = (uint8_t)clampi((int32_t)fxs.amode + sg, 0, 3); auto_arm(); break;
    case K_ABARS: fxs.abars = (uint8_t)clampi((int32_t)fxs.abars + sg, 0, 6); break;
    case K_ASHP: fxs.ashp = sg > 0 ? 1u : 0u; break;
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
static const char *const GROUP_NAME[6] = {"HOME", "FX", "LFO", "GLO", "ENV", "DRUM"};
typedef struct { uint8_t group, pos, flags; const char *tab; cell_t c[4]; } half_t;       /* flags 1: only shown (the knobs do not go to it, SELECT skips it) */
#define LF(i, nm) {G_LFO, 0, 1, nm, {{K_WAVE, i}, {K_NONE, 0}, {K_NONE, 0}, {K_NONE, 0}}}, \
                  {G_LFO, 1, 0, nm, {{K_LSHP, i}, {K_LSPD, i}, {K_LDEP, i}, {K_LPHS, i}}}
static const half_t HALF[] = {
    {G_HOME, 0, 0, "GENERATE", {{K_P, 3}, {K_P, 4}, {K_P, 5}, {K_P, 0}}},
    {G_HOME, 1, 0, "RHYTHM", {{K_P, 1}, {K_DENS, 0}, {K_SWING, 0}, {K_P, 2}}},
    {G_FX, 0, 0, "A VOICE", {{K_INST, 0}, {K_TIMB, 0}, {K_LVL, 0}, {K_NONE, 0}}},
    {G_FX, 1, 0, "A DELAY", {{K_DLY, 0}, {K_DTM, 0}, {K_DFB, 0}, {K_DTN, 0}}},
    {G_FX, 0, 0, "A REVERB", {{K_RSND, 0}, {K_RSIZ, 0}, {K_RTON, 0}, {K_NONE, 0}}},
    {G_FX, 0, 0, "B VOICE", {{K_INST, 1}, {K_TIMB, 1}, {K_LVL, 1}, {K_NONE, 0}}},
    {G_FX, 1, 0, "B DELAY", {{K_DLY, 1}, {K_DTM, 1}, {K_DFB, 1}, {K_DTN, 1}}},
    {G_FX, 0, 0, "B REVERB", {{K_RSND, 1}, {K_RSIZ, 1}, {K_RTON, 1}, {K_NONE, 0}}},
    {G_FX, 1, 0, "MIX", {{K_PANA, 0}, {K_PANB, 0}, {K_NONE, 0}, {K_NONE, 0}}},
    {G_ENV, 0, 0, "A ENVELOPE", {{K_ENVA, 0}, {K_ENVD, 0}, {K_ENVS, 0}, {K_ENVR, 0}}},
    {G_ENV, 1, 0, "B ENVELOPE", {{K_ENVA, 1}, {K_ENVD, 1}, {K_ENVS, 1}, {K_ENVR, 1}}},
    LF(0, "MORPH"), LF(1, "REST"), LF(2, "LEGATO"), LF(3, "ENTROPY"), LF(4, "HARMONY"), LF(5, "CHAOS"),
    {G_GLO, 0, 0, "TEMPO KEY", {{K_BPM, 0}, {K_STEP, 0}, {K_TRANS, 0}, {K_THEME, 0}}},
    {G_GLO, 1, 0, "SCALE MIDI", {{K_ROOT, 0}, {K_SCALE, 0}, {K_CHA, 0}, {K_CHB, 0}}},
    {G_GLO, 0, 0, "CHORDS", {{K_POLY, 0}, {K_CHRD, 0}, {K_VOIC, 0}, {K_NONE, 0}}},
    {G_GLO, 1, 0, "SYNC", {{K_SYNC, 0}, {K_CSRC, 0}, {K_NONE, 0}, {K_NONE, 0}}},
    {G_GLO, 0, 0, "AUTO A-B", {{K_AMODE, 0}, {K_ABARS, 0}, {K_ASHP, 0}, {K_NONE, 0}}},
    {G_GLO, 1, 0, "MIDI IN LOOK", {{K_CCIN, 0}, {K_PCIN, 0}, {K_CMODE, 0}, {K_METER, 0}}},
    {G_DRUM, 0, 0, "KIT", {{K_DON, 0}, {K_DLVL, 0}, {K_DTONE, 0}, {K_DDEC, 0}}},
    {G_DRUM, 1, 0, "FEEL", {{K_DSHD, 0}, {K_DSPC, 0}, {K_DSHD, 1}, {K_DSPC, 1}}},
    {G_DRUM, 0, 0, "TUNING", {{K_DTUNE, 0}, {K_DFOL, 0}, {K_DSND, 0}, {K_DCH, 0}}},
};
#define NHALF ((int32_t)(sizeof HALF / sizeof HALF[0]))
static int32_t cur_half = -1;                   /* -1: the performance screen */
static uint8_t page_auto;                       /* the page was opened by a knob on the performance screen: it closes by itself */
static uint32_t pg_t0;                          /* the last touch (a page goes back to the performance screen 10 s after it) */

static int32_t group_first(uint32_t g) { int32_t i; for (i = 0; i < NHALF; i++) if (HALF[i].group == g) return i; return -1; }
static int32_t group_last(uint32_t g) { int32_t i, l = -1; for (i = 0; i < NHALF; i++) if (HALF[i].group == g) l = i; return l; }
static int32_t half_step(int32_t h, int32_t dir)    /* SELECT: the next half the knobs can use, in the same group, round and round */
{
    uint32_t g = HALF[h].group;
    int32_t f = group_first(g), l = group_last(g), i = h, guard = 0;
    do {
        i += dir;
        if (i > l) i = f;
        if (i < f) i = l;
    } while ((HALF[i].flags & 1u) && guard++ < 40);
    return i;
}
static int32_t pages_in(uint32_t g)             /* the pages of a group: a page is a top half and the one under it */
{
    int32_t i, n = 0;
    for (i = 0; i < NHALF; i++) if (HALF[i].group == g && HALF[i].pos == 0) n++;
    return n;
}
static int32_t page_of(int32_t h)               /* 0-based page of half h in its group */
{
    int32_t i, n = -1;
    for (i = 0; i <= h; i++) if (HALF[i].group == HALF[h].group && HALF[i].pos == 0) n++;
    return n;
}
static void goto_group(uint32_t g)              /* a group's button: into its pages, and out again from them (SELECT goes through them) */
{
    int32_t i, h;
    if (cur_half >= 0 && HALF[cur_half].group == g) { cur_half = -1; return; }
    h = group_first(g);
    if (g == G_FX || g == G_ENV)                /* these open on the voice you are editing */
        for (i = group_first(g); i <= group_last(g); i++)
            if ((HALF[i].c[0].kind == K_INST || HALF[i].c[0].kind == K_ENVA) && (HALF[i].c[0].arg & 1u) == edit_sc) { h = i; break; }
    while (h >= 0 && (HALF[h].flags & 1u)) h++;
    cur_half = h;
}
static uint32_t half_voice(int32_t h)           /* the voice a half is about: 0 A, 1 B, 2 neither (MIX): what the punch-ins act on */
{
    uint32_t i;
    if (h < 0) return 2;
    for (i = 0; i < 4u; i++) {
        uint32_t k = HALF[h].c[i].kind;
        if (k == K_INST || k == K_TIMB || k == K_DLY || k == K_LVL || k == K_DTM || k == K_DFB || k == K_DTN || k == K_RSND || k == K_RSIZ || k == K_RTON) return HALF[h].c[i].arg & 1u;
    }
    return 2;
}

/* ---- the other screens: the menus (SELECT knob) with rows of label, value, bar */
enum { V_PERF, V_SCALE, V_COPY, V_CHAIN, V_SYS, V_SYS2, V_PAGE, V_SLOTS, V_IDLE };      /* (SELECT goes through V_SCALE .. V_SYS2 in this order) */
static uint8_t view, view_drawn = 255;
static uint8_t menu_pg, copy_act, copy_slot, chain_sel, save_mode, recall_mode, tr_view;
static uint32_t tr_t0;
static char rows_cache[5][28];
static const char *const COPY_NAME[6] = {"COPY A TO B", "COPY B TO A", "INIT SCENE", "INIT ALL", "INIT SLOT", "INIT ALL SLOTS"};   /* the last two empty the saved presets: SAVE is held for them */

typedef struct { char label[16], value[16]; int16_t bar; uint8_t hl; } row_t;
static const char *view_title(void)
{
    switch (view) {
    case V_SCALE: return "SCALE";
    case V_COPY: return "COPY INIT";
    case V_CHAIN: return "CHAIN";
    case V_SYS2: return "SYSTEM 2";
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
        if (i == 0) { setrow(r, COPY_NAME[copy_act % 6u], -1, 1); setval_s(r, copy_act >= 4u ? "HOLD SAVE" : "SAVE=GO"); return 1; }
        if (i == 1 && copy_act == 4u) { setrow(r, "SLOT", -1, 0); setval_n(r, (uint32_t)copy_slot + 1u); return 1; }
        return 0;
    case V_CHAIN: {
        uint8_t c = chain[chain_sel % CHAIN_N];
        if (i == 0) { char *p = p_str(r->label, "STEP"); *p = 0; r->bar = -1; r->hl = 1; p = p_num(r->value, (uint32_t)chain_sel + 1u); p = p_str(p, "/8"); *p = 0; return 1; }
        if (i == 1) { setrow(r, "SLOT", -1, 0); if (c & 0x80u) setval_n(r, (uint32_t)(c & 15u) + 1u); else setval_s(r, "--"); return 1; }
        if (i == 2) { uint32_t code = (c >> 4) & 7u; setrow(r, "MEASURES", -1, 0); setval_n(r, 1u << (code > 4u ? 4u : code)); return 1; }
        if (i == 3) { setrow(r, "CHAIN", -1, 0); setval_s(r, chain_on ? "ON" : "OFF"); return 1; }
        if (i == 4 && chain_on && running && chain_pos < CHAIN_N) { char *p = p_str(r->label, "PLAYING STEP"); *p = 0; r->bar = -1; r->hl = 0; p = p_num(r->value, (uint32_t)chain_pos + 1u); *p = 0; return 1; }
        return 0;
    }
    case V_SYS2:
        if (i == 0) { setrow(r, "HELP TEXT", -1, 0); setval_s(r, fxs.hint ? "ON" : "OFF"); return 1; }
        if (i == 1) { setrow(r, "REC KEEPS", -1, 0); setval_s(r, fxs.recm ? "+MOTION" : "NOTES"); return 1; }
        if (i == 2) { static const char *const IDLE_NAME[4] = {"OFF", "30 SEC", "1 MIN", "3 MIN"}; setrow(r, "IDLE SCREEN", -1, 0); setval_s(r, IDLE_NAME[fxs.idle & 3u]); return 1; }
        if (i == 3) { char *p = p_str(r->label, "TAP TEMPO"); *p = 0; r->bar = -1; r->hl = 0; p = p_str(r->value, "SAVE "); p = p_num(p, bpm_get(&cur)); *p = 0; return 1; }
        return 0;
    case V_SYS:
        if (i == 0) { setrow(r, "THEME", -1, 0); setval_s(r, THN); return 1; }
        if (i == 1) { setrow(r, "OUT A CH", -1, 0); setval_n(r, (uint32_t)glob.ch[0] + 1u); return 1; }
        if (i == 2) { setrow(r, "OUT B CH", -1, 0); setval_n(r, (uint32_t)glob.ch[1] + 1u); return 1; }
        if (i == 3) {                           /* knob 4: BLUETOOTH, off at every power-on */
            uint32_t b = bt_state();
            setrow(r, "BLUETOOTH", -1, 0);
            if (b == BT_READY) { char *p = p_str(r->value, "READY "); p = p_num(p, bt_count()); *p = 0; }   /* + the packets queued for it */
            else setval_s(r, b == BT_OFF ? "OFF" : b == BT_NOCAL ? "NO CAL" : b == BT_ADV ? "WAITING" : "LINKED");   /* LINKED: connected, MIDI not enabled by the other side */
            return 1;
        }
        if (i == 4) { char *p; setrow(r, "DSP LOAD", (int16_t)clampi((int32_t)dsp_pct * 255 / 100, 0, 255), 0); p = p_num(r->value, dsp_pct); *p++ = '%'; *p = 0; return 1; }
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
        cv_rect(12, 20, (int32_t)r->bar * 216 / 255, 2, r->hl ? pal[4] : C_A);
    }
    cv_blit(0, y);
}

/* ---- the beginner text (HELP TEXT in the SYSTEM 2 menu): a note on the knob you touch, in the bottom third of the screen */
static const char *help_text(cell_t c)
{
    static const char *const PH[6] = {"Blends each step's chance into the next step's.", "The chance that a step stays silent.",
        "How long notes last: high is smooth and joined.", "Play direction: the middle is forward, the sides reverse or wander.",
        "How many chord notes join each note (POLY on).", "The chance a note jumps an octave."};
    switch (c.kind) {
    case K_DENS: return "Raises or lowers every step's chance of sounding.";
    case K_SWING: return "Delays every other step for a shuffled feel.";
    case K_P: return PH[c.arg % 6u];
    case K_AB: return "Crossfades between scene A and scene B.";
    case K_INST: return "The sound of this voice.";
    case K_TIMB: return "Tone of this voice, darker to brighter.";
    case K_DLY: return "How much of this voice goes into its echo.";
    case K_LVL: return "How loud this voice is.";
    case K_LSPD: return "How fast this LFO moves its parameter, in beats and bars.";
    case K_LDEP: return "How far this LFO pushes its parameter.";
    case K_LSHP: return "The wave the LFO draws: sine, triangle, saw, ramp, square or random.";
    case K_LPHS: return "Where in its wave the LFO starts each time you press PLAY.";
    case K_BPM: return "The tempo, in beats a minute.";
    case K_STEP: return "How long a step is, from a quarter note to a 32nd.";
    case K_TRANS: return "Moves every note up or down in semitones.";
    case K_THEME: return "The colours of the screen.";
    case K_ROOT: return "The key note of the scale.";
    case K_SCALE: return "The scale the arp takes its notes from.";
    case K_CHA: case K_CHB: return "The MIDI channel this voice sends on.";
    case K_RSND: return "How much of this voice goes into its reverb.";
    case K_RSIZ: return "Reverb size: a small room up to a long hall.";
    case K_RTON: return "Reverb brightness: low is dark and soft.";
    case K_DTM: return "Echo time, as a note length at the tempo.";
    case K_DFB: return "How many echoes repeat before they fade.";
    case K_DTN: return "Echo tone: dark to bright.";
    case K_SYNC: return "Sends MIDI start, stop and clock to your other gear.";
    case K_CHRD: return "The kind of chord POLY builds.";
    case K_VOIC: return "How the chord notes are spread: close or open.";
    case K_AMODE: return "Lets the A-B crossfade move by itself: once, ping-pong or saw.";
    case K_ABARS: return "How many measures one A to B trip takes.";
    case K_ASHP: return "Linear, or slowing near the ends.";
    case K_CSRC: return "Where the tempo comes from: this unit or an outside clock.";
    case K_CMODE: return "Colour scheme: soft, rich or wild.";
    case K_METER: return "Shows the output level next to the globe.";
    case K_CCIN: return "Lets MIDI control changes move the parameters.";
    case K_PCIN: return "Lets MIDI program changes recall presets.";
    case K_PANA: case K_PANB: return "Where this voice sits, left to right.";
    case K_ENVA: return "How fast a note fades in.";
    case K_ENVD: return "How fast it falls to the sustain level.";
    case K_ENVS: return "The level it holds while the key is down.";
    case K_ENVR: return "How long a note rings after you let go.";
    case K_POLY: return "Builds a chord on every note; HARMONY sets its size.";
    case K_DON: return "A gentle kit that follows the melody and keeps out of its way.";
    case K_DLVL: return "How loud the drums are. They always stay under the melody.";
    case K_DTONE: return "Brightness of the hats and brush: low is soft and dark.";
    case K_DDEC: return "How long the drums ring.";
    case K_DSHD: return "How much the drums answer the melody's leaps and gaps.";
    case K_DSPC: return "Room left for the melody: high keeps the drums to its gaps.";
    case K_DTUNE: return "Tunes the kick and tom, in semitones from the key.";
    case K_DFOL: return "The kick moves to the fifth when the melody leaves the chord.";
    case K_DSND: return "How much of the drums goes into the reverbs.";
    case K_DCH: return "The MIDI channel the drums send on (10 is the usual).";
    default: return "";
    }
}
static const char *const PI_NAME[16] = {"THROW", "REPEAT", "DLY 1/16", "DLY 1/4", "BLOOM", "HALL", "DARK", "AIR",
                                        "CLOSE", "OPEN", "DUCK", "KILL", "LONG", "SHORT", "SLOW", "FLIP"};   /* the punch-ins: the lower keys on the FX pages */
static cell_t help_c;
static uint32_t help_until, help_gen, help_drawn;
static void help_set(cell_t c)
{
    if (!fxs.hint || c.kind == K_NONE) return;
    help_c = c; help_until = fm1_ms + 4000u; help_gen++;
}
static void draw_help(void)
{
    char name[12], val[16], line[32];
    int32_t bar;
    uint32_t scn, y = 184, n = 0;
    const char *t = help_text(help_c);
    cell_text(help_c, name, val, &bar, &scn);
    lcd_fill(0, 162, 240, 78, C_BLACK);
    draw_text_box(8, 166, 224, &FONT_S, name, scn == 1u ? C_A : scn == 2u ? C_B : GCOL(cur_half >= 0 ? HALF[cur_half].group : G_HOME), 0);
    while (*t && y < 224u) {                                    /* wrapped at 28 characters, on a space */
        uint32_t k = 0, last = 0;
        while (t[k] && k < 28u) { if (t[k] == ' ') last = k; k++; }
        if (t[k] && last) k = last;
        n = 0;
        while (n < k) { line[n] = t[n]; n++; }
        line[n] = 0;
        draw_text_box(8, y, 224, &FONT_S, line, pal[3], 0);
        t += k;
        while (*t == ' ') t++;
        y += 16u;
    }
}

static void draw_dots(uint32_t g, int32_t h)    /* the pages of a group as dots: lit up to the page you are on (all of them on the last) */
{
    int32_t n = pages_in(g), k = page_of(h), i;
    uint16_t gc = GCOL(g);
    lcd_fill(150, 2, 90, 16, C_BLACK);
    for (i = 0; i < n; i++) lcd_fill((uint32_t)(236 - (n - i) * 10), 7, 6, 6, i <= k ? gc : pal[1]);
}

static void draw_wave(int32_t hi, uint32_t y, int lit)      /* an LFO's wave, with a dot where it is now */
{
    uint32_t i = HALF[hi].c[0].arg % 6u, sh = (cur.lfo_sp[i] & 7u) % 6u, phs = (uint32_t)(cur.lfo_sp[i] >> 3) << 27, r = cur.lfo_rate[i];
    int32_t x, py = 32, A = 1 + (int32_t)cur.lfo_depth[i] * 24 / 255, yy;
    uint16_t gc = GCOL(G_LFO), fc = lit ? gc : dimc(gc, 70), wc = r ? gc : dimc(gc, 60);
    cv_begin(240, 64, C_BLACK);
    cv_rect(2, 0, 236, 1, fc); cv_rect(2, 63, 236, 1, fc); cv_rect(2, 0, 1, 64, fc); cv_rect(237, 0, 1, 64, fc);
    cv_rect(6, 32, 228, 1, pal[0]);
    for (x = 0; x < 228; x++) {
        uint32_t ph = (uint32_t)x * 18838535u + phs;
        int32_t v = sh == 5u ? (int32_t)(((ph >> 29) * 2654435761u) >> 16 & 0xFFFFu) - 32768 : lfo_wave(sh, ph, 0);
        yy = 32 - (v * A) / 32768;
        if (!r) yy = 32;
        if (x == 0) py = yy;
        cv_rect(6 + x, yy < py ? yy : py, 1, (yy < py ? py - yy : yy - py) + 1, wc);
        py = yy;
    }
    if (r) {
        int32_t cx = (int32_t)((((lfo_ph[i] + phs) >> 24) * 228u) >> 8);
        yy = 32 - (lfo_out[i] * A) / 32768;
        cv_rect(6 + cx, 4, 1, 56, dimc(gc, 90));
        cv_rect(6 + cx - 3, yy - 3, 7, 7, pal[4]);
    } else cv_text(104, 24, &FONT_S, "OFF", pal[2]);
    cv_blit(0, y);
}

static void draw_slots(void)                    /* the 16 preset slots: filled = saved, outline = empty, the number inside; the keys 1-16 pick them */
{
    uint32_t i;
    bank_open();
    draw_text_box(0, 18, 240, &FONT_L, save_mode ? "SAVE TO" : "RECALL", save_mode ? pal[4] : C_A, 1);
    draw_text_box(0, 54, 240, &FONT_L, "SLOT", save_mode ? pal[4] : C_A, 1);
    cv_begin(240, 64, C_BLACK);
    for (i = 0; i < 16u; i++) {
        int32_t x = 10 + (int32_t)(i & 7u) * 28, y = 4 + (int32_t)(i >> 3) * 30;
        char t[4], *q = p_num(t, i + 1u);
        uint16_t c = i == glob.slot ? pal[4] : C_A;
        *q = 0;
        if (slot_used(i)) { cv_rect(x, y, 24, 24, c); cv_text(x + 12 - text_w(&FONT_S, t) / 2, y + 4, &FONT_S, t, C_BLACK); }
        else {
            cv_rect(x, y, 24, 1, pal[1]); cv_rect(x, y + 23, 24, 1, pal[1]); cv_rect(x, y, 1, 24, pal[1]); cv_rect(x + 23, y, 1, 24, pal[1]);
            cv_text(x + 12 - text_w(&FONT_S, t) / 2, y + 4, &FONT_S, t, pal[1]);
        }
    }
    cv_blit(0, 92);
    draw_text_box(0, 164, 240, &FONT_S, save_mode ? "PRESS A LOWER KEY 1-16" : "KEY 1-16 RECALLS THAT SLOT", pal[3], 1);
    draw_text_box(0, 182, 240, &FONT_S, "FILLED = SAVED  BRIGHT = LAST", pal[2], 1);
    draw_text_box(0, 206, 240, &FONT_S, save_mode ? "SAVE AGAIN TO CANCEL" : "HOLD SAVE TO LEAVE", pal[2], 1);
}

/* one panel of a page: four cells, the lit half framed bright */
/* ---- the formation: 36 particles that spring to a shape (a ring, two rings, a spiral, a lattice, a wave, three rings like the globe), scatter when a note sounds and
 * form again. It is the screen that comes by itself after a while untouched (IDLE in SYSTEM 2), and, forming a ring round the name, the boot logo. A frame is made
 * every 50 ms, in four strips of 60 rows, one strip a call. */
#define NP 36
#define BOOT_MS 2400u
static int32_t pq_x[NP], pq_y[NP], pq_vx[NP], pq_vy[NP];       /* Q8 */
static uint8_t form_shape, form_init;
static uint32_t boot_age;                       /* ms since the boot logo began */
static uint32_t form_t0, form_hits, form_rot;
static const uint32_t IDLE_MS[4] = {0, 30000u, 60000u, 180000u};

static void form_target(uint32_t shape, uint32_t i, int32_t *tx, int32_t *ty)       /* where particle i goes (pixels) */
{
    uint32_t ph = i * (4294967295u / NP) + form_rot;
    switch (shape % 6u) {
    case 0: *tx = 120 + ((cosine(ph) * 92) >> 15); *ty = 120 + ((sine(ph) * 92) >> 15); break;                                      /* a ring */
    case 1: { uint32_t j = i % (NP / 2u), p2 = j * (4294967295u / (NP / 2u)) + (i < NP / 2u ? form_rot : 0u - form_rot);
              *tx = (i < NP / 2u ? 84 : 156) + ((cosine(p2) * 56) >> 15); *ty = 120 + ((sine(p2) * 56) >> 15); break; }            /* two rings, A and B */
    case 2: { int32_t r = 8 + (int32_t)i * 3; *tx = 120 + ((cosine(ph * 3u) * r) >> 15); *ty = 120 + ((sine(ph * 3u) * r) >> 15); break; }   /* a spiral */
    case 3: *tx = 120 + ((int32_t)(i % 6u) - 2) * 26 - 13; *ty = 120 + ((int32_t)(i / 6u) - 2) * 26 - 13; break;                  /* a lattice */
    case 4: *tx = 16 + (int32_t)i * 6; *ty = 120 + ((sine(ph * 2u + form_rot * 2u) * 60) >> 15); break;                           /* a wave */
    default: { uint32_t ring = i / 12u, j = i % 12u, p3 = j * 357913941u + form_rot; int32_t r = 44 + (int32_t)ring * 22;
               *tx = 120 + ((cosine(p3) * r) >> 15); *ty = 120 + ((sine(p3) * (r * 2 / 5 + (int32_t)ring * 14)) >> 15) + ((int32_t)ring - 1) * 30; break; }   /* three rings */
    }
}
static void form_reset(uint32_t scattered)
{
    uint32_t i;
    for (i = 0; i < NP; i++) {
        int32_t tx, ty;
        form_target(form_shape, i, &tx, &ty);
        if (scattered) { pq_x[i] = (int32_t)(((i + 3u) * 2654435761u >> 8) % 240u) << 8; pq_y[i] = (int32_t)(((i + 5u) * 40503u * 7919u >> 6) % 240u) << 8; }
        else { pq_x[i] = tx << 8; pq_y[i] = ty << 8; }
        pq_vx[i] = pq_vy[i] = 0;
    }
    form_init = 1;
}
static void form_step(uint32_t boot)             /* one frame of the motion */
{
    uint32_t i, now = fm1_ms;
    if (!form_init) form_reset(boot);
    if (!boot && now - form_t0 > 7000u) { form_t0 = now; form_shape = (uint8_t)((form_shape + 1u) % 6u); }          /* a new formation every seven seconds */
    form_rot += 3000000u;
    if (hit_n != form_hits) {                                                                                        /* a note: they scatter, outward */
        form_hits = hit_n;
        for (i = 0; i < NP; i++) {
            pq_vx[i] += ((pq_x[i] >> 8) - 120) * 6 + (int32_t)(rnd32() % 1200u) - 600;
            pq_vy[i] += ((pq_y[i] >> 8) - 120) * 6 + (int32_t)(rnd32() % 1200u) - 600;
        }
    }
    for (i = 0; i < NP; i++) {
        int32_t tx, ty;
        form_target(boot ? 0u : form_shape, i, &tx, &ty);
        pq_vx[i] += ((tx << 8) - pq_x[i]) >> 4;
        pq_vy[i] += ((ty << 8) - pq_y[i]) >> 4;
        pq_vx[i] -= pq_vx[i] >> 3; pq_vy[i] -= pq_vy[i] >> 3;
        pq_x[i] += pq_vx[i]; pq_y[i] += pq_vy[i];
    }
}
static void form_strip(uint32_t strip, uint32_t boot)           /* draw 60 rows of the frame */
{
    uint32_t i, y0 = strip * 60u, t = boot_age;
    cv_begin(240, 60, C_BLACK);
    cv_oy = -(int32_t)y0;
    for (i = 0; i < NP; i++) {                                                                                       /* the lines between neighbours: the structure */
        uint32_t j = (i + 1u) % NP;
        int32_t sp = (pq_vx[i] < 0 ? -pq_vx[i] : pq_vx[i]) + (pq_vy[i] < 0 ? -pq_vy[i] : pq_vy[i]);
        if (!boot && form_shape == 3u) {
            if (i % 6u != 5u) cv_line(pq_x[i] >> 8, pq_y[i] >> 8, pq_x[i + 1u] >> 8, pq_y[i + 1u] >> 8, dimc(col_mix, 60));
            if (i + 6u < NP) cv_line(pq_x[i] >> 8, pq_y[i] >> 8, pq_x[i + 6u] >> 8, pq_y[i + 6u] >> 8, dimc(col_mix, 60));
            continue;
        }
        if (!boot && ((form_shape == 4u && j == 0u) || (form_shape == 1u && (i == NP / 2u - 1u || j == 0u)))) continue;
        cv_line(pq_x[i] >> 8, pq_y[i] >> 8, pq_x[j] >> 8, pq_y[j] >> 8, dimc(col_mix, sp > 3000 ? 40u : 90u));
    }
    for (i = 0; i < NP; i++) {                                                                                       /* the particles: A's colour for half, B's for the rest; bright when quick */
        int32_t sp = (pq_vx[i] < 0 ? -pq_vx[i] : pq_vx[i]) + (pq_vy[i] < 0 ? -pq_vy[i] : pq_vy[i]), br = 150 + (sp >> 4);
        uint16_t c = i < NP / 2u ? C_A : C_B;
        cv_rect((pq_x[i] >> 8) - 1, (pq_y[i] >> 8) - 1, 3, 3, dimc(c, br > 255 ? 255u : (uint32_t)br));
    }
    if (boot) {                                                                                                      /* the name, a letter at a time, then the version */
        static const char NAME[] = "FORMATION";
        uint32_t n = t > 300u ? (t - 300u) / 90u : 0u, k, x = (240u - (uint32_t)text_w(&FONT_L, NAME)) / 2u;
        for (k = 0; k < 9u && k < n; k++) {
            char one[2];
            one[0] = NAME[k]; one[1] = 0;
            cv_text((int32_t)x, 104, &FONT_L, one, k < 5u ? C_A : C_B);
            x += (uint32_t)text_w(&FONT_L, one);
        }
        if (t > 1300u) cv_text((240 - text_w(&FONT_S, "v" APP_VER)) / 2, 148, &FONT_S, "v" APP_VER, dimc(pal[4], t > 1700u ? 255u : (t - 1300u) * 255u / 400u));
    }
    cv_oy = 0;
    cv_blit(0, y0);
}
static void form_job(uint32_t boot)             /* one call, one strip (a new frame every 50 ms) */
{
    static uint32_t frame_t, strip = 4;
    if (strip >= 4u) {
        if (fm1_ms - frame_t < 50u) return;
        frame_t = fm1_ms; form_step(boot); strip = 0;
    }
    form_strip(strip, boot);
    strip++;
}

/* ---- the page transition (a page slides in from the side and brightens, in four frames of 50 ms) and the value pulse (a value you turn flashes white and settles) */
static int32_t tr_pg = -1, tr_dir = 1, pulse_half = -1, pulse_i;
static uint32_t tr_t0, pulse_t0;
static const int8_t TR_OX[5] = {36, 22, 10, 3, 0};
static const uint8_t TR_DIM[5] = {110, 145, 180, 215, 255};
static uint32_t pulse_age(int32_t hi, int lit)  /* ms since the value last turned (300 or more: not pulsing) */
{
    uint32_t age = fm1_ms - pulse_t0;
    return (lit && hi == pulse_half && age < 300u) ? age : 300u;
}

static void draw_panel(int32_t hi, uint32_t y, int lit)
{
    uint32_t i;
    uint16_t gc = hi >= 0 ? GCOL(HALF[hi].group) : pal[1], fc = lit ? gc : dimc(gc, 70);
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
            uint16_t cc;
            cell_text(HALF[hi].c[i], name, val, &bar, &scn);
            cc = scn == 1u ? C_A : scn == 2u ? C_B : gc;                       /* a cell of a scene: that scene's colour */
            cv_text(x + 3, 4, &FONT_S, name, lit ? cc : dimc(cc, 90));
            vf = text_w(&FONT_L, val) <= 52 ? &FONT_L : &FONT_S;
            vy = vf == &FONT_L ? 20 : 28;
            {
                uint32_t pa = (int32_t)i == pulse_i ? pulse_age(hi, lit) : 300u, k = 255u - pa * 255u / 300u;                  /* the value you just turned: white, settling */
                cv_text(x + (52 - text_w(vf, val)) / 2, vy, vf, val, lit ? (pa < 300u ? mixc(pal[4], C_WHITE, k) : pal[4]) : dimc(pal[4], 120));
                if (bar >= 0) {
                    cv_rect(x + 3, 56, 46, 3, dimc(cc, 50));
                    cv_rect(x + 3, 56, bar * 46 / 255, 3, lit ? (pa < 300u ? mixc(cc, C_WHITE, k / 2u) : cc) : dimc(cc, 90));
                    if (pa < 300u) cv_rect(x + 3, 52, 46, 2, dimc(cc, k));
                }
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
    uint32_t i, h = (lit ? 99u : 7u) + (uint32_t)fxs.cmode * 1000u + (uint32_t)glob.theme * 17u;     /* (a new colour mode or theme draws it again) */
    if (hi < 0) return h + 1u;
    if (HALF[hi].c[0].kind == K_WAVE) {         /* an LFO's wave: its settings, and a new picture every 70 ms while it moves */
        uint32_t l = HALF[hi].c[0].arg % 6u;
        h = h * 31u + cur.lfo_rate[l] + 16u * cur.lfo_depth[l] + 4096u * cur.lfo_sp[l] + l * 77u;
        return h + (cur.lfo_rate[l] ? fm1_ms / 70u : 0u);
    }
    for (i = 0; i < 4u; i++) {
        char name[12], val[16];
        int32_t bar;
        uint32_t scn;
        cell_text(HALF[hi].c[i], name, val, &bar, &scn);
        h = h * 31u + sig_str(name) + sig_str(val) * 7u + (uint32_t)(bar + 1);
    }
    { uint32_t pa = pulse_age(hi, lit); if (pa < 300u) h = h * 31u + 1u + pa / 60u; }     /* (a new picture for each step of the pulse) */
    return h;
}

static uint32_t t_anim, t_rows, t_tower;
static char head_cache[8], pg_head[40];
static uint32_t foot_sig, morph_sig, slots_sig, pg_sig[2];

static uint32_t sig_of(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    while (n--) { h ^= *b++; h *= 16777619u; }
    return h;
}

/* one draw job a call: so a note due in the main loop never waits for more than one strip of the screen */
static void trans_on(uint32_t tf) { cv_ox = tr_dir * (int32_t)TR_OX[tf > 4u ? 4u : tf]; cv_dim = TR_DIM[tf > 4u ? 4u : tf]; }
static void trans_off(void) { cv_ox = 0; cv_dim = 255; }
static void draw_tick(void)
{
    uint32_t i;
    if (!boot_done) {                           /* the boot logo: the particles gather in a ring round the name */
        static uint32_t b0;
        static uint8_t started;
        if (!started) { started = 1; b0 = fm1_ms; }
        boot_age = fm1_ms - b0;
        if (boot_age >= BOOT_MS) { boot_done = 1; view_drawn = 255; return; }
        form_job(1);
        return;
    }
    if (view != view_drawn) {                   /* a new screen: clear it, the rest follows strip by strip */
        lcd_fill(0, 0, 240, 240, C_BLACK);
        view_drawn = view;
        for (i = 0; i < 5u; i++) rows_cache[i][0] = 1;
        head_cache[0] = 0x7F; pg_head[0] = 1;                 /* (a value no header has: it is drawn again) */
        foot_sig = 0; morph_sig = 0; slots_sig = 0; pg_sig[0] = pg_sig[1] = 0; help_drawn = 0;
        t_anim = 0; tr_pg = -1;
        if (view == V_IDLE) { form_t0 = fm1_ms; form_init = 0; }
        if (view == V_SCALE || view == V_COPY || view == V_CHAIN || view == V_SYS || view == V_SYS2) draw_text_box(0, 18, 240, &FONT_L, view_title(), C_A, 1);
        return;
    }
    if (view == V_IDLE) { form_job(0); return; }
    if (view == V_PERF || view == V_PAGE) {
        uint32_t m = sig_of(&cur.morph_pos, 1) ^ ((uint32_t)edit_sc << 8) ^ ((uint32_t)mute_mask << 12) ^ sig_of(cur.lfo_rate, 12) ^ ((uint32_t)glob.theme << 20);
        uint32_t f = sig_of(&cur.density, 2) ^ ((uint32_t)cur.oct << 16) ^ ((uint32_t)glob.theme << 24);
        f = f * 3u + (bt_state() == BT_READY);
        f = f * 3u + xc_follow;
        f = f * 5u + (running && dsp_pct >= 60u ? (dsp_pct >= 80u ? 2u : 1u) : 0u);
        f = f * 31u + (fm1_ms < hud_until ? sig_str(hud_name) + sig_str(hud_val) * 3u + hud_scn + 1u : 0u);   /* a message shows and goes */
        if (view == V_PERF) {
            char hs[8];
            hs[0] = (char)(edit_sc + 1u); hs[1] = (char)(cur.mode + 1u); hs[2] = (char)(latch + 1u);
            hs[3] = (char)(cur.poly + 1u + 2u * auto_roll + 4u * rec_state + 12u * chain_on + 24u * cur.dr_on); hs[4] = (char)(frozen + 1u + 2u * (bpm_get(&cur) >> 8)); hs[5] = (char)(bpm_get(&cur) & 255u);
            hs[6] = (char)(1u + glob.theme); hs[7] = 0;
            if (memcmp(head_cache, hs, 7) != 0) { memcpy(head_cache, hs, 8); draw_head(); return; }
        } else {
            int32_t lit = cur_half, top = lit - HALF[lit].pos;
            int32_t bot = (top + 1 < NHALF && HALF[top + 1].group == HALF[top].group && HALF[top + 1].pos == 1) ? top + 1 : -1;
            char hs[40], *p;
            uint32_t s0, s1, tf;
            int32_t pgid = (int32_t)HALF[lit].group * 16 + page_of(lit);
            if (pgid != tr_pg) { tr_dir = (tr_pg < 0 || pgid > tr_pg) ? 1 : -1; tr_pg = pgid; tr_t0 = fm1_ms; }       /* a new page slides in */
            tf = fm1_ms - tr_t0 < 200u ? (fm1_ms - tr_t0) / 50u : 4u;
            memset(hs, 0, sizeof hs);
            p = p_str(hs, GROUP_NAME[HALF[lit].group]);
            p = p_str(p, "  ");
            p_str(p, HALF[lit].tab);
            hs[36] = (char)pages_in(HALF[lit].group); hs[37] = (char)(1 + page_of(lit)); hs[38] = (char)(1 + HALF[lit].group);
            hs[39] = (char)(1 + glob.theme);                                    /* (a new theme draws the header again) */
            if (memcmp(pg_head, hs, sizeof hs) != 0) {
                memcpy(pg_head, hs, sizeof hs);
                draw_text_box(8, 2, 140, &FONT_S, hs, GCOL(HALF[lit].group), 0);
                draw_dots(HALF[lit].group, lit);
                return;
            }
            s0 = panel_sig(top, HALF[lit].pos == 0) + (tf < 4u ? (tf + 1u) * 100003u : 0u);
            if (s0 != pg_sig[0]) {
                pg_sig[0] = s0;
                trans_on(tf);
                if (HALF[top].c[0].kind == K_WAVE) draw_wave(top, 22, HALF[lit].pos == 0); else draw_panel(top, 22, HALF[lit].pos == 0);
                trans_off();
                return;
            }
            s1 = panel_sig(bot, HALF[lit].pos == 1) + (tf < 4u ? (tf + 1u) * 100003u : 0u);
            if (s1 != pg_sig[1]) {
                pg_sig[1] = s1;
                if (bot < 0) lcd_fill(0, 92, 240, 64, C_BLACK);
                else { trans_on(tf); draw_panel(bot, 92, HALF[lit].pos == 1); trans_off(); }
                return;
            }
        }
        { uint32_t hs = (fxs.hint && (int32_t)(help_until - fm1_ms) > 0) ? help_gen * 2u + 1u : 0u;    /* the beginner text takes the bottom third while it is on */
          if (hs != help_drawn) { help_drawn = hs; if (hs) draw_help(); else { lcd_fill(0, 162, 240, 78, C_BLACK); morph_sig = 0; foot_sig = 0; } return; }
          if (!hs) {
              if (m != morph_sig) { morph_sig = m; draw_morph(); return; }
              if (f != foot_sig) { foot_sig = f; draw_foot(); return; }
          } }
        if (view == V_PERF) {
            if (fm1_ms - t_anim >= 70u) { t_anim = fm1_ms; gyaw += 0x01400000u; draw_globe_hud(); return; }
            if (fm1_ms - t_tower >= 45u) { t_tower = fm1_ms; draw_towers(); return; }
        }
        return;
    }
    if (view == V_SLOTS) {
        uint32_t m = (uint32_t)glob.slot ^ ((uint32_t)save_mode << 8);
        for (i = 0; i < 16u; i++) m = m * 31u + bank.s[i].model;
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
    switch (copy_act % 6u) {
    case 0: cur.sc[1] = cur.sc[0]; hud("COPIED", "A TO B", -1); flash(KEY_LED(BLACK_KEY[8]), 3); flash(KEY_LED(BLACK_KEY[9]), 3); break;
    case 1: cur.sc[0] = cur.sc[1]; hud("COPIED", "B TO A", -1); flash(KEY_LED(BLACK_KEY[8]), 3); flash(KEY_LED(BLACK_KEY[9]), 3); break;
    case 2: scene_init(&cur.sc[edit_sc], (int)edit_sc); hud("INIT", edit_sc ? "SCENE B" : "SCENE A", -1); flash(KEY_LED(BLACK_KEY[8 + edit_sc]), 3); break;
    case 3: preset_init(&cur); mot_on = 0; hud("INIT", "ALL", -1); flash(KEY_LED(BLACK_KEY[8]), 3); flash(KEY_LED(BLACK_KEY[9]), 3); break;
    case 4:                                                                       /* one saved slot: emptied */
        bank_open();
        memset(&bank.s[copy_slot & 15u], 0, sizeof bank.s[0]);
        bank_save();
        hud_num("INIT SLOT", (int32_t)(copy_slot & 15u) + 1, 0);
        flash(KEY_LED(WHITE_KEY[copy_slot & 15u]), 3);
        break;
    default:                                                                      /* all sixteen */
        bank_open();
        memset(bank.s, 0, sizeof bank.s);
        bank_save();
        hud("INIT", "ALL SLOTS", -1);
        break;
    }
    flash(BTN_SAVE, 3);
}

static void save_slot(uint32_t slot)
{
    bank_open();
    cur.used |= 1u;                             /* (the other bits of used are the tempo's) */
    slot_put((uint32_t)slot, MODEL_FORMATION, &cur, sizeof cur);
    glob.slot = (uint8_t)slot;
    bank_save();
    flash(KEY_LED(WHITE_KEY[slot]), 1);
    flash(BTN_SAVE, 1);
}

static void select_view(void)
{
    uint8_t v = V_PERF;
    if (save_mode || recall_mode) v = V_SLOTS;
    else if (tr_view) v = tr_view;
    else if (cur_half >= 0) v = V_PAGE;
    else if (idle_on) v = V_IDLE;
    view = v;
}

typedef struct { preset16_t cur; uint8_t ver, theme, ch0, ch1, slot; } saved_t;   /* the record up to v1.0 (85 B) */
typedef struct { saved_t s; uint8_t bt[BT_KEPT]; } saved2_t;                         /* + the radio's kept bytes (225 B) */
typedef struct { saved_t s; uint8_t bt[BT_KEPT]; uint8_t fx[4]; } saved3_t;               /* + delay time, feedback, tone, sync (229 B) */
typedef struct { saved_t s; uint8_t bt[BT_KEPT]; uint8_t fx[8]; } saved4_t;               /* + colour, meter, CC / program change in, the pans (233 B): v1.6 */
typedef struct { saved_t s; uint8_t bt[112]; } saved1b_t;                            /* the record of the first Bluetooth builds (197 B) */
typedef struct { preset17_t cur; uint8_t ver, theme, ch0, ch1, slot; uint8_t bt[BT_KEPT]; uint8_t fx[8]; uint8_t chain[CHAIN_N]; } saved5_t;   /* v1.7 */
typedef struct { preset_t cur; uint8_t ver, theme, ch0, ch1, slot; uint8_t bt[BT_KEPT]; uint8_t fx[8]; uint8_t chain[CHAIN_N]; uint8_t ch2, spare; } saved6_t;   /* v1.8: + the drums' MIDI channel */
typedef struct {
    uint8_t ver, theme, ch0, ch1, ch2, slot, model, spare;
    uint8_t bt[BT_KEPT]; uint8_t fx[8]; uint8_t chain[CHAIN_N];
    preset_t f;                                 /* each model's working state: FORMATION, */
    lanes_t l;                                  /* LANES, */
    drift_t d;                                  /* DRIFT */
} saved8_t;                                     /* v1.9 (~660 B: the settings buffer is 1024) */

static void leds(uint32_t lit_notes)
{
    uint32_t i, blink = (fm1_ms >> 8) & 1u, slots = save_mode || recall_mode;
    int32_t g = cur_half >= 0 ? (int32_t)HALF[cur_half].group : -1;
    led(BTN_ARP, cur.mode == MODE_ARP);
    led(BTN_SEQ, cur.mode == MODE_SEQ);
    led(BTN_SEL, frozen);
    led(BTN_REC, rec_state == 2u ? 1 : rec_state == 1u ? (int)blink : 0);
    led(BTN_ENV, g == G_ENV);
    led(BTN_PLAY, running);
    led(BTN_EDIT, auto_roll);
    led(BTN_FX, g == G_FX);
    led(BTN_LFO, g == G_LFO);
    led(BTN_GLO, g == G_GLO);
    led(BTN_HOME, g < 0 || g == G_HOME || g == G_DRUM);
    led(BTN_SAVE, save_mode ? blink : recall_mode);
    led(BTN_OCT_DOWN, cur.oct < 0);
    led(BTN_OCT_UP, cur.oct > 0);
    for (i = 0; i < 16u; i++) {
        int on = slots ? (slot_used(i) && (i != glob.slot || blink)) : (int)((lit_notes >> WHITE_KEY[i]) & 1u);
        led(KEY_LED(WHITE_KEY[i]), on);
    }
    for (i = 0; i < 8u; i++) {
        int on = running && cstep == i ? 1 : (cur.sc[edit_sc].fader[i] > 127u) && !((mute_mask >> i) & 1u);
        led(KEY_LED(BLACK_KEY[i]), on);
    }
    led(KEY_LED(BLACK_KEY[8]), edit_sc == 0);
    led(KEY_LED(BLACK_KEY[9]), edit_sc == 1);
    led(KEY_LED(BLACK_KEY[10]), mute_mask != 0);
}


/* the global settings kept in the record's fx bytes: 4 of them in the older records, 8 now. The delay's three (bits 0..1 of byte 0, bytes 1 and 2) are
 * the delay of v1.0 .. v1.6 (one for both scenes): kept as they were, for the presets of that time that are converted when they are read (leg_dly) */
static void fx_load(const uint8_t *f, uint32_t n)
{
    leg_dly[0] = f[0] & 3u; leg_dly[1] = f[1] > 230u ? 230u : f[1]; leg_dly[2] = f[2];
    fxs.amode = (f[0] >> 2) & 3u; fxs.ashp = (f[0] >> 4) & 1u; fxs.abars = (f[0] >> 5) & 7u;
    if (fxs.abars > 6u || !(f[0] & ~3u)) fxs.abars = 2;               /* (a record from before AUTO: its default, 4 bars) */
    fxs.sync = (f[3] & 3u) > 2u ? 2u : (f[3] & 3u); fxs.chord = (f[3] >> 2) & 3u; fxs.voic = (f[3] >> 4) & 3u; if (fxs.voic > 2u) fxs.voic = 0;
    fxs.csrc = (f[3] >> 6) & 3u; if (fxs.csrc > 2u) fxs.csrc = 0;
    if (n >= 8u) {
        fxs.cmode = f[4] & 3u; if (fxs.cmode > 2u) fxs.cmode = 1;
        fxs.meter = (f[4] >> 2) & 1u; fxs.ccin = (f[4] >> 3) & 1u; fxs.pcin = (f[4] >> 4) & 1u;
        fxs.pana = f[5]; fxs.panb = f[6];
        fxs.hint = f[7] & 1u; fxs.recm = (f[7] >> 1) & 1u;
        { uint32_t v = (f[7] >> 2) & 7u; fxs.idle = v ? (uint8_t)(v - 1u) : 2u; if (fxs.idle > 3u) fxs.idle = 2; }     /* (stored plus one: 0 is a record from before it, and gets the default) */
    }
}
static void fx_save(uint8_t *f)
{
    f[0] = (uint8_t)(leg_dly[0] | fxs.amode << 2 | fxs.ashp << 4 | fxs.abars << 5);
    f[1] = leg_dly[1]; f[2] = leg_dly[2];
    f[3] = (uint8_t)(fxs.sync | fxs.chord << 2 | fxs.voic << 4 | fxs.csrc << 6);
    f[4] = (uint8_t)(fxs.cmode | fxs.meter << 2 | fxs.ccin << 3 | fxs.pcin << 4);
    f[5] = fxs.pana; f[6] = fxs.panb; f[7] = (uint8_t)(fxs.hint | fxs.recm << 1 | (fxs.idle + 1u) << 2);
}

static saved8_t sv;

static void app_init(void)
{
    uint32_t i;
    preset_init(&cur);
    theme_set(0);
    globe_init();
    rng_s ^= fm1_ticks();
    if (!rng_s) rng_s = 1;
    memset(&sv, 0, sizeof sv);
    lanes_init(&sv.l); drift_init(&sv.d);                       /* (the states of the models that an older record does not have) */
    sv.model = MODEL_FORMATION;
    if (settings_load(&sv, sizeof sv) == 0) { fx_load(sv.fx, 8); memcpy(chain, sv.chain, CHAIN_N); glob.ch[2] = sv.ch2 & 15u; }
    else {                                                      /* an older record (v1.0 .. v1.8): its preset has an older layout, and fewer bytes follow it */
        saved6_t o6;
        saved5_t o5;
        saved4_t o4;
        saved3_t o3;
        saved2_t o2;
        saved1b_t o1;
        saved_t o0;
        const saved_t *os = 0;
        memset(&sv, 0, sizeof sv);
        lanes_init(&sv.l); drift_init(&sv.d);
        sv.model = MODEL_FORMATION;
        if (settings_load(&o6, sizeof o6) == 0) {                 /* v1.8: the same preset, in a smaller record */
            fx_load(o6.fx, 8); memcpy(chain, o6.chain, CHAIN_N); memcpy(sv.bt, o6.bt, sizeof o6.bt);
            sv.f = o6.cur;
            sv.ver = o6.ver; sv.theme = o6.theme; sv.ch0 = o6.ch0; sv.ch1 = o6.ch1; sv.slot = o6.slot; sv.ch2 = o6.ch2;
        }
        else if (settings_load(&o5, sizeof o5) == 0) {            /* v1.7: only the preset's layout differs */
            fx_load(o5.fx, 8); memcpy(chain, o5.chain, CHAIN_N); memcpy(sv.bt, o5.bt, sizeof o5.bt);
            preset_from17(&sv.f, &o5.cur);
            sv.ver = o5.ver; sv.theme = o5.theme; sv.ch0 = o5.ch0; sv.ch1 = o5.ch1; sv.slot = o5.slot; sv.ch2 = 9;
        }
        else if (settings_load(&o4, sizeof o4) == 0) { os = &o4.s; memcpy(sv.bt, o4.bt, sizeof o4.bt); fx_load(o4.fx, 8); }
        else if (settings_load(&o3, sizeof o3) == 0) { os = &o3.s; memcpy(sv.bt, o3.bt, sizeof o3.bt); fx_load(o3.fx, 4); }
        else if (settings_load(&o2, sizeof o2) == 0) { os = &o2.s; memcpy(sv.bt, o2.bt, sizeof o2.bt); }          /* (no delay, sync ...) */
        else if (settings_load(&o1, sizeof o1) == 0) { os = &o1.s; memcpy(sv.bt, o1.bt, sizeof o1.bt); }          /* (no pairing) */
        else if (settings_load(&o0, sizeof o0) == 0) os = &o0;                                                     /* (no radio bytes either) */
        if (os && os->ver == 1u) {
            preset_from16(&sv.f, &os->cur);
            sv.ver = 1; sv.theme = os->theme; sv.ch0 = os->ch0; sv.ch1 = os->ch1; sv.slot = os->slot; sv.ch2 = 9;
        }
    }
    bt_init(sv.bt);
    if (sv.ver == 1u) {
        cur = sv.f; glob.theme = sv.theme % NTHEME; glob.ch[0] = sv.ch0 & 15u; glob.ch[1] = sv.ch1 & 15u; glob.ch[2] = sv.ch2 & 15u; glob.slot = sv.slot & 15u;
        bpm_set(&cur, (int32_t)bpm_get(&cur));
        cur.div &= 3u; cur.scale %= 10u; cur.root %= 12u; cur.oct = (int8_t)clampi(cur.oct, -3, 3);
        cur.tr = (int8_t)clampi(cur.tr, -12, 12);
        for (i = 0; i < 6u; i++) if (cur.lfo_rate[i] > 8u) cur.lfo_rate[i] = 0;
        for (i = 0; i < 2u; i++) { cur.sc[i].dtime &= 3u; if (cur.sc[i].dfb > 230u) cur.sc[i].dfb = 230u; }
        theme_set(glob.theme);
        glob.model = (sv.model >= MODEL_FORMATION && sv.model <= MODEL_DRIFT) ? sv.model : MODEL_FORMATION;
        lanes_cur = sv.l; drift_cur = sv.d;
    } else { lanes_init(&lanes_cur); drift_init(&drift_cur); }
    colours_update();
    weights();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    touch_ms = fm1_ms;
}

/* ---- MIDI control change and program change in (CC IN and PROG IN, on the MIDI IN LOOK page). Any channel. Control changes, 0..127 over the range:
 *   8 crossfade A-B, 12 density, 13 swing, 20..25 scene A MORPH REST LEGATO ENTROPY HARMONY CHAOS, 26..31 the same for scene B,
 *   70 / 71 timbre A / B, 72 / 73 level A / B, 74 / 75 delay send A / B, 85 / 86 reverb send A / B, 87 reverb size, 88 reverb bright, 90 / 91 pan A / B.
 * A program change 0..15 recalls that preset (at the end of the measure while it plays). -> 1 when something changed */
/* TAP TEMPO: on the GLO TEMPO KEY page (the one with BPM) the SEL button taps the tempo, from the last four taps; a pause of two seconds starts again */
static uint32_t tap_t[4], tap_n;
static void tap_tempo(void)
{
    uint32_t now = fm1_ms, i, sum = 0, cnt;
    if (tap_n && now - tap_t[(tap_n - 1u) & 3u] > 2000u) tap_n = 0;
    tap_t[tap_n & 3u] = now;
    tap_n++;
    if (tap_n < 2u) return;
    cnt = tap_n > 4u ? 3u : tap_n - 1u;                                     /* the intervals among the last (up to) four taps */
    for (i = 0; i < cnt; i++) sum += tap_t[(tap_n - 1u - i) & 3u] - tap_t[(tap_n - 2u - i) & 3u];
    if (!sum) return;
    bpm_set(&cur, (int32_t)((60000u * cnt + sum / 2u) / sum));
    hud("TAP", "", -1);
    { char *q = p_num(hud_val, bpm_get(&cur)); q = p_str(q, " BPM"); *q = 0; }
}

static int cc_in(uint32_t cc, uint32_t v)
{
    uint32_t v8 = (v * 255u + 63u) / 127u;
    if (v8 > 255u) v8 = 255u;
    if (cc == 8u) cur.morph_pos = (uint8_t)v8;
    else if (cc == 12u) cur.density = (uint8_t)v8;
    else if (cc == 13u) cur.swing = (uint8_t)v8;
    else if (cc >= 20u && cc <= 31u) cur.sc[(cc - 20u) / 6u].p[(cc - 20u) % 6u] = (uint8_t)v8;
    else if (cc == 70u || cc == 71u) cur.sc[cc - 70u].timbre = (uint8_t)v8;
    else if (cc == 72u || cc == 73u) cur.sc[cc - 72u].level = (uint8_t)v8;
    else if (cc == 74u || cc == 75u) cur.sc[cc - 74u].dsend = (uint8_t)v8;
    else if (cc == 85u || cc == 86u) cur.sc[cc - 85u].rsend = (uint8_t)v8;
    else if (cc == 87u) cur.sc[0].rsize = cur.sc[1].rsize = (uint8_t)v8;                 /* the size and the tone: both voices */
    else if (cc == 88u) cur.sc[0].rtone = cur.sc[1].rtone = (uint8_t)v8;
    else if (cc == 90u) fxs.pana = (uint8_t)v8;
    else if (cc == 91u) fxs.panb = (uint8_t)v8;
    else if (cc == 94u) cur.dr_level = (uint8_t)v8;                                       /* the drums: level, shadow and space (both scenes) */
    else if (cc == 95u) cur.dr_shadow[0] = cur.dr_shadow[1] = (uint8_t)v8;
    else if (cc == 96u) cur.dr_space[0] = cur.dr_space[1] = (uint8_t)v8;
    else return 0;
    return 1;
}
static void pc_in(uint32_t prog)
{
    bank_open();
    if (prog > 15u || !slot_used(prog)) return;
    if (running) pend_slot = (int32_t)prog; else recall_apply((int32_t)prog);
    hud("PROGRAM", "", -1);
    { char *q = p_num(hud_val, prog + 1u); *q = 0; }
}

static void do_panic(void)                      /* PLAY held, then OCT- and OCT+ together: everything stops, the notes, the echoes and the reverb */
{
    uint32_t i;
    if (running) transport(0);
    offs_run(0, 1);
    voices_off();
    for (i = 0; i < 2u; i++) { midi_send(0xB0u | glob.ch[i], 123u, 0); midi_send(0xB0u | glob.ch[i], 120u, 0); }     /* all notes off, all sound off */
    held_n = 0; latch = 0; latch_len = 0; first_of_chord = 1;
    freeze_set(0);
    rec_state = 0; chain_on = 0; mot_on = 0;
    kill_until = fm1_ms + 1500u;
    hud("PANIC", "ALL STOPPED", -1);
}

static int cell_fast(uint32_t k)                /* the cells that take bigger steps when the knob is turned fast */
{
    return k == K_P || k == K_DENS || k == K_SWING || k == K_LDEP || k == K_TIMB || k == K_DLY || k == K_LVL || k == K_BPM || k == K_RSND || k == K_RSIZ ||
           k == K_RTON || k == K_DFB || k == K_DTN || k == K_PANA || k == K_PANB || (k >= K_ENVA && k <= K_ENVR) ||
           k == K_DLVL || k == K_DTONE || k == K_DDEC || k == K_DSHD || k == K_DSPC || k == K_DSND;
}

static void app_step(void)                      /* one pass of the main loop (a browser calls it by itself) */
{
    static uint32_t prev, dirty, t_change, save_t0, save_down, save_long, arp_t0, arp_down, arp_long, edit_t0, edit_down,
                    edit_long, t_ui, mute_t0, play_t0, play_down, play_long, panic_until, rec_t0, rec_down, rec_long, home_t0, home_down, home_long;
    static uint8_t liven[27], booted;
    uint32_t k, keys, down, up, rel, b, now, lit = 0, kk;
    uint8_t st, d1, d2;
    int32_t e0, e1, e2, e3, e4, e5, e6;
    uint32_t slots;
    platform_poll();
    if (fm1_ms < 1200u) { draw_tick(); return; }                 /* the splash: the boot logo */
    if (!booted) { booted = 1; lcd_fill(0, 0, 240, 240, C_BLACK); last_t = fm1_ticks(); }
    now = fm1_ticks();

    /* --- the clock: before anything slow */
    update_eff(now - last_t);
    last_t = now;
    offs_run(now, 0);
    ext_poll(now);
    if (running && xc_follow) {                                                     /* following an external clock: the step comes at its tick */
        if (xc_pend && (int32_t)(now - xc_step_t) >= 0) { xc_pend = 0; do_step(); auto_step(); }
    } else if (running && (int32_t)(now - next_t) >= 0) {
        uint32_t base = step_ticks(), sw = (base >> 8) * (((uint32_t)cur.swing * 115u) >> 8);
        do_step();
        auto_step();                                                  /* AUTO A-B: one step of the trip, on the clock */
        next_t += (cstep & 1u) ? base - sw : base + sw;      /* (cstep is the step that just played: even steps are late) */
        if ((int32_t)(now - next_t) > (int32_t)(base > 0x1FFFFFFFu ? 0x7FFFFFFFu : base * 4u)) next_t = now + base;   /* (base is up to 1.44e9 at 1 BPM) */
    }
    if (running && fxs.sync == 2u && !xc_follow) {                                  /* MIDI clock: 24 a beat = one every 60e6 / bpm ticks */
        uint32_t per = 60000000u / bpm_get(&cur), guard = 0;
        while ((int32_t)(now - clk_next) >= 0 && guard++ < 4u) { midi_send(0xF8u, 0, 0); clk_next += per; }
        if ((int32_t)(now - clk_next) > 0) clk_next = now + per;                    /* far behind (a stall): start the grid again */
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
                    if (slot_used(k)) {
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
            } else if (cur_half >= 0 && HALF[cur_half].group == G_DRUM && k < (uint32_t)NDR) {    /* on the drum pages the first six lower keys are soft pads */
                drum_hit(k, 84u, (k == DR_KICK ? 36u : 48u) + cur.root);
                hud(DR_NAME[k], "PAD", -1);
            } else if (cur_half >= 0 && HALF[cur_half].group == G_FX) {          /* on the FX pages the lower keys are punch-ins: momentary effects on the voice shown */
                pi_mask |= (uint16_t)(1u << k);
                hud(PI_NAME[k], pi_voice == 0u ? "A" : pi_voice == 1u ? "B" : "A+B", -1);
                hud_scn = pi_voice == 0u ? 1u : pi_voice == 1u ? 2u : 0u;
            } else {
                liven[k] = (uint8_t)clampi((int32_t)note, 0, 127);
                key_note(liven[k], 1);
            }
        }
        if (up & bit) {
            pi_mask &= (uint16_t)~(1u << k);
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
    if ((int32_t)(panic_until - fm1_ms) > 0) {                                  /* the panic prompt is up: OCT- and OCT+ together do it, and they do not change the octave */
        uint32_t both = (1u << BTN_OCT_DOWN) | (1u << BTN_OCT_UP);
        if ((fm1_in.buttons & both) == both) { do_panic(); panic_until = 0; }
        b &= ~both;
    }
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
    if (b & (1u << BTN_SEL)) { freeze_set(!frozen); hud_btn("FREEZE", frozen ? "ON" : "OFF", BTN_SEL); }
    if (b & (1u << BTN_REC)) { rec_down = 1; rec_t0 = fm1_ms; rec_long = 0; }
    if (rec_down && !rec_long && fm1_ms - rec_t0 > 600u) {                      /* long: put the pattern back as it was before the last capture */
        rec_long = 1;
        if (rec_undo.ok) { rec_restore(); hud_btn("REC", "UNDONE", BTN_REC); dirty = 1; t_change = fm1_ms; }
        else hud_btn("REC", "NOTHING TO UNDO", BTN_REC);
    }
    if (rel & (1u << BTN_REC)) {
        if (rec_down && !rec_long) {
            if (rec_state) { rec_state = 0; hud("REC", "CANCELLED", -1); }
            else { rec_state = 1; hud_btn("REC", running ? "ARMED" : "ARMED: PRESS PLAY", BTN_REC); }
        }
        rec_down = 0;
    }
    if (b & (1u << BTN_PLAY)) { transport(!running); hud_btn(running ? "PLAY" : "STOP", running ? "RUNNING" : "STOPPED", BTN_PLAY); play_down = 1; play_t0 = fm1_ms; play_long = 0; }
    if (play_down && !play_long && (fm1_in.buttons & (1u << BTN_PLAY)) && fm1_ms - play_t0 > 800u) {     /* long: the panic prompt */
        play_long = 1;
        panic_until = fm1_ms + 4000u;
        hud("PANIC?", "OCT- + OCT+", -1);
        hud_until = fm1_ms + 4000u;
    }
    if (rel & (1u << BTN_PLAY)) play_down = 0;
    if (b & (1u << BTN_EDIT)) { edit_down = 1; edit_t0 = fm1_ms; edit_long = 0; }
    if (edit_down && !edit_long && fm1_ms - edit_t0 > 600u) {                    /* long: auto re-roll each measure */
        edit_long = 1;
        auto_roll = !auto_roll;
        hud_btn("AUTO ROLL", auto_roll ? "ON" : "OFF", BTN_EDIT);
    }
    if (rel & (1u << BTN_EDIT)) {
        if (edit_down && !edit_long) { roll(); flash(KEY_LED(BLACK_KEY[8 + edit_sc]), 3); flash(BTN_EDIT, 3); hud("RANDOMISED", edit_sc ? "SCENE B" : "SCENE A", -1); hud_scn = (uint8_t)(edit_sc + 1u); dirty = 1; t_change = fm1_ms; }
        edit_down = 0;
    }
    if (b & ((1u << BTN_FX) | (1u << BTN_LFO) | (1u << BTN_GLO) | (1u << BTN_HOME) | (1u << BTN_ENV))) { tr_view = 0; menu_pg = 0; hud_until = 0; page_auto = 0; }
    if (b & (1u << BTN_FX)) goto_group(G_FX);
    if (b & (1u << BTN_ENV)) goto_group(G_ENV);
    if (b & (1u << BTN_LFO)) goto_group(G_LFO);
    if (b & (1u << BTN_GLO)) goto_group(G_GLO);
    if (b & (1u << BTN_HOME)) { home_down = 1; home_t0 = fm1_ms; home_long = 0; }
    if (home_down && !home_long && (fm1_in.buttons & (1u << BTN_HOME)) && fm1_ms - home_t0 > 600u) {    /* long: the drum pages (and out of them) */
        home_long = 1;
        save_mode = recall_mode = 0;
        goto_group(G_DRUM);
    }
    if (rel & (1u << BTN_HOME)) {
        if (home_down && !home_long) {
            save_mode = recall_mode = 0;
            if (cur_half >= 0) cur_half = -1;                                    /* from any page: back to the performance screen */
            else goto_group(G_HOME);
        }
        home_down = 0;
    }
    if (b & (1u << BTN_SAVE)) {
        if (tr_view == V_COPY && copy_act < 4u) { do_copy(); dirty = 1; t_change = fm1_ms; tr_t0 = fm1_ms; }
        else if (tr_view == V_SYS2) { tap_tempo(); dirty = 1; t_change = fm1_ms; tr_t0 = fm1_ms; }       /* SYSTEM 2: SAVE taps the tempo */
        else { save_down = 1; save_t0 = fm1_ms; save_long = 0; }
    }
    if (save_down && !save_long && fm1_ms - save_t0 > 600u) {                    /* long: recall mode, stays (in COPY INIT: the slot actions) */
        save_long = 1;
        if (tr_view == V_COPY) { do_copy(); dirty = 1; t_change = fm1_ms; tr_t0 = fm1_ms; }
        else { save_mode = 0; recall_mode = !recall_mode; bank_open(); }
    }
    if (rel & (1u << BTN_SAVE)) {
        if (save_down && !save_long && tr_view != V_COPY) {
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
    if (b | rel | down | up | (uint32_t)(e0 | e1 | e2 | e3 | e4 | e5 | e6)) { pg_t0 = fm1_ms; touch_ms = fm1_ms; idle_on = 0; }      /* a touch: the page stays another 10 s, the formation goes */
    if (e0) {
        int32_t n = e0 < 0 ? -e0 : e0, dir = e0 > 0 ? 1 : -1;                    /* (a fast turn can be a few detents) */
        if (n > 8) n = 8;
        if (cur_half >= 0) { while (n--) cur_half = half_step(cur_half, dir); page_auto = 0; }        /* SELECT goes through the pages of the group */
        else { menu_pg = (uint8_t)clampi((int32_t)menu_pg + dir * n, 0, 5); tr_view = menu_pg ? (uint8_t)(V_SCALE + menu_pg - 1u) : 0; tr_t0 = fm1_ms; }   /* at home: the menus */
    }
    if (e1) { cell_t ab = {K_AB, 0}; cell_adj(ab, knob_delta(1, e1)); cell_overlay(ab); help_set(ab); dirty = 1; t_change = fm1_ms; }      /* the A-to-B crossfade, on every screen */
    if (e6) {
        cell_t ins = {K_INST, 0};
        ins.arg = edit_sc;
        cell_adj(ins, e6);
        cell_overlay(ins);
        help_set(ins);
        dirty = 1; t_change = fm1_ms;
    }
    if (e2 | e3 | e4 | e5) {
        int32_t ev[4], i;
        ev[0] = e2; ev[1] = e3; ev[2] = e4; ev[3] = e5;
        if (tr_view) {                                                          /* the menus */
            int32_t dr[4];
            for (i = 0; i < 4; i++) dr[i] = ev[i] > 0 ? 1 : -1;
            tr_t0 = fm1_ms;
            if (tr_view == V_SCALE) {
                if (e2) cur.root = (uint8_t)clampi((int32_t)cur.root + dr[0], 0, 11);
                if (e3) cur.scale = (uint8_t)clampi((int32_t)cur.scale + dr[1], 0, 9);
            } else if (tr_view == V_COPY) {
                if (e2) copy_act = (uint8_t)clampi((int32_t)copy_act + dr[0], 0, 5);
                if (e3 && copy_act == 4u) copy_slot = (uint8_t)clampi((int32_t)copy_slot + dr[1], 0, 15);
            } else if (tr_view == V_CHAIN) {
                uint8_t *c = &chain[chain_sel % CHAIN_N];
                if (e2) chain_sel = (uint8_t)clampi((int32_t)chain_sel + dr[0], 0, CHAIN_N - 1);
                c = &chain[chain_sel % CHAIN_N];
                if (e3) {
                    int32_t sl = (*c & 0x80u) ? (int32_t)(*c & 15u) + dr[1] : (dr[1] > 0 ? 0 : -1);     /* an empty step: turn right for slot 1; turn left past slot 1: empty again */
                    *c = sl < 0 ? (uint8_t)(*c & 0x70u) : (uint8_t)(0x80u | (*c & 0x70u) | (uint32_t)clampi(sl, 0, 15));
                }
                if (e4) { int32_t code = clampi((int32_t)((*c >> 4) & 7u) + dr[2], 0, 4); *c = (uint8_t)((*c & 0x8Fu) | ((uint32_t)code << 4)); }
                if (e5) { chain_on = dr[3] > 0 ? 1u : 0u; chain_pos = 255; chain_left = 0; }
            } else if (tr_view == V_SYS2) {
                if (e2) fxs.hint = dr[0] > 0 ? 1u : 0u;
                if (e3) fxs.recm = dr[1] > 0 ? 1u : 0u;
                if (e4) fxs.idle = (uint8_t)clampi((int32_t)fxs.idle + dr[2], 0, 3);
            } else {
                if (e2) { theme_set((uint32_t)((int32_t)glob.theme + (int32_t)NTHEME + dr[0]) % NTHEME); view_drawn = 255; }
                if (e3) glob.ch[0] = (uint8_t)clampi((int32_t)glob.ch[0] + dr[1], 0, 15);
                if (e4) glob.ch[1] = (uint8_t)clampi((int32_t)glob.ch[1] + dr[2], 0, 15);
                if (e5) bt_set(e5 > 0 ? 1u : 0u);                  /* turn right: ON, left: OFF */
            }
        } else {
            int32_t perf = cur_half < 0;
            int step_edit = perf && (hold_step_mask & 0xFFu) && ev[0];       /* K1 with a step key held: that step's level */
            const half_t *h;
            if (perf && !step_edit) { cur_half = 1; page_auto = 1; }           /* the performance screen turns the RHYTHM cells: they open as a page */
            if (page_auto) pg_t0 = fm1_ms;
            h = &HALF[cur_half];
            for (i = 0; i < 4; i++) {
                cell_t c = h->c[i];
                if (!ev[i] || c.kind == K_NONE || c.kind == K_WAVE) continue;
                if (step_edit && i == 0) {
                    uint32_t first = 8;
                    for (kk = 0; kk < 8u; kk++)
                        if (hold_step_mask >> kk & 1u) { adj(&cur.sc[edit_sc].fader[kk], knob_delta(2, ev[i]), 3); if (first > 7u) first = kk; }
                    hud_pct("STEP LEVEL", cur.sc[edit_sc].fader[first & 7u]);
                    hud_scn = (uint8_t)(edit_sc + 1u);
                } else {
                    cell_adj(c, cell_fast(c.kind) ? knob_delta((uint32_t)(2 + i), ev[i]) : ev[i]);
                    help_set(c);
                    pulse_half = cur_half; pulse_i = i; pulse_t0 = fm1_ms;                         /* (the value flashes) */
                    if (cur_half < 0) cell_overlay(c);                                      /* (a page shows its values itself) */
                }
            }
        }
        dirty = 1; t_change = fm1_ms;
    }

    /* --- MIDI in: notes play like the keys, any channel */
    while (midi_get(&st, &d1, &d2)) {
        if ((st & 0xF0u) == 0x90u && d2) key_note(d1, 1);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) key_note(d1, 0);
        else if ((st & 0xF0u) == 0xB0u && fxs.ccin) { if (cc_in(d1, d2)) { dirty = 1; t_change = fm1_ms; } }
        else if ((st & 0xF0u) == 0xC0u && fxs.pcin) pc_in(d1);
    }

    /* --- bookkeeping: the screens that leave by themselves, the sound's settings, the screen */
    if (tr_view && fm1_ms - tr_t0 > 2500u) { tr_view = 0; menu_pg = 0; }
    if (page_auto && fm1_ms - pg_t0 > 1800u) { page_auto = 0; if (cur_half == 1) cur_half = -1; }   /* the page a knob opened closes by itself */
    if (cur_half >= 0 && !page_auto && fm1_ms - pg_t0 > 10000u) cur_half = -1;                      /* a page you have left alone for 10 s: back to the performance screen */
    idle_on = (fxs.idle && !tr_view && cur_half < 0 && !save_mode && !recall_mode && boot_done && fm1_ms - touch_ms > IDLE_MS[fxs.idle & 3u]) ? 1u : 0u;      /* nothing touched for a while: the formation screen */
    if (!(cur_half >= 0 && HALF[cur_half].group == G_FX)) pi_mask = 0;                              /* the punch-ins are only on the FX pages */
    pi_voice = (uint8_t)half_voice(cur_half);
    if (rec_done) { rec_done = 0; hud("CAPTURED", fxs.recm ? "NOTES + MOTION" : "NOTES", -1); flash(BTN_REC, 3); dirty = 1; t_change = fm1_ms; }
    { static uint32_t t_dsp; if (fm1_ms - t_dsp >= 500u) { uint32_t m = dsp_max; dsp_max = 0; t_dsp = fm1_ms; dsp_pct = m / 1392u; } }   /* % of one 5.8 ms block = 139200 ticks; under 50 is comfortable (docs/RULES.md) */
    weights();
    colours_update();
    wob_inc = bpm_get(&cur) * 3246u;
    select_view();
    if (fm1_ms - t_ui >= 12u) { t_ui = fm1_ms; leds(lit); }
    draw_tick();

    if (bt_kept(sv.bt)) { dirty = 1; t_change = fm1_ms; }                       /* the radio's address and trims copy: saved with the settings */
    if (dirty && !busy && fm1_ms - t_change > 2000u) {                          /* save when silent: an erase stalls the sound */
        sv.f = cur; sv.l = lanes_cur; sv.d = drift_cur; sv.model = glob.model; sv.ver = 1; sv.theme = glob.theme; sv.ch0 = glob.ch[0]; sv.ch1 = glob.ch[1]; sv.ch2 = glob.ch[2]; sv.slot = glob.slot;
        fx_save(sv.fx);
        memcpy(sv.chain, chain, CHAIN_N);
        settings_save(&sv, sizeof sv);
        dirty = 0;
    }
}

static void app_main(void)
{
    app_init();
    for (;;) app_step();
}
