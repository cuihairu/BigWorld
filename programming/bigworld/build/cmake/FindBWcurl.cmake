# - Find BigWorld-bundled curl installation
# Try to find the curl libraries bundled with BigWorld
# Once done, this will define
#	BWCURL_FOUND - BigWorld-bundled curl is present and compiled
#	BWCURL_INCLUDE_DIRS - The BigWorld-bundled curl include directories
#	BWCURL_LIBRARIES - The libraries needed to use the BigWorld-bundled curl

# Based on FindScaleformSDK.cmake, which is
# based on http://www.cmake.org/Wiki/CMake:How_To_Find_Libraries
# and FindALSA.cmake, which is apparently current best-practice

# BIGWORLD_BEGIN(3.13 migration)
# curl is provided by vcpkg (see vcpkg.json next to the root CMakeLists.txt)
# with the openssl feature. When configuring with the vcpkg toolchain file,
# CMake's own FindCURL resolves it; map the results onto the BW-prefixed
# variables and stop. The legacy branches below remain for the prebuilt
# Windows third_party/curl trees.
IF( NOT MSVC )
	FIND_PACKAGE( CURL QUIET )
	IF( CURL_FOUND )
		SET( BWCURL_ROOT_DIR "${CURL_INCLUDE_DIRS}" )
		SET( BWCURL_INCLUDE_DIR_RELEASE "${CURL_INCLUDE_DIRS}"
			CACHE FILEPATH "curl include dir (vcpkg)" FORCE )
		SET( BWCURL_LIBRARY_RELEASE "${CURL_LIBRARIES}"
			CACHE FILEPATH "curl library (vcpkg)" FORCE )
		SET( BWCURL_LIBRARY_DEBUG "${CURL_LIBRARIES}" )
		SET( BWCURL_INCLUDE_DIRS "${CURL_INCLUDE_DIRS}" )
		SET( BWCURL_LIBRARIES "${CURL_LIBRARIES}" )
		SET( BWCURL_FOUND 1 )
		SET( BWcurl_FOUND 1 )
		RETURN()
	ENDIF()
ENDIF()
# BIGWORLD_END

FIND_PATH( BWCURL_ROOT_DIR
	include/curl/curlver.h
	PATHS	${BW_SOURCE_DIR}/third_party/curl
)

# Hide all of the curl build madness.
IF ( BW_ARCH_32 )
	SET( _ARCH "x86" )
ELSE()
	SET( _ARCH "x64" )
ENDIF()

SET( BWCURL_BUILD_RELEASE ${BWCURL_ROOT_DIR}/builds/libcurl-${BW_COMPILER_TOKEN}-${_ARCH}-release-static-sspi )
SET( BWCURL_BUILD_DEBUG ${BWCURL_ROOT_DIR}/builds/libcurl-${BW_COMPILER_TOKEN}-${_ARCH}-debug-static-sspi )

UNSET( _ARCH )

# BWcurl Include dir
# TODO: Release headers always? Can we fix this?
# Not a huge issue, curlbuild.h doesn't differentiate debug or release
FIND_PATH( BWCURL_INCLUDE_DIR_RELEASE
	curl/curlver.h "${BWCURL_BUILD_RELEASE}/include"
)

# Extract the version from LIBCURL_VERSION in curl/curlver.h
IF( BWCURL_INCLUDE_DIR_RELEASE AND EXISTS "${BWCURL_INCLUDE_DIR_RELEASE}/curl/curlver.h" )
	FILE( STRINGS "${BWCURL_INCLUDE_DIR_RELEASE}/curl/curlver.h"
		_BWCURL_VERSION_STR
		REGEX "^#define[\t ]+LIBCURL_VERSION[\t ]+\"[0-9.]+\""
	)
	STRING( REGEX REPLACE "^.*\"([^\ ]*)\".*$" "\\1"
		BWCURL_VERSION_STRING ${_BWCURL_VERSION_STR}
	)
	UNSET( _BWCURL_VERSION_STR )
ENDIF()

# BWcurl library paths
FIND_LIBRARY( BWCURL_LIBRARY_RELEASE
	libcurl_a.lib ${BWCURL_BUILD_RELEASE}/lib
)

FIND_LIBRARY( BWCURL_LIBRARY_DEBUG
	libcurl_a_debug.lib ${BWCURL_BUILD_DEBUG}/lib
)

INCLUDE( FindPackageHandleStandardArgs )

FIND_PACKAGE_HANDLE_STANDARD_ARGS( BWcurl
	REQUIRED_VARS
		# Top of BWcurl tree
		BWCURL_ROOT_DIR 
		# Headers
		BWCURL_INCLUDE_DIR_RELEASE
		# Libraries
		BWCURL_LIBRARY_RELEASE
		BWCURL_LIBRARY_DEBUG
	VERSION_VAR BWCURL_VERSION_STRING
)

IF( BWCURL_FOUND )
	SET( BWCURL_INCLUDE_DIRS
		${BWCURL_INCLUDE_DIR_RELEASE}
	)

	SET( BWCURL_LIBRARIES
		optimized ${BWCURL_LIBRARY_RELEASE}
		debug ${BWCURL_LIBRARY_DEBUG}
		wldap32.lib
	)

ENDIF()

MARK_AS_ADVANCED( 
	BWCURL_INCLUDE_DIR_RELEASE
	BWCURL_LIBRARY_RELEASE
	BWCURL_LIBRARY_DEBUG
)
