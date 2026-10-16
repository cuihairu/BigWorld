# Standalone CMake sample for the vendored BigWorld CPython 3.13 build.
#
# NOT part of the Windows CMake superproject. This is the Linux CMake-track
# counterpart of the make chain's third_party/python build
# (build/make/third_party_python.mak, target libbwpython3.13) and the
# foundation the entitydef sample needs: lib/pyscript/script.cpp calls the
# BWHooks symbols (BW_Py_malloc/free/calloc/realloc, BW_Py_getHooks/setHooks,
# BW_Py_memoryTrackingIgnoreBegin/End) that live in the vendored interpreter's
# Python/bwhooks.c, and no stock CPython build provides them. vcpkg's python3
# port is therefore not an option for anything linking pyscript.
#
# CPython has no CMake build system — upstream ships PCbuild (Windows) and
# autotools (Unix), and third_party_python.mak drives that configure+make. This
# sample wraps the same configure+make in an ExternalProject instead of
# re-listing ~250 .c files, so there is exactly one authority for the configure
# options: the options below are copied verbatim from third_party_python.mak's
# pythonConfigureOpts.
#
# The sed post-processing in third_party_python.mak (TERMNAM undefs in
# pyconfig.h, LINKFORSHARED whole-archive OpenSSL + -u,BW_Py_memoryTrackingIgnore*
# on the interpreter link) is replicated here because it gates the shared
# extension modules: CPython 3.12+ runs Tools/build/check_extension_modules.py
# as part of `make all`, which dlopens every module from the fresh interpreter
# and renames any that fail to import to <name>_failed<EXT_SUFFIX>. _hashlib
# calls BW_Py_memoryTrackingIgnoreBegin/End (Python/bwhooks.o inside
# libpython3.13.a) and _ssl/_hashlib bind OpenSSL from the interpreter image;
# without the sed the check would rename them away.
#
# CMake-track consumers link the interpreter statically (they link with
# -rdynamic/--export-dynamic, e.g. entitydef_test) and DO dlopen the extension
# modules: pyscript's Script::init resolves its stdlib through BWResource, and
# plain stdlib imports (pickle -> _pickle, struct, ...) fail without
# scripts/server_common/lib-dynload-<platform>. So besides libpython3.13.a the
# sample builds python.exe + sharedmods (`make all`) and installs the make
# chain's python_library res tree (stdlib sources, bw_site.py, lib-dynload)
# into <build>/python-res for consumers to put on BWResource's path.
#
# The interpreter still resolves its stdlib at startup relative to the
# configure-time --prefix (<prefix>/lib/python3.13) — populated below with the
# source Lib/ tree, which is what `make install` libinstall copies minus the
# optional bytecode compile; this only serves the initial encodings import
# before pyscript replaces sys.path.
#
# libpython3.13.a contains Python/bwhooks.o (ar t), so linking it is all
# pyscript needs — pyscript references the BW_Py_* symbols, which pulls
# bwhooks.o out of the archive. The -u flags below just pin that dependency
# explicitly, mirroring BW_PYTHON_LDFLAGS from third_party_python_symbols.mak.
#
# One-command build + symbol check: scripts/build-python-cmake.sh
# Manual configure:
#   cmake -S programming/bigworld/cmake/samples/python \
#     -B programming/bigworld/build/cmake-python \
#     -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
#   cmake --build programming/bigworld/build/cmake-python -j
#
# Keep the configure options in sync with third_party_python.mak.

cmake_minimum_required(VERSION 3.20)

# Included as a subdirectory by cmake/samples/entitydef; only a top-level
# invocation needs project() and the vcpkg manifest defaults.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	if(NOT DEFINED VCPKG_MANIFEST_DIR)
		set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../../..")
	endif()
	if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
		set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/../../../build/vcpkg-triplets")
	endif()
	if(NOT DEFINED VCPKG_TARGET_TRIPLET AND UNIX AND NOT APPLE)
		set(VCPKG_TARGET_TRIPLET "x64-linux-bw")
	endif()
	project(bigworld_python_sample LANGUAGES C)
endif()

find_package(Threads REQUIRED)
include(ExternalProject)
find_program(BW_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

# --- vendored sources and build dirs ----------------------------------------
set(BW_PYTHON_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/python")
set(BW_PYTHON_BUILD_DIR "${CMAKE_BINARY_DIR}/bwpython313")
set(BW_PYTHON_ARCHIVE "${BW_PYTHON_BUILD_DIR}/libpython3.13.a")

if(NOT EXISTS "${BW_PYTHON_SRC_DIR}/configure")
	message(FATAL_ERROR "vendored CPython sources missing: ${BW_PYTHON_SRC_DIR}")
endif()

# --- configure options: verbatim from third_party_python.mak -----------------
# vcpkg installs openssl + bzip2 into the manifest install dir, where the make
# chain's VCPKG_INSTALLED points. configure reads their headers for the _ssl /
# _bz2 feature checks; libpython3.13.a ends up with no OpenSSL undefined
# symbols, so the flags only make the checks honest.
set(BW_PYTHON_VCPKG_INSTALLED
	"${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")

# CMAKE_BUILD_PARALLEL_LEVEL is a build-time variable, so -j can only be
# captured from the configure-time value; default to a sane fan-out.
if(NOT DEFINED CMAKE_BUILD_PARALLEL_LEVEL OR CMAKE_BUILD_PARALLEL_LEVEL STREQUAL "")
	set(CMAKE_BUILD_PARALLEL_LEVEL 4)
endif()

# --- sed post-processing: verbatim from third_party_python.mak ---------------
# TERMNAM: functions marked dangerous in man pages are undef'd so CPython uses
# its own implementations (third_party_python.mak does this with one regex
# s/#define (HAVE_T(E)*MPNAM(_R)?) 1/...; written out per macro here because
# ExternalProject embeds the command in a generated cmake string where the
# backslash backreference is not representable).
set(BW_PYTHON_SED_TERMNAM
	"sed;-r;-i;-e;s,#define HAVE_TEMPNAM 1,/* #undef HAVE_TEMPNAM */,;-e;s,#define HAVE_TMPNAM 1,/* #undef HAVE_TMPNAM */,;-e;s,#define HAVE_TMPNAM_R 1,/* #undef HAVE_TMPNAM_R */,;${BW_PYTHON_BUILD_DIR}/pyconfig.h")
# LINKFORSHARED: the interpreter links the same static vcpkg OpenSSL that the
# BW binaries link (whole-archive) and force-loads bwhooks.o via the -u flags
# so check_extension_modules.py does not rename _ssl/_hashlib away (see header
# note). The make chain names them -lbwssl/-lbwcrypto; the cmake track links
# the same vcpkg archives under their stock names.
set(BW_PYTHON_LINKFORSHARED_EXTRA
	"-L${BW_PYTHON_VCPKG_INSTALLED}/lib -Wl,--whole-archive -lssl -lcrypto -Wl,--no-whole-archive -Wl,-u,BW_Py_memoryTrackingIgnoreBegin -Wl,-u,BW_Py_memoryTrackingIgnoreEnd")
set(BW_PYTHON_SED_LINKFORSHARED
	"sed;-r;-i;-e;s|^LINKFORSHARED=.*|LINKFORSHARED= -Xlinker -export-dynamic ${BW_PYTHON_LINKFORSHARED_EXTRA}|;${BW_PYTHON_BUILD_DIR}/Makefile")

# --- runtime res tree installed for embedding consumers -----------------------
# Mirrors third_party_python.mak's python_library target (game/res/bigworld/
# scripts layout); consumers put ${BW_PYTHON_RES_TREE}/game/res/bigworld on
# their BWResource path (see lib/pyscript/script.cpp Script::init and
# lib/cstdmf/bw_platform_info.cpp DEBIAN_EQUIVALENT_PLATFORM for the el7 name).
set(BW_PYTHON_RES_TREE "${CMAKE_CURRENT_BINARY_DIR}/python-res" CACHE INTERNAL
	"BigWorld runtime Python res tree (game/res/bigworld/scripts layout)")

ExternalProject_Add(bwpython313
	SOURCE_DIR "${BW_PYTHON_SRC_DIR}"
	BINARY_DIR "${BW_PYTHON_BUILD_DIR}"
	CONFIGURE_COMMAND "${BW_PYTHON_SRC_DIR}/configure"
		--with-openssl=${BW_PYTHON_VCPKG_INSTALLED}
		--with-bz2=${BW_PYTHON_VCPKG_INSTALLED}
		--with-suffix=.exe
		--prefix=${CMAKE_CURRENT_BINARY_DIR}/python3.13
		--disable-test-modules
		CPPFLAGS=-I${BW_PYTHON_VCPKG_INSTALLED}/include
	COMMAND ${BW_PYTHON_SED_TERMNAM}
	COMMAND ${BW_PYTHON_SED_LINKFORSHARED}
	# make all = libpython3.13.a + python.exe + sharedmods (the make chain's
	# sourcePythonSharedModSentinel rule builds the same `all`)
	BUILD_COMMAND "${BW_MAKE_EXECUTABLE}" -j${CMAKE_BUILD_PARALLEL_LEVEL}
		all EXTRA_CFLAGS=-m64\ -fPIC
	# 1) stdlib under the configure prefix for the startup encodings import
	#    (see header note)
	# 2) the runtime res tree for embedding consumers (install_res.cmake)
	INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${BW_PYTHON_SRC_DIR}/Lib" "${CMAKE_CURRENT_BINARY_DIR}/python3.13/lib/python3.13"
	COMMAND ${CMAKE_COMMAND}
		-DBW_PYTHON_SRC_DIR=${BW_PYTHON_SRC_DIR}
		-DBW_PYTHON_BUILD_DIR=${BW_PYTHON_BUILD_DIR}
		-DBW_PYSCRIPT_DIR=${CMAKE_CURRENT_LIST_DIR}/../../../lib/pyscript
		-DBW_PYTHON_RES_TREE=${BW_PYTHON_RES_TREE}
		-P ${CMAKE_CURRENT_LIST_DIR}/install_res.cmake
	BUILD_BYPRODUCTS "${BW_PYTHON_ARCHIVE}"
	LOG_CONFIGURE TRUE
	LOG_BUILD TRUE
	USES_TERMINAL_BUILD TRUE
)

# --- BWPython::Python imported target ---------------------------------------
# GLOBAL: IMPORTED targets are directory-scoped by default, which would hide
# this one from the parent entitydef sample that includes us.
add_library(BWPython::Python INTERFACE IMPORTED GLOBAL)
add_dependencies(BWPython::Python bwpython313)
set_target_properties(BWPython::Python PROPERTIES
	INTERFACE_INCLUDE_DIRECTORIES
		"${BW_PYTHON_SRC_DIR}/Include;${BW_PYTHON_BUILD_DIR}"
	INTERFACE_COMPILE_DEFINITIONS SCRIPT_PYTHON
	INTERFACE_LINK_LIBRARIES "${BW_PYTHON_ARCHIVE};Threads::Threads;dl;m"
)
# Mirrors BW_PYTHON_LDFLAGS (third_party_python_symbols.mak).
target_link_options(BWPython::Python INTERFACE
	-Wl,-u,BW_Py_memoryTrackingIgnoreBegin
	-Wl,-u,BW_Py_memoryTrackingIgnoreEnd
)

# --- BWHooks symbol check ----------------------------------------------------
# Fail the build if the archive no longer carries the symbols pyscript links
# against: this is the regression that made vcpkg's python3 unusable here.
add_custom_target(bwpython_symbols ALL
	DEPENDS bwpython313
	COMMAND ${CMAKE_COMMAND}
		-DBW_PYTHON_ARCHIVE=${BW_PYTHON_ARCHIVE}
		-P ${CMAKE_CURRENT_LIST_DIR}/check_symbols.cmake
	COMMENT "Checking BWHooks symbols in libpython3.13.a"
)
