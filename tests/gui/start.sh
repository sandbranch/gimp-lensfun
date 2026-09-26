#!/bin/sh
# Opens the plug-in's dialog on the test grid in the Flatpak GIMP on a
# Broadway display (http://127.0.0.1:8085/), to look at it with
# gimp-plugin-devtools/gui/cdp.mjs. Run tests/compare.sh first (it makes
# the grid), and close GIMP. Broadway stops when GIMP quits.
here=$(cd "$(dirname "$0")" && pwd)
tests=$(dirname "$here")
flatpak run --filesystem="$tests" --env=GDK_BACKEND=broadway --env=BROADWAY_DISPLAY=:5 \
  --env=LF_IMAGE="$tests/output/grid.png" --command=sh org.gimp.GIMP -c \
  "broadwayd --port 8085 :5 & bw=\$!; sleep 2; gimp-3.2 --no-splash \
   --batch-interpreter python-fu-eval -b \"exec(open('$here/open-dialog.py').read())\";
   kill \$bw"
