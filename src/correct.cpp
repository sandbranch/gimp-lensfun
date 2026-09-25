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

#include <cfloat>
#include <cmath>
#include <cstring>

#include <gegl.h>

#include "LUT.hpp"
#include "correct.h"

/* interpolation parameters */
static const int cLanczosWidth = 2;
static const int cLanczosTableRes = 256;
static LUT<float> LanczosLUT (cLanczosWidth * 2 * cLanczosTableRes + 1);

static float
Lanczos (float x)
{
    if ((x < FLT_MIN) && (x > -FLT_MIN))
        return 1.0f;

    if ((x >= cLanczosWidth) || (x <= (-1) * cLanczosWidth))
        return 0.0f;

    float xpi = x * static_cast<float> (M_PI);
    return (cLanczosWidth * sin (xpi) * sin (xpi / cLanczosWidth)) / (xpi * xpi);
}

static void
InitLanczos ()
{
    static bool done = false;

    if (done)
        return;
    for (int i = -cLanczosWidth * cLanczosTableRes;
         i < cLanczosWidth * cLanczosTableRes; i++)
        LanczosLUT[i + cLanczosWidth * cLanczosTableRes] =
            Lanczos (static_cast<float> (i) / static_cast<float> (cLanczosTableRes));
    done = true;
}

/* A sample is outside of the image when it is more than half a pixel
   beyond the outer pixel centers; inside, pixels beyond the border are
   taken from the border, so that the edges do not turn dark. */
static inline bool
outside (const FloatImage &img, float x, float y)
{
    return x < -0.5f || y < -0.5f ||
           x > img.width - 0.5f || y > img.height - 0.5f;
}

static inline float
pixel (const FloatImage &img, int x, int y, int chan)
{
    x = x < 0 ? 0 : (x >= img.width ? img.width - 1 : x);
    y = y < 0 ? 0 : (y >= img.height ? img.height - 1 : y);
    return img.pixels[((size_t) y * img.width + x) * img.channels + chan];
}

static inline float
InterpolateLanczos (const FloatImage &img, float xpos, float ypos, int chan)
{
    int xl = int (floorf (xpos));
    int yl = int (floorf (ypos));
    float y = 0.0f;
    float norm = 0.0f;

    /* convolve with lanczos kernel */
    for (int i = xl - cLanczosWidth + 1; i <= xl + cLanczosWidth; i++)
        for (int j = yl - cLanczosWidth + 1; j <= yl + cLanczosWidth; j++)
        {
            float L =
                LanczosLUT[(xpos - static_cast<float> (i)) * cLanczosTableRes +
                           static_cast<float> (cLanczosWidth * cLanczosTableRes)] *
                LanczosLUT[(ypos - static_cast<float> (j)) * cLanczosTableRes +
                           static_cast<float> (cLanczosWidth * cLanczosTableRes)];
            y += pixel (img, i, j, chan) * L;
            norm += L;
        }
    return norm != 0.0f ? y / norm : 0.0f;
}

static inline float
InterpolateLinear (const FloatImage &img, float xpos, float ypos, int chan)
{
    int xl = int (floorf (xpos));
    int yu = int (floorf (ypos));
    float fx = xpos - xl;
    float fy = ypos - yu;
    float top = (1 - fx) * pixel (img, xl, yu, chan) + fx * pixel (img, xl + 1, yu, chan);
    float bottom = (1 - fx) * pixel (img, xl, yu + 1, chan) + fx * pixel (img, xl + 1, yu + 1, chan);

    return (1 - fy) * top + fy * bottom;
}

static inline float
InterpolateNearest (const FloatImage &img, float xpos, float ypos, int chan)
{
    return pixel (img, int (floorf (xpos + 0.5f)), int (floorf (ypos + 0.5f)), chan);
}

static inline float
interpolate (const FloatImage &img, Interpolation interpolation,
             float x, float y, int chan)
{
    switch (interpolation)
    {
    case INTERPOLATION_NEAREST:
        return InterpolateNearest (img, x, y, chan);
    case INTERPOLATION_LINEAR:
        return InterpolateLinear (img, x, y, chan);
    default:
        return InterpolateLanczos (img, x, y, chan);
    }
}

/* the roles of the channels for lensfun's colour correction */
static int
component_roles (const FloatImage &img)
{
    if (img.gray)
        return img.alpha ? LF_CR_2 (INTENSITY, UNKNOWN) : LF_CR_1 (INTENSITY);
    return img.alpha ? LF_CR_4 (RED, GREEN, BLUE, UNKNOWN)
                     : LF_CR_3 (RED, GREEN, BLUE);
}

struct PassData
{
    const lfModifier *mod;
    FloatImage *src;
    float *dest;
    Interpolation interpolation;
    bool geometry;
};

static void
colour_rows (gsize offset, gsize count, gpointer user_data)
{
    PassData *d = static_cast<PassData *> (user_data);
    FloatImage &img = *d->src;
    size_t row = (size_t) img.width * img.channels;

    d->mod->ApplyColorModification (img.pixels + offset * row, 0, offset,
                                    img.width, count, component_roles (img),
                                    row * sizeof (float));
}

static void
geometry_rows (gsize offset, gsize count, gpointer user_data)
{
    PassData *d = static_cast<PassData *> (user_data);
    const FloatImage &img = *d->src;
    int colours = img.gray ? 1 : 3;
    float *coords = g_new (float, (size_t) img.width * 2 * 3);

    for (gsize y = offset; y < offset + count; y++)
    {
        float *out = d->dest + y * (size_t) img.width * img.channels;

        /* the position in the source of each colour of each pixel */
        if (img.gray)
            d->mod->ApplyGeometryDistortion (0, y, img.width, 1, coords);
        else
            d->mod->ApplySubpixelGeometryDistortion (0, y, img.width, 1, coords);

        for (int x = 0; x < img.width; x++, out += img.channels)
        {
            const float *pos = coords + (size_t) x * 2 * (img.gray ? 1 : 3);
            /* alpha follows the green channel, gray has a single position */
            const float *mid = img.gray ? pos : pos + 2;

            for (int c = 0; c < colours; c++)
            {
                const float *p = img.gray ? pos : pos + 2 * c;
                out[c] = outside (img, p[0], p[1]) ? 0.0f
                         : interpolate (img, d->interpolation, p[0], p[1], c);
            }
            if (img.alpha)
                out[colours] = outside (img, mid[0], mid[1]) ? 0.0f
                               : interpolate (img, d->interpolation, mid[0],
                                              mid[1], colours);
        }
    }
    g_free (coords);
}

int
lens_correct (const lfLens *lens, float crop, const LensSettings &settings,
              const CorrectionOptions &options, FloatImage &src, float *dest)
{
    int flags = 0;
    int applied;
    lfLensType target;
    PassData data;
    size_t bytes = (size_t) src.width * src.height * src.channels * sizeof (float);

    if (options.distortion)
        flags |= LF_MODIFY_DISTORTION;
    if (options.tca && !src.gray)
        flags |= LF_MODIFY_TCA;
    if (options.vignetting)
        flags |= LF_MODIFY_VIGNETTING;
    target = options.target == LF_UNKNOWN ? lens->Type : options.target;
    if (target != lens->Type)
        flags |= LF_MODIFY_GEOMETRY;
    if (options.scale_to_fit)
        flags |= LF_MODIFY_SCALE;

    lfModifier *mod = new lfModifier (lens, crop, src.width, src.height);
    applied = mod->Initialize (lens, LF_PF_F32, settings.focal,
                               settings.aperture, options.distance,
                               options.scale_to_fit ? 0.0f : 1.0f, target,
                               flags, false);

    data.mod = mod;
    data.src = &src;
    data.dest = dest;
    data.interpolation = options.interpolation;

    /* first the colours of all pixels, then the geometry, which reads
       the neighbours of every pixel */
    if (applied & LF_MODIFY_VIGNETTING)
        gegl_parallel_distribute_range (src.height, 8.0, colour_rows, &data);

    if (applied & (LF_MODIFY_DISTORTION | LF_MODIFY_TCA | LF_MODIFY_GEOMETRY |
                   LF_MODIFY_SCALE))
    {
        InitLanczos ();
        gegl_parallel_distribute_range (src.height, 2.0, geometry_rows, &data);
    }
    else
        memcpy (dest, src.pixels, bytes);

    delete mod;
    return applied;
}
