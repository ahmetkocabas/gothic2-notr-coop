"""Checks the two GothicCoopTest.log files of a pair run. Prints PASS/FAIL per check.

Both copies run on one PC, so the "wall" field (epoch ms) is a shared clock: what one copy
shows for the other player is compared with where that player really was at that moment.
"""
import bisect, json, math, sys

TOLERANCE = 200      # game units (cm); a step is ~100
MAX_LAG_MS = 1500    # remote body may trail the real one by this much
GOOD_SHARE = 0.9     # share of samples that must match

def load(path):
    out = []
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.strip()
                if line.startswith("{"):
                    try:
                        out.append(json.loads(line))
                    except ValueError:
                        pass
    except FileNotFoundError:
        pass
    return out

server, client = load(sys.argv[1]), load(sys.argv[2])
results = []

def check(name, ok, detail=""):
    results.append(ok)
    print(("PASS " if ok else "FAIL ") + name + (f"  ({detail})" if detail else ""))

def events(log, ev):
    return [e for e in log if e.get("event") == ev]

def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))

def track(log):
    s = [e for e in events(log, "state") if "wall" in e]
    return [e["wall"] for e in s], [e["me"] for e in s]

def remote(log, prefix):
    out = []
    for e in events(log, "state"):
        for p in e.get("players", []):
            if p["name"].upper().startswith(prefix):
                out.append((e["wall"], p["pos"]))
    return out

def matches(owner, seen):
    """Share of 'seen' samples that are within TOLERANCE of the owner's real position
    at some moment in the last MAX_LAG_MS."""
    walls, poss = owner
    good = 0
    worst = 0
    for w, pos in seen:
        lo = max(0, bisect.bisect_left(walls, w - MAX_LAG_MS) - 1)  # owner may have logged nothing (stalled frame)
        hi = bisect.bisect_right(walls, w)
        cands = [dist(pos, poss[i]) for i in range(lo, hi)] or [float("inf")]
        best = min(cands)
        worst = max(worst, best)
        good += best < TOLERANCE
    return (good / len(seen) if seen else 0.0), worst

early = sys.argv[3].split(",") if len(sys.argv) > 3 and sys.argv[3] else []
check("server: ran until the end", "1" not in early)
check("client: ran until the end", "2" not in early)
for label, log in (("server", server), ("client", client)):
    for e in events(log, "exit_process"):
        print(f"INFO {label} exit_process code={e['code']} thread={e['thread']} main={e['mainThread']} stack={e['stack'][:8]}")
    for e in events(log, "exception")[:5]:
        print(f"INFO {label} exception {e['code']} at {e['at']} addr={e.get('address')} stack={e['stack'][:6]}")

for label, log in (("server", server), ("client", client)):
    check(f"{label}: world loaded", bool(events(log, "world_loaded")))
    check(f"{label}: session started", bool(events(log, "session_started")))

for label, log in (("server", server), ("client", client)):
    dead = [e["t"] for e in events(log, "state") if e.get("hp", 1) <= 0]
    check(f"{label}: hero never died", not dead, f"hp 0 from t={dead[0]}" if dead else "")

pings = [e["ping"] for e in events(client, "state") if e.get("ping", -1) > 0]
check("client: ping measured", bool(pings), f"median {sorted(pings)[len(pings)//2]} ms" if pings else "no ping")

host_seen = remote(client, "HOST")
friend_seen = remote(server, "FRIEND")

# While the client is far away (teleports at t >= 95000) the mod despawns the remote player
# bodies on purpose (> BROADCAST_DISTANCE), so follow checks skip that window.
far = [tp for tp in events(client, "teleport") if tp["t"] >= 95000]
far_window = (far[0]["wall"], far[-1]["wall"] + 5000) if far else None
def outside_far(samples):
    if not far_window:
        return samples
    return [(w, p) for w, p in samples if not (far_window[0] <= w <= far_window[1])]
check("client: sees HOST", bool(host_seen), f"{len(host_seen)} samples")
check("server: sees FRIEND", bool(friend_seen), f"{len(friend_seen)} samples")

share, worst = matches(track(server), outside_far(host_seen))
check("position: HOST on client follows host", share >= GOOD_SHARE, f"{share:.0%} within {TOLERANCE}, worst {worst:.0f}")
share, worst = matches(track(client), outside_far(friend_seen))
check("position: FRIEND on server follows client", share >= GOOD_SHARE, f"{share:.0%} within {TOLERANCE}, worst {worst:.0f}")

# Each host teleport must reach the client within 2 s.
for tp in events(server, "teleport"):
    later = [(w, p) for w, p in host_seen if tp["wall"] <= w <= tp["wall"] + 2000]
    hit = [w for w, p in later if dist(p, tp["me"]) < TOLERANCE]
    check(f"teleport at t={tp['t']}: seen on client", bool(hit),
          f"after {hit[0] - tp['wall']} ms" if hit else f"{len(later)} samples, none close")

for tp in [t for t in events(client, "teleport") if t["t"] < 95000]:
    later = [(w, p) for w, p in friend_seen if tp["wall"] <= w <= tp["wall"] + 2000]
    hit = [w for w, p in later if dist(p, tp["me"]) < TOLERANCE]
    check(f"client teleport at t={tp['t']}: seen on server", bool(hit),
          f"after {hit[0] - tp['wall']} ms" if hit else f"{len(later)} samples, none close")

if far:
    c_states = events(client, "state")
    before = [e["syncNpcs"] for e in c_states if far[0]["wall"] - 5000 <= e["wall"] < far[0]["wall"]]
    away = [e["syncNpcs"] for e in c_states if far[0]["wall"] + 3000 <= e["wall"] <= far[0]["wall"] + 12000]
    check("far teleport: client dropped synced NPCs", bool(away) and bool(before) and min(away) < max(before),
          f"before {max(before) if before else '-'}, while away {min(away) if away else '-'}")
    if len(far) > 1:
        back = [e["syncNpcs"] for e in c_states if e["wall"] >= far[1]["wall"] + 5000]
        check("far teleport: synced NPCs back after return", bool(back) and max(back) > 1,
              f"after return {max(back) if back else '-'}")

if host_seen:
    half = host_seen[len(host_seen) // 2][0]
    c_states = [e for e in events(client, "state") if e["wall"] >= half
                and not (far_window and far_window[0] <= e["wall"] <= far_window[1])]
    gone = [e["t"] for e in c_states if not any(p["name"].upper().startswith("HOST") for p in e.get("players", []))]
    check("client: HOST present for whole second half", not gone, f"missing at t={gone[:5]}" if gone else "")

print(f"\n{sum(results)}/{len(results)} checks passed")
sys.exit(0 if all(results) else 1)
