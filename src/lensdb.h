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

#ifndef LENSDB_H
#define LENSDB_H

#include <string>
#include <vector>

#include <lensfun.h>
#include <libgimp/gimp.h>

/* The camera and lens settings of a correction. */
struct LensSettings
{
    std::string maker;
    std::string camera;
    std::string lens;
    double focal = 0.0;
    double aperture = 0.0;
};

/* Load the Lensfun database: the copy installed with the plug-in in
   db_dir, or the user's updates from lensfun-update-data if they are
   newer, plus the user's own additions. */
lfDatabase *lensdb_load (const gchar *db_dir);

/* Sorted names of all camera makers, of the cameras of a maker, and of
   the lenses that fit a camera. */
std::vector<std::string> lensdb_makers (const lfDatabase *db);
std::vector<std::string> lensdb_cameras (const lfDatabase *db,
                                         const std::string &maker);
std::vector<std::string> lensdb_lenses (const lfDatabase *db,
                                        const std::string &maker,
                                        const std::string &camera);

/* The database entries named by settings, or NULL. */
const lfCamera *lensdb_find_camera (const lfDatabase *db,
                                    const LensSettings &settings);
const lfLens *lensdb_find_lens (const lfDatabase *db,
                                const LensSettings &settings);

/* Fill in the camera, lens, focal length and aperture from the Exif data
   of the image. Returns FALSE if the image has no usable Exif data;
   fields that cannot be determined are left empty or 0. */
gboolean lensdb_settings_from_metadata (const lfDatabase *db,
                                        GimpImage *image,
                                        LensSettings &settings);

#endif /* LENSDB_H */
