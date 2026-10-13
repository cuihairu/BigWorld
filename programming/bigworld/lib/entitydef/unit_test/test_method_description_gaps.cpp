// Coverage batch 28: the direct MethodDescription surface - parse variants,
// exposure accessors, exposed message ids, stream sizes, streaming,
// MD5/recording/client-safety queries, components and the Python paths.
//
// Behaviour notes:
//  - parse leaves replayExposureLevel_ at the default OTHER_CLIENTS when no
//    <ReplayExposureLevel> is present (MethodDescription constructor).
//  - detail distance <DetailDistance> is squared before being stored
//    (method_description.cpp parse).
//  - addSubMessageIDToStream MF_ASSERTs that exposedMsgID_ != -1, so the
//    exposed-id state must be set before streaming a client method.
//  - This repo ships no BWTwoWay module, so the two-way error factory stays
//    uninitialised: staticInitTwoWay() fails and createErrorObject returns
//    a null ScriptObject. The tests assert the correlation, not the failure.
//  - ReturnValuesHandler::staticInit() is also called from parse under
//    MF_SERVER; it is idempotent and its result is ignored by parse.
#include "pch.hpp"

#include "test_harness.hpp"

#include "entitydef/data_type.hpp"
#include "entitydef/method_description.hpp"
#include "entitydef/script_data_sink.hpp"
#include "entitydef/script_data_source.hpp"

#include "cstdmf/md5.hpp"
#include "cstdmf/memory_stream.hpp"
#include "network/exposed_message_range.hpp"
#include "pyscript/script.hpp"
#include "resmgr/xml_section.hpp"
#include "script/pickler.hpp"

#include <cfloat>

BW_BEGIN_NAMESPACE

namespace
{

/// Parses a single-method XML string, e.g. "<m1><Args>...</Args></m1>".
DataSectionPtr getMethodSectionForXML( const BW::string & xml )
{
	BW::stringstream stream;
	stream << xml;
	XMLSectionPtr pSection = XMLSection::createFromStream( "", stream );
	return pSection;
}

/// Parses a MethodDescription from single-method XML. The section must have
/// been CHECKed by the caller before the returned value is trusted.
bool parseMethodDescription( MethodDescription & desc,
		const BW::string & xml, MethodDescription::Component component )
{
	DataSectionPtr pSection = getMethodSectionForXML( xml );

	if (!pSection)
	{
		return false;
	}

	return desc.parse( "testiface", pSection, component, NULL );
}

/// Streams a single fixed int32 argument.
void streamInt32( BinaryOStream & stream, int32 value )
{
	stream << value;
}

/// Clears a pending Python error left by an expected failure.
void clearPendingError()
{
	if (Script::hasError())
	{
		Script::printError();
	}
}

} // anonymous namespace


TEST_F( EntityDefUnitTestHarness, MethodDescription_parse_defaults )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );

	CHECK( desc.name() == "m1" );
	CHECK( desc.component() == MethodDescription::BASE );
	CHECK( !desc.isExposed() );
	CHECK( desc.replayExposureLevel() ==
		MethodDescription::REPLAY_EXPOSURE_LEVEL_OTHER_CLIENTS );
	CHECK_EQUAL( 1, desc.args().size() );
	CHECK( desc.args().argName( 0 ) == "a" );
	CHECK_EQUAL( int( sizeof( int32 ) ), desc.args().streamSize() );

	// No DetailDistance tag: the priority stays at its FLT_MAX sentinel.
	CHECK_CLOSE( FLT_MAX, desc.priority(), 1.f );

	CHECK( !desc.hasReturnValues() );
	CHECK_EQUAL( -1, desc.exposedIndex() );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_parse_exposed_flags )
{
	MethodDescription ownClient;
	CHECK( parseMethodDescription( ownClient,
		"<m1><Exposed>OWN_CLIENT</Exposed><Args/></m1>",
		MethodDescription::CELL ) );
	CHECK( ownClient.isExposed() );
	CHECK( ownClient.isExposedToOwnClientOnly() );
	CHECK( !ownClient.isExposedToAllClientsOnly() );
	CHECK( !ownClient.isExposedToDefault() );

	MethodDescription allClients;
	CHECK( parseMethodDescription( allClients,
		"<m1><Exposed>ALL_CLIENTS</Exposed><Args/></m1>",
		MethodDescription::CELL ) );
	CHECK( allClients.isExposedToAllClientsOnly() );
	CHECK( !allClients.isExposedToOwnClientOnly() );

	MethodDescription exposedDefault;
	CHECK( parseMethodDescription( exposedDefault,
		"<m1><Exposed/><Args/></m1>",
		MethodDescription::BASE ) );
	CHECK( exposedDefault.isExposedToDefault() );
	CHECK( !exposedDefault.isExposedToOwnClientOnly() );
	CHECK( !exposedDefault.isExposedToAllClientsOnly() );

	// ALL_CLIENTS is only valid on cell methods.
	MethodDescription allOnBase;
	CHECK( !parseMethodDescription( allOnBase,
		"<m1><Exposed>ALL_CLIENTS</Exposed><Args/></m1>",
		MethodDescription::BASE ) );

	// <Exposed> is rejected on client components.
	MethodDescription exposedOnClient;
	CHECK( !parseMethodDescription( exposedOnClient,
		"<m1><Exposed/><Args/></m1>",
		MethodDescription::CLIENT ) );

	// Unrecognised flags are rejected.
	MethodDescription bogusFlag;
	CHECK( !parseMethodDescription( bogusFlag,
		"<m1><Exposed>NOT_A_FLAG</Exposed><Args/></m1>",
		MethodDescription::BASE ) );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_parse_replay_exposure )
{
	MethodDescription noneDesc;
	CHECK( parseMethodDescription( noneDesc,
		"<m1><ReplayExposureLevel>NONE</ReplayExposureLevel><Args/></m1>",
		MethodDescription::BASE ) );
	CHECK( noneDesc.replayExposureLevel() ==
		MethodDescription::REPLAY_EXPOSURE_LEVEL_NONE );

	MethodDescription otherDesc;
	CHECK( parseMethodDescription( otherDesc,
		"<m1><ReplayExposureLevel>OTHER_CLIENTS"
		"</ReplayExposureLevel><Args/></m1>",
		MethodDescription::BASE ) );
	CHECK( otherDesc.replayExposureLevel() ==
		MethodDescription::REPLAY_EXPOSURE_LEVEL_OTHER_CLIENTS );

	MethodDescription allDesc;
	CHECK( parseMethodDescription( allDesc,
		"<m1><ReplayExposureLevel>ALL_CLIENTS</ReplayExposureLevel>"
		"<Args/></m1>",
		MethodDescription::BASE ) );
	CHECK( allDesc.replayExposureLevel() ==
		MethodDescription::REPLAY_EXPOSURE_LEVEL_ALL_CLIENTS );

	MethodDescription bogusDesc;
	CHECK( !parseMethodDescription( bogusDesc,
		"<m1><ReplayExposureLevel>EVERYONE</ReplayExposureLevel>"
		"<Args/></m1>",
		MethodDescription::BASE ) );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_parse_detail_distance )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><DetailDistance>100</DetailDistance><Args/></m1>",
		MethodDescription::BASE ) );

	// DetailDistance is squared when stored.
	CHECK_CLOSE( 10000.f, desc.priority(), 1.f );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_parse_old_style_args )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Arg> INT32 </Arg><Arg> STRING </Arg></m1>",
		MethodDescription::BASE ) );

	// Old-style args carry no names.
	CHECK_EQUAL( 2, desc.args().size() );
	CHECK( desc.args().argName( 0 ) == "" );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_parse_return_values )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args/><ReturnValues><rv> INT32 </rv></ReturnValues></m1>",
		MethodDescription::BASE ) );
	CHECK( desc.hasReturnValues() );
	CHECK_EQUAL( 1, desc.returnArgs().size() );
	CHECK( desc.returnArgs().argName( 0 ) == "rv" );

	MethodDescription badReturnValues;
	CHECK( !parseMethodDescription( badReturnValues,
		"<m1><Args/><ReturnValues><rv> NOT_A_TYPE </rv></ReturnValues></m1>",
		MethodDescription::BASE ) );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_parse_arg_failure )
{
	MethodDescription desc;
	CHECK( !parseMethodDescription( desc,
		"<m1><Args><a> NOT_A_TYPE </a></Args></m1>",
		MethodDescription::BASE ) );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_exposure_accessors )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc, "<m1><Args/></m1>",
		MethodDescription::BASE ) );

	CHECK( !desc.isExposed() );
	CHECK( !desc.isExposedToOwnClientOnly() );
	CHECK( !desc.isExposedToAllClientsOnly() );
	CHECK( !desc.isExposedToDefault() );

	desc.setExposedToOwnClient();
	CHECK( desc.isExposed() );
	CHECK( desc.isExposedToOwnClientOnly() );
	CHECK( !desc.isExposedToAllClientsOnly() );
	CHECK( !desc.isExposedToDefault() );

	MethodDescription allDesc;
	allDesc.setExposedToAllClients();
	CHECK( allDesc.isExposed() );
	CHECK( allDesc.isExposedToAllClientsOnly() );
	CHECK( !allDesc.isExposedToOwnClientOnly() );
	CHECK( !allDesc.isExposedToDefault() );

	MethodDescription defaultDesc;
	defaultDesc.setExposedToDefault();
	CHECK( defaultDesc.isExposed() );
	CHECK( defaultDesc.isExposedToDefault() );
	CHECK( !defaultDesc.isExposedToOwnClientOnly() );
	CHECK( !defaultDesc.isExposedToAllClientsOnly() );

	// The component lives in the low two bits of flags_.
	CHECK( defaultDesc.component() == MethodDescription::CLIENT );
	defaultDesc.component( MethodDescription::CELL );
	CHECK( defaultDesc.component() == MethodDescription::CELL );

	desc.internalIndex( 7 );
	CHECK_EQUAL( 7, int( desc.internalIndex() ) );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_exposed_msg_ids )
{
	MethodDescription desc;
	desc.setExposedMsgID( 2, 5, NULL );
	CHECK_EQUAL( 2, desc.exposedIndex() );
	// With no range the message id stays at its -1 sentinel, which the uint8
	// MessageID accessor wraps around to 255.
	CHECK_EQUAL( 255, int( desc.exposedMsgID() ) );
	CHECK( !desc.hasSubMsgID() );

	// Five exposed methods fit inside a ten-slot range: no sub id.
	ExposedMethodMessageRange smallRange( 10, 19 );
	desc.setExposedMsgID( 3, 5, &smallRange );
	CHECK_EQUAL( 3, desc.exposedIndex() );
	CHECK_EQUAL( 13, int( desc.exposedMsgID() ) );
	CHECK_EQUAL( -1, desc.exposedSubMsgID() );
	CHECK( !desc.hasSubMsgID() );

	// Twelve exposed methods overflow the range: sub ids kick in.
	ExposedMethodMessageRange overflowRange( 10, 19 );
	desc.setExposedMsgID( 10, 12, &overflowRange );
	CHECK_EQUAL( 10, desc.exposedIndex() );
	CHECK_EQUAL( 19, int( desc.exposedMsgID() ) );
	CHECK_EQUAL( 1, desc.exposedSubMsgID() );
	CHECK( desc.hasSubMsgID() );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_stream_size )
{
	MethodDescription fixed;
	CHECK( parseMethodDescription( fixed,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );

	// A plain fixed-size method is just the argument bytes.
	CHECK_EQUAL( int( sizeof( int32 ) ), fixed.streamSize( false ) );
	CHECK_EQUAL( int( sizeof( int32 ) ), fixed.streamSize( true ) );

	// An exposed cell method carries the source entity id when the call
	// comes from the server.
	MethodDescription exposed;
	CHECK( parseMethodDescription( exposed,
		"<m1><Exposed/><Args><a> INT32 </a></Args></m1>",
		MethodDescription::CELL ) );
	CHECK_EQUAL( int( sizeof( int32 ) ), exposed.streamSize( false ) );
	CHECK_EQUAL( int( sizeof( int32 ) + sizeof( EntityID ) ),
		exposed.streamSize( true ) );

	// Variable-length arguments make the whole message variable-length.
	MethodDescription variable;
	CHECK( parseMethodDescription( variable,
		"<m1><Args><s> STRING </s></Args></m1>",
		MethodDescription::BASE ) );
	CHECK_EQUAL( -int( Mercury::DEFAULT_VARIABLE_LENGTH_HEADER_SIZE ),
		variable.streamSize( false ) );

	// Sub-indexed exposed methods are always variable-size with the default
	// header length.
	MethodDescription subIndexed;
	CHECK( parseMethodDescription( subIndexed,
		"<m1><Exposed/><Args><a> INT32 </a></Args></m1>",
		MethodDescription::CELL ) );
	ExposedMethodMessageRange range( 10, 19 );
	subIndexed.setExposedMsgID( 10, 12, &range );
	CHECK_EQUAL( -int( Mercury::DEFAULT_VARIABLE_LENGTH_HEADER_SIZE ),
		subIndexed.streamSize( true ) );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_add_sub_message_id )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Exposed/><Args><a> INT32 </a></Args></m1>",
		MethodDescription::CELL ) );

	// Without a sub id nothing is written.
	ExposedMethodMessageRange range( 10, 19 );
	desc.setExposedMsgID( 3, 5, &range );

	MemoryOStream stream;
	desc.addSubMessageIDToStream( stream );
	CHECK_EQUAL( 0, stream.remainingLength() );

	// With a sub id a single byte is written.
	desc.setExposedMsgID( 10, 12, &range );
	MemoryOStream subStream;
	desc.addSubMessageIDToStream( subStream );
	CHECK_EQUAL( 1, subStream.remainingLength() );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_add_to_stream )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );

	ScriptTuple args = ScriptTuple::create( 1 );
	args.setItem( 0, ScriptObject::createFrom( 5 ) );

	MemoryOStream stream;
	ScriptDataSource source( args );
	CHECK( desc.addToStream( source, stream ) );

	stream.rewind();
	int32 value = -1;
	stream >> value;
	CHECK_EQUAL( 5, value );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_add_to_server_stream )
{
	// A non-exposed method streams just its arguments.
	MethodDescription plain;
	CHECK( parseMethodDescription( plain,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );

	ScriptTuple args = ScriptTuple::create( 1 );
	args.setItem( 0, ScriptObject::createFrom( 5 ) );

	MemoryOStream plainStream;
	ScriptDataSource plainSource( args );
	CHECK( plain.addToServerStream( plainSource, plainStream, 42 ) );
	plainStream.rewind();

	int32 plainValue = -1;
	plainStream >> plainValue;
	CHECK_EQUAL( 5, plainValue );

	// An exposed cell method is prepended with the calling entity id.
	MethodDescription exposed;
	CHECK( parseMethodDescription( exposed,
		"<m1><Exposed/><Args><a> INT32 </a></Args></m1>",
		MethodDescription::CELL ) );

	MemoryOStream exposedStream;
	ScriptDataSource exposedSource( args );
	CHECK( exposed.addToServerStream( exposedSource, exposedStream, 42 ) );
	exposedStream.rewind();

	int32 sourceID = -1;
	int32 value = -1;
	exposedStream >> sourceID >> value;
	CHECK_EQUAL( 42, sourceID );
	CHECK_EQUAL( 5, value );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_add_to_client_stream )
{
	// Client methods are only exposed when the flags say so.
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::CLIENT ) );
	desc.setExposedToOwnClient();

	// addSubMessageIDToStream asserts that the exposed id state is set.
	ExposedMethodMessageRange range( 10, 19 );
	desc.setExposedMsgID( 3, 5, &range );

	ScriptTuple args = ScriptTuple::create( 1 );
	args.setItem( 0, ScriptObject::createFrom( 5 ) );

	MemoryOStream stream;
	ScriptDataSource source( args );
	CHECK( desc.addToClientStream( source, stream, 77 ) );

	stream.rewind();
	int32 value = -1;
	stream >> value;
	CHECK_EQUAL( 5, value );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_add_return_values )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args/><ReturnValues><rv> INT32 </rv></ReturnValues></m1>",
		MethodDescription::BASE ) );

	ScriptTuple values = ScriptTuple::create( 1 );
	values.setItem( 0, ScriptObject::createFrom( 7 ) );

	MemoryOStream stream;
	ScriptDataSource source( values );
	CHECK( desc.addReturnValues( source, stream ) );

	stream.rewind();
	uint8 status = 0xFF;
	int32 value = -1;
	stream >> status >> value;
	CHECK_EQUAL( 0, int( status ) );
	CHECK_EQUAL( 7, value );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_md5 )
{
	MethodDescription first;
	CHECK( parseMethodDescription( first,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );

	MethodDescription same;
	CHECK( parseMethodDescription( same,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );

	MethodDescription differentArgs;
	CHECK( parseMethodDescription( differentArgs,
		"<m1><Args><a> STRING </a></Args></m1>",
		MethodDescription::BASE ) );

	MD5 md5First;
	MD5 md5Same;
	MD5 md5DifferentArgs;
	MD5 md5LegacyIndex;

	first.addToMD5( md5First, 3 );
	first.addToMD5( md5Same, 3 );
	differentArgs.addToMD5( md5DifferentArgs, 3 );
	first.addToMD5( md5LegacyIndex, 4 );

	MD5::Digest digestFirst;
	MD5::Digest digestSame;
	MD5::Digest digestDifferentArgs;
	MD5::Digest digestLegacyIndex;

	md5First.getDigest( digestFirst );
	md5Same.getDigest( digestSame );
	md5DifferentArgs.getDigest( digestDifferentArgs );
	md5LegacyIndex.getDigest( digestLegacyIndex );

	CHECK( digestFirst == digestSame );
	CHECK( digestFirst != digestDifferentArgs );
	CHECK( digestFirst != digestLegacyIndex );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_client_safety )
{
	MethodDescription safe;
	CHECK( parseMethodDescription( safe,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );
	CHECK_EQUAL( DataType::CLIENT_SAFE, safe.clientSafety() );

	// MAILBOX resolves to an unsupported type on the server.
	MethodDescription unusable;
	CHECK( parseMethodDescription( unusable,
		"<m1><Args><mb> MAILBOX </mb></Args></m1>",
		MethodDescription::BASE ) );
	CHECK( unusable.clientSafety() & DataType::CLIENT_UNUSABLE );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_should_record )
{
	// The constructor default replay exposure level is OTHER_CLIENTS.
	MethodDescription defaulted;
	CHECK( parseMethodDescription( defaulted,
		"<m1><Args/></m1>", MethodDescription::BASE ) );
	CHECK( defaulted.shouldRecord( RECORDING_OPTION_RECORD ) );
	CHECK( defaulted.shouldRecord( RECORDING_OPTION_RECORD_ONLY ) );
	CHECK( !defaulted.shouldRecord( RECORDING_OPTION_DO_NOT_RECORD ) );
	CHECK( !defaulted.shouldRecord( RECORDING_OPTION_METHOD_DEFAULT ) );

	// An ALL_CLIENTS replay level records even on METHOD_DEFAULT.
	MethodDescription allDesc;
	CHECK( parseMethodDescription( allDesc,
		"<m1><ReplayExposureLevel>ALL_CLIENTS</ReplayExposureLevel>"
		"<Args/></m1>", MethodDescription::BASE ) );
	CHECK( allDesc.shouldRecord( RECORDING_OPTION_METHOD_DEFAULT ) );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_to_string )
{
	// toString() is private; the reachable call site is the setExposedMsgID
	// warning for a sub-indexed method whose variable length header differs
	// from the default. VariableLengthHeaderSize is only valid on client
	// methods, so parse with the CLIENT component.
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><VariableLengthHeaderSize>2<WarnLevel>log</WarnLevel>"
		"</VariableLengthHeaderSize>"
		"<Args><a> INT32 </a></Args></m1>",
		MethodDescription::CLIENT ) );

	CHECK_EQUAL( "log", desc.oversizeWarnLevelAsString() );

	// Assigning a sub-indexed exposed id triggers the warning that calls
	// toString().
	ExposedMethodMessageRange range( 10, 19 );
	desc.setExposedMsgID( 10, 12, &range );
	CHECK( desc.hasSubMsgID() );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_components )
{
	MethodDescription desc;
	CHECK( !desc.isComponentised() );
	CHECK( !desc.isImplementedBy( "c1" ) );

	CHECK( desc.addImplementingComponent( "c1" ) );
	CHECK( desc.isComponentised() );
	CHECK( desc.isImplementedBy( "c1" ) );
	CHECK( !desc.isImplementedBy( "c2" ) );

	// A duplicate or entity-scope component is rejected.
	CHECK( !desc.addImplementingComponent( "c1" ) );
	CHECK( !desc.addImplementingComponent( "" ) );

	// Entity-scope registration conflicts the other way around too.
	MethodDescription entityDesc;
	CHECK( entityDesc.addImplementingComponent( "" ) );
	CHECK( !entityDesc.isComponentised() );
	CHECK( entityDesc.isImplementedBy( "" ) );
	CHECK( !entityDesc.addImplementingComponent( "c1" ) );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_is_signature_equal )
{
	MethodDescription first;
	CHECK( parseMethodDescription( first,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );

	MethodDescription same;
	CHECK( parseMethodDescription( same,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );
	CHECK( first.isSignatureEqual( same ) );

	MethodDescription exposed;
	CHECK( parseMethodDescription( exposed,
		"<m1><Exposed/><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );
	CHECK( !first.isSignatureEqual( exposed ) );

	MethodDescription withReturnValues;
	CHECK( parseMethodDescription( withReturnValues,
		"<m1><Args><a> INT32 </a></Args>"
		"<ReturnValues><rv> INT32 </rv></ReturnValues></m1>",
		MethodDescription::BASE ) );
	CHECK( !first.isSignatureEqual( withReturnValues ) );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_p_watcher )
{
	WatcherPtr pWatcher = MethodDescription::pWatcher();
	CHECK( pWatcher );

	// The watcher is a cached singleton.
	CHECK( pWatcher == MethodDescription::pWatcher() );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_create_error_object )
{
	// This repo ships no BWTwoWay module, so the two-way error factory
	// stays uninitialised. Assert the correlation rather than the failure so
	// the test stays valid if the module is ever added.
	bool initialized = MethodDescription::staticInitTwoWay();
	ScriptObject errObj = MethodDescription::createErrorObject(
		"BWTestError", "message" );
	CHECK_EQUAL( initialized, errObj.exists() );
	clearPendingError();
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_are_valid_args )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args><a> INT32 </a><s> STRING </s></Args></m1>",
		MethodDescription::BASE ) );

	ScriptTuple valid = ScriptTuple::create( 2 );
	valid.setItem( 0, ScriptObject::createFrom( 5 ) );
	valid.setItem( 1, ScriptObject::createFrom( "x" ) );
	CHECK( desc.areValidArgs( false, valid, false ) );

	ScriptTuple wrongCount = ScriptTuple::create( 1 );
	wrongCount.setItem( 0, ScriptObject::createFrom( 5 ) );
	CHECK( !desc.areValidArgs( false, wrongCount, false ) );
	clearPendingError();

	ScriptTuple wrongType = ScriptTuple::create( 2 );
	wrongType.setItem( 0, ScriptObject::createFrom( 5 ) );
	wrongType.setItem( 1, ScriptObject::createFrom( 5 ) );
	CHECK( !desc.areValidArgs( false, wrongType, false ) );
	clearPendingError();

	// The exposed-explicit form requires a source entity id as the first
	// ordinary argument.
	MethodDescription exposed;
	CHECK( parseMethodDescription( exposed,
		"<m1><Exposed/><Args><a> INT32 </a><s> STRING </s></Args></m1>",
		MethodDescription::CELL ) );

	ScriptTuple withSource = ScriptTuple::create( 3 );
	withSource.setItem( 0, ScriptObject( Py_None,
		ScriptObject::FROM_BORROWED_REFERENCE ) );
	withSource.setItem( 1, ScriptObject::createFrom( 5 ) );
	withSource.setItem( 2, ScriptObject::createFrom( "x" ) );
	CHECK( exposed.areValidArgs( true, withSource, false ) );

	ScriptTuple badSource = ScriptTuple::create( 3 );
	badSource.setItem( 0, ScriptObject::createFrom( "not an id" ) );
	badSource.setItem( 1, ScriptObject::createFrom( 5 ) );
	badSource.setItem( 2, ScriptObject::createFrom( "x" ) );
	CHECK( !exposed.areValidArgs( true, badSource, false ) );
	clearPendingError();
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_are_valid_return_values )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args/><ReturnValues><rv> INT32 </rv></ReturnValues></m1>",
		MethodDescription::BASE ) );

	ScriptTuple valid = ScriptTuple::create( 1 );
	valid.setItem( 0, ScriptObject::createFrom( 7 ) );
	CHECK( desc.areValidReturnValues( valid ) );

	ScriptTuple invalid = ScriptTuple::create( 0 );
	CHECK( !desc.areValidReturnValues( invalid ) );
	clearPendingError();
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_extract_source_entity_id )
{
	// A non-exposed method keeps its arguments untouched.
	MethodDescription plain;
	CHECK( parseMethodDescription( plain,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );

	ScriptTuple plainArgs = ScriptTuple::create( 2 );
	plainArgs.setItem( 0, ScriptObject::createFrom( 11 ) );
	plainArgs.setItem( 1, ScriptObject::createFrom( 12 ) );

	EntityID sourceEntityID = 999;
	ScriptTuple unchanged = plain.extractSourceEntityID( plainArgs,
		sourceEntityID );
	CHECK_EQUAL( NULL_ENTITY_ID, sourceEntityID );
	CHECK_EQUAL( 2, int( ScriptTuple( unchanged ).size() ) );

	// An exposed cell method pulls the source id off the front.
	MethodDescription exposed;
	CHECK( parseMethodDescription( exposed,
		"<m1><Exposed/><Args><a> INT32 </a></Args></m1>",
		MethodDescription::CELL ) );

	ScriptTuple exposedArgs = ScriptTuple::create( 2 );
	exposedArgs.setItem( 0, ScriptObject::createFrom( 123 ) );
	exposedArgs.setItem( 1, ScriptObject::createFrom( 5 ) );

	ScriptTuple reduced = exposed.extractSourceEntityID( exposedArgs,
		sourceEntityID );
	CHECK_EQUAL( 123, sourceEntityID );
	CHECK_EQUAL( 1, int( ScriptTuple( reduced ).size() ) );
	int32 value = -1;
	CHECK( ScriptTuple( reduced ).getItem( 0 ).convertTo( value,
		ScriptErrorPrint() ) );
	CHECK_EQUAL( 5, value );

	// A None source id means "no source".
	ScriptTuple noneArgs = ScriptTuple::create( 2 );
	noneArgs.setItem( 0, ScriptObject( Py_None,
		ScriptObject::FROM_BORROWED_REFERENCE ) );
	noneArgs.setItem( 1, ScriptObject::createFrom( 5 ) );

	ScriptTuple noneReduced = exposed.extractSourceEntityID( noneArgs,
		sourceEntityID );
	CHECK_EQUAL( NULL_ENTITY_ID, sourceEntityID );
	CHECK_EQUAL( 1, int( ScriptTuple( noneReduced ).size() ) );

	// An empty tuple is rejected for exposed cell methods.
	ScriptTuple emptyArgs = ScriptTuple::create( 0 );
	ScriptTuple failed = exposed.extractSourceEntityID( emptyArgs,
		sourceEntityID );
	CHECK( failed.get() == NULL );
	clearPendingError();
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_convert_keyword_args )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args><a> INT32 </a><s> STRING </s></Args></m1>",
		MethodDescription::BASE ) );

	ScriptTuple positional = ScriptTuple::create( 0 );

	ScriptDict kwargs = ScriptDict::create( 2 );
	kwargs.setItem( "a", ScriptObject::createFrom( 5 ),
		ScriptErrorPrint( "kwargs" ) );
	kwargs.setItem( "s", ScriptObject::createFrom( "x" ),
		ScriptErrorPrint( "kwargs" ) );

	ScriptObject merged = desc.convertKeywordArgs( positional, kwargs );
	CHECK( merged.exists() );
	CHECK_EQUAL( 2, int( ScriptTuple( merged ).size() ) );

	// The wrong total number of arguments is rejected.
	ScriptDict shortKwargs = ScriptDict::create( 1 );
	shortKwargs.setItem( "a", ScriptObject::createFrom( 5 ),
		ScriptErrorPrint( "kwargs" ) );
	CHECK( !desc.convertKeywordArgs( positional, shortKwargs ).exists() );
	clearPendingError();

	// An unknown keyword name is rejected.
	ScriptTuple onePositional = ScriptTuple::create( 1 );
	onePositional.setItem( 0, ScriptObject::createFrom( 5 ) );
	ScriptDict wrongKey = ScriptDict::create( 1 );
	wrongKey.setItem( "zz", ScriptObject::createFrom( "x" ),
		ScriptErrorPrint( "kwargs" ) );
	CHECK( !desc.convertKeywordArgs( onePositional, wrongKey ).exists() );
	clearPendingError();
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_create_dict_from_tuple )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args/><ReturnValues><rv> INT32 </rv></ReturnValues></m1>",
		MethodDescription::BASE ) );

	ScriptTuple values = ScriptTuple::create( 1 );
	values.setItem( 0, ScriptObject::createFrom( 7 ) );

	ScriptDict dict = desc.createDictFromTuple( values );
	CHECK( dict.exists() );

	int32 value = -1;
	CHECK( dict.getItem( "rv", ScriptErrorPrint( "dict" ) ).convertTo( value,
		ScriptErrorPrint() ) );
	CHECK_EQUAL( 7, value );

	// A tuple of the wrong size is rejected.
	ScriptTuple wrongSize = ScriptTuple::create( 0 );
	CHECK( !desc.createDictFromTuple( wrongSize ).exists() );
	clearPendingError();
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_get_args_as_tuple )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );

	MemoryOStream stream;
	streamInt32( stream, 5 );
	stream.rewind();

	ScriptObject args = desc.getArgsAsTuple( stream );
	CHECK( args.exists() );
	CHECK_EQUAL( 1, int( ScriptTuple( args ).size() ) );

	int32 value = -1;
	CHECK( ScriptTuple( args ).getItem( 0 ).convertTo( value,
		ScriptErrorPrint() ) );
	CHECK_EQUAL( 5, value );

	// An empty stream fails to produce arguments.
	MemoryOStream emptyStream;
	emptyStream.rewind();
	CHECK( !desc.getArgsAsTuple( emptyStream ).exists() );

	// Leftover data on the stream is rejected.
	MemoryOStream extraStream;
	streamInt32( extraStream, 5 );
	streamInt32( extraStream, 6 );
	extraStream.rewind();
	CHECK( !desc.getArgsAsTuple( extraStream ).exists() );
	clearPendingError();

	// Exposed cell methods carry the source entity id as an argument: it is
	// passed in explicitly here because the message header that carried it
	// was stripped before the args reached the stream.
	MethodDescription exposed;
	CHECK( parseMethodDescription( exposed,
		"<m1><Exposed/><Args><a> INT32 </a></Args></m1>",
		MethodDescription::CELL ) );

	MemoryOStream exposedStream;
	streamInt32( exposedStream, 5 );
	exposedStream.rewind();

	ScriptObject exposedArgs = exposed.getArgsAsTuple( exposedStream, 42 );
	RETURN_ON_FAIL_CHECK( exposedArgs.exists() );
	CHECK_EQUAL( 2, int( ScriptTuple( exposedArgs ).size() ) );

	int32 source = -1;
	int32 exposedValue = -1;
	CHECK( ScriptTuple( exposedArgs ).getItem( 0 ).convertTo( source,
		ScriptErrorPrint() ) );
	CHECK( ScriptTuple( exposedArgs ).getItem( 1 ).convertTo( exposedValue,
		ScriptErrorPrint() ) );
	CHECK_EQUAL( 42, source );
	CHECK_EQUAL( 5, exposedValue );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_create_return_values_from_stream )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args/><ReturnValues><rv> INT32 </rv></ReturnValues></m1>",
		MethodDescription::BASE ) );

	MemoryOStream stream;
	streamInt32( stream, 9 );
	stream.rewind();

	ScriptDataSink sink;
	CHECK( desc.createReturnValuesFromStream( stream, sink ) );
	ScriptObject result = sink.finalise();
	CHECK( result.exists() );

	int32 value = -1;
	CHECK( ScriptTuple( result ).getItem( 0 ).convertTo( value,
		ScriptErrorPrint() ) );
	CHECK_EQUAL( 9, value );

	// An empty stream fails.
	MemoryOStream emptyStream;
	ScriptDataSink emptySink;
	CHECK( !desc.createReturnValuesFromStream( emptyStream, emptySink ) );
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_return_values_or_failure )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args/><ReturnValues><rv> INT32 </rv></ReturnValues></m1>",
		MethodDescription::BASE ) );

	// A success payload streams the return values.
	MemoryOStream stream;
	stream << uint8( 0 );
	streamInt32( stream, 5 );
	stream.rewind();

	ScriptObject result;
	CHECK_EQUAL( 0,
		int( desc.createReturnValuesOrFailureFromStream( stream, result ) ) );
	CHECK( result.exists() );

	// A failure payload without pickled args covers the empty-args branch.
	MemoryOStream failureStream;
	failureStream << uint8( 1 );
	failureStream << BW::string( "BWTestError" ) << BW::string();
	failureStream.rewind();

	ScriptObject failureResult;
	CHECK_EQUAL( 1, int( desc.createReturnValuesOrFailureFromStream(
		failureStream, failureResult ) ) );
	// The two-way error factory is uninitialised in this repo, so the
	// failure object itself stays null even though the payload parsed.
	CHECK( !failureResult.exists() );

	// With pickled args the unpickle branch is covered as well.
	ScriptTuple errorArgs = ScriptTuple::create( 1 );
	errorArgs.setItem( 0, ScriptObject::createFrom( "boom" ) );
	BW::string pickled = Pickler::pickle( errorArgs );
	clearPendingError();

	if (!pickled.empty())
	{
		MemoryOStream pickledStream;
		pickledStream << uint8( 1 );
		pickledStream << BW::string( "BWTestError" ) << pickled;
		pickledStream.rewind();

		ScriptObject pickledResult;
		CHECK_EQUAL( 1, int( desc.createReturnValuesOrFailureFromStream(
			pickledStream, pickledResult ) ) );
	}
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_call_method )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );

	// Build a Python object that actually has "m1". PyRun_String with
	// Py_file_input returns None, so define the class with exec and create
	// the instance with a separate eval.
	PyObject * pGlobals = PyDict_New();
	PyObject * pBuiltins = PyImport_AddModule( "builtins" );
	PyDict_SetItemString( pGlobals, "__builtins__",
		PyModule_GetDict( pBuiltins ) );

	PyRun_String(
		"class _BWTestMethod(object):\n"
		"    def m1(self, a):\n"
		"        return a\n",
		Py_file_input, pGlobals, pGlobals );
	clearPendingError();

	ScriptObject self( PyRun_String( "_BWTestMethod()\n",
		Py_eval_input, pGlobals, pGlobals ),
		ScriptObject::STEAL_REFERENCE );

	Py_DECREF( pGlobals );
	clearPendingError();
	RETURN_ON_FAIL_CHECK( self.exists() );
	RETURN_ON_FAIL_CHECK( self.get() != Py_None );

	MemoryOStream stream;
	streamInt32( stream, 42 );
	stream.rewind();

	CHECK( desc.callMethod( self, stream ) );

	// A stream that cannot produce the arguments aborts before the method
	// is looked up.
	MemoryOStream emptyStream;
	emptyStream.rewind();
	CHECK( !desc.callMethod( self, emptyStream ) );
	clearPendingError();
}


TEST_F( EntityDefUnitTestHarness, MethodDescription_is_method_handled_by )
{
	MethodDescription desc;
	CHECK( parseMethodDescription( desc,
		"<m1><Args><a> INT32 </a></Args></m1>",
		MethodDescription::BASE ) );

	PyObject * pGlobals = PyDict_New();
	PyObject * pBuiltins = PyImport_AddModule( "builtins" );
	PyDict_SetItemString( pGlobals, "__builtins__",
		PyModule_GetDict( pBuiltins ) );

	// See MethodDescription_call_method: define with exec, instantiate with
	// a separate eval.
	PyRun_String(
		"class _BWTestMethod(object):\n"
		"    def m1(self, a):\n"
		"        return a\n",
		Py_file_input, pGlobals, pGlobals );
	clearPendingError();

	ScriptObject self( PyRun_String( "_BWTestMethod()\n",
		Py_eval_input, pGlobals, pGlobals ),
		ScriptObject::STEAL_REFERENCE );

	Py_DECREF( pGlobals );
	clearPendingError();
	RETURN_ON_FAIL_CHECK( self.exists() );
	RETURN_ON_FAIL_CHECK( self.get() != Py_None );

	CHECK( desc.getMethodFrom( self ).exists() );
	CHECK( desc.isMethodHandledBy( self ) );

	ScriptObject plain = ScriptObject::createFrom( 7 );
	CHECK( !desc.getMethodFrom( plain ).exists() );
	clearPendingError();
	CHECK( !desc.isMethodHandledBy( plain ) );
	CHECK( !Script::hasError() );
}


BW_END_NAMESPACE

// test_method_description_gaps.cpp
