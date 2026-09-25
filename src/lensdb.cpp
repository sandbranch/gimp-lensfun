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

#include <gexiv2/gexiv2.h>

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

    /* the newer of the copy of the plug-in and the user's updates */
    if (db_timestamp (updates) > db_timestamp (db_dir))
        loaded = db->LoadDirectory (updates);
    else
        loaded = db->LoadDirectory (db_dir);

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

static std::string
mlstr (const lfMLstr s)
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
        makers.insert (mlstr (cameras[i]->Maker));
    return std::vector<std::string> (makers.begin (), makers.end ());
}

std::vector<std::string>
lensdb_cameras (const lfDatabase *db, const std::string &maker)
{
    std::set<std::string> names;
    const lfCamera *const *cameras = db->GetCameras ();

    for (int i = 0; cameras && cameras[i]; i++)
        if (mlstr_matches (cameras[i]->Maker, maker))
            names.insert (mlstr (cameras[i]->Model));
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
        names.insert (mlstr (lenses[i]->Model));
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

/* The value of an Exif tag as Exiv2 prints it, which for maker notes is
   the lens name decoded from the lens ID; empty if missing. */
static std::string
exif_string (GExiv2Metadata *metadata, const gchar *tag, bool interpreted)
{
    gchar *value;
    std::string result;

    if (!gexiv2_metadata_try_has_tag (metadata, tag, NULL))
        return "";

    value = interpreted
        ? gexiv2_metadata_try_get_tag_interpreted_string (metadata, tag, NULL)
        : gexiv2_metadata_try_get_tag_string (metadata, tag, NULL);
    if (value)
        result = g_strstrip (value);
    g_free (value);
    return result;
}

static void
replace_all (std::string &str, const std::string &old, const std::string &by)
{
    for (size_t pos = str.find (old); pos != std::string::npos;
         pos = str.find (old, pos + by.size ()))
        str.replace (pos, old.size (), by);
}

gboolean
lensdb_settings_from_metadata (const lfDatabase *db, GimpImage *image,
                               LensSettings &settings)
{
    GimpMetadata *gimp_metadata = gimp_image_get_metadata (image);
    GExiv2Metadata *metadata;
    std::string make, model, maker_lc, lens_name;
    std::vector<const gchar *> lens_tags;
    const lfCamera **cameras;
    const lfCamera *camera = NULL;

    if (!gimp_metadata)
        return FALSE;
    metadata = GEXIV2_METADATA (gimp_metadata);

    make = exif_string (metadata, "Exif.Image.Make", false);
    model = exif_string (metadata, "Exif.Image.Model", false);
    if (make.empty ())
        return FALSE;

    /* the camera, as the database names it */
    settings = LensSettings ();
    cameras = db->FindCameras (make.c_str (), model.c_str ());
    if (cameras)
    {
        camera = cameras[0];
        settings.maker = mlstr (camera->Maker);
        settings.camera = mlstr (camera->Model);
    }
    else
        settings.maker = make;
    lf_free (cameras);

    /* the lens: maker notes tell more than the standard tag for older
       cameras of these makers, the standard tag is used for the rest */
    maker_lc = make;
    std::transform (maker_lc.begin (), maker_lc.end (), maker_lc.begin (),
                    ::tolower);
    if (maker_lc.find ("pentax") != std::string::npos)
        lens_tags.push_back ("Exif.Pentax.LensType");
    else if (maker_lc.find ("canon") != std::string::npos)
        lens_tags.push_back ("Exif.CanonCs.LensType");
    else if (maker_lc.find ("minolta") != std::string::npos)
        lens_tags.push_back ("Exif.Minolta.LensID");
    else if (maker_lc.find ("nikon") != std::string::npos)
    {
        lens_tags.push_back ("Exif.NikonLd3.LensIDNumber");
        lens_tags.push_back ("Exif.NikonLd2.LensIDNumber");
        lens_tags.push_back ("Exif.NikonLd1.LensIDNumber");
    }
    else if (maker_lc.find ("olympus") != std::string::npos)
        lens_tags.push_back ("Exif.OlympusEq.LensType");
    lens_tags.push_back ("Exif.Photo.LensModel");

    for (const gchar *tag : lens_tags)
    {
        lens_name = exif_string (metadata, tag, true);
        if (!lens_name.empty ())
            break;
    }
    if (maker_lc.find ("nikon") != std::string::npos)
    {
        /* modify some lens names for better searching in the database */
        replace_all (lens_name, "Nikon", "");
        replace_all (lens_name, "Zoom-Nikkor", "");
    }

    if (camera)
    {
        /* only take lens names of significant length */
        const lfLens **lenses =
            db->FindLenses (camera, NULL,
                            lens_name.size () > 8 ? lens_name.c_str () : NULL);
        if (lenses)
            settings.lens = mlstr (lenses[0]->Model);
        lf_free (lenses);
    }

    settings.focal = gexiv2_metadata_try_get_focal_length (metadata, NULL);
    settings.aperture = gexiv2_metadata_try_get_fnumber (metadata, NULL);
    if (settings.focal < 0)
        settings.focal = 0;
    if (settings.aperture < 0)
        settings.aperture = 0;

    return TRUE;
}
