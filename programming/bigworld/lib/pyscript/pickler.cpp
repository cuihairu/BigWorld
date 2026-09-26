#include "pch.hpp"

#include "pickler.hpp"
#include "cstdmf/debug.hpp"
#include "pyscript/pyobject_plus.hpp"

DECLARE_DEBUG_COMPONENT2( "Script", 0)


BW_BEGIN_NAMESPACE

// -----------------------------------------------------------------------------
// Section: FailedUnpickle
// -----------------------------------------------------------------------------

/**
 *	This class implements a simple Python object that handles the case where
 *	pickle data cannot be unpickled. This object is created to hold onto the
 *	pickled data.
 */
class FailedUnpickle : public PyObjectPlus
{
	Py_Header( FailedUnpickle, PyObjectPlus )

public:
	FailedUnpickle( const BW::string & pickleData,
			PyTypeObject * pType = &FailedUnpickle::s_type_ ) :
		PyObjectPlus( pType ),
		pickleData_( pickleData )
	{
	}

	const BW::string & pickleData() const	{ return pickleData_; }

private:
	BW::string pickleData_;
};

PY_TYPEOBJECT( FailedUnpickle )

PY_BEGIN_METHODS( FailedUnpickle )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( FailedUnpickle )
PY_END_ATTRIBUTES()


// -----------------------------------------------------------------------------
// Section: Pickler
// -----------------------------------------------------------------------------

namespace
{
// int 		s_refCount			= 0;
PyObject * 	s_pPickleMethod		= NULL;
PyObject * 	s_pUnpickleMethod	= NULL;
}

/**
 * 	This static method initialises Pickler. It loads the Python pickle module
 * 	and queries methods needed.
 */
bool Pickler::init()
{
	// BIGWORLD_BEGIN(3.13 migration)
	// Was "cPickle", which became the plain "pickle" module in Python 3.
	// BIGWORLD_END
	PyObject * pPickleModule = PyImport_ImportModule( "pickle" );

	if (pPickleModule != NULL)
	{
		if (!s_pPickleMethod)
		{
			s_pPickleMethod = PyObject_GetAttrString( pPickleModule, "dumps" );
			if (!s_pPickleMethod)
			{
				ERROR_MSG( "Pickler::init: Failed to get dumps\n" );
				PyErr_PrintEx(0);
			}
		}

		if (!s_pUnpickleMethod)
		{
			s_pUnpickleMethod = PyObject_GetAttrString(pPickleModule, "loads");

			if (!s_pUnpickleMethod)
			{
				ERROR_MSG( "Pickler::init: Failed to get loads\n" );
				PyErr_PrintEx(0);
			}
		}

		Py_DECREF( pPickleModule );
	}
	else
	{
		PyErr_PrintEx(0);
#ifdef _WIN32
		ERROR_MSG( "Failed to import the pickle module.\n" );
#else
		ERROR_MSG( "Failed to import the pickle module. "
			"Is your resource path set correctly?\n"
			"\tThis requires the Python 3 stdlib (including pickle.py) to be "
			"importable (see python_install).\n" );
#endif
	}

	return (s_pPickleMethod != NULL) && (s_pUnpickleMethod != NULL);
}


/**
 * 	This method pickles the given object into a binary string.
 *
 * 	@param pObj		The Python object to pickle.
 *	@param output 	The string to fill with the pickled string.
 * 	@return			True if successfully pickled, otherwise false.
 */
BW::string Pickler::pickle( ScriptObject object )
{
	PyObject * pObj = object.get();

	if (!pObj)
	{
		ERROR_MSG( "Pickler::pickle: attempting to pickle NULL\n" );
	}
	else if (Py_TYPE( pObj ) == &FailedUnpickle::s_type_)
	{
		return static_cast< FailedUnpickle * >( pObj )->pickleData();
	}
	else if (s_pPickleMethod != NULL)
	{
		PyObject * pResult;

		pResult = PyObject_CallFunction( s_pPickleMethod, "(Oi)", pObj, 2 );

		if (pResult == NULL)
		{
			ERROR_MSG( "Pickler::pickle: failed to pickle object\n" );
			PyErr_Print();
		}
		else
		{
			BW::string str;

			// BIGWORLD_BEGIN(3.13 migration)
			// Was PyUnicode_AsUTF8/PyUnicode_GET_LENGTH; the pickle protocol
			// output is bytes now.
			str.assign( PyBytes_AsString( pResult ),
				PyBytes_Size( pResult ) );
			// BIGWORLD_END
			Py_DECREF( pResult );

			return str;
		}
	}

	return "";
}


/**
 * 	This method unpickles the given binary string into a python object.
 *
 * 	@param str	The pickled string
 * 	@return		Object to pickle
 */
ScriptObject Pickler::unpickle( const BW::string & str )
{
	PyObject* pResult = NULL;

	if (s_pUnpickleMethod != NULL)
	{
		/* BIGWORLD_BEGIN(3.13 migration)
		 * "y#" builds a bytes object directly from the buffer; "s#" would
		 * UTF-8 *decode* it into a str first. A pickle stream is binary and
		 * its first byte is the PROTO opcode 0x80 (protocol 2), which is not
		 * valid UTF-8, so "s#" made every unpickle fail with
		 *   UnicodeDecodeError: 'utf-8' codec can't decode byte 0x80 ...
		 * and we silently fell back to the FailedUnpickle stand-in object.
		 */
		pResult = PyObject_CallFunction( s_pUnpickleMethod, "(y#)",
				str.data(), str.length() );
		/* BIGWORLD_END */

		if (pResult == NULL)
		{
			NOTICE_MSG( "Pickler::unpickle: "
					"Failed to unpickle. Using stand-in object.\n" );
			PyErr_Print();
		}
	}

	if (pResult == NULL)
	{
		pResult = new FailedUnpickle( str );
	}

	return ScriptObject( pResult, ScriptObject::STEAL_REFERENCE );
}


/**
 *	This method deletes the global pickle and unpickle methods.
 */
void Pickler::finalise()
{	
	Py_XDECREF( s_pPickleMethod );
	s_pPickleMethod = NULL;
	Py_XDECREF( s_pUnpickleMethod );
	s_pUnpickleMethod = NULL;
}

BW_END_NAMESPACE

// pickler.cpp
