"""Adds sideways single Joy-Con support to the SDK's Switch input driver (local versus, one Joy-Con per player).

Inserts tools/joycon_sideways.inc after the stick reads in SwitchInputDriver::Poll and defines its cvar.
Idempotent; run by build_switch_p2.sh.

    python3 tools/patch_sdk_joycon.py <sdk dir>
"""
import sys
from pathlib import Path

driver = Path(sys.argv[1]) / 'src/input/switch/switch_input_driver.cpp'
s = driver.read_text()
if 'input_joycon_rotate_stick' in s:
    sys.exit(0)
anchor = '  HidAnalogStickState right = padGetStickPos(&slot.pad, 1);\n'
cvar_anchor = 'REXCVAR_DEFINE_BOOL(input_menu_shortcuts,'
assert anchor in s and cvar_anchor in s, 'SDK input driver changed: update tools/patch_sdk_joycon.py'
s = s.replace(anchor, anchor + (Path(__file__).parent / 'joycon_sideways.inc').read_text(), 1)
s = s.replace(cvar_anchor, 'REXCVAR_DEFINE_BOOL(input_joycon_rotate_stick, true, "Input",\n'
              '                    "Single Joy-Con held sideways: turn its stick to match (false if the system already does)");\n\n'
              + cvar_anchor, 1)
driver.write_text(s)
print('joycon: sideways single Joy-Con support added')
