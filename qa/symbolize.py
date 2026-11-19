"""Map Gothic2.exe+0xRVA addresses in a test log to the nearest ZenGin function at or below them,
using the zCall(0x...) addresses in the Union SDK's G2A headers (image base 0x400000)."""
import bisect, glob, json, os, re, sys

API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ZenGin", "Gothic_II_Addon", "API")
funcs = []
cls = ""
for path in glob.glob(os.path.join(API, "*.h")):
    for line in open(path, encoding="latin1"):
        m = re.match(r"\s*(?:class|struct)\s+(\w+)", line)
        if m:
            cls = m.group(1)
        m = re.search(r"([~\w]+)\s*\(.*zCall\(\s*0x([0-9A-Fa-f]+)\s*\)", line)
        if m:
            funcs.append((int(m.group(2), 16), f"{cls}::{m.group(1)}"))
funcs.sort()
addrs = [a for a, _ in funcs]

def name(rva):
    va = 0x400000 + rva
    i = bisect.bisect_right(addrs, va) - 1
    if i < 0:
        return "?"
    a, n = funcs[i]
    return f"{n}+0x{va - a:X}" if va - a < 0x2000 else "?(far)"

for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    if '"main_thread_stuck"' not in line and '"exception"' not in line:
        continue
    e = json.loads(line)
    print(e["event"], e.get("forMs", ""), e.get("wall", ""))
    for s in e.get("stack", []):
        m = re.search(r"Gothic2\.exe\+0x([0-9A-F]+)", s, re.I)
        print("   ", s, "  ", name(int(m.group(1), 16)) if m else "")
