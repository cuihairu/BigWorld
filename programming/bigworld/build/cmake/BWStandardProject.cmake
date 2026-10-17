# Additional include directories
INCLUDE_DIRECTORIES( ${BW_SOURCE_DIR}/lib )
INCLUDE_DIRECTORIES( ${BW_SOURCE_DIR}/third_party )
INCLUDE_DIRECTORIES( $ENV{DXSDK_DIR}/Include )
INCLUDE_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/python/Include )
INCLUDE_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/python/PC )
# The vendored zlib. resmgr (zip_file_system.cpp, binary_block.cpp) and
# lib/network (compression_stream.cpp, zip_stream.cpp) include <zlib.h>, but
# zip is a plain static library with no interface includes, so its header
# directory has to be supplied here like every other third party one (MSVC
# C1083 on the first root superproject build).
INCLUDE_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/zip )

# The vendored libpng (1.6.2, the same major/minor the make chain gets from
# vcpkg). lib/moo/png.cpp includes <png.h>, but libpng is a plain static
# library built from projects/visualc71 with no interface includes, so its
# header directory has to be supplied here like zip's (MSVC C1083 on the
# root superproject build).
INCLUDE_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/png )

# Python.h includes pyconfig.h, which PCbuild generates into pythoncore's own
# IntDir (PCbuild/pyproject.props:14) - the vendored tree ships no pyconfig.h,
# so this directory only comes into being when pythoncore is built by the
# bwpython313 ExternalProject in third_party/python. A path that does not
# exist yet is harmless here: unlike an IMPORTED target's
# INTERFACE_INCLUDE_DIRECTORIES, a plain INCLUDE_DIRECTORIES entry is not
# existence-checked at generate time, and the compiler ignores an absent -I.
# The version comes from the vendored patchlevel.h rather than being
# hardcoded, so a CPython bump cannot silently point at a stale archive.
IF( MSVC AND EXISTS "${BW_SOURCE_DIR}/third_party/python/Include/patchlevel.h" )
	FILE( READ "${BW_SOURCE_DIR}/third_party/python/Include/patchlevel.h" _bw_py_patchlevel )
	string( REGEX MATCH "#define[ \t]+PY_MAJOR_VERSION[ \t]+([0-9]+)" _bw_py_match "${_bw_py_patchlevel}" )
	SET( _bw_py_major "${CMAKE_MATCH_1}" )
	string( REGEX MATCH "#define[ \t]+PY_MINOR_VERSION[ \t]+([0-9]+)" _bw_py_match "${_bw_py_patchlevel}" )
	SET( _bw_py_minor "${CMAKE_MATCH_1}" )
	IF( BW_ARCH_64 OR CMAKE_SIZEOF_VOID_P EQUAL 8 )
		SET( _bw_py_arch "amd64" )
	ELSE()
		SET( _bw_py_arch "win32" )
	ENDIF()
	INCLUDE_DIRECTORIES( "${BW_SOURCE_DIR}/third_party/python/PCbuild/obj/${_bw_py_major}${_bw_py_minor}${_bw_py_arch}_Release/pythoncore" )

	# Python.h inclusion is gated on this (script/first_include.hpp); the
	# make chain defines it for every usePython binary
	# (common_footer_config.mak). The cmake track never defined it, so TUs
	# whose header chain has no other route to Python.h lost the Py API
	# (C2065 'PyExc_ValueError' on bots/patrol_graph.cpp, run 39021844438).
	ADD_DEFINITIONS( -DSCRIPT_PYTHON )
ENDIF()

# Add include directories for remote build system header files
IF (BW_IS_REMOTE_ONLY AND  NOT ${BW_LINUX_CONN_TYPE} MATCHES "RSYNC" )
	INCLUDE_DIRECTORIES(BEFORE SYSTEM ${BW_REMOTE_INCLUDE_DIRS} )
ENDIF()

BW_ADD_COMPILE_FLAGS(
	/fp:fast  # Fast floating point model
)

# Arch dependent library directories and definitions
IF( BW_PLATFORM_WINDOWS AND BW_ARCH_32 ) 
	LINK_DIRECTORIES( $ENV{DXSDK_DIR}/Lib/x86 )
	IF( BW_FMOD_SUPPORT )
		LINK_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/fmod/api/lib )
		LINK_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/fmod/fmoddesignerapi/api/lib )
	ENDIF()
	IF( BW_UMBRA_SUPPORT )
		LINK_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/umbra/lib/win32 )
	ENDIF()
ELSEIF( BW_PLATFORM_WINDOWS AND BW_ARCH_64 )
	LINK_DIRECTORIES( ${BW_SOURCE_DIR}/lib )
	LINK_DIRECTORIES( $ENV{DXSDK_DIR}/Lib/x64 )
	IF( BW_FMOD_SUPPORT )
		LINK_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/fmod/api/lib/x64 )
		LINK_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/fmod/fmoddesignerapi/api/lib/x64 )
	ENDIF()
	IF( BW_UMBRA_SUPPORT )
		LINK_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/umbra/lib/win64 )
	ENDIF()
ELSEIF( BW_PLATFORM_LINUX )
ELSE()
	MESSAGE( FATAL_ERROR "invalid platform '${BW_PLATFORM}'!" )
ENDIF()

ADD_DEFINITIONS( -DBW_BUILD_PLATFORM="${BW_PLATFORM}" )

# TODO: Re-enable the eval version when we can build it
#IF( MSVC90 )
#	LINK_DIRECTORIES( ${BW_SOURCE_DIR}/lib/speedtreert/lib/VC90 )
#ELSEIF( MSVC10 OR MSVC11 )
#	IF( ${BW_PLATFORM} STREQUAL "win64" )
#		LINK_DIRECTORIES( ${BW_SOURCE_DIR}/lib/speedtreert/lib/VC100/x64 )
#	ELSE()
#		LINK_DIRECTORIES( ${BW_SOURCE_DIR}/lib/speedtreert/lib/VC100 )
#	ENDIF()
#ELSE()
#	MESSAGE( FATAL_ERROR "CMake standard project for '${CMAKE_GENERATOR}' not currently supported by BigWorld." )
#ENDIF()

LINK_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/nvtt/lib/${BW_COMPILER_TOKEN} )

IF( BW_PLATFORM_WINDOWS )
# Windows version definitions
ADD_DEFINITIONS(
	-DNTDDI_VERSION=0x06010000 # NTDDI_WIN7
	-D_WIN32_WINNT=0x0601   # Windows 7. inet_pton/inet_ntop in ws2tcpip.h
	                        # are gated on _WIN32_WINNT >= 0x0600, and both
	                        # lib/server/nat_config.cpp and lib/network/
	                        # address_resolver.cpp call them (C2039/C3861 in the
	                        # root superproject build while pinned to XP). The
	                        # VS 2026 toolchain cannot produce XP binaries and the
	                        # embedded CPython 3.13 already requires 8.1+, so the
	                        # XP pin was fiction either way.
	-D_WIN32_WINDOWS=0x0601
	-D_WIN32_IE=0x0600
	# Lean windows.h. A full windows.h drags in ole2/objbase, whose
	# `#define interface struct` rewrites every interface()-named declaration
	# in the Mercury chain into a syntax error (MSVC C2332/C2144/C2059 on
	# db_storage/db_engine_creator.hpp in the root superproject build), and
	# Winsock2.h includes windows.h itself, so include order cannot settle it.
	# lib/cstdmf/cstdmf_windows.hpp already relies on the same define; setting
	# it here closes the path for every header that includes <windows.h> or
	# <Winsock2.h> directly. ole2.h is not included anywhere in the tree, so
	# nothing depends on windows.h pulling it in.
	-DWIN32_LEAN_AND_MEAN
	# The tree still uses the Berkeley address APIs (inet_ntoa x9,
	# gethostbyname x2, gethostbyaddr, inet_addr in lib/network and
	# lib/server) and /WX turns their SDK deprecation note into an error
	# (C4996 x13 in the root superproject build). This is the SDK's own
	# opt-out for exactly that family; converting the call sites to
	# inet_ntop/getaddrinfo is on the Windows lane ledger.
	-D_WINSOCK_DEPRECATED_NO_WARNINGS
)

# Standard preprocessor definitions
ADD_DEFINITIONS(
	-D_CRT_SECURE_NO_WARNINGS
	-D_CRT_NONSTDC_NO_DEPRECATE
	-D_CRT_SECURE_NO_DEPRECATE
	-D_CRT_NONSTDC_NO_DEPRECATE
	#-DD3DXFX_LARGEADDRESS_HANDLE # NOT YET FOR WOWP
)

ENDIF()

IF ( MSVC AND BW_IS_REMOTE_ONLY )
BW_ADD_COMPILE_FLAGS( 
	/u # Remove all predefined macros
	/FI"${CMAKE_BINARY_DIR}/gcc_defines.h" # Force include GCC defines
)
ENDIF()

# Tell python extensions that we are not building against python27.dll
# Ideally this definition should only be enabled in project which depend on python
# But since in BW nearly everything depends on python it's enabled for every project
IF( NOT BW_PYTHON_AS_SOURCE )
	IF( BW_ARCH_32 )
		LINK_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/python/PCbuild )
	ELSEIF( BW_ARCH_64 )
		LINK_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/python/PCbuild/amd64 )
	ENDIF()
ENDIF()
# The old Py_BUILD_CORE define for the AS_SOURCE branch is gone: it comes from
# the era when the Python sources were compiled into the consuming image, and
# under the ExternalProject/DLL model it marks every CPython API declaration
# __declspec(dllexport) instead of dllimport in every TU. Data symbols
# (_Py_NoneStruct, PyType_Type, PyExc_*) then cannot resolve from
# python313.lib at MODULE link time (run 38985715421; the import archive was
# verified present on every link line by the linkdeps dump of 38990281409).
# The make chain never defines it, and no engine source references it.

IF( NOT BW_NO_UNICODE )
	ADD_DEFINITIONS( -DUNICODE -D_UNICODE )
ENDIF()

# Remove /STACK:10000000 set by CMake. This value for stack size
# is very high, limiting the number of threads we can spawn.
# Default value used by Windows is 1MB which is good enough.
STRING( REGEX REPLACE "/STACK:[0-9]+" ""
	CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" )
