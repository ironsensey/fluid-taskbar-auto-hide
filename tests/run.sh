#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Windhawk accepts standalone source files. Ensure embedded testable functions
# remain exactly the same as the functions exercised by the portable tests.
python3 - <<'PY'
from pathlib import Path
for helper, mod in [('animation-math.inc', 'fluid-taskbar-animation.wh.cpp'),
                    ('visibility-math.inc', 'fluid-taskbar-visibility.wh.cpp')]:
    assert Path(helper).read_text() in Path(mod).read_text(), f'{helper} is out of sync'
print('Embedded helpers match tested source')
PY
binary="$(mktemp /tmp/fluid-taskbar-tests.XXXXXX)"
trap 'rm -f "$binary"' EXIT
g++ -std=c++20 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined tests/logic.cpp -o "$binary"
"$binary"
