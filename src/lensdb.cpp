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

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>

#include "lensdb.h"

/* The version of the database format that Lensfun 0.3 reads. */
#define DB_VERSION_DIR "version_1"

/* The time stamp of a database folder, as lensfun-update-data writes it,
   or 0 if it has none. */
static long
db_timestamp (const gchar *dir)
{
    gchar *file = g_build_filename (dir, "timestamp.txt", NULL);
    gchar *contents = NULL;
    long stamp = 0;

    if (g_file_get_contents (file, &contents, NULL, NULL))
        stamp = strtol (contents, NULL, 10);

    g_free (contents);
    g_free (file);
    return stamp;
}

lfDatabase *
lensdb_load (const gchar *db_dir)
{
    lfDatabase *db = new lfDatabase ();
    gchar *updates = g_build_filename (g_get_user_data_dir (), "lensfun",
                                       "updates", DB_VERSION_DIR, NULL);

    bool loaded;

    /* the newer of the copy of the plug-in and the user's updates, and
       the copy of the plug-in if the updates cannot be read */
    loaded = (db_timestamp (updates) > db_timestamp (db_dir) &&
              db->LoadDirectory (updates)) ||
             db->LoadDirectory (db_dir);

    if (loaded)
        /* the user's own definitions, which override the others */
        db->LoadDirectory (db->HomeDataDir);
    else
        /* no copy with the plug-in: the database of the lensfun library,
           which includes the user's updates and definitions */
        db->Load ();

    g_free (updates);
    return db;
}

std::string
lensdb_mlstr (const lfMLstr s)
{
    const char *str = lf_mlstr_get (s);
    return str ? str : "";
}

/* Whether name is any of the variants of a multi-language string, which
   lensfun stores as "default\0lang\0name\0...lang\0name\0\0": the dialog
   shows the English variant ("EOS 5D Mark II"), the database files and
   scripts may use the default one ("Canon EOS 5D Mark II"). */
static bool
mlstr_matches (const lfMLstr s, const std::string &name)
{
    if (!s)
        return false;
    if (g_ascii_strcasecmp (s, name.c_str ()) == 0)
        return true;
    for (const char *lang = strchr (s, 0) + 1; *lang;)
    {
        const char *value = strchr (lang, 0) + 1;
        if (g_ascii_strcasecmp (value, name.c_str ()) == 0)
            return true;
        lang = strchr (value, 0) + 1;
    }
    return false;
}

std::vector<std::string>
lensdb_makers (const lfDatabase *db)
{
    std::set<std::string> makers;
    const lfCamera *const *cameras = db->GetCameras ();

    for (int i = 0; cameras && cameras[i]; i++)
        makers.insert (lensdb_mlstr (cameras[i]->Maker));
    return std::vector<std::string> (makers.begin (), makers.end ());
}

std::vector<std::string>
lensdb_cameras (const lfDatabase *db, const std::string &maker)
{
    std::set<std::string> names;
    const lfCamera *const *cameras = db->GetCameras ();

    for (int i = 0; cameras && cameras[i]; i++)
        if (mlstr_matches (cameras[i]->Maker, maker))
            names.insert (lensdb_mlstr (cameras[i]->Model));
    return std::vector<std::string> (names.begin (), names.end ());
}

std::vector<std::string>
lensdb_lenses (const lfDatabase *db, const std::string &maker,
               const std::string &camera)
{
    std::set<std::string> names;
    LensSettings settings;
    const lfCamera *cam;
    const lfLens **lenses;

    settings.maker = maker;
    settings.camera = camera;
    cam = lensdb_find_camera (db, settings);
    if (!cam)
        return std::vector<std::string> ();

    lenses = db->FindLenses (cam, NULL, NULL);
    for (int i = 0; lenses && lenses[i]; i++)
        names.insert (lensdb_mlstr (lenses[i]->Model));
    lf_free (lenses);
    return std::vector<std::string> (names.begin (), names.end ());
}

const lfCamera *
lensdb_find_camera (const lfDatabase *db, const LensSettings &settings)
{
    const lfCamera *const *cameras = db->GetCameras ();

    if (settings.maker.empty () || settings.camera.empty ())
        return NULL;

    for (int i = 0; cameras && cameras[i]; i++)
        if (mlstr_matches (cameras[i]->Maker, settings.maker) &&
            mlstr_matches (cameras[i]->Model, settings.camera))
            return cameras[i];
    return NULL;
}

const lfLens *
lensdb_find_lens (const lfDatabase *db, const LensSettings &settings)
{
    const lfCamera *camera = lensdb_find_camera (db, settings);
    const lfLens **lenses;
    const lfLens *found = NULL;

    if (!camera || settings.lens.empty ())
        return NULL;

    lenses = db->FindLenses (camera, NULL, NULL);
    for (int i = 0; lenses && lenses[i]; i++)
        if (mlstr_matches (lenses[i]->Model, settings.lens))
        {
            found = lenses[i];
            break;
        }
    lf_free (lenses);
    return found;
}

static void
replace_all (std::string &str, const std::string &old, const std::string &by)
{
    for (size_t pos = str.find (old); pos != std::string::npos;
         pos = str.find (old, pos + by.size ()))
        str.replace (pos, old.size (), by);
}

void
lensdb_settings_from_exif (const lfDatabase *db, const std::string &make,
                           const std::string &model,
                           const std::string &lens_name,
                           LensSettings &settings)
{
    const lfCamera **cameras;
    const lfCamera *camera = NULL;
    const lfLens **lenses = NULL;
    std::string name = lens_name;
    gchar *maker_lc;

    settings = LensSettings ();
    if (make.empty ())
        return;

    /* the camera, as the database names it; without a model lensfun
       would return all cameras of the maker */
    cameras = model.empty () ? NULL
                             : db->FindCameras (make.c_str (), model.c_str ());
    if (cameras)
    {
        camera = cameras[0];
        settings.maker = lensdb_mlstr (camera->Maker);
        settings.camera = lensdb_mlstr (camera->Model);
    }
    else
    {
        settings.maker = make;
        settings.camera = model;
    }
    lf_free (cameras);
    if (!camera)
        return;

    maker_lc = g_ascii_strdown (make.c_str (), -1);
    if (strstr (maker_lc, "nikon"))
    {
        /* modify some lens names for better searching in the database */
        replace_all (name, "Nikon", "");
        replace_all (name, "Zoom-Nikkor", "");
    }
    g_free (maker_lc);

    /* only take lens names of significant length */
    if (name.size () > 8)
        lenses = db->FindLenses (camera, NULL, name.c_str ());
    if (!lenses)
    {
        /* no name, or not one the database knows: only a lens that is the
           only one for the camera (a fixed lens) is the lens for sure */
        lenses = db->FindLenses (camera, NULL, NULL);
        if (lenses && lenses[0] && lenses[1])
        {
            lf_free (lenses);
            lenses = NULL;
        }
    }
    if (lenses && lenses[0])
        settings.lens = lensdb_mlstr (lenses[0]->Model);
    lf_free (lenses);
}
