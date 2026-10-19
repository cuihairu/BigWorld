#include "pch.hpp"

#include "test_harness.hpp"

#include "entitydef/entity_method_descriptions.hpp"

#include "resmgr/xml_section.hpp"

BW_BEGIN_NAMESPACE

namespace
{

/**
 *	Utility method to get a DataSectionPtr given some XML
 */
DataSectionPtr getDataSectionForXML( const BW::string & dataSectionStr )
{
	BW::stringstream dataStrStream;
	dataStrStream << dataSectionStr;
	XMLSectionPtr xml = XMLSection::createFromStream( "", dataStrStream );
	return xml;
}

} // end anonymous namespace


TEST_F( EntityDefUnitTestHarness, MethodDescription_empty_signature )
{
	DataSectionPtr pMethodData1 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args/>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData1 );

	DataSectionPtr pMethodData2 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args/>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData2 );

	EntityMethodDescriptions methods;

	CHECK(methods.init(pMethodData1, MethodDescription::BASE, "iface", "component1"));
	CHECK(methods.init(pMethodData2, MethodDescription::BASE, "iface", "component2"));
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_equal_simple )
{
	DataSectionPtr pMethodData1 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args>"
		"		<strParam>		STRING	</strParam>"
		"		<floatParam>	FLOAT32	</floatParam>"
		"		<intParam>		INT32	</intParam>"
		"	</Args>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData1 );

	DataSectionPtr pMethodData2 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args>"
		"		<strParam>		STRING	</strParam>"
		"		<floatParam>	FLOAT32	</floatParam>"
		"		<intParam>		INT32	</intParam>"
		"	</Args>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData2 );

	EntityMethodDescriptions methods;

	CHECK(methods.init(pMethodData1, MethodDescription::BASE, "iface", "component1"));
	CHECK(methods.init(pMethodData2, MethodDescription::BASE, "iface", "component2"));
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_equal_signature )
{
	DataSectionPtr pMethodData1 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args>"
		"		<strParam>		STRING	</strParam>"
		"		<floatParam>	FLOAT32	</floatParam>"
		"		<intParam>		INT32	</intParam>"
		"		<blobParam>		BLOB 	</blobParam>"
		"		<vec2Param>		VECTOR2	</vec2Param>"
		"		<vec3Param>		VECTOR3	</vec3Param>"
		"		<vec4Param>		VECTOR4	</vec4Param>"
		"		<mbxParam>		MAILBOX	</mbxParam>"
		"		<arrParam>		ARRAY <of> UNICODE_STRING </of>	</arrParam>"
		"		<tupleParam>	ARRAY <of> UINT16 </of>	</tupleParam>"
		"		<stupleParam>	ARRAY <of> FLOAT64 </of> <size> 4 </size> </stupleParam>"
		"		<dictParam> 	FIXED_DICT"
		"			<Properties>"
		"				<prop1>	<Type> INT64 </Type> </prop1>"
		"				<prop2>	<Type> UINT64 </Type> </prop2>"
		"			</Properties>"
		"		</dictParam>"
		"	</Args>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData1 );

	DataSectionPtr pMethodData2 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args>"
		"		<strParam>		STRING	</strParam>"
		"		<floatParam>	FLOAT32	</floatParam>"
		"		<intParam>		INT32	</intParam>"
		"		<blobParam>		BLOB 	</blobParam>"
		"		<vec2Param>		VECTOR2	</vec2Param>"
		"		<vec3Param>		VECTOR3	</vec3Param>"
		"		<vec4Param>		VECTOR4	</vec4Param>"
		"		<mbxParam>		MAILBOX	</mbxParam>"
		"		<arrParam>		ARRAY <of> UNICODE_STRING </of>	</arrParam>"
		"		<tupleParam>	ARRAY <of> UINT16 </of>	</tupleParam>"
		"		<stupleParam>	ARRAY <of> FLOAT64 </of> <size> 4 </size> </stupleParam>"
		"		<dictParam> 	FIXED_DICT"
		"			<Properties>"
		"				<prop1>	<Type> INT64 </Type> </prop1>"
		"				<prop2>	<Type> UINT64 </Type> </prop2>"
		"			</Properties>"
		"		</dictParam>"
		"	</Args>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData2 );

	EntityMethodDescriptions methods;

	CHECK(methods.init(pMethodData1, MethodDescription::BASE, "iface", "component1"));
	CHECK(methods.init(pMethodData2, MethodDescription::BASE, "iface", "component2"));
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_not_equal_simple )
{
	DataSectionPtr pMethodData1 = getDataSectionForXML(
		"<methods>"
		"<testNotEqualMethod>"
		"	<Exposed/>"
		"	<Args>"
		"		<Param>	FLOAT32	</Param>"
		"	</Args>"
		"</testNotEqualMethod>"
		"</methods>");

	CHECK( pMethodData1 );

	DataSectionPtr pMethodData2 = getDataSectionForXML(
		"<methods>"
		"<testNotEqualMethod>"
		"	<Exposed/>"
		"	<Args>"
		"		<Param>	STRING	</Param>"
		"	</Args>"
		"</testNotEqualMethod>"
		"</methods>");

	CHECK( pMethodData2 );

	EntityMethodDescriptions methods;

	CHECK(methods.init(pMethodData1, MethodDescription::BASE, "iface", "component1"));
	CHECK(!methods.init(pMethodData2, MethodDescription::BASE, "iface", "component2"));
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_not_equal_dict )
{
	DataSectionPtr pMethodData1 = getDataSectionForXML(
		"<methods>"
		"<testNotEqualMethod>"
		"	<Exposed/>"
		"	<Args>"
		"		<dictParam> FIXED_DICT"
		"			<Properties>"
		"				<prop1>	<Type> INT64 </Type> </prop1>"
		"				<prop2>	<Type> UINT64 </Type> </prop2>"
		"			</Properties>"
		"		</dictParam>"
		"	</Args>"
		"</testNotEqualMethod>"
		"</methods>");

	CHECK( pMethodData1 );

	DataSectionPtr pMethodData2 = getDataSectionForXML(
		"<methods>"
		"<testNotEqualMethod>"
		"	<Exposed/>"
		"	<Args>"
		"		<dictParam> FIXED_DICT"
		"			<Properties>"
		"				<prop1>	<Type> INT32 </Type> </prop1>"
		"				<prop2>	<Type> UINT64 </Type> </prop2>"
		"			</Properties>"
		"		</dictParam>"
		"	</Args>"
		"</testNotEqualMethod>"
		"</methods>");

	CHECK( pMethodData2 );

	EntityMethodDescriptions methods;

	CHECK(methods.init(pMethodData1, MethodDescription::BASE, "iface", "component1"));
	CHECK(!methods.init(pMethodData2, MethodDescription::BASE, "iface", "component2"));
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_not_equal_exposure )
{
	DataSectionPtr pMethodData1 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Exposed/>"
		"	<Args/>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData1 );

	DataSectionPtr pMethodData2 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args/>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData2 );

	EntityMethodDescriptions methods;

	CHECK(methods.init(pMethodData1, MethodDescription::BASE, "iface", "component1"));
	CHECK(!methods.init(pMethodData2, MethodDescription::BASE, "iface", "component2"));
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_not_equal_component )
{
	DataSectionPtr pMethodData1 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args/>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData1 );

	DataSectionPtr pMethodData2 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args/>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData2 );

	EntityMethodDescriptions methods;

	CHECK(methods.init(pMethodData1, MethodDescription::BASE, "iface", "component1"));
	CHECK(!methods.init(pMethodData2, MethodDescription::CELL, "iface", "component2"));
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_duplicate_in_component )
{
	DataSectionPtr pMethodData1 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args/>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData1 );

	DataSectionPtr pMethodData2 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args/>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData2 );

	EntityMethodDescriptions methods;

	CHECK(methods.init(pMethodData1, MethodDescription::BASE, "iface", "component1"));
	CHECK(!methods.init(pMethodData2, MethodDescription::BASE, "iface", "component1"));
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_entity_component_conflict )
{
	DataSectionPtr pMethodData1 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args/>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData1 );

	DataSectionPtr pMethodData2 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args/>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData2 );

	EntityMethodDescriptions methods;

	CHECK(methods.init(pMethodData1, MethodDescription::BASE, "iface", ""));
	CHECK(!methods.init(pMethodData2, MethodDescription::BASE, "iface", "component1"));
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_component_entity_conflict )
{
	DataSectionPtr pMethodData1 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args/>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData1 );

	DataSectionPtr pMethodData2 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args/>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData2 );

	EntityMethodDescriptions methods;

	CHECK(methods.init(pMethodData1, MethodDescription::BASE, "iface", "component1"));
	CHECK(!methods.init(pMethodData2, MethodDescription::BASE, "iface", ""));
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_equal_mailbox )
{
	DataSectionPtr pMethodData1 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args>"
		"		<mbxParam>		MAILBOX	</mbxParam>"
		"	</Args>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData1 );

	DataSectionPtr pMethodData2 = getDataSectionForXML(
		"<methods>"
		"<testEqualMethod>"
		"	<Args>"
		"		<mbxParam>		MAILBOX	</mbxParam>"
		"	</Args>"
		"</testEqualMethod>"
		"</methods>");

	CHECK( pMethodData2 );

	EntityMethodDescriptions methods;

	CHECK(methods.init(pMethodData1, MethodDescription::BASE, "iface", "component1"));
	CHECK(methods.init(pMethodData2, MethodDescription::BASE, "iface", "component2"));
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_not_equal_param_name )
{
	DataSectionPtr pMethodData1 = getDataSectionForXML(
		"<methods>"
		"<testNotEqualMethod>"
		"	<Exposed/>"
		"	<Args>"
		"		<Param1>	STRING	</Param1>"
		"	</Args>"
		"</testNotEqualMethod>"
		"</methods>");

	CHECK( pMethodData1 );

	DataSectionPtr pMethodData2 = getDataSectionForXML(
		"<methods>"
		"<testNotEqualMethod>"
		"	<Exposed/>"
		"	<Args>"
		"		<Param2>	STRING	</Param2>"
		"	</Args>"
		"</testNotEqualMethod>"
		"</methods>");

	CHECK( pMethodData2 );

	EntityMethodDescriptions methods;

	CHECK(methods.init(pMethodData1, MethodDescription::BASE, "iface", "component1"));
	CHECK(!methods.init(pMethodData2, MethodDescription::BASE, "iface", "component2"));
}

/**
 *	An exposed CELL method extracts the source entity id from the head of
 *	the args tuple: an int head is taken directly, a None head records
 *	NULL_ENTITY_ID, and either way the returned tuple drops the head.
 */
TEST_F( EntityDefUnitTestHarness, MethodDescription_extractSourceEntityIDIntAndNone )
{
	DataSectionPtr pMethodData = getDataSectionForXML(
		"<methods>"
		"<cellShout>"
		"	<Exposed/>"
		"	<Args>"
		"		<msg> STRING </msg>"
		"	</Args>"
		"</cellShout>"
		"</methods>");

	CHECK( pMethodData );

	EntityMethodDescriptions methods;
	CHECK( methods.init( pMethodData, MethodDescription::CELL, "iface", "c1" ) );
	MethodDescription * pMethod = methods.internalMethod( 0 );
	CHECK( pMethod != NULL );

	ScriptTuple args = ScriptTuple::create( 2 );
	CHECK( args.setItem( 0, ScriptInt::create( 77 ) ) );
	CHECK( args.setItem( 1, ScriptString::create( "hi" ) ) );

	EntityID sourceEntityID = 0;
	ScriptTuple stripped = pMethod->extractSourceEntityID( args,
		sourceEntityID );

	CHECK_EQUAL( 77, sourceEntityID );
	CHECK_EQUAL( 1, stripped.size() );
	ScriptObject pHi = stripped.getItem( 0 );
	CHECK( ScriptString::check( pHi ) );
	CHECK( strcmp( ScriptString( pHi ).c_str(), "hi" ) == 0 );

	ScriptTuple noneArgs = ScriptTuple::create( 2 );
	CHECK( noneArgs.setItem( 0, ScriptObject( Py_None,
		ScriptObject::FROM_BORROWED_REFERENCE ) ) );
	CHECK( noneArgs.setItem( 1, ScriptString::create( "hi" ) ) );

	sourceEntityID = 99;
	stripped = pMethod->extractSourceEntityID( noneArgs, sourceEntityID );

	CHECK_EQUAL( NULL_ENTITY_ID, sourceEntityID );
	CHECK( stripped.size() == 1 );
}


/**
 *	When the head argument is neither an int nor None, the source entity id
 *	is read from its 'id' attribute; without one the call fails.
 */
TEST_F( EntityDefUnitTestHarness, MethodDescription_extractSourceEntityIDObjectWithId )
{
	DataSectionPtr pMethodData = getDataSectionForXML(
		"<methods>"
		"<cellShout>"
		"	<Exposed/>"
		"	<Args>"
		"		<msg> STRING </msg>"
		"	</Args>"
		"</cellShout>"
		"</methods>");

	CHECK( pMethodData );

	EntityMethodDescriptions methods;
	CHECK( methods.init( pMethodData, MethodDescription::CELL, "iface", "c1" ) );
	MethodDescription * pMethod = methods.internalMethod( 0 );
	CHECK( pMethod != NULL );

	PyObject * pGlobals = PyDict_New();
	CHECK( pGlobals != NULL );
	PyRun_String(
		"class _BW34Src:\n"
		"	def __init__( self, i ): self.id = i\n",
		Py_file_input, pGlobals, pGlobals );
	CHECK( PyErr_Occurred() == NULL );

	ScriptTuple args = ScriptTuple::create( 2 );
	CHECK( args.setItem( 0, ScriptObject( PyRun_String( "_BW34Src( 77 )\n",
		Py_eval_input, pGlobals, pGlobals ),
		ScriptObject::STEAL_REFERENCE ) ) );
	CHECK( args.setItem( 1, ScriptString::create( "hi" ) ) );

	EntityID sourceEntityID = 0;
	ScriptTuple stripped = pMethod->extractSourceEntityID( args,
		sourceEntityID );

	CHECK_EQUAL( 77, sourceEntityID );
	CHECK( stripped.size() == 1 );

	// An object without an 'id' attribute fails the extraction.
	ScriptTuple noIdArgs = ScriptTuple::create( 2 );
	CHECK( noIdArgs.setItem( 0, ScriptObject( PyRun_String( "object()\n",
		Py_eval_input, pGlobals, pGlobals ),
		ScriptObject::STEAL_REFERENCE ) ) );
	CHECK( noIdArgs.setItem( 1, ScriptString::create( "hi" ) ) );

	sourceEntityID = 0;
	ScriptTuple failed = pMethod->extractSourceEntityID( noIdArgs,
		sourceEntityID );

	CHECK( !failed.exists() );

	Py_DECREF( pGlobals );
	PyErr_Clear();
}


/**
 *	Only exposed CELL methods take a source entity id: a BASE method passes
 *	the args through untouched, and an exposed CELL method with no args at
 *	all reports an error.
 */
TEST_F( EntityDefUnitTestHarness, MethodDescription_extractSourceEntityIDPassthroughAndEmpty )
{
	DataSectionPtr pBaseData = getDataSectionForXML(
		"<methods>"
		"<basePing>"
		"	<Args>"
		"		<msg> STRING </msg>"
		"	</Args>"
		"</basePing>"
		"</methods>");

	CHECK( pBaseData );

	EntityMethodDescriptions baseMethods;
	CHECK( baseMethods.init( pBaseData, MethodDescription::BASE, "iface", "c1" ) );
	MethodDescription * pBaseMethod = baseMethods.internalMethod( 0 );
	CHECK( pBaseMethod != NULL );

	ScriptTuple args = ScriptTuple::create( 1 );
	CHECK( args.setItem( 0, ScriptString::create( "hi" ) ) );

	EntityID sourceEntityID = 55;
	ScriptTuple result = pBaseMethod->extractSourceEntityID( args,
		sourceEntityID );

	CHECK_EQUAL( NULL_ENTITY_ID, sourceEntityID );
	CHECK_EQUAL( 1, result.size() );
	ScriptObject pHi = result.getItem( 0 );
	CHECK( ScriptString::check( pHi ) );
	CHECK( strcmp( ScriptString( pHi ).c_str(), "hi" ) == 0 );

	// An exposed CELL method with empty args cannot carry a source id.
	DataSectionPtr pCellData = getDataSectionForXML(
		"<methods>"
		"<cellShout>"
		"	<Exposed/>"
		"	<Args/>"
		"</cellShout>"
		"</methods>");

	CHECK( pCellData );

	EntityMethodDescriptions cellMethods;
	CHECK( cellMethods.init( pCellData, MethodDescription::CELL, "iface", "c1" ) );
	MethodDescription * pCellMethod = cellMethods.internalMethod( 0 );
	CHECK( pCellMethod != NULL );

	ScriptTuple empty = ScriptTuple::create( 0 );
	CHECK( !pCellMethod->extractSourceEntityID( empty, sourceEntityID ).exists() );
}


/**
 *	Keyword args fill the tail of the args tuple by name: the positional
 *	prefix is copied verbatim, each remaining named slot is looked up in
 *	the kwargs dict, and any mismatch in count or names fails.
 */
TEST_F( EntityDefUnitTestHarness, MethodDescription_convertKeywordArgs )
{
	DataSectionPtr pMethodData = getDataSectionForXML(
		"<methods>"
		"<greet>"
		"	<Args>"
		"		<name> STRING </name>"
		"		<count> UINT16 </count>"
		"	</Args>"
		"</greet>"
		"</methods>");

	CHECK( pMethodData );

	EntityMethodDescriptions methods;
	CHECK( methods.init( pMethodData, MethodDescription::BASE, "iface", "c1" ) );
	MethodDescription * pMethod = methods.internalMethod( 0 );
	CHECK( pMethod != NULL );

	// Fully positional.
	ScriptTuple positional = ScriptTuple::create( 2 );
	CHECK( positional.setItem( 0, ScriptString::create( "bob" ) ) );
	CHECK( positional.setItem( 1, ScriptInt::create( 3 ) ) );

	ScriptObject result = pMethod->convertKeywordArgs( positional,
		ScriptDict::create() );
	CHECK( result.exists() );
	CHECK( ScriptTuple::check( result ) );
	CHECK_EQUAL( 2, ScriptTuple( result ).size() );
	ScriptObject pZero = ScriptTuple( result ).getItem( 0 );
	CHECK( ScriptString::check( pZero ) );
	CHECK( strcmp( ScriptString( pZero ).c_str(), "bob" ) == 0 );
	ScriptObject pOne = ScriptTuple( result ).getItem( 1 );
	CHECK( ScriptInt::check( pOne ) );
	CHECK_EQUAL( 3, ScriptInt( pOne ).asLong() );

	// One positional plus one keyword fills the named tail slot.
	ScriptTuple partial = ScriptTuple::create( 1 );
	CHECK( partial.setItem( 0, ScriptString::create( "bob" ) ) );

	ScriptDict kwargs = ScriptDict::create();
	CHECK( kwargs.setItem( "count", ScriptInt::create( 3 ),
		ScriptErrorPrint() ) );

	result = pMethod->convertKeywordArgs( partial, kwargs );
	CHECK( result.exists() );
	CHECK( ScriptTuple::check( result ) );
	CHECK_EQUAL( 2, ScriptTuple( result ).size() );
	ScriptObject pTailZero = ScriptTuple( result ).getItem( 0 );
	CHECK( ScriptString::check( pTailZero ) );
	CHECK( strcmp( ScriptString( pTailZero ).c_str(), "bob" ) == 0 );
	ScriptObject pTailOne = ScriptTuple( result ).getItem( 1 );
	CHECK( ScriptInt::check( pTailOne ) );
	CHECK_EQUAL( 3, ScriptInt( pTailOne ).asLong() );

	// A missing name fails, and the error is cleared for the next check.
	ScriptDict wrongName = ScriptDict::create();
	CHECK( wrongName.setItem( "nope", ScriptInt::create( 3 ),
		ScriptErrorPrint() ) );

	CHECK( !pMethod->convertKeywordArgs( partial, wrongName ).exists() );
	PyErr_Clear();

	// Too few arguments in total fails.
	CHECK( !pMethod->convertKeywordArgs( partial, ScriptDict::create() ).exists() );
	PyErr_Clear();
}


/**
 *	Return values are validated against the <ReturnValues> section by arity
 *	and per-element checks, and a valid tuple converts to a dict keyed by
 *	the declared return names.
 */
TEST_F( EntityDefUnitTestHarness, MethodDescription_returnValuesHelpers )
{
	DataSectionPtr pMethodData = getDataSectionForXML(
		"<methods>"
		"<fetchThing>"
		"	<Args>"
		"		<key> STRING </key>"
		"	</Args>"
		"	<ReturnValues>"
		"		<name> STRING </name>"
		"		<count> UINT16 </count>"
		"	</ReturnValues>"
		"</fetchThing>"
		"</methods>");

	CHECK( pMethodData );

	EntityMethodDescriptions methods;
	CHECK( methods.init( pMethodData, MethodDescription::BASE, "iface", "c1" ) );
	MethodDescription * pMethod = methods.internalMethod( 0 );
	CHECK( pMethod != NULL );

	ScriptTuple values = ScriptTuple::create( 2 );
	CHECK( values.setItem( 0, ScriptString::create( "bob" ) ) );
	CHECK( values.setItem( 1, ScriptInt::create( 3 ) ) );

	CHECK( pMethod->areValidReturnValues( values ) );

	ScriptDict dict = pMethod->createDictFromTuple( values );
	CHECK( dict.exists() );
	ScriptObject pName = dict.getItem( "name", ScriptErrorPrint() );
	CHECK( ScriptString::check( pName ) );
	CHECK( strcmp( ScriptString( pName ).c_str(), "bob" ) == 0 );
	ScriptObject pCount = dict.getItem( "count", ScriptErrorPrint() );
	CHECK( ScriptInt::check( pCount ) );
	CHECK_EQUAL( 3, ScriptInt( pCount ).asLong() );

	// Wrong arity fails.
	ScriptTuple shortTuple = ScriptTuple::create( 1 );
	CHECK( shortTuple.setItem( 0, ScriptString::create( "bob" ) ) );
	CHECK( !pMethod->areValidReturnValues( shortTuple ) );
	PyErr_Clear();

	// Wrong element type fails.
	ScriptTuple badType = ScriptTuple::create( 2 );
	CHECK( badType.setItem( 0, ScriptString::create( "bob" ) ) );
	CHECK( badType.setItem( 1, ScriptString::create( "3" ) ) );
	CHECK( !pMethod->areValidReturnValues( badType ) );
	PyErr_Clear();
}


BW_END_NAMESPACE

// test_stream.cpp
