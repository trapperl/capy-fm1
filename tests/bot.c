/* SPDX-License-Identifier: GPL-3.0-only */
/* A player that knows the answers: it plays one game of Capy on the host (tests/host.c) and answers every question
 * right, the rhythms in time. tests/run_bot.sh runs it through all the games and checks that each reaches its top
 * level, that no question takes too long to come, and that TUNES gets through its tunes: a question that cannot be
 * answered, or a level that never ends, shows up here.
 *
 *   build/host/bot OUT SCRIPT     the script enters the games and picks one (tests/run_bot.sh writes it)
 *   -> a line "BOT <game> level L/MAX right N gap MS tunes T" at the end */
#include <stdlib.h>
static void *bot_buf;                           /* (the host keeps the sound it made, a block at a time: one buffer that doubles) */
static size_t bot_cap;
static void *bot_realloc(void *p, size_t n)
{
    (void)p;
    if (n > bot_cap) { bot_cap = n * 2u + (1u << 20); bot_buf = (realloc)(bot_buf, bot_cap); }
    return bot_buf;
}
#define realloc(p, n) bot_realloc(p, n)
static void bot(void);
#define CAPY_LOOP_HOOK bot
#include "../firmware/src/capy.c"

static uint32_t botq_t[64], botq_m[64], botq_n, botq_i, bot_n, bot_gap, bot_last, bot_tunes;
static void bot_add(uint32_t t, uint32_t m) { if (botq_n < 64u) { botq_t[botq_n] = t; botq_m[botq_n++] = m; } }
static void bot(void)
{
    static uint8_t phase, song = 255;
    uint32_t t = fm1_ms + 200u, i, j, k;
    if (!place) return;
    if (song == 255u) song = set.song;
    if (set.song != song) { song = set.song; bot_tunes++; }     /* a tune was played through, and back */
    if (g_phase == P_DONE && phase != P_DONE) {                  /* a right answer */
        bot_n++;
        if (fm1_ms - bot_last > bot_gap) bot_gap = fm1_ms - bot_last;
        bot_last = fm1_ms;
    }
    phase = g_phase;
    if (botq_i < botq_n) {                      /* the keys it has planned, as their times come */
        if ((int32_t)(fm1_ms - botq_t[botq_i]) >= 0) fm1_in.notes = botq_m[botq_i++];
        return;
    }
    if (g_phase != P_WAIT) return;
    botq_n = botq_i = 0;
    switch (game) {
    case G_TUNES: bot_add(t, 1u << song_key(SONG[set.song % NSONG].notes[song_pos])); bot_add(t + 80u, 0); break;
    case G_FIND: for (k = 7; (53u + k) % 12u != q_pc; k++) ; bot_add(t, 1u << k); bot_add(t + 80u, 0); break;
    case G_COPY: for (i = q_pos; i < q_n; i++) { bot_add(t + i * 200u, 1u << q_note[i]); bot_add(t + i * 200u + 100u, 0); } break;
    case G_HILO: host_edges |= 1u << (q_hi ? BTN_OCT_UP : BTN_OCT_DOWN); bot_add(t + 300u, 0); break;
    case G_CHORD: bot_add(t, pc_keys(q_mask, 7, 18)); bot_add(t + 300u, 0); break;
    case G_TAP: for (i = 0; i < 8u; i++) if (q_mask >> i & 1u) { bot_add(t + i * 300u, 1u << 7); bot_add(t + i * 300u + 80u, 0); } break;
    case G_ALONG: bot_add(fm1_ms, 1u << q_note[q_pos]); bot_add(fm1_ms + 60u, 0); break;
    case G_EAR: bot_add(t, 1u << q_pc); bot_add(t + 80u, 0); break;
    case G_DRUM: for (i = 0, j = 0; i < 8u; i++) if (q_mask >> i & 1u) { bot_add(t + i * 300u, 1u << (7u + q_note[j++])); bot_add(t + i * 300u + 80u, 0); } break;
    }
}
__attribute__((destructor)) static void bot_report(void)
{
    printf("BOT %s level %u/%u right %u gap %u tunes %u\n", GNAME[game], LVL(game), GMAX[game], bot_n, bot_gap, bot_tunes);
}
