#include "pch.hpp"
#include "test_harness.hpp"
#include "cstdmf/bw_namespace.hpp"
#include "pyscript/py_data_section.hpp"
#include "pyscript/script.hpp"
#include "resmgr/bwresource.hpp"


namespace BW
{

namespace
{
	// The XML fixture loaded once per test from the component res directory.
	DataSectionPtr loadTestSection()
	{
		return BWResource::openSection( "test_py_data_section.xml" );
	}


	/**
	 *	Calls a method on a Python object, consuming the argument tuple just
	 *	like Script::ask does.
	 */
	PyObject * callMethod( PyObject * pObj, const char * name,
		PyObject * pArgs )
	{
		return Script::ask( PyObject_GetAttrString( pObj, name ), pArgs,
			name, false );
	}


	/**
	 *	Calls a no-argument method on a Python object.
	 */
	PyObject * callMethodNoArgs( PyObject * pObj, const char * name )
	{
		return callMethod( pObj, name, PyTuple_New( 0 ) );
	}


	/**
	 *	Reads a string attribute (the RW accessor style) off an object.
	 *	Returns an empty string if the attribute is missing or not a str;
	 *	the caller's CHECK_EQUAL reports the mismatch.
	 */
	BW::string attributeString( PyObject * pObj, const char * name )
	{
		PyObject * pValue = PyObject_GetAttrString( pObj, name );
		if ((pValue == NULL) || !PyUnicode_Check( pValue ))
		{
			Py_XDECREF( pValue );
			return BW::string();
		}

		BW::string result( PyUnicode_AsUTF8( pValue ) );
		Py_DECREF( pValue );
		return result;
	}
}


// -----------------------------------------------------------------------------
// Section: Structure
// -----------------------------------------------------------------------------

// A loaded XML section exposes its structure through keys(), has_key(),
// len(), subscripting and child()/childName().
TEST_F( PyScriptUnitTestHarness, PyDataSection_openAndStructure )
{
	DataSectionPtr pSection = loadTestSection();
	CHECK( pSection );

	PyDataSectionPtr pDS( new PyDataSection( pSection ), true );
	CHECK( pDS );

	// The section name attribute comes straight from the data section.
	{
		PyObject * pName = PyObject_GetAttrString( pDS.get(), "name" );
		CHECK( pName );
		CHECK_EQUAL( BW::string( "test_py_data_section.xml" ),
			BW::string( PyUnicode_AsUTF8( pName ) ) );
		Py_DECREF( pName );
	}

	// Length is the number of children.
	CHECK_EQUAL( 9, PyObject_Size( pDS.get() ) );

	// keys() lists the child names in document order.
	{
		PyObject * pKeys = callMethodNoArgs( pDS.get(), "keys" );
		CHECK( pKeys );
		CHECK( PyList_Check( pKeys ) );
		CHECK_EQUAL( 9, PyList_Size( pKeys ) );

		CHECK_EQUAL( BW::string( "child1" ),
			BW::string( PyUnicode_AsUTF8( PyList_GetItem( pKeys, 0 ) ) ) );
		Py_DECREF( pKeys );
	}

	// has_key() finds existing children only.
	{
		PyObject * pHas = callMethod( pDS.get(), "has_key",
			Py_BuildValue( "(s)", "child2" ) );
		CHECK( pHas );
		CHECK( pHas == Py_True );
		Py_DECREF( pHas );

		pHas = callMethod( pDS.get(), "has_key",
			Py_BuildValue( "(s)", "nonexistent" ) );
		CHECK( pHas );
		CHECK( pHas == Py_False );
		Py_DECREF( pHas );
	}

	// Subscripting opens the named child; missing names give None.
	{
		PyObject * pChild = PyObject_GetItem( pDS.get(),
			PyUnicode_FromString( "child1" ) );
		CHECK( pChild );
		CHECK( PyDataSection::Check( pChild ) );
		CHECK_EQUAL( BW::string( "hello" ),
			attributeString( pChild, "asString" ) );
		Py_DECREF( pChild );

		pChild = PyObject_GetItem( pDS.get(),
			PyUnicode_FromString( "nonexistent" ) );
		CHECK( pChild == Py_None );
	}

	// child()/childName() address children by index.
	{
		PyObject * pChildName = callMethod( pDS.get(), "childName",
			Py_BuildValue( "(i)", 0 ) );
		CHECK( pChildName );
		CHECK_EQUAL( BW::string( "child1" ),
			BW::string( PyUnicode_AsUTF8( pChildName ) ) );
		Py_DECREF( pChildName );

		PyObject * pChild = callMethod( pDS.get(), "child",
			Py_BuildValue( "(i)", 0 ) );
		CHECK( pChild );
		CHECK( PyDataSection::Check( pChild ) );
		Py_DECREF( pChild );
	}

	// values() has one entry per child, items() pairs each with its name.
	{
		PyObject * pValues = callMethodNoArgs( pDS.get(), "values" );
		CHECK( pValues );
		CHECK_EQUAL( 9, PyObject_Size( pValues ) );
		Py_DECREF( pValues );

		PyObject * pItems = callMethodNoArgs( pDS.get(), "items" );
		CHECK( pItems );
		CHECK_EQUAL( 9, PyObject_Size( pItems ) );

		PyObject * pFirst = PySequence_GetItem( pItems, 0 );
		CHECK( pFirst );
		CHECK( PyTuple_Check( pFirst ) );
		CHECK_EQUAL( 2, PyTuple_Size( pFirst ) );
		Py_DECREF( pFirst );
		Py_DECREF( pItems );
	}
}


// -----------------------------------------------------------------------------
// Section: Typed reads
// -----------------------------------------------------------------------------

// read<Type> methods interpret child section values, honouring defaults
// and reading repeated children as tuples.
TEST_F( PyScriptUnitTestHarness, PyDataSection_readTypedValues )
{
	DataSectionPtr pSection = loadTestSection();
	CHECK( pSection );

	PyDataSectionPtr pDS( new PyDataSection( pSection ), true );

	// Strings, with and without a default.
	{
		PyObject * pResult = callMethod( pDS.get(), "readString",
			Py_BuildValue( "(s)", "child1" ) );
		CHECK( pResult );
		CHECK_EQUAL( BW::string( "hello" ),
			BW::string( PyUnicode_AsUTF8( pResult ) ) );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readString",
			Py_BuildValue( "(ss)", "missing", "fallback" ) );
		CHECK( pResult );
		CHECK_EQUAL( BW::string( "fallback" ),
			BW::string( PyUnicode_AsUTF8( pResult ) ) );
		Py_DECREF( pResult );
	}

	// Numbers and bools.
	{
		PyObject * pResult = callMethod( pDS.get(), "readInt",
			Py_BuildValue( "(s)", "child2" ) );
		CHECK( pResult );
		CHECK_EQUAL( 42, PyLong_AsLong( pResult ) );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readFloat",
			Py_BuildValue( "(s)", "child3" ) );
		CHECK( pResult );
		CHECK_CLOSE( 3.5, PyFloat_AsDouble( pResult ), 0.0001 );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readBool",
			Py_BuildValue( "(s)", "child4" ) );
		CHECK( pResult );
		CHECK( pResult == Py_True );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readInt64",
			Py_BuildValue( "(s)", "big" ) );
		CHECK( pResult );
		CHECK_EQUAL( 4294967296LL, PyLong_AsLongLong( pResult ) );
		Py_DECREF( pResult );
	}

	// Vectors.
	{
		PyObject * pResult = callMethod( pDS.get(), "readVector2",
			Py_BuildValue( "(s)", "vector2" ) );
		CHECK( pResult );
		CHECK_EQUAL( 2, PyObject_Size( pResult ) );
		CHECK_CLOSE( 1.5, PyFloat_AsDouble( PySequence_GetItem( pResult, 0 ) ),
			0.0001 );
		CHECK_CLOSE( 2.5, PyFloat_AsDouble( PySequence_GetItem( pResult, 1 ) ),
			0.0001 );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readVector3",
			Py_BuildValue( "(s)", "vector3" ) );
		CHECK( pResult );
		CHECK_EQUAL( 3, PyObject_Size( pResult ) );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readVector4",
			Py_BuildValue( "(s)", "vector4" ) );
		CHECK( pResult );
		CHECK_EQUAL( 4, PyObject_Size( pResult ) );
		Py_DECREF( pResult );
	}

	// Plural reads return tuples with one entry per matching child.
	{
		PyObject * pResult = callMethod( pDS.get(), "readStrings",
			Py_BuildValue( "(s)", "alist/item" ) );
		CHECK( pResult );
		CHECK( PyTuple_Check( pResult ) );
		CHECK_EQUAL( 3, PyTuple_Size( pResult ) );
		CHECK_EQUAL( BW::string( "1" ),
			BW::string( PyUnicode_AsUTF8( PyTuple_GetItem( pResult, 0 ) ) ) );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readInts",
			Py_BuildValue( "(s)", "alist/item" ) );
		CHECK( pResult );
		CHECK_EQUAL( 3, PyTuple_Size( pResult ) );
		CHECK_EQUAL( 1, PyLong_AsLong( PyTuple_GetItem( pResult, 0 ) ) );
		CHECK_EQUAL( 3, PyLong_AsLong( PyTuple_GetItem( pResult, 2 ) ) );
		Py_DECREF( pResult );
	}

	// The as<Type> accessors on a child section.
	{
		PyObject * pChild = PyObject_GetItem( pDS.get(),
			PyUnicode_FromString( "child3" ) );
		CHECK( pChild );
		CHECK_CLOSE( 3.5, PyFloat_AsDouble(
			PyObject_GetAttrString( pChild, "asFloat" ) ), 0.0001 );
		Py_DECREF( pChild );

		pChild = PyObject_GetItem( pDS.get(),
			PyUnicode_FromString( "child4" ) );
		CHECK( pChild );
		PyObject * pBool = PyObject_GetAttrString( pChild, "asBool" );
		CHECK( pBool );
		CHECK( pBool == Py_True );
		Py_DECREF( pBool );
		Py_DECREF( pChild );
	}

	// A clean name reports as such.
	{
		PyObject * pChild = PyObject_GetItem( pDS.get(),
			PyUnicode_FromString( "child1" ) );
		CHECK( pChild );

		PyObject * pClean = callMethodNoArgs( pChild, "isNameClean" );
		CHECK( pClean );
		CHECK( pClean == Py_True );
		Py_DECREF( pClean );
		Py_DECREF( pChild );
	}
}


// -----------------------------------------------------------------------------
// Section: Writes and mutation
// -----------------------------------------------------------------------------

// Writes mutate the in-memory section, createSection/deleteSection/copy
// manage children, and nothing is persisted back to the res file.
TEST_F( PyScriptUnitTestHarness, PyDataSection_writeAndMutate )
{
	DataSectionPtr pSection = loadTestSection();
	CHECK( pSection );

	PyDataSectionPtr pDS( new PyDataSection( pSection ), true );

	// Typed writes round trip in memory.
	{
		PyObject * pResult = callMethod( pDS.get(), "writeString",
			Py_BuildValue( "(ss)", "writestr", "goodbye" ) );
		CHECK( pResult );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readString",
			Py_BuildValue( "(s)", "writestr" ) );
		CHECK( pResult );
		CHECK_EQUAL( BW::string( "goodbye" ),
			BW::string( PyUnicode_AsUTF8( pResult ) ) );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "writeInt",
			Py_BuildValue( "(si)", "writeint", 7 ) );
		CHECK( pResult );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readInt",
			Py_BuildValue( "(s)", "writeint" ) );
		CHECK( pResult );
		CHECK_EQUAL( 7, PyLong_AsLong( pResult ) );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "writeFloat",
			Py_BuildValue( "(sd)", "writefloat", 1.25 ) );
		CHECK( pResult );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readFloat",
			Py_BuildValue( "(s)", "writefloat" ) );
		CHECK( pResult );
		CHECK_CLOSE( 1.25, PyFloat_AsDouble( pResult ), 0.0001 );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "writeBool",
			Py_BuildValue( "(sO)", "writebool", Py_False ) );
		CHECK( pResult );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readBool",
			Py_BuildValue( "(s)", "writebool" ) );
		CHECK( pResult );
		CHECK( pResult == Py_False );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "writeBlob",
			Py_BuildValue( "(ss)", "writeblob", "blobdata" ) );
		CHECK( pResult );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readBlob",
			Py_BuildValue( "(s)", "writeblob" ) );
		CHECK( pResult );
		CHECK_EQUAL( BW::string( "blobdata" ),
			BW::string( PyUnicode_AsUTF8( pResult ) ) );
		Py_DECREF( pResult );
	}

	// Plural writes create one subsection per sequence element.
	{
		PyObject * pResult = callMethod( pDS.get(), "writeStrings",
			Py_BuildValue( "(sO)", "writelist",
				Py_BuildValue( "(ss)", "a", "b" ) ) );
		CHECK( pResult );
		Py_DECREF( pResult );

		// writeStrings() creates one child named after the tag per value;
		// readStrings() collects the children matching the final tag.
		pResult = callMethod( pDS.get(), "readStrings",
			Py_BuildValue( "(s)", "writelist" ) );
		CHECK( pResult );
		CHECK( PyTuple_Check( pResult ) );
		CHECK_EQUAL( 2, PyTuple_Size( pResult ) );
		Py_DECREF( pResult );
	}

	// write() dispatches on the Python value type.
	{
		PyObject * pResult = callMethod( pDS.get(), "write",
			Py_BuildValue( "(si)", "autoint", 99 ) );
		CHECK( pResult );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readInt",
			Py_BuildValue( "(s)", "autoint" ) );
		CHECK( pResult );
		CHECK_EQUAL( 99, PyLong_AsLong( pResult ) );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "write",
			Py_BuildValue( "(ss)", "autostr", "text" ) );
		CHECK( pResult );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readString",
			Py_BuildValue( "(s)", "autostr" ) );
		CHECK( pResult );
		CHECK_EQUAL( BW::string( "text" ),
			BW::string( PyUnicode_AsUTF8( pResult ) ) );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "write",
			Py_BuildValue( "(sO)", "autovec3",
				Py_BuildValue( "(ddd)", 1.0, 2.0, 3.0 ) ) );
		CHECK( pResult );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readVector3",
			Py_BuildValue( "(s)", "autovec3" ) );
		CHECK( pResult );
		CHECK_EQUAL( 3, PyObject_Size( pResult ) );
		Py_DECREF( pResult );
	}

	// Wide strings round trip.
	{
		PyObject * pResult = callMethod( pDS.get(), "writeWideString",
			Py_BuildValue( "(sN)", "writewide", PyUnicode_FromString( "wide" ) ) );
		CHECK( pResult );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readWideString",
			Py_BuildValue( "(s)", "writewide" ) );
		CHECK( pResult );
		CHECK( PyUnicode_Check( pResult ) );
		CHECK_EQUAL( BW::string( "wide" ),
			BW::string( PyUnicode_AsUTF8( pResult ) ) );
		Py_DECREF( pResult );
	}

	// createSection always makes a new child; createSectionFromString
	// parses an XML snippet.
	{
		PyObject * pCreated = callMethod( pDS.get(), "createSection",
			Py_BuildValue( "(s)", "created/newchild" ) );
		CHECK( pCreated );
		CHECK( PyDataSection::Check( pCreated ) );
		Py_DECREF( pCreated );

		PyObject * pHas = callMethod( pDS.get(), "has_key",
			Py_BuildValue( "(s)", "created/newchild" ) );
		CHECK( pHas );
		CHECK( pHas == Py_True );
		Py_DECREF( pHas );

		pCreated = callMethod( pDS.get(), "createSectionFromString",
			Py_BuildValue( "(s)", "<fromstr><x>1</x></fromstr>" ) );
		CHECK( pCreated );
		CHECK( PyDataSection::Check( pCreated ) );
		Py_DECREF( pCreated );
	}

	// deleteSection works with a path or a PyDataSection, and reports
	// failures for missing paths.
	{
		PyObject * pResult = callMethod( pDS.get(), "deleteSection",
			Py_BuildValue( "(s)", "child3" ) );
		CHECK( pResult );
		CHECK( PyObject_IsTrue( pResult ) );
		Py_DECREF( pResult );

		PyObject * pHas = callMethod( pDS.get(), "has_key",
			Py_BuildValue( "(s)", "child3" ) );
		CHECK( pHas );
		CHECK( pHas == Py_False );
		Py_DECREF( pHas );

		pResult = callMethod( pDS.get(), "deleteSection",
			Py_BuildValue( "(s)", "nonexistent" ) );
		CHECK( pResult );
		CHECK( !PyObject_IsTrue( pResult ) );
		Py_DECREF( pResult );
	}

	// copy() adopts another section's contents into this one.
	{
		PyObject * pResMgr = PyImport_ImportModule( "ResMgr" );
		CHECK( pResMgr );

		PyObject * pSource = callMethod( pResMgr, "DataSection",
			Py_BuildValue( "(s)", "copied" ) );
		CHECK( pSource );
		CHECK( PyDataSection::Check( pSource ) );
		Py_DECREF( pResMgr );

		PyObject * pResult = callMethod( pSource, "writeString",
			Py_BuildValue( "(ss)", "copieditem", "copiedvalue" ) );
		CHECK( pResult );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "copy", PyTuple_Pack( 1, pSource ) );
		CHECK( pResult );
		Py_DECREF( pResult );

		pResult = callMethod( pDS.get(), "readString",
			Py_BuildValue( "(s)", "copieditem" ) );
		CHECK( pResult );
		CHECK_EQUAL( BW::string( "copiedvalue" ),
			BW::string( PyUnicode_AsUTF8( pResult ) ) );
		Py_DECREF( pResult );

		Py_DECREF( pSource );
	}
}


// -----------------------------------------------------------------------------
// Section: Factory
// -----------------------------------------------------------------------------

// ResMgr.DataSection() creates a brand new empty section, either unnamed
// or with the given root tag.
TEST_F( PyScriptUnitTestHarness, PyDataSection_factoryCreatesEmptySection )
{
	PyObject * pResMgr = PyImport_ImportModule( "ResMgr" );
	CHECK( pResMgr );

	PyObject * pEmpty = callMethodNoArgs( pResMgr, "DataSection" );
	CHECK( pEmpty );
	CHECK( PyDataSection::Check( pEmpty ) );
	CHECK_EQUAL( 0, PyObject_Size( pEmpty ) );
	Py_DECREF( pEmpty );

	PyObject * pNamed = callMethod( pResMgr, "DataSection",
		Py_BuildValue( "(s)", "tagged" ) );
	CHECK( pNamed );
	CHECK( PyDataSection::Check( pNamed ) );

	PyObject * pName = PyObject_GetAttrString( pNamed, "name" );
	CHECK( pName );
	CHECK_EQUAL( BW::string( "tagged" ),
		BW::string( PyUnicode_AsUTF8( pName ) ) );
	Py_DECREF( pName );
	Py_DECREF( pNamed );

	Py_DECREF( pResMgr );
}

} // namespace BW

// test_py_data_section.cpp
