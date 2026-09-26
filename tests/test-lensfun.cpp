/*
 * Unit tests of the lens database functions (src/lensdb.cpp) and of the
 * correction (src/correct.cpp), without GIMP: run by `meson test`.
 *
 *   test-lensfun <database folder>
 *
 * Prints PASS or FAIL for each case and exits non-zero if any failed, or
 * 77 (skipped) without a database.
 *
 * This file is part of GimpLensfun.
 *
 * GimpLensfun is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version
 * 3 of the License, or (at your option) any later version.
 */

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <glib/gstdio.h>
#include <gegl.h>

#include "correct.h"
#include "lensdb.h"

static int failures = 0;
static bool case_ok;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond))                                                    \
        {                                                               \
            printf ("  %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            case_ok = false;                                            \
        }                                                               \
    } while (0)

static void
run (const char *name, void (*test) ())
{
    case_ok = true;
    test ();
    printf ("%s %s\n", case_ok ? "PASS" : "FAIL", name);
    if (!case_ok)
        failures++;
}

static lfDatabase *db;

static const char *nikon_lens = "Nikkor AF-S 18-105mm f/3.5-5.6G DX ED VR";

static bool
contains (const std::vector<std::string> &v, const std::string &s)
{
    return std::find (v.begin (), v.end (), s) != v.end ();
}

static LensSettings
nikon (double focal = 18.0, double aperture = 3.5)
{
    LensSettings s;

    s.maker = "Nikon";
    s.camera = "D90";
    s.lens = nikon_lens;
    s.focal = focal;
    s.aperture = aperture;
    return s;
}

/* ---------------------------------------------------------------------
 * images
 */

struct Image
{
    std::vector<float> px;
    FloatImage img;

    Image (int w, int h, int channels, bool gray, bool alpha)
        : px ((size_t) w * h * channels)
    {
        img.pixels = px.data ();
        img.width = w;
        img.height = h;
        img.channels = channels;
        img.gray = gray;
        img.alpha = alpha;
    }

    float &at (int x, int y, int c)
    {
        return px[((size_t) y * img.width + x) * img.channels + c];
    }
};

static Image
grid (int w, int h, int channels = 3, bool gray = false, bool alpha = false)
{
    Image im (w, h, channels, gray, alpha);
    int colours = channels - (alpha ? 1 : 0);

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            float v = (x % 10 < 2 || y % 10 < 2) ? 0.1f : 0.8f;
            for (int c = 0; c < colours; c++)
                im.at (x, y, c) = v;
            if (alpha)
                im.at (x, y, colours) = 1.0f;
        }
    return im;
}

static Image
flat (int w, int h, float v, int channels = 3, bool gray = false,
      bool alpha = false)
{
    Image im (w, h, channels, gray, alpha);

    std::fill (im.px.begin (), im.px.end (), v);
    if (alpha)
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                im.at (x, y, channels - 1) = 1.0f;
    return im;
}

/* Corrects a copy of src (the correction may change src in place). */
static std::vector<float>
correct (const Image &src, const LensSettings &s, const CorrectionOptions &o,
         int *applied = NULL, float crop = 0)
{
    Image in = src;
    std::vector<float> out (in.px.size (), -1.0f);
    const lfLens *lens = lensdb_find_lens (db, s);
    const lfCamera *camera = lensdb_find_camera (db, s);

    in.img.pixels = in.px.data ();
    int a = lens_correct (lens, crop > 0 ? crop : (camera ? camera->CropFactor : 1.0f),
                          s, o, in.img, out.data ());
    if (applied)
        *applied = a;
    return out;
}

static bool
all_finite (const std::vector<float> &v)
{
    for (float f : v)
        if (!std::isfinite (f))
            return false;
    return true;
}

static float
max_diff (const std::vector<float> &a, const std::vector<float> &b)
{
    float d = 0;

    for (size_t i = 0; i < a.size () && i < b.size (); i++)
        d = std::max (d, std::fabs (a[i] - b[i]));
    return a.size () == b.size () ? d : INFINITY;
}

static CorrectionOptions
nothing ()
{
    CorrectionOptions o;

    o.distortion = false;
    o.tca = false;
    o.vignetting = false;
    o.scale_to_fit = false;
    o.target = LF_UNKNOWN;
    return o;
}

/* ---------------------------------------------------------------------
 * the database
 */

static void
test_load_missing ()
{
    lfDatabase *d = lensdb_load ("/nonexistent/lensfun-db");

    CHECK (d != NULL);
    lensdb_makers (d);
    delete d;
}

static void
test_load_empty ()
{
    gchar *dir = g_dir_make_tmp ("lensfun-test-XXXXXX", NULL);
    lfDatabase *d = lensdb_load (dir);

    CHECK (d != NULL);
    delete d;
    g_rmdir (dir);
    g_free (dir);
}

static void
test_lists ()
{
    CHECK (contains (lensdb_makers (db), "Nikon"));
    CHECK (contains (lensdb_cameras (db, "Nikon"), "D90"));
    /* the default variant of the maker, in other case */
    CHECK (contains (lensdb_cameras (db, "nikon corporation"), "D90"));
    CHECK (contains (lensdb_lenses (db, "Nikon", "D90"), nikon_lens));
    CHECK (lensdb_cameras (db, "No Such Maker").empty ());
    CHECK (lensdb_lenses (db, "Nikon", "No Such Camera").empty ());
}

static void
test_find ()
{
    LensSettings s = nikon ();
    const lfCamera *c = lensdb_find_camera (db, s);

    CHECK (c != NULL);
    CHECK (lensdb_find_lens (db, s) != NULL);

    /* the English and the default variants name the same camera */
    s.maker = "Canon";
    s.camera = "EOS 5D Mark II";
    c = lensdb_find_camera (db, s);
    CHECK (c != NULL);
    s.camera = "Canon EOS 5D Mark II";
    CHECK (lensdb_find_camera (db, s) == c);

    /* nothing, or something the database does not know */
    CHECK (lensdb_find_camera (db, LensSettings ()) == NULL);
    s = nikon ();
    s.camera = "";
    CHECK (lensdb_find_camera (db, s) == NULL);
    s.camera = "D9000000";
    CHECK (lensdb_find_camera (db, s) == NULL);
    CHECK (lensdb_find_lens (db, s) == NULL);

    s = nikon ();
    s.lens = "";
    CHECK (lensdb_find_lens (db, s) == NULL);
    s.lens = "No Such Lens 1-2mm";
    CHECK (lensdb_find_lens (db, s) == NULL);
    /* a lens of another mount does not fit */
    s.lens = "Canon EF 50mm f/1.8 II";
    CHECK (lensdb_find_lens (db, s) == NULL);
}

static void
test_exif ()
{
    LensSettings s;

    /* how a Nikon writes it */
    lensdb_settings_from_exif (db, "NIKON CORPORATION", "NIKON D90",
                               "AF-S DX VR Zoom-Nikkor 18-105mm f/3.5-5.6G ED",
                               s);
    CHECK (s.maker == "Nikon");
    CHECK (s.camera == "D90");
    CHECK (s.lens == nikon_lens);
    CHECK (s.focal == 0 && s.aperture == 0);

    /* no lens name on a camera for many lenses: no lens, not any lens */
    lensdb_settings_from_exif (db, "NIKON CORPORATION", "NIKON D90", "", s);
    CHECK (s.camera == "D90");
    CHECK (s.lens.empty ());
    lensdb_settings_from_exif (db, "NIKON CORPORATION", "NIKON D90",
                               "Something unknown 1234", s);
    CHECK (s.lens.empty ());

    /* a camera with a fixed lens needs no lens name */
    lensdb_settings_from_exif (db, "Canon", "Canon PowerShot G12", "", s);
    CHECK (s.camera == "PowerShot G12");
    CHECK (!s.lens.empty ());
    CHECK (lensdb_find_lens (db, s) != NULL);

    /* no model: not the first camera of the maker */
    lensdb_settings_from_exif (db, "NIKON CORPORATION", "", "", s);
    CHECK (s.maker == "NIKON CORPORATION");
    CHECK (s.camera.empty ());
    CHECK (s.lens.empty ());

    /* unknown cameras keep their names, for the messages */
    lensdb_settings_from_exif (db, "\xc3\x91\xc3\xad" "k\xc3\xb8n \xff", "X 1",
                               "\xe2\x80\x94", s);
    CHECK (s.maker == "\xc3\x91\xc3\xad" "k\xc3\xb8n \xff");
    CHECK (s.camera == "X 1");
    CHECK (s.lens.empty ());

    lensdb_settings_from_exif (db, "", "NIKON D90", nikon_lens, s);
    CHECK (s.maker.empty () && s.camera.empty () && s.lens.empty ());
}

/* ---------------------------------------------------------------------
 * the correction
 */

static void
test_identity ()
{
    Image im = grid (64, 48);
    int applied = -1;
    std::vector<float> out = correct (im, nikon (), nothing (), &applied);

    CHECK (applied == 0);
    CHECK (out == im.px);
}

static void
test_no_lens ()
{
    Image im = grid (32, 24);
    std::vector<float> out (im.px.size ());
    CorrectionOptions o;

    CHECK (lens_correct (NULL, 1.5f, nikon (), o, im.img, out.data ()) == 0);
    CHECK (out == im.px);
}

static void
test_empty ()
{
    CorrectionOptions o;
    FloatImage img = { NULL, 0, 0, 3, false, false };
    const lfLens *lens = lensdb_find_lens (db, nikon ());

    CHECK (lens_correct (lens, 1.5f, nikon (), o, img, NULL) == 0);
    img.width = 10;
    CHECK (lens_correct (lens, 1.5f, nikon (), o, img, NULL) == 0);
}

static void
test_distortion ()
{
    Image im = grid (300, 200);
    int applied = 0;
    CorrectionOptions o;
    std::vector<float> out = correct (im, nikon (), o, &applied);
    size_t changed = 0;

    CHECK (applied & LF_MODIFY_DISTORTION);
    CHECK (applied & LF_MODIFY_SCALE);
    CHECK (all_finite (out));
    for (size_t i = 0; i < out.size (); i++)
        if (std::fabs (out[i] - im.px[i]) > 0.1f)
            changed++;
    /* the grid lines move */
    CHECK (changed > out.size () / 20);
    /* the center stays where it is */
    for (int c = 0; c < 3; c++)
        CHECK (std::fabs (out[(100 * 300 + 150) * 3 + c] - im.at (150, 100, c)) < 0.02f);

    /* without scale to fit, the result is not scaled */
    o.scale_to_fit = false;
    std::vector<float> unscaled = correct (im, nikon (), o, &applied);
    CHECK (!(applied & LF_MODIFY_SCALE));
    CHECK (max_diff (out, unscaled) > 0.1f);

    /* parts that come from beyond the photo are empty (transparent):
       a fisheye of a normal lens, not scaled, has empty corners */
    Image rgba = grid (300, 200, 4, false, true);
    o.target = LF_FISHEYE;
    out = correct (rgba, nikon (), o);
    CHECK (out[0] == 0.0f && out[3] == 0.0f);
    CHECK (out[(100 * 300 + 150) * 4 + 3] == 1.0f);
}

/* A flat image stays flat where the photo covers the result, with each
   interpolation, also at the borders: the kernel is normalized and pixels
   beyond the border are taken from it. */
static void
test_flat ()
{
    for (Interpolation interp : { INTERPOLATION_NEAREST, INTERPOLATION_LINEAR,
                                  INTERPOLATION_LANCZOS })
    {
        Image im = flat (97, 61, 0.5f);
        CorrectionOptions o;

        o.tca = true;
        o.interpolation = interp;
        std::vector<float> out = correct (im, nikon (), o);
        float d = 0;
        for (float f : out)
            d = std::max (d, std::fabs (f - 0.5f));
        CHECK (d < 1e-4f);
    }
}

/* Nearest neighbour only takes values of the source. */
static void
test_nearest ()
{
    Image im = grid (120, 80);
    CorrectionOptions o;

    o.interpolation = INTERPOLATION_NEAREST;
    std::vector<float> out = correct (im, nikon (), o);
    bool ok = true;
    for (float f : out)
        ok = ok && (f == 0.1f || f == 0.8f);
    CHECK (ok);
}

/* Small images, down to a pixel, with every correction, geometry and
   interpolation. */
static void
test_tiny ()
{
    static const int sizes[][2] = { { 1, 1 }, { 1, 2 }, { 2, 1 }, { 2, 2 },
                                    { 3, 3 }, { 1, 100 }, { 100, 1 } };

    for (const auto &size : sizes)
        for (Interpolation interp : { INTERPOLATION_NEAREST,
                                      INTERPOLATION_LINEAR,
                                      INTERPOLATION_LANCZOS })
            for (bool scale : { false, true })
            {
                Image im = grid (size[0], size[1], 4, false, true);
                CorrectionOptions o;

                o.tca = o.vignetting = true;
                o.scale_to_fit = scale;
                o.interpolation = interp;
                o.target = LF_FISHEYE;
                std::vector<float> out = correct (im, nikon (), o);
                CHECK (all_finite (out));
                o.target = LF_RECTILINEAR;
                out = correct (im, nikon (), o);
                CHECK (all_finite (out));
            }
}

/* Every target geometry, from a rectilinear and from a fisheye lens. */
static void
test_geometries ()
{
    static const lfLensType types[] = {
        LF_RECTILINEAR, LF_FISHEYE, LF_PANORAMIC, LF_EQUIRECTANGULAR,
        LF_FISHEYE_ORTHOGRAPHIC, LF_FISHEYE_STEREOGRAPHIC,
        LF_FISHEYE_EQUISOLID, LF_FISHEYE_THOBY
    };
    LensSettings fisheye = nikon (10.5);
    const lfCamera *camera = lensdb_find_camera (db, fisheye);
    const lfLens **lenses = db->FindLenses (camera, NULL, NULL);

    fisheye.lens.clear ();
    for (int i = 0; lenses && lenses[i]; i++)
        if (lenses[i]->Type == LF_FISHEYE && lenses[i]->CalibDistortion)
        {
            fisheye.lens = lensdb_mlstr (lenses[i]->Model);
            fisheye.focal = lenses[i]->MinFocal;
            break;
        }
    lf_free (lenses);
    CHECK (!fisheye.lens.empty ());

    for (const LensSettings &s : { nikon (), fisheye })
        for (lfLensType type : types)
            for (bool scale : { false, true })
            {
                Image im = grid (90, 60);
                CorrectionOptions o;
                int applied = 0;

                o.target = type;
                o.scale_to_fit = scale;
                std::vector<float> out = correct (im, s, o, &applied);
                CHECK (all_finite (out));
                if (type != lensdb_find_lens (db, s)->Type)
                    CHECK (applied & LF_MODIFY_GEOMETRY);
            }
}

static void
test_vignetting ()
{
    Image im = flat (150, 100, 0.25f);
    CorrectionOptions o = nothing ();
    int applied = 0;

    o.vignetting = true;
    std::vector<float> out = correct (im, nikon (18, 3.5), o, &applied);
    CHECK (applied == LF_MODIFY_VIGNETTING);
    /* the corners are brightened, the center hardly */
    CHECK (out[0] > 0.3f);
    CHECK (std::fabs (out[(50 * 150 + 75) * 3] - 0.25f) < 0.01f);

    /* on gray with alpha: the alpha stays */
    Image ga = flat (150, 100, 0.25f, 2, true, true);
    out = correct (ga, nikon (18, 3.5), o, &applied);
    CHECK (out[0] > 0.3f);
    CHECK (out[1] == 1.0f);
}

/* The chromatic aberration is not corrected on gray images. */
static void
test_gray ()
{
    Image im = grid (80, 60, 1, true, false);
    CorrectionOptions o;
    int applied = 0;

    o.tca = true;
    std::vector<float> out = correct (im, nikon (), o, &applied);
    CHECK (!(applied & LF_MODIFY_TCA));
    CHECK (applied & LF_MODIFY_DISTORTION);
    CHECK (all_finite (out));

    Image rgb = grid (80, 60);
    std::vector<float> colour = correct (rgb, nikon (), o, &applied);
    CHECK (applied & LF_MODIFY_TCA);

    /* without it, gray and colour move the pixels alike */
    o.tca = false;
    out = correct (im, nikon (), o);
    colour = correct (rgb, nikon (), o);
    float d = 0;
    for (size_t i = 0; i < out.size (); i++)
        d = std::max (d, std::fabs (out[i] - colour[i * 3 + 1]));
    CHECK (d < 1e-5f);
}

/* Alpha follows the colours, and is opaque where the photo covers the
   result. */
static void
test_alpha ()
{
    Image im = grid (80, 60, 4, false, true);
    CorrectionOptions o;

    o.tca = true;
    std::vector<float> out = correct (im, nikon (), o);
    float d = 0;
    for (size_t i = 3; i < out.size (); i += 4)
        d = std::max (d, std::fabs (out[i] - 1.0f));
    CHECK (d < 1e-4f);
}

/* Outside the calibrated range, lensfun takes the nearest calibration. */
static void
test_focal_range ()
{
    Image im = grid (120, 80);
    CorrectionOptions o;
    std::vector<float> at18 = correct (im, nikon (18), o);
    std::vector<float> at105 = correct (im, nikon (105), o);

    CHECK (max_diff (correct (im, nikon (1), o), at18) < 1e-5f);
    CHECK (max_diff (correct (im, nikon (5000), o), at105) < 1e-5f);
    CHECK (max_diff (at18, at105) > 0.1f);
}

/* The crop factor of the camera matters: the calibration was made with
   another one. */
static void
test_crop ()
{
    Image im = grid (120, 80);
    CorrectionOptions o;

    CHECK (max_diff (correct (im, nikon (), o, NULL, 1.523f),
                     correct (im, nikon (), o, NULL, 3.0f)) > 0.1f);
}

/* The positions of whole rows are as exact as those of single pixels,
   for which lensfun uses neither SSE nor a running sum. */
static void
test_positions ()
{
    const lfLens *lens = lensdb_find_lens (db, nikon ());
    const int w = 6000, h = 4000;

    for (bool gray : { false, true })
        for (int flags : { LF_MODIFY_DISTORTION | LF_MODIFY_SCALE,
                           LF_MODIFY_DISTORTION | LF_MODIFY_TCA | LF_MODIFY_SCALE })
        {
            lfModifier mod (lens, 1.523f, w, h);
            int per_pixel = gray ? 2 : 6;
            std::vector<float> row ((size_t) w * per_pixel);
            float one[6], d = 0;

            mod.Initialize (lens, LF_PF_F32, 18, 3.5, 1, 0.0f, LF_RECTILINEAR,
                            flags, false);
            for (int y : { 0, 1234, h - 1 })
            {
                lens_positions (&mod, 0, y, w, gray, row.data ());
                for (int x = 0; x < w; x += 7)
                {
                    if (gray)
                        mod.ApplyGeometryDistortion (x, y, 1, 1, one);
                    else
                        mod.ApplySubpixelGeometryDistortion (x, y, 1, 1, one);
                    for (int k = 0; k < per_pixel; k++)
                        d = std::max (d, std::fabs (row[(size_t) x * per_pixel + k] - one[k]));
                }
            }
            CHECK (d < 0.01f);
        }
}

/* The work spread over threads gives the same result as one thread; the
   vignetting differs by rounding, as the SSE code of lensfun depends on
   where a part of the rows starts. */
static void
test_threads ()
{
    Image im = grid (257, 131, 4, false, true);
    CorrectionOptions o;
    int threads;

    o.tca = o.vignetting = true;
    g_object_get (gegl_config (), "threads", &threads, NULL);
    g_object_set (gegl_config (), "threads", 1, NULL);
    std::vector<float> one = correct (im, nikon (), o);
    g_object_set (gegl_config (), "threads", 8, NULL);
    std::vector<float> many = correct (im, nikon (), o);
    g_object_set (gegl_config (), "threads", threads, NULL);
    CHECK (max_diff (one, many) < 1e-5f);
    o.vignetting = false;
    g_object_set (gegl_config (), "threads", 1, NULL);
    one = correct (im, nikon (), o);
    g_object_set (gegl_config (), "threads", 8, NULL);
    many = correct (im, nikon (), o);
    g_object_set (gegl_config (), "threads", threads, NULL);
    CHECK (one == many);
}

int
main (int argc, char **argv)
{
    if (argc < 2 || !g_file_test (argv[1], G_FILE_TEST_IS_DIR))
    {
        printf ("SKIP no lensfun database given\n");
        return 77;
    }

    /* the English names of the database */
    setlocale (LC_ALL, "C");
    gegl_init (NULL, NULL);

    db = lensdb_load (argv[1]);
    if (!db || lensdb_makers (db).empty ())
    {
        printf ("FAIL loading the database from %s\n", argv[1]);
        return 1;
    }

    run ("lensdb: missing database folder", test_load_missing);
    run ("lensdb: empty database folder", test_load_empty);
    run ("lensdb: makers, cameras and lenses", test_lists);
    run ("lensdb: find camera and lens", test_find);
    run ("lensdb: settings from Exif strings", test_exif);
    run ("correct: nothing to do copies", test_identity);
    run ("correct: no lens copies", test_no_lens);
    run ("correct: empty image", test_empty);
    run ("correct: distortion", test_distortion);
    run ("correct: flat image stays flat", test_flat);
    run ("correct: nearest neighbour", test_nearest);
    run ("correct: tiny images", test_tiny);
    run ("correct: target geometries", test_geometries);
    run ("correct: vignetting", test_vignetting);
    run ("correct: gray", test_gray);
    run ("correct: alpha", test_alpha);
    run ("correct: focal length outside the range", test_focal_range);
    run ("correct: crop factor", test_crop);
    run ("correct: positions as exact as single pixels", test_positions);
    run ("correct: threads", test_threads);

    delete db;
    gegl_exit ();

    printf ("%s: %d failed\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
