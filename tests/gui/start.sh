#!/bin/sh
# Opens the plug-in's dialog on the test grid in the Flatpak GIMP on a
# Broadway display (http://127.0.0.1:8085/), to look at it with
# gimp-plugin-devtools/gui/cdp.mjs. Run tests/compare.sh first (it makes
# the grid). Broadway stops when GIMP quits.
# GIMP runs with a throwaway profile and isolated from your folders
# (tests/isolate.sh): the installed plug-in is copied from your plug-in
# folder into the profile (only it: other plug-ins of yours can change
# their own folders when GIMP starts them), the installed filter is read
# from your GEGL folder of the Flatpak, nothing is written there or
# anywhere else of yours, and a GIMP of yours does not matter.
here=$(cd "$(dirname "$0")" && pwd)
tests=$(dirname "$here")
src=$(dirname "$tests")
rm -rf "$tests/output/gui-profile"
mkdir -p "$tests/output/gui-profile"
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$tests/output/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/../isolate.sh
. "$tests/isolate.sh"
# a copy of the installed plug-in, and the installed GEGL operations
# (read only)
installed=${XDG_CONFIG_HOME:-$HOME/.config}/GIMP/3.2/plug-ins/gimp-lensfun
[ -x "$installed/gimp-lensfun" ] || { echo "no installed plug-in in $installed (README)" >&2; exit 1; }
gegl_path="$HOME/.var/app/org.gimp.GIMP/data/gegl-0.4/plug-ins:/app/lib/gegl-0.4"
rm -rf "$tests/output/gui-profile/plug-ins"
mkdir -p "$tests/output/gui-profile/plug-ins"
cp -R "$installed" "$tests/output/gui-profile/plug-ins/"
gimp_run --flatpak --filesystem="$tests" --env=GDK_BACKEND=broadway --env=BROADWAY_DISPLAY=:5 \
  --env=GIMP3_DIRECTORY="$tests/output/gui-profile" --env=GEGL_PATH="$gegl_path" \
  --env=LF_IMAGE="$tests/output/grid.png" -- sh -c \
  "broadwayd --port 8085 :5 & bw=\$!; sleep 2; gimp-3.2 --new-instance --no-splash \
   --batch-interpreter python-fu-eval -b \"exec(open('$here/open-dialog.py').read())\";
   kill \$bw"
