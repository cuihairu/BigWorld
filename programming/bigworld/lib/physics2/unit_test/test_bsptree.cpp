#include "pch.hpp"

#include "cstdmf/guard.hpp"
#include "physics2/bsp.hpp"
#include "resmgr/binary_block.hpp"

// This file used to be wrapped in `#ifndef MF_SERVER` ("not compiled on
// server, due to unusual linkage issues to be fixed later"), which made it
// compile to nothing in the server-side unit-test binary - bsp.cpp sat at 0%
// coverage despite having tests. bsp.cpp's dependency footprint is now just
// physics2/cstdmf/math/resmgr, all already in this test's dependsOn, so the
// guard is gone and the whole suite below runs on server builds
// (coverage batch 19).

BW_BEGIN_NAMESPACE

// -----------------------------------------------------------------------------
// Section: helpers
// -----------------------------------------------------------------------------

namespace
{

// Floating point comparisons go through explicit tolerances: unit_test_lib
// has no CHECK_CLOSE, and the BSP builder picks splitting planes with rand(),
// so no assertion below may depend on the exact tree shape.
bool closeTo( float lhs, float rhs, float epsilon = 1e-4f )
{
	return fabsf( lhs - rhs ) < epsilon;
}

// A small horizontal triangle at height y, tiled so consecutive tiles cover
// the strip x in [-22, 2]. Tile 11 spans x in [0, 2] and contains the point
// (1, y, 1), which the ray tests below are aimed at.
WorldTriangle deckTriangle( uint i, float y, WorldTriangle::Flags flags = 0 )
{
	return WorldTriangle( Vector3( 2.f * i - 22.f, y, 0.f ),
		Vector3( 2.f * i - 21.f, y, 2.f ),
		Vector3( 2.f * i - 20.f, y, 0.f ),
		flags );
}

// A vertical wall spanning both decks; its plane (x = 0) is one of the
// splitting-plane candidates during construction.
WorldTriangle wallTriangle()
{
	return WorldTriangle( Vector3( 0.f, -1.f, 0.f ),
		Vector3( 0.f, 5.f, 0.f ),
		Vector3( 0.f, 5.f, 3.f ) );
}

// Ray from (1,-2,1) to (1,6,1): crosses the y=0 deck at t = 0.25 and the
// y=4 deck at t = 0.75 (both exact binary fractions).
const Vector3 RAY_START( 1.f, -2.f, 1.f );
const Vector3 RAY_END( 1.f, 6.f, 1.f );

// CollisionVisitor that counts every visit and accepts all of them.
class CountingVisitor : public CollisionVisitor
{
public:
	CountingVisitor() : numVisits_( 0 ) {}

	virtual bool visit( const WorldTriangle &, float )
	{
		++numVisits_;
		return true;
	}

	int numVisits_;
};

// CollisionVisitor that rejects hits closer along the ray than a threshold,
// accepting the rest - drives the "visitor vetoed this hit" arms.
class VetoVisitor : public CollisionVisitor
{
public:
	VetoVisitor( float vetoBelow ) :
		vetoBelow_( vetoBelow ),
		numVetoed_( 0 ),
		numAccepted_( 0 )
	{}

	virtual bool visit( const WorldTriangle &, float dist )
	{
		if (dist < vetoBelow_)
		{
			++numVetoed_;
			return false;
		}
		++numAccepted_;
		return true;
	}

	float	vetoBelow_;
	int		numVetoed_;
	int		numAccepted_;
};

} // end anonymous namespace

// -----------------------------------------------------------------------------
// Section: construction and queries
// -----------------------------------------------------------------------------

TEST( BSPTree_Construction )
{
	// No triangles
	RealWTriangleSet empty;

	// One triangle
	RealWTriangleSet single;
	single.push_back( WorldTriangle( Vector3::zero(),
									 Vector3( 1.0f, 2.0f, 3.0f ),
									 Vector3( 3.0f, 2.0f, 1.0f ),
										0 ) );

	// Store number of nodes and triangles (unused; kept for comment clarity)
	[[maybe_unused]] int nodes = 0;
	[[maybe_unused]] int tris = 0;

	// Empty BSP is empty and has no root
	BSPTree* emptyBsp = BSPTreeTool::buildBSP( empty );
	CHECK_EQUAL( true ,		emptyBsp->numTriangles() == 0 );

	// Empty BSP has one node and zero triangles
	CHECK_EQUAL( 1,			emptyBsp->numNodes() );
	CHECK_EQUAL( 0,			emptyBsp->numTriangles() );

	bw_safe_delete( emptyBsp );

	// Single BSP is not empty
	BSPTree* singleBsp = BSPTreeTool::buildBSP( single );
	CHECK_EQUAL( false,		singleBsp->numTriangles() == 0 );

	// Single BSP has one node and one triangle
	CHECK_EQUAL( 1,			singleBsp->numNodes() );
	CHECK_EQUAL( 1,			singleBsp->numTriangles() );

	bw_safe_delete( singleBsp );
}

TEST( BSP_Intersection )
{
	// create a simple tree with a single triangle on the x plane
	RealWTriangleSet		single;
	single.push_back(
		WorldTriangle(
			Vector3::zero(),
			Vector3( 0.0f, 0.0f, 5.0f ),
			Vector3( 0.0f, 5.0f, 0.0f ),
			0 )
		);
	BSPTree*				simpleBSP = BSPTreeTool::buildBSP( single );
	float					interval = 1.0f;
	const WorldTriangle*	pHitTriangle;

	// test intersection against a ray
	bool hit = simpleBSP->intersects(	Vector3(-5.0f, 0.0f, 2.0f ),
										Vector3( 5.0f, 2.0f, 0.0f ),
										interval,
										&pHitTriangle );
	CHECK_EQUAL( true, hit );
	CHECK( closeTo( interval, 0.5f ) );
	bw_safe_delete( simpleBSP );
};

class TestVisitor: public CollisionVisitor
{
public:
	TestVisitor()
		: numHits_( 0 )
	{
	}

	virtual bool visit( const WorldTriangle & hitTriangle, float dist )
	{
		numHits_++;
		return numHits_ == 2; // terminate after two hits
	}

	uint32 numHits_;
};

TEST( BSP_Multiple_Intersection )
{
	// create a simple tree with two coplanar triangles on the x plane
	RealWTriangleSet		multiple;
	multiple.push_back(
		WorldTriangle(
			Vector3::zero(),
			Vector3( 0.0f, 0.0f, 5.0f ),
			Vector3( 0.0f, 5.0f, 0.0f ),
			0 )
		);
	multiple.push_back(
		WorldTriangle(
			Vector3::zero(),
			Vector3( 0.0f, 0.0f, 6.0f ),
			Vector3( 0.0f, 6.0f, 0.0f ),
			0 )
		);

	BSPTree*				simpleBSP = BSPTreeTool::buildBSP(  multiple );
	float					interval = 1.0f;
	TestVisitor				visitor;

	// test intersection against a ray - visitor should return a hit for each
	// triangle (even though they are coplanar).
	bool hit = simpleBSP->intersects(	Vector3(-5.0f, 2.0f, 2.0f ),
										Vector3( 5.0f, 2.0f, 2.0f ),
										interval,
										NULL,
										&visitor );
	CHECK_EQUAL( true,	hit );
	CHECK( closeTo( interval, 0.5f ) );
	CHECK_EQUAL( 2,		visitor.numHits_ );
	bw_safe_delete( simpleBSP );
}

TEST( BSP_RayMissAndDistanceLimit )
{
	// Two decks tall enough to straddle the test ray, few enough triangles to
	// stay a single node.
	RealWTriangleSet tris;
	tris.push_back( deckTriangle( 11, 0.f ) );
	tris.push_back( deckTriangle( 11, 4.f ) );
	BSPTree* pTree = BSPTreeTool::buildBSP( tris );

	const WorldTriangle* pHit = (const WorldTriangle *)0x1;
	float dist = 1.f;

	// A ray that passes beside every triangle leaves all outputs untouched.
	CHECK( !pTree->intersects( Vector3( 5.f, -2.f, 1.f ),
		Vector3( 5.f, 6.f, 1.f ), dist, &pHit ) );
	CHECK( closeTo( dist, 1.f ) );
	CHECK( pHit == (const WorldTriangle *)0x1 );

	// dist limits the considered interval: the y=0 deck sits at 0.25, beyond
	// a 0.2 budget, so the sweep misses and dist comes back as passed in.
	dist = 0.2f;
	CHECK( !pTree->intersects( RAY_START, RAY_END, dist, &pHit ) );
	CHECK( closeTo( dist, 0.2f ) );

	// With a 0.5 budget the deck at 0.25 is found and reported.
	dist = 0.5f;
	CHECK( pTree->intersects( RAY_START, RAY_END, dist, &pHit ) );
	CHECK( closeTo( dist, 0.25f ) );
	CHECK( pHit != NULL );
	CHECK( closeTo( pHit->v0().y, 0.f ) );

	bw_safe_delete( pTree );
}

TEST( BSP_VisitorVetoFindsLaterHit )
{
	RealWTriangleSet tris;
	tris.push_back( deckTriangle( 11, 0.f ) );
	tris.push_back( deckTriangle( 11, 4.f ) );
	BSPTree* pTree = BSPTreeTool::buildBSP( tris );

	// Reject the y=0 deck hit at 0.25; the walk must continue to the y=4
	// deck at 0.75 instead of reporting the vetoed hit.
	VetoVisitor visitor( 0.5f );
	float dist = 1.f;
	CHECK( pTree->intersects( RAY_START, RAY_END, dist, NULL, &visitor ) );
	CHECK( closeTo( dist, 0.75f ) );
	CHECK_EQUAL( 1, visitor.numVetoed_ );
	CHECK_EQUAL( 1, visitor.numAccepted_ );

	// A visitor that rejects everything turns the sweep into a miss.
	VetoVisitor rejectAll( 1e9f );
	dist = 1.f;
	CHECK( !pTree->intersects( RAY_START, RAY_END, dist, NULL, &rejectAll ) );
	CHECK_EQUAL( 2, rejectAll.numVetoed_ );

	bw_safe_delete( pTree );
}

TEST( BSP_TriangleOverlapQuery )
{
	// One vertical triangle on the x=1 plane (y,z in the lower triangle).
	RealWTriangleSet tris;
	tris.push_back( WorldTriangle( Vector3( 1.f, 0.f, 0.f ),
		Vector3( 1.f, 2.f, 0.f ),
		Vector3( 1.f, 0.f, 2.f ) ) );
	BSPTree* pTree = BSPTreeTool::buildBSP( tris );

	// A query triangle piercing straight through it.
	WorldTriangle query( Vector3( 0.f, 1.f, 0.5f ),
		Vector3( 2.f, 1.f, 0.5f ),
		Vector3( 1.f, 1.f, 1.5f ) );
	const WorldTriangle* pHit = NULL;
	CHECK( pTree->intersects( query, &pHit ) );
	// The node-level path reports the query triangle itself - the tree keeps
	// copies of its triangles, so only the input address can come back here.
	CHECK( pHit == &query );

	// A disjoint query misses and leaves the out pointer untouched.
	WorldTriangle farAway( Vector3( 0.f, 10.f, 0.f ),
		Vector3( 2.f, 10.f, 0.f ),
		Vector3( 1.f, 10.f, 2.f ) );
	const WorldTriangle* pMiss = (const WorldTriangle *)0x1;
	CHECK( !pTree->intersects( farAway, &pMiss ) );
	CHECK( pMiss == (const WorldTriangle *)0x1 );

	bw_safe_delete( pTree );
}

TEST( BSP_SweptTriangleTranslation )
{
	// One vertical tree triangle: x = 1, covering y,z >= 0 with y + z <= 3.
	RealWTriangleSet tris;
	tris.push_back( WorldTriangle( Vector3( 1.f, 0.f, 0.f ),
		Vector3( 1.f, 3.f, 0.f ),
		Vector3( 1.f, 0.f, 3.f ) ) );
	BSPTree* pTree = BSPTreeTool::buildBSP( tris );

	// The swept volume is the mover extruded along the offset, so the offset
	// must leave the mover's plane (a zero or in-plane offset collapses the
	// prism to a flat sheet and misses everything).
	//
	// Brute-force path (offset parallel to the tree triangle's plane):
	// a horizontal mover just above the tree, sweeping down 1 unit, its
	// prism overlapping the tree triangle's upper part.
	WorldTriangle grazer( Vector3( 0.5f, 2.f, 0.5f ),
		Vector3( 1.5f, 2.f, 0.5f ),
		Vector3( 1.f, 2.f, 1.5f ) );
	CountingVisitor grazeVisitor;
	CHECK( pTree->intersects( grazer, Vector3( 0.f, -1.f, 0.f ),
		&grazeVisitor ) );
	CHECK_EQUAL( 1, grazeVisitor.numVisits_ );

	// Same path shape but from far away: sweep 20 units down through the
	// tree triangle and well past it.
	WorldTriangle flier( Vector3( 0.5f, 10.f, 0.5f ),
		Vector3( 1.5f, 10.f, 0.5f ),
		Vector3( 1.f, 10.f, 1.5f ) );
	CountingVisitor passThrough;
	CHECK( pTree->intersects( flier, Vector3( 0.f, -20.f, 0.f ),
		&passThrough ) );
	CHECK( passThrough.numVisits_ > 0 );

	// Sweeping upward from above the tree triangle misses.
	CountingVisitor missVisitor;
	CHECK( !pTree->intersects( flier, Vector3( 0.f, 5.f, 0.f ),
		&missVisitor ) );
	CHECK_EQUAL( 0, missVisitor.numVisits_ );

	// Fast path (offset along the tree triangle's normal): a mover sitting
	// in an x = 5 plane whose yz footprint sits inside the tree triangle's,
	// swept 8 units across it.
	WorldTriangle plug( Vector3( 5.f, 0.5f, 0.5f ),
		Vector3( 5.f, 2.5f, 0.5f ),
		Vector3( 5.f, 0.5f, 2.5f ) );
	CountingVisitor plugVisitor;
	CHECK( pTree->intersects( plug, Vector3( -8.f, 0.f, 0.f ),
		&plugVisitor ) );
	CHECK_EQUAL( 1, plugVisitor.numVisits_ );

	// The same plug swept away from the tree triangle is rejected by the
	// fast path without ever reaching the brute-force check.
	CountingVisitor awayVisitor;
	CHECK( !pTree->intersects( plug, Vector3( 10.f, 0.f, 0.f ),
		&awayVisitor ) );
	CHECK_EQUAL( 0, awayVisitor.numVisits_ );

	bw_safe_delete( pTree );
}

TEST( BSP_PartitionedTreeQueries )
{
	// 25 triangles: MAX_TRIANGLES_PER_NODE is 10, so the root must partition
	// (twice, since the wall is a splitting-plane candidate). The deck tiling
	// guarantees the test ray hits the y=0 deck at 0.25 and the y=4 deck at
	// 0.75 regardless of which planes rand() picks.
	RealWTriangleSet tris;
	for (uint i = 0; i < 12; ++i)
	{
		tris.push_back( deckTriangle( i, 0.f ) );
		tris.push_back( deckTriangle( i, 4.f ) );
	}
	tris.push_back( wallTriangle() );
	BSPTree* pTree = BSPTreeTool::buildBSP( tris );

	CHECK( pTree->numNodes() > 1 );
	CHECK_EQUAL( 25, (int)pTree->numTriangles() );
	CHECK( pTree->canCollide() );
	CHECK( !pTree->empty() );

	// Nearest hit is the y=0 deck no matter how the tree is shaped.
	const WorldTriangle* pHit = NULL;
	float dist = 1.f;
	CHECK( pTree->intersects( RAY_START, RAY_END, dist, &pHit ) );
	CHECK( closeTo( dist, 0.25f ) );
	CHECK( closeTo( pHit->v0().y, 0.f ) );

	// A ray starting between the decks finds only the y=4 deck, at 0.5.
	dist = 1.f;
	CHECK( pTree->intersects( Vector3( 1.f, 2.f, 1.f ),
		Vector3( 1.f, 6.f, 1.f ), dist, NULL ) );
	CHECK( closeTo( dist, 0.5f ) );

	// A ray crossing the wall in x hits it at 0.5.
	dist = 1.f;
	CHECK( pTree->intersects( Vector3( -2.f, 2.f, 1.f ),
		Vector3( 2.f, 2.f, 1.f ), dist, NULL ) );
	CHECK( closeTo( dist, 0.5f ) );

	// The bounding box covers every input vertex.
	const BoundingBox & bbox = pTree->boundingBox();
	CHECK( closeTo( bbox.minBounds().y, -1.f ) );
	CHECK( closeTo( bbox.maxBounds().y, 5.f ) );

	bw_safe_delete( pTree );
}

// -----------------------------------------------------------------------------
// Section: serialisation
// -----------------------------------------------------------------------------

TEST( BSP_SaveLoadRoundTrip )
{
	RealWTriangleSet tris;
	for (uint i = 0; i < 12; ++i)
	{
		tris.push_back( deckTriangle( i, 0.f ) );
		tris.push_back( deckTriangle( i, 4.f ) );
	}
	tris.push_back( wallTriangle() );
	BSPTree* pTree = BSPTreeTool::buildBSP( tris );
	CHECK( pTree->numNodes() > 1 );

	BinaryPtr userData = new BinaryBlock( "roundtrip", 9,
		"BSPUnitTest/UserData" );
	pTree->setUserData( BSPTree::MD5_DIGEST, userData );

	BinaryPtr saved = BSPTreeTool::saveBSPInMemory( pTree );
	CHECK( saved.get() != NULL );
	CHECK_EQUAL( BSPTreeTool::getCurrentBSPVersion(),
		BSPTreeTool::getBSPVersion( saved ) );

	BSPTree* pLoaded = BSPTreeTool::loadBSP( saved );
	CHECK( pLoaded != NULL );
	CHECK_EQUAL( pTree->numTriangles(), pLoaded->numTriangles() );
	CHECK_EQUAL( pTree->numNodes(), pLoaded->numNodes() );

	bool allTrianglesEqual = true;
	for (uint i = 0; i < pTree->numTriangles(); ++i)
	{
		allTrianglesEqual &=
			(pTree->triangles()[i] == pLoaded->triangles()[i]);
	}
	CHECK( allTrianglesEqual );

	BinaryPtr loadedUserData = pLoaded->getUserData( BSPTree::MD5_DIGEST );
	CHECK( loadedUserData.get() != NULL );
	CHECK_EQUAL( 9, loadedUserData->len() );
	CHECK( memcmp( loadedUserData->cdata(), "roundtrip", 9 ) == 0 );
	CHECK( pLoaded->getUserData( BSPTree::TIME_STAMP ).get() == NULL );

	// The loaded tree answers queries identically.
	float distTree = 1.f;
	float distLoaded = 1.f;
	CHECK( pTree->intersects( RAY_START, RAY_END, distTree, NULL ) );
	CHECK( pLoaded->intersects( RAY_START, RAY_END, distLoaded, NULL ) );
	CHECK( closeTo( distTree, 0.25f ) );
	CHECK( closeTo( distLoaded, 0.25f ) );

	bw_safe_delete( pLoaded );
	bw_safe_delete( pTree );
}

TEST( BSP_LoadRejectsBadMagic )
{
	char garbage[16];
	for (size_t i = 0; i < sizeof( garbage ); ++i)
	{
		garbage[i] = (char)0xAB;
	}
	BinaryPtr bp = new BinaryBlock( garbage, sizeof( garbage ),
		"BSPUnitTest/Garbage" );

	// 0xFF is the file-local BSP_FILE_INVALID_VERSION sentinel.
	CHECK_EQUAL( 0xFF, BSPTreeTool::getBSPVersion( bp ) );
	CHECK( BSPTreeTool::loadBSP( bp ) == NULL );
}

TEST( BSP_LegacyFormatThreeNodeTree )
{
	// Hand-assemble a version-0 (legacy) BSP blob: 3 triangles in 3 nodes -
	// a root on the x=0 plane holding the on-plane triangle, a front child at
	// x=1 and a back child at x=-1 - plus one user-data record. This drives
	// loadBSPLegacy including its skip-the-front-subtree walk used to locate
	// the back child index.
	BW::vector<char> blob;

	// Header: magic (version 0), triangle/node counts, max tris per node.
	const uint32 LEGACY_TOKEN = 0x505342; // "BSP" + version 0 in the top byte
	const uint32 counts[3] = { 3, 3, 10 };
	blob.insert( blob.end(), (const char *)&LEGACY_TOKEN,
		(const char *)&LEGACY_TOKEN + sizeof( uint32 ) );
	for (uint i = 0; i < 3; ++i)
	{
		blob.insert( blob.end(), (const char *)&counts[i],
			(const char *)&counts[i] + sizeof( uint32 ) );
	}

	// Triangles: v0/v1/v2 then flags+padding, 40 bytes each (verified by
	// loadBSPLegacy's own MF_ASSERT_DEV).
	struct TriRecord
	{
		Vector3 v0, v1, v2;
		uint16 flags;
		uint16 padding;
	};
	TriRecord triRecords[3] =
	{
		{ Vector3( 0.f, 0.f, 0.f ), Vector3( 0.f, 2.f, 0.f ),
			Vector3( 0.f, 0.f, 2.f ), 0, 0 },	// root: on the x=0 plane
		{ Vector3( 1.f, 0.f, 0.f ), Vector3( 1.f, 2.f, 0.f ),
			Vector3( 1.f, 0.f, 2.f ), 0, 0 },	// front child
		{ Vector3( -1.f, 0.f, 0.f ), Vector3( -1.f, 2.f, 0.f ),
			Vector3( -1.f, 0.f, 2.f ), 0, 0 }	// back child
	};
	MF_ASSERT( sizeof( TriRecord ) == 40 );
	blob.insert( blob.end(), (const char *)triRecords,
		(const char *)triRecords + sizeof( triRecords ) );

	// Nodes in prefix order: root (front|back = 0x06), then front child,
	// then back child. Record layout: uint8 flags, PlaneEq (normal + d),
	// uint16 ref count, then uint16 triangle indices.
	struct NodeRecord
	{
		uint8 flags;
		float nx, ny, nz, d;
		uint16 numRefs;
		uint16 ref;
	};
	NodeRecord nodeRecords[3] =
	{
		{ 0x06, 1.f, 0.f, 0.f, 0.f, 1, 0 },
		{ 0x00, 0.f, 1.f, 0.f, 1.f, 1, 1 },
		{ 0x00, 0.f, 1.f, 0.f, 1.f, 1, 2 }
	};
	MF_ASSERT( sizeof( NodeRecord ) == 24 );
	for (uint i = 0; i < 3; ++i)
	{
		// The flags byte is followed by the plane but NOT by padding: write
		// the record field by field, not as a struct blob.
		blob.push_back( nodeRecords[i].flags );
		const float plane[4] = { nodeRecords[i].nx, nodeRecords[i].ny,
			nodeRecords[i].nz, nodeRecords[i].d };
		blob.insert( blob.end(), (const char *)plane,
			(const char *)plane + sizeof( plane ) );
		blob.insert( blob.end(), (const char *)&nodeRecords[i].numRefs,
			(const char *)&nodeRecords[i].numRefs + sizeof( uint16 ) );
		blob.insert( blob.end(), (const char *)&nodeRecords[i].ref,
			(const char *)&nodeRecords[i].ref + sizeof( uint16 ) );
	}

	// One user-data record: key 0 (MD5_DIGEST), 4 bytes of payload.
	const uint32 udKey = 0;
	const int32 udLen = 4;
	const char udPayload[4] = { 'B', 'S', 'P', '!' };
	blob.insert( blob.end(), (const char *)&udKey,
		(const char *)&udKey + sizeof( uint32 ) );
	blob.insert( blob.end(), (const char *)&udLen,
		(const char *)&udLen + sizeof( int32 ) );
	blob.insert( blob.end(), udPayload, udPayload + udLen );

	BinaryPtr bp = new BinaryBlock( &blob[0], blob.size(),
		"BSPUnitTest/Legacy" );
	CHECK_EQUAL( 0, BSPTreeTool::getBSPVersion( bp ) );

	BSPTree* pTree = BSPTreeTool::loadBSP( bp );
	CHECK( pTree != NULL );
	if (pTree == NULL)
	{
		return;
	}
	CHECK_EQUAL( 3, (int)pTree->numTriangles() );
	CHECK_EQUAL( 3, (int)pTree->numNodes() );

	// The user data blob made it through loadUserData.
	BinaryPtr loadedUserData = pTree->getUserData( BSPTree::MD5_DIGEST );
	CHECK( loadedUserData.get() != NULL );
	CHECK_EQUAL( 4, loadedUserData->len() );
	CHECK( memcmp( loadedUserData->cdata(), udPayload, udLen ) == 0 );

	// A ray from the back half hits the x=-1 triangle at 0.25; from the
	// front half, the x=1 triangle at 0.25.
	float dist = 1.f;
	CHECK( pTree->intersects( Vector3( -2.f, 0.5f, 0.5f ),
		Vector3( 2.f, 0.5f, 0.5f ), dist, NULL ) );
	CHECK( closeTo( dist, 0.25f ) );
	dist = 1.f;
	CHECK( pTree->intersects( Vector3( 2.f, 0.5f, 0.5f ),
		Vector3( -2.f, 0.5f, 0.5f ), dist, NULL ) );
	CHECK( closeTo( dist, 0.25f ) );

	bw_safe_delete( pTree );
}

TEST( BSP_EmptyTreeSaveLoad )
{
	RealWTriangleSet none;
	BSPTree* pTree = BSPTreeTool::buildBSP( none );
	CHECK_EQUAL( 1, pTree->numNodes() );
	CHECK( pTree->empty() );
	CHECK( !pTree->canCollide() );

	// The zero-triangle branches of save and load round-trip cleanly.
	BinaryPtr saved = BSPTreeTool::saveBSPInMemory( pTree );
	BSPTree* pLoaded = BSPTreeTool::loadBSP( saved );
	CHECK( pLoaded != NULL );
	if (pLoaded == NULL)
	{
		bw_safe_delete( pTree );
		return;
	}
	CHECK_EQUAL( 0, (int)pLoaded->numTriangles() );
	CHECK( pLoaded->empty() );

	float dist = 1.f;
	CHECK( !pLoaded->intersects( Vector3( -1.f, 0.f, 0.f ),
		Vector3( 2.f, 0.f, 0.f ), dist, NULL ) );

	bw_safe_delete( pLoaded );
	bw_safe_delete( pTree );
}

TEST( BSP_RemapsFlagsThroughModulo )
{
	RealWTriangleSet tris;
	tris.push_back( WorldTriangle( Vector3( 0.f, 0.f, 0.f ),
		Vector3( 1.f, 0.f, 0.f ),
		Vector3( 0.f, 1.f, 0.f ), 0 ) );
	tris.push_back( WorldTriangle( Vector3( 0.f, 0.f, 5.f ),
		Vector3( 1.f, 0.f, 5.f ),
		Vector3( 0.f, 1.f, 5.f ), 5 ) );
	BSPTree* pTree = BSPTreeTool::buildBSP( tris );

	// Material group IDs map through modulo: 0 % 3 = 0, 5 % 3 = 2.
	BSPFlagsMap flagsMap;
	flagsMap.push_back( 10 );
	flagsMap.push_back( 20 );
	flagsMap.push_back( 30 );
	pTree->remapFlags( flagsMap );

	CHECK_EQUAL( WorldTriangle::Flags( 10 ),
		pTree->triangles()[0].flags() );
	CHECK_EQUAL( WorldTriangle::Flags( 30 ),
		pTree->triangles()[1].flags() );

	bw_safe_delete( pTree );
}

TEST( BSP_NotInBspTrianglesExcluded )
{
	// One collideable triangle at x=1 and one marked TRIANGLE_NOT_IN_BSP at
	// x=3; both fit in a single node.
	RealWTriangleSet tris;
	tris.push_back( WorldTriangle( Vector3( 1.f, 0.f, 0.f ),
		Vector3( 1.f, 2.f, 0.f ),
		Vector3( 1.f, 0.f, 2.f ) ) );
	tris.push_back( WorldTriangle( Vector3( 3.f, 0.f, 0.f ),
		Vector3( 3.f, 2.f, 0.f ),
		Vector3( 3.f, 0.f, 2.f ), TRIANGLE_NOT_IN_BSP ) );
	BSPTree* pTree = BSPTreeTool::buildBSP( tris );
	CHECK( pTree->canCollide() );

	// A query that geometrically pierces only the excluded triangle misses:
	// the intersect loops skip TRIANGLE_NOT_IN_BSP candidates.
	WorldTriangle excludedOnly( Vector3( 2.5f, 1.f, 0.5f ),
		Vector3( 3.5f, 1.f, 0.5f ),
		Vector3( 3.f, 1.f, 1.5f ) );
	CHECK( !pTree->intersects( excludedOnly ) );

	// The collideable triangle is still found.
	WorldTriangle piercesFirst( Vector3( 0.f, 1.f, 0.5f ),
		Vector3( 2.f, 1.f, 0.5f ),
		Vector3( 1.f, 1.f, 1.5f ) );
	const WorldTriangle* pHit = NULL;
	CHECK( pTree->intersects( piercesFirst, &pHit ) );

	bw_safe_delete( pTree );

	// A tree made only of excluded triangles has nothing to collide with.
	RealWTriangleSet onlyExcluded;
	onlyExcluded.push_back( WorldTriangle( Vector3( 3.f, 0.f, 0.f ),
		Vector3( 3.f, 2.f, 0.f ),
		Vector3( 3.f, 0.f, 2.f ), TRIANGLE_NOT_IN_BSP ) );
	BSPTree* pOnly = BSPTreeTool::buildBSP( onlyExcluded );
	CHECK( !pOnly->canCollide() );
	bw_safe_delete( pOnly );
}

BW_END_NAMESPACE

// test_bsptree.cpp
