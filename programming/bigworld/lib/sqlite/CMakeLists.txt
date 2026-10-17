CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( sqlite )

INCLUDE( BWStandardProject )
INCLUDE( BWStandardLibrary )

# The vendored amalgamation lives one level up; the make chain adds
# third_party/sqlite to the include path (sqlite_connection.hpp includes
# "sqlite3.h" while compiling this wrapper, C1083 in the root superproject
# build, run 39016799505).
INCLUDE_DIRECTORIES( ${BW_SOURCE_DIR}/third_party/sqlite )

SET( ALL_SRCS
	sqlite_connection.cpp
	sqlite_connection.hpp
	sqlite_statement.cpp
	sqlite_statement.hpp
	../../third_party/sqlite/sqlite3.c
)
SOURCE_GROUP( "" FILES ${ALL_SRCS} )

BW_ADD_LIBRARY( sqlite
	${ALL_SRCS}
)

BW_PROJECT_CATEGORY( sqlite "BigWorld Server" )
