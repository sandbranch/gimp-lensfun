#!/bin/sh
# All tests, one command, no display and no network:
#
#   tests/run.sh
#
# 1. builds the plug-in and lensfun:correct into tests/output/build and
#    installs them into tests/output/install (not into GIMP's folders);
# 2. meson test: the unit tests (tests/test-lensfun.cpp), and again under
#    valgrind if it is there;
# 3. GEGL alone: the filter's cache after a change (tests/gegl-cache.py);
# 4. GIMP without a window, in a throwaway profile that loads only the
#    test build (tests/gimp-test.py), and the gegl command line.
#
# With the Flatpak GIMP it builds with gimp-plugin-devtools/gimp-build.sh
# (next to this repository, or $GIMP_BUILD). The builds, GEGL and GIMP
# run isolated from your folders (tests/isolate.sh, with
# gimp-plugin-devtools/gimp-run.sh): HOME and the XDG folders inside the
# Flatpak point into tests/output/gimp-home, so nothing lands in
# ~/.var/app/org.gimp.GIMP, and your lensfun updates are not used. Before
# and after, it lists your folders of GIMP and the other apps
# (gimp-plugin-devtools/snapshot.sh) and fails if anything there changed.
# Exits non-zero if anything failed.
set -u
here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
out="$here/output"
install="$out/install"
build="$out/build"
gimp_build=${GIMP_BUILD:-$top/../gimp-plugin-devtools/gimp-build.sh}
failed=0

step () { echo; echo "== $*"; }
fail () { echo "FAIL $*"; failed=1; }

mkdir -p "$out"
src=$top
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$here/output/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
snapshot_take "$out/snapshot-before.txt"

step build
if [ ! -f "$build/build.ninja" ]; then
    "$gimp_build" "$top" "meson setup '$build' -Dplugindir='$install/plug-ins' \
        -Dmoduledir='$install/gegl'" >"$out/setup.log" 2>&1 ||
        { cat "$out/setup.log"; fail "meson setup"; exit 1; }
fi
"$gimp_build" "$top" "ninja -C '$build' install" >"$out/build.log" 2>&1 ||
    { tail -30 "$out/build.log"; fail "build"; exit 1; }
echo "PASS build"

step unit tests
"$gimp_build" "$top" "meson test -C '$build' --print-errorlogs" >"$out/unit.log" 2>&1
grep -E '^(PASS|FAIL|SKIP|  )' "$build/meson-logs/testlog.txt"
# SKIP: no database to test with (system lensfun and no -Dlensfun_db)
grep -qE '^ *1/1 .* (OK|SKIP)' "$out/unit.log" || fail "unit tests"

if "$gimp_build" "$top" "command -v valgrind" >/dev/null 2>&1; then
    "$gimp_build" "$top" "meson test -C '$build' --setup valgrind" \
        >"$out/valgrind.log" 2>&1
    if grep -qE '^ *1/1 .* (OK|SKIP)' "$out/valgrind.log"; then
        echo "PASS unit tests under valgrind"
    else
        fail "unit tests under valgrind (see $build/meson-logs/testlog-valgrind.txt)"
    fi
else
    echo "SKIP valgrind is not installed"
fi

# GEGL loads operations from GEGL_PATH only; a folder with just the
# operation, since the JSON files meson writes in a build folder crash it
gegl_path="$install/gegl:/app/lib/gegl-0.4"

# the user's lensfun updates and GEGL operations are not used
data="$out/xdg-data"
profile="$out/profile"
rm -rf "$profile" "$data"
mkdir -p "$profile" "$data"
cat >"$profile/gimprc" <<RC
(plug-in-path "$install/plug-ins:\${gimp_plug_in_dir}/plug-ins")
RC

# run_flatpak <command...>: in the GIMP Flatpak
run_flatpak () {
    gimp_run --flatpak --filesystem="$top" --env=GEGL_PATH="$gegl_path" \
        --env=XDG_DATA_HOME="$data" --env=GIMP3_DIRECTORY="$profile" \
        --env=LF_TEST_OUT="$out" --env=LF_TEST_INSTALL="$install" \
        -- "$@"
}

step GEGL
run_flatpak python3 "$here/gegl-cache.py" \
    >"$out/gegl.log" 2>&1
grep -E '^(PASS|FAIL|    )' "$out/gegl.log"
grep -q '^PASS' "$out/gegl.log" && ! grep -q '^FAIL' "$out/gegl.log" ||
    { tail -5 "$out/gegl.log"; fail "GEGL"; }

step GIMP
run_flatpak gimp-console-3.2 --no-interface \
    --no-data --no-fonts --batch-interpreter python-fu-eval \
    -b "exec(open('$here/gimp-test.py').read())" --quit >"$out/gimp.log" 2>&1
grep -E '^(PASS|FAIL|    )' "$out/gimp.log"
grep -q '^PASS: ' "$out/gimp.log" ||
    { tail -20 "$out/gimp.log"; fail "GIMP"; }

step your folders of GIMP and the other apps
snapshot_check "$out/snapshot-before.txt" "" || failed=1

echo
if [ $failed = 0 ]; then echo "ALL PASSED"; else echo "SOME FAILED"; fi
exit $failed
