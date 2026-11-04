#!/usr/bin/env python3
"""Checks the logs of tests/clockin.txt and tests/clockauto.txt (python3 tests/check_clock.py ext.log auto.log)."""
import re, sys

def notes(log):
    return [int(m.group(1)) for l in open(log) for m in [re.match(r"\s*(\d+) ms\s+MIDI out 90 [1-9]", l)] if m]
def gaps(ts): return [b - a for a, b in zip(ts, ts[1:])]
def on_grid(g, period, tol=3):          # a gap is a whole number of steps (a rest skips one or more)
    return all(abs(x - round(x / period) * period) <= tol and x > 0 for x in g)
def lines(log, pat): return [l for l in open(log) if re.search(pat, l)]
bad = 0
def check(ok, msg):
    global bad
    print(("ok   " if ok else "FAIL ") + msg)
    bad += 0 if ok else 1

ext, auto = sys.argv[1], sys.argv[2]
n = notes(ext)
early = [t for t in n if 1750 <= t < 2190]          # (the held keys sound once when they go down, at 1700)
check(not early, "EXT: nothing sounds before MIDI Start (PLAY alone does nothing without a clock)")
run = [t for t in n if 2200 <= t <= 5300]
g = gaps(run)
check(len(run) > 20 and on_grid(g[1:], 125.0), "EXT: the steps follow the clock, one every 125 ms (%d notes)" % len(run))
check(not [t for t in n if t > 5450], "EXT: MIDI Stop stops the sound")
check(not lines(ext, r"MIDI out F[8ACD]"), "EXT: no clock or transport is sent while it follows")
n = notes(auto)
before = [t for t in n if 1850 <= t < 3990]
mid = [t for t in n if 4300 <= t <= 5950]
after = [t for t in n if 6700 <= t <= 7500]
gb, gm, ga = gaps(before), gaps(mid), gaps(after)
check(len(before) > 5 and on_grid(gb[1:], 187.5), "AUTO: before a clock comes it runs on the internal clock (187.5 ms steps)")
check(len(mid) > 10 and on_grid(gm[2:], 125.0), "AUTO: with a clock coming in the steps follow it (125 ms steps)")
check(len(after) > 3 and on_grid(ga[1:], 125.0), "AUTO: when the clock has gone it carries on at the clock's tempo (125 ms steps)")
print("ALL OK" if not bad else "SOME FAILED")
sys.exit(1 if bad else 0)
