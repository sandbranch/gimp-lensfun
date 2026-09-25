# Runs inside GIMP (tests/compare.sh): corrects the grid with the plug-in,
# and with lensfun:correct as a non-destructive filter, with the same
# settings, and saves both.
import os

import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio

out = os.environ['LF_TEST_OUT']
settings = dict(maker='Nikon', camera='D90', lens='Nikkor AF-S 18-105mm f/3.5-5.6G DX ED VR', focal=18.0)

def load():
    return Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(os.path.join(out, 'grid.png')))

image = load()
proc = Gimp.get_pdb().lookup_procedure('plug-in-lensfun')
config = proc.create_config()
config.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
config.set_property('image', image)
config.set_core_object_array('drawables', image.get_layers())
config.set_property('camera-maker', settings['maker'])
config.set_property('camera-model', settings['camera'])
config.set_property('lens-model', settings['lens'])
config.set_property('focal-length', settings['focal'])
result = proc.run(config)
print('plug-in:', result.index(0))
Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(os.path.join(out, 'plugin.png')), None)

image = load()
layer = image.get_layers()[0]
f = Gimp.DrawableFilter.new(layer, 'lensfun:correct', 'Lens Correction')
c = f.get_config()
c.set_property('camera-maker', settings['maker'])
c.set_property('camera-model', settings['camera'])
c.set_property('lens-model', settings['lens'])
c.set_property('focal-length', settings['focal'])
f.update()
layer.append_filter(f)
print('filters on the layer:', [x.get_name() for x in layer.get_filters()])
Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(os.path.join(out, 'filter.png')), None)

# the plug-in again, adding the filter instead of changing the pixels
image = load()
config = proc.create_config()
config.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
config.set_property('image', image)
config.set_core_object_array('drawables', image.get_layers())
config.set_property('camera-maker', settings['maker'])
config.set_property('camera-model', settings['camera'])
config.set_property('lens-model', settings['lens'])
config.set_property('focal-length', settings['focal'])
config.set_property('as-filter', True)
result = proc.run(config)
layer = image.get_layers()[0]
print('plug-in as filter:', result.index(0), [x.get_name() for x in layer.get_filters()])
Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(os.path.join(out, 'plugin-filter.png')), None)
