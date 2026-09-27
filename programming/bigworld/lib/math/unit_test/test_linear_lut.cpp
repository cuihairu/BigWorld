#include "pch.hpp"

#include "math/linear_lut.hpp"


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

/**
 *	The three-point table used by most of the tests: y doubles with x,
 * with a non-zero value at the lower end so the boundary conditions are
 * distinguishable from each other.
 */
LinearLUT doublingTable( LinearLUT::BoundaryCondition lowerBC =
		LinearLUT::BC_CONSTANT_EXTEND,
	LinearLUT::BoundaryCondition upperBC = LinearLUT::BC_CONSTANT_EXTEND )
{
	BW::vector< Vector2 > points;
	points.push_back( Vector2( 0.f, 2.f ) );
	points.push_back( Vector2( 1.f, 4.f ) );
	points.push_back( Vector2( 2.f, 8.f ) );

	LinearLUT lut;
	lut.data( points );
	lut.lowerBoundaryCondition( lowerBC );
	lut.upperBoundaryCondition( upperBC );
	return lut;
}

} // end namespace (anonymous)


// Lookups inside the table interpolate between the bracketing entries, a
// second lookup in the same segment is served by the cached segment, and
// exact nodes return their own values.
TEST( LinearLUT_interpolatesInsideTable )
{
	LinearLUT lut = doublingTable();

	CHECK_CLOSE( 2.f, lut( 0.f ), 0.0001f );
	CHECK_CLOSE( 4.f, lut( 1.f ), 0.0001f );
	CHECK_CLOSE( 8.f, lut( 2.f ), 0.0001f );

	// The first interior lookup seeds the segment cache... slope of
	// this segment is ( 8 - 4 ) / ( 2 - 1 ) = 4, hence 6 at 1.5.
	CHECK_CLOSE( 6.f, lut( 1.5f ), 0.0001f );
	// ... and the second one in the same segment takes the cached path.
	CHECK_CLOSE( 6.4f, lut( 1.6f ), 0.0001f );

	// Both halves of the first segment.
	CHECK_CLOSE( 3.f, lut( 0.5f ), 0.0001f );
	CHECK_CLOSE( 2.5f, lut( 0.25f ), 0.0001f );
}


// data() sorts its input by x-coordinate, so an unsorted table behaves
// exactly like the sorted one, and the accessor hands the sorted points
// back.
TEST( LinearLUT_dataIsSortedOnInsert )
{
	BW::vector< Vector2 > points;
	points.push_back( Vector2( 2.f, 8.f ) );
	points.push_back( Vector2( 0.f, 2.f ) );
	points.push_back( Vector2( 1.f, 4.f ) );

	LinearLUT lut;
	CHECK_EQUAL( 0u, unsigned( lut.data().size() ) );
	lut.data( points );

	CHECK_EQUAL( 3u, unsigned( lut.data().size() ) );
	CHECK_CLOSE( 0.f, lut.data()[ 0 ].x, 0.0001f );
	CHECK_CLOSE( 1.f, lut.data()[ 1 ].x, 0.0001f );
	CHECK_CLOSE( 2.f, lut.data()[ 2 ].x, 0.0001f );

	CHECK_CLOSE( 4.f, lut( 1.f ), 0.0001f );
	CHECK_CLOSE( 6.f, lut( 1.5f ), 0.0001f );
}


// An empty table answers 0 everywhere, and a one-entry table honours the
// zero boundary conditions on its own.
TEST( LinearLUT_degenerateTables )
{
	LinearLUT empty;
	CHECK_EQUAL( 0u, unsigned( empty.data().size() ) );
	CHECK_CLOSE( 0.f, empty( 1.f ), 0.0001f );

	BW::vector< Vector2 > single;
	single.push_back( Vector2( 1.f, 5.f ) );

	// The default boundary conditions extend the only value everywhere.
	LinearLUT constant;
	constant.data( single );
	CHECK_CLOSE( 5.f, constant( 0.f ), 0.0001f );
	CHECK_CLOSE( 5.f, constant( 1.f ), 0.0001f );
	CHECK_CLOSE( 5.f, constant( 2.f ), 0.0001f );

	// With a zero condition on either side, that side answers 0.
	LinearLUT zeroed;
	zeroed.data( single );
	zeroed.lowerBoundaryCondition( LinearLUT::BC_ZERO );
	zeroed.upperBoundaryCondition( LinearLUT::BC_ZERO );
	CHECK_CLOSE( 0.f, zeroed( 0.f ), 0.0001f );
	CHECK_CLOSE( 5.f, zeroed( 1.f ), 0.0001f );
	CHECK_CLOSE( 0.f, zeroed( 2.f ), 0.0001f );
}


// The four lower boundary conditions answer differently for x below the
// table. Note BC_WRAP's asymmetry: fmod() keeps the sign of its dividend,
// so a wrapped-below x lands in [x0 - span, x0] and falls through to the
// first-segment interpolation - values one full span apart below the table
// read the same, but nothing ever wraps in from the top.
TEST( LinearLUT_lowerBoundaryConditions )
{
	LinearLUT zeroed = doublingTable( LinearLUT::BC_ZERO );
	CHECK_CLOSE( 0.f, zeroed( -0.5f ), 0.0001f );

	LinearLUT constant = doublingTable( LinearLUT::BC_CONSTANT_EXTEND );
	CHECK_CLOSE( 2.f, constant( -0.5f ), 0.0001f );

	LinearLUT linear = doublingTable( LinearLUT::BC_LINEAR_EXTEND );
	CHECK_CLOSE( 1.f, linear( -0.5f ), 0.0001f );

	LinearLUT wrapped = doublingTable( LinearLUT::BC_WRAP );
	CHECK_CLOSE( 1.f, wrapped( -0.5f ), 0.0001f );
	// One full span lower reads the same as -0.5f.
	CHECK_CLOSE( 1.f, wrapped( -2.5f ), 0.0001f );
}


// The four upper boundary conditions answer differently for x above the
// table; up here BC_WRAP really does wrap, back in at the bottom of the
// table.
TEST( LinearLUT_upperBoundaryConditions )
{
	LinearLUT zeroed = doublingTable(
		LinearLUT::BC_CONSTANT_EXTEND, LinearLUT::BC_ZERO );
	CHECK_CLOSE( 0.f, zeroed( 2.5f ), 0.0001f );

	LinearLUT constant = doublingTable(
		LinearLUT::BC_CONSTANT_EXTEND, LinearLUT::BC_CONSTANT_EXTEND );
	CHECK_CLOSE( 8.f, constant( 2.5f ), 0.0001f );

	LinearLUT linear = doublingTable(
		LinearLUT::BC_CONSTANT_EXTEND, LinearLUT::BC_LINEAR_EXTEND );
	CHECK_CLOSE( 10.f, linear( 2.5f ), 0.0001f );

	LinearLUT wrapped = doublingTable(
		LinearLUT::BC_CONSTANT_EXTEND, LinearLUT::BC_WRAP );
	// 2.5 wraps to 0.5, which interpolates to 3.
	CHECK_CLOSE( 3.f, wrapped( 2.5f ), 0.0001f );
}


// The boundary condition accessors read back what was set, and both
// conditions default to extending the end values.
TEST( LinearLUT_boundaryAccessors )
{
	LinearLUT lut;
	CHECK_EQUAL( LinearLUT::BC_CONSTANT_EXTEND,
		lut.lowerBoundaryCondition() );
	CHECK_EQUAL( LinearLUT::BC_CONSTANT_EXTEND,
		lut.upperBoundaryCondition() );

	lut.lowerBoundaryCondition( LinearLUT::BC_WRAP );
	lut.upperBoundaryCondition( LinearLUT::BC_LINEAR_EXTEND );
	CHECK_EQUAL( LinearLUT::BC_WRAP, lut.lowerBoundaryCondition() );
	CHECK_EQUAL( LinearLUT::BC_LINEAR_EXTEND, lut.upperBoundaryCondition() );
}

BW_END_NAMESPACE

// test_linear_lut.cpp
