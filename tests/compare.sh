#!/bin/sh
# Corrects a grid with the installed plug-in and with the installed
# lensfun:correct filter, inside the Flatpak GIMP without a window, and
# checks that both give the same result and that the result is corrected.
# Install first (README), and close GIMP.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out="$here/output"
mkdir -p "$out"
python3 "$here/make-grid.py" "$out/grid.png"
flatpak run --filesystem="$here" --env=LF_TEST_OUT="$out" \
  --command=gimp-console-3.2 org.gimp.GIMP --no-interface --no-data \
  --batch-interpreter python-fu-eval \
  -b "exec(open('$here/compare.py').read())" --quit 2>&1 | grep -E "plug-in:|plug-in as filter|filters on|Error"
python3 - "$out" <<'PY'
import sys
import numpy as np
from PIL import Image
out = sys.argv[1]
L = lambda n: np.asarray(Image.open(out + '/' + n).convert('RGB')).astype(int)
grid, plugin, filt = L('grid.png'), L('plugin.png'), L('filter.png')
print('changed by the correction: %.1f %% of pixels' % (100 * (np.abs(plugin - grid).max(2) > 10).mean()))
d = np.abs(plugin - filt).max(2)
print('plug-in vs filter: max difference %d, pixels differing by more than 2: %d' % (d.max(), (d > 2).sum()))
d = np.abs(L('plugin-filter.png') - filt).max(2)
print('filter added by the plug-in vs filter: max difference %d' % d.max())
PY
