#include "pch.hpp"

#include "cstdmf/bw_safe_allocatable.hpp"


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

// Live-instance counter, so the tests can prove the class operator
// new/delete pair runs the whole object lifecycle.
int s_liveInstances = 0;


/**
 *	A minimal class that inherits the allocator overrides; nothing
 * else about it is interesting here. The default constructor exists
 * for the array tests.
 */
class Tracked : public SafeAllocatable
{
public:
	Tracked() : id_( 0 ), stamp_( 0 )
	{
		++s_liveInstances;
	}

	explicit Tracked( int id ) : id_( id ), stamp_( 0 )
	{
		++s_liveInstances;
	}

	~Tracked()
	{
		--s_liveInstances;
	}

	int id() const			{ return id_; }
	void stamp( int value )	{ stamp_ = value; }
	int stamp() const		{ return stamp_; }

private:
	int id_;
	int stamp_;
};

} // end namespace (anonymous)


// A single object comes and goes through the class operator new/delete,
// which is where the class earns its keep: these are the BigWorld
// allocator's entry points, not ::new/::delete.
TEST( SafeAllocatable_singleObject )
{
	CHECK_EQUAL( 0, s_liveInstances );

	Tracked * pTracked = new Tracked( 7 );
	CHECK( pTracked != NULL );
	CHECK_EQUAL( 1, s_liveInstances );
	CHECK_EQUAL( 7, pTracked->id() );
	CHECK_EQUAL( 0, pTracked->stamp() );

	pTracked->stamp( 42 );
	CHECK_EQUAL( 42, pTracked->stamp() );

	delete pTracked;
	CHECK_EQUAL( 0, s_liveInstances );
}


// Arrays route through operator new[]/operator delete[], including
// the zero-length request, which the underlying allocator still has to
// hand back as a real (1 byte) block.
TEST( SafeAllocatable_array )
{
	CHECK_EQUAL( 0, s_liveInstances );

	Tracked * pArray = new Tracked[ 4 ];
	CHECK( pArray != NULL );
	CHECK_EQUAL( 4, s_liveInstances );

	for ( int i = 0; i < 4; ++i )
	{
		pArray[ i ].stamp( i );
	}
	for ( int i = 0; i < 4; ++i )
	{
		CHECK_EQUAL( i, pArray[ i ].stamp() );
	}

	delete[] pArray;
	CHECK_EQUAL( 0, s_liveInstances );

	// A zero-element array still allocates and frees a block, and the
	// destructor must not run.
	Tracked * pEmpty = new Tracked[ 0 ];
	CHECK( pEmpty != NULL );
	CHECK_EQUAL( 0, s_liveInstances );
	delete[] pEmpty;
	CHECK_EQUAL( 0, s_liveInstances );
}

BW_END_NAMESPACE

// test_bw_safe_allocatable.cpp
