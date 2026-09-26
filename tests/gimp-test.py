# Runs inside GIMP without a window (tests/run.sh), with the plug-in and
# lensfun:correct of the test build: checks the plug-in, the filter, and
# that they agree. Prints PASS or FAIL for each case, then a summary.
import array
import os
import subprocess
import traceback

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, Gio

OUT = os.environ['LF_TEST_OUT']
BUILD = os.environ['LF_TEST_INSTALL']

LENS = 'Nikkor AF-S 18-105mm f/3.5-5.6G DX ED VR'
NIKON = dict(maker='Nikon', camera='D90', lens=LENS, focal=18.0)

pdb = Gimp.get_pdb()
proc = pdb.lookup_procedure('plug-in-lensfun')
results = []


def case(name):
    def wrap(fn):
        try:
            fn()
            results.append(True)
            print('PASS', name)
        except Exception:
            results.append(False)
            print('FAIL', name)
            for line in traceback.format_exc().strip().splitlines():
                print('   ', line)
        return fn
    return wrap


# ---------------------------------------------------------------------
# images

PRECISIONS = {
    'u8': Gimp.Precision.U8_NON_LINEAR,
    'u16': Gimp.Precision.U16_LINEAR,
    'float': Gimp.Precision.FLOAT_LINEAR,
}


def layer_type(base, alpha):
    if base == Gimp.ImageBaseType.RGB:
        return Gimp.ImageType.RGBA_IMAGE if alpha else Gimp.ImageType.RGB_IMAGE
    return Gimp.ImageType.GRAYA_IMAGE if alpha else Gimp.ImageType.GRAY_IMAGE


def fmt(layer):
    gray = not layer.is_rgb()
    return "Y'A float" if gray else "R'G'B'A float"


def grid_value(x, y):
    return 0.1 if (x % 10 < 2 or y % 10 < 2) else 0.8


def make_image(w=300, h=200, base=Gimp.ImageBaseType.RGB, precision='u8',
               alpha=False, offset=None, image_size=None):
    iw, ih = image_size or (w, h)
    image = Gimp.Image.new_with_precision(iw, ih, base, PRECISIONS[precision])
    layer = Gimp.Layer.new(image, 'photo', w, h, layer_type(base, alpha),
                           100, Gimp.LayerMode.NORMAL)
    image.insert_layer(layer, None, 0)
    if offset:
        layer.set_offsets(*offset)
    set_pixels(layer, lambda x, y: grid_value(x, y))
    return image, layer


def set_pixels(layer, value):
    w, h = layer.get_width(), layer.get_height()
    gray = not layer.is_rgb()
    px = array.array('f')
    for y in range(h):
        for x in range(w):
            v = value(x, y)
            # a colour, so that the chromatic aberration shows
            px.extend([v, 1.0] if gray else [v, v * 0.9, v * 0.7, 1.0])
    buf = layer.get_buffer()
    buf.set(Gegl.Rectangle.new(0, 0, w, h), fmt(layer), px.tobytes())
    buf.flush()
    layer.update(0, 0, w, h)


def pixels(layer):
    w, h = layer.get_width(), layer.get_height()
    data = layer.get_buffer().get(Gegl.Rectangle.new(0, 0, w, h), 1.0,
                                  fmt(layer), Gegl.AbyssPolicy.NONE)
    return array.array('f', bytes(data))


def max_diff(a, b):
    assert len(a) == len(b), (len(a), len(b))
    return max((abs(p - q) for p, q in zip(a, b)), default=0.0)


def changed(a, b, tol=0.05):
    return sum(1 for p, q in zip(a, b) if abs(p - q) > tol)


# ---------------------------------------------------------------------
# running the plug-in and the filter

def run_plugin(image, drawables=None, settings=NIKON, **args):
    config = proc.create_config()
    config.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    config.set_property('image', image)
    config.set_core_object_array('drawables', drawables or image.get_layers())
    config.set_property('camera-maker', settings.get('maker', ''))
    config.set_property('camera-model', settings.get('camera', ''))
    config.set_property('lens-model', settings.get('lens', ''))
    config.set_property('focal-length', settings.get('focal', 0.0))
    for key, value in args.items():
        config.set_property(key.replace('_', '-'), value)
    return proc.run(config).index(0)


FILTER_ARGS = {'correct_tca': 'correct-tca',
               'correct_vignetting': 'correct-vignetting',
               'correct_distortion': 'correct-distortion',
               'scale_to_fit': 'scale-to-fit',
               'target_geometry': 'target-geometry',
               'interpolation': 'interpolation',
               'aperture': 'aperture'}


def add_filter(layer, settings=NIKON, **args):
    f = Gimp.DrawableFilter.new(layer, 'lensfun:correct', 'Lens Correction')
    c = f.get_config()
    c.set_property('camera-maker', settings.get('maker', ''))
    c.set_property('camera-model', settings.get('camera', ''))
    c.set_property('lens-model', settings.get('lens', ''))
    c.set_property('focal-length', settings.get('focal', 0.0))
    for key, value in args.items():
        c.set_property(FILTER_ARGS[key], value)
    f.update()
    layer.append_filter(f)
    return f


def filtered(layer, **kw):
    add_filter(layer, **kw)
    layer.merge_filters()
    return pixels(layer)


def plugin_vs_filter(**make_args):
    def both(**args):
        image, layer = make_image(**make_args)
        assert run_plugin(image, **args) == Gimp.PDBStatusType.SUCCESS
        a = pixels(layer)
        image2, layer2 = make_image(**make_args)
        b = filtered(layer2, **args)
        orig = pixels(make_image(**make_args)[1])
        return a, b, orig
    return both


SUCCESS = Gimp.PDBStatusType.SUCCESS
ERROR = Gimp.PDBStatusType.EXECUTION_ERROR


# ---------------------------------------------------------------------
# the cases

@case('the test build is the one loaded, not an installed copy')
def _():
    assert proc is not None, 'plug-in-lensfun is not registered'
    assert Gimp.directory().startswith(OUT), Gimp.directory()
    # GEGL operations are loaded by GIMP itself, the parent of this script
    image, layer = make_image(20, 20)
    filtered(layer)
    with open('/proc/%d/maps' % os.getppid()) as f:
        maps = [l.split()[-1] for l in f if 'lensfun-correct' in l]
    assert maps and all(m.startswith(BUILD) for m in maps), maps


@case('plug-in: unknown camera is an error and changes nothing')
def _():
    image, layer = make_image()
    before = pixels(layer)
    assert run_plugin(image, settings=dict(NIKON, camera='D9999')) == ERROR
    assert pixels(layer) == before


@case('plug-in: unknown lens is an error and changes nothing')
def _():
    image, layer = make_image()
    before = pixels(layer)
    assert run_plugin(image, settings=dict(NIKON, lens='No Such Lens')) == ERROR
    assert run_plugin(image, settings=dict(NIKON, lens='')) == ERROR
    assert pixels(layer) == before


@case('plug-in: no camera and no Exif is an error')
def _():
    image, layer = make_image()
    before = pixels(layer)
    assert run_plugin(image, settings={}) == ERROR
    assert pixels(layer) == before


@case('filter: unknown or empty settings pass the photo through')
def _():
    for settings in (dict(NIKON, camera='D9999'), dict(NIKON, lens='x'), {}):
        image, layer = make_image()
        before = pixels(layer)
        assert filtered(layer, settings=settings) == before


@case('plug-in: a known lens corrects, the center stays')
def _():
    image, layer = make_image()
    before = pixels(layer)
    assert run_plugin(image) == SUCCESS
    after = pixels(layer)
    assert changed(before, after) > len(before) // 20
    c = (100 * 300 + 150) * 4
    assert max_diff(before[c:c + 4], after[c:c + 4]) < 0.03


@case('plug-in and filter agree: 8-bit RGB, defaults')
def _():
    a, b, orig = plugin_vs_filter()()
    assert changed(a, orig) > 0
    assert max_diff(a, b) <= 1.5 / 255, max_diff(a, b)


@case('plug-in and filter agree: all corrections, fisheye, nearest')
def _():
    both = plugin_vs_filter()
    a, b, orig = both(correct_tca=True, correct_vignetting=True,
                      target_geometry='fisheye', interpolation='nearest',
                      aperture=4.0)
    assert changed(a, orig) > 0
    assert max_diff(a, b) <= 1.5 / 255, max_diff(a, b)


@case('plug-in and filter agree: 16-bit and float, with alpha')
def _():
    for precision in ('u16', 'float'):
        for alpha in (False, True):
            a, b, orig = plugin_vs_filter(precision=precision, alpha=alpha)(
                correct_tca=True)
            assert changed(a, orig) > 0
            assert max_diff(a, b) < 1e-3, (precision, alpha, max_diff(a, b))


@case('plug-in and filter agree: gray, with chromatic aberration on')
def _():
    for precision, alpha in (('u8', False), ('float', True)):
        a, b, orig = plugin_vs_filter(base=Gimp.ImageBaseType.GRAY,
                                      precision=precision, alpha=alpha)(
            correct_tca=True)
        assert changed(a, orig) > 0
        assert max_diff(a, b) <= 1.5 / 255, (precision, alpha, max_diff(a, b))


@case('high precision is kept: float result is not rounded to 8 bits')
def _():
    image, layer = make_image(precision='float')
    assert run_plugin(image) == SUCCESS
    assert image.get_precision() == Gimp.Precision.FLOAT_LINEAR
    values = set(pixels(layer)[0::4])
    off_grid = [v for v in values if abs(v * 255 - round(v * 255)) > 0.01]
    assert len(off_grid) > 10, len(off_grid)


@case('plug-in and filter: tiny images')
def _():
    for w, h in ((1, 1), (2, 3), (5, 1), (1, 7)):
        image, layer = make_image(w, h, alpha=True)
        assert run_plugin(image, correct_tca=True, correct_vignetting=True) \
            == SUCCESS, (w, h)
        image, layer = make_image(w, h, alpha=True)
        filtered(layer, correct_tca=True, correct_vignetting=True)


@case('plug-in as filter: adds lensfun:correct with the same settings')
def _():
    args = dict(correct_tca=True, target_geometry='fisheye',
                interpolation='nearest', scale_to_fit=False)
    image, layer = make_image()
    assert run_plugin(image, as_filter=True, **args) == SUCCESS
    filters = layer.get_filters()
    assert [f.get_operation_name() for f in filters] == ['lensfun:correct']
    c = filters[0].get_config()
    got = {k: c.get_property(FILTER_ARGS[k]) for k in args}
    assert got == args, got
    assert c.get_property('camera-model') == 'D90'
    assert abs(c.get_property('focal-length') - 18.0) < 1e-9
    layer.merge_filters()
    a = pixels(layer)
    image2, layer2 = make_image()
    assert run_plugin(image2, **args) == SUCCESS
    assert max_diff(a, pixels(layer2)) <= 1.5 / 255


@case('plug-in as filter: unknown lens is an error, no filter added')
def _():
    image, layer = make_image()
    assert run_plugin(image, as_filter=True,
                      settings=dict(NIKON, lens='x')) == ERROR
    assert layer.get_filters() == []


@case('layer offsets: the layer is the photo')
def _():
    image, layer = make_image(160, 100, offset=(37, 21), image_size=(300, 200))
    assert run_plugin(image) == SUCCESS
    image2, layer2 = make_image(160, 100)
    assert run_plugin(image2) == SUCCESS
    assert max_diff(pixels(layer), pixels(layer2)) < 1e-6
    image3, layer3 = make_image(160, 100, offset=(37, 21), image_size=(300, 200))
    assert max_diff(filtered(layer3), pixels(layer2)) <= 1.5 / 255


@case('selection: only the selected part changes')
def _():
    image, layer = make_image()
    before = pixels(layer)
    image.select_rectangle(Gimp.ChannelOps.REPLACE, 0, 0, 100, 200)
    assert run_plugin(image) == SUCCESS
    after = pixels(layer)
    outside = [i for i in range(len(before)) if (i // 4) % 300 >= 101]
    inside = [i for i in range(len(before)) if (i // 4) % 300 < 99]
    assert all(before[i] == after[i] for i in outside)
    assert changed([before[i] for i in inside], [after[i] for i in inside]) > 0


@case('plug-in: more than one drawable is a calling error')
def _():
    image, layer = make_image()
    other = Gimp.Layer.new(image, 'other', 300, 200, Gimp.ImageType.RGB_IMAGE,
                           100, Gimp.LayerMode.NORMAL)
    image.insert_layer(other, None, 0)
    assert run_plugin(image, drawables=[layer, other]) == \
        Gimp.PDBStatusType.CALLING_ERROR


@case('plug-in: a layer mask is corrected as gray')
def _():
    image, layer = make_image(alpha=True)
    mask = layer.create_mask(Gimp.AddMaskType.WHITE)
    layer.add_mask(mask)
    assert run_plugin(image, drawables=[mask]) == SUCCESS


@case('plug-in: a layer group is an error; as a filter it works')
def _():
    image, layer = make_image()
    group = Gimp.GroupLayer.new(image, 'group')
    image.insert_layer(group, None, 0)
    image.reorder_item(layer, group, 0)
    assert run_plugin(image, drawables=[group]) == ERROR
    assert run_plugin(image, drawables=[group], as_filter=True) == SUCCESS
    assert [f.get_operation_name() for f in group.get_filters()] == \
        ['lensfun:correct']


def with_exif(tags, **make_args):
    image, layer = make_image(**make_args)
    md = image.get_metadata() or Gimp.Metadata.new()
    for tag, value in tags.items():
        md.try_set_tag_string(tag, value)
    image.set_metadata(md)
    return image, layer


NIKON_EXIF = {'Exif.Image.Make': 'NIKON CORPORATION',
              'Exif.Image.Model': 'NIKON D90',
              'Exif.Photo.LensModel': 'AF-S DX VR Zoom-Nikkor 18-105mm f/3.5-5.6G ED',
              'Exif.Photo.FocalLength': '18/1',
              'Exif.Photo.FNumber': '35/10'}


@case('Exif: camera, lens and focal length from the image')
def _():
    image, layer = with_exif(NIKON_EXIF)
    assert run_plugin(image, settings={}) == SUCCESS
    image2, layer2 = make_image()
    assert run_plugin(image2) == SUCCESS
    assert max_diff(pixels(layer), pixels(layer2)) < 1e-6


@case('Exif: no lens name is an error, not some lens of the mount')
def _():
    tags = dict(NIKON_EXIF)
    del tags['Exif.Photo.LensModel']
    image, layer = with_exif(tags)
    before = pixels(layer)
    assert run_plugin(image, settings={}) == ERROR
    assert pixels(layer) == before


@case('Exif: focal length 0/0 or 0 counts as unknown')
def _():
    for focal in ('0/0', '0/1'):
        image, layer = with_exif(dict(NIKON_EXIF, **{'Exif.Photo.FocalLength': focal}))
        before = pixels(layer)
        assert run_plugin(image, settings={}) == ERROR, focal
        assert pixels(layer) == before
        # given by the caller, it is used
        assert run_plugin(image, settings=dict(focal=18.0)) == SUCCESS


@case('Exif: non-ASCII maker, no model')
def _():
    image, layer = with_exif({'Exif.Image.Make': 'N\u00edk\u00f8n \u20ac'})
    assert run_plugin(image, settings={}) == ERROR


def gegl_cli(src, dest, **props):
    args = ['gegl', '-i', src, '-o', dest, '--', 'lensfun:correct']
    args += ['%s=%s' % (k.replace('_', '-'), v) for k, v in props.items()]
    subprocess.run(args, check=True, timeout=120)
    image = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(dest))
    return pixels(image.get_layers()[0])


@case('gegl command line: same result as the plug-in, unknown passes')
def _():
    grid = os.path.join(OUT, 'grid.png')
    image, layer = make_image(alpha=True)
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image,
                   Gio.File.new_for_path(grid), None)
    before = pixels(layer)
    cli = gegl_cli(grid, os.path.join(OUT, 'cli.png'), camera_maker='Nikon',
                   camera_model='D90', lens_model=LENS, focal_length=18)
    same = gegl_cli(grid, os.path.join(OUT, 'cli-unknown.png'),
                    camera_maker='Nikon', camera_model='D90', lens_model='x')
    assert run_plugin(image) == SUCCESS
    assert max_diff(cli, pixels(layer)) <= 1.5 / 255, max_diff(cli, pixels(layer))
    assert max_diff(same, before) <= 0.5 / 255


print('%s: %d of %d cases failed' % ('FAIL' if not all(results) else 'PASS',
                                    results.count(False), len(results)))
