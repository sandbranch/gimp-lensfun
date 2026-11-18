# GIMP-Lensfun for GIMP 3

A GIMP plug-in that corrects the distortion, the transversal chromatic
aberration (colour fringes) and the vignetting of photos, using the
[lensfun](https://lensfun.github.io/) database of cameras and lenses.

This is the GIMP 3 version of Sebastian Kraft's
[GIMP-Lensfun](https://github.com/seebk/GIMP-Lensfun), whose last release was
for GIMP 2.x. Please report problems with it at
https://github.com/sandbranch/gimp-lensfun/issues.

## Using it

Filters > Enhance > Lens Correction (Lensfun)...

The camera, the lens, the focal length and the aperture are read from the
Exif data of the image and can be changed in the dialog. The preview shows
the whole layer corrected. Only the corrections the lens is calibrated for
can be switched on. "Target geometry" converts the projection, for example
a fisheye photo to a normal (rectilinear) perspective.

The whole layer is taken as the photo, with the optical center in its
middle; a selection only limits where the result is applied. The pixels
of a layer group cannot be changed; keep the correction as an editable
filter there (below).

When the Exif data name a lens that the database does not know, or no
lens at all, the lens has to be chosen: the plug-in only picks one by
itself for cameras with a fixed lens.

Images of any precision (8 and 16 bit, floating point), RGB and grayscale,
with or without alpha, are corrected in linear light at full precision.

### As an editable filter

The correction also exists as a GEGL operation, `lensfun:correct`, which
GIMP keeps on the layer as a non-destructive filter: its settings can be
changed later, with live preview. The easiest way to add it is "Keep as an
editable filter" in the plug-in's dialog: the plug-in fills in the camera
and lens from the Exif data and adds the filter instead of changing the
pixels. The filter can also be added directly, from Filters > Enhance >
Lens Correction (Lensfun Filter)..., with the camera maker, camera and lens
typed as they are in the database (it cannot read Exif data itself).

From scripts, the procedure `plug-in-lensfun` takes the camera maker,
camera model, lens model, focal length and aperture (empty or 0 to use the
Exif data), the subject distance, which corrections to apply, the target
geometry and the interpolation, and `as-filter` to add the filter.

## The lens database

When lensfun is built in (below), the plug-in installs the lensfun
database next to itself, and the filter a copy in
`~/.local/share/gegl-0.4/lensfun-db`. If you run `lensfun-update-data`
(on Debian and Ubuntu in the package `liblensfun-bin`), or download a newer
database into `~/.local/share/lensfun/updates/version_1`, the newer one is
used. Built against the system's lensfun, both use the database of the
lensfun library (`/usr/share/lensfun`), with those updates. The database is updated daily at
https://lensfun.github.io/db/version_1.tar.bz2; `lensfun-update-data` also
writes a `timestamp.txt` there with the time stamp from
https://lensfun.github.io/db/versions.json.

## Building and installing

Needs the native GIMP 3 with its development files, meson, ninja, a C++
compiler, and either the lensfun 0.3 development files or, to build
lensfun from source, CMake and git. On Debian and Ubuntu:

    sudo apt install gimp libgimp-3.0-dev meson ninja-build g++ liblensfun-dev

(or `cmake git` instead of `liblensfun-dev`). Then:

    meson setup build -Dplugindir=$HOME/.config/GIMP/3.2/plug-ins \
      -Dmoduledir=$HOME/.local/share/gegl-0.4/plug-ins
    ninja -C build install

`-Dmoduledir` is where GEGL loads operations from; the filter's copy of
the database goes into `lensfun-db` next to that folder, and the filter
finds it by the path given at build time, so build again after moving
the folders. `-Dgegl_op=disabled` builds only the plug-in.

Lensfun 0.3.4 is built from source and linked in when the system has no
lensfun 0.3 (`-Dbundled_lensfun=enabled` forces it). Its database is then
installed with the plug-in; `-Dlensfun_db=<folder>` installs another one.

Or with [gimp-devtools](https://github.com/sandbranch/gimp-devtools),
which finds the folders of the GIMP it builds for:

    gimp-build.sh . meson setup build -Dplugindir=\$GIMP_PLUGINDIR -Dmoduledir=\$GEGL_OPDIR
    gimp-build.sh . ninja -C build install

Restart GIMP after installing.

## Tests

    tests/run.sh

builds into `tests/output` (not into GIMP's folders) and runs all tests
without a display or the network, printing PASS or FAIL for each and
exiting non-zero if any failed: the unit tests (`meson test`, also under
valgrind if it is installed), lensfun:correct in GEGL alone, and GIMP
without a window in a throwaway profile that loads only the test build
(plug-in and filter agree, 8/16-bit and float, gray, alpha, Exif data,
the gegl command line, and more). It builds with gimp-devtools, next to
this repository or given as `$GIMP_BUILD`. Close
GIMP first. The unit tests need a database: the bundled lensfun's, or
one given with `-Dlensfun_db`; without one they are skipped.

With AddressSanitizer and UndefinedBehaviorSanitizer:

    meson setup build-asan -Db_sanitize=address,undefined -Db_lundef=false
    meson test -C build-asan

`tests/compare.sh` corrects a grid with the *installed* plug-in and
filter and checks that they agree.
## License

GPL version 3 or later, see LICENSE.txt. Lensfun is LGPL 3; its database is
CC BY-SA 3.0.
