/* Formation host unit test: every chord of every scale (chord_build). cc -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen tests/unit_chords.c */
#define main host_main_unused
#include "../firmware/src/formation.c"
#undef main
int main(void)
{
    uint32_t sc, ct, v, d, n, cnt[4][3]; memset(cnt,0,sizeof cnt);
    for (sc=0; sc<10; sc++) { preset_init(&cur); cur.scale=(uint8_t)sc; cur.root=0;
      for (ct=0; ct<4; ct++) for (v=0; v<3; v++) { fxs.chord=(uint8_t)ct; fxs.voic=(uint8_t)v;
        for (d=0; d<8; d++) for (n=3;n<=4;n++) { uint8_t pl[4], s[4]; uint32_t i,j; chord_build(60+SCALE[sc][d],n,pl);
            for(i=0;i<n;i++) s[i]=pl[i]; for(i=0;i<n;i++) for(j=i+1;j<n;j++) if(s[j]<s[i]){uint8_t t=s[i];s[i]=s[j];s[j]=t;}
            for(i=0;i+1<n;i++) if(s[i+1]-s[i]<3){ cnt[ct][v]++; if (ct!=1 && cnt[ct][v]<3) printf("ct %u v %u scale %u deg %u n %u: %d %d %d %d\n",ct,v,sc,d,n,s[0],s[1],s[2],n>3?s[3]:0); break; } } } }
    for (ct=0; ct<4; ct++) printf("chord %u: clashing chords (3-4 notes) AUTO %u CLOSE %u OPEN %u\n", ct, cnt[ct][0], cnt[ct][1], cnt[ct][2]);
    {   /* the chords of every scale: STACK, QUART and ADD9 never have two notes closer than a minor third, SUS only when closed (its fourth and fifth are a tone apart) */
        uint32_t bad = cnt[0][0] + cnt[0][1] + cnt[0][2] + cnt[1][2] + cnt[2][0] + cnt[2][1] + cnt[2][2] + cnt[3][0] + cnt[3][1] + cnt[3][2];
        printf("%s\n", bad ? "FAIL: chords with notes too close" : "ALL OK");
        return bad != 0;
    }
}
