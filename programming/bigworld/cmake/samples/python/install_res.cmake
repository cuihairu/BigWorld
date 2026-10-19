# Install the runtime Python res tree for CMake-track consumers that embed the
# interpreter. Invoked from ExternalProject_Add's INSTALL_COMMAND
# (cmake/samples/python/CMakeLists.txt); inputs arrive as -D cache variables.
#
# Mirrors third_party_python.mak's python_library target: pyscript's
# Script::init resolves the stdlib through BWResource, not the configure
# prefix, and plain stdlib imports dlopen the extension modules
# (lib/pyscript/script.cpp builds scripts/server_common/lib-dynload-<platform>
# from BW::PlatformInfo::buildStr(); lib/cstdmf/bw_platform_info.cpp maps
# Debian-family hosts to el7 like the make chain's DEBIAN_EQUIVALENT_PLATFORM).
#   scripts/common/Lib                          CPython stdlib sources
#   scripts/common/bw_site.py                   engine site.py replacement
#   scripts/server_common/lib-dynload-el7/      built extension modules
# The unit_test_lib harness and entitydef sample point --res at
# ${BW_PYTHON_RES_TREE}/game/res/bigworld; the directory is gitignored in a
# fresh checkout, so this install is the only source of these files on the
# CMake track.

if(NOT BW_PYTHON_SRC_DIR OR NOT BW_PYTHON_BUILD_DIR OR NOT BW_PYTHON_RES_TREE)
	message(FATAL_ERROR "install_res.cmake: missing -D inputs")
endif()

set(BW_RES_BIGWORLD "${BW_PYTHON_RES_TREE}/game/res/bigworld")
set(BW_RES_COMMON "${BW_RES_BIGWORLD}/scripts/common")
set(BW_RES_DYNLOAD "${BW_RES_BIGWORLD}/scripts/server_common/lib-dynload-el7")

file(MAKE_DIRECTORY "${BW_RES_COMMON}" "${BW_RES_DYNLOAD}")

# CPython stdlib sources (libinstall minus the optional bytecode compile).
file(COPY "${BW_PYTHON_SRC_DIR}/Lib" DESTINATION "${BW_RES_COMMON}")

# bw_site.py: Script::init sets Py_NoSiteFlag = 1 and imports bw_site from
# scripts/common, failing hard if it is missing (see third_party_python.mak).
if(NOT EXISTS "${BW_PYSCRIPT_DIR}/bw_site.py")
	message(FATAL_ERROR "install_res.cmake: bw_site.py missing under ${BW_PYSCRIPT_DIR}")
endif()
file(COPY "${BW_PYSCRIPT_DIR}/bw_site.py" DESTINATION "${BW_RES_COMMON}")

# Built extension modules. check_extension_modules.py renames any module that
# failed its dlopen test to *_failed<EXT_SUFFIX>; skip those like the make
# chain's existence assertions would.
file(GLOB BW_PYTHON_SHARED_MODS "${BW_PYTHON_BUILD_DIR}/Modules/*.so")
list(FILTER BW_PYTHON_SHARED_MODS EXCLUDE REGEX "_failed")
if(NOT BW_PYTHON_SHARED_MODS)
	message(FATAL_ERROR
		"install_res.cmake: no extension modules built in ${BW_PYTHON_BUILD_DIR}/Modules")
endif()
file(COPY ${BW_PYTHON_SHARED_MODS} DESTINATION "${BW_RES_DYNLOAD}")
