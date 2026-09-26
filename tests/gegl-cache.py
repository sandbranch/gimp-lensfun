# Runs lensfun:correct in GEGL alone (tests/run.sh): after a small change
# to the input, every part of the result must be computed again, since a
# pixel of the result can come from anywhere in the photo; GEGL's default
# is to redo only the changed rectangle, and to serve the rest from the
# operation's cache.
import struct

import gi
gi.require_version('Gegl', '0.4')
from gi.repository import Gegl

Gegl.init(None)
W, H = 600, 400
FMT = 'RGBA float'
LENS = dict(camera_maker='Nikon', camera_model='D90',
            lens_model='Nikkor AF-S 18-105mm f/3.5-5.6G DX ED VR',
            focal_length=18.0)


def rect(x, y, w, h):
    return Gegl.Rectangle.new(x, y, w, h)


def graph(buf):
    g = Gegl.Node()
    src = g.create_child('gegl:buffer-source')
    src.set_property('buffer', buf)
    op = g.create_child('lensfun:correct')
    for k, v in LENS.items():
        op.set_property(k, v)
    # a node after it, so that the operation's own cache is used
    nop = g.create_child('gegl:nop')
    src.connect_to('output', op, 'input')
    op.connect_to('output', nop, 'input')
    return g, nop


def render(node, r):
    out = Gegl.Buffer.new(FMT, r.x, r.y, r.width, r.height)
    node.blit_buffer(out, r, 0, Gegl.AbyssPolicy.NONE)
    data = out.get(r, 1.0, FMT, Gegl.AbyssPolicy.NONE)
    return struct.unpack('%df' % (r.width * r.height * 4), bytes(data))


px = []
for y in range(H):
    for x in range(W):
        v = 0.1 if (x % 10 < 2 or y % 10 < 2) else 0.9
        px += [v, v, v, 1.0]
buf = Gegl.Buffer.new(FMT, 0, 0, W, H)
buf.set(rect(0, 0, W, H), FMT, struct.pack('%df' % len(px), *px))

print('PASS' if Gegl.has_operation('lensfun:correct') else 'FAIL',
      'GEGL: lensfun:correct is there')

g, node = graph(buf)
render(node, rect(0, 0, W, H))
# a small red square, which the correction moves
px0, py0, n = 30, 20, 6
buf.set(rect(px0, py0, n, n), FMT, struct.pack('%df' % (n * n * 4),
                                               *([1.0, 0.0, 0.0, 1.0] * n * n)))
g2, fresh = graph(buf)
stale = 0
for ty in range(0, 80, 4):
    for tx in range(0, 80, 4):
        if not (tx + 4 <= px0 or tx >= px0 + n or ty + 4 <= py0 or ty >= py0 + n):
            continue
        r = rect(tx, ty, 4, 4)
        a, b = render(node, r), render(fresh, r)
        if max(abs(p - q) for p, q in zip(a, b)) > 1e-6:
            stale += 1
print('PASS' if stale == 0 else 'FAIL',
      'GEGL: the result is redone after a change (%d stale parts)' % stale)
