/* SPDX-License-Identifier: GPL-3.0-only */
/* Formation's Bluetooth LE MIDI: Optimist's stack (w0ts/optimist, GPL-3.0-only; ble_*.c, rf_share.h and
 * hal/fm1_ble.h, fm1_ble_rf.h, ble_rf_tables_v15.h) and the glue around it below, which is fm1-dock's hx.c (Joakim
 * Eriksson, GPL-3.0-only; the code that ran this stack on a real FM-1) taken out of its separate flash unit and
 * linked into the app. See firmware/src/ble/README.md for where each file comes from.
 *
 * It is included by platform.c (after usb.c and the flash code) and is part of the app's one translation unit, so
 * every name here starts with bt_ (the stack's own start with ble_, hw_, rfs_ ...).
 *
 *   out  usb.c's midi_out_event (the one place every MIDI byte the app sends passes) also calls bt_out(): while a
 *        central listens, each USB-MIDI event packet is queued with its millisecond and the stack packs the queue
 *        into notifications at the next connection event.
 *   in   the stack decodes a central's writes (in the BLE interrupts) into USB-MIDI event packets (channel messages,
 *        clock and transport); TIMER5 (bt_poll, 2 kHz) hands them to usb.c's MIDI-in ring, where midi_get() reads
 *        them as it reads USB's. A central's held notes are ended with note-offs when it goes.
 *   OFF  (the default at every power-on) the radio is never started: no BLE / RF register is written, and the two
 *        interrupts stay masked.
 *   keep the radio's factory trims come from stock's VM (flash 0xE8000, read, never written) and a copy is kept in
 *        the settings (bt_kept), together with a random static address made once, so that a computer finds the
 *        same device after a power-off. Without trims the radio is never started (state BT_NOCAL).
 *
 * Order at boot: bt_attach() in fm1_cstart, before fm1_guard_lock_top() (the vector table is write-locked after
 * it: fm1-dock's first build crashed there); bt_init() once the settings are read; bt_set() from the main loop. */

#define BLE_API static
#define BLE_DEVICE_NAME "Formation"            /* advertised as "Formation XXXX" (XXXX: the end of the address) */
#include "ble_stack.c"
#include "ble_vm.c"

#define BT_QN 64u
#define BT_ADDR_KEPT 0xA5u                      /* bt_rec[6]: bt_rec[0..5] is the address */

static uint32_t bt_in_q[BT_QN], bt_out_q[BT_QN], bt_out_ms[BT_QN];
static volatile uint32_t bt_in_w, bt_in_r, bt_out_w, bt_out_r;
static volatile uint8_t bt_out_on, bt_release;
static volatile uint32_t bt_txn;                /* packets queued for a listening central (the SYSTEM row shows it) */
static uint8_t bt_was_conn, bt_on, bt_up, bt_rf_src, bt_rec_new;
static uint8_t bt_held[16][16];                 /* the notes a central has on: a bit per note, per channel (TIMER5's) */
static uint8_t bt_rec[BT_KEPT];                 /* [0..7] the address, [8..107] the radio's trims copy */
static struct ble_rf_trims bt_use;

BLE_API void ble_app_state(void)                /* the BLE interrupts, or the main loop with them held */
{
    uint8_t c = (uint8_t)ble_connected();
    bt_out_on = (uint8_t)ble_midi_ready();
    if (!bt_out_on) {
        bt_out_r = bt_out_w;                    /* nothing waits for a central that does not listen */
        bt_txn = 0;
    }
    if (bt_was_conn && !c)
        bt_release = 1;
    bt_was_conn = c;
}

BLE_API int ble_app_midi_peek(uint32_t *pkt, uint32_t *t_ms)
{
    if (bt_out_r == bt_out_w)
        return 0;
    RING_PUBLISH();                             /* the slot is read after the index */
    *pkt = bt_out_q[bt_out_r % BT_QN];
    *t_ms = bt_out_ms[bt_out_r % BT_QN];
    return 1;
}

BLE_API void ble_app_midi_pop(void)
{
    RING_PUBLISH();
    bt_out_r++;
}

BLE_API void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    (void)ts, (void)last_ts;
    if (!bt_on || (!(cin >= 8u && cin <= 0xEu) && !(cin == 0xFu && (st == 0xF8u || (st >= 0xFAu && st <= 0xFCu)))))
        return;                                 /* channel messages, clock and transport only (as usb.c's) */
    if (bt_in_w - bt_in_r >= BT_QN)
        return;
    bt_in_q[bt_in_w % BT_QN] = pkt;
    RING_PUBLISH();
    bt_in_w++;
}

static int bt_ring_in(uint32_t pkt)             /* TIMER5: into usb.c's MIDI-in ring (what midi_get reads) -> 1 taken */
{
    if (mi_w - mi_r >= MQ)
        return 0;
    midi_in_q[mi_w % MQ] = pkt;
    RING_PUBLISH();
    mi_w++;
    return 1;
}

static void bt_note_seen(uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 15u, n = (pkt >> 16) & 0x7Fu;
    if (st == 0x90u && ((pkt >> 24) & 0x7Fu))
        bt_held[ch][n >> 3] |= (uint8_t)(1u << (n & 7u));
    else if (st == 0x80u || st == 0x90u)
        bt_held[ch][n >> 3] &= (uint8_t)~(1u << (n & 7u));
}

static void bt_notes_end(void)                  /* a note-off for each note a central left on (the next tick if full) */
{
    uint32_t ch, n, left = 0;
    for (ch = 0; ch < 16u; ch++)
        for (n = 0; n < 128u; n++) {
            if (!(bt_held[ch][n >> 3] & (1u << (n & 7u))))
                continue;
            if (bt_ring_in(8u | (0x80u | ch) << 8 | n << 16))
                bt_held[ch][n >> 3] &= (uint8_t)~(1u << (n & 7u));
            else
                left = 1;
        }
    if (!left)
        bt_release = 0;
}

static void bt_poll(void)                       /* TIMER5, 2 kHz (platform.c APP_T5_POLL) */
{
    while (bt_in_r != bt_in_w) {
        RING_PUBLISH();
        if (!bt_ring_in(bt_in_q[bt_in_r % BT_QN]))
            return;                             /* the ring is full: the next tick */
        bt_note_seen(bt_in_q[bt_in_r % BT_QN]);
        RING_PUBLISH();
        bt_in_r++;
    }
    if (bt_release)
        bt_notes_end();                         /* (after what the central had sent) */
}

static void bt_out(uint32_t pkt)                /* usb.c midi_out_event (platform.c APP_MIDI_OUT) */
{
    if (!bt_out_on || bt_out_w - bt_out_r >= BT_QN)
        return;
    bt_out_q[bt_out_w % BT_QN] = pkt;
    bt_out_ms[bt_out_w % BT_QN] = fm1_ms;
    RING_PUBLISH();
    bt_out_w++;
    bt_txn++;
}

static uint32_t bt_count(void)                  /* packets queued since the central started listening */
{
    return bt_txn;
}

static int bt_vm_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)   /* the flash as stored */
{
    (void)ctx;
    return st_read(off, dst, n);
}

/* the radio, the baseband and the stack, the first time BLUETOOTH is ON, the BLE interrupts masked -> 1 */
static int bt_radio_start(void)
{
    uint8_t a[6], rnd;
    uint32_t i;
    if (bt_up)
        return 1;
    if (bt_rf_src == BLE_RF_NONE)
        return 0;
    rnd = ble_hw_addr(a);                       /* (0: the VM's public address) */
    if (rnd && bt_rec[6] == BT_ADDR_KEPT && (bt_rec[5] >> 6) == 3u) {
        for (i = 0; i < 6u; i++)
            a[i] = bt_rec[i];
    } else if (rnd) {
        for (i = 0; i < 6u; i++)
            bt_rec[i] = a[i];
        bt_rec[6] = BT_ADDR_KEPT;
        bt_rec_new = 1;
    }
    ble_init(a, rnd);
    ble_rf_copy_get(bt_rec + 8, &bt_use);       /* (the VM's set: ble_rf_choose made the copy from it) */
    ble_hw_wl82_start(&bt_use);
    bt_up = 1;
    return 1;
}

/* once in fm1_cstart, interrupts off, before fm1_guard_lock_top(): the two BLE vectors, both masked: no BLE / RF
 * register is written */
static void bt_attach(void)
{
    ble_hw_wl82_attach();
}

/* once, after the settings are read: the radio's trims (stock's VM, else the copy in rec). Reads flash only. */
static void bt_init(const uint8_t rec[BT_KEPT])
{
    struct ble_vm_info info;
    struct ble_rf_trims vm;
    uint32_t i;
    int complete, save;
    for (i = 0; i < BT_KEPT; i++)
        bt_rec[i] = rec[i];
    complete = ble_vm_scan(bt_vm_rd, 0, &info, &vm, 0, 0);
    bt_rf_src = (uint8_t)ble_rf_choose(complete, &vm, info.area, bt_rec + 8, &bt_use, &save);
    if (save)
        bt_rec_new = 1;
    bt_on = 0;
}

static void bt_set(uint32_t want)               /* the main loop: BLUETOOTH ON / OFF (the first ON starts the radio) */
{
    want = want ? 1u : 0u;
    if (want == bt_on)
        return;
    bt_on = (uint8_t)want;
    if (bt_on && !bt_radio_start())
        return;                                 /* (no trims: only the setting) */
    if (!bt_up)
        return;
    fm1_ble_irqs_hold(1);
    ble_enable((int)bt_on);
    if (!bt_on)
        bt_release = 1;
    fm1_ble_irqs_hold(0);
}

static uint32_t bt_state(void)
{
    if (!bt_on)
        return BT_OFF;
    if (!bt_up)
        return BT_NOCAL;
    return ble_midi_ready() ? BT_READY : ble_connected() ? BT_LINK : BT_ADV;
}

static int bt_kept(uint8_t rec[BT_KEPT])        /* -> 1 when rec (the settings record) changed and should be saved */
{
    uint32_t i;
    if (!bt_rec_new)
        return 0;
    bt_rec_new = 0;
    for (i = 0; i < BT_KEPT; i++)
        rec[i] = bt_rec[i];
    return 1;
}
