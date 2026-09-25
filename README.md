# GIMP-Lensfun for GIMP 3

A GIMP plug-in that corrects the distortion, the transversal chromatic
aberration (colour fringes) and the vignetting of photos, using the
[lensfun](https://lensfun.github.io/) database of cameras and lenses.

This is the GIMP 3 version of Sebastian Kraft's
[GIMP-Lensfun](https://github.com/seebk/GIMP-Lensfun), whose last release was
for GIMP 2.x. Please report problems with it at
https://github.com/sandbranch/GIMP-Lensfun/issues.

## Using it

Filters > Enhance > Lens Correction (Lensfun)...

The camera, the lens, the focal length and the aperture are read from the
Exif data of the image and can be changed in the dialog. The preview shows
the whole layer corrected. Only the corrections the lens is calibrated for
can be switched on. "Target geometry" converts the projection, for example
a fisheye photo to a normal (rectilinear) perspective.

The whole layer is taken as the photo, with the optical center in its
middle; a selection only limits where the result is applied.

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

The plug-in installs the lensfun database next to itself. If you run
`lensfun-update-data`, or download a newer database into
`~/.local/share/lensfun/updates/version_1` (for the Flatpak version of GIMP,
`~/.var/app/org.gimp.GIMP/data/lensfun/updates/version_1`), the newer one is
used. The database is updated daily at
https://lensfun.github.io/db/version_1.tar.bz2; `lensfun-update-data` also
writes a `timestamp.txt` there with the time stamp from
https://lensfun.github.io/db/versions.json.

## Building and installing

Needs meson, ninja, a C++ compiler, the GIMP 3 development files, and either
lensfun 0.3 or, to build lensfun from source, CMake and git.

    meson setup build -Dplugindir=$HOME/.config/GIMP/3.2/plug-ins \
      -Dmoduledir=$HOME/.local/share/gegl-0.4/plug-ins
    ninja -C build install

`-Dmoduledir` is where GEGL loads operations from (for the Flatpak version
of GIMP, `~/.var/app/org.gimp.GIMP/data/gegl-0.4/plug-ins`); the filter's
copy of the database goes into `lensfun-db` next to that folder.
`-Dgegl_op=disabled` builds only the plug-in.

Lensfun 0.3.4 is built from source and linked in when the system has no
lensfun 0.3 (`-Dbundled_lensfun=enabled` forces it). Its database is then
installed with the plug-in; `-Dlensfun_db=<folder>` installs another one.

### Flatpak GIMP

The Flatpak version of GIMP has no lensfun. Build against the GIMP of the
Flatpak, with the GNOME SDK that GIMP was built with (see
`flatpak info org.gimp.GIMP`); lensfun is then built in:

    flatpak install --user flathub org.gnome.Sdk//50
    flatpak run --devel --filesystem=$PWD --filesystem=~/.config/GIMP \
      --env=PKG_CONFIG_PATH=/app/lib/pkgconfig --command=sh org.gimp.GIMP -c \
      'meson setup build -Dplugindir=$HOME/.config/GIMP/3.2/plug-ins \
         -Dmoduledir=$XDG_DATA_HOME/gegl-0.4/plug-ins &&
       ninja -C build install'

or with [gimp-plugin-devtools](https://github.com/sandbranch/gimp-plugin-devtools):

    gimp-build.sh . meson setup build -Dplugindir=\$GIMP_PLUGINDIR -Dmoduledir=\$GEGL_OPDIR
    gimp-build.sh . ninja -C build install

`tests/compare.sh` corrects a grid with the installed plug-in and filter
and checks that they agree.

Restart GIMP after installing.

## License

GPL version 3 or later, see LICENSE.txt. Lensfun is LGPL 3; its database is
CC BY-SA 3.0.
