/* Formation host unit test: the tempo range (1..480 BPM, timing arithmetic) and AUTO A-B (trips, shapes, the hand). cc -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen tests/unit_auto.c */
#define main host_main_unused
#include "../firmware/src/formation.c"
#undef main
#include <assert.h>
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg); fails++;} else printf("ok   %s\n",msg);}while(0)
int main(void)
{
    uint32_t i, steps; int max=0, prev;
    preset_init(&cur); weights();
    /* tempo range */
    bpm_set(&cur, 1);   CHECK(bpm_get(&cur)==1, "BPM 1 reads back");
    bpm_set(&cur, 480); CHECK(bpm_get(&cur)==480 && cur.used==((cur.used&1u)|(1u<<1)) , "BPM 480 reads back (high bits in used)");
    bpm_set(&cur, 300); CHECK(bpm_get(&cur)==300, "BPM 300 reads back");
    bpm_set(&cur, 9999); CHECK(bpm_get(&cur)==480, "BPM clamps at 480");
    bpm_set(&cur, 0); CHECK(bpm_get(&cur)==1, "BPM clamps at 1");
    cur.used = 1; cur.bpm = 120; CHECK(bpm_get(&cur)==120, "an old preset (used 1, bpm 120) reads as before");
    { uint32_t b; for (b=1; b<=480; b+=1) { uint32_t d; for (d=0; d<4; d++) { uint64_t base, sw, add;
        bpm_set(&cur,(int32_t)b); cur.div=(uint8_t)d; base=step_ticks(); sw=(base>>8)*((255u*115u)>>8); add=base+sw;
        if (add >= 0x7FFFFFFFull || base == 0) { printf("FAIL: timing overflow at bpm %u div %u (base %llu add %llu)\n", b,d,(unsigned long long)base,(unsigned long long)add); fails++; } } } }
    bpm_set(&cur,1); cur.div=0; printf("     1 BPM, 1/4 steps: one step = %u ticks = %.1f s\n", step_ticks(), step_ticks()/24e6);
    bpm_set(&cur,480); cur.div=3; printf("     480 BPM, 1/32 steps: one step = %u ticks = %.2f ms\n", step_ticks(), step_ticks()/24e3);
    bpm_set(&cur,80); cur.div=2; frozen=0;

    /* AUTO: ONCE, 1 bar, 1/16 steps = 16 steps */
    fxs.amode=1; fxs.abars=0; fxs.ashp=0; cur.morph_pos=0; auto_arm();
    for (i=0;i<16;i++) auto_step();
    CHECK(cur.morph_pos>=254, "ONCE: A to B in 16 steps (1 bar of 16ths)");
    for (i=0;i<20;i++) auto_step();
    CHECK(cur.morph_pos==255 || cur.morph_pos==254, "ONCE: rests at B");
    auto_arm(); for (i=0;i<16;i++) auto_step();
    CHECK(cur.morph_pos<=1, "ONCE: re-armed from B goes back to A");
    /* AUTO: PING, 2 bars of 16ths = 32 steps a trip */
    fxs.amode=2; fxs.abars=1; cur.morph_pos=0; auto_arm();
    max=0; for (i=0;i<32;i++){ auto_step(); if(cur.morph_pos>max)max=cur.morph_pos; }
    CHECK(max>=254, "PING: reaches B after 32 steps (2 bars)");
    for (i=0;i<32;i++) auto_step();
    CHECK(cur.morph_pos<=2, "PING: back at A after another 32");
    /* BARS 4 at 1/4 steps = 16 steps */
    fxs.amode=1; fxs.abars=2; cur.div=0; cur.morph_pos=0; auto_arm(); for (i=0;i<16;i++) auto_step();
    CHECK(cur.morph_pos>=254, "4 bars of quarter-note steps = 16 steps");
    /* the longest: 64 bars of 1/32 steps = 2048 steps */
    cur.div=3; fxs.abars=6; cur.morph_pos=0; auto_arm(); for (i=0;i<2048;i++) auto_step();
    CHECK(cur.morph_pos>=254, "64 bars of 32nds = 2048 steps");
    /* SAW */
    cur.div=2; fxs.amode=3; fxs.abars=0; cur.morph_pos=0; auto_arm(); prev=0; max=0; int wrapped=0;
    for (i=0;i<40;i++){ auto_step(); if(cur.morph_pos<prev-100) wrapped=1; prev=cur.morph_pos; }
    CHECK(wrapped, "SAW: jumps back to A after B");
    /* EASE: monotonic up, slow at the ends */
    fxs.amode=1; fxs.abars=1; fxs.ashp=1; cur.div=2; cur.morph_pos=0; auto_arm(); prev=-1; int mono=1; int d1=0, dmid=0;
    for (i=0;i<32;i++){ int before=cur.morph_pos; auto_step(); if(cur.morph_pos<before) mono=0; if(i==0)d1=cur.morph_pos-before; if(i==15)dmid=cur.morph_pos-before; }
    CHECK(mono, "EASE: never goes backwards");
    CHECK(d1<dmid, "EASE: slower at the start than in the middle");
    /* the hand wins */
    fxs.ashp=0; fxs.amode=2; fxs.abars=1; cur.morph_pos=0; auto_arm(); for(i=0;i<8;i++) auto_step();
    cur.morph_pos=200; auto_step(); CHECK(cur.morph_pos>200 && cur.morph_pos<225, "the hand wins: carries on from 200 towards B");
    /* frozen / off */
    fxs.amode=0; cur.morph_pos=77; auto_step(); CHECK(cur.morph_pos==77, "OFF: does not move");
    fxs.amode=2; frozen=1; auto_step(); CHECK(cur.morph_pos==77, "frozen: does not move"); frozen=0;
    printf("%s\n", fails? "SOME FAILED":"ALL OK"); return fails;
}
