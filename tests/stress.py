# SPDX-License-Identifier: GPL-3.0-only
# A child at the keys, at random: a script for the host (tests/host.c) of keys, buttons (tapped and held), knobs and
# MIDI notes, the same for the same seed.   python3 tests/stress.py SEED MS > script.txt   (tests/run_bot.sh runs them)
import random,sys
seed=int(sys.argv[1]); dur=int(sys.argv[2]); r=random.Random(seed)
t=50; keys=0; held=set(); out=[]
mode=sys.argv[3] if len(sys.argv)>3 else r.choice(['mix','games','keys','knobs'])
while t<dur:
    t+=r.choice([5,20,60,120,250,400,900]) if r.random()<.97 else r.choice([3000,7000])
    if t>=dur: break
    if mode=='games': w=[.45,.2,.25,.05,.05]
    elif mode=='keys': w=[.75,.1,.1,.03,.02]
    elif mode=='knobs': w=[.3,.15,.5,.03,.02]
    else: w=[.5,.2,.2,.05,.05]
    c=r.choices(range(5),w)[0]
    if c==0:
        y=r.random()
        if y<.5: keys^=1<<r.randrange(27)
        elif y<.7: keys=0
        elif y<.85: keys=1<<r.choice([7,9,11,12,14,16,18,19])
        elif y<.95: keys=r.getrandbits(27)
        else: keys=0x7FFFFFF
        out.append(f"{t} keys {keys:#x}")
    elif c==1:
        b=r.choice([3,3,3]+list(range(14)))
        out.append(f"{t} btn {b}")
    elif c==2:
        e=r.choice([0,0,6,6]+list(range(7)))
        out.append(f"{t} enc {e} {r.choice([-7,-3,-2,-1,-1,1,1,2,3,7])}")
    elif c==3:
        b=r.randrange(14)
        if b in held: held.discard(b); out.append(f"{t} btnup {b}")
        else: held.add(b); out.append(f"{t} btndown {b}")
    else:
        n=r.randrange(128); out.append(f"{t} midi {r.choice([0x90,0x80,0x90])} {n} {r.choice([0,100,127])}")
out.append(f"{dur} end")
print("\n".join(out))
