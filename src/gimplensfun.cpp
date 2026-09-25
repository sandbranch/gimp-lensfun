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

#include <cstring>
#include <string>
#include <vector>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "correct.h"
#include "lensdb.h"
#include "lensdb-exif.h"

#define PLUG_IN_PROC    "plug-in-lensfun"
#define PLUG_IN_BINARY  "gimp-lensfun"
#define VERSIONSTR      "0.3.0"

/* the folder of the database installed with the plug-in, next to it */
#define DB_DIR_NAME     "lensfun-db"

/* The target geometries, as nicks of the "target-geometry" argument and
   the lensfun lens types. "lens" keeps the geometry of the lens. */
static const struct
{
    const char *nick;
    const char *label;
    lfLensType type;
} geometries[] = {
    { "rectilinear",           "Rectilinear",              LF_RECTILINEAR },
    { "lens",                  "Keep the lens geometry",   LF_UNKNOWN },
    { "fisheye",               "Fisheye (equidistant)",    LF_FISHEYE },
    { "fisheye-equisolid",     "Fisheye (equisolid)",      LF_FISHEYE_EQUISOLID },
    { "fisheye-orthographic",  "Fisheye (orthographic)",   LF_FISHEYE_ORTHOGRAPHIC },
    { "fisheye-stereographic", "Fisheye (stereographic)",  LF_FISHEYE_STEREOGRAPHIC },
    { "fisheye-thoby",         "Fisheye (Thoby)",          LF_FISHEYE_THOBY },
    { "panoramic",             "Panoramic (cylindrical)",  LF_PANORAMIC },
    { "equirectangular",       "Equirectangular",          LF_EQUIRECTANGULAR },
};

typedef struct _GimpLensfun      GimpLensfun;
typedef struct _GimpLensfunClass GimpLensfunClass;

struct _GimpLensfun
{
    GimpPlugIn parent_instance;
};

struct _GimpLensfunClass
{
    GimpPlugInClass parent_class;
};

#define GIMP_LENSFUN_TYPE (gimp_lensfun_get_type ())

GType gimp_lensfun_get_type (void);

static GList          *gimp_lensfun_query_procedures (GimpPlugIn *plug_in);
static GimpProcedure  *gimp_lensfun_create_procedure (GimpPlugIn *plug_in,
                                                      const gchar *name);
static GimpValueArray *gimp_lensfun_run (GimpProcedure *procedure,
                                         GimpRunMode run_mode,
                                         GimpImage *image,
                                         GimpDrawable **drawables,
                                         GimpProcedureConfig *config,
                                         gpointer run_data);

G_DEFINE_TYPE (GimpLensfun, gimp_lensfun, GIMP_TYPE_PLUG_IN)

GIMP_MAIN (GIMP_LENSFUN_TYPE)

static void
gimp_lensfun_class_init (GimpLensfunClass *klass)
{
    GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS (klass);

    plug_in_class->query_procedures = gimp_lensfun_query_procedures;
    plug_in_class->create_procedure = gimp_lensfun_create_procedure;
    /* the plug-in has no translations */
    plug_in_class->set_i18n = NULL;
}

static void
gimp_lensfun_init (GimpLensfun *lensfun)
{
}

static GList *
gimp_lensfun_query_procedures (GimpPlugIn *plug_in)
{
    return g_list_append (NULL, g_strdup (PLUG_IN_PROC));
}

static GimpProcedure *
gimp_lensfun_create_procedure (GimpPlugIn *plug_in, const gchar *name)
{
    GimpProcedure *procedure;
    GimpChoice *geometry_choice;

    if (strcmp (name, PLUG_IN_PROC))
        return NULL;

    procedure = gimp_image_procedure_new (plug_in, name,
                                          GIMP_PDB_PROC_TYPE_PLUGIN,
                                          gimp_lensfun_run, NULL, NULL);

    gimp_procedure_set_image_types (procedure, "RGB*, GRAY*");
    gimp_procedure_set_sensitivity_mask (procedure,
                                         GIMP_PROCEDURE_SENSITIVE_DRAWABLE);
    gimp_procedure_set_menu_label (procedure, "_Lens Correction (Lensfun)...");
    gimp_procedure_add_menu_path (procedure, "<Image>/Filters/Enhance");
    gimp_procedure_set_documentation (
        procedure,
        "Correct lens distortion, chromatic aberration and vignetting "
        "with lensfun",
        "Corrects the distortion, the transversal chromatic aberration "
        "and the vignetting of a photo using the lensfun database of "
        "cameras and lenses. Camera, lens, focal length and aperture "
        "are taken from the Exif data of the image unless given. The "
        "whole layer is the photo, whose center is the optical center.",
        name);
    gimp_procedure_set_attribution (procedure, "Sebastian Kraft",
                                    "Copyright Sebastian Kraft",
                                    "2010-2026");

    gimp_procedure_add_string_argument (procedure, "camera-maker",
                                        "Camera _maker",
                                        "Camera maker as named in the lensfun "
                                        "database, empty for the one of the "
                                        "Exif data",
                                        "", G_PARAM_READWRITE);
    gimp_procedure_add_string_argument (procedure, "camera-model",
                                        "_Camera", "Camera model, empty for the "
                                        "one of the Exif data",
                                        "", G_PARAM_READWRITE);
    gimp_procedure_add_string_argument (procedure, "lens-model",
                                        "_Lens", "Lens model, empty for the one "
                                        "of the Exif data",
                                        "", G_PARAM_READWRITE);
    gimp_procedure_add_double_argument (procedure, "focal-length",
                                        "_Focal length (mm)",
                                        "Focal length in mm, 0 for the one of "
                                        "the Exif data",
                                        0.0, 10000.0, 0.0, G_PARAM_READWRITE);
    gimp_procedure_add_double_argument (procedure, "aperture",
                                        "_Aperture (f-number)",
                                        "Aperture as f-number, used for the "
                                        "vignetting, 0 for the one of the "
                                        "Exif data",
                                        0.0, 128.0, 0.0, G_PARAM_READWRITE);
    gimp_procedure_add_double_argument (procedure, "distance",
                                        "Subject _distance (m)",
                                        "Distance to the subject in meters, "
                                        "used for the vignetting",
                                        0.01, 1000.0, 1.0, G_PARAM_READWRITE);
    gimp_procedure_add_boolean_argument (procedure, "correct-distortion",
                                         "Dis_tortion", "Correct the distortion",
                                         TRUE, G_PARAM_READWRITE);
    gimp_procedure_add_boolean_argument (procedure, "correct-tca",
                                         "C_hromatic aberration",
                                         "Correct the transversal chromatic "
                                         "aberration (colour fringes)",
                                         FALSE, G_PARAM_READWRITE);
    gimp_procedure_add_boolean_argument (procedure, "correct-vignetting",
                                         "_Vignetting",
                                         "Correct the vignetting (darker "
                                         "corners)",
                                         FALSE, G_PARAM_READWRITE);
    gimp_procedure_add_boolean_argument (procedure, "as-filter",
                                         "_Keep as an editable filter",
                                         "Add the correction as a filter "
                                         "(lensfun:correct) that stays "
                                         "editable on the layer, instead of "
                                         "changing its pixels",
                                         FALSE, G_PARAM_READWRITE);
    gimp_procedure_add_boolean_argument (procedure, "scale-to-fit",
                                         "_Scale to fit",
                                         "Scale the result so that it has no "
                                         "empty borders",
                                         TRUE, G_PARAM_READWRITE);

    geometry_choice = gimp_choice_new ();
    for (guint i = 0; i < G_N_ELEMENTS (geometries); i++)
        gimp_choice_add (geometry_choice, geometries[i].nick, i,
                         geometries[i].label, NULL);
    gimp_procedure_add_choice_argument (procedure, "target-geometry",
                                        "Target _geometry",
                                        "The projection of the result; "
                                        "\"rectilinear\" turns a fisheye "
                                        "into a normal perspective",
                                        geometry_choice, "rectilinear",
                                        G_PARAM_READWRITE);
    gimp_procedure_add_choice_argument (
        procedure, "interpolation", "_Interpolation",
        "How pixels are resampled",
        gimp_choice_new_with_values ("nearest", INTERPOLATION_NEAREST,
                                     "Nearest neighbour", NULL,
                                     "linear", INTERPOLATION_LINEAR,
                                     "Linear", NULL,
                                     "lanczos", INTERPOLATION_LANCZOS,
                                     "Lanczos", NULL,
                                     NULL),
        "lanczos", G_PARAM_READWRITE);

    return procedure;
}

/* The lensfun database installed next to the plug-in. */
static lfDatabase *
load_database (void)
{
    gchar *dir = g_path_get_dirname (gimp_get_progname ());
    gchar *db_dir = g_build_filename (dir, DB_DIR_NAME, NULL);
    lfDatabase *db = lensdb_load (db_dir);

    g_free (db_dir);
    g_free (dir);
    return db;
}

static std::string
config_string (GimpProcedureConfig *config, const gchar *property)
{
    gchar *value = NULL;
    std::string result;

    g_object_get (config, property, &value, NULL);
    if (value)
        result = value;
    g_free (value);
    return result;
}

static void
read_config (GimpProcedureConfig *config, LensSettings &settings,
             CorrectionOptions &options)
{
    gboolean distortion, tca, vignetting, scale_to_fit;

    settings.maker = config_string (config, "camera-maker");
    settings.camera = config_string (config, "camera-model");
    settings.lens = config_string (config, "lens-model");
    g_object_get (config,
                  "focal-length", &settings.focal,
                  "aperture", &settings.aperture,
                  "distance", &options.distance,
                  "correct-distortion", &distortion,
                  "correct-tca", &tca,
                  "correct-vignetting", &vignetting,
                  "scale-to-fit", &scale_to_fit,
                  NULL);
    options.distortion = distortion;
    options.tca = tca;
    options.vignetting = vignetting;
    options.scale_to_fit = scale_to_fit;
    options.target = geometries[gimp_procedure_config_get_choice_id (
        config, "target-geometry")].type;
    options.interpolation = (Interpolation)
        gimp_procedure_config_get_choice_id (config, "interpolation");
}

/* Linear float pixels of the drawable, so that the vignetting correction
   works on light and any precision is kept. */
static const Babl *
linear_format (GimpDrawable *drawable)
{
    const Babl *space = babl_format_get_space (gimp_drawable_get_format (drawable));
    const char *name;

    if (gimp_drawable_is_rgb (drawable))
        name = gimp_drawable_has_alpha (drawable) ? "RGBA float" : "RGB float";
    else
        name = gimp_drawable_has_alpha (drawable) ? "YA float" : "Y float";
    return babl_format_with_space (name, space);
}

/* Whether the camera and lens are in the database, with a focal length. */
static gboolean
check_settings (const lfDatabase *db, const LensSettings &settings,
                GError **error)
{
    const lfCamera *camera = lensdb_find_camera (db, settings);
    const lfLens *lens = lensdb_find_lens (db, settings);

    if (settings.maker.empty () && settings.camera.empty ())
    {
        g_set_error (error, GIMP_PLUG_IN_ERROR, 0,
                     "No camera is given, and the image has no camera "
                     "information (Exif).");
        return FALSE;
    }
    if (!camera)
    {
        g_set_error (error, GIMP_PLUG_IN_ERROR, 0,
                     "The camera \"%s %s\" is not in the lensfun database.",
                     settings.maker.c_str (), settings.camera.c_str ());
        return FALSE;
    }
    if (!lens)
    {
        g_set_error (error, GIMP_PLUG_IN_ERROR, 0,
                     settings.lens.empty ()
                     ? "No lens is given, and the lens of the image was not "
                       "recognized."
                     : "The lens \"%s\" is not in the lensfun database, or "
                       "does not fit the camera.",
                     settings.lens.c_str ());
        return FALSE;
    }
    if (settings.focal <= 0)
    {
        g_set_error (error, GIMP_PLUG_IN_ERROR, 0,
                     "The focal length is unknown; it is needed for the "
                     "correction.");
        return FALSE;
    }
    return TRUE;
}

/* Adds lensfun:correct to the drawable as a filter with these settings,
   which stays editable. */
static gboolean
add_filter (GimpDrawable *drawable, const LensSettings &settings,
            const CorrectionOptions &options, GError **error)
{
    static const gchar *interpolations[] = { "nearest", "linear", "lanczos" };
    GimpDrawableFilter *filter;
    const gchar *geometry = "rectilinear";

    if (!gegl_has_operation ("lensfun:correct") ||
        !(filter = gimp_drawable_filter_new (drawable, "lensfun:correct",
                                             "Lens Correction (Lensfun)")))
    {
        g_set_error (error, GIMP_PLUG_IN_ERROR, 0,
                     "The lensfun:correct filter is not installed.");
        return FALSE;
    }

    for (guint i = 0; i < G_N_ELEMENTS (geometries); i++)
        if (geometries[i].type == options.target)
            geometry = geometries[i].nick;

    g_object_set (gimp_drawable_filter_get_config (filter),
                  "camera-maker", settings.maker.c_str (),
                  "camera-model", settings.camera.c_str (),
                  "lens-model", settings.lens.c_str (),
                  "focal-length", settings.focal,
                  "aperture", settings.aperture,
                  "distance", options.distance,
                  "correct-distortion", (gboolean) options.distortion,
                  "correct-tca", (gboolean) options.tca,
                  "correct-vignetting", (gboolean) options.vignetting,
                  "scale-to-fit", (gboolean) options.scale_to_fit,
                  "target-geometry", geometry,
                  "interpolation", interpolations[options.interpolation],
                  NULL);
    gimp_drawable_filter_update (filter);
    gimp_drawable_append_filter (drawable, filter);
    return TRUE;
}

static gboolean
correct_drawable (GimpDrawable *drawable, const lfDatabase *db,
                  const LensSettings &settings,
                  const CorrectionOptions &options, GError **error)
{
    const lfCamera *camera = lensdb_find_camera (db, settings);
    const lfLens *lens = lensdb_find_lens (db, settings);
    const Babl *format = linear_format (drawable);
    FloatImage src;
    GeglBuffer *buffer;
    gsize size;
    float *dest;

    if (!check_settings (db, settings, error))
        return FALSE;

    /* the whole layer is the photo, its center the optical center; the
       selection only limits where the result is applied */
    src.width = gimp_drawable_get_width (drawable);
    src.height = gimp_drawable_get_height (drawable);
    src.channels = babl_format_get_n_components (format);
    src.gray = !gimp_drawable_is_rgb (drawable);
    src.alpha = gimp_drawable_has_alpha (drawable);

    if (!g_size_checked_mul (&size, src.width, src.height) ||
        !g_size_checked_mul (&size, size, src.channels) ||
        !(src.pixels = (float *) g_try_malloc (size * sizeof (float))))
    {
        g_set_error (error, GIMP_PLUG_IN_ERROR, 0,
                     "There was not enough memory to complete the operation.");
        return FALSE;
    }
    if (!(dest = (float *) g_try_malloc (size * sizeof (float))))
    {
        g_free (src.pixels);
        g_set_error (error, GIMP_PLUG_IN_ERROR, 0,
                     "There was not enough memory to complete the operation.");
        return FALSE;
    }

    gimp_progress_init ("Lensfun correction...");
    buffer = gimp_drawable_get_buffer (drawable);
    gegl_buffer_get (buffer, GEGL_RECTANGLE (0, 0, src.width, src.height), 1.0,
                     format, src.pixels, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
    g_object_unref (buffer);
    gimp_progress_update (0.2);

    lens_correct (lens, camera->CropFactor, settings, options, src, dest);
    gimp_progress_update (0.9);

    buffer = gimp_drawable_get_shadow_buffer (drawable);
    gegl_buffer_set (buffer, GEGL_RECTANGLE (0, 0, src.width, src.height), 0,
                     format, dest, GEGL_AUTO_ROWSTRIDE);
    g_object_unref (buffer);
    gimp_drawable_merge_shadow (drawable, TRUE);
    gimp_drawable_update (drawable, 0, 0, src.width, src.height);
    gimp_progress_update (1.0);

    g_free (dest);
    g_free (src.pixels);
    return TRUE;
}

/* ---------------------------------------------------------------------
 * dialog
 */

struct DialogData
{
    GimpProcedureConfig *config;
    GimpDrawable *drawable;
    const lfDatabase *db;
    GtkWidget *preview;
    GtkWidget *maker_combo;
    GtkWidget *camera_combo;
    GtkWidget *lens_combo;
    GtkWidget *tca_check;
    GtkWidget *vignetting_check;
    GtkWidget *status_label;
    bool updating;
};

static void
fill_combo (GtkWidget *combo, const std::vector<std::string> &items,
            const std::string &active)
{
    GtkComboBoxText *text = GTK_COMBO_BOX_TEXT (combo);
    int index = -1;

    gtk_combo_box_text_remove_all (text);
    for (size_t i = 0; i < items.size (); i++)
    {
        gtk_combo_box_text_append_text (text, items[i].c_str ());
        if (g_ascii_strcasecmp (items[i].c_str (), active.c_str ()) == 0)
            index = i;
    }
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo), index);
}

static std::string
combo_text (GtkWidget *combo)
{
    gchar *text = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT (combo));
    std::string result = text ? text : "";

    g_free (text);
    return result;
}

/* Fill the combo boxes from the config and show what the lens offers. */
static void
update_lens_widgets (DialogData *d)
{
    LensSettings settings;
    CorrectionOptions options;
    const lfLens *lens;

    d->updating = true;
    read_config (d->config, settings, options);
    fill_combo (d->maker_combo, lensdb_makers (d->db), settings.maker);
    fill_combo (d->camera_combo, lensdb_cameras (d->db, settings.maker),
                settings.camera);
    fill_combo (d->lens_combo,
                lensdb_lenses (d->db, settings.maker, settings.camera),
                settings.lens);
    d->updating = false;

    /* only offer the corrections the lens is calibrated for */
    lens = lensdb_find_lens (d->db, settings);
    gtk_widget_set_sensitive (d->tca_check, lens && lens->CalibTCA &&
                              gimp_drawable_is_rgb (d->drawable));
    gtk_widget_set_sensitive (d->vignetting_check,
                              lens && lens->CalibVignetting);

    if (!lensdb_find_camera (d->db, settings))
        gtk_label_set_text (GTK_LABEL (d->status_label),
                            "Select the camera and the lens.");
    else if (!lens)
        gtk_label_set_text (GTK_LABEL (d->status_label), "Select the lens.");
    else if (settings.focal <= 0)
        gtk_label_set_text (GTK_LABEL (d->status_label),
                            "Enter the focal length.");
    else
        gtk_label_set_text (GTK_LABEL (d->status_label), "");
}

static void
combo_changed (GtkComboBox *combo, DialogData *d)
{
    if (d->updating)
        return;

    if (GTK_WIDGET (combo) == d->maker_combo)
        g_object_set (d->config, "camera-maker", combo_text (d->maker_combo).c_str (),
                      "camera-model", "", "lens-model", "", NULL);
    else if (GTK_WIDGET (combo) == d->camera_combo)
        g_object_set (d->config, "camera-model", combo_text (d->camera_combo).c_str (),
                      "lens-model", "", NULL);
    else
        g_object_set (d->config, "lens-model", combo_text (d->lens_combo).c_str (),
                      NULL);
    update_lens_widgets (d);
}

/* The preview corrects the whole layer scaled to the size of the preview,
   which lensfun treats like the photo itself in smaller size. */
static void
preview_update (GimpPreview *preview, DialogData *d)
{
    LensSettings settings;
    CorrectionOptions options;
    const lfCamera *camera;
    const lfLens *lens;
    gint width, height;
    gdouble scale;
    GeglBuffer *buffer;
    const Babl *linear, *display;
    FloatImage src;
    float *dest;
    guchar *out;

    read_config (d->config, settings, options);
    camera = lensdb_find_camera (d->db, settings);
    lens = lensdb_find_lens (d->db, settings);

    gimp_preview_get_size (preview, &width, &height);
    scale = MIN ((gdouble) width / gimp_drawable_get_width (d->drawable),
                 (gdouble) height / gimp_drawable_get_height (d->drawable));

    linear = linear_format (d->drawable);
    src.width = width;
    src.height = height;
    src.channels = babl_format_get_n_components (linear);
    src.gray = !gimp_drawable_is_rgb (d->drawable);
    src.alpha = gimp_drawable_has_alpha (d->drawable);
    display = babl_format_with_space (
        src.gray ? (src.alpha ? "Y'A u8" : "Y' u8")
                 : (src.alpha ? "R'G'B'A u8" : "R'G'B' u8"),
        linear);

    src.pixels = g_new (float, (gsize) width * height * src.channels);
    dest = g_new (float, (gsize) width * height * src.channels);
    out = g_new (guchar, (gsize) width * height * src.channels);

    buffer = gimp_drawable_get_buffer (d->drawable);
    gegl_buffer_get (buffer, GEGL_RECTANGLE (0, 0, width, height), scale,
                     linear, src.pixels, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_CLAMP);
    g_object_unref (buffer);

    if (camera && lens && settings.focal > 0)
        lens_correct (lens, camera->CropFactor, settings, options, src, dest);
    else
        memcpy (dest, src.pixels, (gsize) width * height * src.channels * sizeof (float));

    babl_process (babl_fish (linear, display), dest, out, (long) width * height);
    gimp_preview_draw_buffer (preview, out, width * src.channels);

    g_free (out);
    g_free (dest);
    g_free (src.pixels);
}

static GtkWidget *
labelled (GtkWidget *grid, int row, const gchar *text, GtkWidget *widget)
{
    GtkWidget *label = gtk_label_new_with_mnemonic (text);

    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget);
    gtk_widget_set_hexpand (widget, TRUE);
    gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), widget, 1, row, 1, 1);
    return widget;
}

/* A box of the dialog for widgets of our own: a box filled with a hidden
   empty label, since a box without any item would get all arguments. */
static GtkWidget *
custom_box (GimpProcedureDialog *dialog, const gchar *id)
{
    gchar *label_id = g_strconcat (id, "-placeholder", NULL);
    GtkWidget *label = gimp_procedure_dialog_get_label (dialog, label_id, "",
                                                        FALSE, FALSE);
    GtkWidget *box = gimp_procedure_dialog_fill_box (dialog, id, label_id,
                                                     NULL);

    gtk_widget_set_no_show_all (label, TRUE);
    gtk_widget_hide (label);
    g_free (label_id);
    return box;
}

static gboolean
lensfun_dialog (GimpProcedure *procedure, GimpProcedureConfig *config,
                GimpDrawable *drawable, const lfDatabase *db,
                const gchar *detected)
{
    GimpProcedureDialog *dialog;
    DialogData d = {};
    GtkWidget *box, *grid, *label;
    gboolean run;

    gimp_ui_init (PLUG_IN_BINARY);

    dialog = GIMP_PROCEDURE_DIALOG (gimp_procedure_dialog_new (
        procedure, config, "Lens Correction (Lensfun " VERSIONSTR ")"));
    d.config = config;
    d.drawable = drawable;
    d.db = db;

    /* preview of the whole layer */
    d.preview = gimp_aspect_preview_new_from_drawable (drawable);
    gtk_widget_set_size_request (d.preview, 360, 240);
    g_signal_connect (d.preview, "invalidated", G_CALLBACK (preview_update), &d);
    g_signal_connect_swapped (config, "notify",
                              G_CALLBACK (gimp_preview_invalidate), d.preview);
    box = custom_box (dialog, "preview-box");
    gtk_box_pack_start (GTK_BOX (box), d.preview, TRUE, TRUE, 0);
    gtk_widget_show (d.preview);

    /* camera and lens */
    grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 4);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 8);
    d.maker_combo = labelled (grid, 0, "Camera _maker:", gtk_combo_box_text_new ());
    d.camera_combo = labelled (grid, 1, "_Camera:", gtk_combo_box_text_new ());
    d.lens_combo = labelled (grid, 2, "_Lens:", gtk_combo_box_text_new ());
    label = gtk_label_new (detected);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
    gimp_label_set_attributes (GTK_LABEL (label), PANGO_ATTR_STYLE,
                               PANGO_STYLE_ITALIC, -1);
    gtk_grid_attach (GTK_GRID (grid), label, 0, 3, 2, 1);
    d.status_label = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (d.status_label), 0.0);
    gimp_label_set_attributes (GTK_LABEL (d.status_label), PANGO_ATTR_WEIGHT,
                               PANGO_WEIGHT_BOLD, -1);
    gtk_grid_attach (GTK_GRID (grid), d.status_label, 0, 4, 2, 1);
    gtk_widget_show_all (grid);
    box = custom_box (dialog, "lens-box");
    gtk_box_pack_start (GTK_BOX (box), grid, FALSE, FALSE, 0);
    for (GtkWidget *combo : { d.maker_combo, d.camera_combo, d.lens_combo })
        g_signal_connect (combo, "changed", G_CALLBACK (combo_changed), &d);

    gimp_procedure_dialog_get_label (dialog, "lens-title", "Camera and lens",
                                     FALSE, FALSE);
    gimp_procedure_dialog_fill_frame (dialog, "lens-frame", "lens-title",
                                      FALSE, "lens-box");

    /* number fields: a scale over the whole range would be too coarse */
    for (const gchar *prop : { "focal-length", "aperture", "distance" })
        gimp_procedure_dialog_get_widget (dialog, prop, GIMP_TYPE_LABEL_SPIN);
    gimp_procedure_dialog_fill_box (dialog, "shot-box", "focal-length",
                                    "aperture", "distance", NULL);
    gimp_procedure_dialog_get_label (dialog, "shot-title", "Shot", FALSE, FALSE);
    gimp_procedure_dialog_fill_frame (dialog, "shot-frame", "shot-title",
                                      FALSE, "shot-box");

    d.tca_check = gimp_procedure_dialog_get_widget (dialog, "correct-tca",
                                                    GTK_TYPE_CHECK_BUTTON);
    d.vignetting_check = gimp_procedure_dialog_get_widget (
        dialog, "correct-vignetting", GTK_TYPE_CHECK_BUTTON);
    gimp_procedure_dialog_fill_box (dialog, "correct-box", "correct-distortion",
                                    "correct-tca", "correct-vignetting",
                                    "scale-to-fit", "target-geometry",
                                    "interpolation", "as-filter", NULL);
    gimp_procedure_dialog_set_sensitive (dialog, "as-filter",
                                         gegl_has_operation ("lensfun:correct"),
                                         NULL, NULL, FALSE);
    gimp_procedure_dialog_get_label (dialog, "correct-title", "Correction",
                                     FALSE, FALSE);
    gimp_procedure_dialog_fill_frame (dialog, "correct-frame", "correct-title",
                                      FALSE, "correct-box");

    gimp_procedure_dialog_fill_box (dialog, "settings-box", "lens-frame",
                                    "shot-frame", "correct-frame", NULL);
    box = gimp_procedure_dialog_fill_box (dialog, "main-box", "preview-box",
                                          "settings-box", NULL);
    gtk_orientable_set_orientation (GTK_ORIENTABLE (box),
                                    GTK_ORIENTATION_HORIZONTAL);
    gtk_box_set_spacing (GTK_BOX (box), 12);
    gimp_procedure_dialog_fill (dialog, "main-box", NULL);

    update_lens_widgets (&d);

    run = gimp_procedure_dialog_run (dialog);
    gtk_widget_destroy (GTK_WIDGET (dialog));
    return run;
}

/* ---------------------------------------------------------------------
 * run
 */

static GimpValueArray *
gimp_lensfun_run (GimpProcedure *procedure, GimpRunMode run_mode,
                  GimpImage *image, GimpDrawable **drawables,
                  GimpProcedureConfig *config, gpointer run_data)
{
    GimpDrawable *drawable;
    lfDatabase *db;
    LensSettings exif, settings;
    CorrectionOptions options;
    gboolean has_exif;
    gchar *detected;
    GError *error = NULL;
    GimpPDBStatusType status = GIMP_PDB_SUCCESS;

    gegl_init (NULL, NULL);

    if (gimp_core_object_array_get_length ((GObject **) drawables) != 1)
    {
        g_set_error (&error, GIMP_PLUG_IN_ERROR, 0,
                     "Procedure '%s' only works with one drawable.",
                     PLUG_IN_PROC);
        return gimp_procedure_new_return_values (procedure,
                                                 GIMP_PDB_CALLING_ERROR, error);
    }
    drawable = drawables[0];

    db = load_database ();
    has_exif = lensdb_settings_from_metadata (db, image, exif);

    if (has_exif)
        detected = g_strdup_printf ("From the image: %s %s, %s, %.1f mm, f/%.1f",
                                    exif.maker.c_str (), exif.camera.c_str (),
                                    exif.lens.empty () ? "lens not recognized"
                                                       : exif.lens.c_str (),
                                    exif.focal, exif.aperture);
    else
        detected = g_strdup ("The image has no camera information (Exif).");

    if (run_mode == GIMP_RUN_INTERACTIVE)
    {
        /* what the image says replaces the last used camera and lens */
        if (has_exif)
            g_object_set (config,
                          "camera-maker", exif.maker.c_str (),
                          "camera-model", exif.camera.c_str (),
                          "lens-model", exif.lens.c_str (),
                          "focal-length", exif.focal,
                          "aperture", exif.aperture,
                          NULL);
        if (!lensfun_dialog (procedure, config, drawable, db, detected))
            status = GIMP_PDB_CANCEL;
    }

    if (status == GIMP_PDB_SUCCESS)
    {
        read_config (config, settings, options);

        /* anything not given comes from the image */
        if (settings.maker.empty ())
            settings.maker = exif.maker;
        if (settings.camera.empty ())
            settings.camera = exif.camera;
        if (settings.lens.empty ())
            settings.lens = exif.lens;
        if (settings.focal <= 0)
            settings.focal = exif.focal;
        if (settings.aperture <= 0)
            settings.aperture = exif.aperture;

        gboolean as_filter;

        g_object_get (config, "as-filter", &as_filter, NULL);
        if (as_filter
            ? !(check_settings (db, settings, &error) &&
                add_filter (drawable, settings, options, &error))
            : !correct_drawable (drawable, db, settings, options, &error))
            status = GIMP_PDB_EXECUTION_ERROR;
        else if (run_mode != GIMP_RUN_NONINTERACTIVE)
            gimp_displays_flush ();
    }

    g_free (detected);
    delete db;
    return gimp_procedure_new_return_values (procedure, status, error);
}
