"""	bw_site.py

	Engine replacement for the standard library site module.

	Script::init() sets Py_NoSiteFlag = 1 before Py_Initialize(), so CPython
	skips importing site. This module is imported from scripts/common instead
	(see the bw_site import in pyscript/script.cpp); if it is missing or
	raises, Script::init() fails with "Unable to import bw_site".

	sys.path is built by the engine itself from the PyImportPaths passed to
	Script::init() plus the scripts/common, scripts/common/Lib and
	scripts/server_common/lib-dynload-<platform> resource paths, so unlike the
	stdlib site module this file performs no path manipulation of its own.
"""

#
# bw_site.py
#
