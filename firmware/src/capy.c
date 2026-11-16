/* SPDX-License-Identifier: GPL-3.0-only */
/* Capy: a first real instrument for a child of five to seven, on the M-VAVE FM-1.
 *
 * What it holds to:
 *   - Real instruments and real names: piano, organ, bells, guitar, bass, strings, brass, flute, drums; the notes by
 *     their letters, the chords by their names. What she learns here is true at any piano.
 *   - The games make their questions up, so they do not run out; a wrong answer takes nothing away.
 *   - Two places: playing, and the games (SEL). HOME always goes back.
 *
 *   keys        play (12 voices, two-operator FM). The screen names the note, the notes, or the chord she holds
 *   PRESETS     the instrument; the last two are a drum kit and twelve animals, a call on every key
 *   OCT- OCT+   the octave
 *   KNOB 1      ECHO                                 FX  switches it on / off   (the knobs in the order of their
 *   KNOB 2      LENGTH: how long notes ring          ENV switches it on / off    buttons below them: FX, ENV, LFO;
 *   KNOB 3      WOBBLE: vibrato                      LFO switches it on / off    the screen shows the same order)
 *   KNOB 4      TONE: dark .. bright
 *   GLO         the note letters (on the screen's keys, and what she holds), or the sound's wave
 *   ARP         held keys play one after the other
 *   SEQ         CLICK (a metronome), then the band: POP, BLUES, WALTZ, LULLABY, off. First only its drums; a low key (F to E, marked on
 *               the screen) leads it: bass and chords play that key's chord until she picks another, the keys that
 *               fit glow                                           ALGORITHM: the SPEED of band, arp and recording
 *   SELECT      which of her four songs REC, PLAY, SAVE and EDIT mean; the one she leaves is kept
 *   REC         record what you play; REC again: it goes round. REC while it plays: add a layer
 *   PLAY        the recording on / off
 *   EDIT        tap: take the last layer away. Held a second: erase the recording
 *   SAVE        keep the recording for next time
 *   SEL         the games (SELECT: which one; SEL again: another question): TUNES, FIND IT, COPY ME, HIGH LOW,
 *               CHORDS, TAP IT
 *   HOME        back to the start: piano, everything off. Held 3 s (grown-ups): QUIET, half the loudness
 *   (nothing touched for two minutes: the pond comes back, asleep, until something is touched)
 *
 * Built on Dinghy (zvenson) and its platform (Felucca by Leo Kuroshita, with SLOOP's changes): platform.c and below.
 * Integers only: no float, no 64-bit division (docs/RULES.md). tests/host.c runs this file on a computer. */
#define APP_NAME "CAPY"
#define STORE_MAGIC 0x4E455257u                 /* never another firmware's data (unchanged since the first builds, so what was saved still loads) */
static void app_idle(void);
#define LCD_IDLE app_idle                       /* while the screen's DMA runs: the clock and MIDI */
#include "platform.c"

/* ================================================================ the sound (audio ISR) */
#define NV 12u                                  /* voices */
#define LEVEL_PEAK 18000                       /* the mix's peak before the echo and the ceiling's knee (20000) */
#define SUB 32u                                 /* envelopes, pitch and index move every SUB frames (1378 Hz) */
#define ELEN 32768u                             /* the echo's line: 743 ms */

typedef struct {
    const char *name;
    uint16_t col;                               /* its colour on the screen */
    int8_t tr;                                  /* transpose, semitones */
    uint16_t ratio_q8;                          /* modulator : carrier frequency, Q8 */
    uint16_t ipk, isus;                         /* modulation index at the start and held (326 = 1 radian) */
    uint8_t itau;                               /* how fast it falls from one to the other (TAUMUL index) */
    uint32_t arate;                             /* attack: Q23 per SUB frames */
    uint8_t dtau;                               /* decay to sus */
    uint16_t sus;                               /* Q15 */
    uint8_t rtau;                               /* release */
    uint16_t fb;                                /* modulator feedback (as the index) */
    uint8_t follow;                             /* 1: the index follows the loudness (bowed, blown) */
    uint8_t vib;                                /* its own vibrato, 0..16 */
    uint16_t gain;                              /* Q15 */
} patch_t;

#define NPATCH 10u
#define PI_DRUMS 8u                             /* the drum kit: its keys are drums (drum_hit), never a voice */
#define PI_ANIMALS 9u                           /* the animals: its keys are calls (animal_hit), never a voice */
static const patch_t PATCH[NPATCH] = {
    /* name      colour               tr  ratio ipk   isus itau arate    dtau sus    rtau fb   fol vib gain */
    {"PIANO",   RGB(255, 255, 255),   0,  256,  980,  180, 5,  3043486, 10,  0,     4,   0,   0,  0,  9000},
    {"ORGAN",   RGB(255, 150, 60),    0,  512,  400,  400, 0,  1217394, 0,   32767, 1,   110, 0,  0,  6000},
    {"BELLS",   RGB(120, 220, 255),   12, 896,  815,  100, 8,  3043486, 10,  0,     9,   0,   0,  0,  7500},
    {"GUITAR",  RGB(240, 200, 110),   0,  256,  1300, 110, 3,  3043486, 8,   0,     4,   0,   0,  0,  9000},
    {"BASS",    RGB(190, 120, 255),   -12, 256, 1140, 326, 4,  3043486, 9,   16000, 2,   0,   0,  0,  11000},
    {"STRINGS", RGB(255, 110, 130),   0,  256,  520,  0,   0,  50725,   0,   32767, 6,   290, 1,  2,  6000},
    {"BRASS",   RGB(255, 215, 0),     0,  256,  1040, 0,   0,  101450,  6,   24000, 3,   200, 1,  0,  7000},
    {"FLUTE",   RGB(130, 255, 170),   12, 256,  200,  200, 0,  202899,  0,   32767, 3,   0,   0,  3,  9000},
    {"DRUMS",   RGB(255, 90, 60),     0,  256,  0,    0,   0,  3043486, 0,   0,     0,   0,   0,  0,  0},
    {"ANIMALS", RGB(150, 230, 90),    0,  256,  0,    0,   0,  3043486, 0,   0,     0,   0,   0,  0,  0},
};

/* exp(-1 / (tau * 1378.125)) in Q20, tau = 0.03 0.05 0.08 0.12 0.2 0.3 0.5 0.8 1.2 2 3 4.5 7 10 15 22 s */
static const uint32_t TAUMUL[16] = {1023518, 1033468, 1039108, 1042255, 1044779, 1046043, 1047055, 1047625,
                                    1047942, 1048196, 1048322, 1048407, 1048467, 1048500, 1048525, 1048541};

static struct {
    volatile uint32_t inc;                      /* main -> ISR: the pitch, f * 2^32 / 44100 */
    volatile uint32_t trig;                     /* main -> ISR: counts note-ons */
    volatile uint8_t gate, pi;                  /* main -> ISR: held; the patch */
    volatile uint16_t vol;                      /* main -> ISR: its level, Q8 (the band plays softer) */
    volatile uint8_t stage;                     /* ISR -> main: 0 silent, 1 attack, 2 decay / hold, 3 release */
    uint32_t seen, cph, mph;
    int32_t env, ie, a, fbs;
    uint32_t age;                               /* (main) */
    uint8_t note, src;                          /* (main) the note as played; 0 live, 1 the recording */
} voice[NV];

static volatile uint8_t g_tone = 8, g_len = 13, g_len_on, g_wob = 8, g_wob_on, g_echo = 9, g_echo_on, g_quiet;
static volatile uint32_t g_echo_len = 13230;    /* frames: an eighth note */
static volatile uint32_t k_trig, s_trig, h_trig;   /* the drums */
static volatile uint32_t t_trig, t_hi, y_trig, p_trig, h_kit;   /* the kit's toms (t_hi: the high one), cymbal, clap */
static volatile uint32_t c_trig, c_one;         /* the click; c_one: this one is the bar's first, higher */
static int16_t ebuf[ELEN] __attribute__((section(".pool")));
static int16_t hist[512];                       /* the last frames, for the scope */
static volatile uint32_t hist_w;

static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (Dinghy's) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
static uint32_t tau_at(int32_t i) { return TAUMUL[i < 0 ? 0 : i > 15 ? 15 : i]; }

/* ---- the animals: twelve calls, one on each key of the octave. A call is one FM pair whose pitch, loudness and
 * brightness follow a small drawing in time (animal_at, every SUB frames), so it needs no samples. Four can
 * sound at once. */
#define NA 4u
#ifndef NAP_MS
#define NAP_MS 120000u                          /* untouched this long, the pond comes back, asleep */
#endif
static struct {
    volatile uint32_t trig;                     /* main -> ISR: counts the calls */
    volatile uint8_t kind;                      /* main -> ISR: which animal, 0..11 (the key's pitch class) */
    uint32_t seen, t, cph, mph;                 /* t: frames since it began */
    int32_t a, fbs;
    uint8_t on;
} animal[NA];
typedef struct { int32_t f, amp, idx, ratio_q8, fb, nz; } acall_t;   /* f: Hz; idx, fb: as a patch's; nz: noise */
static int32_t lin(int32_t t, int32_t t0, int32_t t1, int32_t a, int32_t b)   /* a until t0, b from t1, a line between */
{
    if (t <= t0) return a;
    if (t >= t1) return b;
    return a + (b - a) * (t - t0) / (t1 - t0);
}
static int32_t syl(int32_t t, int32_t t0, int32_t t1, int32_t att, int32_t rel)   /* a syllable t0..t1: 0..256 */
{
    if (t < t0 || t >= t1) return 0;
    if (t - t0 < att) return 256 * (t - t0) / att;
    if (t1 - t < rel) return 256 * (t1 - t) / rel;
    return 256;
}
static int animal_at(uint32_t kind, int32_t ms, acall_t *c)   /* the call at ms; 0: it is over */
{
    int32_t e = 0, k, f, w, peak = 0;
    c->f = 440; c->amp = 0; c->idx = 0; c->ratio_q8 = 256; c->fb = 0; c->nz = 0;
    switch (kind) {
    case 0:                                     /* C, the frog: "rib-bit", a rattle */
        if (ms >= 340) return 0;
        e = syl(ms, 0, 110, 8, 20) + syl(ms, 150, 340, 8, 50);
        if (ms % 18 >= 9) e = (e * 70) >> 8;
        c->f = ms < 150 ? lin(ms, 0, 110, 420, 520) : lin(ms, 150, 340, 600, 760);
        c->ratio_q8 = 128; c->idx = 650; peak = 14000;
        break;
    case 1:                                     /* C#, the cricket: three chirps of quick ticks */
        if (ms >= 640) return 0;
        e = syl(ms, 0, 130, 3, 10) + syl(ms, 250, 380, 3, 10) + syl(ms, 500, 630, 3, 10);
        if (ms % 16 >= 8) e = 0;
        c->f = 4300; peak = 6000;
        break;
    case 2:                                     /* D, the duck: "quack quack", through the nose */
        if (ms >= 430) return 0;
        e = syl(ms, 0, 170, 10, 40) + syl(ms, 230, 430, 10, 60);
        k = ms < 230 ? ms : ms - 230;
        f = k < 50 ? lin(k, 0, 50, 560, 400) : lin(k, 50, 200, 400, 330);
        c->f = 3 * f; c->ratio_q8 = 85; c->idx = 700; c->fb = 150; peak = 11000;
        break;
    case 3:                                     /* D#, the bee: it comes by and goes */
        if (ms >= 950) return 0;
        e = syl(ms, 0, 950, 350, 450);
        w = sine((uint32_t)ms * 6u * 4294967u);
        c->f = 200 + lin(ms, 0, 950, 0, 40) + ((w * 12) >> 15);
        c->idx = 1000; c->fb = 250; peak = 9000;
        break;
    case 4:                                     /* E, the bird: "tweet tweet twee-oo" */
        if (ms >= 340) return 0;
        e = syl(ms, 0, 70, 5, 20) + syl(ms, 110, 180, 5, 20) + syl(ms, 220, 340, 5, 40);
        c->f = ms < 110 ? lin(ms, 0, 70, 2600, 4000) : ms < 220 ? lin(ms, 110, 180, 2800, 4200) : lin(ms, 220, 340, 4200, 3000);
        c->idx = 80; peak = 8000;
        break;
    case 5:                                     /* F, the cow: "moo", up and slowly down */
        if (ms >= 1150) return 0;
        e = syl(ms, 0, 1150, 120, 250);
        f = ms < 300 ? lin(ms, 0, 300, 105, 150) : lin(ms, 700, 1150, 150, 112);
        c->f = 2 * f; c->ratio_q8 = 128; c->fb = 120; peak = 15000;
        c->idx = ms < 350 ? lin(ms, 0, 350, 150, 900) : lin(ms, 350, 1150, 900, 450);
        break;
    case 6:                                     /* F#, the dog: "woof woof", rough */
        if (ms >= 420) return 0;
        e = syl(ms, 0, 140, 6, 50) + syl(ms, 260, 420, 6, 70);
        k = ms < 260 ? ms : ms - 260;
        c->f = 2 * lin(k, 0, 120, 420, 200); c->ratio_q8 = 128; c->idx = 1100; c->fb = 250; c->nz = 2500; peak = 14000;
        break;
    case 7:                                     /* G, the owl: "hoo, hoo-hoo", round */
        if (ms >= 1020) return 0;
        e = syl(ms, 0, 260, 40, 90) + syl(ms, 420, 560, 30, 60) + syl(ms, 640, 1020, 40, 160);
        c->f = ms < 420 ? lin(ms, 0, 260, 430, 390) : ms < 640 ? lin(ms, 420, 560, 420, 400) : lin(ms, 640, 1020, 430, 370);
        c->idx = 130; peak = 15000;
        break;
    case 8:                                     /* G#, the sheep: "baa", shaking */
        if (ms >= 850) return 0;
        w = sine((uint32_t)ms * 9u * 4294967u);
        e = (syl(ms, 0, 850, 50, 200) * (200 + ((w * 56) >> 15))) >> 8;
        f = lin(ms, 0, 850, 400, 350) + ((w * 20) >> 15);
        c->f = 3 * f; c->ratio_q8 = 85; c->idx = 800; peak = 11000;
        break;
    case 9:                                     /* A, the cat: "meow", up, bright, and down */
        if (ms >= 780) return 0;
        e = syl(ms, 0, 780, 70, 200);
        f = ms < 220 ? lin(ms, 0, 220, 520, 800) : ms < 450 ? lin(ms, 220, 450, 800, 860) : lin(ms, 450, 780, 860, 470);
        c->f = 2 * f; c->ratio_q8 = 128; peak = 11000;
        c->idx = ms < 320 ? lin(ms, 0, 320, 250, 850) : lin(ms, 320, 780, 850, 300);
        break;
    case 10:                                    /* A#, the chick: "peep peep peep" */
        if (ms >= 410) return 0;
        e = syl(ms, 0, 80, 5, 25) + syl(ms, 160, 240, 5, 25) + syl(ms, 320, 410, 5, 30);
        k = ms % 160;
        c->f = k < 40 ? lin(k, 0, 40, 2800, 3600) : lin(k, 40, 80, 3600, 3200);
        c->idx = 50; peak = 6500;
        break;
    default:                                    /* B, the capybara: two squeaks, going up */
        if (ms >= 400) return 0;
        e = syl(ms, 0, 140, 10, 40) + syl(ms, 200, 400, 10, 70);
        c->f = ms < 200 ? lin(ms, 0, 140, 900, 1900) : lin(ms, 200, 400, 1000, 2300);
        c->idx = 260; peak = 9000;
        break;
    }
    c->amp = (e * peak) >> 8;
    return 1;
}

static void app_audio(int32_t *out, uint32_t n)
{
    static int32_t mono[256];
    static uint32_t lfo_ph, ew, k_seen, s_seen, h_seen, k_ph, k_sw, s_ph, rnd = 1;
    static int32_t k_amp, s_amp, h_amp, h_prev, c_amp, t_amp, y_amp, p_amp;
    static uint32_t c_seen, c_ph, c_inc, t_seen, t_ph, t_inc, t_sw, y_seen, p_seen;
    uint32_t sb, v, i, elen = g_echo_len, tone = g_tone;
    int32_t loff = g_len_on ? ((int32_t)g_len - 8) / 2 : 0, wet = g_echo_on ? (int32_t)g_echo * 1400 : 0;
    int32_t quiet = g_quiet;
    if (n > 256u) n = 256u;
    for (i = 0; i < n; i++) mono[i] = 0;

    for (sb = 0; sb < n; sb += SUB) {
        uint32_t cnt = n - sb < SUB ? n - sb : SUB;
        int32_t l, dev;
        lfo_ph += 535654u * SUB;                /* 5.5 Hz */
        l = sine(lfo_ph);
        dev = g_wob_on ? (l * (int32_t)g_wob) >> 8 : 0;   /* Q16 of the pitch: 16 -> +-3 % */
        for (v = 0; v < NV; v++) {
            const patch_t *p = &PATCH[voice[v].pi % NPATCH];
            uint32_t cph, mph, cinc, minc, inc;
            int32_t env, a, at, idx, fbs, fb = p->fb, vdev;
            if (voice[v].trig != voice[v].seen) {
                voice[v].seen = voice[v].trig;
                voice[v].stage = 1;
                voice[v].ie = 32767;
                if (voice[v].env < 4096) voice[v].cph = voice[v].mph = 0, voice[v].fbs = 0;
            }
            if (!voice[v].stage) continue;
            if (!voice[v].gate) voice[v].stage = 3;
            env = voice[v].env;
            if (voice[v].stage == 1) {
                env += (int32_t)p->arate;
                if (env >= 8388607) env = 8388607, voice[v].stage = 2;
            } else if (voice[v].stage == 2) {
                int32_t sus = (int32_t)p->sus << 8;
                env = sus + (int32_t)(((int64_t)(env - sus) * tau_at(p->dtau + (p->sus < 32767u ? loff : 0))) >> 20);
            } else {
                env = (int32_t)(((int64_t)env * tau_at(p->rtau + loff)) >> 20);
                if (env < 8192) env = 0;
            }
            voice[v].env = env;
            voice[v].ie = (int32_t)(((int64_t)voice[v].ie * TAUMUL[p->itau]) >> 20);
            idx = p->follow ? ((int32_t)p->ipk * (env >> 8)) >> 15
                            : (int32_t)p->isus + ((((int32_t)p->ipk - (int32_t)p->isus) * voice[v].ie) >> 15);
            idx = (idx * (int32_t)tone) >> 3;   /* TONE: 0 a sine .. 16 twice as bright */
            at = ((((env >> 8) * (int32_t)p->gain) >> 15) * (int32_t)voice[v].vol) >> 8;
            vdev = dev + ((l * (int32_t)p->vib) >> 8);
            inc = voice[v].inc;
            cinc = inc + (uint32_t)(int32_t)(((int64_t)inc * vdev) >> 16);
            minc = (uint32_t)(((uint64_t)cinc * p->ratio_q8) >> 8);
            cph = voice[v].cph; mph = voice[v].mph; a = voice[v].a; fbs = voice[v].fbs;
            for (i = 0; i < cnt; i++) {
                int32_t m, s;
                a += (at - a) >> 4;             /* no clicks */
                mph += minc;
                m = sine(mph + ((uint32_t)(fbs * fb) << 6));
                fbs = (fbs + m) >> 1;
                cph += cinc;
                s = sine(cph + ((uint32_t)(m * idx) << 6));
                mono[sb + i] += (s * a) >> 15;
            }
            voice[v].cph = cph; voice[v].mph = mph; voice[v].fbs = fbs;
            if (!env) a = 0, voice[v].stage = 0;
            voice[v].a = a;
        }
        for (v = 0; v < NA; v++) {              /* the animals */
            static uint32_t arnd = 7;
            acall_t c;
            uint32_t cph, mph, cinc, minc;
            int32_t a, fbs;
            if (animal[v].trig != animal[v].seen) {
                animal[v].seen = animal[v].trig;
                animal[v].t = 0; animal[v].cph = animal[v].mph = 0; animal[v].fbs = 0;
                animal[v].on = 1;
            }
            if (!animal[v].on) continue;
            if (!animal_at(animal[v].kind, (int32_t)((animal[v].t * 743u) >> 15), &c) && animal[v].a < 8) {
                animal[v].on = 0;
                animal[v].a = 0;
                continue;
            }
            animal[v].t += cnt;
            cinc = (uint32_t)c.f * 97392u;
            minc = (uint32_t)(((uint64_t)cinc * (uint32_t)c.ratio_q8) >> 8);
            cph = animal[v].cph; mph = animal[v].mph; a = animal[v].a; fbs = animal[v].fbs;
            for (i = 0; i < cnt; i++) {
                int32_t m, s;
                a += (c.amp - a) >> 4;
                mph += minc;
                m = sine(mph + ((uint32_t)(fbs * c.fb) << 6));
                fbs = (fbs + m) >> 1;
                cph += cinc;
                s = sine(cph + ((uint32_t)(m * c.idx) << 6));
                if (c.nz) {
                    arnd = arnd * 1664525u + 1013904223u;
                    s += (((int32_t)arnd >> 17) * c.nz) >> 14;
                }
                mono[sb + i] += (s * a) >> 15;
            }
            animal[v].cph = cph; animal[v].mph = mph; animal[v].a = a; animal[v].fbs = fbs;
        }
    }

    /* the drums: a kick (a sine falling from 180 to 50 Hz), a snare (noise and a tone), a hat (bright noise) */
    if (k_trig != k_seen) { k_seen = k_trig; k_amp = 30000; k_sw = 12660000u; k_ph = 0; }
    if (s_trig != s_seen) { s_seen = s_trig; s_amp = 22000; }
    if (h_trig != h_seen) { h_seen = h_trig; h_amp = h_kit ? 20000 : 9000; }   /* (in the band the hat stays behind) */
    if (c_trig != c_seen) { c_seen = c_trig; c_amp = 15000; c_ph = 0; c_inc = c_one ? 171409500u : 114435400u; }   /* A6, D6 */
    if (t_trig != t_seen) { t_seen = t_trig; t_amp = 26000; t_ph = 0; t_inc = t_hi ? 13634810u : 8765235u; t_sw = t_inc + (t_inc >> 1); }   /* 140, 90 Hz */
    if (y_trig != y_seen) { y_seen = y_trig; y_amp = 18000; }
    if (p_trig != p_seen) { p_seen = p_trig; p_amp = 30000; }
    if (k_amp > 16 || s_amp > 16 || h_amp > 16 || c_amp > 16 || t_amp > 16 || y_amp > 16 || p_amp > 16)
        for (i = 0; i < n; i++) {
            int32_t nz, x = 0;
            rnd = rnd * 1664525u + 1013904223u;
            nz = (int32_t)rnd >> 17;            /* +-16383 */
            if (k_amp > 16) {
                k_ph += 4869546u + k_sw;
                k_sw -= k_sw >> 9;
                x += (sine(k_ph) * k_amp) >> 16;
                k_amp -= (k_amp >> 12) + 1;
            }
            if (s_amp > 16) {
                s_ph += 18017000u;
                x += ((nz + (sine(s_ph) >> 2)) * s_amp) >> 16;
                s_amp -= (s_amp >> 11) + 1;
            }
            if (h_amp > 16) {
                x += ((nz - h_prev) * h_amp) >> 16;
                h_amp -= (h_amp >> 9) + 1;
            }
            if (t_amp > 16) {                   /* a tom: a sine falling to its note */
                t_ph += t_inc + t_sw;
                t_sw -= t_sw >> 10;
                x += (sine(t_ph) * t_amp) >> 16;
                t_amp -= (t_amp >> 12) + 1;
            }
            if (y_amp > 16) {                   /* the cymbal: the hat's noise, ringing on */
                x += ((nz - h_prev) * y_amp) >> 16;
                y_amp -= (y_amp >> 13) + 1;
            }
            if (p_amp > 16) {                   /* the clap: a short burst of noise */
                x += ((nz + h_prev) * p_amp) >> 17;   /* (its highs taken off: a hand, not a hiss) */
                p_amp -= (p_amp >> 10) + 1;
            }
            if (c_amp > 16) {                   /* the click: a short high tick */
                c_ph += c_inc;
                x += (sine(c_ph) * c_amp) >> 16;
                c_amp -= (c_amp >> 8) + 1;
            }
            h_prev = nz;
            mono[i] += x;
        }

    /* the level: many notes at once added up past the ceiling's knee, and its corner garbled them. The block is
     * all here before it goes out, so its peak is known: over LEVEL_PEAK the whole mix turns down (within 32
     * frames), and comes back up over ~0.1 s (1/16 of the way a 5.8 ms block). A note or two is untouched. */
    {
        static int32_t lg = 4096;               /* the gain, Q12 */
        int32_t pk = 0, want = 4096, g0 = lg, g1, x;
        for (i = 0; i < n; i++) { x = mono[i] < 0 ? -mono[i] : mono[i]; if (x > pk) pk = x; }
        if (pk > LEVEL_PEAK) want = (int32_t)(((uint32_t)LEVEL_PEAK << 12) / (uint32_t)pk);
        g1 = want < g0 ? want : g0 + ((want - g0) >> 4);
        lg = g1;
        if (g0 < 4096 || g1 < 4096)
            for (i = 0; i < n; i++) {
                int32_t g = i < 32u ? g0 + (((g1 - g0) * (int32_t)i) >> 5) : g1;
                mono[i] = (mono[i] * g) >> 12;
            }
    }

    /* the echo, the ceiling, out */
    for (i = 0; i < n; i++) {
        int32_t dry = mono[i], d = ebuf[(ew - elen) & (ELEN - 1u)], x, y;
        x = (wet ? dry : 0) + ((d * 15000) >> 15);
        ebuf[ew] = (int16_t)(x > 32767 ? 32767 : x < -32767 ? -32767 : x);
        ew = (ew + 1u) & (ELEN - 1u);
        y = dry + ((d * wet) >> 15);
        if (y > 20000) y = 20000 + ((y - 20000) >> 2);          /* a soft knee instead of a hard clip */
        else if (y < -20000) y = -20000 + ((y + 20000) >> 2);
        if (y > 32767) y = 32767;
        if (y < -32767) y = -32767;
        if (quiet) y >>= 1;
        out[2u * i] += y;
        out[2u * i + 1u] += y;
        hist[(hist_w + i) & 511u] = (int16_t)y;
    }
    hist_w += n;
}

/* ================================================================ notes (main loop) */
static const uint32_t INC_C9[12] = {            /* MIDI 120..131; lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static uint32_t ages;
enum { S_LIVE, S_LOOP, S_BAND, S_CUE };         /* who plays a voice: the keys, the recording, the band, a game */

/* the drum kit: every octave the same seven drums on its white keys, the sticks on the black ones */
static const char *const DRUM_NAME[12] = {"KICK", "STICKS", "SNARE", "STICKS", "CLAP", "LOW TOM", "STICKS", "HIGH TOM",
                                          "STICKS", "HAT", "STICKS", "CYMBAL"};
static uint8_t drum_pc;                         /* the last drum or animal played, for the screen */
static uint32_t drum_n, drum_ms;
static void drum_hit(uint32_t note, uint32_t src)
{
    uint32_t pc = note % 12u;
    switch (pc) {
    case 0: k_trig++; break;
    case 2: s_trig++; break;
    case 4: p_trig++; break;
    case 5: t_hi = 0; t_trig++; break;
    case 7: t_hi = 1; t_trig++; break;
    case 9: h_kit = 1; h_trig++; break;
    case 11: y_trig++; break;
    default: c_one = 0; c_trig++; break;
    }
    if (src == 0u) { drum_pc = (uint8_t)pc; drum_n++; drum_ms = fm1_ms; }   /* (0: S_LIVE) */
}
static const char *const ANIMAL_NAME[12] = {"FROG", "CRICKET", "DUCK", "BEE", "BIRD", "COW", "DOG", "OWL", "SHEEP", "CAT",
                                            "CHICK", "CAPYBARA"};
static void animal_hit(uint32_t note, uint32_t src)   /* every octave the same twelve animals */
{
    static uint32_t next;
    uint32_t pc = note % 12u;
    animal[next].kind = (uint8_t)pc;
    animal[next].trig++;
    next = (next + 1u) % NA;
    if (src == 0u) { drum_pc = (uint8_t)pc; drum_n++; drum_ms = fm1_ms; }   /* (0: S_LIVE) */
}
static void play(uint32_t note, uint32_t pi, uint32_t src, uint32_t vol)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    int32_t n;
    if (note > 127u) return;
    if (pi == PI_DRUMS) { drum_hit(note, src); return; }
    if (pi == PI_ANIMALS) { animal_hit(note, src); return; }
    for (v = 0; v < NV; v++) {                  /* the same note again, else a silent voice, else a released, else the oldest */
        uint32_t score;
        if (voice[v].note == note && voice[v].src == src && voice[v].pi == pi && voice[v].stage) score = 0;
        else if (!voice[v].stage && voice[v].trig == voice[v].seen) score = 1;
        else if (!voice[v].gate) score = 0x40000000u + voice[v].age;
        else score = 0x80000000u + voice[v].age;
        if (score < best) { best = score; pick = v; }
    }
    n = (int32_t)note + PATCH[pi % NPATCH].tr;
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    voice[pick].note = (uint8_t)note;
    voice[pick].src = (uint8_t)src;
    voice[pick].age = ++ages;
    voice[pick].pi = (uint8_t)pi;
    voice[pick].vol = (uint16_t)vol;
    voice[pick].inc = INC_C9[n % 12] >> (10 - n / 12);
    voice[pick].gate = 1;
    voice[pick].trig++;
}
static void stop(uint32_t note, uint32_t pi, uint32_t src)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].note == note && voice[v].src == src && voice[v].pi == pi) voice[v].gate = 0;
}
static void stop_src(uint32_t src)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].src == src) voice[v].gate = 0;
}
static int quiet(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].stage || voice[v].trig != voice[v].seen) return 0;
    return 1;
}

/* ---- names: notes and chords */
static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const struct { uint16_t mask; const char *name; } CHORD[] = {   /* bit i: i semitones above the root */
    {0x091, " MAJOR"}, {0x089, " MINOR"}, {0x049, " DIM"}, {0x111, " AUG"}, {0x0A1, " SUS"},
    {0x491, "7"}, {0x891, " MAJ7"}, {0x489, " MIN7"},
};
#define NCHORD (sizeof CHORD / sizeof CHORD[0])
static char *sput(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *nput(char *p, uint32_t v)
{
    char t[10];
    uint32_t i = 0;
    do { t[i++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (i) *p++ = t[--i];
    *p = 0;
    return p;
}
static int chord_name(uint32_t pcs, uint32_t low_pc, char *out)   /* pcs: bit = pitch class held. 1: a chord, named */
{
    uint32_t i, r, c;
    for (i = 0; i < 12u; i++) {                 /* the lowest note first: it is most likely the root */
        uint32_t rel;
        r = (low_pc + i) % 12u;
        if (!(pcs >> r & 1u)) continue;
        rel = ((pcs >> r) | (pcs << (12u - r))) & 0xFFFu;
        for (c = 0; c < NCHORD; c++)
            if (CHORD[c].mask == rel) { sput(sput(out, NN[r]), CHORD[c].name); return 1; }
    }
    return 0;
}

/* ================================================================ the clock: 480 ticks a beat */
#define PPQ 480u
static uint32_t tick, tick_acc, tick_ms, bpm = 100;
static uint8_t cur_patch, band, arp_on, letters = 1, place;   /* place: 0 playing, 1 the games */
static int8_t oct;

static char msg[32];
static uint32_t msg_until, party_until, yuzu_until, bounce_t, flash_until;
static void say(const char *s)
{
    uint32_t i = 0;
    while (s[i] && i < 30u) { msg[i] = s[i]; i++; }
    msg[i] = 0;
    msg_until = fm1_ms + 1600u;
}

/* ---- the band: drums she switches on, and a bass and chords that follow her: the lowest octave of keys (F..E)
 * leads, each white key its chord in C (B: G, its nearest), kept until she picks another. Until the first one the
 * drums play alone: a beat is easy to play over, nothing in it can clash. */
static const struct { uint8_t pc, minor; const char *name; } BCH[6] = {
    {0, 0, "C"}, {2, 1, "D MINOR"}, {4, 1, "E MINOR"}, {5, 0, "F"}, {7, 0, "G"}, {9, 1, "A MINOR"}};
typedef struct {
    const char *name;
    uint8_t spb;                                /* sixteenths a bar */
    uint16_t kick, snare, hat, bass, comp, arp; /* bit s: on sixteenth s of the bar */
    uint8_t walk;                               /* the bass walks up the chord */
} style_t;
#define NSTYLE 4u                               /* band: 0 off, 1 the click (a metronome), 2.. STYLE[band - 2] */
#define CLICK 1u
static const style_t STYLE[NSTYLE] = {
    {"POP", 16, 0x0501, 0x1010, 0x5555, 0x0541, 0x1041, 0, 0},
    {"BLUES", 16, 0x0101, 0x1010, 0x5555, 0x1111, 0x4444, 0, 1},
    {"WALTZ", 12, 0x001, 0, 0x110, 0x001, 0x110, 0, 0},
    {"LULLABY", 16, 0, 0, 0x1010, 0x0001, 0, 0x5555, 0},
};
#define LEAD_KEYS 12u                           /* the keys that lead the band: F3..E4 as printed (any octave) */
static uint32_t band_origin, band_step = 0xFFFFFFFFu, band_ch;
static uint8_t click_mute;                      /* TAP IT's rhythm is heard alone */
static uint8_t band_led;                        /* she has picked a chord: the bass and the chords play */
static uint8_t band_bass = 255, band_comp[3] = {255, 255, 255}, band_arp = 255;
static uint32_t chord_pcs(uint32_t ch)          /* bit = pitch class of a chord tone */
{
    uint32_t r = BCH[ch].pc;
    return 1u << r | 1u << (r + (BCH[ch].minor ? 3u : 4u)) % 12u | 1u << (r + 7u) % 12u;
}
static void band_quiet(void)
{
    stop_src(S_BAND);
    band_bass = band_arp = 255;
    band_comp[0] = band_comp[1] = band_comp[2] = 255;
}
static void band_run(void)
{
    const style_t *y;
    uint32_t st, s, r, third, i;
    if (!band) return;
    st = (tick - band_origin) / (PPQ / 4u);
    if (st == band_step) return;
    band_step = st;
    if (band == CLICK) {                        /* four in a bar, the first one higher */
        if (st % 4u == 0u && !click_mute) { c_one = st % 16u == 0u; c_trig++; }
        return;
    }
    y = &STYLE[band - 2u];
    s = st % y->spb;
    r = BCH[band_ch].pc;
    third = BCH[band_ch].minor ? 3u : 4u;
    if (y->kick >> s & 1u) k_trig++;
    if (y->snare >> s & 1u) s_trig++;
    if (y->hat >> s & 1u) { h_kit = 0; h_trig++; }
    if (!band_led) return;                      /* the drums alone, until she leads */
    if (y->bass >> s & 1u) {
        static const uint8_t WALK[4] = {0, 4, 7, 9};
        uint32_t n = 48u + r + (y->walk ? WALK[s / 4u & 3u] : 0u);
        if (band_bass != 255u) stop(band_bass, 4, S_BAND);
        band_bass = (uint8_t)n;
        play(n, 4, S_BAND, 190);
    }
    if (y->comp >> s & 1u) {
        uint32_t t[3];
        t[0] = r; t[1] = r + third; t[2] = r + 7u;
        for (i = 0; i < 3u; i++) {
            if (band_comp[i] != 255u) stop(band_comp[i], 0, S_BAND);
            band_comp[i] = (uint8_t)(55u + (t[i] + 5u) % 12u);   /* folded into G3..F#4 */
            play(band_comp[i], 0, S_BAND, 105);
        }
    }
    if (y->arp >> s & 1u) {
        uint32_t t[4];
        t[0] = r; t[1] = r + third; t[2] = r + 7u; t[3] = r + third;
        if (band_arp != 255u) stop(band_arp, 3, S_BAND);
        band_arp = (uint8_t)(60u + t[s / 2u & 3u] % 12u);
        play(band_arp, 3, S_BAND, 130);
    }
}
static void band_lead(uint32_t k, uint32_t keys)   /* key k (0 = F) went down with the band on; keys: all held */
{
    static const int8_t CH[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 4};   /* by pitch class: C Dm Em F G Am, B: G */
    int32_t ch;
    uint32_t r, third, t[3], i;
    if (band <= CLICK || k >= LEAD_KEYS) return;
    for (k = 0; !(keys >> k & 1u); k++) ;       /* the lowest key held leads: C E G in the low keys is C, not E minor */
    ch = CH[(53u + k) % 12u];
    if (ch < 0 || (band_led && (uint32_t)ch == band_ch)) return;
    band_ch = (uint32_t)ch;
    band_led = 1;
    band_quiet();
    r = BCH[ch].pc;                             /* and it answers at once: the bass and the chord, not at the next hit */
    third = BCH[ch].minor ? 3u : 4u;
    band_bass = (uint8_t)(48u + r);
    play(band_bass, 4, S_BAND, 190);
    if (STYLE[band - 2u].comp) {
        t[0] = r; t[1] = r + third; t[2] = r + 7u;
        for (i = 0; i < 3u; i++) {
            band_comp[i] = (uint8_t)(55u + (t[i] + 5u) % 12u);
            play(band_comp[i], 0, S_BAND, 105);
        }
    }
}

/* ---- the recording: note events in ticks, sorted; layers on top of each other */
#define NEV 1500u
typedef struct { uint32_t t; uint8_t note, pi, on, layer; } ev_t;
static struct { uint32_t len; uint16_t n; uint8_t layers, pad; ev_t ev[NEV]; } L;   /* (saved as it is: slot 0) */
enum { R_IDLE, R_ARMED, R_FIRST, R_DUB };
static uint8_t rec, playing, song_dirty;        /* song_dirty: the recording is not as it was saved */
static uint32_t loop_t0, loop_ptr, rec_t0;
static uint16_t open_n, open_note[16];          /* notes recorded on and not yet off: note | pi << 8 */

static int ev_insert(uint32_t at, uint32_t t, uint32_t note, uint32_t pi, uint32_t on)
{
    uint32_t i;
    if (L.n >= NEV) return -1;
    for (i = L.n; i > at; i--) L.ev[i] = L.ev[i - 1u];
    L.ev[at].t = t; L.ev[at].note = (uint8_t)note; L.ev[at].pi = (uint8_t)pi; L.ev[at].on = (uint8_t)on;
    L.ev[at].layer = (uint8_t)(L.layers ? L.layers - 1u : 0u);
    L.n++;
    return 0;
}
static void open_track(uint32_t note, uint32_t pi, uint32_t on)
{
    uint32_t i, key = note | pi << 8;
    for (i = 0; i < open_n; i++)
        if (open_note[i] == key) { open_note[i] = open_note[--open_n]; break; }
    if (on && open_n < 16u) open_note[open_n++] = (uint16_t)key;
}
static void loop_start(uint32_t at_tick)
{
    playing = 1;
    loop_t0 = at_tick;
    loop_ptr = 0;
}
static void rec_close(void)                     /* the notes still held get their note-off, the recording ends */
{
    uint32_t i;
    song_dirty = 1;
    if (rec == R_FIRST) {
        int32_t len = (int32_t)(tick - rec_t0);
        if (len < 240) len = 240;
        if (band) {                             /* with the band on: a whole number of beats */
            len = (len + 240) / 480 * 480;
            if (len < 480) len = 480;
        }
        L.len = (uint32_t)len;
        for (i = 0; i < L.n; i++)
            if (L.ev[i].t >= L.len) L.ev[i].t = L.len - 1u;
        for (i = 0; i < open_n; i++) ev_insert(L.n, L.len - 1u, open_note[i] & 255u, open_note[i] >> 8, 0);
        if (!L.n) { L.len = 0; L.layers = 0; rec = R_IDLE; open_n = 0; return; }
        for (i = 0; i < open_n; i++) stop(open_note[i] & 255u, open_note[i] >> 8, S_LIVE);   /* (the recording takes over) */
        loop_start(band ? rec_t0 + L.len : tick);
    } else if (rec == R_DUB) {
        uint32_t pos = tick - loop_t0, added = 0;
        if (pos >= L.len) pos = L.len - 1u;
        for (i = 0; i < open_n; i++)
            if (!ev_insert(loop_ptr, pos, open_note[i] & 255u, open_note[i] >> 8, 0)) loop_ptr++;
        for (i = 0; i < L.n; i++)
            if (L.ev[i].layer == L.layers - 1u) added++;
        if (!added) L.layers--;                 /* nothing played: no layer */
    }
    open_n = 0;
    rec = R_IDLE;
}
static void rec_note(uint32_t note, uint32_t pi, uint32_t on)   /* every live note passes here */
{
    if (rec == R_ARMED && on) {
        L.n = 0; L.len = 0; L.layers = 1; open_n = 0;
        rec_t0 = band ? band_origin + (tick - band_origin + 240u) / 480u * 480u : tick;   /* the nearest beat */
        rec = R_FIRST;
    }
    if (rec == R_FIRST) {
        int32_t t = (int32_t)(tick - rec_t0);
        if (ev_insert(L.n, t < 0 ? 0u : (uint32_t)t, note, pi, on)) { rec_close(); say("FULL"); return; }
        open_track(note, pi, on);
    } else if (rec == R_DUB) {
        int32_t pos = (int32_t)(tick - loop_t0);
        if (pos < 0) pos = 0;
        if (ev_insert(loop_ptr, (uint32_t)pos, note, pi, on)) { rec_close(); say("FULL"); return; }
        loop_ptr++;
        open_track(note, pi, on);
    }
}
static void loop_stop(void)
{
    if (rec == R_DUB) rec_close();
    playing = 0;
    stop_src(S_LOOP);
}
static void loop_run(void)
{
    int32_t pos;
    if (!playing || !L.len) return;
    pos = (int32_t)(tick - loop_t0);
    if (pos < 0) return;                        /* (it starts on the next beat) */
    for (;;) {
        uint32_t lim = (uint32_t)pos >= L.len ? L.len : (uint32_t)pos + 1u;
        while (loop_ptr < L.n && L.ev[loop_ptr].t < lim) {
            const ev_t *e = &L.ev[loop_ptr++];
            if (e->on) play(e->note, e->pi, S_LOOP, 256);
            else stop(e->note, e->pi, S_LOOP);
        }
        if ((uint32_t)pos < L.len) break;
        pos -= (int32_t)L.len;                  /* round again */
        loop_t0 += L.len;
        loop_ptr = 0;
    }
}
static void loop_undo(void)                     /* the last layer away */
{
    uint32_t i, j = 0;
    int32_t pos;
    song_dirty = 1;
    if (rec != R_IDLE) rec_close();
    if (L.layers <= 1u) {
        loop_stop();
        L.len = 0; L.n = 0; L.layers = 0;
        return;
    }
    for (i = 0; i < L.n; i++)
        if (L.ev[i].layer != L.layers - 1u) L.ev[j++] = L.ev[i];
    L.n = (uint16_t)j;
    L.layers--;
    stop_src(S_LOOP);
    pos = (int32_t)(tick - loop_t0);
    for (loop_ptr = 0; loop_ptr < L.n && (int32_t)L.ev[loop_ptr].t <= pos; loop_ptr++) {}
}

/* ---- live notes: the keys, the arp and MIDI in all come through here */
static void live_on(uint32_t note, uint32_t pi, int send)
{
    play(note, pi, S_LIVE, 256);
    if (send) midi_send(0x90u, note, 100);
    rec_note(note, pi, 1);
}
static void live_off(uint32_t note, uint32_t pi, int send)
{
    stop(note, pi, S_LIVE);
    if (send) midi_send(0x80u, note, 0);
    rec_note(note, pi, 0);
}

/* ---- the arp: the held keys one after the other, upwards, in eighth notes */
static uint32_t arp_next, arp_key = 255, arp_note = 255, arp_pi;
static void arp_run(void)
{
    uint32_t mask = fm1_in.notes & 0x7FFFFFFu, k, i;
    if (!arp_on || !mask) {
        if (arp_note != 255u) { live_off(arp_note, arp_pi, 1); arp_note = 255; }
        arp_key = 255;
        return;
    }
    if (arp_key != 255u && (int32_t)(tick - arp_next) < 0) return;
    arp_next = (arp_key == 255u ? tick : arp_next) + PPQ / 2u;
    if (arp_note != 255u) live_off(arp_note, arp_pi, 1);
    k = arp_key == 255u ? 26u : arp_key;
    for (i = 0; i < 27u; i++) {                 /* the next held key above, round */
        k = (k + 1u) % 27u;
        if (mask >> k & 1u) break;
    }
    arp_key = k;
    arp_note = (uint32_t)(53 + 12 * oct + (int32_t)k);
    arp_pi = cur_patch;
    live_on(arp_note, arp_pi, 1);
}

/* ================================================================ the games */
/* Seven games that never run out: each makes its questions up as it goes, gets a little harder after five right
 * answers and remembers its level. A wrong answer costs nothing: the question comes again, then with a hint. */
enum { G_TUNES, G_FIND, G_COPY, G_HILO, G_CHORD, G_TAP, G_ALONG, NGAME };
static const char *const GNAME[NGAME] = {"TUNES", "FIND IT", "COPY ME", "HIGH LOW", "CHORDS", "TAP IT", "JOIN IN"};   /* (JOIN IN: "play along", in letters that fit beside the capybara) */
static const uint8_t GMAX[NGAME] = {1, 5, 12, 5, 4, 4, 5};
enum { P_NEW, P_CUE, P_WAIT, P_DONE };
#define NSAVE 4u                                /* her songs: SELECT picks one, slot 0..3 */
static struct { uint8_t quiet, letters, song, lvl[NGAME], slot; } set;   /* what survives a power-off (new things go at the end:
                                                                          an older, shorter one still loads, see app_main) */
static uint32_t set_dirty;
static uint8_t game, g_phase, g_ok, g_miss, cue_hide;
static uint32_t g_t, rnd_m = 12345u, hint_mask, q_pc = 99, q_n, q_pos, q_hi, q_mask, q_t0, chord_seen;
static uint8_t q_note[16];
static uint32_t tap_t[8];
static uint32_t al_t0, al_i;                    /* PLAY ALONG: the tick its tune starts on (a bar line), the next note to play */
static uint8_t al_slot[8];                      /* ... and the eighth of the bar each note falls on */

static uint32_t rnd_n(uint32_t n)
{
    rnd_m = rnd_m * 1664525u + 1013904223u + fm1_ms;
    return (rnd_m >> 16) % n;
}

/* a cue: the notes a game plays to her (key numbers, 0 = F3; 255 = a drum) */
static struct { uint16_t t; uint8_t key, dur; } cue[16];
static uint32_t cue_n, cue_i, cue_t0, cue_off, cue_on = 255, cue_pi;
static void cue_add(uint32_t t, uint32_t key, uint32_t dur_ms)
{
    if (cue_n < 16u) { cue[cue_n].t = (uint16_t)t; cue[cue_n].key = (uint8_t)key; cue[cue_n].dur = (uint8_t)(dur_ms / 10u); cue_n++; }
}
static void cue_go(uint32_t wait_ms) { cue_i = 0; cue_t0 = fm1_ms + wait_ms; g_phase = P_CUE; }
static void cue_run(void)
{
    if (cue_on != 255u && (int32_t)(fm1_ms - cue_off) >= 0) { stop(cue_on, cue_pi, S_CUE); cue_on = 255; }
    if (g_phase != P_CUE || !place || game == G_ALONG) return;   /* (PLAY ALONG keeps its own time, in ticks) */
    while (cue_i < cue_n && (int32_t)(fm1_ms - cue_t0) >= (int32_t)cue[cue_i].t) {
        if (cue[cue_i].key == 255u) s_trig++;
        else {
            if (cue_on != 255u) stop(cue_on, cue_pi, S_CUE);
            cue_on = 53u + cue[cue_i].key;
            cue_pi = cur_patch;
            cue_off = fm1_ms + cue[cue_i].dur * 10u;
            play(cue_on, cue_pi, S_CUE, 256);
        }
        cue_i++;
    }
    if (cue_i >= cue_n && cue_on == 255u) { g_phase = P_WAIT; q_t0 = fm1_ms; q_pos = 0; }
}

/* c d e f g a b = C4..B4, C D E F G = C5..G5, x y z = G3 A3 B3. Traditional tunes, written out note by note. */
/* ch: the band under it, a letter where it plays a chord (C F G; D E A the minor ones), '-' where only the hat ticks */
static const struct { const char *name, *notes, *ch; } SONG[] = {
    {"TWINKLE TWINKLE", "ccggaagffeeddcggffeedggffeedccggaagffeeddc", "C-C-F-CF-C-G-CC-F-C-GC-F-C-GC-C-F-CF-C-G-C"},
    {"MARY HAD A LAMB", "edcdeeedddeggedcdeeeeddedc", "C---C--G--C--C---C---G---C"},
    {"ODE TO JOY", "eefggfedccdeeddeefggfedccdedcc", "C---G---C---G--C---G---C---GC-"},
    {"FRERE JACQUES", "cdeccdecefgefggagfecgagfeccxccxc", "C---C---C--C--C---C-C---C-CGCCGC"},
    {"ROW YOUR BOAT", "cccdeedefgCCCgggeeecccgfedc", "C----C----C-----C-----G---C"},
    {"OLD MACDONALD", "cccxyyxeeddcxcccxyyxeeddc", "C---F-CC-G-C-C---F-CC-G-C"},
    {"JINGLE BELLS", "eeeeeeegcdefffffeeeeddedgeeeeeeegcdefffffeeeggfdc", "C--C--C----F----C---G----C--C--C----F----C--G---C"},
    {"LONDON BRIDGE", "gagfefgdefefggagfefgdgec", "C---C--G--C--C---C--G-C-"},
    {"HAPPY BIRTHDAY", "ggagCbggagDCggGECbaFFECDC", "--C--G--G--C--C---F--C-GC"},
};
#define NSONG (sizeof SONG / sizeof SONG[0])
static uint32_t song_pos;
static uint32_t song_key(char c)                /* the key (0 = F3) of a song letter */
{
    static const uint8_t LOW[7] = {16, 18, 7, 9, 11, 12, 14};   /* a b c d e f g */
    static const uint8_t HIGH[7] = {0, 0, 19, 21, 23, 24, 26};  /* C D E F G */
    if (c >= 'a' && c <= 'g') return LOW[c - 'a'];
    if (c >= 'C' && c <= 'G') return HIGH[c - 'A'];
    return c == 'x' ? 2u : c == 'y' ? 4u : 6u;
}
static uint32_t song_len(void) { uint32_t n = 0; while (SONG[set.song % NSONG].notes[n]) n++; return n; }

static uint32_t pc_keys(uint32_t pcs, uint32_t from, uint32_t to)   /* the keys from..to whose pitch class is in pcs */
{
    uint32_t k, m = 0;
    for (k = from; k <= to && k < 27u; k++)
        if (pcs >> (53u + k) % 12u & 1u) m |= 1u << k;
    return m;
}

static uint32_t tune_off;                       /* the band under a tune: its chord rings until then */
static void tune_band(char c)                   /* she played the tune's next note: the band plays with her, at her pace */
{
    static const int8_t CH[7] = {5, -1, 0, 1, 2, 3, 4};   /* A C D E F G -> BCH */
    uint32_t r, i, t[3];
    if (c < 'A' || c > 'G' || CH[c - 'A'] < 0) { h_kit = 0; h_trig++; return; }   /* in between: the hat */
    band_quiet();
    r = BCH[CH[c - 'A']].pc;
    t[0] = r; t[1] = r + (BCH[CH[c - 'A']].minor ? 3u : 4u); t[2] = r + 7u;
    band_bass = (uint8_t)(48u + r);
    play(band_bass, 4, S_BAND, 170);
    for (i = 0; i < 3u; i++) {
        band_comp[i] = (uint8_t)(55u + (t[i] + 5u) % 12u);
        play(band_comp[i], 0, S_BAND, 80);
    }
    k_trig++;
    tune_off = fm1_ms + 1800u;
    if (!tune_off) tune_off = 1;
}
static uint8_t copy_note(void)                  /* COPY ME's notes: C D E, then G A, then the C above */
{
    static const uint8_t POOL[6] = {7, 9, 11, 14, 16, 19};      /* (they sound well in any order) */
    uint32_t lvl = set.lvl[G_COPY];
    return POOL[rnd_n(lvl < 4u ? 3u : lvl < 7u ? 5u : 6u)];
}
static void game_new(void)                      /* the next question */
{
    static const uint8_t WHITE[7] = {0, 2, 4, 5, 7, 9, 11};
    uint32_t lvl = set.lvl[game], i, pc;
    g_miss = 0;
    hint_mask = 0;
    cue_n = 0;
    cue_hide = 0;
    g_phase = P_WAIT;
    q_t0 = fm1_ms;
    switch (game) {
    case G_TUNES:
        song_pos = 0;
        break;
    case G_FIND:                                /* a letter: find its key. C D E, then more, then the black keys */
        do pc = lvl >= 4u ? rnd_n(12) : WHITE[rnd_n(lvl == 1u ? 3u : lvl == 2u ? 5u : 7u)]; while (pc == q_pc);
        q_pc = pc;
        break;
    case G_COPY:                                /* a tune that grows by a note each time */
        if (q_n < 2u || q_n > 12u) q_n = 0;
        while (q_n < 2u) q_note[q_n++] = copy_note();
        for (i = 0; i < q_n; i++) cue_add(i * 480u, q_note[i], 380);
        cue_go(500);
        break;
    case G_HILO: {                              /* two notes: is the second higher or lower? Closer and closer */
        static const uint8_t LO[6] = {0, 8, 5, 3, 2, 1}, SPAN[6] = {0, 5, 3, 2, 1, 1};
        uint32_t d = LO[lvl] + rnd_n(SPAN[lvl]), a = rnd_n(27u - d);
        q_hi = rnd_n(2);
        q_note[0] = (uint8_t)(q_hi ? a : a + d);
        q_note[1] = (uint8_t)(q_hi ? a + d : a);
        cue_add(0, q_note[0], 500);
        cue_add(700, q_note[1], 500);
        cue_hide = 1;
        cue_go(500);
        break;
    }
    case G_CHORD: {                             /* a chord's name: hold its three notes */
        static const uint8_t EASY[6] = {0, 5, 7, 9, 2, 4};      /* C F G, then A minor, D minor, E minor */
        uint32_t minor;
        do {
            if (lvl <= 2u) { i = rnd_n(lvl == 1u ? 3u : 6u); pc = EASY[i]; minor = i >= 3u; }
            else { pc = rnd_n(12); minor = lvl >= 4u && rnd_n(2); }
        } while (pc == q_pc);
        q_pc = pc;
        q_hi = minor;
        q_mask = 1u << pc | 1u << (pc + (minor ? 3u : 4u)) % 12u | 1u << (pc + 7u) % 12u;
        chord_seen = 0;
        break;
    }
    case G_ALONG: {                             /* a short tune over the beat: a bar to listen, a bar to play it */
        static const uint8_t POOL[5] = {7, 9, 11, 14, 16};      /* C D E G A: they fit in any order */
        uint32_t m = 1u, n = 1u, at = rnd_n(5), bar = 4u * PPQ;
        q_n = lvl + 2u;                         /* 3 notes .. 7 */
        if (lvl <= 2u) m = lvl == 1u ? 0x15u : 0x55u;           /* on the beats first */
        else
            while (n < q_n) {                   /* then on any eighth, the first always on the one */
                i = rnd_n(8);
                if (!(m >> i & 1u)) { m |= 1u << i; n++; }
            }
        for (i = 0, n = 0; i < 8u; i++)
            if (m >> i & 1u) {
                al_slot[n] = (uint8_t)i;
                q_note[n++] = POOL[at];
                at = at == 0u ? 1u : at == 4u ? 3u : rnd_n(2) ? at + 1u : at - 1u;   /* a step up or down */
            }
        al_t0 = band_origin + ((tick - band_origin + PPQ / 2u) / bar + 1u) * bar;   /* the next bar line */
        al_i = 0;
        g_phase = P_CUE;
        break;
    }
    case G_TAP: {                               /* a rhythm on the drum: tap it back on any key */
        uint32_t hits = lvl + 2u, m = 1u, n = 1u;
        while (n < hits) {
            i = lvl <= 2u ? rnd_n(4) * 2u : rnd_n(8);           /* quarters first, eighths later */
            if (!(m >> i & 1u)) { m |= 1u << i; n++; }
        }
        q_mask = m;
        q_n = hits;
        for (i = 0; i < 8u; i++)
            if (m >> i & 1u) cue_add(i * 300u, 255, 0);
        cue_go(600);
        break;
    }
    }
}
/* A right answer: every key's light breathes once, dim to bright to dim. The rise and fall use the platform's
 * backlight layer (fm1_led_bg, one pulse a frame of fm1_led_bg_ns), from barely there to about 1/7 of lit, steps
 * the eye reads as even; the top is plain lit. The pulse is capped there: above the 595's shift the LED tick
 * waits it out, so a long one would take time from the audio. (The host and the SMK-37 have no such layer: lit
 * at the top only.) */
#define BREATH_MS 500u
static const uint16_t BREATH_NS[16] = {300, 400, 540, 720, 970, 1300, 1740, 2330, 3120, 4180, 5600, 7500, 10000,
                                       13000, 0, 0};   /* 0 at the top: lit */
#if !defined(DINGHY_HOST) && !BOARD_SMK37
static void keys_backlight(uint32_t mask, uint32_t ns)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++) {
            int32_t id = FM1_KEYMAP[r][p];
            if (id >= (int32_t)KEY_LED(0) && id < (int32_t)KEY_LED(27)) {
                if (mask >> (id - (int32_t)KEY_LED(0)) & 1u) fm1_led_bg[p] |= (uint8_t)(1u << r);
                else fm1_led_bg[p] &= (uint8_t)~(1u << r);
            }
        }
    fm1_led_bg_ns = (uint16_t)ns;
}
#else
static void keys_backlight(uint32_t mask, uint32_t ns) { (void)mask; (void)ns; }
#endif
static uint32_t babies(void);
static void game_right(void)
{
    static const char *const YES[4] = {"YES!", "THAT'S IT!", "GOOD EAR!", "RIGHT!"};
    flash_until = fm1_ms + BREATH_MS;          /* every key breathes once, dim to bright to dim: no sound over hers */
    yuzu_until = fm1_ms + 1500u;
    if (++g_ok >= 5u && set.lvl[game] < GMAX[game]) {
        uint32_t had = babies();
        set.lvl[game]++;
        g_ok = 0;
        set_dirty = 1;
        say(babies() > had ? "A NEW BABY! SEE IT AT HOME" : "LEVEL UP!");
        party_until = fm1_ms + 1200u;
    } else
        say(YES[rnd_n(4)]);
    g_phase = P_DONE;
    g_t = fm1_ms + 1000u;
}
static void game_again(void)                    /* not right: once more, the same one */
{
    say(game == G_CHORD || game == G_FIND ? "TRY ANOTHER KEY" : "LISTEN AGAIN");
    if (g_miss < 9u) g_miss++;
    if (cue_n) { cue_hide = 0; cue_go(900); }   /* (HIGH LOW: this time the keys light) */
}
static void game_key(uint32_t k)                /* a key went down */
{
    uint32_t pc = (53u + k) % 12u;
    if (g_phase != P_WAIT) return;
    switch (game) {
    case G_TUNES:
        if (k != song_key(SONG[set.song % NSONG].notes[song_pos])) break;
        tune_band(SONG[set.song % NSONG].ch[song_pos]);
        if (++song_pos >= song_len()) {
            y_trig++;                           /* the last note: a cymbal */
            set.song = (uint8_t)((set.song + 1u) % NSONG);   /* on to the next tune */
            set_dirty = 1;
            song_pos = 0;
            say("WELL DONE!");
            party_until = fm1_ms + 1600u;
        }
        break;
    case G_FIND:
        if (pc == q_pc) game_right();
        else {
            char s[24];
            sput(sput(s, "THAT WAS "), NN[pc]);
            say(s);
            if (++g_miss >= 2u) hint_mask = pc_keys(1u << q_pc, 0, 26);
        }
        break;
    case G_COPY:
        if (k == q_note[q_pos]) {
            if (++q_pos >= q_n) {
                if (q_n < 12u) q_note[q_n++] = copy_note();
                if (q_n - 1u > set.lvl[game]) { set.lvl[game] = (uint8_t)(q_n - 1u); set_dirty = 1; }
                game_right();
            }
        } else {
            game_again();
            if (g_miss >= 2u && q_n > 2u) {     /* twice wrong: a note shorter */
                q_n--;
                cue_n = q_n;
                g_miss = 0;
            }
        }
        break;
    case G_ALONG:                               /* her bar: the notes in their order (a wrong key costs nothing) */
        if (k != q_note[q_pos]) break;
        if (++q_pos >= q_n) { hint_mask = 0; game_right(); }
        else hint_mask = 1u << q_note[q_pos];
        break;
    case G_TAP:
        if (q_pos < 8u) tap_t[q_pos++] = fm1_ms;
        if (q_pos >= q_n) {
            uint32_t i, j = 0, ok = 1;
            for (i = 0; i < 8u; i++)            /* each tap as far from the first as its drum hit was (the first hit is slot 0) */
                if (q_mask >> i & 1u) {
                    int32_t want = (int32_t)(i * 300u), d = want - (int32_t)(tap_t[j] - tap_t[0]), tol = 120 + want / 6;
                    if (d < -tol || d > tol) ok = 0;
                    j++;
                }
            if (ok) game_right();
            else game_again();
        }
        break;
    default:
        break;
    }
}
static void game_run(uint32_t keys)             /* every pass of the main loop */
{
    if (tune_off && (int32_t)(fm1_ms - tune_off) >= 0) { band_quiet(); tune_off = 0; }   /* (the band under a tune rings out) */
    if (g_phase == P_DONE && (int32_t)(fm1_ms - g_t) >= 0) game_new();
    if (game == G_ALONG && (int32_t)(tick - al_t0) >= 0) {
        uint32_t pos = tick - al_t0, bar = 4u * PPQ;
        if (g_phase == P_CUE) {                 /* the first bar: it plays the tune, the keys light */
            while (al_i < q_n && pos >= al_slot[al_i] * (PPQ / 2u)) {
                if (cue_on != 255u) stop(cue_on, cue_pi, S_CUE);
                cue_on = 53u + q_note[al_i++];
                cue_pi = cur_patch;
                cue_off = fm1_ms + 220u;
                play(cue_on, cue_pi, S_CUE, 256);
            }
            if (pos >= bar) { g_phase = P_WAIT; q_pos = 0; hint_mask = 1u << q_note[0]; }   /* the second: hers */
        } else if (g_phase == P_WAIT && pos >= 2u * bar + PPQ / 2u) {   /* her bar is over, and a little more */
            say("ONCE MORE");
            if (g_miss < 9u) g_miss++;
            al_t0 += 2u * bar;
            al_i = 0;
            hint_mask = 0;
            g_phase = P_CUE;
        }
    }
    if (g_phase != P_WAIT) return;
    if (game == G_CHORD) {
        uint32_t k, pcs = 0, n = 0;
        for (k = 0; k < 27u; k++)
            if (keys >> k & 1u) { pcs |= 1u << (53u + k) % 12u; n++; }
        if (n >= 3u && pcs == q_mask) game_right();
        else if (n >= 3u && keys != chord_seen) { chord_seen = keys; if (++g_miss >= 2u) hint_mask = pc_keys(q_mask, 7, 18); }
        if (fm1_ms - q_t0 > 9000u) hint_mask = pc_keys(q_mask, 7, 18);   /* stuck: the keys light */
    }
    if (game == G_TAP && q_pos && q_pos < q_n && fm1_ms - tap_t[q_pos - 1u] > 2500u) game_again();
}
static void game_enter(uint32_t g)
{
    game = (uint8_t)(g % NGAME);
    if (!set.lvl[game] || set.lvl[game] > GMAX[game]) set.lvl[game] = 1;
    g_ok = 0;
    q_n = 0;
    q_pc = 99;
    stop_src(S_CUE);
    cue_on = 255;
    if (game == G_ALONG) {                      /* its beat: the POP band's drums, and nothing else */
        band = CLICK + 1u;
        band_led = 0;
        band_origin = tick;
        band_step = 0xFFFFFFFFu;
    } else if (band > CLICK) { band = 0; band_quiet(); }
    game_new();
}
static void game_text(char *big, char *small)   /* what the game shows */
{
    big[0] = small[0] = 0;
    switch (game) {
    case G_TUNES:
        sput(big, NN[(53u + song_key(SONG[set.song % NSONG].notes[song_pos])) % 12u]);
        nput(sput(nput(sput(sput(small, SONG[set.song % NSONG].name), "  "), song_pos), "/"), song_len());
        break;
    case G_FIND:
        sput(big, NN[q_pc % 12u]);
        sput(small, "FIND THIS NOTE");
        break;
    case G_COPY:
        nput(sput(big, g_phase == P_CUE ? "LISTEN " : "PLAY "), q_n);
        sput(small, g_phase == P_CUE ? "WATCH THE KEYS" : "NOW YOU: THE SAME NOTES");
        break;
    case G_HILO:
        sput(big, g_phase == P_DONE ? (q_hi ? "HIGHER" : "LOWER") : g_phase == P_CUE ? "LISTEN" : "?");
        sput(small, "OCT- LOWER    OCT+ HIGHER");
        break;
    case G_CHORD:
        sput(sput(big, NN[q_pc % 12u]), q_hi ? " MINOR" : " MAJOR");
        sput(small, "HOLD ITS THREE NOTES");
        break;
    case G_ALONG:
        sput(big, g_phase == P_WAIT ? "YOUR TURN" : g_phase == P_DONE ? "YES!" : "LISTEN");
        sput(small, g_phase == P_WAIT ? "PLAY IT WITH THE DRUMS" : "A TUNE OVER THE DRUMS");
        break;
    case G_TAP:
        sput(small, g_phase == P_CUE ? "LISTEN TO THE DRUM" : "TAP IT ON ANY KEY");
        break;
    }
}

/* ================================================================ the clock runs everything that keeps time */
static uint32_t midi_in(void);
static uint32_t in_clock;
static void clock_run(void)
{
    uint32_t dt;
    if (in_clock) return;
    in_clock = 1;
    dt = fm1_ms - tick_ms;
    tick_ms += dt;
    if (dt > 200u) dt = 200u;
    tick_acc += dt * bpm * PPQ;
    tick += tick_acc / 60000u;
    tick_acc %= 60000u;
    band_run();
    arp_run();
    loop_run();
    cue_run();
    midi_in();
    in_clock = 0;
}
static uint32_t idle_ok;
static void app_idle(void) { if (idle_ok) clock_run(); }

static uint8_t midi_pi[128];
static uint32_t midi_in(void)                   /* 1: a note came in */
{
    uint8_t st, d1, d2;
    uint32_t got = 0;
    while (midi_get(&st, &d1, &d2) != 0) {
        uint32_t ty = st & 0xF0u;
        if (st >= 0xF0u || d1 > 127u) continue;
        if (ty == 0x90u && d2) { midi_pi[d1] = cur_patch; live_on(d1, cur_patch, 0); got = 1; }
        else if (ty == 0x80u || ty == 0x90u) live_off(d1, midi_pi[d1], 0);
    }
    return got;
}

/* ================================================================ the screen */
#define C_KEY RGB(215, 215, 215)
#define C_GREY RGB(70, 70, 70)
#define C_TXT RGB(170, 170, 170)
#define C_GOAL RGB(60, 230, 90)
#define C_FIT RGB(80, 150, 255)
#define C_LEAD RGB(255, 170, 30)
#define KEY0 53u
static int black(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }

static void draw_keys(uint32_t lit, uint32_t goal, uint32_t fit, uint16_t col, int lead)   /* goal: a game points at them, green; fit: the band's chord */
{
    uint32_t k, w = 0;
    cv_begin(240, 56, C_BLACK);
    for (k = 0; k < 27u; k++)
        if (!black(KEY0 + k)) {
            int32_t x = 8 + (int32_t)w * 14;
            cv_rect(x, 0, 13, 56, goal >> k & 1u ? C_GOAL : lit >> k & 1u ? col : C_KEY);
            if (fit >> k & 1u) cv_rect(x, 50, 13, 6, C_FIT);                           /* it fits the band's chord */
            if (lead && k < LEAD_KEYS) cv_rect(x, 0, 13, 4, C_LEAD);                    /* it leads the band */
            if (letters) cv_text(x + 3, 34, &FONT_S, NN[(KEY0 + k) % 12u], C_BLACK);   /* its letter */
            w++;
        }
    for (k = 0, w = 0; k < 27u; k++)
        if (black(KEY0 + k))
            cv_rect(8 + (int32_t)w * 14 - 5, 0, 9, 34, goal >> k & 1u ? C_GOAL : lit >> k & 1u ? col : C_BLACK);
        else
            w++;
    cv_blit(0, 134);
}

static void draw_scope(uint16_t col)            /* what the sound looks like, held still on a rising edge */
{
    uint32_t h0 = hist_w - 512u, start = 0, j;
    int32_t px = 0, py = 32;
    cv_begin(240, 64, C_BLACK);
    cv_rect(0, 32, 240, 1, C_GREY);
    for (j = 1; j < 256u; j++)
        if (hist[(h0 + j - 1u) & 511u] <= 0 && hist[(h0 + j) & 511u] > 0) { start = j; break; }
    for (j = 0; j < 240u; j++) {
        int32_t y = 32 - ((hist[(h0 + start + j) & 511u] * 60) >> 15);
        if (y < 1) y = 1;
        if (y > 62) y = 62;
        if (j) cv_line(px, py, (int32_t)j, y, col);
        px = (int32_t)j; py = y;
    }
    cv_blit(0, 44);
}

static void draw_animal(uint32_t pc, int32_t x, int32_t y);
static void draw_panel(const char *big, const char *small, uint16_t col)   /* two lines: a big one and a small one */
{
    cv_begin(240, 64, C_BLACK);
    if (!place && cur_patch == PI_ANIMALS && big[0]) {   /* an animal: its picture, then its name */
        int32_t x = (240 - 58 - text_w(&FONT_L, big)) / 2;
        draw_animal(drum_pc, x, 2);
        cv_text(x + 58, 6, &FONT_L, big, col);
        cv_blit(0, 44);
        return;
    }
    cv_text((240 - text_w(&FONT_L, big)) / 2, 2, &FONT_L, big, col);
    cv_text((240 - text_w(&FONT_S, small)) / 2, 42, &FONT_S, small, C_TXT);
    if (place && game == G_TAP) {               /* the rhythm: eight boxes, the filled ones are hits */
        uint32_t i;
        for (i = 0; i < 8u; i++) {
            int32_t x = 28 + (int32_t)i * 23;
            cv_rect(x, 8, 20, 24, C_GREY);
            if (q_mask >> i & 1u) cv_rect(x + 2, 10, 16, 20, C_WHITE);
            else cv_rect(x + 2, 10, 16, 20, C_BLACK);
        }
    }
    cv_blit(0, 44);
}

/* ---- the capybara: she sits by the name, sings when notes sound, nods to the band, blinks, and gets a yuzu on
 * her head for a chord or a right answer. b body, d dark, e eye, n nose, m mouth, o yuzu, g its leaf */
static const char *const CAPY[20] = {
    ".................g......",
    "................ooo.....",
    "...............ooooo....",
    "................ooo.....",
    "..............dd....dd..",
    "..............bbbbbbbb..",
    ".............bbbbbbbbbb.",
    ".............bbebbbbbbbb",
    ".............bbbbbbbbbnn",
    "...bbbbbbbbbbbbbbbbbbbbb",
    "..bbbbbbbbbbbbbbbbbbmmm.",
    ".bbbbbbbbbbbbbbbbbbbbb..",
    ".bbbbbbbbbbbbbbbbbbb....",
    ".bbbbbbbbbbbbbbbbbbb....",
    ".bbbbbbbbbbbbbbbbbbb....",
    ".bbbbbbbbbbbbbbbbbb.....",
    "..bbbbbbbbbbbbbbbb......",
    "..bbb.bbb....bbb.bbb....",
    "..bbb.bbb....bbb.bbb....",
    "..ddd.ddd....ddd.ddd....",
};
enum { CAPY_SING = 1, CAPY_BLINK = 2, CAPY_YUZU = 4, CAPY_UP = 8, CAPY_FLIP = 16, CAPY_STEP_A = 32, CAPY_STEP_B = 64 };   /* FLIP: she looks left; STEP: two feet off the ground */
static void capy_px(int cv, int32_t x, int32_t y, int32_t s, uint16_t c)
{
    if (cv) cv_rect(x, y, s, s, c);
    else lcd_fill((uint32_t)x, (uint32_t)y, (uint32_t)s, (uint32_t)s, c);
}
static void draw_capy(int cv, int32_t x, int32_t y, int32_t s, uint32_t f, uint32_t rows)   /* cv: into the canvas; s: pixel size; rows: 20 all of her, 12 swimming */
{
    uint32_t r, c;
    for (r = 0; r < rows; r++)
        for (c = 0; c < 24u; c++) {
            char ch = CAPY[r][c];
            uint16_t col;
            if (ch == '.') continue;
            int32_t cx = f & CAPY_FLIP ? 23 - (int32_t)c : (int32_t)c;
            if ((ch == 'o' || ch == 'g') && !(f & CAPY_YUZU)) continue;
            if (r >= 18u && f & CAPY_STEP_A && ((c >= 2u && c <= 4u) || (c >= 17u && c <= 19u))) continue;
            if (r >= 18u && f & CAPY_STEP_B && ((c >= 6u && c <= 8u) || (c >= 13u && c <= 15u))) continue;
            col = ch == 'b' ? RGB(176, 124, 72) : ch == 'd' ? RGB(104, 68, 40) : ch == 'n' ? RGB(60, 40, 30)
                : ch == 'o' ? RGB(255, 170, 30) : ch == 'g' ? RGB(70, 190, 70)
                : ch == 'e' ? (f & CAPY_BLINK ? RGB(104, 68, 40) : C_BLACK)
                : f & CAPY_SING ? RGB(60, 20, 20) : RGB(104, 68, 40);          /* m: the mouth, open when she sings */
            capy_px(cv, x + cx * s, y + (int32_t)r * s - (f & CAPY_UP ? s : 0), s, col);
            if (ch == 'm' && f & CAPY_SING) capy_px(cv, x + cx * s, y + (int32_t)(r + 1u) * s - (f & CAPY_UP ? s : 0), s, col);
        }
}
/* ---- the animals' pictures, 16 x 12, by the key's pitch class (the capybara is the one above). g G greens, w white,
 * k black, y yellow, o orange, b B browns, c light blue, e grey, p pink, r red */
static const char *const ANIMAL_PIC[11][12] = {
    {   /* frog */
        "..ggg......ggg..",
        ".gwwwg....gwwwg.",
        ".gwkwg....gwkwg.",
        ".gggggggggggggg.",
        "gggggggggggggggg",
        "gggggggggggggggg",
        "ggGggggggggggGgg",
        "gggGGGGGGGGGGggg",
        ".gggggggggggggg.",
        "..gggggggggggg..",
        ".ggg.gg..gg.ggg.",
        "ggg..gg..gg..ggg",
    },
    {   /* cricket */
        "................",
        "..........G...G.",
        "...........G.G..",
        "...........ggg..",
        "..gggggggggkgg..",
        ".gggggggggggg...",
        ".gGgGgGgGggg....",
        "..gggggggg......",
        "..G.g..g..G.....",
        ".G..g..g...G....",
        "G...g..g....G...",
        "................",
    },
    {   /* duck */
        "..........yyy...",
        ".........yyyyy..",
        ".........yykyyoo",
        ".........yyyyooo",
        "..........yyy...",
        "y........yyy....",
        "yy..yyyyyyyy....",
        "yyyyyyyyyyyy....",
        ".yyyyyyyyyyy....",
        "..yyyyyyyyy.....",
        ".....o..o.......",
        "....oo.oo.......",
    },
    {   /* bee */
        "......cc.cc.....",
        ".....ccccccc....",
        ".....ccccccc....",
        "......ccccc.....",
        "..ByByByByyyy...",
        ".BByByByByyyyy..",
        "BBByByByByykyy..",
        ".BByByByByyyyy..",
        "..ByByByByyyy...",
        "................",
        "................",
        "................",
    },
    {   /* bird */
        "................",
        "........ccc.....",
        ".......ccccc....",
        ".......cckccoo..",
        ".......ccccc....",
        "c...cccccccc....",
        "cc.cccccccc.....",
        ".ccccwwwwcc.....",
        "..cccwwwwc......",
        "....ccccc.......",
        ".....e.e........",
        ".....e.e........",
    },
    {   /* cow */
        ".e............e.",
        ".ee.wwwwwwww.ee.",
        "..wwwwwwwwwwww..",
        "ppwwwwwwwwBBBwpp",
        "..wwkwwwwkwBBw..",
        "..wwkwwwwkwwww..",
        "..wwwwwwwwwwww..",
        "...pppppppppp...",
        "..pppppppppppp..",
        "..ppBppppppBpp..",
        "..pppppppppppp..",
        "...pppppppppp...",
    },
    {   /* dog: a West Highland terrier */
        "...w........w...",
        "..wpw......wpw..",
        "..wpww.ww.wwpw..",
        ".wwwwwwwwwwwwww.",
        "w.wwwwwwwwwwww.w",
        "wwwwkkwwwwkkwwww",
        ".wwwwwwwwwwwwww.",
        "wwwwwewkkwewwwww",
        ".wwwewwkkwwewww.",
        "w.wwewwrrwweww.w",
        "..wwwewwwwewww..",
        "...w.ww.ww.ww...",
    },
    {   /* owl */
        ".b............b.",
        ".bb..........bb.",
        ".bbbbbbbbbbbbbb.",
        ".bwwwwbbbbwwwwb.",
        ".bwkkwbbbbwkkwb.",
        ".bwkkwboobwkkwb.",
        ".bwwwwboobwwwwb.",
        ".bbbbbbbbbbbbbb.",
        ".bBbBbBbBbBbBbb.",
        ".bbBbBbBbBbBbbb.",
        "..bbbbbbbbbbbb..",
        "....oo....oo....",
    },
    {   /* sheep */
        "...ww.wwww.ww...",
        "..wwwwwwwwwwww..",
        ".wwwwwwwwwwwwww.",
        "eewwweeeeeewwwee",
        ".eeweweeeewewee.",
        "..wweeeeeeeeww..",
        "..wweeeeeeeeww..",
        "...weeeeeeeew...",
        "....eeeppeee....",
        "....eeeeeeee....",
        "....eeeeeeee....",
        ".....eeeeee.....",
    },
    {   /* cat */
        "oo............oo",
        "ooo..........ooo",
        "oopo........opoo",
        "oooooooooooooooo",
        "oooooooooooooooo",
        "oogkoooooooogkoo",
        "oogkoooooooogkoo",
        "oooooooppooooooo",
        "oooooowppwoooooo",
        ".oooowwwwwwoooo.",
        "..oooowwwwoooo..",
        "....oooooooo....",
    },
    {   /* chick */
        "................",
        ".....yyyyyy.....",
        "....yyyyyyyy....",
        "....ykyyyyky....",
        "....yyyooyyy....",
        "....yyyooyyy....",
        "..yyyyyyyyyyyy..",
        ".yyyyyyyyyyyyyy.",
        ".yyyyyyyyyyyyyy.",
        "..yyyyyyyyyyyy..",
        "...yyyyyyyyyy...",
        ".....oo..oo.....",
    },
};
static void draw_animal(uint32_t pc, int32_t x, int32_t y)   /* into the canvas, 48 x 36 */
{
    uint32_t r, c;
    if (pc >= 11u) { draw_capy(1, x, y - 2, 2, 0, 20); return; }
    for (r = 0; r < 12u; r++)
        for (c = 0; c < 16u; c++) {
            char ch = ANIMAL_PIC[pc][r][c];
            if (ch == '.') continue;
            cv_rect(x + (int32_t)c * 3, y + (int32_t)r * 3, 3, 3,
                    ch == 'g' ? RGB(90, 200, 70) : ch == 'G' ? RGB(30, 110, 40) : ch == 'w' ? C_WHITE : ch == 'k' ? RGB(20, 20, 20)
                    : ch == 'y' ? RGB(255, 220, 60) : ch == 'o' ? RGB(255, 150, 30) : ch == 'b' ? RGB(176, 124, 72)
                    : ch == 'B' ? RGB(104, 68, 40) : ch == 'c' ? RGB(120, 200, 255) : ch == 'e' ? RGB(130, 130, 130)
                    : ch == 'p' ? RGB(255, 170, 190) : RGB(230, 60, 60));
        }
}
static void draw_top(const char *name, uint16_t col, uint32_t f)   /* the instrument or the game, and the capybara */
{
    cv_begin(240, 44, C_BLACK);
    cv_text((188 - text_w(&FONT_L, name)) / 2, 6, &FONT_L, name, col);
    draw_capy(1, 190, 2, 2, f, 20);
    draw_capy(1, 164, 23, 1, (f & ~(uint32_t)(CAPY_YUZU | CAPY_UP)) | (f & CAPY_UP ? 0u : CAPY_UP), 20);   /* a baby, nodding in between */
    cv_blit(0, 0);
}

/* ---- home: the pond. The mother, and a baby for every three levels the games have gone up: on the grass, in the
 * water. HOME shows it, and switching on. */
static uint32_t babies(void)
{
    uint32_t g, ups = 0;
    for (g = 0; g < NGAME; g++)
        if (set.lvl[g] > 1u && set.lvl[g] <= GMAX[g]) ups += set.lvl[g] - 1u;
    return 1u + (ups / 3u > 4u ? 4u : ups / 3u);
}
static const int16_t SPOT[5][2] = {{128, 104}, {96, 170}, {182, 110}, {150, 184}, {46, 186}};   /* land, pond, land, pond, pond */

/* The pond is alive: below the title the whole picture is drawn again every 50 ms, into the canvas in three strips
 * (cv_oy), so nothing flickers. It plays short scenes one after the other, each a function of its time t (ms):
 * the first baby is their actor, the others blink and bob as always. OCT- and OCT+ step through the scenes. */
#define POND_Y 58
#define POND_H 61                               /* a strip (the canvas holds 64 rows): three of them reach the bottom */
#define C_GRASS RGB(24, 70, 34)
#define C_WATER RGB(40, 120, 200)
#define C_RIPPLE RGB(110, 190, 240)
#define C_YUZU RGB(255, 170, 30)
#define C_LEAF RGB(70, 190, 70)
enum { SC_CALM, SC_SPLASH, SC_RIDE, SC_WREN, SC_NAP, SC_BATH, NSCENE };
static const uint16_t SC_MS[NSCENE] = {5000, 9000, 8000, 8000, 8000, 9000};

static int32_t ramp(int32_t a, int32_t b, int32_t t, int32_t t0, int32_t t1)   /* a until t0, b from t1, a line between */
{
    return t <= t0 ? a : t >= t1 ? b : a + (b - a) * (t - t0) / (t1 - t0);
}
static int32_t arc(int32_t h, int32_t t, int32_t t0, int32_t t1)   /* a hop: 0 at t0 and t1, h halfway */
{
    int32_t u;
    if (t <= t0 || t >= t1) return 0;
    u = (t - t0) * 256 / (t1 - t0);
    return h * 4 * u * (256 - u) / 65536;
}
static int32_t wave(uint32_t t, uint32_t period, int32_t amp)   /* -amp .. amp .. -amp, round and round */
{
    int32_t v = (int32_t)(t % period * 4u) * amp / (int32_t)period;
    return v < 2 * amp ? v - amp : 3 * amp - v;
}
static uint32_t stepf(int32_t t) { return (t / 120) & 1 ? CAPY_STEP_A : CAPY_STEP_B; }   /* walking: the feet in turn */
static void yuzu_at(int32_t x, int32_t y)
{
    cv_rect(x, y, 8, 8, C_YUZU);
    cv_rect(x + 2, y - 2, 2, 2, C_LEAF);
}
static void drops(int32_t x, int32_t y, int32_t t, int32_t t0, int32_t n, int32_t spread, int32_t up)   /* water thrown up at t0 */
{
    int32_t i, u = t - t0;
    if (u < 0 || u > 700) return;
    for (i = 0; i < n; i++)
        cv_rect(x + (2 * i - n + 1) * spread * u / 700 / n, y - arc(up + i % 3 * 6, u, 0, 700), 3, 3, C_RIPPLE);
}
/* the wren: t tail, w wing, h head, b back, c belly, e eye, y beak, l legs. [0] the wing up, [1] down */
static const char *const BIRD[2][6] = {
    {"t...hh..", "tw.hheyy", ".bbbbb..", ".bbccc..", "..bcc...", "...l.l.."},
    {"t...hh..", "t..hheyy", ".bbbbb..", "wwbccc..", "..bcc...", "...l.l.."},
};
static void bird_at(int32_t x, int32_t y, uint32_t wing, int flip)
{
    int32_t r, c;
    for (r = 0; r < 6; r++)
        for (c = 0; c < 8; c++) {
            char ch = BIRD[wing & 1u][r][c];
            if (ch == '.') continue;
            cv_rect(x + (flip ? 7 - c : c) * 3, y + r * 3, 3, 3,
                    ch == 'c' ? RGB(235, 205, 165) : ch == 'e' ? C_BLACK : ch == 'y' ? RGB(245, 195, 60)
                    : ch == 'l' || ch == 'w' || ch == 't' ? RGB(100, 62, 34) : RGB(160, 104, 58));
        }
}
static void note_at(int32_t x, int32_t y)       /* a little note, as she sings */
{
    cv_rect(x, y + 8, 5, 4, C_WHITE);
    cv_rect(x + 4, y, 1, 9, C_WHITE);
    cv_rect(x + 5, y, 3, 2, C_WHITE);
}
static void pond_scene(uint32_t sc, int32_t t, uint32_t now, uint32_t n)   /* the picture at t of scene sc, into the canvas */
{
    static uint8_t pw[61];                      /* the pond's half width, row by row */
    static const int16_t BX[5] = {40, 84, 150, 190, 118}, BY[5] = {192, 206, 204, 190, 214};   /* the bath's yuzus */
    uint32_t f = now / 300u, i, mf = CAPY_YUZU | (f % 7u == 4u ? CAPY_BLINK : 0u), bf = f % 6u == 3u ? CAPY_BLINK : 0u;
    int32_t dy, k, bx = SPOT[0][0], by = SPOT[0][1], my = 62, wet = 0;
    int32_t px = 0, py = 0, bird = 0, wing = 0, pflip = 0;
    if (!pw[30])
        for (dy = -30; dy <= 30; dy++) {
            int32_t w = 0, q = 900 - dy * dy;
            while ((w + 1) * (w + 1) <= q) w++;     /* sqrt(30^2 - dy^2) */
            pw[dy + 30] = (uint8_t)(w * 108 / 30);
        }
    cv_rect(0, 138, 240, 102, C_GRASS);
    for (dy = -30; dy <= 30; dy++)              /* the pond, its ripples drifting */
        cv_rect(120 - pw[dy + 30], 196 + dy, 2 * pw[dy + 30], 1,
                dy == -30 || ((uint32_t)(dy + 30) + 11u - now / 500u % 11u) % 11u == 0u ? C_RIPPLE : C_WATER);
    if (sc != SC_BATH) yuzu_at(116 + wave(now, 9000, 60), 212 + wave(now, 1300, 1));   /* a yuzu drifts in the water */

    switch (sc) {
    case SC_SPLASH:                             /* she runs to the pond, jumps in, swims, climbs out and shakes */
        if (t < 1800) {
            bx = ramp(128, 142, t, 600, 1800); by = ramp(104, 120, t, 600, 1800);
            if (t > 600) bf |= stepf(t);
        } else if (t < 2400) {
            bx = ramp(142, 128, t, 1800, 2400); by = ramp(120, 176, t, 1800, 2400) - arc(34, t, 1800, 2400);
        } else if (t < 6200) {
            wet = 1;
            bx = 100 - wave((uint32_t)(t - 2400), 3800, 28); by = 176 + ((t / 300) & 1 ? 2 : 0);
            if (t < 4300) bf |= CAPY_FLIP;
        } else if (t < 7000) {
            wet = t < 6600;
            bx = ramp(128, 142, t, 6200, 7000); by = ramp(176, 120, t, 6200, 7000);
        } else if (t < 8200) {
            bx = 142 + ((t / 70) & 1 ? 2 : -2); by = 120;
        } else {
            bx = ramp(142, 128, t, 8200, 9000); by = ramp(120, 104, t, 8200, 9000);
            bf |= CAPY_FLIP | stepf(t);
        }
        break;
    case SC_RIDE:                               /* she hops onto her mother's back for a ride */
        if (t < 1200) {
            bx = ramp(128, 112, t, 500, 1200);
            bf |= CAPY_FLIP | (t > 500 ? stepf(t) : 0u);
        } else if (t < 2200) {
            bx = ramp(112, 26, t, 1200, 2200); by = ramp(104, 60, t, 1200, 2200) - arc(22, t, 1200, 2200);
            bf |= CAPY_FLIP;
        } else if (t < 6500) {
            bx = 26; by = 60 + ((t / 600) & 1); my += (t / 600) & 1;
            if ((t / 1100) & 1) mf |= CAPY_BLINK;   /* (her eyes close: this is nice) */
        } else if (t < 7400) {
            bx = ramp(26, 112, t, 6500, 7400); by = ramp(60, 104, t, 6500, 7400) - arc(22, t, 6500, 7400);
        } else {
            bx = ramp(112, 128, t, 7400, 8000);
            bf |= stepf(t);
        }
        break;
    case SC_WREN:                               /* a wren lands on her back and sings */
        bird = 1;
        if (t < 2000) {
            px = ramp(244, 22, t, 0, 2000); py = ramp(64, 83, t, 0, 2000) + wave((uint32_t)t, 500, 5);
            wing = (t / 120) & 1; pflip = 1;
        } else if (t < 6000) {
            px = 22; py = 83 - arc(8, t, 2600, 2900) - arc(8, t, 3200, 3500); wing = 1;
            if (t < 2300) mf |= CAPY_UP;        /* (oh!) */
            by -= arc(7, t, 3700, 4000) + arc(7, t, 4500, 4800);
        } else {
            px = ramp(22, -30, t, 6000, 8000); py = ramp(83, 60, t, 6000, 8000) + wave((uint32_t)t, 500, 5);
            wing = (t / 120) & 1; pflip = 1;
        }
        break;
    case SC_NAP:                                /* the mother dozes off; the baby tiptoes over and wakes her */
        if (t >= 800 && t < 5200) { mf |= CAPY_BLINK; my += (t / 800) & 1; }
        if (t >= 5200 && t < 5500) mf = (mf & ~(uint32_t)CAPY_BLINK) | CAPY_UP;
        if (t < 6200) {
            bx = ramp(128, 116, t, 2500, 4500);
            bf |= CAPY_FLIP | (t > 2500 && t < 4500 ? (t / 300) & 1 ? CAPY_STEP_A : CAPY_STEP_B : 0u);
            by -= arc(14, t, 4500, 4850) + arc(14, t, 4850, 5200) + arc(10, t, 5300, 5600) + arc(10, t, 5700, 6000);
        } else {
            bx = ramp(116, 128, t, 6200, 7000);
            if (t < 7000) bf |= stepf(t);
        }
        break;
    case SC_BATH:                               /* yuzus drop into the water, one after the other: a yuzu bath */
        for (k = 0; k < 5; k++) by -= arc(8, t, 1100 + k * 1300, 1400 + k * 1300);
        break;
    default:
        break;
    }

    draw_capy(1, 14, my, 4, mf, 20);            /* the mother */
    for (i = 1; i < n; i++) {                   /* the other babies: they blink, the swimmers bob, each in its own time */
        uint32_t blink = (f + 2u * i) % 6u == 3u ? CAPY_BLINK : 0u;
        if (i == 1u || i >= 3u) {
            int32_t y = SPOT[i][1] + ((f + i) & 1u ? 2 : 0);
            draw_capy(1, SPOT[i][0], y, 2, blink, 12);
            cv_rect(SPOT[i][0] - 2, y + 24, 52, 2, C_RIPPLE);
        } else
            draw_capy(1, SPOT[i][0], SPOT[i][1], 2, blink, 20);
    }
    if (wet) {                                  /* the first baby, the scene's actor */
        draw_capy(1, bx, by, 2, bf, 12);
        cv_rect(bx - 2, by + 24, 52, 2, C_RIPPLE);
    } else
        draw_capy(1, bx, by, 2, bf, 20);

    if (sc == SC_SPLASH) {
        drops(152, 196, t, 2400, 7, 64, 30);
        if (t > 2400 && t < 3300) {             /* the ring it leaves */
            int32_t r = ramp(10, 44, t, 2400, 3300);
            cv_rect(152 - r - 6, 199, 6, 2, C_RIPPLE);
            cv_rect(152 + r, 199, 6, 2, C_RIPPLE);
        }
        for (k = 0; k < 4; k++) drops(166, 136, t, 7000 + k * 280, 4, 70, 10);
    }
    if (bird) {
        bird_at(px, py, (uint32_t)wing, pflip);
        for (k = 0; k < 3; k++) {               /* three notes float up */
            int32_t u = t - 3500 - k * 600;
            if (u > 0 && u < 1500) note_at(30 + k * 9 + wave((uint32_t)u, 700, 3), 76 - u * 20 / 1500);
        }
    }
    if (sc == SC_NAP)
        for (k = 0; k < 3; k++) {               /* z z z */
            int32_t u = t - 800 - k * 700;
            if (u > 0 && t < 5200) { u %= 2100; cv_text(108 + u * 18 / 2100, 74 - u * 16 / 2100, &FONT_S, "Z", C_TXT); }
        }
    if (sc == SC_BATH)
        for (k = 0; k < 5; k++) {
            int32_t u = t - 600 - k * 1300;
            if (u < 0) continue;
            yuzu_at(BX[k], u < 500 ? ramp(POND_Y, BY[k], u, 0, 500) : BY[k] + wave(now + (uint32_t)k * 400u, 1300, 1));
            drops(BX[k] + 4, BY[k], u, 500, 4, 26, 12);
        }
}

static const uint8_t POND_ENC[4] = {ENC_KNOB1, ENC_KNOB2, ENC_KNOB3, ENC_KNOB4};
static int32_t pond_presets;                    /* PRESETS turned at the pond: it leaves, and the turn still counts */
static uint32_t pond_b, pond_rel;               /* the button that closed it: the main loop still gets it (not OCT- / OCT+) */
static int8_t pond_want = -1;                   /* the scene the next visit begins with (the nap) */
static void pond_show(const char *title)        /* the pond, until a key, a knob or a button other than OCT- / OCT+ */
{
    static uint8_t scene, been;                 /* each visit begins with the next scene */
    const uint32_t octs = 1u << BTN_OCT_DOWN | 1u << BTN_OCT_UP;
    uint32_t start = fm1_ms, t0 = fm1_ms, drawn = 0, n = babies(), was = fm1_in.buttons & octs, i, rel;
    if (pond_want >= 0) scene = (uint8_t)pond_want, pond_want = -1;
    else if (been) scene = (uint8_t)((scene + 1u) % NSCENE);
    been = 1;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 8, 240, &FONT_L, title, C_YUZU, 1);
    if (title[1] == 'E') draw_text_box(0, 42, 240, &FONT_S, APP_NAME " " APP_VER, C_GREY, 1);   /* (hello: which build this is) */
    for (i = 0; i < 4u; i++) (void)fm1_enc_take(POND_ENC[i]);   /* (turns from before the pond don't close it) */
    (void)fm1_enc_take(ENC_PRESETS); (void)fm1_enc_take(ENC_SELECT); (void)fm1_enc_take(ENC_ALGORITHM);
    while (!fm1_in.notes && !(fm1_in.buttons & ~octs)) {
        uint32_t o = fm1_in.buttons & octs, strip;
        int32_t turned = 0, e;
        platform_poll();
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) pond_presets += e, turned = 1;
        for (i = 0; i < 4u; i++)
            if (fm1_enc_take(POND_ENC[i])) turned = 1;
        if (fm1_enc_take(ENC_SELECT) | fm1_enc_take(ENC_ALGORITHM)) turned = 1;
        if (turned || midi_in()) break;         /* (a note over MIDI plays, and the pond leaves) */
        if (o & ~was) {                         /* OCT+ the next scene, OCT- the one before */
            scene = (uint8_t)((scene + (o & 1u << BTN_OCT_UP ? 1u : NSCENE - 1u)) % NSCENE);
            t0 = fm1_ms;
            drawn = 0;
        }
        was = o;
        if (fm1_ms - t0 >= SC_MS[scene]) { scene = (uint8_t)((scene + 1u) % NSCENE); t0 = fm1_ms; }
        if (drawn && fm1_ms - drawn < 50u) continue;
        drawn = fm1_ms | 1u;
        for (strip = 0; strip < 3u; strip++) {
            cv_begin(240, POND_H, C_BLACK);
            cv_oy = -(int32_t)(POND_Y + POND_H * strip);
            pond_scene(scene, (int32_t)(fm1_ms - t0), fm1_ms - start, n);
            cv_oy = 0;
            cv_blit(0, POND_Y + POND_H * strip);
            platform_poll();
        }
    }
    pond_b = fm1_input_edges(&rel) & ~octs;     /* (the OCT presses were the pond's) */
    pond_rel = rel & ~octs;
}

static void draw_knobs(void)                    /* the four knobs: their names, how far, lit when on */
{
    static const char *const NAME[4] = {"ECHO", "LENGTH", "WOBBLE", "TONE"};   /* as the knobs, and FX ENV LFO under them */
    uint32_t val[4], on[4], i;
    val[0] = g_echo; val[1] = g_len; val[2] = g_wob; val[3] = g_tone;
    on[0] = g_echo_on; on[1] = g_len_on; on[2] = g_wob_on; on[3] = 1;
    cv_begin(240, 46, C_BLACK);
    for (i = 0; i < 4u; i++) {
        int32_t x = (int32_t)i * 60 + 6;
        uint16_t c = on[i] ? PATCH[cur_patch].col : C_GREY;
        cv_text(x + (48 - text_w(&FONT_S, NAME[i])) / 2, 2, &FONT_S, NAME[i], on[i] ? C_WHITE : C_GREY);
        cv_rect(x, 26, 48, 10, C_GREY);
        cv_rect(x + 1, 27, 46, 8, C_BLACK);
        cv_rect(x + 1, 27, (int32_t)(val[i] * 46u / 16u), 8, c);
    }
    cv_blit(0, 194);
}

static void status_text(char *s)                /* what is going on, in one line */
{
    char *p = s;
    if ((int32_t)(msg_until - fm1_ms) > 0) { sput(s, msg); return; }
    if (place) {
        p = nput(sput(p, "LEVEL "), set.lvl[game]);
        sput(p, "   SEL: ANOTHER ONE");
    } else if (rec == R_ARMED) sput(p, "REC: PLAY SOMETHING");
    else if (rec == R_FIRST) sput(p, "RECORDING");
    else if (rec == R_DUB) nput(sput(p, "RECORDING LAYER "), L.layers);
    else if (band) {                            /* the chord now and the one coming: what fits */
        if (band == CLICK) nput(sput(p, "CLICK   SPEED "), bpm);
        else {
            p = sput(sput(p, STYLE[band - 2u].name), ": ");
            sput(p, band_led ? BCH[band_ch].name : "A LOW KEY LEADS");
        }
    } else if (playing) sput(nput(sput(p, "SONG "), set.slot + 1u), " PLAYS");
    else if (L.len) sput(nput(sput(p, "SONG "), set.slot + 1u), ": PLAY TO HEAR IT");
    else {
        p = sput(nput(sput(p, "SONG "), set.slot + 1u), "  OCT ");   /* (which of her songs REC and PLAY mean) */
        *p++ = oct < 0 ? '-' : '+';
        p = nput(p, (uint32_t)(oct < 0 ? -oct : oct));
        nput(sput(p, "  SPEED "), bpm);
    }
}

/* what she holds, in words: one note's letter, several notes' letters, a chord's name */
static uint32_t held_text(char *big, char *small)   /* returns a number that changes when the notes do; 0: none */
{
    uint32_t v, n = 0, i, j, pcs = 0, sig;
    uint8_t note[NV];
    char *p = big;
    for (v = 0; v < NV; v++)
        if (voice[v].gate && voice[v].src == S_LIVE) {
            for (j = n; j && note[j - 1u] > voice[v].note; j--) note[j] = note[j - 1u];
            note[j] = voice[v].note;
            n++;
        }
    if (cur_patch >= PI_DRUMS && drum_n && fm1_ms - drum_ms < (cur_patch == PI_DRUMS ? 500u : 1200u)) {   /* the drum or the animal, by name */
        sput(big, cur_patch == PI_DRUMS ? DRUM_NAME[drum_pc] : ANIMAL_NAME[drum_pc]);
        small[0] = 0;
        return 0x40000000u | (drum_n << 1) | 1u;
    }
    if (!n) return 0;
    sig = n << 24;
    for (i = 0; i < n; i++) { pcs |= 1u << note[i] % 12u; sig = sig * 31u + note[i]; }
    big[0] = small[0] = 0;
    if (chord_name(pcs, note[0] % 12u, big)) { p = small; yuzu_until = fm1_ms + 800u; }   /* the chord big, its notes small; a yuzu */
    for (i = 0, j = 0, v = 0; i < n && j < 5u; i++) {   /* each letter once, from the lowest up */
        if (v >> note[i] % 12u & 1u) continue;
        v |= 1u << note[i] % 12u;
        if (j++) p = sput(p, " ");
        p = sput(p, NN[note[i] % 12u]);
    }
    if (n == 1u && note[0] == 60u && !PATCH[cur_patch].tr) sput(small, "MIDDLE C");
    return sig | 1u;
}

/* ================================================================ the main loop */
static void echo_time(void)                     /* the echo answers an eighth note later */
{
    uint32_t f = 44100u * 30u / bpm;
    g_echo_len = f > ELEN - 1u ? ELEN - 1u : f;
}
static void song_load(void)                     /* her song set.slot, or an empty one */
{
    if (slot_load(set.slot, &L, sizeof L) || L.n > NEV || !L.len || !L.layers) L.len = 0, L.n = 0, L.layers = 0;
    song_dirty = 0;
}
static void go_home(void)
{
    loop_stop();
    if (rec != R_IDLE) rec_close(), loop_stop();
    band = 0;
    band_quiet();
    stop_src(S_CUE);
    cue_on = 255;
    place = 0;
    cur_patch = 0; oct = 0; bpm = 100;
    g_tone = 8; g_len = 13; g_wob = 8; g_echo = 9;
    g_len_on = g_wob_on = g_echo_on = 0;
    arp_on = 0;
    hint_mask = 0;
    echo_time();
}
static void wait_quiet(void)                    /* a flash erase stalls the sound: let it ring out first */
{
    uint32_t v, t0 = fm1_ms;
    for (v = 0; v < NV; v++) voice[v].gate = 0;
    while (!quiet() && fm1_ms - t0 < 1500u) platform_poll();
}

static void app_main(void)
{
    uint32_t prev = 0, k, hash, keys_drawn = ~0u, name_drawn = ~0u, knobs_drawn = ~0u, status_drawn = ~0u;
    uint32_t panel_drawn = ~0u, scope_t = 0, scope_idle = 0, home_t = 0, edit_t = 0, home_done = 0, edit_done = 0;
    uint32_t was_held = 0, idle_t, idle_sig = 0;
    uint8_t held_note[27] = {0}, held_pi[27] = {0}, held[27] = {0};
    char line[40], big[40] = "", small[40] = "";

    if (settings_load(&set, sizeof set)) {      /* (or the settings of before PLAY ALONG and the songs: 9 bytes, the same start) */
        if (settings_load(&set, 9)) { set.quiet = 0; set.letters = 1; set.song = 0; for (k = 0; k < NGAME; k++) set.lvl[k] = 1; }
        set.lvl[G_ALONG] = 1;
        set.slot = 0;
    }
    if (set.slot >= NSAVE) set.slot = 0;
    g_quiet = set.quiet ? 1 : 0;
    letters = set.letters ? 1 : 0;
    song_load();
    go_home();
    tick_ms = fm1_ms;

    pond_show("HELLO!");          /* hello: the pond, until she picks something */
    tick_ms = idle_t = fm1_ms;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, goal = 0, glow = 0, top = 0;
        int32_t e;
        platform_poll();
        clock_run();

        /* ---- the keys */
        keys = fm1_in.notes & 0x7FFFFFFu;
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {
            if (down >> k & 1u) {
                if (!arp_on) {                  /* (with the arp on, arp_run plays the held keys) */
                    held[k] = 1;
                    held_note[k] = (uint8_t)(53 + 12 * oct + (int32_t)k);
                    held_pi[k] = cur_patch;
                    live_on(held_note[k], held_pi[k], 1);
                }
                bounce_t = fm1_ms;
                if (place) game_key(k);
                else band_lead(k, keys);
            }
            if (up >> k & 1u && held[k]) { held[k] = 0; live_off(held_note[k], held_pi[k], 1); }
        }
        if (place) game_run(keys);
        click_mute = place && game == G_TAP;

        /* ---- the buttons */
        b = fm1_input_edges(&rel) | pond_b;     /* (and the button that closed the pond) */
        rel |= pond_rel;
        pond_b = pond_rel = 0;
        if (place && game == G_HILO) {          /* here OCT- and OCT+ are the answer: lower, higher */
            uint32_t ans = b & (1u << BTN_OCT_DOWN | 1u << BTN_OCT_UP);
            if (ans && g_phase == P_WAIT) {
                if ((ans == 1u << BTN_OCT_UP) == (q_hi != 0u)) game_right();
                else game_again();
            }
        } else if (!place) {
            if (b & 1u << BTN_OCT_DOWN && oct > -2) oct--;
            if (b & 1u << BTN_OCT_UP && oct < 2) oct++;
        }
        if (b & 1u << BTN_FX) { g_echo_on = !g_echo_on; say(g_echo_on ? "ECHO ON" : "ECHO OFF"); }
        if (b & 1u << BTN_LFO) { g_wob_on = !g_wob_on; say(g_wob_on ? "WOBBLE ON" : "WOBBLE OFF"); }
        if (b & 1u << BTN_ENV) { g_len_on = !g_len_on; say(g_len_on ? "LENGTH ON" : "LENGTH OFF"); }
        if (b & 1u << BTN_GLO) {                /* the note letters, or the sound's wave */
            letters = !letters;
            set.letters = letters;
            set_dirty = 1;
            keys_drawn = panel_drawn = ~0u;
            was_held = 0;
            say(letters ? "LETTERS" : "WAVE");
        }
        if (b & 1u << BTN_SEL) {                /* the games; in a game: another question */
            if (!place) {
                for (k = 0; k < 27u; k++)
                    if (held[k]) { held[k] = 0; live_off(held_note[k], held_pi[k], 1); }
                go_home();
                place = 1;
                name_drawn = ~0u;
                game_enter(game);
            } else {
                if (game == G_TUNES) set.song = (uint8_t)((set.song + 1u) % NSONG);
                if (game == G_COPY) q_n = 0;
                stop_src(S_CUE);
                cue_on = 255;
                game_new();
            }
        }
        if (place && b & 1u << BTN_SEQ && game == G_ALONG) say("ITS DRUMS KEEP THE TIME");
        else if (place && b & 1u << BTN_SEQ) {  /* in the games: the click on and off */
            band = band ? 0 : CLICK;
            band_origin = tick;
            band_step = 0xFFFFFFFFu;
            say(band ? "CLICK ON" : "CLICK OFF");
        }
        if (place && b & (1u << BTN_ARP | 1u << BTN_REC | 1u << BTN_PLAY | 1u << BTN_SAVE | 1u << BTN_EDIT))
            say("HOME: BACK TO PLAYING");
        else if (!place) {
            if (b & 1u << BTN_ARP) {
                arp_on = !arp_on;
                for (k = 0; k < 27u; k++)       /* the keys held now are the arp's from here */
                    if (held[k]) { held[k] = 0; live_off(held_note[k], held_pi[k], 1); }
                say(arp_on ? "ARP ON: HOLD SOME KEYS" : "ARP OFF");
            }
            if (b & 1u << BTN_SEQ) {            /* the band: each press the next one, then off */
                band = (uint8_t)((band + 1u) % (NSTYLE + 2u));
                band_quiet();
                band_origin = playing ? loop_t0 : tick;   /* its "one" is now, or the recording's */
                band_step = 0xFFFFFFFFu;
                band_led = 0;                   /* each band starts with its drums alone */
                if (!band) say("BAND OFF");
            }
            if (b & 1u << BTN_REC) {
                if (rec == R_ARMED) { rec = R_IDLE; say("REC OFF"); }
                else if (rec != R_IDLE) rec_close();
                else if (!L.len) rec = R_ARMED;
                else if (L.layers >= 250u) say("FULL");
                else {                          /* a layer on top */
                    if (!playing) loop_start(tick);
                    L.layers++;
                    open_n = 0;
                    rec = R_DUB;
                }
            }
            if (b & 1u << BTN_PLAY) {
                if (rec == R_FIRST) rec_close();
                else if (playing) loop_stop();
                else if (L.len) { rec = R_IDLE; loop_start(tick); if (band) band_origin = tick, band_step = 0xFFFFFFFFu; }
                else say("NOTHING RECORDED YET");
            }
            if (b & 1u << BTN_EDIT) { edit_t = fm1_ms; edit_done = 0; }
            if (fm1_in.buttons & 1u << BTN_EDIT && !edit_done && fm1_ms - edit_t > 1000u) {
                edit_done = 1;
                if (rec != R_IDLE) rec_close();
                loop_stop();
                L.len = 0; L.n = 0; L.layers = 0;
                song_dirty = 1;
                say("RECORDING ERASED");
            }
            if (rel & 1u << BTN_EDIT && !edit_done) {
                if (!L.len && rec == R_IDLE) say("NOTHING TO UNDO");
                else { loop_undo(); say(L.len ? "LAST LAYER GONE" : "RECORDING ERASED"); }
            }
            if (b & 1u << BTN_SAVE) {
                if (rec != R_IDLE) rec_close();
                loop_stop();
                band = 0;
                band_quiet();
                draw_text_box(0, 112, 240, &FONT_S, "SAVING...", C_WHITE, 1);
                wait_quiet();
                slot_save(set.slot, &L, sizeof L);
                song_dirty = 0;
                { char t[24]; sput(nput(sput(t, "SONG "), set.slot + 1u), L.len ? " SAVED" : " SAVED (EMPTY)"); say(t); }
                status_drawn = ~0u;
                prev = fm1_in.notes & 0x7FFFFFFu;
                tick_ms = fm1_ms;
            }
        }
        if (b & 1u << BTN_HOME) { home_t = fm1_ms; home_done = 0; }
        if (fm1_in.buttons & 1u << BTN_HOME && !home_done && fm1_ms - home_t > 3000u) {
            home_done = 1;                      /* for the grown-ups: half the loudness */
            g_quiet = !g_quiet;
            set.quiet = g_quiet;
            set_dirty = 1;
            say(g_quiet ? "QUIET ON" : "QUIET OFF");
        }
        if (rel & 1u << BTN_HOME && !home_done) {
            for (k = 0; k < 27u; k++)
                if (held[k]) { held[k] = 0; live_off(held_note[k], held_pi[k], 1); }
            go_home();
            pond_show("HOME");                  /* the capybaras' home, until she picks something */
            lcd_fill(0, 0, 240, 240, C_BLACK);
            name_drawn = panel_drawn = keys_drawn = knobs_drawn = status_drawn = ~0u;
            was_held = 0;
            prev = 0;
            tick_ms = fm1_ms;
        }

        /* ---- the nap: nothing touched and nothing sounding for NAP_MS, and the capybaras go to sleep at the
         * pond. Everything stays as it is: the instrument, the knobs, the song. */
        hash = cur_patch | bpm << 4 | (uint32_t)g_tone << 12 | (uint32_t)g_len << 17 | (uint32_t)g_wob << 22 | (uint32_t)g_echo << 27;
        hash ^= (uint32_t)set.slot * 40503u ^ (uint32_t)(oct + 2) * 7919u;
        if (keys || b || rel || fm1_in.buttons || hash != idle_sig || place || band || playing || rec != R_IDLE || !quiet())
            idle_t = fm1_ms;
        idle_sig = hash;
        if (fm1_ms - idle_t > NAP_MS) {
            pond_want = SC_NAP;
            pond_show("ZZZ");
            lcd_fill(0, 0, 240, 240, C_BLACK);
            name_drawn = panel_drawn = keys_drawn = knobs_drawn = status_drawn = ~0u;
            was_held = 0;
            prev = 0;
            tick_ms = idle_t = fm1_ms;
        }

        /* ---- the knobs */
        e = fm1_enc_take(ENC_PRESETS) + pond_presets;
        pond_presets = 0;
        if (e != 0) {
            cur_patch = (uint8_t)(((int32_t)cur_patch + e % (int32_t)NPATCH + (int32_t)NPATCH) % (int32_t)NPATCH);
            if (place && cur_patch >= PI_DRUMS) cur_patch = e > 0 ? 0 : PI_DRUMS - 1u;   /* (the games ask for notes: no drums or animals there) */
            if (place) say(PATCH[cur_patch].name);
            knobs_drawn = keys_drawn = panel_drawn = ~0u;
        }
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {
            int32_t v = (int32_t)bpm + 4 * e;
            bpm = (uint32_t)(v < 60 ? 60 : v > 180 ? 180 : v);
            echo_time();
            nput(sput(line, "SPEED "), bpm);
            say(line);
        }
        if ((e = fm1_enc_take(ENC_SELECT)) != 0 && place) {   /* which game */
            game_enter((uint32_t)(((int32_t)game + e % (int32_t)NGAME + (int32_t)NGAME) % (int32_t)NGAME));
            name_drawn = ~0u;
        } else if (e) {                         /* which of her songs: the one she leaves is kept */
            char t[24];
            if (rec != R_IDLE) rec_close();
            loop_stop();
            if (song_dirty) {
                band = 0;
                band_quiet();
                draw_text_box(0, 112, 240, &FONT_S, "SAVING...", C_WHITE, 1);
                wait_quiet();
                slot_save(set.slot, &L, sizeof L);
                song_dirty = 0;
            }
            set.slot = (uint8_t)(((int32_t)set.slot + e % (int32_t)NSAVE + (int32_t)NSAVE) % (int32_t)NSAVE);
            set_dirty = 1;
            song_load();
            sput(nput(sput(t, "SONG "), set.slot + 1u), L.len ? "" : ": EMPTY");
            say(t);
            status_drawn = ~0u;
            prev = fm1_in.notes & 0x7FFFFFFu;
            tick_ms = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_KNOB4)) != 0) { int32_t v = g_tone + e; g_tone = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v); }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) { int32_t v = g_len + e; g_len = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v); g_len_on = 1; }
        if ((e = fm1_enc_take(ENC_KNOB3)) != 0) { int32_t v = g_wob + e; g_wob = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v); g_wob_on = 1; }
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) { int32_t v = g_echo + e; g_echo = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v); g_echo_on = 1; }

        /* ---- the lights */
        {
            int32_t base = 53 + 12 * oct;
            for (k = 0; k < NV; k++)
                if (voice[k].gate && voice[k].note >= base && voice[k].note < base + 27
                    && (voice[k].src == S_LIVE || voice[k].src == S_LOOP || (voice[k].src == S_CUE && !cue_hide && voice[k].note == cue_on)))
                    lit |= 1u << (voice[k].note - base);
            if (cur_patch >= PI_DRUMS && !arp_on && !place) lit |= keys;   /* (drums and animals are no voices: the keys held) */
        }
        if (place) {
            goal = hint_mask;
            if (game == G_TUNES) goal = 1u << song_key(SONG[set.song % NSONG].notes[song_pos]);
            glow = 1u << 7 | 1u << 19;          /* C4, C5: to find your way */
        } else
            glow = band > CLICK && band_led ? pc_keys(chord_pcs(band_ch), 0, 26) : 1u << 7 | 1u << 19;   /* with the band: the notes that fit */
        {
            static uint32_t breathing;
            uint32_t left = flash_until - fm1_ms, lvl = 0;
            if ((int32_t)left > 0 && left <= BREATH_MS) {   /* right: the keys breathe */
                uint32_t i = (BREATH_MS - left) * 32u / BREATH_MS;
                lvl = i < 16u ? i : 31u - i;
                if (BREATH_NS[lvl]) keys_backlight(0x7FFFFFFu, BREATH_NS[lvl]);
                else keys_backlight(0, 0), top = 0x7FFFFFFu;   /* (the lights only, not the screen) */
                breathing = 1;
            } else if (breathing) {
                keys_backlight(0, 0);
                breathing = 0;
            }
        }
        for (k = 0; k < 27u; k++) {
            uint32_t on = (lit | goal | top) >> k & 1u;
            if ((int32_t)(party_until - fm1_ms) > 0) on = k == (fm1_ms >> 5) % 27u;   /* a light runs along the keys */
            fm1_led_key(KEY_LED(k), (int)on);
            fm1_led_key_glow(KEY_LED(k), !on && glow >> k & 1u);
        }
        fm1_led_key(BTN_OCT_DOWN, place ? game == G_HILO && g_phase == P_WAIT : oct < 0);
        fm1_led_key(BTN_OCT_UP, place ? game == G_HILO && g_phase == P_WAIT : oct > 0);
        fm1_led_key(BTN_FX, g_echo_on);
        fm1_led_key(BTN_LFO, g_wob_on);
        fm1_led_key(BTN_ENV, g_len_on);
        fm1_led_key(BTN_GLO, letters);
        fm1_led_key(BTN_ARP, arp_on);
        fm1_led_key(BTN_SEL, place);
        fm1_led_key(BTN_SEQ, band && (tick - band_origin) % PPQ < 120u);              /* blinks on the beat */
        fm1_led_key(BTN_REC, rec == R_ARMED ? (int)(fm1_ms >> 8 & 1u) : rec != R_IDLE);
        fm1_led_key(BTN_PLAY, playing);
        fm1_led_key(BTN_SAVE, 0);
        fm1_led_key(BTN_HOME, 0);
        fm1_led_key(BTN_EDIT, 0);

        /* ---- the screen: only what changed */
        idle_ok = 1;
        {                                       /* the top line: the instrument or the game, and the capybara */
            uint32_t f = 0, sounding = 0;
            for (k = 0; k < NV; k++)
                if (voice[k].gate && (voice[k].src == S_LIVE || voice[k].src == S_CUE)) sounding = 1;
            if (sounding) f |= CAPY_SING;
            if (fm1_ms % 4000u < 150u) f |= CAPY_BLINK;
            if (band ? (tick - band_origin) % PPQ < 200u : sounding && fm1_ms - bounce_t < 120u) f |= CAPY_UP;
            if ((int32_t)(yuzu_until - fm1_ms) > 0 || (int32_t)(party_until - fm1_ms) > 0) f |= CAPY_YUZU;
            hash = (place ? 100u + game : cur_patch) | f << 8;
            if (hash != name_drawn) {
                if ((hash ^ name_drawn) & 255u) { knobs_drawn = keys_drawn = panel_drawn = ~0u; scope_idle = 0; }
                name_drawn = hash;
                draw_top(place ? GNAME[game] : PATCH[cur_patch].name, place ? C_GOAL : PATCH[cur_patch].col, f);
            }
        }
        if (place) {                            /* the game's question */
            game_text(big, small);
            for (hash = 5381u + g_phase + q_mask, k = 0; big[k]; k++) hash = hash * 33u ^ (uint8_t)big[k];
            for (k = 0; small[k]; k++) hash = hash * 33u ^ (uint8_t)small[k];
            if (hash != panel_drawn) { panel_drawn = hash; draw_panel(big, small, C_WHITE); }
        } else if (letters) {                   /* the notes she holds, by name; the last ones stay, greyed */
            uint32_t sig = held_text(big, small);
            hash = sig ? sig : was_held ? 2u : 4u;
            if (hash != panel_drawn) {
                panel_drawn = hash;
                if (!sig && !was_held) big[0] = small[0] = 0;
                draw_panel(big, small, sig ? PATCH[cur_patch].col : C_GREY);
            }
            if (sig) was_held = 1;
        } else if (fm1_ms - scope_t > 60u) {    /* the wave: while something sounds, and once more after */
            int busy = !quiet() || band;
            scope_t = fm1_ms;
            if (busy || scope_idle < 8u) draw_scope(PATCH[cur_patch].col);
            scope_idle = busy ? 0 : scope_idle + 1u;
            panel_drawn = ~0u;
        }
        status_text(line);
        for (hash = 5381u, k = 0; line[k]; k++) hash = hash * 33u ^ (uint8_t)line[k];
        if (hash != status_drawn) {
            status_drawn = hash;
            draw_text_box(0, 112, 240, &FONT_S, line, (int32_t)(msg_until - fm1_ms) > 0 ? C_WHITE : C_TXT, 1);
        }
        hash = lit * 2654435761u ^ goal * 40503u ^ letters ^ (band && !place ? glow * 7919u + band + 256u * band_led : 0u);
        if (hash != keys_drawn) { keys_drawn = hash; draw_keys(lit, goal, band > CLICK && !place && band_led ? glow : 0u, PATCH[cur_patch].col, band > CLICK && !place); }
        hash = g_tone | (uint32_t)g_len << 5 | (uint32_t)g_wob << 10 | (uint32_t)g_echo << 15
             | (uint32_t)g_len_on << 20 | (uint32_t)g_wob_on << 21 | (uint32_t)g_echo_on << 22;
        if (hash != knobs_drawn) { knobs_drawn = hash; draw_knobs(); }
        idle_ok = 0;

        if (set_dirty && quiet() && !playing && !band && (!place || g_phase == P_WAIT)) {   /* settings: saved when nothing sounds */
            settings_save(&set, sizeof set);
            set_dirty = 0;
            tick_ms = fm1_ms;
        }
    }
}
