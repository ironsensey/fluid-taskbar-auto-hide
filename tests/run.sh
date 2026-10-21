#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/build_single_file.py --check
# Windhawk accepts standalone source files. Ensure embedded testable functions
# remain exactly the same as the functions exercised by the portable tests.
python3 - <<'PY'
from pathlib import Path
for helper, mod in [('animation-math.inc', 'fluid-taskbar-auto-hide.wh.cpp'),
                    ('visibility-math.inc', 'fluid-taskbar-auto-hide.wh.cpp')]:
    assert Path(helper).read_text() in Path(mod).read_text(), f'{helper} is out of sync'
import re
source = Path('fluid-taskbar-auto-hide.wh.cpp').read_text()
for entry in ['BOOL Wh_ModInit()', 'void Wh_ModAfterInit()',
              'void Wh_ModUninit()', 'BOOL Wh_ModSettingsChanged(BOOL* reload)']:
    assert source.count(entry) == 1, f'Duplicate/missing lifecycle entry: {entry}'
settings = source.split('// ==WindhawkModSettings==')[1].split('// ==/WindhawkModSettings==')[0]
keys = re.findall(r'^- (\w+):', settings, re.M)
assert len(keys) == len(set(keys)) == 18
assert keys.count('oldTaskbarOnWin11') == 1
assert source.count('HMODULE WINAPI LoadLibraryExW_Hook(') == 1
assert not re.search(r'^#include ["]', source, re.M), 'Local include in standalone file'
print('Single-file lifecycle, settings, loader and embedded helpers checked')
PY
binary="$(mktemp /tmp/fluid-taskbar-tests.XXXXXX)"
trap 'rm -f "$binary"' EXIT
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined tests/logic.cpp -o "$binary"
"$binary"

python3 tests/check_lifecycle.py
