"""Compare a manual (human-played) session from two GothicCoopTest.log files.

usage: python -I compare_manual.py <host log> <friend log> [clock_offset_ms]
Both copies on one PC share a clock; for two PCs pass the friend's clock minus the host's
(unknown -> 0, then the lag figures are off by that much).
"""
import bisect, json, math, sys

def load(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return [json.loads(l) for l in f if l.startswith("{")]

host, friend = load(sys.argv[1]), load(sys.argv[2])
offset = int(sys.argv[3]) if len(sys.argv) > 3 else 0
for e in friend:
    if "wall" in e:
        e["wall"] -= offset

def states(log):
    return [e for e in log if e.get("event") == "state"]

def track(log):
    s = states(log)
    return [e["wall"] for e in s], [e["me"] for e in s]

def seen(log, prefix):
    return [(e["wall"], p["pos"], e["t"]) for e in states(log)
            for p in e.get("players", []) if p["name"].upper().startswith(prefix)]

def compare(label, owner, samples):
    walls, poss = owner
    if not samples:
        print(f"{label}: no samples (never saw the other player)")
        return
    rows = []
    for w, pos, t in samples:
        lo = max(0, bisect.bisect_left(walls, w - 1500) - 1)
        hi = bisect.bisect_right(walls, w)
        best = min([math.dist(pos, poss[i]) for i in range(lo, hi)] or [float("inf")])
        rows.append((best, t))
    ds = sorted(d for d, _ in rows)
    n = len(ds)
    print(f"{label}: {n} samples, median {ds[n // 2]:.0f}, "
          f"{sum(d < 200 for d in ds) / n:.0%} within 2 m, {sum(d < 500 for d in ds) / n:.0%} within 5 m")
    worst = sorted(rows, reverse=True)[:5]
    print("   largest gaps (units, at t s):", ", ".join(f"{d:.0f}@{t / 1000:.0f}" for d, t in worst))

for name, log in (("host", host), ("friend", friend)):
    ev = [e for e in log if e.get("event") in ("world_loaded", "session_started")]
    print(name, "->", ", ".join(f"{e['event']}{'(' + e.get('by', '') + ')' if e.get('by') else ''} t={e['t'] / 1000:.0f}s" for e in ev) or "no events")
    dead = [e["t"] for e in states(log) if e.get("hp", 1) <= 0]
    if dead:
        print(f"   {name} hp 0 at t={dead[0] / 1000:.0f}s")
    for e in log:
        if e.get("event") in ("exit_process", "main_thread_stuck"):
            print(f"   {name} {e['event']}")

pings = [e["ping"] for e in states(friend) if e.get("ping", -1) > 0]
if pings:
    print(f"friend ping: median {sorted(pings)[len(pings) // 2]} ms, max {max(pings)} ms")
compare("friend sees HOST vs host's real position", track(host), seen(friend, "HOST"))
compare("host sees FRIEND vs friend's real position", track(friend), seen(host, "FRIEND"))
