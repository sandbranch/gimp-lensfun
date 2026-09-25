#!/usr/bin/env python3
# A test image for tests/compare.sh: a straight grid, 1200x800, whose lines
# the distortion correction bends.
import sys

import numpy as np
from PIL import Image

h, w = 800, 1200
img = np.full((h, w, 3), 230, np.uint8)
y, x = np.mgrid[0:h, 0:w]
img[(x % 50 < 2) | (y % 50 < 2)] = [20, 20, 20]
img[:, :, 0][(x - w / 2) ** 2 + (y - h / 2) ** 2 < 40 ** 2] = 200
Image.fromarray(img).save(sys.argv[1])
