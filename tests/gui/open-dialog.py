# Runs inside GIMP on a Broadway display (tests/gui/start.sh): opens the
# test grid and the plug-in's dialog.
import os

import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio

image = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(os.environ['LF_IMAGE']))
Gimp.Display.new(image)
proc = Gimp.get_pdb().lookup_procedure('plug-in-lensfun')
config = proc.create_config()
config.set_property('run-mode', Gimp.RunMode.INTERACTIVE)
config.set_property('image', image)
config.set_core_object_array('drawables', image.get_layers())
proc.run(config)
