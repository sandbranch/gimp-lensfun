/*
 * Copyright 2010-2011 Sebastian Kraft
 *
 * This file is part of GimpLensfun.
 *
 * GimpLensfun is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version
 * 3 of the License, or (at your option) any later version.
 *
 * GimpLensfun is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with GimpLensfun. If not, see
 * http://www.gnu.org/licenses/.
 */

#ifndef LENSDB_EXIF_H
#define LENSDB_EXIF_H

#include <libgimp/gimp.h>

#include "lensdb.h"

/* Fill in the camera, lens, focal length and aperture from the Exif data
   of the image. Returns FALSE if the image has no usable Exif data;
   fields that cannot be determined are left empty or 0. */
gboolean lensdb_settings_from_metadata (const lfDatabase *db,
                                        GimpImage *image,
                                        LensSettings &settings);

#endif /* LENSDB_EXIF_H */
