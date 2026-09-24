#include "pch.hpp"

#include "cstdmf/debug_message_source.hpp"
#include "cstdmf/log_msg.hpp"

#include "pyscript/pyobject_plus.hpp"
#include "pyscript/script.hpp"


BW_BEGIN_NAMESPACE

int PyLogging_token = 1;

namespace PyLogging
{

PyObject * py_logCommon( PyObject * args, DebugMessagePriority priority,
		const char * name )
{
	if (!PyTuple_Check( args ))
	{
		PyErr_Format( PyExc_TypeError,
			"Invalid arguments provided to py_log%s.", name );
		return NULL;
	}

	if (PyTuple_Size( args ) != 3)
	{
		PyErr_Format( PyExc_TypeError,
			"py_log%s argument tuple has invalid number of elements.", name );
		return NULL;
	}

	PyObject * objCategoryString = PyTuple_GET_ITEM( args, 0 );
	PyObject * objMessageString = PyTuple_GET_ITEM( args, 1 );
	PyObject * objLogMetaData = PyTuple_GET_ITEM( args, 2 );

	// BIGWORLD_BEGIN(3.13 migration)
	// Was PyString_Check; str is unicode now.
	if (!PyUnicode_Check( objCategoryString ) ||
		!PyUnicode_Check( objMessageString ) ||
		(!PyUnicode_Check( objLogMetaData ) && (objLogMetaData != Py_None)))
	// BIGWORLD_END
	{
		PyErr_Format( PyExc_TypeError,
			"py_log%s has invalid element types.", name );
		return NULL;
	}

	// BIGWORLD_BEGIN(3.13 migration)
	// Was PyString_AsString.
	const char * pCategoryString = PyUnicode_AsUTF8( objCategoryString );
	const char * pMessageString = PyUnicode_AsUTF8( objMessageString );
	// BIGWORLD_END

	if (pMessageString == NULL)
	{
		PyErr_Format( PyExc_ValueError,
			"py_log%s has invalid message string.", name );
		return NULL;
	}

	// BIGWORLD_BEGIN(3.13 migration)
	// Was PyString_Size/PyString_AsString.
	if ((objLogMetaData != Py_None) &&
			(PyUnicode_GET_LENGTH( objLogMetaData ) > 0))
	{
		const char * pMetaData = PyUnicode_AsUTF8( objLogMetaData );
		// BIGWORLD_END

		LogMsg( priority, pCategoryString ).
				source( MESSAGE_SOURCE_SCRIPT ).
				metaAsJSON( pMetaData ).
				write( "%s\n", pMessageString );
	}
	else
	{
		LogMsg( priority, pCategoryString ).
				source( MESSAGE_SOURCE_SCRIPT ).
				write( "%s\n", pMessageString );
	}

	Py_RETURN_NONE;
}

#define CREATE_PY_LOG_HANDLER( NAME, PRIORITY )								\
PyObject * py_log ## NAME( PyObject * args )								\
{																			\
	return py_logCommon( args, PRIORITY, #NAME );							\
}																			\
PY_MODULE_FUNCTION_WITH_DOC( log ## NAME, BigWorld, __DOC__log ## NAME )	\


#define __DOC__logTrace \
"Log a message with severity 'TRACE'."
#define __DOC__logDebug \
"Log a message with severity 'DEBUG'."
#define __DOC__logInfo \
"Log a message with severity 'INFO'."
#define __DOC__logNotice \
"Log a message with severity 'NOTICE'."
#define __DOC__logWarning \
"Log a message with severity 'WARNING'."
#define __DOC__logError \
"Log a message with severity 'ERROR'."
#define __DOC__logCritical \
"Log a message with severity 'CRITICAL'."
#define __DOC__logHack \
"Log a message with severity 'HACK'."

CREATE_PY_LOG_HANDLER( Trace,    MESSAGE_PRIORITY_TRACE )
CREATE_PY_LOG_HANDLER( Debug,    MESSAGE_PRIORITY_DEBUG )
CREATE_PY_LOG_HANDLER( Info,     MESSAGE_PRIORITY_INFO )
CREATE_PY_LOG_HANDLER( Notice,   MESSAGE_PRIORITY_NOTICE )
CREATE_PY_LOG_HANDLER( Warning,  MESSAGE_PRIORITY_WARNING )
CREATE_PY_LOG_HANDLER( Error,    MESSAGE_PRIORITY_ERROR )
CREATE_PY_LOG_HANDLER( Critical, MESSAGE_PRIORITY_CRITICAL )
CREATE_PY_LOG_HANDLER( Hack,     MESSAGE_PRIORITY_HACK )

#undef CREATE_PY_LOG_HANDLER

} // namespace PyLogging

BW_END_NAMESPACE

// py_logging.cpp
