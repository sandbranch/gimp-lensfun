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

#ifndef CORRECT_H
#define CORRECT_H

#include <lensfun.h>

#include "lensdb.h"

enum Interpolation
{
    INTERPOLATION_NEAREST,
    INTERPOLATION_LINEAR,
    INTERPOLATION_LANCZOS
};

struct CorrectionOptions
{
    bool distortion = true;
    bool tca = false;
    bool vignetting = false;
    bool scale_to_fit = true;
    double distance = 1.0;
    /* the geometry to convert to, LF_UNKNOWN to keep the one of the lens */
    lfLensType target = LF_RECTILINEAR;
    Interpolation interpolation = INTERPOLATION_LANCZOS;
};

/* An image as linear float pixels: gray or RGB, with or without alpha as
   the last channel. */
struct FloatImage
{
    float *pixels;
    int width;
    int height;
    int channels;
    bool gray;
    bool alpha;
};

/* Correct src into dest, which has the same size and layout. The whole of
   src is the photo: its center is the optical center. The vignetting
   correction is applied to src in place, before the geometry is corrected.
   Returns the corrections that were applied, as LF_MODIFY_* flags. */
int lens_correct (const lfLens *lens, float crop, const LensSettings &settings,
                  const CorrectionOptions &options, FloatImage &src,
                  float *dest);

#endif /* CORRECT_H */
