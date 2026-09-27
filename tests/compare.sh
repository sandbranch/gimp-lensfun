#!/bin/sh
# Corrects a grid with the installed plug-in and with the installed
# lensfun:correct filter, inside the Flatpak GIMP without a window, and
# checks that both give the same result and that the result is corrected.
# Install first (README).
# GIMP runs with a throwaway profile and isolated from your folders
# (tests/isolate.sh): the installed plug-in is copied from your plug-in
# folder into the profile (only it: other plug-ins of yours can change
# their own folders when GIMP starts them), the installed filter is read
# from your GEGL folder of the Flatpak, nothing is written there or
# anywhere else of yours, and a GIMP of yours does not matter.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$(dirname "$here")
out="$here/output"
rm -rf "$out/compare-profile"
mkdir -p "$out/compare-profile"
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$here/output/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
# a copy of the installed plug-in, and the installed GEGL operations
# (read only)
installed=${XDG_CONFIG_HOME:-$HOME/.config}/GIMP/3.2/plug-ins/gimp-lensfun
[ -x "$installed/gimp-lensfun" ] || { echo "no installed plug-in in $installed (README)" >&2; exit 1; }
gegl_path="$HOME/.var/app/org.gimp.GIMP/data/gegl-0.4/plug-ins:/app/lib/gegl-0.4"
rm -rf "$out/compare-profile/plug-ins"
mkdir -p "$out/compare-profile/plug-ins"
cp -R "$installed" "$out/compare-profile/plug-ins/"
python3 "$here/make-grid.py" "$out/grid.png"
gimp_run --flatpak --filesystem="$here" --env=LF_TEST_OUT="$out" \
  --env=GIMP3_DIRECTORY="$out/compare-profile" --env=GEGL_PATH="$gegl_path" \
  -- gimp-console-3.2 --no-interface --no-data \
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
