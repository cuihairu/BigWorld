#include "pch.hpp"

#include "cstdmf/debug.hpp"
#include "resmgr/bwresource.hpp"
#include "resmgr/bin_section.hpp"
#include "resmgr/xml_section.hpp"
#include "resmgr/packed_section.hpp"
#include "resmgr/dir_section.hpp"


BW_BEGIN_NAMESPACE

namespace
{

DataSectionPtr b26xml( const char * string )
{
	BW::stringstream sstream;
	sstream << string;

	return XMLSection::createFromStream( "", sstream ).get();
}

BinaryPtr b26binary( const char * data, size_t len )
{
	return new BinaryBlock( data, len, "BinaryBlock/test_datasection" );
}

/// Serves createAppropriateSection's creator dispatch arm.
class B26TestCreator : public DataSectionCreator
{
public:
	DataSectionPtr create( DataSectionPtr /*pSection*/,
		const BW::StringRef& /*tag*/ ) override
	{
		return NULL;
	}

	DataSectionPtr load( DataSectionPtr /*pSection*/,
		const BW::StringRef& tag, BinaryPtr pBinary ) override
	{
		return new BinSection( tag.to_string(), pBinary );
	}
};

/// A minimal concrete DataSection that overrides nothing beyond the pure
/// virtuals, so the base-class asBinary() default is reachable.
class B26BareSection : public DataSection
{
public:
	int countChildren() override { return 0; }
	DataSectionPtr openChild( int ) override { return NULL; }
	DataSectionPtr newSection( const BW::StringRef&,
		DataSectionCreator* = NULL ) override
	{
		return NULL;
	}
	DataSectionPtr findChild( const BW::StringRef&,
		DataSectionCreator* = NULL ) override
	{
		return NULL;
	}
	void delChild( const BW::StringRef& ) override {}
	void delChild( DataSectionPtr ) override {}
	void delChildren() override {}
	BW::string sectionName() const override { return "Bare"; }
	int bytes() const override { return 0; }
	bool save( const BW::StringRef& ) override { return false; }
};

} // anonymous namespace


TEST_F( ResMgrUnitTestHarness, DataSection_iteratorBasics )
{
	CHECK( this->isOK() );

	DataSectionPtr root = b26xml( "<root><a/><b/><c/></root>" );
	CHECK_EQUAL( 3, root->countChildren() );

	// Default-constructed iterators compare equal to each other only.
	DataSectionIterator dflt;
	CHECK( dflt == DataSectionIterator() );
	CHECK( dflt != root->begin() );

	// Full iteration through the pre-increment protocol.
	DataSectionIterator it = root->begin();
	DataSectionIterator itEnd = root->end();
	CHECK( it != itEnd );
	CHECK( !( it == itEnd ) );

	int count = 0;
	while (it != itEnd)
	{
		CHECK( (*it).exists() );
		++it;
		++count;
	}
	CHECK_EQUAL( 3, count );
	CHECK( it == itEnd );

	// tag() resolves the child name through childSectionName.
	CHECK_EQUAL( BW::string( "a" ), root->begin().tag() );
	DataSectionIterator bumped = root->begin();
	++bumped;
	++bumped;
	CHECK_EQUAL( BW::string( "c" ), bumped.tag() );

	// operator== is false for the same index over different sections.
	CHECK( root->begin() != b26xml( "<root><a/></root>" )->begin() );

	// Post-increment advances but hands back a copy of the old position.
	it = root->begin();
	DataSectionIterator old = it++;
	CHECK_EQUAL( BW::string( "a" ), old.tag() );
	CHECK_EQUAL( BW::string( "b" ), it.tag() );

	// operator* hands back the child the index points at.
	CHECK_EQUAL( BW::string( "a" ), root->begin().operator*()->sectionName() );
}


TEST_F( ResMgrUnitTestHarness, DataSection_indexAndChildName )
{
	DataSectionPtr root = b26xml( "<root><a/><b/><c/></root>" );

	DataSectionPtr second = root->openChild( 1 );
	CHECK_EQUAL( 1, root->getIndex( second ) );
	CHECK_EQUAL( 0, root->getIndex( root->openChild( 0 ) ) );

	// A section that is not a child has no index here.
	DataSectionPtr stranger = b26xml( "<root><a/></root>" );
	CHECK_EQUAL( -1, root->getIndex( stranger ) );

	CHECK_EQUAL( BW::string( "a" ), root->childSectionName( 0 ) );
	CHECK_EQUAL( BW::string( "c" ), root->childSectionName( 2 ) );

	// openFirstSection: first child vs nothing to open.
	CHECK_EQUAL( BW::string( "a" ), root->openFirstSection()->sectionName() );
	DataSectionPtr empty = b26xml( "<empty/>" );
	CHECK( !empty->openFirstSection().exists() );
}


TEST_F( ResMgrUnitTestHarness, DataSection_openSectionPaths )
{
	DataSectionPtr root = b26xml( "<root><a><b/></a></root>" );

	// Empty path resolves to the section itself.
	CHECK( root->openSection( "" ) == root );

	CHECK( root->openSection( "a/b" ).exists() );

	// A trailing slash ends the walk at the named child.
	CHECK( root->openSection( "a/" ) == root->openChild( 0 ) );

	// Deep path with a missing leaf: refuse or create.
	CHECK( !root->openSection( "a/zz" ).exists() );
	CHECK( root->openSection( "a/zz", true ).exists() );
	CHECK( root->openSection( "a/zz" ).exists() );

	// Single missing tag with makeNewSection.
	CHECK( !root->openSection( "missing" ).exists() );
	CHECK( root->openSection( "newOne", true ).exists() );
	CHECK( root->openSection( "newOne" ).exists() );
}


TEST_F( ResMgrUnitTestHarness, DataSection_openSections )
{
	DataSectionPtr root = b26xml(
		"<root><a><b/><b/></a><b/><b/><c/></root>" );

	// Flat match walks the children directly.
	BW::vector<DataSectionPtr> dest;
	root->openSections( "b", dest );
	CHECK_EQUAL( 2, int( dest.size() ) );

	// Nested path recurses into the named parent only.
	dest.clear();
	root->openSections( "a/b", dest );
	CHECK_EQUAL( 2, int( dest.size() ) );
	CHECK_EQUAL( BW::string( "b" ), dest[ 0 ]->sectionName() );

	// A missing parent in a nested path leaves dest untouched.
	dest.clear();
	root->openSections( "zz/b", dest );
	CHECK( dest.empty() );

	// A flat miss also leaves dest untouched.
	dest.clear();
	root->openSections( "missing", dest );
	CHECK( dest.empty() );
}


TEST_F( ResMgrUnitTestHarness, DataSection_deleteSectionPaths )
{
	DataSectionPtr root = b26xml(
		"<root><a><b><c/></b></a><dup/><dup/><dup/></root>" );

	// Multi-token path walks parent links and removes the leaf.
	CHECK( root->deleteSection( "a/b/c" ) );
	CHECK( !root->openSection( "a/b/c" ).exists() );
	CHECK( root->openSection( "a/b" ).exists() );

	// The same path no longer resolves.
	CHECK( !root->deleteSection( "a/b/c" ) );

	// A missing top-level tag fails too.
	CHECK( !root->deleteSection( "no_such" ) );

	// deleteSections sweeps every match under the tag.
	root->deleteSections( "dup" );
	BW::vector<DataSectionPtr> left;
	root->openSections( "dup", left );
	CHECK( left.empty() );
}


TEST_F( ResMgrUnitTestHarness, DataSection_scalarReadWrite )
{
	DataSectionPtr root = b26xml( "<root/>" );

	CHECK( root->writeBool( "flag", true ) );
	CHECK( root->writeInt( "num", -42 ) );
	CHECK( root->writeUInt( "unum", 42 ) );
	CHECK( root->writeLong( "lnum", -44 ) );
	CHECK( root->writeInt64( "i64", -46 ) );
	CHECK( root->writeUInt64( "u64", 48 ) );
	CHECK( root->writeFloat( "flt", 1.5f ) );
	CHECK( root->writeDouble( "dbl", 2.5 ) );
	CHECK( root->writeString( "str", "hello" ) );

	CHECK_EQUAL( true, root->readBool( "flag", false ) );
	CHECK_EQUAL( -42, root->readInt( "num", 0 ) );
	CHECK_EQUAL( 42u, root->readUInt( "unum", 0 ) );
	CHECK_EQUAL( -44L, root->readLong( "lnum", 0L ) );
	CHECK_EQUAL( (int64)-46, root->readInt64( "i64", 0 ) );
	CHECK_EQUAL( (uint64)48, root->readUInt64( "u64", 0 ) );
	CHECK_CLOSE( 1.5f, root->readFloat( "flt", 0.f ), 0.0001f );
	CHECK_CLOSE( 2.5, root->readDouble( "dbl", 0. ), 0.0001 );
	CHECK_EQUAL( BW::string( "hello" ), root->readString( "str", "" ) );

	// Deep paths write and read through the same helpers.
	CHECK( root->writeInt( "deep/er/path", 9 ) );
	CHECK_EQUAL( 9, root->readInt( "deep/er/path", 0 ) );

	// Every reader falls back to its default on a missing tag.
	CHECK_EQUAL( true, root->readBool( "missing", true ) );
	CHECK_EQUAL( -7, root->readInt( "missing", -7 ) );
	CHECK_EQUAL( 7u, root->readUInt( "missing", 7 ) );
	CHECK_EQUAL( -9L, root->readLong( "missing", -9L ) );
	CHECK_EQUAL( (int64)-11, root->readInt64( "missing", -11 ) );
	CHECK_EQUAL( (uint64)13, root->readUInt64( "missing", 13 ) );
	CHECK_CLOSE( 1.25f, root->readFloat( "missing", 1.25f ), 0.0001f );
	CHECK_CLOSE( 2.75, root->readDouble( "missing", 2.75 ), 0.0001 );
	CHECK_EQUAL( BW::string( "d" ), root->readString( "missing", "d" ) );
}


TEST_F( ResMgrUnitTestHarness, DataSection_wideBlobBinaryReadWrite )
{
	DataSectionPtr root = b26xml( "<root/>" );

	CHECK( root->writeWideString( "wstr", L"wide" ) );
	CHECK( root->readWideString( "wstr", L"" ) == BW::wstring( L"wide" ) );
	CHECK( root->readWideString( "missing", L"wd" ) == BW::wstring( L"wd" ) );

	CHECK( root->writeBlob( "blob", "abc" ) );
	CHECK_EQUAL( BW::string( "abc" ), root->readBlob( "blob", "" ) );
	CHECK_EQUAL( BW::string( "bd" ), root->readBlob( "missing", "bd" ) );

	// Binary round-trips need a host whose children accept setBinary;
	// XML sections stringify their children, so a BinSection hosts this one.
	BinaryPtr payload = b26binary( "XYZ", 3 );
	DataSectionPtr binHost( new BinSection( "binHost",
		b26binary( "PARENT", 6 ) ) );
	CHECK( binHost->writeBinary( "bin", payload ) );
	BinaryPtr back = binHost->readBinary( "bin" );
	CHECK( back.exists() );
	CHECK( back->compare( *payload ) == 0 );

	// A missing binary tag reads back as nothing.
	CHECK( !binHost->readBinary( "missing" ).exists() );

	// The DataSection base setBinary refuses; writeBinary through an XML
	// host surfaces that refusal since XML children keep the base version.
	CHECK( !b26xml( "<r/>" )->writeBinary( "x", payload ) );
}


TEST_F( ResMgrUnitTestHarness, DataSection_vectorMatrixReadWrite )
{
	DataSectionPtr root = b26xml( "<root/>" );

	CHECK( root->writeVector2( "v2", Vector2( 1.f, 2.f ) ) );
	Vector2 v2 = root->readVector2( "v2", Vector2( 0.f, 0.f ) );
	CHECK_CLOSE( 1.f, v2.x, 0.001f );
	CHECK_CLOSE( 2.f, v2.y, 0.001f );

	CHECK( root->writeVector3( "v3", Vector3( 1.f, 2.f, 3.f ) ) );
	Vector3 v3 = root->readVector3( "v3", Vector3( 0.f, 0.f, 0.f ) );
	CHECK_CLOSE( 3.f, v3.z, 0.001f );

	CHECK( root->writeVector4( "v4", Vector4( 1.f, 2.f, 3.f, 4.f ) ) );
	Vector4 v4 = root->readVector4( "v4", Vector4( 0.f, 0.f, 0.f, 0.f ) );
	CHECK_CLOSE( 4.f, v4.w, 0.001f );

	Matrix mOut;
	mOut.setTranslate( 1.f, 2.f, 3.f );
	CHECK( root->writeMatrix34( "m34", mOut ) );
	Matrix mBack = root->readMatrix34( "m34", Matrix::identity );
	Vector3 origin = mBack.applyToOrigin();
	CHECK_CLOSE( 1.f, origin.x, 0.001f );
	CHECK_CLOSE( 2.f, origin.y, 0.001f );
	CHECK_CLOSE( 3.f, origin.z, 0.001f );

	// Missing tags fall back to the supplied defaults.
	Vector2 v2d = root->readVector2( "missing", Vector2( 9.f, 8.f ) );
	CHECK_CLOSE( 9.f, v2d.x, 0.001f );
	CHECK_CLOSE( 8.f, v2d.y, 0.001f );
	Vector3 v3d = root->readVector3( "missing", Vector3( 7.f, 6.f, 5.f ) );
	CHECK_CLOSE( 5.f, v3d.z, 0.001f );
	Vector4 v4d = root->readVector4( "missing", Vector4( 4.f, 3.f, 2.f, 1.f ) );
	CHECK_CLOSE( 1.f, v4d.w, 0.001f );
	Matrix mDefault;
	mDefault.setTranslate( 5.f, 6.f, 7.f );
	Vector3 mOrigin = root->readMatrix34( "missing", mDefault ).applyToOrigin();
	CHECK_CLOSE( 7.f, mOrigin.z, 0.001f );
}


TEST_F( ResMgrUnitTestHarness, DataSection_pluralScalarReadWrite )
{
	DataSectionPtr root = b26xml( "<root/>" );

	// Flat tag: write then read back the full sequence.
	BW::vector<int> ints;
	ints.push_back( 1 );
	ints.push_back( 2 );
	ints.push_back( 3 );
	root->writeInts( "ints", ints );
	BW::vector<int> intsBack;
	root->readInts( "ints", intsBack );
	CHECK_EQUAL( 3, int( intsBack.size() ) );
	CHECK_EQUAL( 2, intsBack[ 1 ] );

	// Nested tag: the plural helpers split the path and address a parent.
	BW::vector<bool> bools;
	bools.push_back( true );
	bools.push_back( false );
	root->writeBools( "pv/pb", bools );
	BW::vector<bool> boolsBack;
	root->readBools( "pv/pb", boolsBack );
	CHECK_EQUAL( 2, int( boolsBack.size() ) );
	CHECK_EQUAL( true, boolsBack[ 0 ] );
	CHECK_EQUAL( false, boolsBack[ 1 ] );

	BW::vector<long> longs;
	longs.push_back( -8L );
	longs.push_back( 9L );
	root->writeLongs( "lngs", longs );
	BW::vector<long> longsBack;
	root->readLongs( "lngs", longsBack );
	CHECK_EQUAL( 2, int( longsBack.size() ) );
	CHECK_EQUAL( 9L, longsBack[ 1 ] );

	BW::vector<float> floats;
	floats.push_back( 0.5f );
	root->writeFloats( "flts", floats );
	BW::vector<float> floatsBack;
	root->readFloats( "flts", floatsBack );
	CHECK_EQUAL( 1, int( floatsBack.size() ) );
	CHECK_CLOSE( 0.5f, floatsBack[ 0 ], 0.0001f );

	BW::vector<double> doubles;
	doubles.push_back( 1.25 );
	root->writeDoubles( "dbls", doubles );
	BW::vector<double> doublesBack;
	root->readDoubles( "dbls", doublesBack );
	CHECK_EQUAL( 1, int( doublesBack.size() ) );
	CHECK_CLOSE( 1.25, doublesBack[ 0 ], 0.0001 );

	BW::vector<BW::string> strings;
	strings.push_back( "one" );
	strings.push_back( "two" );
	root->writeStrings( "strs", strings );
	BW::vector<BW::string> stringsBack;
	root->readStrings( "strs", stringsBack );
	CHECK_EQUAL( 2, int( stringsBack.size() ) );
	CHECK_EQUAL( BW::string( "two" ), stringsBack[ 1 ] );

	BW::vector<BW::wstring> wstrings;
	wstrings.push_back( BW::wstring( L"one" ) );
	root->writeWideStrings( "wstrs", wstrings );
	BW::vector<BW::wstring> wstringsBack;
	root->readWideStrings( "wstrs", wstringsBack );
	CHECK_EQUAL( 1, int( wstringsBack.size() ) );
	CHECK( wstringsBack[ 0 ] == BW::wstring( L"one" ) );

	// A plural read whose parent path does not resolve returns nothing.
	BW::vector<int> noParent;
	root->readInts( "no/parent", noParent );
	CHECK( noParent.empty() );
}


TEST_F( ResMgrUnitTestHarness, DataSection_pluralVectorMatrixReadWrite )
{
	DataSectionPtr root = b26xml( "<root/>" );

	BW::vector<Vector2> v2s;
	v2s.push_back( Vector2( 1.f, 2.f ) );
	v2s.push_back( Vector2( 3.f, 4.f ) );
	root->writeVector2s( "pv/v2s", v2s );
	BW::vector<Vector2> v2sBack;
	root->readVector2s( "pv/v2s", v2sBack );
	CHECK_EQUAL( 2, int( v2sBack.size() ) );
	CHECK_CLOSE( 3.f, v2sBack[ 1 ].x, 0.001f );

	BW::vector<Vector3> v3s;
	v3s.push_back( Vector3( 1.f, 2.f, 3.f ) );
	root->writeVector3s( "pv/v3s", v3s );
	BW::vector<Vector3> v3sBack;
	root->readVector3s( "pv/v3s", v3sBack );
	CHECK_EQUAL( 1, int( v3sBack.size() ) );
	CHECK_CLOSE( 3.f, v3sBack[ 0 ].z, 0.001f );

	BW::vector<Vector4> v4s;
	v4s.push_back( Vector4( 1.f, 2.f, 3.f, 4.f ) );
	root->writeVector4s( "pv/v4s", v4s );
	BW::vector<Vector4> v4sBack;
	root->readVector4s( "pv/v4s", v4sBack );
	CHECK_EQUAL( 1, int( v4sBack.size() ) );
	CHECK_CLOSE( 4.f, v4sBack[ 0 ].w, 0.001f );

	Matrix mOut;
	mOut.setTranslate( 1.f, 2.f, 3.f );
	BW::vector<Matrix> mats;
	mats.push_back( mOut );
	mats.push_back( mOut );
	root->writeMatrix34s( "pv/m34s", mats );
	BW::vector<Matrix> matsBack;
	root->readMatrix34s( "pv/m34s", matsBack );
	CHECK_EQUAL( 2, int( matsBack.size() ) );
	CHECK_CLOSE( 3.f, matsBack[ 1 ].applyToOrigin().z, 0.001f );

	// Missing parent leaves the destination vectors empty.
	BW::vector<Vector4> missBack;
	root->readVector4s( "no/x", missBack );
	CHECK( missBack.empty() );
}


TEST_F( ResMgrUnitTestHarness, DataSection_baseAsDefaultsViaBinSection )
{
	// BinSection overrides only the binary accessors, so every other as*
	// dispatches to the DataSection base implementation and echoes the
	// caller's default back.
	BinaryPtr bin = b26binary( "DATA", 4 );
	DataSectionPtr sec( new BinSection( "b26bin", bin ) );

	CHECK_EQUAL( true, sec->asBool( true ) );
	CHECK_EQUAL( false, sec->asBool( false ) );
	CHECK_EQUAL( -3, sec->asInt( -3 ) );
	CHECK_EQUAL( 3u, sec->asUInt( 3 ) );
	CHECK_EQUAL( -5L, sec->asLong( -5L ) );
	CHECK_EQUAL( (int64)-7, sec->asInt64( -7 ) );
	CHECK_EQUAL( (uint64)9, sec->asUInt64( 9 ) );
	CHECK_CLOSE( 1.25f, sec->asFloat( 1.25f ), 0.0001f );
	CHECK_CLOSE( 2.75, sec->asDouble( 2.75 ), 0.0001 );
	CHECK_EQUAL( BW::string( "dflt" ), sec->asString( "dflt" ) );
	CHECK( sec->asWideString( L"w" ) == BW::wstring( L"w" ) );

	Vector2 v2 = sec->asVector2( Vector2( 1.f, 2.f ) );
	CHECK_CLOSE( 1.f, v2.x, 0.001f );
	CHECK_CLOSE( 2.f, v2.y, 0.001f );
	Vector3 v3 = sec->asVector3( Vector3( 1.f, 2.f, 3.f ) );
	CHECK_CLOSE( 3.f, v3.z, 0.001f );
	Vector4 v4 = sec->asVector4( Vector4( 1.f, 2.f, 3.f, 4.f ) );
	CHECK_CLOSE( 4.f, v4.w, 0.001f );

	Matrix m;
	m.setTranslate( 1.f, 2.f, 3.f );
	Vector3 origin = sec->asMatrix34( m ).applyToOrigin();
	CHECK_CLOSE( 1.f, origin.x, 0.001f );
	CHECK_CLOSE( 3.f, origin.z, 0.001f );

	CHECK_EQUAL( BW::string( "bl" ), sec->asBlob( "bl" ) );

	// BinSection's own binary accessor round-trips the payload.
	CHECK( sec->asBinary().exists() );
	CHECK( sec->asBinary()->compare( *bin ) == 0 );

	// A section that overrides nothing reaches the base asBinary, which
	// reports no binary content.
	DataSectionPtr bare( new B26BareSection() );
	CHECK( !bare->asBinary().exists() );
}


TEST_F( ResMgrUnitTestHarness, DataSection_baseSetNoOpsViaBinSection )
{
	// The base set* implementations all refuse; BinSection overrides only
	// setBinary, which accepts.
	BinaryPtr bin = b26binary( "DATA", 4 );
	DataSectionPtr sec( new BinSection( "b26set", bin ) );

	CHECK( !sec->setBool( true ) );
	CHECK( !sec->setInt( 1 ) );
	CHECK( !sec->setUInt( 1 ) );
	CHECK( !sec->setLong( 1 ) );
	CHECK( !sec->setInt64( 1 ) );
	CHECK( !sec->setUInt64( 1 ) );
	CHECK( !sec->setFloat( 1.f ) );
	CHECK( !sec->setDouble( 1. ) );
	CHECK( !sec->setString( "x" ) );
	CHECK( !sec->setWideString( L"x" ) );
	CHECK( !sec->setVector2( Vector2( 1.f, 1.f ) ) );
	CHECK( !sec->setVector3( Vector3( 1.f, 1.f, 1.f ) ) );
	CHECK( !sec->setVector4( Vector4( 1.f, 1.f, 1.f, 1.f ) ) );
	CHECK( !sec->setMatrix34( Matrix::identity ) );
	CHECK( !sec->setBlob( "x" ) );

	CHECK( sec->setBinary( bin ) );
}


TEST_F( ResMgrUnitTestHarness, DataSection_createAppropriateSection )
{
	// Valid XML parses into an XMLSection tree.
	BinaryPtr xmlData = b26binary( "<r><c/></r>", 11 );
	DataSectionPtr sec = DataSection::createAppropriateSection( "tag1", xmlData );
	CHECK( sec.exists() );
	CHECK_EQUAL( BW::string( "c" ), sec->childSectionName( 0 ) );

	// allowModifyData=false copies the buffer before the XML parse touches it.
	DataSectionPtr sec2 =
		DataSection::createAppropriateSection( "tag2", xmlData, false );
	CHECK( sec2.exists() );
	CHECK_EQUAL( '<', xmlData->cdata()[ 0 ] );

	// Input that starts with '<' but is not valid XML, with the copy
	// protection on, falls back to a binary section.
	BinaryPtr bad = b26binary( "<not closed", 11 );
	DataSectionPtr sec3 =
		DataSection::createAppropriateSection( "tag3", bad, false );
	CHECK( sec3.exists() );
	CHECK( dynamic_cast< BinSection * >( sec3.getObject() ) != NULL );

	// The same garbage with the copy protection off reports a parse failure
	// by handing back an empty pointer.
	DataSectionPtr sec4 =
		DataSection::createAppropriateSection( "tag4", bad, true );
	CHECK( !sec4.exists() );

	// A supplied creator short-circuits the sniffing entirely.
	B26TestCreator creator;
	DataSectionPtr sec5 = DataSection::createAppropriateSection(
		"tag5", xmlData, true, &creator );
	CHECK( sec5.exists() );
	CHECK( dynamic_cast< BinSection * >( sec5.getObject() ) != NULL );
}


TEST_F( ResMgrUnitTestHarness, DataSection_splitSaveAsFileNameViaSave )
{
	// splitSaveAsFileName is protected and runs inside the concrete save()
	// implementations; drive it from there. Each arm uses its own probe so
	// the observable tag rewrite of one arm cannot confuse another.

	BinaryPtr bin = b26binary( "DATA", 4 );

	// An unresolvable parent refuses before any write is attempted and
	// leaves the section name untouched.
	DataSectionPtr missing( new BinSection( "probeA", bin ) );
	CHECK( !missing->save( "b26_no_dir/x" ) );
	CHECK_EQUAL( BW::string( "probeA" ), missing->sectionName() );

	// No slash: the parent becomes the resource root and the tag the whole
	// name; the write then fails on the name of the current directory.
	DataSectionPtr noslash( new BinSection( "probeB", bin ) );
	CHECK( !noslash->save( "." ) );
	CHECK_EQUAL( BW::string( "." ), noslash->sectionName() );

	// With a slash: the parent opens by path (exist.zip) and the tag is the
	// last element. Whether the zip child write sticks depends on the
	// cached zip's introspection state, so only the split contract is
	// asserted; the cache is purged so later suites see an untouched zip.
	BWResource::instance().purgeAll();
	DataSectionPtr slashed( new BinSection( "probeC", bin ) );
	slashed->save( "exist.zip/sub" );
	CHECK_EQUAL( BW::string( "sub" ), slashed->sectionName() );
	BWResource::instance().purgeAll();
}


TEST_F( ResMgrUnitTestHarness, DataSection_setWatcherValues )
{
	// An empty path means no watcher to set; the walk still recurses.
	DataSectionPtr root = b26xml( "<r><a>1</a><b>2</b></r>" );
	root->setWatcherValues( "" );

	// A section drives its own text into the watcher at the given path.
	int wVal = 0;
	WatcherPtr wWatcher =
		new DataWatcher< int >( wVal, Watcher::WT_READ_WRITE );
	CHECK( Watcher::rootWatcher().addChild( "batch26/w26", wWatcher ) );

	DataSectionPtr v77 = b26xml( "<v>77</v>" );
	v77->setWatcherValues( "batch26/w26" );
	CHECK_EQUAL( 77, wVal );
	CHECK( Watcher::rootWatcher().removeChild( "batch26/w26" ) );

	// A path with a dot retries once with the dots turned into spaces.
	int dVal = 0;
	WatcherPtr dWatcher =
		new DataWatcher< int >( dVal, Watcher::WT_READ_WRITE );
	CHECK( Watcher::rootWatcher().addChild( "batch26/dot name", dWatcher ) );

	v77->setWatcherValues( "batch26/dot.name" );
	CHECK_EQUAL( 77, dVal );
	CHECK( Watcher::rootWatcher().removeChild( "batch26/dot name" ) );

	// An empty section value fails both attempts without a warning.
	DataSectionPtr vEmpty = b26xml( "<e></e>" );
	vEmpty->setWatcherValues( "batch26/none" );
}


TEST_F( ResMgrUnitTestHarness, DataSection_copy )
{
	// BinSection to BinSection with modifyCurrent copies the blob outright.
	DataSectionPtr srcB( new BinSection( "s", b26binary( "NEWB", 4 ) ) );
	DataSectionPtr dstB( new BinSection( "d", b26binary( "OLDB", 4 ) ) );
	dstB->copy( srcB, true );
	CHECK( dstB->asBinary()->compare( *srcB->asBinary() ) == 0 );

	// Without modifyCurrent the short-circuit does not apply and a childless
	// source leaves the destination binary untouched.
	DataSectionPtr dstC( new BinSection( "d2", b26binary( "OLDB", 4 ) ) );
	dstC->copy( srcB, false );
	CHECK( dstC->asBinary()->compare( *srcB->asBinary() ) != 0 );

	// Section to section with modifyCurrent replaces the value and children.
	DataSectionPtr srcX =
		b26xml( "<tree>val<c1>x</c1><c2>y</c2></tree>" );
	DataSectionPtr dstX = b26xml( "<tree><old/></tree>" );
	dstX->copy( srcX, true );
	CHECK_EQUAL( BW::string( "val" ), dstX->asString() );
	CHECK_EQUAL( 2, dstX->countChildren() );
	CHECK( dstX->openChild( 0 )->compare( srcX->openChild( 0 ) ) == 0 );
	CHECK( dstX->openChild( 1 )->compare( srcX->openChild( 1 ) ) == 0 );
	CHECK( dstX->isAttribute() == srcX->isAttribute() );

	// The full copy leaves the trees comparable.
	CHECK( dstX->compare( srcX ) == 0 );
}


TEST_F( ResMgrUnitTestHarness, DataSection_copySections )
{
	// Tagged copy takes only the matching children, in order.
	DataSectionPtr src2 = b26xml(
		"<src><keep>1</keep><toss>2</toss><keep>3</keep></src>" );
	DataSectionPtr dst2 = b26xml( "<dst/>" );
	dst2->copySections( src2, "keep" );
	CHECK_EQUAL( 2, dst2->countChildren() );
	CHECK_EQUAL( BW::string( "keep" ), dst2->childSectionName( 0 ) );
	CHECK( dst2->openChild( 0 )->compare( src2->openChild( 0 ) ) == 0 );
	CHECK( dst2->openChild( 1 )->compare( src2->openChild( 2 ) ) == 0 );

	// Untagged copy merges same-named children into one slot, last wins.
	DataSectionPtr src3 = b26xml( "<src>top<a>1</a><a>2</a></src>" );
	DataSectionPtr dst3 = b26xml( "<dst/>" );
	dst3->copySections( src3 );
	CHECK_EQUAL( BW::string( "top" ), dst3->asString() );
	CHECK_EQUAL( 1, dst3->countChildren() );
	CHECK_EQUAL( BW::string( "a" ), dst3->childSectionName( 0 ) );
	CHECK_EQUAL( BW::string( "2" ), dst3->openChild( 0 )->asString() );
}


TEST_F( ResMgrUnitTestHarness, DataSection_compare )
{
	DataSectionPtr none;
	CHECK_EQUAL( 1, b26xml( "<a/>" )->compare( none ) );

	// Child section names differ.
	DataSectionPtr a = b26xml( "<r><aaa>1</aaa></r>" );
	DataSectionPtr b = b26xml( "<r><bbb>1</bbb></r>" );
	CHECK( a->openChild( 0 )->compare( b->openChild( 0 ) ) != 0 );

	// Child values differ.
	DataSectionPtr c = b26xml( "<r><x>1</x></r>" );
	DataSectionPtr d = b26xml( "<r><x>2</x></r>" );
	CHECK( c->openChild( 0 )->compare( d->openChild( 0 ) ) != 0 );

	// Identical subtrees compare equal.
	DataSectionPtr cTwin = b26xml( "<r><x>1</x></r>" );
	CHECK_EQUAL( 0, c->openChild( 0 )->compare( cTwin->openChild( 0 ) ) );

	// Child counts differ.
	DataSectionPtr e = b26xml( "<r><x>1</x><y/></r>" );
	CHECK( c->compare( e ) != 0 );
}


TEST_F( ResMgrUnitTestHarness, DataSection_binaryCompare )
{
	DataSectionPtr none;
	DataSectionPtr b1( new BinSection( "x", b26binary( "AAAA", 4 ) ) );
	DataSectionPtr b2( new BinSection( "x", b26binary( "AAAA", 4 ) ) );
	DataSectionPtr b3( new BinSection( "x", b26binary( "BBBB", 4 ) ) );
	DataSectionPtr b4( new BinSection( "y", b26binary( "AAAA", 4 ) ) );

	CHECK_EQUAL( 1, b1->binaryCompare( none ) );

	// Childless sections fall back to comparing their binary payloads.
	CHECK_EQUAL( 0, b1->binaryCompare( b2 ) );
	CHECK( b1->binaryCompare( b3 ) != 0 );

	// Names differ.
	CHECK( b1->binaryCompare( b4 ) != 0 );

	// Child counts differ.
	DataSectionPtr x1 = b26xml( "<r><c/></r>" );
	DataSectionPtr x2 = b26xml( "<r><c/><d/></r>" );
	CHECK( x1->binaryCompare( x2 ) != 0 );

	// A differing child stops the walk with a non-zero result.
	DataSectionPtr x3 = b26xml( "<r><c>1</c><z/></r>" );
	DataSectionPtr x4 = b26xml( "<r><c>2</c><z/></r>" );
	CHECK( x3->binaryCompare( x4 ) != 0 );

	// Equal trees walk all the way to zero.
	DataSectionPtr x5 = b26xml( "<r><c/></r>" );
	CHECK_EQUAL( 0, x1->binaryCompare( x5 ) );
}


TEST_F( ResMgrUnitTestHarness, DataSection_pluralMissingParentGuards )
{
	// Every plural reader and writer refuses quietly when the parent path
	// does not resolve. A DirSection over a nonexistent base directory makes
	// the parent creation inside the writers fail too, so both guard
	// families run; nothing touches the filesystem because mkdir of a
	// nested path never creates the missing parents.
	DirSection dirHost( "b26_missing_root_dir",
		BWResource::instance().nativeFileSystem() );

	// Readers leave the destination empty.
	BW::vector<bool> bools;
	dirHost.readBools( "no/parent", bools );
	CHECK( bools.empty() );

	BW::vector<long> longs;
	dirHost.readLongs( "no/parent", longs );
	CHECK( longs.empty() );

	BW::vector<float> floats;
	dirHost.readFloats( "no/parent", floats );
	CHECK( floats.empty() );

	BW::vector<double> doubles;
	dirHost.readDoubles( "no/parent", doubles );
	CHECK( doubles.empty() );

	BW::vector<BW::string> strings;
	dirHost.readStrings( "no/parent", strings );
	CHECK( strings.empty() );

	BW::vector<BW::wstring> wstrings;
	dirHost.readWideStrings( "no/parent", wstrings );
	CHECK( wstrings.empty() );

	BW::vector<Vector2> v2s;
	dirHost.readVector2s( "no/parent", v2s );
	CHECK( v2s.empty() );

	BW::vector<Vector3> v3s;
	dirHost.readVector3s( "no/parent", v3s );
	CHECK( v3s.empty() );

	BW::vector<Matrix> mats;
	dirHost.readMatrix34s( "no/parent", mats );
	CHECK( mats.empty() );

	// Writers are no-ops and leave no children behind.
	BW::vector<int> ints;
	BW::vector<Vector4> v4s;
	dirHost.writeBools( "no/parent", bools );
	dirHost.writeInts( "no/parent", ints );
	dirHost.writeLongs( "no/parent", longs );
	dirHost.writeFloats( "no/parent", floats );
	dirHost.writeDoubles( "no/parent", doubles );
	dirHost.writeStrings( "no/parent", strings );
	dirHost.writeWideStrings( "no/parent", wstrings );
	dirHost.writeVector2s( "no/parent", v2s );
	dirHost.writeVector3s( "no/parent", v3s );
	dirHost.writeVector4s( "no/parent", v4s );
	dirHost.writeMatrix34s( "no/parent", mats );

	CHECK_EQUAL( 0, dirHost.countChildren() );
}


TEST_F( ResMgrUnitTestHarness, DataSection_sanitiseHelpers )
{
	// Hosted on a BinSection so the DataSection base implementations run;
	// XMLSection overrides the name mapping itself.
	DataSectionPtr sec( new BinSection( "base", b26binary( "D", 1 ) ) );

	CHECK_EQUAL( BW::string( "as is" ), sec->sanitise( "as is" ) );
	CHECK_EQUAL( BW::string( "as is" ), sec->unsanitise( "as is" ) );

	// The base implementations sanitise nothing and accept everything.
	CHECK( !sec->sanitiseSectionName() );
	CHECK( sec->isValidSectionName() );
}


BW_END_NAMESPACE

// test_datasection.cpp
