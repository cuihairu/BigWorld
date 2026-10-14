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
# Only libpython3.13.a is produced. The sed post-processing in
# third_party_python.mak (TERMNAM undefs in pyconfig.h, LINKFORSHARED
# whole-archive OpenSSL + -u,BW_Py_memoryTrackingIgnore* on the interpreter
# link) only affects python.exe and the shared extension modules, and this
# sample builds neither: CMake-track consumers link the interpreter statically
# and do not execute extension modules (entitydef_test initializes Python and
# runs script code but never dlopens lib-dynload).
#
# The interpreter still resolves its stdlib at runtime relative to the
# configure-time --prefix (<prefix>/lib/python3.13), which in the make chain is
# populated by `make install`. Installing just the source Lib/ tree (what
# libinstall copies, minus the optional bytecode compile) is enough: the
# initial import of encodings and everything entitydef_test touches is pure
# Python, and building python.exe/lib-dynload to run a full `make install`
# would drag in the sed hacks above for no benefit.
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
	BUILD_COMMAND "${BW_MAKE_EXECUTABLE}" -j${CMAKE_BUILD_PARALLEL_LEVEL}
		libpython3.13.a EXTRA_CFLAGS=-m64\ -fPIC
	# stdlib the runtime resolves under <prefix>/lib/python3.13 (see header
	# note); a full `make install` would also build python.exe + lib-dynload
	INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${BW_PYTHON_SRC_DIR}/Lib" "${CMAKE_CURRENT_BINARY_DIR}/python3.13/lib/python3.13"
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
