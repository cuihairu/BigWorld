CMAKE_MINIMUM_REQUIRED( VERSION 2.8 )
PROJECT( _bw_profile )

INCLUDE( BWStandardProject )

SET( ALL_SRCS
	bw_profile.cpp
)
SOURCE_GROUP( "" FILES ${ALL_SRCS} )

BW_ADD_LIBRARY( _bw_profile MODULE ${ALL_SRCS} )


# MODULE (DLL on MSVC) targets resolve every symbol at link time, and this
# one spans the Python C-API (PyArg_ParseTuple, PyModule_Create2) plus
# cstdmf's profiler. On the make chain _bwlog.so-style modules resolve the
# interpreter at load time from the hosting process; a MODULE target needs
# the import archive itself, same as the bwengine_* modules.
BW_TARGET_LINK_LIBRARIES( _bw_profile
	cstdmf
	libpython
)

BW_PROJECT_CATEGORY( _bw_profile "Tools" )
