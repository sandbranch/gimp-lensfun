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
#include <cstring>

#include <gexiv2/gexiv2.h>

#include "lensdb.h"
#include "lensdb-exif.h"

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
        settings.maker = lensdb_mlstr (camera->Maker);
        settings.camera = lensdb_mlstr (camera->Model);
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
            settings.lens = lensdb_mlstr (lenses[0]->Model);
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
