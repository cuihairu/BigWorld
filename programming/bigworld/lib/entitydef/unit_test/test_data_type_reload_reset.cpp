// test_data_type_reload_reset.cpp
//
// Coverage batch 50: what DataType::clearStaticsForReload actually tears
// down, and whether a failed full reload is left half-migrated at this
// layer.
//
// The failure path of a full reload reaches the reset and then gives up:
// EntityType::init(true) (server/cellapp/entity_type.cpp) calls
// DataType::clearStaticsForReload() before parsing the new entity
// descriptions, and returns false without rebuilding when the parse
// fails (F8, todo.md). At the DataType boundary the reset has to be
// either owning-and-torn or non-owning-and-self-healing; these tests pin
// the answer through the public surface only (buildDataType,
// clearStaticsForReload). The singleton map is a value-deduplication
// index of non-owning SingletonPtr wrappers (data_type.hpp, both
// private), so it is driven through buildDataType, whose tail is
// findOrAddType.
//
//   * the reset releases the index, not the indexed instances: a
//     DataType held by the caller survives clearStaticsForReload;
//   * buildDataType reallocates the index lazily on first use, and value
//     deduplication is live again after the reset;
//   * the lazy rebuild is a fresh index, not the freed one resurrected -
//     an instance registered before the reset is not a match for an equal
//     instance built after it.
//
// Out of reach of this target: the entitydef description/type registry
// reset (EntityType::s_curTypes_ / s_newTypes_), which is cellapp-local
// and needs the cellapp runtime carrier recorded in todo.md L48.
//
// clearStaticsForReload() is process-global. Each test that resets is
// self-contained - it rebuilds whatever it reads afterwards - and the
// harness tears the statics down again at exit (main.cpp).

#include "pch.hpp"

#include "test_harness.hpp"

#include "entitydef/data_type.hpp"

BW_BEGIN_NAMESPACE

/**
 *	Two value-equal type sections collapse onto one instance through the
 *	singleton index, while a different parameterisation registers as its
 *	own instance. This is the deduplication contract MethodArgs relies on
 *	(its comparison operator assumes same-type arguments share one
 *	DataType).
 */
TEST_F( EntityDefUnitTestHarness, DataType_buildDataType_dedupesValueEqualTypes )
{
	DataTypePtr pFirst = DataType::buildDataType(
		"<Type> ARRAY <of> INT8 </of> <size> 5 </size> </Type>" );
	CHECK( pFirst );

	DataTypePtr pSecond = DataType::buildDataType(
		"<Type> ARRAY <of> INT8 </of> <size> 5 </size> </Type>" );
	CHECK( pSecond );

	CHECK_EQUAL( (DataType*)pFirst.get(), (DataType*)pSecond.get() );

	// Same kind, different parameter: not a duplicate.
	DataTypePtr pOther = DataType::buildDataType(
		"<Type> ARRAY <of> INT8 </of> <size> 6 </size> </Type>" );
	CHECK( pOther );
	CHECK( pOther.get() != pFirst.get() );
}


/**
 *	The singleton index is rebuilt lazily after the reset, and
 *	deduplication is live again: two equal sections built after
 *	clearStaticsForReload collapse onto one fresh instance.
 */
TEST_F( EntityDefUnitTestHarness, DataType_clearStaticsForReload_rebuildsDedupIndex )
{
	DataType::clearStaticsForReload();

	DataTypePtr pFirst = DataType::buildDataType(
		"<Type> ARRAY <of> INT8 </of> <size> 4 </size> </Type>" );
	CHECK( pFirst );

	DataTypePtr pSecond = DataType::buildDataType(
		"<Type> ARRAY <of> INT8 </of> <size> 4 </size> </Type>" );
	CHECK( pSecond );

	CHECK_EQUAL( (DataType*)pFirst.get(), (DataType*)pSecond.get() );
}


/**
 *	The lazily rebuilt index is a fresh one, not the freed instance
 *	resurrected: an equal section built after the reset is not a match
 *	for the instance registered before it, and post-reset duplicates
 *	collapse onto the post-reset instance instead.
 */
TEST_F( EntityDefUnitTestHarness, DataType_clearStaticsForReload_freshIndexAfterReset )
{
	DataTypePtr pBefore = DataType::buildDataType(
		"<Type> ARRAY <of> INT16 </of> <size> 3 </size> </Type>" );
	CHECK( pBefore );

	DataType::clearStaticsForReload();

	DataTypePtr pAfter = DataType::buildDataType(
		"<Type> ARRAY <of> INT16 </of> <size> 3 </size> </Type>" );
	CHECK( pAfter );

	CHECK( pAfter.get() != pBefore.get() );

	DataTypePtr pAfterSecond = DataType::buildDataType(
		"<Type> ARRAY <of> INT16 </of> <size> 3 </size> </Type>" );
	CHECK( pAfterSecond );
	CHECK_EQUAL( (DataType*)pAfter.get(), (DataType*)pAfterSecond.get() );
}


/**
 *	The reset releases the index, not the indexed instances. The
 *	singleton map holds non-owning wrappers, so a DataType still owned by
 *	the caller survives clearStaticsForReload - no dangling pointer left
 *	behind for a reload that then fails.
 */
TEST_F( EntityDefUnitTestHarness, DataType_clearStaticsForReload_noOwnership )
{
	DataTypePtr pType = DataType::buildDataType(
		"<Type> ARRAY <of> INT8 </of> <size> 5 </size> </Type>" );
	CHECK( pType );
	int size = pType->streamSize();
	CHECK( size > 0 );

	DataType::clearStaticsForReload();

	// Same instance, still addressable and still reporting its layout.
	CHECK( pType.get() != NULL );
	CHECK_EQUAL( size, pType->streamSize() );
}


BW_END_NAMESPACE
