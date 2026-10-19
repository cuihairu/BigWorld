CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( build )

INCLUDE( BWStandardProject )
INCLUDE( BWStandardLibrary )

# build.cpp includes build_revision.hpp, a generated header. The make chain
# builds it from build_revision.hpp.in with build.py (p4 -> svn -> -1) into an
# intermediate dir kept on the include path (lib/build/Makefile.rules); the
# CMake side had no equivalent, so every consumer of this library died with
# C1083 on that include (run 39010548962). Generate it at configure time with
# the repo's own script so the fallback matches the make chain; without a
# python interpreter, configure_file stamps the UNKNOWN_REVISION placeholder.
SET( BUILD_REVISION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/build_revision.hpp )
SET( BUILD_REVISION_GENERATED FALSE )
IF( NOT CMAKE_VERSION VERSION_LESS 3.12 )
	FIND_PACKAGE( Python3 COMPONENTS Interpreter QUIET )
	IF( Python3_FOUND )
		EXECUTE_PROCESS(
			COMMAND ${Python3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/build.py
				${CMAKE_CURRENT_SOURCE_DIR}/build_revision.hpp.in
				${BUILD_REVISION_HEADER}
			RESULT_VARIABLE BUILD_REVISION_RESULT )
		IF( BUILD_REVISION_RESULT EQUAL 0 )
			SET( BUILD_REVISION_GENERATED TRUE )
		ENDIF()
	ENDIF()
ENDIF()
IF( NOT BUILD_REVISION_GENERATED )
	SET( REVISION_NUMBER -1 )
	CONFIGURE_FILE( build_revision.hpp.in ${BUILD_REVISION_HEADER} @ONLY )
ENDIF()

INCLUDE_DIRECTORIES( ${CMAKE_CURRENT_BINARY_DIR} )

SET( ALL_SRCS
	build.cpp
	build.hpp
)
SOURCE_GROUP( "" FILES ${ALL_SRCS} )

BW_BLOB_SOURCES( BLOB_SRCS ${ALL_SRCS} )
BW_ADD_LIBRARY( build
	${BLOB_SRCS}
)

BW_TARGET_LINK_LIBRARIES( build INTERFACE
	cstdmf
	)

BW_PROJECT_CATEGORY( build "BigWorld Libs" )
