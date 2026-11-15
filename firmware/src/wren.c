/* SPDX-License-Identifier: GPL-3.0-only */
/* Wren: a first real instrument for a child of five to seven, on the M-VAVE FM-1.
 *
 * What it holds to:
 *   1. She is the musician. Nothing plays unless she plays it or switched it on: her own recording (REC / PLAY)
 *      and a band she starts and plays over (SEQ). No demo.
 *   2. Real instruments and real names: piano, organ, bells, guitar, bass, strings, brass, flute; the notes by their
 *      letters, the chords by their names. What she learns here is true at any piano.
 *   3. Every button does what is printed on it. Two places: playing, and the games (SEL). HOME always goes back.
 *   4. The games make their questions up, so they do not run out; a wrong answer takes nothing away.
 *
 *   keys        play (12 voices, two-operator FM). The screen names the note, the notes, or the chord she holds
 *   PRESETS     the instrument
 *   OCT- OCT+   the octave
 *   KNOB 1      TONE: dark .. bright
 *   KNOB 2      LENGTH: how long notes ring          ENV switches it on / off
 *   KNOB 3      WOBBLE: vibrato                      LFO switches it on / off
 *   KNOB 4      ECHO                                 FX  switches it on / off
 *   GLO         the note letters (on the screen's keys, and what she holds), or the sound's wave
 *   ARP         held keys play one after the other
 *   SEQ         the band: POP, BLUES, WALTZ, LULLABY, off. Drums, bass and chords in C; the screen says the chord
 *               now and the next one, the keys that fit glow       ALGORITHM: the SPEED of band, arp and recording
 *   REC         record what you play; REC again: it goes round. REC while it plays: add a layer
 *   PLAY        the recording on / off
 *   EDIT        tap: take the last layer away. Held a second: erase the recording
 *   SAVE        keep the recording for next time
 *   SEL         the games (SELECT: which one; SEL again: another question): TUNES, FIND IT, COPY ME, HIGH LOW,
 *               CHORDS, TAP IT
 *   HOME        back to the start: piano, everything off. Held 3 s (grown-ups): QUIET, half the loudness
 *
 * Built on Dinghy (zvenson) and its platform (Felucca by Leo Kuroshita, with SLOOP's changes): platform.c and below.
 * Integers only: no float, no 64-bit division (docs/RULES.md). tests/host.c runs this file on a computer. */
#define APP_NAME "WREN"
#define STORE_MAGIC 0x4E455257u                 /* "WREN": never another firmware's data */
static void app_idle(void);
#define LCD_IDLE app_idle                       /* while the screen's DMA runs: the clock and MIDI */
#include "platform.c"

/* ================================================================ the sound (audio ISR) */
#define NV 12u                                  /* voices */
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

#define NPATCH 8u
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

static void app_audio(int32_t *out, uint32_t n)
{
    static int32_t mono[256];
    static uint32_t lfo_ph, ew, k_seen, s_seen, h_seen, k_ph, k_sw, s_ph, rnd = 1;
    static int32_t k_amp, s_amp, h_amp, h_prev;
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
    }

    /* the drums: a kick (a sine falling from 180 to 50 Hz), a snare (noise and a tone), a hat (bright noise) */
    if (k_trig != k_seen) { k_seen = k_trig; k_amp = 30000; k_sw = 12660000u; k_ph = 0; }
    if (s_trig != s_seen) { s_seen = s_trig; s_amp = 22000; }
    if (h_trig != h_seen) { h_seen = h_trig; h_amp = 9000; }
    if (k_amp > 16 || s_amp > 16 || h_amp > 16)
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
            h_prev = nz;
            mono[i] += x;
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

static void play(uint32_t note, uint32_t pi, uint32_t src, uint32_t vol)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    int32_t n;
    if (note > 127u) return;
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
static uint32_t msg_until, party_until, yuzu_until, bounce_t;
static void say(const char *s)
{
    uint32_t i = 0;
    while (s[i] && i < 30u) { msg[i] = s[i]; i++; }
    msg[i] = 0;
    msg_until = fm1_ms + 1600u;
}

/* ---- the band: drums, a bass and chords she switches on and plays over. It goes round a few chords in C. */
static const struct { uint8_t pc, minor; const char *name; } BCH[6] = {
    {0, 0, "C"}, {2, 1, "D MINOR"}, {4, 1, "E MINOR"}, {5, 0, "F"}, {7, 0, "G"}, {9, 1, "A MINOR"}};
typedef struct {
    const char *name;
    uint8_t spb, nbars;                         /* sixteenths a bar, bars */
    uint8_t prog[12];                           /* a chord (BCH) for each bar */
    uint16_t kick, snare, hat, bass, comp, arp; /* bit s: on sixteenth s of the bar */
    uint8_t walk;                               /* the bass walks up the chord */
} style_t;
#define NSTYLE 4u
static const style_t STYLE[NSTYLE] = {
    {"POP", 16, 4, {0, 4, 5, 3}, 0x0501, 0x1010, 0x5555, 0x0541, 0x1041, 0, 0},
    {"BLUES", 16, 12, {0, 0, 0, 0, 3, 3, 0, 0, 4, 3, 0, 4}, 0x0101, 0x1010, 0x5555, 0x1111, 0x4444, 0, 1},
    {"WALTZ", 12, 4, {0, 3, 4, 0}, 0x001, 0, 0x110, 0x001, 0x110, 0, 0},
    {"LULLABY", 16, 4, {5, 3, 0, 4}, 0, 0, 0x1010, 0x0001, 0, 0x5555, 0},
};
static uint32_t band_origin, band_step = 0xFFFFFFFFu, band_ch, band_next;
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
    uint32_t st, s, bar, r, third, i;
    if (!band) return;
    y = &STYLE[band - 1u];
    st = (tick - band_origin) / (PPQ / 4u);
    if (st == band_step) return;
    band_step = st;
    s = st % y->spb;
    bar = st / y->spb;
    band_ch = y->prog[bar % y->nbars];
    band_next = y->prog[(bar + 1u) % y->nbars];
    r = BCH[band_ch].pc;
    third = BCH[band_ch].minor ? 3u : 4u;
    if (y->kick >> s & 1u) k_trig++;
    if (y->snare >> s & 1u) s_trig++;
    if (y->hat >> s & 1u) h_trig++;
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

/* ---- the recording: note events in ticks, sorted; layers on top of each other */
#define NEV 1500u
typedef struct { uint32_t t; uint8_t note, pi, on, layer; } ev_t;
static struct { uint32_t len; uint16_t n; uint8_t layers, pad; ev_t ev[NEV]; } L;   /* (saved as it is: slot 0) */
enum { R_IDLE, R_ARMED, R_FIRST, R_DUB };
static uint8_t rec, playing;
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
/* Six games that never run out: each makes its questions up as it goes, gets a little harder after five right
 * answers and remembers its level. A wrong answer costs nothing: the question comes again, then with a hint. */
enum { G_TUNES, G_FIND, G_COPY, G_HILO, G_CHORD, G_TAP, NGAME };
static const char *const GNAME[NGAME] = {"TUNES", "FIND IT", "COPY ME", "HIGH LOW", "CHORDS", "TAP IT"};
static const uint8_t GMAX[NGAME] = {1, 5, 12, 5, 4, 4};
enum { P_NEW, P_CUE, P_WAIT, P_DONE };
static struct { uint8_t quiet, letters, song, lvl[NGAME]; } set;   /* what survives a power-off */
static uint32_t set_dirty;
static uint8_t game, g_phase, g_ok, g_miss, cue_hide;
static uint32_t g_t, rnd_m = 12345u, hint_mask, q_pc = 99, q_n, q_pos, q_hi, q_mask, q_t0, chord_seen;
static uint8_t q_note[16];
static uint32_t tap_t[8];

static uint32_t rnd_n(uint32_t n)
{
    rnd_m = rnd_m * 1664525u + 1013904223u + fm1_ms;
    return (rnd_m >> 16) % n;
}

/* a cue: the notes a game plays to her (key numbers, 0 = F3; 255 = a drum) */
static struct { uint16_t t; uint8_t key, dur; } cue[16];
static uint32_t cue_n, cue_i, cue_t0, cue_off, cue_on = 255, cue_pi, ding_off;
static void cue_add(uint32_t t, uint32_t key, uint32_t dur_ms)
{
    if (cue_n < 16u) { cue[cue_n].t = (uint16_t)t; cue[cue_n].key = (uint8_t)key; cue[cue_n].dur = (uint8_t)(dur_ms / 10u); cue_n++; }
}
static void cue_go(uint32_t wait_ms) { cue_i = 0; cue_t0 = fm1_ms + wait_ms; g_phase = P_CUE; }
static void cue_run(void)
{
    if (cue_on != 255u && (int32_t)(fm1_ms - cue_off) >= 0) { stop(cue_on, cue_pi, S_CUE); cue_on = 255; }
    if (ding_off && (int32_t)(fm1_ms - ding_off) >= 0) { stop(84, 2, S_CUE); stop(88, 2, S_CUE); ding_off = 0; }
    if (g_phase != P_CUE || !place) return;
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
static const struct { const char *name, *notes; } SONG[] = {
    {"TWINKLE TWINKLE", "ccggaagffeeddcggffeedggffeedccggaagffeeddc"},
    {"MARY HAD A LAMB", "edcdeeedddeggedcdeeeeddedc"},
    {"ODE TO JOY", "eefggfedccdeeddeefggfedccdedcc"},
    {"FRERE JACQUES", "cdeccdecefgefggagfecgagfeccxccxc"},
    {"ROW YOUR BOAT", "cccdeedefgCCCgggeeecccgfedc"},
    {"OLD MACDONALD", "cccxyyxeeddcxcccxyyxeeddc"},
    {"JINGLE BELLS", "eeeeeeegcdefffffeeeeddedgeeeeeeegcdefffffeeeggfdc"},
    {"LONDON BRIDGE", "gagfefgdefefggagfefgdgec"},
    {"HAPPY BIRTHDAY", "ggagCbggagDCggGECbaFFECDC"},
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
static void game_right(void)
{
    static const char *const YES[4] = {"YES!", "THAT'S IT!", "GOOD EAR!", "RIGHT!"};
    play(84, 2, S_CUE, 150);                    /* two little bells */
    play(88, 2, S_CUE, 150);
    ding_off = fm1_ms + 200u;
    yuzu_until = fm1_ms + 1500u;
    if (++g_ok >= 5u && set.lvl[game] < GMAX[game]) {
        set.lvl[game]++;
        g_ok = 0;
        set_dirty = 1;
        say("LEVEL UP!");
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
        if (++song_pos >= song_len()) {
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
    if (g_phase == P_DONE && (int32_t)(fm1_ms - g_t) >= 0) game_new();
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
    case G_TAP:
        sput(small, g_phase == P_CUE ? "LISTEN TO THE DRUM" : "TAP IT ON ANY KEY");
        break;
    }
}

/* ================================================================ the clock runs everything that keeps time */
static void midi_in(void);
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
static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2) != 0) {
        uint32_t ty = st & 0xF0u;
        if (st >= 0xF0u || d1 > 127u) continue;
        if (ty == 0x90u && d2) { midi_pi[d1] = cur_patch; live_on(d1, cur_patch, 0); }
        else if (ty == 0x80u || ty == 0x90u) live_off(d1, midi_pi[d1], 0);
    }
}

/* ================================================================ the screen */
#define C_KEY RGB(215, 215, 215)
#define C_GREY RGB(70, 70, 70)
#define C_TXT RGB(170, 170, 170)
#define C_GOAL RGB(60, 230, 90)
#define C_FIT RGB(80, 150, 255)
#define KEY0 53u
static int black(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }

static void draw_keys(uint32_t lit, uint32_t goal, uint32_t fit, uint16_t col)   /* goal: a game points at them, green; fit: the band's chord */
{
    uint32_t k, w = 0;
    cv_begin(240, 56, C_BLACK);
    for (k = 0; k < 27u; k++)
        if (!black(KEY0 + k)) {
            int32_t x = 8 + (int32_t)w * 14;
            cv_rect(x, 0, 13, 56, goal >> k & 1u ? C_GOAL : lit >> k & 1u ? col : C_KEY);
            if (fit >> k & 1u) cv_rect(x, 50, 13, 6, C_FIT);                           /* it fits the band's chord */
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

static void draw_panel(const char *big, const char *small, uint16_t col)   /* two lines: a big one and a small one */
{
    cv_begin(240, 64, C_BLACK);
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
enum { CAPY_SING = 1, CAPY_BLINK = 2, CAPY_YUZU = 4, CAPY_UP = 8 };
static void capy_px(int cv, int32_t x, int32_t y, int32_t s, uint16_t c)
{
    if (cv) cv_rect(x, y, s, s, c);
    else lcd_fill((uint32_t)x, (uint32_t)y, (uint32_t)s, (uint32_t)s, c);
}
static void draw_capy(int cv, int32_t x, int32_t y, int32_t s, uint32_t f)   /* cv: into the canvas; s: pixel size */
{
    uint32_t r, c;
    for (r = 0; r < 20u; r++)
        for (c = 0; c < 24u; c++) {
            char ch = CAPY[r][c];
            uint16_t col;
            if (ch == '.') continue;
            if ((ch == 'o' || ch == 'g') && !(f & CAPY_YUZU)) continue;
            col = ch == 'b' ? RGB(176, 124, 72) : ch == 'd' ? RGB(104, 68, 40) : ch == 'n' ? RGB(60, 40, 30)
                : ch == 'o' ? RGB(255, 170, 30) : ch == 'g' ? RGB(70, 190, 70)
                : ch == 'e' ? (f & CAPY_BLINK ? RGB(104, 68, 40) : C_BLACK)
                : f & CAPY_SING ? RGB(60, 20, 20) : RGB(104, 68, 40);          /* m: the mouth, open when she sings */
            capy_px(cv, x + (int32_t)c * s, y + (int32_t)r * s - (f & CAPY_UP ? s : 0), s, col);
            if (ch == 'm' && f & CAPY_SING) capy_px(cv, x + (int32_t)c * s, y + (int32_t)(r + 1u) * s - (f & CAPY_UP ? s : 0), s, col);
        }
}
static void draw_top(const char *name, uint16_t col, uint32_t f)   /* the instrument or the game, and the capybara */
{
    cv_begin(240, 44, C_BLACK);
    cv_text((188 - text_w(&FONT_L, name)) / 2, 6, &FONT_L, name, col);
    draw_capy(1, 190, 2, 2, f);
    cv_blit(0, 0);
}

static void draw_knobs(void)                    /* the four knobs: their names, how far, lit when on */
{
    static const char *const NAME[4] = {"TONE", "LENGTH", "WOBBLE", "ECHO"};
    uint32_t val[4], on[4], i;
    val[0] = g_tone; val[1] = g_len; val[2] = g_wob; val[3] = g_echo;
    on[0] = 1; on[1] = g_len_on; on[2] = g_wob_on; on[3] = g_echo_on;
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
        p = sput(sput(p, STYLE[band - 1u].name), ": ");
        p = sput(sput(p, BCH[band_ch].name), "  NEXT ");
        sput(p, BCH[band_next].name);
    } else if (playing) sput(p, "YOUR RECORDING PLAYS");
    else if (L.len) sput(p, "PLAY: HEAR YOUR RECORDING");
    else {
        p = sput(p, "OCTAVE ");
        *p++ = oct < 0 ? '-' : '+';
        p = nput(p, (uint32_t)(oct < 0 ? -oct : oct));
        nput(sput(p, "   SPEED "), bpm);
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
    uint32_t was_held = 0;
    uint8_t held_note[27] = {0}, held_pi[27] = {0}, held[27] = {0};
    char line[40], big[40] = "", small[40] = "";

    if (settings_load(&set, sizeof set)) { set.quiet = 0; set.letters = 1; set.song = 0; for (k = 0; k < NGAME; k++) set.lvl[k] = 1; }
    g_quiet = set.quiet ? 1 : 0;
    letters = set.letters ? 1 : 0;
    if (slot_load(0, &L, sizeof L) || L.n > NEV || !L.len || !L.layers) L.len = 0, L.n = 0, L.layers = 0;
    go_home();
    tick_ms = fm1_ms;

    lcd_fill(0, 0, 240, 240, C_BLACK);          /* hello: the capybara, big, until a key or a second and a half */
    draw_capy(0, 48, 30, 6, CAPY_YUZU);
    draw_text_box(0, 170, 240, &FONT_L, "HELLO!", RGB(255, 170, 30), 1);
    draw_text_box(0, 212, 240, &FONT_S, "WREN " APP_VER, C_GREY, 1);
    while (fm1_ms - tick_ms < 1500u && !fm1_in.notes && !fm1_in.buttons) platform_poll();
    tick_ms = fm1_ms;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, goal = 0, glow = 0;
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
            }
            if (up >> k & 1u && held[k]) { held[k] = 0; live_off(held_note[k], held_pi[k], 1); }
        }
        if (place) game_run(keys);

        /* ---- the buttons: each does what is printed on it */
        b = fm1_input_edges(&rel);
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
        if (place && b & (1u << BTN_ARP | 1u << BTN_SEQ | 1u << BTN_REC | 1u << BTN_PLAY | 1u << BTN_SAVE | 1u << BTN_EDIT))
            say("HOME: BACK TO PLAYING");
        else {
            if (b & 1u << BTN_ARP) {
                arp_on = !arp_on;
                for (k = 0; k < 27u; k++)       /* the keys held now are the arp's from here */
                    if (held[k]) { held[k] = 0; live_off(held_note[k], held_pi[k], 1); }
                say(arp_on ? "ARP ON: HOLD SOME KEYS" : "ARP OFF");
            }
            if (b & 1u << BTN_SEQ) {            /* the band: each press the next one, then off */
                band = (uint8_t)((band + 1u) % (NSTYLE + 1u));
                band_quiet();
                band_origin = playing ? loop_t0 : tick;   /* its "one" is now, or the recording's */
                band_step = 0xFFFFFFFFu;
                if (band) { band_ch = STYLE[band - 1u].prog[0]; band_next = STYLE[band - 1u].prog[1]; }
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
                slot_save(0, &L, sizeof L);
                say(L.len ? "SAVED" : "SAVED (EMPTY)");
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
            name_drawn = panel_drawn = ~0u;
            was_held = 0;
            say("HOME");
        }

        /* ---- the knobs */
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {
            cur_patch = (uint8_t)(((int32_t)cur_patch + e % (int32_t)NPATCH + (int32_t)NPATCH) % (int32_t)NPATCH);
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
        } else if (e)
            say("SEL: THE GAMES");
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) { int32_t v = g_tone + e; g_tone = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v); }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) { int32_t v = g_len + e; g_len = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v); g_len_on = 1; }
        if ((e = fm1_enc_take(ENC_KNOB3)) != 0) { int32_t v = g_wob + e; g_wob = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v); g_wob_on = 1; }
        if ((e = fm1_enc_take(ENC_KNOB4)) != 0) { int32_t v = g_echo + e; g_echo = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v); g_echo_on = 1; }

        /* ---- the lights */
        {
            int32_t base = 53 + 12 * oct;
            for (k = 0; k < NV; k++)
                if (voice[k].gate && voice[k].note >= base && voice[k].note < base + 27
                    && (voice[k].src == S_LIVE || voice[k].src == S_LOOP || (voice[k].src == S_CUE && !cue_hide && voice[k].note == cue_on)))
                    lit |= 1u << (voice[k].note - base);
        }
        if (place) {
            goal = hint_mask;
            if (game == G_TUNES) goal = 1u << song_key(SONG[set.song % NSONG].notes[song_pos]);
            glow = 1u << 7 | 1u << 19;          /* C4, C5: to find your way */
        } else
            glow = band ? pc_keys(chord_pcs(band_ch), 0, 26) : 1u << 7 | 1u << 19;   /* with the band: the notes that fit */
        for (k = 0; k < 27u; k++) {
            uint32_t on = (lit | goal) >> k & 1u;
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
        hash = lit * 2654435761u ^ goal * 40503u ^ letters ^ (band && !place ? glow * 7919u : 0u);
        if (hash != keys_drawn) { keys_drawn = hash; draw_keys(lit, goal, band && !place ? glow : 0u, PATCH[cur_patch].col); }
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
