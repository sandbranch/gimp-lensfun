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

#include <glib.h>
#include <lensfun.h>

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

/* The camera and lens of a photo, as the database names them, from the
   maker, model and lens name of its Exif data; the focal length and
   aperture are left 0. Without a lens name that the database recognizes,
   the lens is only chosen when it is the only one that fits the camera
   (a camera with a fixed lens). Fields that cannot be determined are
   left empty. */
void lensdb_settings_from_exif (const lfDatabase *db, const std::string &make,
                                const std::string &model,
                                const std::string &lens_name,
                                LensSettings &settings);

/* The default (untranslated) variant of a multi-language string. */
std::string lensdb_mlstr (const lfMLstr s);

#endif /* LENSDB_H */
