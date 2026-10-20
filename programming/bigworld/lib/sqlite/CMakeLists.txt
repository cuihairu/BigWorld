CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( sqlite )

INCLUDE( BWStandardProject )
INCLUDE( BWStandardLibrary )

# sqlite3 comes from vcpkg (vcpkg.json) when a vcpkg toolchain file is in
# play; this wrapper then compiles without the vendored amalgamation and
# links the vcpkg library instead. The vendored fallback keeps compiling
# third_party/sqlite/sqlite3.c into the wrapper (the make chain takes the
# same port from vcpkg). The make chain adds third_party/sqlite to the
# include path (sqlite_connection.hpp includes "sqlite3.h" while compiling
# this wrapper, C1083 in the root superproject build, run 39016799505).
# Consumers (baseapp/sqlite_database.cpp, transfer_db/consolidate.cpp, same
# C1083 in run 39021844438) get the headers through the PUBLIC target
# include of whichever source is in play.
FIND_PACKAGE( SQLite3 QUIET )

IF( SQLite3_FOUND )
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

	TARGET_INCLUDE_DIRECTORIES( sqlite PUBLIC ${SQLite3_INCLUDE_DIRS} )
	TARGET_LINK_LIBRARIES( sqlite PUBLIC ${SQLite3_LIBRARIES} )
ELSE()
	SET( WRAPPER_SRCS
		sqlite_connection.cpp
		sqlite_connection.hpp
		sqlite_statement.cpp
		sqlite_statement.hpp
		../../third_party/sqlite/sqlite3.c
	)
	SOURCE_GROUP( "" FILES ${WRAPPER_SRCS} )

	BW_ADD_LIBRARY( sqlite
		${WRAPPER_SRCS}
	)

	TARGET_INCLUDE_DIRECTORIES( sqlite PUBLIC ${BW_SOURCE_DIR}/third_party/sqlite )
ENDIF()

BW_PROJECT_CATEGORY( sqlite "BigWorld Server" )
