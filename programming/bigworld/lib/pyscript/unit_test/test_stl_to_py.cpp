#include "pch.hpp"
#include "test_harness.hpp"

#include "cstdmf/bw_vector.hpp"
#include "pyscript/script.hpp"
#include "pyscript/stl_to_py.hpp"


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

typedef BW::vector< int > IntVector;
typedef PySTLSequenceHolder< IntVector > IntVectorHolder;


/**
 *	Fetches the exception type currently raised as a string and consumes
 *	the error, so a pending exception never leaks into the next check.
 *	Returns an empty string if no exception is set.
 */
BW::string currentExceptionType()
{
	if (!PyErr_Occurred())
	{
		return BW::string();
	}

	PyObject * pType = NULL;
	PyObject * pValue = NULL;
	PyObject * pTraceback = NULL;
	PyErr_Fetch( &pType, &pValue, &pTraceback );

	BW::string result( (pType != NULL) ?
		((PyTypeObject*)pType)->tp_name : "" );

	Py_XDECREF( pType );
	Py_XDECREF( pValue );
	Py_XDECREF( pTraceback );

	return result;
}


/**
 *	Reads a long from a Python object; returns false if it isn't one.
 */
bool isLong( PyObject * pObject, long value )
{
	return (pObject != NULL) && PyLong_Check( pObject ) &&
		(PyLong_AsLong( pObject ) == value);
}


/**
 *	Builds a new PyLong-owned list [first, second].
 */
PyObject * newIntList( long first, long second )
{
	PyObject * pList = PyList_New( 2 );
	PyList_SetItem( pList, 0, PyLong_FromLong( first ) );
	PyList_SetItem( pList, 1, PyLong_FromLong( second ) );
	return pList;
}

} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: PySTLSequence read paths
// -----------------------------------------------------------------------------

// The sequence exposes the wrapped vector's size and items through the
// standard sequence protocol, contains() converts the needle to the item
// type, and concat/repeat produce plain lists.
TEST_F( PyScriptUnitTestHarness, PySTLSequence_readOps )
{
	IntVector ints;
	ints.push_back( 10 );
	ints.push_back( 20 );
	ints.push_back( 30 );

	IntVectorHolder holder( ints, NULL, /* writable */ true );
	PyObjectPtr pSeq( Script::getData( holder ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pSeq.get() != NULL );

	CHECK_EQUAL( 3, int( PySequence_Size( pSeq.get() ) ) );

	// Items convert through Script::getData.
	PyObjectPtr pItem( PySequence_GetItem( pSeq.get(), 0 ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( isLong( pItem.get(), 10 ) );

	PyObjectPtr pLast( PySequence_GetItem( pSeq.get(), 2 ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( isLong( pLast.get(), 30 ) );

	// Negative indices are normalised by the protocol layer
	// (PySequence_GetItem adds the length), not by the slot itself.
	PyObjectPtr pNeg( PySequence_GetItem( pSeq.get(), -1 ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( isLong( pNeg.get(), 30 ) );

	// Truly out of range raises IndexError.
	PyErr_Clear();
	CHECK( PySequence_GetItem( pSeq.get(), 3 ) == NULL );
	CHECK_EQUAL( BW::string( "IndexError" ), currentExceptionType() );

	// contains() converts the needle; a needle of the wrong type is
	// silently "not contained".
	PyObjectPtr pTwenty( PyLong_FromLong( 20 ), PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( 1, PySequence_Contains( pSeq.get(), pTwenty.get() ) );

	PyObjectPtr pMissing( PyLong_FromLong( 99 ), PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( 0, PySequence_Contains( pSeq.get(), pMissing.get() ) );

	CHECK( !PyErr_Occurred() );
	PyObjectPtr pWrongType( PyUnicode_FromString( "not an int" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( 0, PySequence_Contains( pSeq.get(), pWrongType.get() ) );
	CHECK( !PyErr_Occurred() );

	// Concatenation and repeat yield plain lists and don't touch the vector.
	PyObjectPtr pOther( newIntList( 7, 8 ), PyObjectPtr::STEAL_REFERENCE );

	PyObjectPtr pConcat( PySequence_Concat( pSeq.get(), pOther.get() ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( 5, int( PyList_Size( pConcat.get() ) ) );
	CHECK( isLong( PyList_GetItem( pConcat.get(), 3 ), 7 ) );
	CHECK_EQUAL( 3, int( ints.size() ) );

	PyObjectPtr pRepeat( PySequence_Repeat( pSeq.get(), 2 ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( 6, int( PyList_Size( pRepeat.get() ) ) );

	PyObjectPtr pZeroed( PySequence_Repeat( pSeq.get(), 0 ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( 0, int( PyList_Size( pZeroed.get() ) ) );

	PyObjectPtr pNegative( PySequence_Repeat( pSeq.get(), -2 ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( 0, int( PyList_Size( pNegative.get() ) ) );

	// The length attribute agrees with the protocol.
	PyObjectPtr pLength( PyObject_GetAttrString( pSeq.get(), "length" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( isLong( pLength.get(), 3 ) );

	// The repr renders like a list.
	PyObjectPtr pRepr( PyObject_Repr( pSeq.get() ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( BW::string( "[10, 20, 30]" ),
		BW::string( PyUnicode_AsUTF8( pRepr.get() ) ) );
}


// -----------------------------------------------------------------------------
// Section: PySTLSequence write paths
// -----------------------------------------------------------------------------

// Assignment through the protocol replaces the item in the underlying
// vector; in-place concatenation and repeat mutate it.
TEST_F( PyScriptUnitTestHarness, PySTLSequence_writeOps )
{
	IntVector ints;
	ints.push_back( 10 );
	ints.push_back( 20 );
	ints.push_back( 30 );

	IntVectorHolder holder( ints, NULL, /* writable */ true );
	PyObjectPtr pSeq( Script::getData( holder ),
		PyObjectPtr::STEAL_REFERENCE );

	// x[i] = v goes through erase + insertRange + insert + commit.
	PyObjectPtr pNine( PyLong_FromLong( 9 ), PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( 0, PySequence_SetItem( pSeq.get(), 1, pNine.get() ) );
	CHECK_EQUAL( size_t( 3 ), ints.size() );
	CHECK_EQUAL( 10, ints[ 0 ] );
	CHECK_EQUAL( 9, ints[ 1 ] );
	CHECK_EQUAL( 30, ints[ 2 ] );

	// Negative assignments are normalised by the protocol layer too:
	// x[-1] = 9 replaces the last item.
	CHECK_EQUAL( 0, PySequence_SetItem( pSeq.get(), -1, pNine.get() ) );
	CHECK_EQUAL( size_t( 3 ), ints.size() );
	CHECK_EQUAL( 9, ints[ 2 ] );

	// Truly out of range assignments raise IndexError.
	PyErr_Clear();
	CHECK_EQUAL( -1, PySequence_SetItem( pSeq.get(), 5, pNine.get() ) );
	CHECK_EQUAL( BW::string( "IndexError" ), currentExceptionType() );
	CHECK_EQUAL( size_t( 3 ), ints.size() );

	// x += y extends the vector in place and hands back the same object.
	PyObjectPtr pOther( newIntList( 7, 8 ), PyObjectPtr::STEAL_REFERENCE );
	PyObject * pConcatRet = PySequence_InPlaceConcat( pSeq.get(),
		pOther.get() );
	CHECK( pConcatRet == pSeq.get() );
	Py_DECREF( pConcatRet );
	CHECK_EQUAL( size_t( 5 ), ints.size() );
	CHECK_EQUAL( 7, ints[ 3 ] );
	CHECK_EQUAL( 8, ints[ 4 ] );

	// x *= n repeats the contents in place.
	PyObject * pRepeatRet = PySequence_InPlaceRepeat( pSeq.get(), 2 );
	CHECK( pRepeatRet == pSeq.get() );
	Py_DECREF( pRepeatRet );
	CHECK_EQUAL( size_t( 10 ), ints.size() );
	CHECK_EQUAL( 10, ints[ 0 ] );
	CHECK_EQUAL( 8, ints[ 9 ] );

	// x *= 0 (or any non-positive n) clears the sequence.
	pRepeatRet = PySequence_InPlaceRepeat( pSeq.get(), 0 );
	CHECK( pRepeatRet == pSeq.get() );
	Py_DECREF( pRepeatRet );
	CHECK( ints.empty() );
}


// A failed insert (item not convertible to the item type) cancels the
// whole commit group and leaves the vector untouched.
TEST_F( PyScriptUnitTestHarness, PySTLSequence_failedInsertCancels )
{
	IntVector ints;
	ints.push_back( 10 );
	ints.push_back( 20 );
	ints.push_back( 30 );

	IntVectorHolder holder( ints, NULL, /* writable */ true );
	PyObjectPtr pSeq( Script::getData( holder ),
		PyObjectPtr::STEAL_REFERENCE );

	// x += [7, "bad", 8]: the second insert fails, so the whole +=
	// cancels.
	PyObjectPtr pBadList( PyList_New( 3 ), PyObjectPtr::STEAL_REFERENCE );
	PyList_SetItem( pBadList.get(), 0, PyLong_FromLong( 7 ) );
	PyList_SetItem( pBadList.get(), 1, PyUnicode_FromString( "bad" ) );
	PyList_SetItem( pBadList.get(), 2, PyLong_FromLong( 8 ) );

	PyErr_Clear();
	CHECK( PySequence_InPlaceConcat( pSeq.get(), pBadList.get() ) == NULL );
	CHECK( PyErr_Occurred() );
	PyErr_Clear();
	CHECK_EQUAL( size_t( 3 ), ints.size() );
	CHECK_EQUAL( 10, ints[ 0 ] );
	CHECK_EQUAL( 20, ints[ 1 ] );
	CHECK_EQUAL( 30, ints[ 2 ] );

	// The sequence still reads fine after the cancel.
	PyObjectPtr pItem( PySequence_GetItem( pSeq.get(), 1 ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( isLong( pItem.get(), 20 ) );

	// x[i] = None also fails (None is not an int) and leaves the old item
	// in place.
	PyErr_Clear();
	CHECK_EQUAL( -1, PySequence_SetItem( pSeq.get(), 1, Py_None ) );
	PyErr_Clear();
	CHECK_EQUAL( size_t( 3 ), ints.size() );
	CHECK_EQUAL( 20, ints[ 1 ] );
}


// A read-only holder rejects every write path with a TypeError.
TEST_F( PyScriptUnitTestHarness, PySTLSequence_readOnlyRejections )
{
	IntVector ints;
	ints.push_back( 10 );
	ints.push_back( 20 );

	IntVectorHolder holder( ints, NULL, /* writable */ false );
	CHECK( !holder.writable() );
	CHECK( holder.pOwner() == NULL );

	PyObjectPtr pSeq( Script::getData( holder ),
		PyObjectPtr::STEAL_REFERENCE );

	PyObjectPtr pNine( PyLong_FromLong( 9 ), PyObjectPtr::STEAL_REFERENCE );

	PyErr_Clear();
	CHECK_EQUAL( -1, PySequence_SetItem( pSeq.get(), 0, pNine.get() ) );
	CHECK_EQUAL( BW::string( "TypeError" ), currentExceptionType() );

	PyObjectPtr pOther( newIntList( 7, 8 ), PyObjectPtr::STEAL_REFERENCE );

	PyErr_Clear();
	CHECK( PySequence_InPlaceConcat( pSeq.get(), pOther.get() ) == NULL );
	CHECK_EQUAL( BW::string( "TypeError" ), currentExceptionType() );

	PyErr_Clear();
	CHECK( PySequence_InPlaceRepeat( pSeq.get(), 2 ) == NULL );
	CHECK_EQUAL( BW::string( "TypeError" ), currentExceptionType() );

	// Reads still work on the read-only sequence.
	CHECK_EQUAL( 2, int( PySequence_Size( pSeq.get() ) ) );
	CHECK_EQUAL( size_t( 2 ), ints.size() );
}


// Script::setData replaces the whole vector contents from a Python
// sequence; same-holder assignment is a no-op and bad inputs are rejected.
TEST_F( PyScriptUnitTestHarness, PySTLSequence_setDataOverwrite )
{
	IntVector ints;
	ints.push_back( 1 );
	ints.push_back( 2 );
	ints.push_back( 3 );

	IntVectorHolder holder( ints, NULL, /* writable */ true );
	PyObjectPtr pSeq( Script::getData( holder ),
		PyObjectPtr::STEAL_REFERENCE );

	// Overwrite from a plain list.
	PyObjectPtr pList( newIntList( 7, 8 ), PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( 0, Script::setData( pList.get(), holder, "ints" ) );
	CHECK_EQUAL( size_t( 2 ), ints.size() );
	CHECK_EQUAL( 7, ints[ 0 ] );
	CHECK_EQUAL( 8, ints[ 1 ] );

	// Assigning our own sequence is a no-op (it would otherwise erase
	// elements then try to read them back).
	CHECK_EQUAL( 0, Script::setData( pSeq.get(), holder, "ints" ) );
	CHECK_EQUAL( size_t( 2 ), ints.size() );

	// A second PySTLSequence over the same holder is the same case.
	PyObjectPtr pSeq2( Script::getData( holder ), PyObjectPtr::STEAL_REFERENCE );
	CHECK( PySTLSequence::Check( pSeq2.get() ) );
	CHECK_EQUAL( 0, Script::setData( pSeq2.get(), holder, "ints" ) );
	CHECK_EQUAL( size_t( 2 ), ints.size() );

	// Non-sequences are rejected.
	PyErr_Clear();
	PyObjectPtr pNotAList( PyLong_FromLong( 42 ), PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( -1, Script::setData( pNotAList.get(), holder, "ints" ) );
	CHECK_EQUAL( BW::string( "TypeError" ), currentExceptionType() );
	CHECK_EQUAL( size_t( 2 ), ints.size() );

	// Read-only holders are rejected too.
	IntVector otherInts;
	otherInts.push_back( 5 );
	IntVectorHolder roHolder( otherInts, NULL, /* writable */ false );
	CHECK_EQUAL( -1, Script::setData( pList.get(), roHolder, "otherInts" ) );
	CHECK_EQUAL( size_t( 1 ), otherInts.size() );

	// A list that contains unconvertible items cancels wholesale.
	PyObjectPtr pBadList( PyList_New( 2 ), PyObjectPtr::STEAL_REFERENCE );
	PyList_SetItem( pBadList.get(), 0, PyLong_FromLong( 9 ) );
	PyList_SetItem( pBadList.get(), 1, PyUnicode_FromString( "bad" ) );

	PyErr_Clear();
	CHECK_EQUAL( -1, Script::setData( pBadList.get(), holder, "ints" ) );
	PyErr_Clear();
	CHECK_EQUAL( size_t( 2 ), ints.size() );
	CHECK_EQUAL( 7, ints[ 0 ] );
}


BW_END_NAMESPACE

// test_stl_to_py.cpp
