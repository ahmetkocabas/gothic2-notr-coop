"""Read a run_story.ps1 run: what each side saw during talks and while apart, and how far the
two copies' story state drifted. usage: analyze_story.py <host System dir> <client System dir>"""
import json, math, os, sys

def load_log(d):
    p = os.path.join(d, "GothicCoopTest.log")
    with open(p, encoding="utf-8", errors="replace") as f:
        return [json.loads(l) for l in f if l.startswith("{")]

def load_story(d):
    p = os.path.join(d, "GothicCoopStory.json")
    return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else None

host_dir, client_dir = sys.argv[1], sys.argv[2]
H, C = load_log(host_dir), load_log(client_dir)
ev = lambda log, name: [e for e in log if e.get("event") == name]
states = lambda log: ev(log, "state")

def window(log, w0, w1):
    return [e for e in states(log) if w0 <= e["wall"] <= w1]

def summary(label, log):
    s = ev(log, "session_started")
    print(f"{label}: world_loaded={bool(ev(log, 'world_loaded'))} session={'t=%ds' % (s[0]['t'] // 1000) if s else 'no'}"
          f" last t={states(log)[-1]['t'] // 1000 if states(log) else '-'}s"
          f" exceptions={len(ev(log, 'exception'))} stuck={len(ev(log, 'main_thread_stuck'))} slow={len(ev(log, 'slow_frame'))}")

print("== sessions")
summary("host", H)
summary("client", C)

def talk_report(who, log, other_log, other_name):
    starts = ev(log, "talk_start")
    if not starts:
        print(f"{who}: no talk")
        return
    st = starts[0]
    end = next((e for e in ev(log, "talk_end") if e["wall"] >= st["wall"]), None)
    w1 = end["wall"] if end else st["wall"] + 60000
    picks = [(c.get("pick"), len(c.get("offered", []))) for c in ev(log, "talk_choice") if st["wall"] <= c["wall"] <= w1]
    print(f"{who} talk to {st['npc']}: found={st['found']} picks={picks[:6]}{' ...' if len(picks) > 6 else ''}"
          f" end={'%d ms' % end['ms'] if end else 'none'} timedOut={end.get('timedOut') if end else '-'}")
    told_self = [t for e in ev(log, "story") if st["wall"] <= e["wall"] <= w1 + 6000 for t in e.get("newTold", [])]
    told_other = [t for e in ev(other_log, "story") if st["wall"] <= e["wall"] <= w1 + 6000 for t in e.get("newTold", [])]
    print(f"   newly told on {who}: {told_self}")
    print(f"   newly told on {other_name} in the same time: {told_other}")
    # what the other side shows for that NPC while the talk runs
    anis = {}
    for e in window(other_log, st["wall"], w1):
        for n in e.get("near", []):
            if n["n"] == st["npc"]:
                anis[n.get("ani", "")] = anis.get(n.get("ani", ""), 0) + 1
    print(f"   {other_name} sees {st['npc']} animations during the talk: {anis or 'not near'}")
    talking = sum(1 for e in window(log, st["wall"], w1) if e.get("talking"))
    print(f"   {who} in dialog camera for {talking} samples")

print("\n== talks")
talk_report("host", H, C, "client")
talk_report("client", C, H, "host")

def near_report(label, samples):
    if not samples:
        print(f"{label}: no samples")
        return
    counts = [len(e.get("near", [])) for e in samples]
    synced = [sum(1 for n in e.get("near", []) if n.get("synced")) for e in samples]
    moved, total, still_anis = 0, 0, {}
    first = {}
    for e in samples:
        for n in e.get("near", []):
            key = n["n"]
            if key in first:
                total += 1
                if math.dist(first[key], n["p"]) > 50:
                    moved += 1
            else:
                first[key] = n["p"]
            still_anis[n.get("ani", "")] = still_anis.get(n.get("ani", ""), 0) + 1
    top = sorted(still_anis.items(), key=lambda x: -x[1])[:6]
    print(f"{label}: {len(samples)} samples, NPCs near {min(counts)}-{max(counts)}, synced {min(synced)}-{max(synced)},"
          f" {len(first)} distinct, moved>0.5m in {moved}/{total} later samples; animations {top}")

print("\n== apart: client in the city, host at the tower")
c_tp = ev(C, "teleport")
h_tp = ev(H, "teleport")
if c_tp:
    w0 = c_tp[0]["wall"] + 3000
    w1 = h_tp[0]["wall"] if h_tp else w0 + 40000
    near_report("client sees around itself", window(C, w0, w1))
    hf = [e for e in window(H, w0, w1) if e.get("nearFriend")]
    if hf:
        counts = [len(v["npcs"]) for e in hf for v in e["nearFriend"].values()]
        at = [v["at"] for e in hf for v in e["nearFriend"].values()][-1]
        print(f"host world around the client's last known position {at}: NPCs present {min(counts)}-{max(counts)}")
    else:
        print("host: no nearFriend data")
    print("\n== together in the city")
    if h_tp:
        w2 = h_tp[0]["wall"] + 5000
        near_report("client sees around itself", window(C, w2, w2 + 60000))
        near_report("host sees around itself", window(H, w2, w2 + 60000))
        bc = [sum(1 for n in e.get("near", []) if n.get("broadcast")) for e in window(H, w2, w2 + 60000)]
        if bc:
            print(f"host broadcasts {min(bc)}-{max(bc)} of the NPCs around it")

print("\n== story state at the end (GothicCoopStory.json)")
hs, cs = load_story(host_dir), load_story(client_dir)
if hs and cs:
    diff = [(k, hs["ints"][k], cs["ints"].get(k)) for k in hs["ints"] if cs["ints"].get(k) != hs["ints"][k]]
    print(f"script ints: {len(hs['ints'])}, differing {len(diff)}: {diff[:25]}")
    ht, ct = set(hs["told"]), set(cs["told"])
    print(f"told infos: host {len(ht)}, client {len(ct)}; only host {sorted(ht - ct)}; only client {sorted(ct - ht)}")
    print(f"log topics: host {[t[0] for t in hs['log']]}, client {[t[0] for t in cs['log']]}")
    print(f"xp: host {hs['xp']}, client {cs['xp']}")
else:
    print("missing story files")
