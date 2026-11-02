"""Direct calls between recompiled functions (after Prototype 2 NX tools/prepare_direct_calls.py, GPL-3.0).

Every generated `sub_X` is a weak, noinline alias of `__imp__sub_X` so a hook can replace it; calling through it
keeps GCC from inlining small game functions. Calls to functions nothing hooks are rewritten to `__imp__sub_X`
(already declared by the generated headers). Protected (kept on the weak name): every 0x82xxxxxx address in the
app's hook sources and in the manifest (hooks, rexcrt routines, hand-registered functions). Idempotent.

    python3 tools/prepare_direct_calls.py generated/default
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
GEN = Path(sys.argv[1])
CALL = re.compile(r'(?<![\w])(sub_[0-9A-F]{8})\(ctx, base\);')
ADDR = re.compile(r'(?<![0-9A-Fa-f])(82[0-9A-Fa-f]{6})(?![0-9A-Fa-f])')

protected = set()
sources = [HERE / 'rb2_manifest.toml', *(HERE / 'src').glob('*.[ch]*'), *(HERE / '../src').glob('*.[ch]*')]
for p in sources:
    if p.is_file():
        protected.update('sub_' + a.upper() for a in ADDR.findall(p.read_text(errors='replace')))
assert 'sub_8249A410' in protected, 'hook sources not found: refusing to bypass hooks'

total = 0
for p in sorted(GEN.glob('rb2_recomp.*.cpp')):
    s = p.read_text(encoding='utf-8')
    out, _ = CALL.subn(lambda m: m[0] if m[1] in protected else '__imp__' + m[0], s)
    direct = out.count('__imp__sub_') - s.count('__imp__sub_')
    if direct:
        p.write_text(out, encoding='utf-8')
    total += direct
print(f'direct calls: {total} call sites rewritten, {len(protected)} protected functions')
