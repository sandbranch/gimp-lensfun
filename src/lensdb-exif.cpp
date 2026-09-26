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

#include <cmath>
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

gboolean
lensdb_settings_from_metadata (const lfDatabase *db, GimpImage *image,
                               LensSettings &settings)
{
    GimpMetadata *gimp_metadata = gimp_image_get_metadata (image);
    GExiv2Metadata *metadata;
    std::string make, model, lens_name;
    std::vector<const gchar *> lens_tags;
    gchar *maker_lc;

    settings = LensSettings ();
    if (!gimp_metadata)
        return FALSE;
    metadata = GEXIV2_METADATA (gimp_metadata);

    make = exif_string (metadata, "Exif.Image.Make", false);
    model = exif_string (metadata, "Exif.Image.Model", false);
    if (make.empty ())
        return FALSE;

    /* the lens: maker notes tell more than the standard tag for older
       cameras of these makers, the standard tag is used for the rest */
    maker_lc = g_ascii_strdown (make.c_str (), -1);
    if (strstr (maker_lc, "pentax"))
        lens_tags.push_back ("Exif.Pentax.LensType");
    else if (strstr (maker_lc, "canon"))
        lens_tags.push_back ("Exif.CanonCs.LensType");
    else if (strstr (maker_lc, "minolta"))
        lens_tags.push_back ("Exif.Minolta.LensID");
    else if (strstr (maker_lc, "nikon"))
    {
        lens_tags.push_back ("Exif.NikonLd3.LensIDNumber");
        lens_tags.push_back ("Exif.NikonLd2.LensIDNumber");
        lens_tags.push_back ("Exif.NikonLd1.LensIDNumber");
    }
    else if (strstr (maker_lc, "olympus"))
        lens_tags.push_back ("Exif.OlympusEq.LensType");
    lens_tags.push_back ("Exif.Photo.LensModel");
    g_free (maker_lc);

    for (const gchar *tag : lens_tags)
    {
        lens_name = exif_string (metadata, tag, true);
        if (!lens_name.empty ())
            break;
    }

    lensdb_settings_from_exif (db, make, model, lens_name, settings);

    /* missing tags give -1, odd rationals (0/0) may give no number */
    settings.focal = gexiv2_metadata_try_get_focal_length (metadata, NULL);
    settings.aperture = gexiv2_metadata_try_get_fnumber (metadata, NULL);
    if (!std::isfinite (settings.focal) || settings.focal < 0)
        settings.focal = 0;
    if (!std::isfinite (settings.aperture) || settings.aperture < 0)
        settings.aperture = 0;

    return TRUE;
}
