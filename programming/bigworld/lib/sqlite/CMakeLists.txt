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
#
# Under the port the link goes through its CMake config package
# (unofficial-sqlite3): the imported target carries headers and picks the
# right CRT variant per configuration. Module-mode FindSQLite3 resolves the
# one shared library name in <prefix>/debug/lib before <prefix>/lib under
# the vcpkg toolchain and handed the MDd sqlite3.lib to the Hybrid
# transfer_db link (LNK2038 class, run 39439040925), so it stays only as
# the fallback for hosts without the port.
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
ENDIF()

BW_PROJECT_CATEGORY( sqlite "BigWorld Server" )
