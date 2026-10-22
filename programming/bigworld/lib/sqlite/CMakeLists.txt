CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( sqlite )

INCLUDE( BWStandardProject )
INCLUDE( BWStandardLibrary )

# sqlite3 comes from the pinned vcpkg manifest. The wrapper stays a thin
# translation layer around the port: the imported target carries headers and
# picks the right CRT variant per configuration.
FIND_PACKAGE( unofficial-sqlite3 CONFIG QUIET )

IF( TARGET unofficial::sqlite3::sqlite3 )
	SET( WRAPPER_SRCS
		sqlite_connection.cpp
		sqlite_connection.hpp
		sqlite_statement.cpp
		sqlite_statement.hpp
	)
	SOURCE_GROUP( "" FILES ${WRAPPER_SRCS} )

	BW_ADD_LIBRARY( sqlite
		${WRAPPER_SRCS}
	)

	TARGET_LINK_LIBRARIES( sqlite PUBLIC unofficial::sqlite3::sqlite3 )
ELSE()
	MESSAGE( FATAL_ERROR "sqlite3 requires the vcpkg manifest" )
ENDIF()

BW_PROJECT_CATEGORY( sqlite "BigWorld Server" )
