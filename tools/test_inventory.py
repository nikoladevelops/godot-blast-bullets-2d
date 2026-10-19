"""Test inventory: proves a test refactor (merging suites, moving helpers)
lost nothing. Records every GUT test name with the number of assert_/expect_
calls in its own body (helpers it calls are not counted), then compares two
records by name, ignoring which file a test lives in.

usage:
  python3 tools/test_inventory.py save <out.json>
  python3 tools/test_inventory.py diff <before.json> <after.json> [renames.json]
renames.json maps an old name ("test_x" or "test_x@<old file stem>") to its
new name. diff exits 1 when a test is missing or has fewer asserts."""
import json, re, sys, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent / 'test_project' / 'tests'
def inventory():
    inv = {}
    for f in sorted(ROOT.rglob('test_*.gd')):
        lines = f.read_text().split('\n')
        cur = None
        for line in lines:
            m = re.match(r'func (test_\w+)\(', line)
            if m:
                cur = m.group(1)
                if cur in inv:
                    cur = f"{cur}@{f.stem}"
                inv[cur] = {"file": str(f.relative_to(ROOT)), "asserts": 0}
                continue
            if re.match(r'(static )?func |class_name|const |var ', line):
                cur = None
                continue
            if cur:
                inv[cur]["asserts"] += len(re.findall(r'\b(assert_\w+|expect_\w+)\(', line))
    return inv
if len(sys.argv) < 3 or sys.argv[1] not in ('save', 'diff') or (sys.argv[1] == 'diff' and len(sys.argv) < 4):
    print(__doc__)
    sys.exit(2)
if sys.argv[1] == 'save':
    inv = inventory()
    json.dump(inv, open(sys.argv[2], 'w'), indent=0, sort_keys=True)
    print(len(inv), "tests,", sum(v['asserts'] for v in inv.values()), "assert/expect calls,", len({v['file'] for v in inv.values()}), "files")
else:
    a = json.load(open(sys.argv[2])); b = json.load(open(sys.argv[3]))
    renames = json.load(open(sys.argv[4])) if len(sys.argv) > 4 else {}
    def multiset(inv, ren):
        out = {}
        for key, v in inv.items():
            name = key.split('@')[0]
            name = ren.get(name + '@' + pathlib.Path(v['file']).stem, ren.get(name, name))
            out.setdefault(name, []).append(v['asserts'])
        return {k: sorted(v) for k, v in out.items()}
    ma = multiset(a, renames); mb = multiset(b, {})
    missing = [k for k in ma if len(mb.get(k, [])) < len(ma[k])]
    fewer = [(k, ma[k], mb[k]) for k in ma if k in mb and len(mb[k]) >= len(ma[k]) and sum(mb[k]) < sum(ma[k])]
    print("tests", len(a), "->", len(b), "| asserts", sum(v['asserts'] for v in a.values()), "->", sum(v['asserts'] for v in b.values()), "| files", len({v['file'] for v in a.values()}), "->", len({v['file'] for v in b.values()}))
    print("MISSING:", missing)
    print("FEWER ASSERTS:", fewer)
    sys.exit(1 if missing or fewer else 0)
