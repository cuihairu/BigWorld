// test_reload_script_plan.cpp
//
// The CellApp::reloadScript action sequence, as testable data. The sequence is
// the part of reloadScript that has gone wrong historically (failure modes F1,
// F3 and F8 in docs/upgrade-plan/hot-reload-productionization.md), and it is
// only reachable inside a live CellApp. planReload() carries it as data, so
// the branch policy and the ordering asymmetries can be pinned down here:
//
//   * success destroys the scratch interpreter BEFORE the migrate chain runs
//     (cellapp.cpp) and cleans up last;
//   * failure destroys it AFTER cleanup;
//   * the old-scripts recovery happens on every failure path except a failed
//     full reload - a full reload has no old scripts to fall back to;
//   * a failed full reload wipes the registered data types and never reloads
//     them (F8), which planReload() reports as dataTypesWiped.

#include "pch.hpp"

#include "../reload_script_plan.hpp"


BW_BEGIN_NAMESPACE


namespace
{

// Find an action in a plan, or -1 if it is not there.
int indexOf( const ReloadScript::ReloadPlan & plan, ReloadScript::Action action )
{
	for (int i = 0; i < plan.count; ++i)
	{
		if (plan.actions[ i ] == action)
		{
			return i;
		}
	}
	return -1;
}


// Count occurrences of an action in a plan.
int countOf( const ReloadScript::ReloadPlan & plan, ReloadScript::Action action )
{
	int count = 0;
	for (int i = 0; i < plan.count; ++i)
	{
		if (plan.actions[ i ] == action)
		{
			++count;
		}
	}
	return count;
}


} // end anonymous namespace


/**
 *	A successful full reload runs the whole sequence: the scratch interpreter
 *	is swapped in, the loads run inside it, it is destroyed before the migrate
 *	chain, and cleanup runs last.
 */
TEST( ReloadScriptPlan_FullReloadSucceeds )
{
	const ReloadScript::PlanInputs inputs = { true, true, true };
	const ReloadScript::ReloadPlan plan = ReloadScript::planReload( inputs );

	CHECK_EQUAL( true, plan.succeeded );
	CHECK_EQUAL( false, plan.dataTypesWiped );
	CHECK_EQUAL( 10, plan.count );

	CHECK_EQUAL( ReloadScript::SWAP_IN_INTERPRETER, plan.actions[ 0 ] );
	CHECK_EQUAL( ReloadScript::LOAD_ENTITY_DEFS, plan.actions[ 1 ] );
	CHECK_EQUAL( ReloadScript::LOAD_USER_DATA_OBJECTS, plan.actions[ 2 ] );
	CHECK_EQUAL( ReloadScript::SWAP_BACK_INTERPRETER, plan.actions[ 3 ] );
	CHECK_EQUAL( ReloadScript::DESTROY_NEW_INTERPRETER, plan.actions[ 4 ] );
	CHECK_EQUAL( ReloadScript::MIGRATE_ENTITY_TYPES, plan.actions[ 5 ] );
	CHECK_EQUAL( ReloadScript::MIGRATE_USER_DATA_OBJECT_TYPES,
		plan.actions[ 6 ] );
	CHECK_EQUAL( ReloadScript::MIGRATE_MAIL_BOXES, plan.actions[ 7 ] );
	CHECK_EQUAL( ReloadScript::MIGRATE_ENTITIES, plan.actions[ 8 ] );
	CHECK_EQUAL( ReloadScript::CLEANUP_AFTER_RELOAD, plan.actions[ 9 ] );

	// The ordering asymmetry that makes the migrate chain work: the scratch
	// interpreter is gone before anything migrates, and cleanup is last.
	CHECK( indexOf( plan, ReloadScript::DESTROY_NEW_INTERPRETER )
		< indexOf( plan, ReloadScript::MIGRATE_ENTITY_TYPES ) );
	CHECK_EQUAL( plan.count - 1,
		indexOf( plan, ReloadScript::CLEANUP_AFTER_RELOAD ) );
	CHECK( indexOf( plan, ReloadScript::RECOVER_OLD_SCRIPTS ) == -1 );
}


/**
 *	A scripts-only reload has the same success tail: the migrate chain is the
 *	same either way, and only the defs load itself differs between modes.
 */
TEST( ReloadScriptPlan_ScriptsOnlyReloadSucceeds )
{
	const ReloadScript::PlanInputs fullInputs = { true, true, true };
	const ReloadScript::PlanInputs scriptInputs = { false, true, true };

	const ReloadScript::ReloadPlan fullPlan =
		ReloadScript::planReload( fullInputs );
	const ReloadScript::ReloadPlan scriptPlan =
		ReloadScript::planReload( scriptInputs );

	CHECK_EQUAL( true, scriptPlan.succeeded );
	CHECK_EQUAL( 10, scriptPlan.count );

	for (int i = 3; i < fullPlan.count; ++i)
	{
		CHECK_EQUAL( fullPlan.actions[ i ], scriptPlan.actions[ i ] );
	}
}


/**
 *	A failed full reload has no old scripts to recover from, and it wiped the
 *	registered data types before it failed (F8): nothing in the sequence
 *	restores them.
 */
TEST( ReloadScriptPlan_FullReloadDefsFail )
{
	const ReloadScript::PlanInputs inputs = { true, false, false };
	const ReloadScript::ReloadPlan plan = ReloadScript::planReload( inputs );

	CHECK_EQUAL( false, plan.succeeded );
	CHECK_EQUAL( true, plan.dataTypesWiped );
	CHECK_EQUAL( 5, plan.count );

	CHECK_EQUAL( ReloadScript::LOAD_ENTITY_DEFS, plan.actions[ 1 ] );
	CHECK( indexOf( plan, ReloadScript::LOAD_USER_DATA_OBJECTS ) == -1 );
	CHECK( indexOf( plan, ReloadScript::RECOVER_OLD_SCRIPTS ) == -1 );

	// Failure destroys the scratch interpreter AFTER cleanup, the reverse of
	// the success order.
	CHECK( indexOf( plan, ReloadScript::CLEANUP_AFTER_RELOAD )
		< indexOf( plan, ReloadScript::DESTROY_NEW_INTERPRETER ) );
	CHECK_EQUAL( plan.count - 1,
		indexOf( plan, ReloadScript::DESTROY_NEW_INTERPRETER ) );
}


/**
 *	A failed scripts-only reload recovers the old scripts between the swap
 *	back and the cleanup.
 */
TEST( ReloadScriptPlan_ScriptsOnlyReloadDefsFail )
{
	const ReloadScript::PlanInputs inputs = { false, false, false };
	const ReloadScript::ReloadPlan plan = ReloadScript::planReload( inputs );

	CHECK_EQUAL( false, plan.succeeded );
	CHECK_EQUAL( false, plan.dataTypesWiped );
	CHECK_EQUAL( 6, plan.count );

	CHECK_EQUAL( ReloadScript::SWAP_BACK_INTERPRETER, plan.actions[ 2 ] );
	CHECK_EQUAL( ReloadScript::RECOVER_OLD_SCRIPTS, plan.actions[ 3 ] );
	CHECK_EQUAL( ReloadScript::CLEANUP_AFTER_RELOAD, plan.actions[ 4 ] );
	CHECK_EQUAL( ReloadScript::DESTROY_NEW_INTERPRETER,
		plan.actions[ 5 ] );
}


/**
 *	A failed UDO load follows the failure path too: the migrate chain never
 *	runs and the entity definitions loaded inside the scratch interpreter are
 *	not migrated into the live one.
 */
TEST( ReloadScriptPlan_UdoLoadFails )
{
	const ReloadScript::PlanInputs fullInputs = { true, true, false };
	const ReloadScript::PlanInputs scriptInputs = { false, true, false };

	for (int mode = 0; mode < 2; ++mode)
	{
		const ReloadScript::PlanInputs * pInputs =
			(mode == 0) ? &fullInputs : &scriptInputs;
		const ReloadScript::ReloadPlan plan =
			ReloadScript::planReload( *pInputs );

		CHECK_EQUAL( false, plan.succeeded );
		CHECK_EQUAL( false, plan.dataTypesWiped );
		CHECK_EQUAL( pInputs->isFullReload ? 6 : 7, plan.count );

		CHECK_EQUAL( 1, countOf( plan,
			ReloadScript::LOAD_USER_DATA_OBJECTS ) );
		CHECK( indexOf( plan, ReloadScript::LOAD_USER_DATA_OBJECTS ) > 0 );

		CHECK( indexOf( plan, ReloadScript::MIGRATE_ENTITY_TYPES ) == -1 );
		CHECK( indexOf( plan, ReloadScript::MIGRATE_USER_DATA_OBJECT_TYPES )
			== -1 );
		CHECK( indexOf( plan, ReloadScript::MIGRATE_MAIL_BOXES ) == -1 );
		CHECK( indexOf( plan, ReloadScript::MIGRATE_ENTITIES ) == -1 );

		CHECK( indexOf( plan, ReloadScript::CLEANUP_AFTER_RELOAD )
			< indexOf( plan, ReloadScript::DESTROY_NEW_INTERPRETER ) );
	}
}


/**
 *	The UDO load is never attempted after a failed defs load, so its reported
 *	outcome is irrelevant on that path.
 */
TEST( ReloadScriptPlan_UdoOutcomeIgnoredAfterDefsFailure )
{
	const ReloadScript::PlanInputs ignoredTrue = { true, false, true };
	const ReloadScript::PlanInputs ignoredFalse = { true, false, false };

	const ReloadScript::ReloadPlan planTrue =
		ReloadScript::planReload( ignoredTrue );
	const ReloadScript::ReloadPlan planFalse =
		ReloadScript::planReload( ignoredFalse );

	CHECK_EQUAL( planFalse.count, planTrue.count );
	for (int i = 0; i < planTrue.count; ++i)
	{
		CHECK_EQUAL( planFalse.actions[ i ], planTrue.actions[ i ] );
	}
	CHECK_EQUAL( planFalse.dataTypesWiped, planTrue.dataTypesWiped );
}


/**
 *	Every outcome combination fits the fixed plan array, and the whole matrix
 *	is one of the three shapes the sequence has.
 */
TEST( ReloadScriptPlan_AllOutcomeCombinationsFit )
{
	for (int full = 0; full <= 1; ++full)
	{
		for (int defs = 0; defs <= 1; ++defs)
		{
			for (int udo = 0; udo <= 1; ++udo)
			{
				const ReloadScript::PlanInputs inputs =
					{ full != 0, defs != 0, udo != 0 };
				const ReloadScript::ReloadPlan plan =
					ReloadScript::planReload( inputs );

				CHECK( plan.count <= ReloadScript::ACTION_COUNT );
				CHECK_EQUAL( (defs && udo), plan.succeeded );
				CHECK_EQUAL( (full && !defs), plan.dataTypesWiped );

				// head (2 + defs load) + success tail (7) or failure tail
				// (3, plus the recovery on the scripts-only path).
				CHECK_EQUAL( 2 + (defs ? 1 : 0) +
					(plan.succeeded ? 7 : 3 + (full ? 0 : 1)),
					plan.count );
			}
		}
	}
}


// test_reload_script_plan.cpp

BW_END_NAMESPACE
