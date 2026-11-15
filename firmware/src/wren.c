/* SPDX-License-Identifier: GPL-3.0-only */
/* Wren: a first real instrument for a child of five to seven, on the M-VAVE FM-1.
 *
 * Three rules:
 *   1. Nothing plays unless she plays it. No demo, no backing band. The only things that repeat are what she
 *      recorded herself (REC / PLAY) and a plain drum beat she switches on (SEQ).
 *   2. Real instruments with their real names: piano, organ, bells, guitar, bass, strings, brass, flute.
 *   3. Every button does what is printed on it, always. No pages, no modes, no menus. HOME always goes home.
 *
 *   keys        play (10 voices, two-operator FM)
 *   PRESETS     the instrument
 *   OCT- OCT+   the octave
 *   KNOB 1      TONE: dark .. bright
 *   KNOB 2      LENGTH: how long notes ring          ENV switches it on / off
 *   KNOB 3      WOBBLE: vibrato                      LFO switches it on / off
 *   KNOB 4      ECHO                                 FX  switches it on / off
 *   ARP         held keys play one after the other
 *   SEQ         a drum beat to play along with       ALGORITHM: the SPEED of the beat, the arp and the recording
 *   REC         record what you play; REC again: it goes round. REC while it plays: add a layer
 *   PLAY        the recording on / off
 *   EDIT        tap: take the last layer away. Held a second: erase the recording
 *   SAVE        keep the recording for next time
 *   SEL         follow the lights: the next key of a tune is lit and waits for you   SELECT: which tune
 *   GLO         the C keys glow, to find your way
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
#define NV 10u                                  /* voices */
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
            at = ((env >> 8) * (int32_t)p->gain) >> 15;
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

static void play(uint32_t note, uint32_t pi, uint32_t src)
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

/* ================================================================ the clock: 480 ticks a beat */
#define PPQ 480u
static uint32_t tick, tick_acc, tick_ms, bpm = 100;
static uint8_t cur_patch, beat_on, arp_on, song_on, glo_on = 1;
static int8_t oct;
static uint32_t beat_origin, beat_step = 0xFFFFFFFFu;

/* ---- the recording: note events in ticks, sorted; layers on top of each other */
#define NEV 1500u
typedef struct { uint32_t t; uint8_t note, pi, on, layer; } ev_t;
static struct { uint32_t len; uint16_t n; uint8_t layers, pad; ev_t ev[NEV]; } L;   /* (saved as it is: slot 0) */
enum { R_IDLE, R_ARMED, R_FIRST, R_DUB };
static uint8_t rec, playing;
static uint32_t loop_t0, loop_ptr, rec_t0;
static uint16_t open_n, open_note[16];          /* notes recorded on and not yet off: note | pi << 8 */

static char msg[32];
static uint32_t msg_until;
static void say(const char *s)
{
    uint32_t i = 0;
    while (s[i] && i < 30u) { msg[i] = s[i]; i++; }
    msg[i] = 0;
    msg_until = fm1_ms + 1600u;
}
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
        if (beat_on) {                          /* with the beat on: a whole number of beats */
            len = (len + 240) / 480 * 480;
            if (len < 480) len = 480;
        }
        L.len = (uint32_t)len;
        for (i = 0; i < L.n; i++)
            if (L.ev[i].t >= L.len) L.ev[i].t = L.len - 1u;
        for (i = 0; i < open_n; i++) ev_insert(L.n, L.len - 1u, open_note[i] & 255u, open_note[i] >> 8, 0);
        if (!L.n) { L.len = 0; L.layers = 0; rec = R_IDLE; open_n = 0; return; }
        for (i = 0; i < open_n; i++) stop(open_note[i] & 255u, open_note[i] >> 8, 0);   /* (the recording takes over) */
        loop_start(beat_on ? rec_t0 + L.len : tick);
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
        rec_t0 = beat_on ? beat_origin + (tick - beat_origin + 240u) / 480u * 480u : tick;   /* the nearest beat */
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
    stop_src(1);
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
            if (e->on) play(e->note, e->pi, 1);
            else stop(e->note, e->pi, 1);
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
    stop_src(1);
    pos = (int32_t)(tick - loop_t0);
    for (loop_ptr = 0; loop_ptr < L.n && (int32_t)L.ev[loop_ptr].t <= pos; loop_ptr++) {}
}

/* ---- live notes: the keys, the arp and MIDI in all come through here */
static void live_on(uint32_t note, uint32_t pi, int send)
{
    play(note, pi, 0);
    if (send) midi_send(0x90u, note, 100);
    rec_note(note, pi, 1);
}
static void live_off(uint32_t note, uint32_t pi, int send)
{
    stop(note, pi, 0);
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

/* ---- the beat: kick on 1 and 3 (and a push), snare on 2 and 4, a hat on every eighth */
static void beat_run(void)
{
    uint32_t st = (tick - beat_origin) / (PPQ / 4u), s;
    if (!beat_on || st == beat_step) return;
    beat_step = st;
    s = st & 15u;
    if (s == 0u || s == 8u || s == 10u) k_trig++;
    if (s == 4u || s == 12u) s_trig++;
    if (!(s & 1u)) h_trig++;
}

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
    beat_run();
    arp_run();
    loop_run();
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

/* ================================================================ follow the lights */
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
static uint32_t song_i, song_pos, party_until;
static uint32_t song_key(char c)                /* the key (0 = F3) of a song letter */
{
    static const uint8_t LOW[7] = {16, 18, 7, 9, 11, 12, 14};   /* a b c d e f g */
    static const uint8_t HIGH[7] = {0, 0, 19, 21, 23, 24, 26};  /* C D E F G */
    if (c >= 'a' && c <= 'g') return LOW[c - 'a'];
    if (c >= 'C' && c <= 'G') return HIGH[c - 'A'];
    return c == 'x' ? 2u : c == 'y' ? 4u : 6u;
}
static uint32_t song_len(void) { uint32_t n = 0; while (SONG[song_i].notes[n]) n++; return n; }
static uint32_t song_target(void) { return song_on ? song_key(SONG[song_i].notes[song_pos]) : 255u; }

/* ================================================================ the screen */
#define C_KEY RGB(215, 215, 215)
#define C_GREY RGB(70, 70, 70)
#define C_TXT RGB(170, 170, 170)
#define C_GOAL RGB(60, 230, 90)
#define KEY0 53u
static int black(uint32_t note) { return (0x54Au >> (note % 12u)) & 1u; }

static void draw_keys(uint32_t lit, uint32_t goal, uint16_t col)
{
    uint32_t k, w = 0;
    cv_begin(240, 56, C_BLACK);
    for (k = 0; k < 27u; k++)
        if (!black(KEY0 + k)) {
            cv_rect(8 + (int32_t)w * 14, 0, 13, 56, k == goal ? C_GOAL : lit >> k & 1u ? col : C_KEY);
            w++;
        }
    for (k = 0, w = 0; k < 27u; k++)
        if (black(KEY0 + k))
            cv_rect(8 + (int32_t)w * 14 - 5, 0, 9, 34, k == goal ? C_GOAL : lit >> k & 1u ? col : C_BLACK);
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
    if (song_on) {
        p = sput(p, SONG[song_i].name);
        p = sput(p, "  ");
        p = nput(p, song_pos);
        p = sput(p, "/");
        nput(p, song_len());
    } else if (rec == R_ARMED) sput(p, "REC: PLAY SOMETHING");
    else if (rec == R_FIRST) sput(p, "RECORDING");
    else if (rec == R_DUB) { p = sput(p, "RECORDING LAYER "); nput(p, L.layers); }
    else if (playing) sput(p, "YOUR RECORDING PLAYS");
    else if (L.len) sput(p, "PLAY: HEAR YOUR RECORDING");
    else {
        p = sput(p, "OCTAVE ");
        *p++ = oct < 0 ? '-' : '+';
        p = nput(p, (uint32_t)(oct < 0 ? -oct : oct));
        p = sput(p, "   SPEED ");
        nput(p, bpm);
    }
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
    cur_patch = 0; oct = 0; bpm = 100;
    g_tone = 8; g_len = 13; g_wob = 8; g_echo = 9;
    g_len_on = g_wob_on = g_echo_on = 0;
    beat_on = arp_on = song_on = 0;
    glo_on = 1;
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
    uint32_t scope_t = 0, scope_idle = 0, home_t = 0, edit_t = 0, home_done = 0, edit_done = 0, set_dirty = 0;
    uint8_t held_note[27] = {0}, held_pi[27] = {0}, held[27] = {0};
    char line[40];
    struct { uint8_t quiet; } set = {0};

    settings_load(&set, sizeof set);
    g_quiet = set.quiet ? 1 : 0;
    if (slot_load(0, &L, sizeof L) || L.n > NEV || !L.len || !L.layers) L.len = 0, L.n = 0, L.layers = 0;
    go_home();
    tick_ms = fm1_ms;

    lcd_fill(0, 0, 240, 240, C_BLACK);
    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, goal;
        int32_t e;
        platform_poll();
        clock_run();

        /* ---- the keys */
        keys = fm1_in.notes & 0x7FFFFFFu;
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        goal = song_target();
        for (k = 0; k < 27u; k++) {
            if (down >> k & 1u) {
                if (!arp_on) {                  /* (with the arp on, arp_run plays the held keys) */
                    held[k] = 1;
                    held_note[k] = (uint8_t)(53 + 12 * oct + (int32_t)k);
                    held_pi[k] = cur_patch;
                    live_on(held_note[k], held_pi[k], 1);
                }
                if (k == goal) {                /* the lit key: on to the next one */
                    if (++song_pos >= song_len()) {
                        song_pos = 0;
                        say("WELL DONE!");
                        party_until = fm1_ms + 1600u;
                    }
                }
            }
            if (up >> k & 1u && held[k]) { held[k] = 0; live_off(held_note[k], held_pi[k], 1); }
        }

        /* ---- the buttons: each does what is printed on it */
        b = fm1_input_edges(&rel);
        if (b & 1u << BTN_OCT_DOWN && oct > -2) oct--;
        if (b & 1u << BTN_OCT_UP && oct < 2) oct++;
        if (b & 1u << BTN_FX) { g_echo_on = !g_echo_on; say(g_echo_on ? "ECHO ON" : "ECHO OFF"); }
        if (b & 1u << BTN_LFO) { g_wob_on = !g_wob_on; say(g_wob_on ? "WOBBLE ON" : "WOBBLE OFF"); }
        if (b & 1u << BTN_ENV) { g_len_on = !g_len_on; say(g_len_on ? "LENGTH ON" : "LENGTH OFF"); }
        if (b & 1u << BTN_GLO) { glo_on = !glo_on; say(glo_on ? "C KEYS GLOW" : "GLOW OFF"); }
        if (b & 1u << BTN_ARP) {
            arp_on = !arp_on;
            for (k = 0; k < 27u; k++)           /* the keys held now are the arp's from here */
                if (held[k]) { held[k] = 0; live_off(held_note[k], held_pi[k], 1); }
            say(arp_on ? "ARP ON: HOLD SOME KEYS" : "ARP OFF");
        }
        if (b & 1u << BTN_SEQ) {
            beat_on = !beat_on;
            beat_origin = playing ? loop_t0 : tick;   /* its "one" is now, or the recording's */
            beat_step = 0xFFFFFFFFu;
            say(beat_on ? "BEAT ON" : "BEAT OFF");
        }
        if (b & 1u << BTN_SEL) {
            song_on = !song_on;
            song_pos = 0;
            say(song_on ? "FOLLOW THE GREEN KEY" : "TUNE OFF");
        }
        if (b & 1u << BTN_REC) {
            if (rec == R_ARMED) { rec = R_IDLE; say("REC OFF"); }
            else if (rec != R_IDLE) rec_close();
            else if (!L.len) rec = R_ARMED;
            else if (L.layers >= 250u) say("FULL");
            else {                              /* a layer on top */
                if (!playing) loop_start(tick);
                L.layers++;
                open_n = 0;
                rec = R_DUB;
            }
        }
        if (b & 1u << BTN_PLAY) {
            if (rec == R_FIRST) rec_close();
            else if (playing) loop_stop();
            else if (L.len) { rec = R_IDLE; loop_start(tick); if (beat_on) beat_origin = tick, beat_step = 0xFFFFFFFFu; }
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
            draw_text_box(0, 112, 240, &FONT_S, "SAVING...", C_WHITE, 1);
            wait_quiet();
            slot_save(0, &L, sizeof L);
            say(L.len ? "SAVED" : "SAVED (EMPTY)");
            status_drawn = ~0u;
            prev = fm1_in.notes & 0x7FFFFFFu;
            tick_ms = fm1_ms;
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
            say("HOME");
        }

        /* ---- the knobs */
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0)
            cur_patch = (uint8_t)(((int32_t)cur_patch + e % (int32_t)NPATCH + (int32_t)NPATCH) % (int32_t)NPATCH);
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {
            int32_t v = (int32_t)bpm + 4 * e;
            bpm = (uint32_t)(v < 60 ? 60 : v > 180 ? 180 : v);
            echo_time();
            line[0] = 0;
            nput(sput(line, "SPEED "), bpm);
            say(line);
        }
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) {
            song_i = (uint32_t)(((int32_t)song_i + e % (int32_t)NSONG + (int32_t)NSONG) % (int32_t)NSONG);
            song_on = 1;
            song_pos = 0;
        }
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0) { int32_t v = g_tone + e; g_tone = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v); }
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) { int32_t v = g_len + e; g_len = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v); g_len_on = 1; }
        if ((e = fm1_enc_take(ENC_KNOB3)) != 0) { int32_t v = g_wob + e; g_wob = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v); g_wob_on = 1; }
        if ((e = fm1_enc_take(ENC_KNOB4)) != 0) { int32_t v = g_echo + e; g_echo = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v); g_echo_on = 1; }

        /* ---- the lights */
        goal = song_target();
        {
            int32_t base = 53 + 12 * oct;
            for (k = 0; k < NV; k++)
                if (voice[k].gate && voice[k].note >= base && voice[k].note < base + 27)
                    lit |= 1u << (voice[k].note - base);
        }
        if (song_on) lit = keys;
        for (k = 0; k < 27u; k++) {
            uint32_t on = lit >> k & 1u || k == goal;
            if ((int32_t)(party_until - fm1_ms) > 0) on = k == (fm1_ms >> 5) % 27u;   /* a light runs along the keys */
            fm1_led_key(KEY_LED(k), (int)on);
            fm1_led_key_glow(KEY_LED(k), glo_on && !on && (k == 7u || k == 19u));     /* C4, C5 */
        }
        fm1_led_key(BTN_OCT_DOWN, oct < 0);
        fm1_led_key(BTN_OCT_UP, oct > 0);
        fm1_led_key(BTN_FX, g_echo_on);
        fm1_led_key(BTN_LFO, g_wob_on);
        fm1_led_key(BTN_ENV, g_len_on);
        fm1_led_key(BTN_GLO, glo_on);
        fm1_led_key(BTN_ARP, arp_on);
        fm1_led_key(BTN_SEL, song_on);
        fm1_led_key(BTN_SEQ, beat_on && (tick - beat_origin) % PPQ < 120u);           /* blinks on the beat */
        fm1_led_key(BTN_REC, rec == R_ARMED ? (int)(fm1_ms >> 8 & 1u) : rec != R_IDLE);
        fm1_led_key(BTN_PLAY, playing);
        fm1_led_key(BTN_SAVE, 0);
        fm1_led_key(BTN_HOME, 0);
        fm1_led_key(BTN_EDIT, 0);

        /* ---- the screen: only what changed */
        idle_ok = 1;
        if (cur_patch != name_drawn) {
            name_drawn = cur_patch;
            draw_text_box(0, 6, 240, &FONT_L, PATCH[cur_patch].name, PATCH[cur_patch].col, 1);
            knobs_drawn = keys_drawn = ~0u;
            scope_idle = 0;
        }
        if (fm1_ms - scope_t > 60u) {           /* the scope: while something sounds, and once more after */
            int busy = !quiet() || beat_on;
            scope_t = fm1_ms;
            if (busy || scope_idle < 8u) draw_scope(PATCH[cur_patch].col);
            scope_idle = busy ? 0 : scope_idle + 1u;
        }
        status_text(line);
        for (hash = 5381u, k = 0; line[k]; k++) hash = hash * 33u ^ (uint8_t)line[k];
        if (hash != status_drawn) {
            status_drawn = hash;
            draw_text_box(0, 112, 240, &FONT_S, line, (int32_t)(msg_until - fm1_ms) > 0 ? C_WHITE : C_TXT, 1);
        }
        hash = lit ^ goal << 27;
        if (hash != keys_drawn) { keys_drawn = hash; draw_keys(lit, goal, PATCH[cur_patch].col); }
        hash = g_tone | (uint32_t)g_len << 5 | (uint32_t)g_wob << 10 | (uint32_t)g_echo << 15
             | (uint32_t)g_len_on << 20 | (uint32_t)g_wob_on << 21 | (uint32_t)g_echo_on << 22;
        if (hash != knobs_drawn) { knobs_drawn = hash; draw_knobs(); }
        idle_ok = 0;

        if (set_dirty && quiet() && !playing && !beat_on) {   /* the QUIET setting: saved when nothing sounds */
            settings_save(&set, sizeof set);
            set_dirty = 0;
            tick_ms = fm1_ms;
        }
    }
}
