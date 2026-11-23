"""For a human-played session: every stretch where a player was in a dialog, who the partner
probably was (nearest NPC), whether that NPC was host-driven on the client, how far apart
the players were, and what the talk changed on each side.
usage: analyze_manual_talks.py <host log> <client log> [from_wall_ms]"""
import json, math, sys

def load(p, since):
    with open(p, encoding="utf-8", errors="replace") as f:
        return [e for e in (json.loads(l) for l in f if l.startswith("{")) if e.get("wall", 0) >= since]

since = int(sys.argv[3]) if len(sys.argv) > 3 else 0
H, C = load(sys.argv[1], since), load(sys.argv[2], since)

def stretches(log):
    out, cur = [], None
    for e in log:
        if e.get("event") != "state":
            continue
        if e.get("talking"):
            if not cur:
                cur = [e, e]
            cur[1] = e
        elif cur:
            out.append(cur)
            cur = None
    if cur:
        out.append(cur)
    return out

def story_between(log, w0, w1):
    told, log_d, changed = [], 0, []
    for e in log:
        if e.get("event") == "story" and w0 <= e["wall"] <= w1:
            told += e.get("newTold", [])
            log_d += e.get("entriesDelta", 0) or 0
            changed += [c[0] for c in e.get("changed", [])]
    return told, log_d, changed

for who, log, other in (("client", C, H), ("host", H, C)):
    print(f"== {who} dialogs")
    for a, b in stretches(log):
        near = a.get("near", [])
        partner = near[0] if near else None
        players = a.get("players", [])
        dist = math.dist(a["me"], players[0]["pos"]) / 100 if players else None
        told, log_d, changed = story_between(log, a["wall"] - 1000, b["wall"] + 6000)
        o_told, o_log, _ = story_between(other, a["wall"] - 1000, b["wall"] + 6000)
        print(f"t={a['t'] // 1000}s..{b['t'] // 1000}s ({(b['wall'] - a['wall']) // 1000 + 1}s)"
              f" partner={partner['n'] if partner else '?'} synced={partner.get('synced') if partner else '?'}"
              f" ani={partner.get('ani') if partner else '?'}"
              f" other player {('%.0f m' % dist) if dist is not None else 'not seen'} away")
        print(f"     {who}: new told {told}, log entries +{log_d}, globals {changed[:8]}")
        print(f"     other side: new told {o_told}, log entries +{o_log}")
