CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( consolidate_dbs )

INCLUDE( BWStandardProject )


# The MySQL client is a system dependency (make chain: SYSTEM_HAS_MYSQL,
# third_party_mysql.mak - the tool is skipped when the development package
# is missing, same ifneq guard as its Makefile.rules).
FIND_PATH( BW_MYSQL_INCLUDE_DIR mysql/mysql.h )
MARK_AS_ADVANCED( BW_MYSQL_INCLUDE_DIR )
IF( NOT BW_MYSQL_INCLUDE_DIR )
	MESSAGE( STATUS "MySQL development headers not found. Skipping ${PROJECT_NAME} build." )
	RETURN()
ENDIF()
SET( ALL_SRCS
	command_line_parser.cpp
	command_line_parser.hpp
	consolidate_dbs_app.cpp
	consolidate_dbs_app.hpp
	consolidate_entity_task.cpp
	consolidate_entity_task.hpp
	consolidation_progress_reporter.cpp
	consolidation_progress_reporter.hpp
	db_consolidator_errors.hpp
	db_file_transfer_error_monitor.cpp
	db_file_transfer_error_monitor.hpp
	dbapp.cpp
	dbapp.hpp
	dbapp_status_reporter.hpp
	file_receiver.cpp
	file_receiver.hpp
	file_receiver_mgr.cpp
	file_receiver_mgr.hpp
	file_transfer_progress_reporter.cpp
	file_transfer_progress_reporter.hpp
	main.cpp
	msg_receiver.cpp
	msg_receiver.hpp
	primary_database_update_queue.cpp
	primary_database_update_queue.hpp
	secondary_database.cpp
	secondary_database.hpp
	secondary_database_table.cpp
	secondary_database_table.hpp
	secondary_db_info.hpp
	sluggish_progress_reporter.hpp
	tcp_listener.hpp
	transfer_db_process.cpp
	transfer_db_process.hpp
)
SOURCE_GROUP( "" FILES ${ALL_SRCS} )

BW_ADD_EXECUTABLE( consolidate_dbs ${ALL_SRCS} )


BW_TARGET_LINK_LIBRARIES( consolidate_dbs
	build
	chunk
	db_storage
	db_storage_mysql
	entitydef
	pyscript
	script
	sqlite
	server
)

BW_PROJECT_CATEGORY( consolidate_dbs "Tools" )
