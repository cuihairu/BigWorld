#include "pch.hpp"
#include "test_harness.hpp"

#include "pyscript/script.hpp"
#include "pyscript/script_events.hpp"


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	Evaluates the given Python source in the __main__ module namespace.
 *	Prints the traceback and returns false on error.
 */
bool runPython( const char * source )
{
	PyObject * pMain = PyImport_AddModule( "__main__" );
	PyObject * pGlobals = PyModule_GetDict( pMain );

	PyObject * pResult = PyRun_String( source, Py_file_input,
		pGlobals, pGlobals );

	if (pResult == NULL)
	{
		// A pending error from an earlier check poisons PyRun_String, so
		// dump whatever is set to make the diagnosis obvious.
		PyObject * pType = NULL;
		PyObject * pValue = NULL;
		PyObject * pTraceback = NULL;
		PyErr_Fetch( &pType, &pValue, &pTraceback );
		PyErr_NormalizeException( &pType, &pValue, &pTraceback );

		fprintf( stderr, "runPython failed: type=%s value=%s\n",
			(pType != NULL) ? ((PyTypeObject *)pType)->tp_name : "(none)",
			(pValue != NULL) ?
				PyUnicode_AsUTF8( PyObject_Str( pValue ) ) : "(no value)" );
		Py_XDECREF( pType );
		Py_XDECREF( pValue );
		Py_XDECREF( pTraceback );
		return false;
	}

	Py_DECREF( pResult );
	return true;
}


/**
 *	Fetches an attribute of the __main__ module as a new reference.
 */
PyObject * mainAttr( const char * name )
{
	PyObject * pMain = PyImport_AddModule( "__main__" );
	return PyObject_GetAttrString( pMain, name );
}


/**
 *	Returns the current contents of the __main__.order list as integers.
 */
void fetchOrder( BW::vector< long > & order )
{
	order.clear();

	PyObjectPtr pOrder( mainAttr( "order" ), PyObjectPtr::STEAL_REFERENCE );
	if (pOrder.get() == NULL)
	{
		PyErr_Clear();
		return;
	}

	Py_ssize_t size = PyList_Size( pOrder.get() );
	for (Py_ssize_t i = 0; i < size; ++i)
	{
		order.push_back( PyLong_AsLong( PyList_GetItem( pOrder.get(), i ) ) );
	}
}

} // end namespace (anonymous)


// -----------------------------------------------------------------------------
// Section: ScriptEventList
// -----------------------------------------------------------------------------

// Listeners are called in level order (stable for equal levels), each
// listener's return value lands in the results list (None for failures),
// and removal only detaches the matching callable.
TEST_F( PyScriptUnitTestHarness, ScriptEventList_addTriggerRemove )
{
	CHECK( runPython(
		"order = []\n"
		"def first_listener( arg ):\n"
		"\torder.append( 1 )\n"
		"\treturn 'first'\n"
		"def mid_listener( arg ):\n"
		"\torder.append( 2 )\n"
		"\treturn 'mid'\n"
		"def high_listener( arg ):\n"
		"\torder.append( 3 )\n"
		"\treturn None\n" ) );

	PyObjectPtr pFirst( mainAttr( "first_listener" ),
		PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pMid( mainAttr( "mid_listener" ),
		PyObjectPtr::STEAL_REFERENCE );
	PyObjectPtr pHigh( mainAttr( "high_listener" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pFirst.get() && pMid.get() && pHigh.get() );

	if (!(pFirst.get() && pMid.get() && pHigh.get()))
	{
		return;
	}

	ScriptEventList list;
	CHECK( list.add( pFirst.get(), -1 ) );
	CHECK( list.add( pMid.get(), 0 ) );
	CHECK( list.add( pHigh.get(), 10 ) );

	// An empty argument tuple works too; the listener's arg is unused.
	ScriptList results( PyList_New( 0 ), ScriptObject::STEAL_REFERENCE );
	CHECK( results );
	CHECK( list.triggerEvent( Py_BuildValue( "(i)", 7 ), results ) );

	// Listeners ran in level order: first(-1), mid(0), high(10).
	BW::vector< long > order;
	fetchOrder( order );
	CHECK_EQUAL( size_t( 3 ), order.size() );
	CHECK_EQUAL( 1, order[ 0 ] );
	CHECK_EQUAL( 2, order[ 1 ] );
	CHECK_EQUAL( 3, order[ 2 ] );

	// Results are the listeners' return values, None for the void one.
	CHECK_EQUAL( 3, int( PyList_Size( results.get() ) ) );
	CHECK_EQUAL( BW::string( "first" ),
		BW::string( PyUnicode_AsUTF8( PyList_GetItem( results.get(), 0 ) ) ) );
	CHECK_EQUAL( BW::string( "mid" ),
		BW::string( PyUnicode_AsUTF8( PyList_GetItem( results.get(), 1 ) ) ) );
	CHECK( PyList_GetItem( results.get(), 2 ) == Py_None );

	// Equal levels keep insertion order: this listener lands after mid.
	CHECK( runPython(
		"def late_listener( arg ):\n"
		"\torder.append( 4 )\n" ) );
	PyObjectPtr pLate( mainAttr( "late_listener" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( list.add( pLate.get(), 0 ) );

	// Removing an unknown or already-removed listener fails.
	CHECK( list.remove( pMid.get() ) );
	CHECK( !list.remove( pMid.get() ) );
	CHECK( runPython( "def stranger( arg ):\n\tpass\n" ) );
	PyObjectPtr pStranger( mainAttr( "stranger" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( !list.remove( pStranger.get() ) );

	// Reset the order log so the next trigger's reads are unambiguous.
	CHECK( runPython( "order = []\n" ) );

	// A listener that raises makes the trigger fail and contributes a
	// None to the results list.
	CHECK( runPython(
		"def bad_listener( arg ):\n"
		"\torder.append( 5 )\n"
		"\traise RuntimeError( 'boom' )\n" ) );
	PyObjectPtr pBad( mainAttr( "bad_listener" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( list.add( pBad.get(), 100 ) );

	ScriptList results2( PyList_New( 0 ), ScriptObject::STEAL_REFERENCE );
	CHECK( !list.triggerEvent( Py_BuildValue( "(i)", 8 ), results2 ) );
	fetchOrder( order );
	// first(-1), late(0), high(10), bad(100); mid was removed.
	CHECK_EQUAL( size_t( 4 ), order.size() );
	CHECK_EQUAL( 1, order[ 0 ] );
	CHECK_EQUAL( 4, order[ 1 ] );
	CHECK_EQUAL( 3, order[ 2 ] );
	CHECK_EQUAL( 5, order[ 3 ] );
	CHECK_EQUAL( 4, int( PyList_Size( results2.get() ) ) );
	CHECK( PyList_GetItem( results2.get(), 3 ) == Py_None );

	// Triggering an empty list succeeds and appends nothing.
	ScriptEventList emptyList;
	ScriptList results3( PyList_New( 0 ), ScriptObject::STEAL_REFERENCE );
	CHECK( emptyList.triggerEvent( Py_BuildValue( "(i)", 9 ), results3 ) );
	CHECK_EQUAL( 0, int( PyList_Size( results3.get() ) ) );
}


// -----------------------------------------------------------------------------
// Section: ScriptEvents
// -----------------------------------------------------------------------------

// The registry maps event names to listener lists; unknown event names are
// rejected by add/remove/trigger, triggerTwoEvents fans out one argument
// tuple, and clear() forgets everything.
TEST_F( PyScriptUnitTestHarness, ScriptEvents_registry )
{
	CHECK( runPython(
		"order = []\n"
		"def alpha_listener( arg ):\n"
		"\torder.append( arg )\n" ) );

	PyObjectPtr pListener( mainAttr( "alpha_listener" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pListener.get() != NULL );
	if (pListener.get() == NULL)
	{
		return;
	}

	ScriptEvents events;
	events.createEventType( "onAlpha" );
	events.createEventType( "onBeta" );

	// Unknown event names are rejected everywhere.
	CHECK( !events.addEventListener( "missing", pListener.get(), 0 ) );
	CHECK( !events.removeEventListener( "missing", pListener.get() ) );
	CHECK( !events.triggerEvent( "missing",
		Py_BuildValue( "(i)", 1 ) ) );
	CHECK( !PyErr_Occurred() );

	// Triggering a registered event with no listeners succeeds.
	CHECK( events.triggerEvent( "onBeta", Py_BuildValue( "(i)", 2 ) ) );

	CHECK( events.addEventListener( "onAlpha", pListener.get(), 0 ) );

	ScriptList results( PyList_New( 0 ), ScriptObject::STEAL_REFERENCE );
	CHECK( events.triggerEvent( "onAlpha",
		Py_BuildValue( "(i)", 3 ), results ) );
	CHECK_EQUAL( 1, int( PyList_Size( results.get() ) ) );

	BW::vector< long > order;
	fetchOrder( order );
	CHECK_EQUAL( size_t( 1 ), order.size() );
	CHECK_EQUAL( 3, order[ 0 ] );

	// triggerTwoEvents runs both (the second with no listeners is fine).
	CHECK( events.triggerTwoEvents( "onAlpha", "onBeta",
		Py_BuildValue( "(i)", 4 ) ) );
	fetchOrder( order );
	CHECK_EQUAL( size_t( 2 ), order.size() );
	CHECK_EQUAL( 4, order[ 1 ] );

	// A missing event in the pair fails the combined result.
	CHECK( !events.triggerTwoEvents( "onAlpha", "missing",
		Py_BuildValue( "(i)", 5 ) ) );
	fetchOrder( order );
	CHECK_EQUAL( size_t( 3 ), order.size() );
	CHECK_EQUAL( 5, order[ 2 ] );

	// After removal the listener no longer fires.
	CHECK( events.removeEventListener( "onAlpha", pListener.get() ) );
	CHECK( events.triggerEvent( "onAlpha",
		Py_BuildValue( "(i)", 6 ) ) );
	fetchOrder( order );
	CHECK_EQUAL( size_t( 3 ), order.size() );

	// clear() drops the event types themselves.
	events.clear();
	CHECK( !events.triggerEvent( "onAlpha", Py_BuildValue( "(i)", 7 ) ) );
	CHECK( !events.addEventListener( "onAlpha", pListener.get(), 0 ) );
}


// initFromPersonality picks up personality-module attributes named after
// the registered event types as level-0 listeners.
TEST_F( PyScriptUnitTestHarness, ScriptEvents_initFromPersonality )
{
	CHECK( runPython(
		"order = []\n"
		"def onPersEvent( arg ):\n"
		"\torder.append( arg )\n" ) );

	ScriptEvents events;
	events.createEventType( "onPersEvent" );
	events.createEventType( "onPersMissing" );

	// The __main__ module plays the role of the personality module; it has
	// onPersEvent but not onPersMissing.
	ScriptModule personality( PyImport_AddModule( "__main__" ),
		ScriptObject::FROM_BORROWED_REFERENCE );
	events.initFromPersonality( personality );

	// The personality function is registered and fires on trigger.
	CHECK( events.triggerEvent( "onPersEvent",
		Py_BuildValue( "(i)", 42 ) ) );
	BW::vector< long > order;
	fetchOrder( order );
	CHECK_EQUAL( size_t( 1 ), order.size() );
	CHECK_EQUAL( 42, order[ 0 ] );

	// The undefined one still exists as an event type with no listeners.
	CHECK( events.triggerEvent( "onPersMissing",
		Py_BuildValue( "(i)", 43 ) ) );
}


// The BigWorld.addEventListener / removeEventListener module functions
// drive the singleton ScriptEvents instance.
TEST_F( PyScriptUnitTestHarness, ScriptEvents_moduleFunctions )
{
	CHECK( runPython(
		"mod_order = []\n"
		"def mod_listener( arg ):\n"
		"\tmod_order.append( arg )\n" ) );

	PyObjectPtr pListener( mainAttr( "mod_listener" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pListener.get() != NULL );

	// Our instance becomes the module functions' singleton.
	ScriptEvents events;
	events.createEventType( "onModuleTest" );

	PyObjectPtr pBigWorld( PyImport_ImportModule( "BigWorld" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pBigWorld.get() != NULL );
	if (pBigWorld.get() == NULL)
	{
		return;
	}

	// addEventListener succeeds.
	PyObjectPtr pAddRet( PyObject_CallMethod( pBigWorld.get(),
		"addEventListener", "sO", "onModuleTest", pListener.get() ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pAddRet.get() != NULL );
	CHECK( !PyErr_Occurred() );

	// The listener fires through the registry.
	CHECK( events.triggerEvent( "onModuleTest",
		Py_BuildValue( "(i)", 21 ) ) );
	PyObjectPtr pOrder( mainAttr( "mod_order" ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK_EQUAL( 1, int( PyList_Size( pOrder.get() ) ) );
	CHECK( PyLong_AsLong( PyList_GetItem( pOrder.get(), 0 ) ) == 21 );

	// Adding a non-callable raises TypeError before the event is checked.
	PyErr_Clear();
	CHECK( PyObject_CallMethod( pBigWorld.get(), "addEventListener",
		"sO", "onModuleTest", Py_None ) == NULL );
	CHECK( PyErr_ExceptionMatches( PyExc_TypeError ) );
	PyErr_Clear();

	// An unknown event name raises ValueError.
	PyErr_Clear();
	CHECK( PyObject_CallMethod( pBigWorld.get(), "addEventListener",
		"sO", "bogus_event", pListener.get() ) == NULL );
	CHECK( PyErr_ExceptionMatches( PyExc_ValueError ) );
	PyErr_Clear();

	// removeEventListener removes it; a second remove fails loudly.
	PyObjectPtr pRemoveRet( PyObject_CallMethod( pBigWorld.get(),
		"removeEventListener", "sO", "onModuleTest", pListener.get() ),
		PyObjectPtr::STEAL_REFERENCE );
	CHECK( pRemoveRet.get() != NULL );

	PyErr_Clear();
	CHECK( PyObject_CallMethod( pBigWorld.get(), "removeEventListener",
		"sO", "onModuleTest", pListener.get() ) == NULL );
	CHECK( PyErr_ExceptionMatches( PyExc_ValueError ) );
	PyErr_Clear();

	// The listener really is gone.
	CHECK( events.triggerEvent( "onModuleTest",
		Py_BuildValue( "(i)", 22 ) ) );
	CHECK_EQUAL( 1, int( PyList_Size( pOrder.get() ) ) );
}


BW_END_NAMESPACE

// test_script_events.cpp
