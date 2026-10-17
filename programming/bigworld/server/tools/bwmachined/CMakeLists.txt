CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( bwmachined )

INCLUDE( BWStandardProject )

# The machine daemon of the Linux track: it speaks syslog, setrlimit and
# /proc (sys/resource.h + syslog.h + unistd.h, C1083 x7 in the root
# superproject build, run 39016799505) and carries no win32 machine guard -
# this tarball's server processes ran behind the Linux bwmachined, so the
# make chain never built it on Windows. Skipping mirrors snapshot_helper;
# a win32 guard port is a design item of its own.
IF( BW_PLATFORM_WINDOWS )
	MESSAGE( STATUS "bwmachined is Linux-only. Skipping ${PROJECT_NAME} build." )
	RETURN()
ENDIF()

SET( ALL_SRCS
	main.cpp
	bwmachined.cpp
	bwmachined.hpp
	bwmachined.ipp
	cluster.cpp
	cluster.hpp
	common_machine_guard.hpp
	incoming_packet.cpp
	incoming_packet.hpp
	linux_machine_guard.cpp
	linux_machine_guard.hpp
	listeners.cpp
	listeners.hpp
	message_with_destination.hpp
	process_binary_version.hpp
	server_platform.cpp
	server_platform.hpp
	usermap.cpp
	usermap.hpp
)

IF( BW_PLATFORM_LINUX )
	LIST( APPEND ALL_SRCS
		server_platform_linux.cpp
		server_platform_linux.hpp
	)
ENDIF()

SOURCE_GROUP( "" FILES ${ALL_SRCS} )

BW_ADD_EXECUTABLE( bwmachined ${ALL_SRCS} )

BW_TARGET_LINK_LIBRARIES( bwmachined
	build
	server 
	network 
	cstdmf
)

BW_PROJECT_CATEGORY( bwmachined "Tools" )
