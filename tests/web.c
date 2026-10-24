/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 for a web browser: the app (firmware/src/formation.c) compiled to WebAssembly runs unchanged on it.
 * platform.c includes this file instead of the hardware when DINGHY_WEB is set (with DINGHY_HOST too: the app's
 * memory-backed storage paths). Nothing runs by itself: the page (web/emulator.html) calls the emu_* functions below
 * with the time that has passed, the keys, buttons and knobs that moved, and reads back the screen, the sound, the
 * LEDs and the MIDI the FM-1 sends.
 *
 *   python3 tools/build_emu.py        (-> build/emu.wasm; tools/make_site.py puts it into emulator.html) */
#include <stdint.h>
#include <stddef.h>

#define printf(...) ((void)0)                   /* (the app's host-mode messages) */
#ifndef APP_NAME
#define APP_NAME "FORMATION"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u
#define EXPORT(n) __attribute__((export_name(n)))

#include "libc.c"                               /* memset, memcpy, memcmp: there is no libc here either */

static volatile uint32_t fm1_ms;
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return fm1_ms * 24000u; }   /* the 24 MHz counter, at 1 ms resolution here */
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges, host_rel;
static int32_t host_enc[8];
static uint32_t fm1_input_edges(uint32_t *released) { uint32_t b = host_edges; host_edges = 0; *released = host_rel; host_rel = 0; return b; }
static uint32_t fm1_input_note_edges(void) { return 0; }
static int32_t fm1_enc_take(uint32_t e) { int32_t d = host_enc[e & 7u]; host_enc[e & 7u] = 0; return d; }
static uint8_t host_led[48];
static void fm1_led_key(uint32_t id, int on) { if (id < 48u) host_led[id] = (uint8_t)on; }
static void platform_poll(void) {}              /* the page drives the clock */

/* the screen: 240 x 240 RGB565 (gfx.c sends the bytes swapped, lcd_blit swaps them back) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}
#define CV_MAX (240u * 64u)
#include "gfx.c"

/* what the FM-1 keeps: the page saves these two buffers (localStorage) and puts them back before it starts */
static uint8_t host_store[256 + 4];
static int settings_load(void *p, uint32_t n)
{
    if (host_store[0] != 1 || n > 256u) return -1;
    memcpy(p, host_store + 4, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    host_store[0] = 1;
    memcpy(host_store + 4, p, n);
    return 0;
}
static uint8_t host_pre[4096];
static int host_pre_ok;

/* MIDI: in from the page (a keyboard), out to the page */
static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    mq_r++;
    return 1;
}
static uint8_t mo[256][3];
static uint32_t mo_w, mo_r;
static uint8_t mo_tmp[4];
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t i = mo_w++ % 256u;
    mo[i][0] = (uint8_t)st; mo[i][1] = (uint8_t)d1; mo[i][2] = (uint8_t)d2;
    if (mo_w - mo_r > 256u) mo_r = mo_w - 256u;
}

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);
static void app_init(void);
static void app_step(void);
static int32_t audio_blk[2u * HALF_FRAMES];

/* ---- what the page calls */
EXPORT("emu_init") void emu_init(void) { app_init(); }
EXPORT("emu_step") void emu_step(uint32_t ms)   /* ms milliseconds go by; the main loop runs a few times in each */
{
    if (ms > 1000u) ms = 1000u;                  /* (a negative number from the page is a huge one here: never hang) */
    while (ms--) {
        uint32_t i;
        fm1_ms++;
        for (i = 0; i < 3u; i++) app_step();
    }
}
EXPORT("emu_audio") uint32_t emu_audio(void)    /* one block of 256 frames, stereo, int32 (+-32767 is full scale) */
{
    memset(audio_blk, 0, sizeof audio_blk);
    app_audio(audio_blk, HALF_FRAMES);
    return (uint32_t)(uintptr_t)audio_blk;
}
EXPORT("emu_key") void emu_key(uint32_t k, uint32_t down)
{
    if (k > 26u) return;
    if (down) fm1_in.notes |= 1u << k; else fm1_in.notes &= ~(1u << k);
}
EXPORT("emu_btn") void emu_btn(uint32_t id, uint32_t down)
{
    if (id > 13u) return;
    if (down) { fm1_in.buttons |= 1u << id; host_edges |= 1u << id; }
    else { fm1_in.buttons &= ~(1u << id); host_rel |= 1u << id; }
}
EXPORT("emu_enc") void emu_enc(uint32_t id, int32_t d) { host_enc[id & 7u] += d; }
EXPORT("emu_midi_in") void emu_midi_in(uint32_t st, uint32_t d1, uint32_t d2)
{
    mq[mq_w % 64u][0] = (uint8_t)st; mq[mq_w % 64u][1] = (uint8_t)d1; mq[mq_w % 64u][2] = (uint8_t)d2;
    mq_w++;
}
EXPORT("emu_midi_pop") uint32_t emu_midi_pop(void)   /* 1: a message is waiting at emu_midi_tmp() (status, data1, data2) */
{
    if (mo_r == mo_w) return 0;
    mo_tmp[0] = mo[mo_r % 256u][0]; mo_tmp[1] = mo[mo_r % 256u][1]; mo_tmp[2] = mo[mo_r % 256u][2];
    mo_r++;
    return 1;
}
EXPORT("emu_midi_tmp") uint32_t emu_midi_tmp(void) { return (uint32_t)(uintptr_t)mo_tmp; }
EXPORT("emu_fb") uint32_t emu_fb(void) { return (uint32_t)(uintptr_t)fb; }
EXPORT("emu_led") uint32_t emu_led(void) { return (uint32_t)(uintptr_t)host_led; }      /* 0-13 the buttons, 14+k the keys */
EXPORT("emu_store") uint32_t emu_store(void) { return (uint32_t)(uintptr_t)host_store; } /* 260 bytes */
EXPORT("emu_pre") uint32_t emu_pre(void) { return (uint32_t)(uintptr_t)host_pre; }       /* 4096 bytes */
EXPORT("emu_pre_ok") uint32_t emu_pre_ok(uint32_t set) { if (set) host_pre_ok = 1; return (uint32_t)host_pre_ok; }
