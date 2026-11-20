#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Every game of Capy, played by tests/bot.c for twenty minutes each (seconds, here): each must reach its top level
# with no question more than 15 s after the last, and TUNES must get through all its tunes. Then eight sessions of
# random keys, buttons and knobs (tests/stress.py), three minutes each: none may crash. Both run with the compiler's
# checks for memory and arithmetic errors on.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host/bot
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
D=; [ "${THEME:-capy}" = dino ] && D=-DDINO=1    # (THEME=dino: the dinosaurs)
cc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"bot"' $D -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o build/host/bot/bot tests/bot.c
bad=0
for g in 0 1 2 3 4 5 6 7 8; do
    s=build/host/bot/g$g.txt
    printf '100 keys 0x80\n150 keys 0\n300 btn 3\n' > $s
    [ $g -gt 0 ] && echo "600 enc 0 $g" >> $s
    echo "1200000 end" >> $s
    if ! out=$(build/host/bot/bot "$tmp/out" $s 2>&1); then echo "game $g: the run failed"; echo "$out" | tail -20; bad=1; continue; fi
    line=$(echo "$out" | grep '^BOT ' | tail -1)
    echo "$line"
    echo "$line" | awk -v g=$g '{ split($(NF-6), l, "/"); gap = $(NF-2); tunes = $NF
        if (g == 0) { if (tunes < 20) { print "  FAIL: TUNES played " tunes " tunes, not all twenty"; exit 1 } }
        else if (l[1] != l[2]) { print "  FAIL: stopped at level " l[1]; exit 1 }
        else if (gap > 15000) { print "  FAIL: " gap " ms without a right answer"; exit 1 } }' || bad=1
done
for seed in 1 2 3 4 5 6 7 8; do
    python3 tests/stress.py $seed 180000 > build/host/bot/stress$seed.txt
    if ! out=$(build/host/bot/bot "$tmp/out" build/host/bot/stress$seed.txt 2>&1); then echo "stress $seed: FAIL"; echo "$out" | tail -20; bad=1; fi
done
[ $bad = 0 ] && echo "stress: eight random sessions, no crash"
exit $bad
