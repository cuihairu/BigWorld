#include "pch.hpp"

#include "physics2/quad_tree.hpp"
#include "math/boundbox.hpp"

BW_BEGIN_NAMESPACE

// This build treats -Wfloat-equal as an error, so exact-value float
// comparisons go through an explicit tolerance instead.
namespace
{

bool closeTo( float lhs, float rhs, float epsilon = 1e-4f )
{
	return fabsf( lhs - rhs ) < epsilon;
}

} // end anonymous namespace


struct TestMemPoolItem
{
	int anInt_;
	float aFloat_;
	bool aBool_;
};


TEST( QuadTreeMemPool_usage )
{
	//Create some memory pools.
	QuadTreeMemPool<TestMemPoolItem> memPool;
	QuadTreeMemPool<TestMemPoolItem> memPool1;
	QuadTreeMemPool<TestMemPoolItem> memPool2;

	memPool.init();
	memPool1.init();
	memPool2.init();

	//Make sure they are in initial state
	CHECK_EQUAL( 0, memPool.size() );
	CHECK_EQUAL( 0, memPool.capacity() );
	CHECK_EQUAL( reinterpret_cast<TestMemPoolItem*>(0), memPool.data_ );

	CHECK_EQUAL( 0, memPool1.size() );
	CHECK_EQUAL( 0, memPool1.capacity() );
	CHECK_EQUAL( reinterpret_cast<TestMemPoolItem*>(0), memPool1.data_ );

	CHECK_EQUAL( 0, memPool2.size() );
	CHECK_EQUAL( 0, memPool2.capacity() );
	CHECK_EQUAL( reinterpret_cast<TestMemPoolItem*>(0), memPool2.data_ );

	//Fill out the memory pool items with some values we can check later.
	const int maxItems = 16777;
	for( int i = 0; i < maxItems; ++i )
	{
		TestMemPoolItem& newItem = memPool.createNew();
		newItem.anInt_ = i;
		newItem.aFloat_ = static_cast<float>(i);
		newItem.aBool_ = i % 2 == 0;

		TestMemPoolItem& newItem1 = memPool1.createNew();
		newItem1.anInt_ = i;
		newItem1.aFloat_ = static_cast<float>(i);
		newItem1.aBool_ = i % 2 == 0;

		TestMemPoolItem& newItem2 = memPool2.createNew();
		newItem2.anInt_ = i;
		newItem2.aFloat_ = static_cast<float>(i);
		newItem2.aBool_ = i % 2 == 0;
	}

	//Make sure size and capacity are what we expect
	CHECK_EQUAL( maxItems, memPool.size() );
	CHECK_EQUAL( maxItems, memPool1.size() );
	CHECK_EQUAL( maxItems, memPool2.size() );

	CHECK( memPool.capacity() >= memPool.size() );
	CHECK( memPool1.capacity() >= memPool1.size() );
	CHECK( memPool2.capacity() >= memPool2.size() );

	bool sameCapacity = memPool.capacity() == memPool1.capacity();
	sameCapacity &= memPool.capacity() == memPool2.capacity();
	CHECK( sameCapacity );

	//Check the values are the same as those we added. The stored float is
	//a bit-identical copy of static_cast<float>(i), and i fits well below
	//the 2^24 exact-range of a float, so comparing as ints is equivalent
	//without tripping -Werror=float-equal.
	for( int i = 0; i < maxItems; ++i )
	{
		const TestMemPoolItem& item = memPool[i];
		CHECK_EQUAL( i, item.anInt_ );
		CHECK_EQUAL( i, static_cast<int>( item.aFloat_ ) );
		CHECK_EQUAL( i % 2 == 0, item.aBool_ );

		const TestMemPoolItem& item1 = memPool1[i];
		CHECK_EQUAL( i, item1.anInt_ );
		CHECK_EQUAL( i, static_cast<int>( item1.aFloat_ ) );
		CHECK_EQUAL( i % 2 == 0, item1.aBool_ );

		const TestMemPoolItem& item2 = memPool2[i];
		CHECK_EQUAL( i, item2.anInt_ );
		CHECK_EQUAL( i, static_cast<int>( item2.aFloat_ ) );
		CHECK_EQUAL( i % 2 == 0, item2.aBool_ );
	}

	//Destroy everything
	memPool.destroy();
	memPool1.destroy();
	memPool2.destroy();

	CHECK_EQUAL( reinterpret_cast<TestMemPoolItem*>(0), memPool.data_ );
	CHECK_EQUAL( reinterpret_cast<TestMemPoolItem*>(0), memPool1.data_ );
	CHECK_EQUAL( reinterpret_cast<TestMemPoolItem*>(0), memPool2.data_ );
}


struct TestQuadTreeItem
{
	int anInt_;
	float aFloat_;
	bool aBool_;

	BoundingBox bb_;
	Matrix transform_;
};


//This function is used when objects are added to a quad tree, it
//determines the position and extent of the object
QTRange calculateQTRange( const TestQuadTreeItem & input, const Vector2 & origin,
						 float range, int depth )
{
	const float WIDTH = float(1 << depth);

	BoundingBox bb = input.bb_;
	bb.transformBy( input.transform_ );

	QTRange rv;

	rv.left_	= int(WIDTH * (bb.minBounds().x - origin.x)/range);
	rv.right_	= int(WIDTH * (bb.maxBounds().x - origin.x)/range);
	rv.bottom_	= int(WIDTH * (bb.minBounds().z - origin.y)/range);
	rv.top_		= int(WIDTH * (bb.maxBounds().z - origin.y)/range);

	return rv;
}


TEST( QuadTree_usage )
{
	//Create a quad tree
	const float treeRange = 1000.f;
	const int treeMaxDepth = 5;
	const float treeOffsetX = 0.f;
	const float treeOffsetY = 0.f;
	QuadTree<TestQuadTreeItem> quadTree(
		treeOffsetX, treeOffsetY, treeMaxDepth, treeRange);

	//Quad tree should always have at least the root node.
	CHECK_EQUAL( 1, quadTree.countNodes() );

	//Calculate max number of nodes
	int maxNodes = 0;
	for (int i = 0, levelNodes = 1; i <= treeMaxDepth; ++i )
	{
		maxNodes += levelNodes;
		levelNodes *= 4;
	}

	//
	//Create some test objects. Also try and fill the tree
	//in terms of creating all the possible child nodes.
	//
	const int maxItemsX = 70;
	const int maxItemsZ = 70;
	const int maxItems = maxItemsX*maxItemsZ;
	TestQuadTreeItem* items = new TestQuadTreeItem[maxItems];

	for( int z = 0; z < maxItemsZ; ++z )
	{
		for( int x = 0; x < maxItemsX; ++x )
		{
			int i = z*maxItemsX + x;
			TestQuadTreeItem& newItem = items[i];
			newItem.anInt_ = i;
			newItem.aFloat_ = static_cast<float>(i);
			newItem.aBool_ = i % 2 == 0;

			float xPos = x*(treeRange / (float)maxItemsX);
			float zPos = z*(treeRange / (float)maxItemsZ);

			items[i].transform_.setIdentity();
			items[i].transform_.setTranslate( xPos, 0.f, zPos );
			items[i].bb_.setBounds( Vector3(0.f,0.f,0.f), Vector3(1.f,1.f,1.f) );
		}
	}

	//
	//Adding and deleting at the root. This also tests code that deals
	//with the element arrays.
	//
	quadTree.addToRoot( items[0] );
	size_t numElements = quadTree.getNumElements( quadTree.root() );
	CHECK_EQUAL( 1, numElements );
	bool removed = quadTree.removeFromRoot( items[0] );
	CHECK_EQUAL( true, removed );
	numElements = quadTree.getNumElements( quadTree.root() );
	CHECK_EQUAL( 0, numElements );

	//Add some items
	quadTree.addToRoot( items[0] );
	quadTree.addToRoot( items[1] );
	quadTree.addToRoot( items[2] );

	//Get all the unique ones, which should be what we just added.
	BW::set<const TestQuadTreeItem*> uniqueElements;
	quadTree.getUniqueElements( uniqueElements );
	CHECK_EQUAL( 3, uniqueElements.size() );

	//Check the function getting the number of elements from the root.
	numElements = quadTree.getNumElements( quadTree.root() );
	CHECK_EQUAL( 3, numElements );
	CHECK_EQUAL( &items[0], quadTree.getElement( quadTree.root(), 0 ));
	CHECK_EQUAL( &items[1], quadTree.getElement( quadTree.root(), 1 ));
	CHECK_EQUAL( &items[2], quadTree.getElement( quadTree.root(), 2 ));

	//Delete out of order. The way delete works in the quad tree,
	//the last element in the array is copied over the top of the
	//deleted element.
	quadTree.removeFromRoot( items[1] );
	numElements = quadTree.getNumElements( quadTree.root() );
	CHECK_EQUAL( 2, numElements );
	CHECK_EQUAL( &items[0], quadTree.getElement( quadTree.root(), 0 ));
	CHECK_EQUAL( &items[2], quadTree.getElement( quadTree.root(), 1 ));

	quadTree.removeFromRoot( items[0] );
	numElements = quadTree.getNumElements( quadTree.root() );
	CHECK_EQUAL( 1, numElements );
	CHECK_EQUAL( &items[2], quadTree.getElement( quadTree.root(), 0 ));

	quadTree.removeFromRoot( items[2] );
	numElements = quadTree.getNumElements( quadTree.root() );
	CHECK_EQUAL( 0, numElements );
	CHECK_EQUAL( 1, quadTree.countNodes() );

	//The quad tree does not support deleting of elements from
	//within the rest of the tree. Only dynamic objects are being
	//deleted from the tree in the game but they are only at the
	//root node, which is what the previous tests were doing.

	//
	//Create more than one tree and add objects to them
	//
	QuadTree<TestQuadTreeItem> quadTree1(
		treeOffsetX, treeOffsetY, treeMaxDepth, treeRange);

	QuadTree<TestQuadTreeItem> quadTree2(
		treeOffsetX, treeOffsetY, treeMaxDepth, treeRange);

	//At this point, the first tree is empty again.

	//Adding all the test objects. Create the three trees at the
	//same time. This allows for a different memory allocation
	//pattern as the quad trees grow.
	for( int i = 0; i < maxItems; ++i )
	{
		quadTree.add( items[i] );
		quadTree1.add( items[i] );
		quadTree2.add( items[i] );
	}

	//Check quad tree has all its nodes created.
	//The test objects added should cause all the nodes
	//to get created.
	int numNodes = quadTree.countNodes();
	CHECK( numNodes == maxNodes );
	numNodes = quadTree1.countNodes();
	CHECK( numNodes == maxNodes );
	numNodes = quadTree2.countNodes();
	CHECK( numNodes == maxNodes );

	//Elements might be associated with more than one node.
	//Possible because an element may overlap multiple nodes.
	numElements = quadTree.countElements();
	CHECK( maxItems <= numElements );
	numElements = quadTree1.countElements();
	CHECK( maxItems <= numElements );
	numElements = quadTree2.countElements();
	CHECK( maxItems <= numElements );

	//check each has correct number of elements
	//and that all the items exist in the sets.
	uniqueElements.clear();
	quadTree.getUniqueElements( uniqueElements );
	CHECK_EQUAL( maxItems, uniqueElements.size() );

	for( int i = 0; i < maxItems; ++i )
	{
		BW::set<const TestQuadTreeItem*>::const_iterator found =
			uniqueElements.find( &items[i] );
		CHECK( found != uniqueElements.end() );
	}

	uniqueElements.clear();
	quadTree1.getUniqueElements( uniqueElements );
	CHECK_EQUAL( maxItems, uniqueElements.size() );

	for( int i = 0; i < maxItems; ++i )
	{
		BW::set<const TestQuadTreeItem*>::const_iterator found =
			uniqueElements.find( &items[i] );
		CHECK( found != uniqueElements.end() );
	}

	uniqueElements.clear();
	quadTree2.getUniqueElements( uniqueElements );
	CHECK_EQUAL( maxItems, uniqueElements.size() );

	for( int i = 0; i < maxItems; ++i )
	{
		BW::set<const TestQuadTreeItem*>::const_iterator found =
			uniqueElements.find( &items[i] );
		CHECK( found != uniqueElements.end() );
	}

	delete[] items;
}


// -----------------------------------------------------------------------------
// Section: QuadTree query surface (coverage batch 20)
// -----------------------------------------------------------------------------

namespace
{

TestQuadTreeItem makeTreeItem( float x, float z, int id )
{
	TestQuadTreeItem item;
	item.anInt_ = id;
	item.aFloat_ = static_cast<float>( id );
	item.aBool_ = id % 2 == 0;

	item.transform_.setIdentity();
	item.transform_.setTranslate( x, 0.f, z );
	item.bb_.setBounds( Vector3( 0.f, 0.f, 0.f ), Vector3( 1.f, 1.f, 1.f ) );

	return item;
}


typedef BW::set< const TestQuadTreeItem * > TraversalSet;

// Templated on the tree's member type so the traversal helper can serve
// every QuadTree<MEMBER_TYPE> instantiated in this file, not just the one
// used by the upstream tests.
template <class MEMBER_TYPE>
BW::set< const MEMBER_TYPE * > collectTraversal(
	const QuadTree< MEMBER_TYPE > & tree,
	const Vector3 & src, const Vector3 & dst, float radius = 0.f )
{
	BW::set< const MEMBER_TYPE * > found;
	typename QuadTree< MEMBER_TYPE >::Traversal traversal =
		tree.traverse( src, dst, radius );

	while (const MEMBER_TYPE * pElement = traversal.next())
	{
		found.insert( pElement );
	}

	return found;
}

} // end anonymous namespace


namespace
{

// Generic collectTraversal for any MEMBER_TYPE
template <typename MEMBER_TYPE>
BW::set< const MEMBER_TYPE * > collectTraversalGeneric(
    const QuadTree< MEMBER_TYPE > & tree,
    const Vector3 & src, const Vector3 & dst, float radius = 0.f )
{
    BW::set< const MEMBER_TYPE * > found;
    typename QuadTree< MEMBER_TYPE >::Traversal traversal =
        tree.traverse( src, dst, radius );

    while (const MEMBER_TYPE * pElement = traversal.next())
    {
        found.insert( pElement );
    }

    return found;
}

} // end anonymous namespace


TEST( QuadTreeQTCoordHelpers )
{
	// clipMin pins the bottom-left corner to the origin.
	QTCoord coord;
	coord.x_ = -3;
	coord.y_ = 2;
	coord.clipMin();
	CHECK_EQUAL( 0, coord.x_ );
	CHECK_EQUAL( 2, coord.y_ );

	// clipMax pins the top-right corner inside a node of the given depth.
	QTCoord wide;
	wide.x_ = 40;
	wide.y_ = -1;
	wide.clipMax( 5 );
	CHECK_EQUAL( 31, wide.x_ );
	CHECK_EQUAL( -1, wide.y_ );

	// offset() stamps the quadrant bits of a child coord, findChild() reads
	// them back out again for all four quadrants.
	QTCoord tr;
	tr.x_ = 0;
	tr.y_ = 0;
	tr.offset( QUAD_TR, 2 );
	CHECK_EQUAL( 4, tr.x_ );
	CHECK_EQUAL( 4, tr.y_ );
	CHECK_EQUAL( QUAD_TR, tr.findChild( 2 ) );

	QTCoord bl;
	bl.x_ = 0;
	bl.y_ = 0;
	bl.offset( QUAD_BL, 3 );
	CHECK_EQUAL( 0, bl.x_ );
	CHECK_EQUAL( 0, bl.y_ );
	CHECK_EQUAL( QUAD_BL, bl.findChild( 3 ) );

	QTCoord tl;
	tl.x_ = 0;
	tl.y_ = 0;
	tl.offset( QUAD_TL, 1 );
	CHECK_EQUAL( 0, tl.x_ );
	CHECK_EQUAL( 2, tl.y_ );
	CHECK_EQUAL( QUAD_TL, tl.findChild( 1 ) );

	QTCoord br;
	br.x_ = 0;
	br.y_ = 0;
	br.offset( QUAD_BR, 0 );
	CHECK_EQUAL( 1, br.x_ );
	CHECK_EQUAL( 0, br.y_ );
	CHECK_EQUAL( QUAD_BR, br.findChild( 0 ) );
}


TEST( QuadTreeQTRangePredicates )
{
	// A range that exactly covers a depth-5 node.
	QTRange full;
	full.left_ = 0;
	full.bottom_ = 0;
	full.right_ = 31;
	full.top_ = 31;
	CHECK( full.fills( 5 ) );
	CHECK( full.isValid( 5 ) );
	CHECK( full.inQuad( 5 ) );
	full.clip( 5 );
	CHECK_EQUAL( 0, full.left_ );
	CHECK_EQUAL( 31, full.right_ );
	CHECK_EQUAL( 31, full.top_ );

	// Straddling the top-right corner: not a full fill, still in quad, and
	// clipping pulls the loose edges back to the node border.
	QTRange straddle;
	straddle.left_ = 30;
	straddle.bottom_ = 30;
	straddle.right_ = 40;
	straddle.top_ = 40;
	CHECK( !straddle.fills( 5 ) );
	CHECK( straddle.inQuad( 5 ) );
	straddle.clip( 5 );
	CHECK_EQUAL( 30, straddle.left_ );
	CHECK_EQUAL( 30, straddle.bottom_ );
	CHECK_EQUAL( 31, straddle.right_ );
	CHECK_EQUAL( 31, straddle.top_ );

	// Straddling the bottom-left corner: the range only becomes valid after
	// clipping, which is why add() clips before it validates.
	QTRange negative;
	negative.left_ = -4;
	negative.bottom_ = -4;
	negative.right_ = 3;
	negative.top_ = 3;
	CHECK( !negative.isValid( 5 ) );
	CHECK( negative.inQuad( 5 ) );
	negative.clip( 5 );
	CHECK_EQUAL( 0, negative.left_ );
	CHECK_EQUAL( 0, negative.bottom_ );
	CHECK_EQUAL( 3, negative.right_ );
	CHECK( negative.isValid( 5 ) );

	// Entirely past the far corner: inQuad() rejects it, so add() skips the
	// element without ever creating a node.
	QTRange beyond;
	beyond.left_ = 100;
	beyond.bottom_ = 100;
	beyond.right_ = 120;
	beyond.top_ = 120;
	CHECK( !beyond.inQuad( 5 ) );
}


TEST( QuadTree_addOutOfRangeSkips )
{
	QuadTree< TestQuadTreeItem > quadTree( 0.f, 0.f, 5, 1000.f );
	CHECK_EQUAL( 1, quadTree.countNodes() );

	// A one-unit box at (5000, 5000) is far outside the 1000-unit tree:
	// add() skips it with a message instead of creating nodes.
	TestQuadTreeItem farAway = makeTreeItem( 5000.f, 5000.f, 0 );
	quadTree.add( farAway );

	CHECK_EQUAL( 1, quadTree.countNodes() );

	TraversalSet uniqueElements;
	quadTree.getUniqueElements( uniqueElements );
	CHECK_EQUAL( 0, uniqueElements.size() );

	CHECK_EQUAL( 5, quadTree.depth() );
	CHECK( closeTo( quadTree.range(), 1000.f ) );
	CHECK( quadTree.size() > 0 );
}


TEST( QuadTree_childRefWalk )
{
	QuadTree< TestQuadTreeItem > quadTree( 0.f, 0.f, 5, 1000.f );

	// A fresh tree holds a single root node, so size() only accounts for it.
	const long rootOnlySize = quadTree.size();
	CHECK( rootOnlySize > 0 );

	// A one-unit box in the very bottom-left corner subdivides a single
	// chain of bottom-left children all the way down to depth 0.
	TestQuadTreeItem corner = makeTreeItem( 5.f, 5.f, 7 );
	quadTree.add( corner );

	// Five extra nodes and an element pool make the tree report more memory.
	CHECK( quadTree.size() > rootOnlySize );

	typedef QuadTree< TestQuadTreeItem > Tree;
	typedef QuadTreeNode< TestQuadTreeItem > Node;

	Tree::NodeRef rootRef = quadTree.root();
	CHECK_EQUAL( 0, quadTree.getNumElements( rootRef ) );

	CHECK_EQUAL( Node::INVALID_NODE, quadTree.getChildRef( rootRef, QUAD_TL ) );
	CHECK_EQUAL( Node::INVALID_NODE, quadTree.getChildRef( rootRef, QUAD_TR ) );
	CHECK_EQUAL( Node::INVALID_NODE, quadTree.getChildRef( rootRef, QUAD_BR ) );

	Tree::NodeRef nodeRef = quadTree.getChildRef( rootRef, QUAD_BL );
	CHECK( nodeRef != Node::INVALID_NODE );
	for (int depth = 4; depth > 0; --depth)
	{
		nodeRef = quadTree.getChildRef( nodeRef, QUAD_BL );
		CHECK( nodeRef != Node::INVALID_NODE );
	}

	CHECK_EQUAL( 1, quadTree.getNumElements( nodeRef ) );
	CHECK_EQUAL( &corner, quadTree.getElement( nodeRef, 0 ) );

	// The root plus one child per level: 6 nodes for a 5-deep chain.
	CHECK_EQUAL( 6, quadTree.countNodes() );
	CHECK_EQUAL( 1, quadTree.countElements() );
}


TEST( QuadTree_traverseEmptyTree )
{
	QuadTree< TestQuadTreeItem > quadTree( 0.f, 0.f, 5, 1000.f );

	TraversalSet found = collectTraversal( quadTree,
		Vector3( 10.f, 0.f, 10.f ), Vector3( 900.f, 0.f, 900.f ) );
	CHECK_EQUAL( 0, found.size() );
}


TEST( QuadTree_traverseFourDirections )
{
	QuadTree< TestQuadTreeItem > quadTree( 0.f, 0.f, 5, 1000.f );

	// One item sitting on each of the four sweep lines below.
	TestQuadTreeItem neItem = makeTreeItem( 150.f, 150.f, 0 );
	TestQuadTreeItem nwItem = makeTreeItem( 850.f, 150.f, 1 );
	TestQuadTreeItem swItem = makeTreeItem( 700.f, 700.f, 2 );
	TestQuadTreeItem seItem = makeTreeItem( 350.f, 650.f, 3 );
	quadTree.add( neItem );
	quadTree.add( nwItem );
	quadTree.add( swItem );
	quadTree.add( seItem );

	// dirType 0 - bottom-left to top-right
	TraversalSet ne = collectTraversal( quadTree,
		Vector3( 10.f, 0.f, 10.f ), Vector3( 400.f, 0.f, 400.f ), 2.f );
	CHECK_EQUAL( 1, ne.size() );
	CHECK( ne.find( &neItem ) != ne.end() );

	// dirType 1 - bottom-right to top-left
	TraversalSet nw = collectTraversal( quadTree,
		Vector3( 900.f, 0.f, 100.f ), Vector3( 500.f, 0.f, 500.f ), 2.f );
	CHECK_EQUAL( 1, nw.size() );
	CHECK( nw.find( &nwItem ) != nw.end() );

	// dirType 3 - top-right to bottom-left
	TraversalSet sw = collectTraversal( quadTree,
		Vector3( 900.f, 0.f, 900.f ), Vector3( 500.f, 0.f, 500.f ), 2.f );
	CHECK_EQUAL( 1, sw.size() );
	CHECK( sw.find( &swItem ) != sw.end() );

	// dirType 2 - top-left to bottom-right
	TraversalSet se = collectTraversal( quadTree,
		Vector3( 100.f, 0.f, 900.f ), Vector3( 500.f, 0.f, 500.f ), 2.f );
	CHECK_EQUAL( 1, se.size() );
	CHECK( se.find( &seItem ) != se.end() );
}


TEST( QuadTree_traverseDegenerateOutsideRange )
{
	QuadTree< TestQuadTreeItem > quadTree( 0.f, 0.f, 5, 1000.f );

	TestQuadTreeItem item = makeTreeItem( 500.f, 500.f, 0 );
	quadTree.add( item );

	// Lines that lie wholly outside the tree divide no axis, so the traversal
	// takes the degenerate branch, which fakes the axis crossings with
	// BIG_FLOAT sentinels. The sign of that fake crossing then decides
	// everything: off the low edge the crossing is negative, so neither the
	// "entirely left/right" nor the "entirely above/below" test ever fires and
	// the walk considers every half of the node - it over-reports. Off the
	// high edge the crossing is positive, the walk is confined to the quad on
	// the far side of the crossing, and an element on the near side is not
	// reported. Both directions matter: the degenerate path has to stay
	// conservative, because processHead() has no narrow phase and hands back
	// every element of any node it visits.
	TraversalSet leftOfTree = collectTraversal( quadTree,
		Vector3( -100.f, 0.f, 100.f ), Vector3( -100.f, 0.f, 900.f ) );
	TraversalSet rightOfTree = collectTraversal( quadTree,
		Vector3( 1500.f, 0.f, 100.f ), Vector3( 1500.f, 0.f, 900.f ) );
	TraversalSet belowTree = collectTraversal( quadTree,
		Vector3( 100.f, 0.f, -100.f ), Vector3( 900.f, 0.f, -100.f ) );
	TraversalSet aboveTree = collectTraversal( quadTree,
		Vector3( 100.f, 0.f, 1500.f ), Vector3( 900.f, 0.f, 1500.f ) );

	CHECK_EQUAL( 1, leftOfTree.size() );
	CHECK( leftOfTree.find( &item ) != leftOfTree.end() );
	CHECK_EQUAL( 1, belowTree.size() );
	CHECK( belowTree.find( &item ) != belowTree.end() );

	// The two misses only involve lines that never reach the tree, so the
	// exact tests a caller runs afterwards are not losing anything.
	CHECK_EQUAL( 0, rightOfTree.size() );
	CHECK_EQUAL( 0, aboveTree.size() );
}


TEST( QuadTree_traverseAxisParallel )
{
	QuadTree< TestQuadTreeItem > quadTree( 0.f, 0.f, 5, 1000.f );

	// dir.x == 0 exercises the x-degenerate setup in the traversal
	// constructor, dir.y == 0 the y-degenerate one.
	TestQuadTreeItem verticalItem = makeTreeItem( 500.f, 300.f, 0 );
	TestQuadTreeItem horizontalItem = makeTreeItem( 300.f, 500.f, 1 );
	quadTree.add( verticalItem );
	quadTree.add( horizontalItem );

	TraversalSet up = collectTraversal( quadTree,
		Vector3( 500.f, 0.f, 100.f ), Vector3( 500.f, 0.f, 900.f ), 2.f );
	CHECK_EQUAL( 1, up.size() );
	CHECK( up.find( &verticalItem ) != up.end() );

	TraversalSet across = collectTraversal( quadTree,
		Vector3( 100.f, 0.f, 500.f ), Vector3( 900.f, 0.f, 500.f ), 2.f );
	CHECK_EQUAL( 1, across.size() );
	CHECK( across.find( &horizontalItem ) != across.end() );
}


TEST( QuadTree_traverseRadiusBand )
{
	QuadTree< TestQuadTreeItem > quadTree( 0.f, 0.f, 5, 1000.f );

	// The item box sits ~43 units off the z = x diagonal.
	TestQuadTreeItem offLine = makeTreeItem( 202.f, 140.f, 0 );
	quadTree.add( offLine );

	// A wide band reaches it, a narrow one does not.
	TraversalSet wide = collectTraversal( quadTree,
		Vector3( 10.f, 0.f, 10.f ), Vector3( 400.f, 0.f, 400.f ), 50.f );
	CHECK_EQUAL( 1, wide.size() );
	CHECK( wide.find( &offLine ) != wide.end() );

	TraversalSet narrow = collectTraversal( quadTree,
		Vector3( 10.f, 0.f, 10.f ), Vector3( 400.f, 0.f, 400.f ), 10.f );
	CHECK_EQUAL( 0, narrow.size() );
}


TEST( QuadTree_largeElementStaysAtRoot )
{
	QuadTree< TestQuadTreeItem > quadTree( 0.f, 0.f, 5, 1000.f );

	// A box spanning the whole 1000-unit range clips to exactly the root
	// range, so the tree stores it in the root node rather than descending
	// into a chain of overlapping children.
	TestQuadTreeItem huge = makeTreeItem( 0.f, 0.f, 3 );
	huge.bb_.setBounds( Vector3( 0.f, 0.f, 0.f ), Vector3( 1000.f, 1.f, 1000.f ) );
	quadTree.add( huge );

	CHECK_EQUAL( 1, quadTree.countNodes() );
	CHECK_EQUAL( 1, quadTree.getNumElements( quadTree.root() ) );
	CHECK_EQUAL( &huge, quadTree.getElement( quadTree.root(), 0 ) );
	CHECK_EQUAL( 1, quadTree.countElements() );

	TraversalSet uniqueElements;
	quadTree.getUniqueElements( uniqueElements );
	CHECK_EQUAL( 1, uniqueElements.size() );
	CHECK( uniqueElements.find( &huge ) != uniqueElements.end() );

	// Every sweep reports it: it sits in the root node the traversal starts
	// from, and size() only walks the root.
	TraversalSet found = collectTraversal( quadTree,
		Vector3( 100.f, 0.f, 100.f ), Vector3( 900.f, 0.f, 900.f ) );
	CHECK_EQUAL( 1, found.size() );
	CHECK( found.find( &huge ) != found.end() );
}


TEST( QuadTree_removeFromRootReportsMissing )
{
	QuadTree< TestQuadTreeItem > quadTree( 0.f, 0.f, 5, 1000.f );

	TestQuadTreeItem present = makeTreeItem( 10.f, 10.f, 1 );
	quadTree.addToRoot( present );

	CHECK( quadTree.removeFromRoot( present ) );
	CHECK_EQUAL( 0, quadTree.getNumElements( quadTree.root() ) );

	// A second removal of the same element finds nothing left to remove.
	CHECK_EQUAL( false, quadTree.removeFromRoot( present ) );
	CHECK_EQUAL( 0, quadTree.getNumElements( quadTree.root() ) );
}


TEST( QuadTreeNodeElementPoolAndDelStub )
{
	typedef QuadTreeNode< TestQuadTreeItem > Node;

	Node node;
	node.init();

	CHECK_EQUAL( Node::INVALID_NODE, node.getChildRef( QUAD_BL ) );
	CHECK_EQUAL( Node::INVALID_NODE, node.getChildRef( QUAD_BR ) );
	CHECK_EQUAL( Node::INVALID_NODE, node.getChildRef( QUAD_TL ) );
	CHECK_EQUAL( Node::INVALID_NODE, node.getChildRef( QUAD_TR ) );
	CHECK_EQUAL( 0, node.getNumElements() );
	CHECK_EQUAL( 0, node.getElementsCapacity() );

	TestQuadTreeItem first = makeTreeItem( 10.f, 10.f, 1 );
	TestQuadTreeItem second = makeTreeItem( 20.f, 20.f, 2 );

	// addElement() reserves a batch up front, so capacity is non-zero even
	// though only one element has been added.
	node.addElement( &first );
	CHECK_EQUAL( 1, node.getNumElements() );
	CHECK_EQUAL( &first, node.getElement( 0 ) );
	CHECK( node.getElementsCapacity() >= node.getNumElements() );

	// Node del() is an upstream stub ("TODO: Implement this!") and the game
	// never reaches it, because its only caller QuadTree::del does not
	// compile. Pin the stub's behaviour here rather than leaving the delete
	// path silently uncovered.
	QTRange range;
	range.left_ = 0;
	range.bottom_ = 0;
	range.right_ = 3;
	range.top_ = 3;

	CHECK_EQUAL( false, node.del( &first, range, 5 ) );
	CHECK_EQUAL( 1, node.getNumElements() );
	CHECK_EQUAL( &first, node.getElement( 0 ) );

	// swapLastRemove() moves the last element over the removed slot.
	node.addElement( &second );
	CHECK_EQUAL( 2, node.getNumElements() );
	node.removeElement( 0 );
	CHECK_EQUAL( 1, node.getNumElements() );
	CHECK_EQUAL( &second, node.getElement( 0 ) );

	// Removing the only element leaves an empty pool.
	node.removeElement( 0 );
	CHECK_EQUAL( 0, node.getNumElements() );

	node.destroy();
	CHECK_EQUAL( 0, node.getNumElements() );
	CHECK_EQUAL( 0, node.getElementsCapacity() );
}


// Note: QuadTree::testPoint, QuadTree::del and the debug print family
// (print/printQTNode/countAt) are NOT exercised here. They are never
// instantiated by the game (ObstacleTree only uses add/addToRoot/
// removeFromRoot) and carry latent compile errors that surface on
// instantiation: the public testPoint calls a non-existent
// QuadTreeNode::testPoint, del passes 3 arguments to a 4-parameter
// calculateQTRange, and printQTNode calls the non-existent
// node.elements(). countAt has no other caller. Registered as structurally
// untestable in TESTING.md.

// -----------------------------------------------------------------------------
// Section: Batch 20 coverage — newly added tests targeting uncovered paths
// -----------------------------------------------------------------------------

namespace
{

struct TreeItemWithBBox
{
    int id_;
    BoundingBox bb_;
    Matrix transform_;
};

TreeItemWithBBox makeBBoxItem( float x, float z, float w, float h, int id )
{
    TreeItemWithBBox item;
    item.id_ = id;
    item.transform_.setIdentity();
    item.transform_.setTranslate( x, 0.f, z );
    item.bb_.setBounds( Vector3( 0.f, 0.f, 0.f ), Vector3( w, 1.f, h ) );
    return item;
}

QTRange calculateQTRange( const TreeItemWithBBox & input, const Vector2 & origin,
                          float range, int depth )
{
    const float WIDTH = float(1 << depth);

    BoundingBox bb = input.bb_;
    bb.transformBy( input.transform_ );

    QTRange rv;
    rv.left_   = int(WIDTH * (bb.minBounds().x - origin.x)/range);
    rv.right_  = int(WIDTH * (bb.maxBounds().x - origin.x)/range);
    rv.bottom_ = int(WIDTH * (bb.minBounds().z - origin.y)/range);
    rv.top_    = int(WIDTH * (bb.maxBounds().z - origin.y)/range);

    return rv;
}

} // end anonymous namespace


// --- QTRange corner cases ----------------------------------------------------

TEST( QuadTreeQTRange_CornersAndClip )
{
    // fills(): exact node coverage at various depths
    {
        QTRange r; r.left_=0; r.bottom_=0; r.right_=3; r.top_=3;
        CHECK( r.fills( 2 ) );        // 2^2-1 = 3
        CHECK( !r.fills( 3 ) );
    }
    {
        QTRange r; r.left_=0; r.bottom_=0; r.right_=15; r.top_=15;
        CHECK( r.fills( 4 ) );
    }

    // isValid() with negative coords and exceeding MAX
    {
        QTRange r; r.left_=-1; r.bottom_=0; r.right_=3; r.top_=3;
        CHECK( !r.isValid( 2 ) );
    }
    {
        QTRange r; r.left_=0; r.bottom_=0; r.right_=4; r.top_=3; // right exceeds MAX=3
        CHECK( !r.isValid( 2 ) );
    }
    {
        QTRange r; r.left_=0; r.bottom_=0; r.right_=3; r.top_=3;
        CHECK( r.isValid( 2 ) );
    }

    // inQuad() rejects entirely-outside ranges
    {
        QTRange r; r.left_=10; r.bottom_=10; r.right_=12; r.top_=12;
        CHECK( !r.inQuad( 2 ) );     // MAX=3
    }
    {
        QTRange r; r.left_=-5; r.bottom_=-5; r.right_=-2; r.top_=-2;
        CHECK( !r.inQuad( 2 ) );
    }
    {
        QTRange r; r.left_=1; r.bottom_=1; r.right_=2; r.top_=2;
        CHECK( r.inQuad( 2 ) );
    }

    // clip() pulls coordinates back into [0, MAX]
    {
        QTRange r; r.left_=-2; r.bottom_=-1; r.right_=5; r.top_=4;
        r.clip( 3 );                 // MAX=7
        CHECK_EQUAL( 0, r.left_ );
        CHECK_EQUAL( 0, r.bottom_ );
        CHECK_EQUAL( 5, r.right_ );
        CHECK_EQUAL( 4, r.top_ );
    }
    {
        QTRange r; r.left_=0; r.bottom_=0; r.right_=10; r.top_=10;
        r.clip( 2 );                 // MAX=3
        CHECK_EQUAL( 0, r.left_ );
        CHECK_EQUAL( 0, r.bottom_ );
        CHECK_EQUAL( 3, r.right_ );
        CHECK_EQUAL( 3, r.top_ );
    }
}


// --- Recursive addElement subdivision (spanning multiple quadrants) ----------

TEST( QuadTree_addElement_SpansFourQuadrants_CreatesAllChildren )
{
    // Tree: origin (0,0), range 1000, depth 4 => cell size = 1000/16 = 62.5
    // Element spans x in [200,800], z in [200,800] -> covers centre, hits all 4 quadrants
    QuadTree< TreeItemWithBBox > tree( 0.f, 0.f, 4, 1000.f );

    TreeItemWithBBox big = makeBBoxItem( 200.f, 200.f, 600.f, 600.f, 42 );
    tree.add( big );

    // Root plus 4 children at level 1 should exist; deeper children where the
    // range continues to span will also be created. At minimum we expect 5 nodes.
    CHECK( tree.countNodes() >= 5 );

    // The element should be reachable via traversal
    typedef BW::set< const TreeItemWithBBox * > FoundSet;
    FoundSet found;
    tree.getUniqueElements( found );
    CHECK_EQUAL( 1, found.size() );
    CHECK( found.find( &big ) != found.end() );
}


TEST( QuadTree_addElement_SpansTwoQuadrants_Horizontal )
{
    // Spans left and right child at root, but fits in one vertical child
    QuadTree< TreeItemWithBBox > tree( 0.f, 0.f, 4, 1000.f );

    TreeItemWithBBox wide = makeBBoxItem( 100.f, 100.f, 800.f, 200.f, 7 ); // x spans, z narrow
    tree.add( wide );

    CHECK( tree.countNodes() >= 3 ); // root + 2 horizontal children
    typedef BW::set< const TreeItemWithBBox * > FoundSet;
    FoundSet found;
    tree.getUniqueElements( found );
    CHECK_EQUAL( 1, found.size() );
}


TEST( QuadTree_addElement_SpansTwoQuadrants_Vertical )
{
    QuadTree< TreeItemWithBBox > tree( 0.f, 0.f, 4, 1000.f );

    TreeItemWithBBox tall = makeBBoxItem( 100.f, 100.f, 200.f, 800.f, 9 ); // z spans, x narrow
    tree.add( tall );

    CHECK( tree.countNodes() >= 3 ); // root + 2 vertical children
    typedef BW::set< const TreeItemWithBBox * > FoundSet;
    FoundSet found;
    tree.getUniqueElements( found );
    CHECK_EQUAL( 1, found.size() );
}


TEST( QuadTree_addElement_SpansDeepQuadrants )
{
    // An element that keeps splitting for several levels before filling a node
    QuadTree< TreeItemWithBBox > tree( 0.f, 0.f, 5, 1000.f );

    // 125..875 spans almost whole tree but not quite root
    TreeItemWithBBox almostFull = makeBBoxItem( 125.f, 125.f, 750.f, 750.f, 99 );
    tree.add( almostFull );

    // Should create a chain of nodes down to where it finally fills
    CHECK( tree.countNodes() > 10 );

    typedef BW::set< const TreeItemWithBBox * > FoundSet;
    FoundSet found;
    tree.getUniqueElements( found );
    CHECK_EQUAL( 1, found.size() );
}


// --- Traversal edge cases ----------------------------------------------------

TEST( QuadTree_traverse_DegenerateZeroLengthLine )
{
    QuadTree< TreeItemWithBBox > tree( 0.f, 0.f, 5, 1000.f );
    TreeItemWithBBox item = makeBBoxItem( 500.f, 500.f, 10.f, 10.f, 1 );
    tree.add( item );

    // Zero-length segment: src == dst. The traversal should still visit the node
    // containing the point if radius covers it.
    typedef BW::set< const TreeItemWithBBox * > FoundSet;
    FoundSet found = collectTraversalGeneric( tree,
        Vector3( 500.f, 0.f, 500.f ), Vector3( 500.f, 0.f, 500.f ), 5.f );
    CHECK_EQUAL( 1, found.size() );

    // Without radius, a zero-length line at the item's position still intersects
    // because the traversal checks the root node elements. This is expected
    // behaviour - the radius band check only applies to line traversal.
    FoundSet foundZero = collectTraversalGeneric( tree,
        Vector3( 500.f, 0.f, 500.f ), Vector3( 500.f, 0.f, 500.f ), 0.f );
    CHECK_EQUAL( 1, foundZero.size() );

    // But a zero-length line far from the item should find nothing
    FoundSet foundFar = collectTraversalGeneric( tree,
        Vector3( 100.f, 0.f, 100.f ), Vector3( 100.f, 0.f, 100.f ), 0.f );
    CHECK_EQUAL( 0, foundFar.size() );
}


TEST( QuadTree_traverse_DegenerateAxisParallel_WithRadius )
{
    // dir.x == 0 (vertical) and dir.y == 0 (horizontal) with non-zero radius
    QuadTree< TreeItemWithBBox > tree( 0.f, 0.f, 5, 1000.f );
    TreeItemWithBBox v = makeBBoxItem( 500.f, 300.f, 10.f, 10.f, 1 );
    TreeItemWithBBox h = makeBBoxItem( 300.f, 500.f, 10.f, 10.f, 2 );
    tree.add( v );
    tree.add( h );

    typedef BW::set< const TreeItemWithBBox * > FoundSet;

    // Vertical line with radius reaching h's x=300..310
    FoundSet vr = collectTraversalGeneric( tree,
        Vector3( 500.f, 0.f, 100.f ), Vector3( 500.f, 0.f, 900.f ), 200.f );
    CHECK( vr.find( &v ) != vr.end() );
    CHECK( vr.find( &h ) != vr.end() );

    // Horizontal line with radius reaching v's z=300..310
    FoundSet hr = collectTraversalGeneric( tree,
        Vector3( 100.f, 0.f, 500.f ), Vector3( 900.f, 0.f, 500.f ), 200.f );
    CHECK( hr.find( &v ) != hr.end() );
    CHECK( hr.find( &h ) != hr.end() );
}


TEST( QuadTree_traverse_NarrowRadius_Misses )
{
    QuadTree< TreeItemWithBBox > tree( 0.f, 0.f, 5, 1000.f );
    TreeItemWithBBox item = makeBBoxItem( 202.f, 140.f, 10.f, 10.f, 1 );
    tree.add( item );

    typedef BW::set< const TreeItemWithBBox * > FoundSet;
    FoundSet narrow = collectTraversalGeneric( tree,
        Vector3( 10.f, 0.f, 10.f ), Vector3( 400.f, 0.f, 400.f ), 5.f );
    CHECK_EQUAL( 0, narrow.size() );

    FoundSet wide = collectTraversalGeneric( tree,
        Vector3( 10.f, 0.f, 10.f ), Vector3( 400.f, 0.f, 400.f ), 100.f );
    CHECK_EQUAL( 1, wide.size() );
}


// --- Debug / introspection functions ----------------------------------------

TEST( QuadTree_countNodes_countElements_getUniqueElements_Recursive )
{
    QuadTree< TreeItemWithBBox > tree( 0.f, 0.f, 3, 1000.f );

    // Add items that force creation of multiple nodes - keep them alive!
    TreeItemWithBBox items[20];
    for (int i = 0; i < 20; ++i)
    {
        float x = 50.f + (i % 4) * 200.f;
        float z = 50.f + (i / 4) * 200.f;
        items[i] = makeBBoxItem( x, z, 50.f, 50.f, i );
        tree.add( items[i] );
    }

    int nodes = tree.countNodes();
    int elements = tree.countElements();

    CHECK( nodes > 1 );
    CHECK( elements >= 20 ); // some elements may be in multiple nodes

    BW::set< const TreeItemWithBBox * > unique;
    tree.getUniqueElements( unique );
    CHECK_EQUAL( 20, unique.size() );
}


// countAt is private and has an internal type bug (passes int to getChildRef
// which expects Quad enum). Skipped per TESTING.md note.


// --- Node element pool stress ------------------------------------------------

TEST( QuadTreeNode_ElementPool_GrowthAndSwapRemove )
{
    typedef QuadTreeNode< TreeItemWithBBox > Node;

    Node node;
    node.init();

    // Add enough elements to force multiple reallocations
    const int N = 100;
    TreeItemWithBBox* items = new TreeItemWithBBox[N];

    for (int i = 0; i < N; ++i)
    {
        items[i] = makeBBoxItem( float(i), float(i), 1.f, 1.f, i );
        node.addElement( &items[i] );
    }

    CHECK_EQUAL( N, node.getNumElements() );
    CHECK( node.getElementsCapacity() >= N );

    // Remove from middle repeatedly, verifying swap-last behaviour
    for (int i = N - 1; i >= 0; --i)
    {
        node.removeElement( 0 ); // always remove first -> last gets swapped in
        CHECK_EQUAL( i, node.getNumElements() );
    }

    CHECK_EQUAL( 0, node.getNumElements() );
    node.destroy();

    delete[] items;
}


// --- testPoint is structurally broken in the library (QuadTree::testPoint calls
// non-existent QuadTreeNode::testPoint). Skipped per TESTING.md note.
// 
// namespace
// {
// bool containsTestPoint( const TreeItemWithBBox & item, const Vector3 & point )
// {
//     BoundingBox bb = item.bb_;
//     bb.transformBy( item.transform_ );
//     return bb.intersects( point );
// }
// }
// 
// TEST( QuadTree_testPoint_HitAndMiss ) { ... }
// TEST( QuadTree_testPoint_PrefersDeeperNode ) { ... }


BW_END_NAMESPACE
