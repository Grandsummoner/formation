/* Formation host unit test, v1.9 milestone 1: the three models' storage (tagged slots, one working state for each model, the settings record of 1024 bytes),
 * and the older banks and records read as before. cc -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen tests/unit_v19.c */
#define main host_main_unused
#include "../firmware/src/formation.c"
#undef main
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok   %s\n", msg); } while (0)
static preset_t sp(uint32_t k) { preset_t t; memcpy(&t, bank.s[k].data, sizeof t); return t; }

int main(void)
{
    uint32_t i, k;
    /* ---- sizes */
    CHECK(sizeof(core_t) == 44 && sizeof(lanes_t) == 234 && sizeof(drift_t) == 144 && sizeof(preset_t) == 117, "the payloads: core 44 B, LANES 234 B, DRIFT 144 B, FORMATION 117 B");
    CHECK(sizeof(pslot_t) == 235 && sizeof(prebank_t) == 8 + 16 * 235 && sizeof(prebank_t) <= 4096u, "a saved slot is 235 B; the bank of 16 fits its flash sector");
    CHECK(sizeof(saved8_t) <= 1024u && sizeof(saved8_t) >= 600u, "the settings record holds the three working states and fits the 1024-byte buffer");
    CHECK(sizeof(lanes_t) <= PAY_B && sizeof(drift_t) <= PAY_B && sizeof(preset_t) <= PAY_B, "every model's payload fits a slot");

    /* ---- a v1.8 bank (PNV4: 16 FORMATION presets of 117 B) is converted in place */
    {
        static preset_t src[16];
        memset(&bank, 0, sizeof bank);
        for (k = 0; k < 16u; k++) {
            preset_init(&src[k]);
            src[k].density = (uint8_t)(10 + k); src[k].dr_shadow[1] = (uint8_t)(100 + k); src[k].sc[1].env[2] = (uint8_t)(20 + k);
            src[k].used = k % 3u == 2u ? 0u : 1u;                                   /* every third slot empty */
        }
        bank.magic = PRE_MAGIC4;
        memcpy((uint8_t *)&bank + 8, src, sizeof src);
        bank_conv(2);
        for (k = 0; k < 16u; k++) {
            preset_t t = sp(k);
            int empty = k % 3u == 2u;
            if (t.density != 10 + k || t.dr_shadow[1] != 100 + k || t.sc[1].env[2] != 20 + k) break;
            if (bank.s[k].model != (empty ? MODEL_NONE : MODEL_FORMATION) || slot_used(k) == empty) break;
        }
        CHECK(k == 16u && bank.magic == PRE_MAGIC, "a PNV4 bank: all sixteen slots converted in place, tagged FORMATION (empty ones empty)");
        bank_ok = 1;
        bank_save();
        memset(&bank, 0, sizeof bank); bank_ok = 0;
        CHECK(bank_load() == 0 && sp(4).density == 14 && bank.s[4].model == MODEL_FORMATION && !slot_used(2), "(and the new bank saves and loads)");
    }

    /* ---- slots of the other models */
    {
        lanes_t l;
        drift_t d;
        preset_t f;
        bank_ok = 1; memset(&bank, 0, sizeof bank);
        lanes_init(&l); l.d[7] = 99; l.c.bpm = 123;
        drift_init(&d); d.d[3] = 55;
        preset_init(&f); f.density = 66; f.used |= 1u;
        slot_put(0, MODEL_LANES, &l, sizeof l);
        slot_put(1, MODEL_DRIFT, &d, sizeof d);
        slot_put(2, MODEL_FORMATION, &f, sizeof f);
        bank_save(); memset(&bank, 0, sizeof bank); bank_ok = 0;
        CHECK(bank_load() == 0 && bank.s[0].model == MODEL_LANES && bank.s[1].model == MODEL_DRIFT && bank.s[2].model == MODEL_FORMATION, "slots of all three models are kept with their tags");
        { lanes_t l2; drift_t d2; memcpy(&l2, bank.s[0].data, sizeof l2); memcpy(&d2, bank.s[1].data, sizeof d2);
          CHECK(l2.d[7] == 99 && l2.c.bpm == 123 && d2.d[3] == 55 && sp(2).density == 66, "(and their payloads)"); }
        preset_init(&cur); cur.density = 3; pend_slot = -1;
        recall_apply(0);
        CHECK(cur.density == 3, "FORMATION does not recall a LANES slot");
        recall_apply(2);
        CHECK(cur.density == 66, "FORMATION recalls its own slot");
        memset(chain, 0, sizeof chain); chain[0] = 0x80u | 0u; chain_pos = 255; chain_left = 0; cur.density = 4;
        chain_measure();
        CHECK(cur.density == 4, "the chain skips a slot of another model");
        cur.density = 9; save_slot(5);
        CHECK(bank.s[5].model == MODEL_FORMATION && sp(5).density == 9 && (sp(5).used & 1u), "SAVE writes a FORMATION slot");
        copy_act = 4; copy_slot = 5; do_copy();
        CHECK(!slot_used(5), "INIT SLOT empties a tagged slot");
        copy_act = 5; do_copy();
        CHECK(!slot_used(0) && !slot_used(1) && !slot_used(2), "INIT ALL SLOTS empties them all");
    }

    /* ---- the working states of the models */
    {
        saved6_t o6;
        memset(&o6, 0, sizeof o6);
        preset_init(&o6.cur); o6.cur.density = 71;
        o6.ver = 1; o6.theme = 2; o6.ch0 = 3; o6.ch1 = 4; o6.slot = 5; o6.ch2 = 8; o6.chain[2] = 0x84;
        host_store[0] = 0;
        settings_save(&o6, sizeof o6);
        memset(chain, 0, sizeof chain); glob.model = 0;
        app_init();
        CHECK(cur.density == 71 && glob.theme == 2 && glob.ch[1] == 4 && glob.ch[2] == 8 && glob.slot == 5 && chain[2] == 0x84, "a v1.8 settings record is read as it was");
        CHECK(glob.model == MODEL_FORMATION && lanes_cur.c.v[0].inst == 1 && lanes_cur.c.bpm == 80 && drift_cur.c.v[1].inst == INST_SAW, "(it comes up as FORMATION; the other two models start from their defaults)");
        host_store[0] = 0;

        preset_init(&cur); cur.density = 12;
        lanes_cur.d[11] = 77; lanes_cur.c.swing = 40; drift_cur.d[50] = 88; glob.model = MODEL_DRIFT;
        memset(&sv, 0, sizeof sv); sv.f = cur; sv.l = lanes_cur; sv.d = drift_cur; sv.model = glob.model; sv.ver = 1; fx_save(sv.fx);
        settings_save(&sv, sizeof sv);
        cur.density = 0; lanes_init(&lanes_cur); drift_init(&drift_cur); glob.model = MODEL_FORMATION;
        app_init();
        CHECK(cur.density == 12 && lanes_cur.d[11] == 77 && lanes_cur.c.swing == 40 && drift_cur.d[50] == 88 && glob.model == MODEL_DRIFT, "the v1.9 record keeps each model's own working state and which model runs");
        host_store[0] = 0;
        memset(&sv, 0, sizeof sv); sv.f = cur; sv.model = 9; sv.ver = 1; fx_save(sv.fx);
        settings_save(&sv, sizeof sv);
        app_init();
        CHECK(glob.model == MODEL_FORMATION, "a model number that is not one falls back to FORMATION");
        host_store[0] = 0;
    }
    {
        core_t c;
        preset_t p;
        core_init(&c); preset_init(&p);
        CHECK(c.v[0].level == p.sc[0].level && c.v[1].inst == p.sc[1].inst && c.v[0].rsend == p.sc[0].rsend && c.dr_level == p.dr_level && c.bpm == p.bpm, "LANES and DRIFT start with the same voices and drums as a new FORMATION preset");
    }
    return fails ? 1 : (printf("ALL OK\n"), 0);
}
