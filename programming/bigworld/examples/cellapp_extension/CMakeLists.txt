CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( cellapp_extension )

INCLUDE( BWStandardProject )

SET( ALL_SRCS
	bounce_grenade_extra.cpp
	bounce_grenade_extra.hpp
	debug_extension.cpp
	dump_controllers_extra.cpp
	dump_controllers_extra.hpp
	egcontroller.cpp
	egcontroller.hpp
	egextra.cpp
	egextra.hpp
	speed_controller.cpp
	speed_controller.hpp
	speed_extra.cpp
	speed_extra.hpp
)
SOURCE_GROUP( "" FILES ${ALL_SRCS} )

BW_ADD_LIBRARY( cellapp_extension MODULE ${ALL_SRCS} )

# The example extends cellapp and compiles against its headers
# ("cellapp/controller.hpp" et al), which live in the server tree - the same
# include the cellapp module itself grants its own TUs
# (server/cellapp/CMakeLists.txt). The legacy VS project carried this
# additional include dir; without it every TU fails with
# "'cellapp/controller.hpp': No such file or directory".
TARGET_INCLUDE_DIRECTORIES( cellapp_extension PRIVATE ${BW_SOURCE_DIR}/server )

BW_PROJECT_CATEGORY( cellapp_extension "Examples" )
