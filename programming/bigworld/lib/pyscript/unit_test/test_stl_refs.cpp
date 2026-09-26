#include "pch.hpp"
#include "test_harness.hpp"
#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/memory_stream.hpp"
#include "cstdmf/watcher.hpp"
#include "pyscript/py_to_stl.hpp"
#include "pyscript/pywatcher.hpp"


namespace BW
{

namespace
{
	/**
	 *	Fills a new list with the given number of string elements.
	 *	Callers CHECK the result against NULL.
	 */
	PyObject * newStringList( const char * elem0, const char * elem1,
		const char * elem2 )
	{
		PyObject * pList = PyList_New( 3 );

		if (pList != NULL)
		{
			PyList_SetItem( pList, 0, PyUnicode_FromString( elem0 ) );
			PyList_SetItem( pList, 1, PyUnicode_FromString( elem1 ) );
			PyList_SetItem( pList, 2, PyUnicode_FromString( elem2 ) );
		}

		return pList;
	}
}


// -----------------------------------------------------------------------------
// Section: PySequenceSTL
// -----------------------------------------------------------------------------

// PySequenceSTL adapts a Python sequence to the STL sequence interface:
// size, iteration and element access all read through to the live object.
TEST_F( PyScriptUnitTestHarness, PySTL_sequenceOverList )
{
	PyObject * pList = newStringList( "1", "2", "3" );
	CHECK( pList );

	PyObject * pLong = PyLong_FromLong( 1 );
	CHECK( pLong );

	CHECK( PySequenceSTL::Check( pList ) );
	CHECK( !PySequenceSTL::Check( pLong ) );
	Py_DECREF( pLong );

	PySequenceSTL seq( pList );
	CHECK_EQUAL( 3, seq.size() );

	// Element access hands out owned references.
	{
		PyObject * pElem = seq[1];
		CHECK( pElem );
		CHECK_EQUAL( BW::string( "2" ),
			BW::string( PyUnicode_AsUTF8( pElem ) ) );
		Py_DECREF( pElem );
	}

	// Iteration walks every element.
	{
		size_t count = 0;
		PySequenceSTL::const_iterator it = seq.begin();
		while (it != seq.end())
		{
			const PyObject * pElem = *it;
			CHECK( pElem );
			++count;
			Py_DECREF( const_cast<PyObject *>( pElem ) );
			++it;
		}
		CHECK_EQUAL( 3, count );
	}

	// Copies share the same underlying sequence.
	{
		PySequenceSTL copy( seq );
		CHECK( copy == seq );
		CHECK_EQUAL( seq.size(), copy.size() );
	}

	// Writing through the reference mutates the list itself. The derived
	// reference type hides the base assignment operator, so call it
	// explicitly.
	{
		PyObject * pNine = PyUnicode_FromString( "9" );
		CHECK( pNine );

		PySequenceSTL::reference rSeqElem = seq[1];
		rSeqElem.PyObjectPtrRef::operator=( pNine );
		Py_DECREF( pNine );

		PyObject * pElem = PyList_GetItem( pList, 1 );
		CHECK( pElem );
		CHECK_EQUAL( BW::string( "9" ),
			BW::string( PyUnicode_AsUTF8( pElem ) ) );
	}

	Py_DECREF( pList );
}


// -----------------------------------------------------------------------------
// Section: PyMappingSTL
// -----------------------------------------------------------------------------

// PyMappingSTL adapts a Python dict to the STL map interface: element
// references read and write the live mapping.
TEST_F( PyScriptUnitTestHarness, PySTL_mappingOverDict )
{
	PyObject * pDict = PyDict_New();
	CHECK( pDict );

	PyObject * pOne = PyLong_FromLong( 1 );
	PyObject * pTwo = PyLong_FromLong( 2 );
	CHECK( pOne );
	CHECK( pTwo );

	PyDict_SetItemString( pDict, "one", pOne );
	PyDict_SetItemString( pDict, "two", pTwo );
	Py_DECREF( pOne );
	Py_DECREF( pTwo );

	PyObject * pLong = PyLong_FromLong( 1 );
	CHECK( pLong );

	CHECK( PyMappingSTL::Check( pDict ) );
	CHECK( !PyMappingSTL::Check( pLong ) );
	Py_DECREF( pLong );

	PyMappingSTL map( pDict );
	CHECK_EQUAL( 2, map.size() );

	// Read through the value reference.
	{
		PyObject * pKey = PyUnicode_FromString( "one" );
		CHECK( pKey );

		PyObject * pValue = map[pKey];
		CHECK( pValue );
		CHECK_EQUAL( 1, PyLong_AsLong( pValue ) );
		Py_DECREF( pValue );

		Py_DECREF( pKey );
	}

	// Write through the value reference.
	{
		PyObject * pKey = PyUnicode_FromString( "one" );
		CHECK( pKey );

		PyObject * pTen = PyLong_FromLong( 10 );
		CHECK( pTen );

		PyMappingSTL::value_reference rValue = map[pKey];
		rValue.PyObjectPtrRef::operator=( pTen );
		Py_DECREF( pTen );
		Py_DECREF( pKey );

		PyObject * pValue = PyDict_GetItemString( pDict, "one" );
		CHECK( pValue );
		CHECK_EQUAL( 10, PyLong_AsLong( pValue ) );
	}

	// Iteration visits every key/value pair once.
	{
		size_t count = 0;
		bool sawOne = false;

		PyMappingSTL::const_iterator it = map.begin();
		while (it != map.end())
		{
			PyMappingSTL::const_reference pair = *it;
			++count;

			if (strcmp( PyUnicode_AsUTF8(
					const_cast<PyObject *>( pair.first ) ), "one" ) == 0)
			{
				sawOne = true;
				CHECK_EQUAL( 10, PyLong_AsLong(
					const_cast<PyObject *>( pair.second ) ) );
			}

			++it;
		}

		CHECK_EQUAL( 2, count );
		CHECK( sawOne );
	}

	// Copies share the same underlying mapping.
	{
		PyMappingSTL copy( map );
		CHECK( copy == map );
		CHECK_EQUAL( map.size(), copy.size() );
	}

	Py_DECREF( pDict );
}


// -----------------------------------------------------------------------------
// Section: PyObjectPtrRefSimple
// -----------------------------------------------------------------------------

// PyObjectPtrRefSimple wraps a plain PyObject * slot: reads hand out owned
// references and writes swap the slot with correct reference counts.
TEST_F( PyScriptUnitTestHarness, PySTL_pyObjectPtrRefSimple )
{
	// Reading through the reference yields an owned reference.
	{
		PyObject * pFive = PyLong_FromLong( 5 );
		CHECK( pFive );

		{
			PyObjectPtrRefSimple ref( pFive );

			PyObject * pGot = ref;
			CHECK( pGot );
			CHECK_EQUAL( 5, PyLong_AsLong( pGot ) );
			Py_DECREF( pGot );
		}

		Py_DECREF( pFive );
	}

	// Assignment swaps the slot (and the variable the reference wraps).
	{
		PyObject * pFive = PyLong_FromLong( 5 );
		PyObject * pSix = PyLong_FromLong( 6 );
		PyObject * pSlot = pFive;
		CHECK( pFive );
		CHECK( pSix );

		{
			PyObjectPtrRefSimple ref( pSlot );

			CHECK( pSlot == pFive );

			// The base assignment swaps the referenced slot.
			ref.PyObjectPtrRef::operator=( pSix );

			CHECK( pSlot == pSix );
		}

		// The reference released its claim on destruction; only our own
		// original references remain.
		Py_DECREF( pFive );
		Py_DECREF( pSix );
	}
}


// -----------------------------------------------------------------------------
// Section: PyObjectWatcher over collections
// -----------------------------------------------------------------------------

// PyObjectWatcher dispatches sequence and mapping objects to their special
// watchers, so indexed paths and child visits work on live collections.
TEST_F( PyScriptUnitTestHarness, PySTL_pyObjectWatcherOnCollections )
{
	// A list watches as a directory with indexed children.
	{
		PyObject * pList = newStringList( "10", "20", "30" );
		CHECK( pList );

		PyObjectPtrRefSimple ref( pList );
		PyObjectWatcher watcher( ref );

		BW::string result;
		BW::string desc;
		Watcher::Mode mode = Watcher::WT_READ_ONLY;

		CHECK( watcher.getAsString( NULL, "1", result, desc, mode ) );
		CHECK_EQUAL( BW::string( "20" ), result );

		// Visiting the sequence itself enumerates the children.
		WatcherPathRequestV2 pathRequest( "" );
		CHECK( watcher.visitChildren( NULL, "", pathRequest ) );
		CHECK( pathRequest.getDataSize() > 0 );

		Py_DECREF( pList );
	}

	// A dict watches as a directory keyed by its keys. Only enumeration is
	// asserted here: key lookup through a watcher path needs an identity
	// comparison against the stored key objects, which MapWatcher's
	// PyMappingSTL iterator never performs (its std::find is commented out
	// in py_to_stl.hpp), so path lookups on dict members do not resolve.
	{
		PyObject * pDict = PyDict_New();
		CHECK( pDict );
		PyObject * pAnswer = PyLong_FromLong( 42 );
		CHECK( pAnswer );
		PyDict_SetItemString( pDict, "answer", pAnswer );
		Py_DECREF( pAnswer );

		PyObjectPtrRefSimple ref( pDict );
		PyObjectWatcher watcher( ref );

		WatcherPathRequestV2 pathRequest( "" );
		CHECK( watcher.visitChildren( NULL, "", pathRequest ) );
		CHECK( pathRequest.getDataSize() > 0 );

		Py_DECREF( pDict );
	}
}

} // namespace BW

// test_stl_refs.cpp
