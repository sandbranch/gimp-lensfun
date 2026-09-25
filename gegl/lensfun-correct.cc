/*
 * lensfun:correct, the lens correction of GIMP-Lensfun as a GEGL operation
 *
 * Copyright 2026 by David
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
 *
 * The same correction as the plug-in (src/correct.cpp), as a filter that
 * GIMP can keep editable on a layer, with live preview. A GEGL operation
 * sees only pixels, not the Exif data of the photo, so the camera, lens,
 * focal length and aperture are its settings; the plug-in fills them in
 * from the Exif data when it adds this filter.
 */

#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (lensfun_geometry)
  enum_value (LENSFUN_GEOMETRY_RECTILINEAR,     "rectilinear",           N_("Rectilinear"))
  enum_value (LENSFUN_GEOMETRY_LENS,            "lens",                  N_("Keep the lens geometry"))
  enum_value (LENSFUN_GEOMETRY_FISHEYE,         "fisheye",               N_("Fisheye (equidistant)"))
  enum_value (LENSFUN_GEOMETRY_EQUISOLID,       "fisheye-equisolid",     N_("Fisheye (equisolid)"))
  enum_value (LENSFUN_GEOMETRY_ORTHOGRAPHIC,    "fisheye-orthographic",  N_("Fisheye (orthographic)"))
  enum_value (LENSFUN_GEOMETRY_STEREOGRAPHIC,   "fisheye-stereographic", N_("Fisheye (stereographic)"))
  enum_value (LENSFUN_GEOMETRY_THOBY,           "fisheye-thoby",         N_("Fisheye (Thoby)"))
  enum_value (LENSFUN_GEOMETRY_PANORAMIC,       "panoramic",             N_("Panoramic (cylindrical)"))
  enum_value (LENSFUN_GEOMETRY_EQUIRECTANGULAR, "equirectangular",       N_("Equirectangular"))
enum_end (LensfunGeometry)

enum_start (lensfun_interpolation)
  enum_value (LENSFUN_INTERPOLATION_NEAREST, "nearest", N_("Nearest neighbour"))
  enum_value (LENSFUN_INTERPOLATION_LINEAR,  "linear",  N_("Linear"))
  enum_value (LENSFUN_INTERPOLATION_LANCZOS, "lanczos", N_("Lanczos"))
enum_end (LensfunInterpolation)

property_string (camera_maker, _("Camera maker"), "")
  description (_("Maker of the camera, as in the Lensfun database, e.g. Canon"))

property_string (camera_model, _("Camera"), "")
  description (_("Camera model, as in the Lensfun database, e.g. Canon EOS 5D Mark II"))

property_string (lens_model, _("Lens"), "")
  description (_("Lens model, as in the Lensfun database"))

property_double (focal_length, _("Focal length (mm)"), 0.0)
  description (_("Focal length in mm; 0 for the shortest one of the lens"))
  value_range (0.0, 10000.0)
  ui_range (0.0, 400.0)

property_double (aperture, _("Aperture (f-number)"), 0.0)
  description (_("Aperture as f-number, used for the vignetting"))
  value_range (0.0, 128.0)
  ui_range (0.0, 32.0)

property_double (distance, _("Subject distance (m)"), 1.0)
  description (_("Distance to the subject in meters, used for the vignetting"))
  value_range (0.01, 1000.0)
  ui_range (0.1, 100.0)
  ui_gamma (3.0)

property_boolean (correct_distortion, _("Distortion"), TRUE)
  description (_("Correct the distortion"))

property_boolean (correct_tca, _("Chromatic aberration"), FALSE)
  description (_("Correct the transversal chromatic aberration (colour fringes)"))

property_boolean (correct_vignetting, _("Vignetting"), FALSE)
  description (_("Correct the vignetting (darker corners)"))

property_boolean (scale_to_fit, _("Scale to fit"), TRUE)
  description (_("Scale the result so that it has no empty borders"))

property_enum (target_geometry, _("Target geometry"), LensfunGeometry,
               lensfun_geometry, LENSFUN_GEOMETRY_RECTILINEAR)
  description (_("The projection of the result; rectilinear turns a fisheye "
                 "into a normal perspective"))

property_enum (interpolation, _("Interpolation"), LensfunInterpolation,
               lensfun_interpolation, LENSFUN_INTERPOLATION_LANCZOS)
  description (_("How pixels are resampled"))

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     lensfun_correct
#define GEGL_OP_C_SOURCE lensfun-correct.cc

#include "gegl-op.h"

#include <cstring>

#include "correct.h"
#include "lensdb.h"

/* the database is read once per GIMP session */
static lfDatabase *
database (void)
{
  static gsize        once = 0;
  static lfDatabase  *db   = NULL;

  if (g_once_init_enter (&once))
    {
      db = lensdb_load (LENSFUN_OP_DB_DIR);
      g_once_init_leave (&once, 1);
    }

  return db;
}

static const lfLensType geometries[] =
{
  LF_RECTILINEAR, LF_UNKNOWN, LF_FISHEYE, LF_FISHEYE_EQUISOLID,
  LF_FISHEYE_ORTHOGRAPHIC, LF_FISHEYE_STEREOGRAPHIC, LF_FISHEYE_THOBY,
  LF_PANORAMIC, LF_EQUIRECTANGULAR
};

static void
prepare (GeglOperation *operation)
{
  const Babl *space  = gegl_operation_get_source_space (operation, "input");
  /* the correction works in linear light */
  const Babl *format = babl_format_with_space ("RGBA float", space);

  gegl_operation_set_format (operation, "input", format);
  gegl_operation_set_format (operation, "output", format);
}

/* the optical center is the center of the whole photo, and a pixel of the
 * result can come from anywhere in it */
static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  const GeglRectangle *in = gegl_operation_source_get_bounding_box (operation, "input");

  return in ? *in : *roi;
}

static GeglRectangle
get_cached_region (GeglOperation       *operation,
                   const GeglRectangle *roi)
{
  const GeglRectangle *in = gegl_operation_source_get_bounding_box (operation, "input");

  return in ? *in : *roi;
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *result,
         gint                 level)
{
  GeglProperties      *o      = GEGL_PROPERTIES (operation);
  const Babl          *format = gegl_operation_get_format (operation, "output");
  const GeglRectangle *whole  = gegl_operation_source_get_bounding_box (operation, "input");
  lfDatabase          *db     = database ();
  LensSettings         settings;
  CorrectionOptions    options;
  const lfCamera      *camera;
  const lfLens        *lens;
  FloatImage           src;
  float               *dest;

  settings.maker    = o->camera_maker ? o->camera_maker : "";
  settings.camera   = o->camera_model ? o->camera_model : "";
  settings.lens     = o->lens_model ? o->lens_model : "";
  settings.focal    = o->focal_length;
  settings.aperture = o->aperture;

  camera = lensdb_find_camera (db, settings);
  lens   = lensdb_find_lens (db, settings);

  /* not in the database (or not set yet): the photo stays as it is */
  if (!camera || !lens || !whole)
    {
      gegl_buffer_copy (input, result, GEGL_ABYSS_NONE, output, result);
      return TRUE;
    }

  if (settings.focal <= 0.0)
    settings.focal = lens->MinFocal;

  options.distortion    = o->correct_distortion;
  options.tca           = o->correct_tca;
  options.vignetting    = o->correct_vignetting;
  options.scale_to_fit  = o->scale_to_fit;
  options.distance      = o->distance;
  options.target        = geometries[o->target_geometry];
  options.interpolation = (Interpolation) o->interpolation;

  src.width    = whole->width;
  src.height   = whole->height;
  src.channels = 4;
  src.gray     = false;
  src.alpha    = true;
  src.pixels   = (float *) g_malloc ((gsize) src.width * src.height * 4 * sizeof (float));
  dest         = (float *) g_malloc ((gsize) src.width * src.height * 4 * sizeof (float));

  gegl_buffer_get (input, whole, 1.0, format, src.pixels,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
  lens_correct (lens, camera->CropFactor, settings, options, src, dest);
  gegl_buffer_set (output, whole, 0, format, dest, GEGL_AUTO_ROWSTRIDE);

  g_free (dest);
  g_free (src.pixels);

  return TRUE;
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass       *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationFilterClass *filter_class    = GEGL_OPERATION_FILTER_CLASS (klass);

  operation_class->prepare                 = prepare;
  operation_class->get_required_for_output = get_required_for_output;
  operation_class->get_cached_region       = get_cached_region;
  /* lens_correct spreads its work over GEGL's threads itself */
  operation_class->threaded                = FALSE;
  filter_class->process                    = process;

  gegl_operation_class_set_keys (operation_class,
    "name",            "lensfun:correct",
    "title",           _("Lens Correction (Lensfun)"),
    "categories",      "distort",
    "description",     _("Corrects the distortion, chromatic aberration and "
                         "vignetting of a camera lens, from the Lensfun "
                         "database"),
    "gimp:menu-path",  "<Image>/Filters/Enhance",
    "gimp:menu-label", _("Lens Correction (Lensfun Filter)..."),
    NULL);
}

#endif
