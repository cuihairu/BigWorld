# libpython - Windows CMake carrier for the vendored BigWorld CPython 3.13.
#
# Why this file exists
# --------------------
# The root superproject adds one subdirectory per entry of
# BWConfiguration_server.cmake's BW_LIBRARY_PROJECTS table (root
# CMakeLists.txt:206) and names each target after the first column, so this
# directory must define a target called exactly "libpython"
# (BWConfiguration_server.cmake:96). lib/pyscript/CMakeLists.txt:97 and
# lib/script/CMakeLists.txt:54 both link it by that name, which is the whole
# contract this file has to satisfy.
#
# CPython ships no CMake build system: upstream provides PCbuild (MSVC) and
# autotools (Unix). The Linux track already has a carrier for the same problem
# in cmake/samples/python, which wraps ./configure + make in an ExternalProject
# and therefore never invokes CMake against this directory. This file is the
# Windows counterpart and is deliberately NOT a translation of PCbuild into
# CMake: it drives upstream's own entry point, PCbuild\build.bat, so there is
# one authority for the interpreter's ~250 translation units.
#
# bwhooks.c is compiled by pythoncore, not here
# ---------------------------------------------
# lib/pyscript/script.cpp needs BW_Py_malloc/free/calloc/realloc,
# BW_Py_getHooks/setHooks and BW_Py_memoryTrackingIgnoreBegin/End, all of
# which live in the vendored Python/bwhooks.c. Linux builds it into the
# interpreter archive (build/make/third_party_python.mak notes the archive
# carries Python/bwhooks.o and pulls it in with -u,BW_Py_memoryTrackingIgnore*).
# On Windows the hooks must be resolvable by BOTH kinds of consumer:
# lib/pyscript/script.cpp in the embedder, and PCbuild extension modules
# (_hashlib.pyd et al) that link only python313.lib. This target used to
# compile bwhooks.c itself, which satisfied pyscript but left every extension
# module with LNK2001 on BW_Py_memoryTrackingIgnoreBegin/End (first root
# build probe) - and compiling it in both places would create two independent
# s_hooks states in one process. PCbuild/Directory.Build.targets now injects
# bwhooks.c into pythoncore (with BW_PY_BUILDING_PYTHONCORE, which makes
# Include/bwhooks.h dllexport the entry points), so python313.dll is the
# single carrier, python313.lib carries the symbols, and this target is a
# pure link-through to that import library.
#
# Configurations
# --------------
# pythoncore is built once, in Release, and every superproject configuration
# (the root adds "Hybrid") resolves to that one archive. PCbuild has no Hybrid
# configuration and build.bat only knows Release/Debug; this matches the
# documented Release-instead-of-Hybrid deviation in .github/workflows/
# ci-windows.yml.
#
# Build-time requirements (not exercised by a configure-only run)
# ---------------------------------------------------------------
#   * git and network on PATH. build.bat's default IncludeExternals=true runs
#     PCbuild\get_externals.bat, which clones externals/ (openssl, zlib,
#     libffi, bzip2, tcltk, sqlite, xz). This is mandatory: pythoncore compiles
#     $(zlibDir)\*.c and only disables the externals step when
#     $(zlibDir)\zlib.h already exists (pythoncore.vcxproj:85). Do NOT pass
#     -E here.
#   * MSBuild, found by build.bat through PCbuild\find_msbuild.bat (PATH, then
#     vswhere, then the registry) - no developer-shell setup is required.
#
# Configure-verified only: this carrier is proven against the root
# superproject's configure step by the root-configure-probe job in
# .github/workflows/ci-windows.yml, which never runs the ExternalProject build
# step. The build-time items above are carried as comments, not as verified
# behaviour - the first real build of this target is what settles them.

if(NOT MSVC)
	# Only the server target adds this directory, and only on Windows. The
	# guard keeps any other configure path (a Linux superproject run, a
	# sample that happens to reach this directory) from pulling in PCbuild.
	return()
endif()

include(ExternalProject)

set(BW_PYTHON_SRC "${CMAKE_CURRENT_LIST_DIR}")
set(BW_PYTHON_PCBUILD "${BW_PYTHON_SRC}/PCbuild")

# Interpreter version, read from the vendored headers rather than hardcoded so
# a CPython bump does not silently retarget a stale archive.
file(READ "${BW_PYTHON_SRC}/Include/patchlevel.h" _bw_py_patchlevel)
string(REGEX MATCH "#define[ \t]+PY_MAJOR_VERSION[ \t]+([0-9]+)"
	_bw_py_match "${_bw_py_patchlevel}")
if(NOT _bw_py_match)
	message(FATAL_ERROR
		"libpython: could not read PY_MAJOR_VERSION from "
		"${BW_PYTHON_SRC}/Include/patchlevel.h")
endif()
set(BW_PYTHON_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define[ \t]+PY_MINOR_VERSION[ \t]+([0-9]+)"
	_bw_py_match "${_bw_py_patchlevel}")
if(NOT _bw_py_match)
	message(FATAL_ERROR
		"libpython: could not read PY_MINOR_VERSION from "
		"${BW_PYTHON_SRC}/Include/patchlevel.h")
endif()
set(BW_PYTHON_MINOR "${CMAKE_MATCH_1}")
set(BW_PYTHON_VERSION "${BW_PYTHON_MAJOR}.${BW_PYTHON_MINOR}")

# python.props maps the x64 platform to ArchName=amd64, and pythoncore's
# OutDir is $(PySourcePath)PCbuild\$(ArchName)\ (python.props:43,49).
if(BW_ARCH_64 OR CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(_bw_py_arch "amd64")
else()
	set(_bw_py_arch "win32")
endif()
set(BW_PYTHONCORE_ARCHIVE
	"${BW_PYTHON_PCBUILD}/${_bw_py_arch}/python${BW_PYTHON_VERSION}.lib")

# pyproject.props:14 - pythoncore.vcxproj regenerates pyconfig.h into its own
# IntDir, which is the only copy in the tree. Python.h includes it, so it has
# to be on this target's include path as well.
set(BW_PYTHON_PYCONFIG_DIR
	"${BW_PYTHON_PCBUILD}/obj/${BW_PYTHON_MAJOR}${BW_PYTHON_MINOR}${_bw_py_arch}_Release/pythoncore")

# The interpreter itself: upstream's build entry point, no hand-rolled
# MSBuild invocation. CONFIGURE_COMMAND is empty because autotools does not
# exist on Windows and PCbuild is self-configuring.
ExternalProject_Add(bwpython313
	SOURCE_DIR "${BW_PYTHON_SRC}"
	BINARY_DIR "${CMAKE_BINARY_DIR}/bwpython313"
	CONFIGURE_COMMAND ""
	BUILD_COMMAND cmd.exe /c
		"${BW_PYTHON_PCBUILD}/build.bat" -c Release -p x64
	BUILD_BYPRODUCTS "${BW_PYTHONCORE_ARCHIVE}"
	INSTALL_COMMAND ""
	TEST_COMMAND ""
	LOG_BUILD TRUE
)

# The target the root superproject names (BWConfiguration_server.cmake:96) and
# the one lib/pyscript and lib/script link. With the BW hooks now compiled into
# pythoncore (PCbuild/Directory.Build.targets, see the header comment) the whole
# Windows contract is python313.lib, so libpython IS that imported archive
# rather than a wrapper target of its own - a sourceless STATIC target is
# rejected at generate time anyway ("No SOURCES given to target: libpython",
# run 38762101165), and a wrapper would only exist to hold an object that no
# longer exists.
#
# No INTERFACE_INCLUDE_DIRECTORIES here: IMPORTED targets are checked at
# generate time for existing paths, and the pyconfig.h directory below only
# comes into being when pythoncore builds. Consumers get the two Python source
# include dirs from BWStandardProject.cmake:5-6, which every module includes.
add_library(libpython STATIC IMPORTED GLOBAL)
add_dependencies(libpython bwpython313)
set_target_properties(libpython PROPERTIES
	IMPORTED_LOCATION "${BW_PYTHONCORE_ARCHIVE}")

message(STATUS "libpython: pythoncore ${BW_PYTHONCORE_ARCHIVE}, "
	"bwhooks.c carried by python313.dll via Directory.Build.targets")

# No /INCLUDE: or -u equivalent is needed here, unlike the Linux carrier: there
# the hooks live inside the interpreter archive and nothing in it references
# them, so build/make/third_party_python.mak has to force them in. Here
# lib/pyscript/script.cpp calls the BW_Py_* entry points directly, and
# python313.lib satisfies every such reference from python313.dll - the one
# copy of s_hooks in the process.
