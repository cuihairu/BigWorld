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
//     them (F8), which planReload() reports as dataTypesWiped;
//   * a request's expectedDigest that disagrees with this load's schema
//     digest is judged right after the defs load, so the reload takes the
//     failure path and migrates nothing - the versioned-schema barrier,
//     which also counts as wiping on a full reload (F8);
//   * a dry run stops after the loads: swap back and destroy the scratch
//     interpreter, no cleanup and no recovery, whatever the loads reported.

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
	CHECK_EQUAL( false, plan.oldLinksDeferred );
	CHECK_EQUAL( 12, plan.count );

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
	CHECK_EQUAL( ReloadScript::MIGRATED_ALL, plan.actions[ 9 ] );
	CHECK_EQUAL( ReloadScript::CLEANUP_AFTER_RELOAD, plan.actions[ 10 ] );
	CHECK_EQUAL( ReloadScript::BREAK_OLD_LINKS, plan.actions[ 11 ] );

	// The ordering asymmetry that makes the migrate chain work: the scratch
	// interpreter is gone before anything migrates, and the cleanup halves
	// come last - the immediate half first, then the old-link break (this
	// is the window at zero: the break stays in the reload's own tick).
	CHECK( indexOf( plan, ReloadScript::DESTROY_NEW_INTERPRETER )
		< indexOf( plan, ReloadScript::MIGRATE_ENTITY_TYPES ) );
	CHECK( indexOf( plan, ReloadScript::CLEANUP_AFTER_RELOAD )
		< indexOf( plan, ReloadScript::BREAK_OLD_LINKS ) );
	CHECK_EQUAL( plan.count - 1,
		indexOf( plan, ReloadScript::BREAK_OLD_LINKS ) );
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
	CHECK_EQUAL( 11, scriptPlan.count );

	// The migrate chain is the same either way; the full reload carries one
	// extra step at the end, the old-link break that a window of zero keeps in
	// its own tick, which a scripts-only reload never runs.
	CHECK_EQUAL( fullPlan.count - 1, scriptPlan.count );

	for (int i = 3; i < scriptPlan.count; ++i)
	{
		CHECK_EQUAL( fullPlan.actions[ i ], scriptPlan.actions[ i ] );
	}

	CHECK_EQUAL( ReloadScript::BREAK_OLD_LINKS,
		fullPlan.actions[ fullPlan.count - 1 ] );
	CHECK_EQUAL( -1, indexOf( scriptPlan, ReloadScript::BREAK_OLD_LINKS ) );
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
 *	is one of the four shapes the sequence has. The digest dimension is the
 *	schema barrier: a mismatched digest is one of the outcomes that shapes
 *	the plan, the dry-run dimension is the probe mode, and the window
 *	dimension is the dual-version compatibility window, so all three are
 *	swept with the rest.
 */
TEST( ReloadScriptPlan_AllOutcomeCombinationsFit )
{
	for (int full = 0; full <= 1; ++full)
	{
		for (int defs = 0; defs <= 1; ++defs)
		{
			for (int udo = 0; udo <= 1; ++udo)
			{
				for (int digest = 0; digest <= 1; ++digest)
				{
					for (int dry = 0; dry <= 1; ++dry)
					{
						for (int window = 0; window <= 1; ++window)
						{
							const ReloadScript::PlanInputs inputs =
								{ full != 0, defs != 0, udo != 0,
									digest != 0, dry != 0,
									window != 0 ? 5.f : 0.f };
							const ReloadScript::ReloadPlan plan =
								ReloadScript::planReload( inputs );

							const bool defsUsable = defs && digest;

							// Only a successful full reload can defer
							// anything: a dry run never migrates and a
							// failure tail has no old links to break.
							const bool defer = full && window &&
								defsUsable && udo && !dry;

							CHECK( plan.count <= ReloadScript::ACTION_COUNT );
							CHECK_EQUAL( (defs && udo && digest),
								plan.succeeded );
							CHECK_EQUAL( (full && (dry || !defsUsable)),
								plan.dataTypesWiped );
							CHECK_EQUAL( defer, plan.oldLinksDeferred );

							// head (2 + defs load) + dry-run tail (2, swap
							// back and destroy) + success tail (8, the
							// migrate chain plus the batch-complete hook,
							// plus the old-link break on the full reload that
							// did not defer it) or failure tail (3, plus the
							// recovery on the scripts-only path).
							CHECK_EQUAL( 2 + (defsUsable ? 1 : 0) +
								(dry ? 2 :
									(plan.succeeded ?
										8 + ((full && !defer) ? 1 : 0) :
										3 + (full ? 0 : 1))),
									plan.count );
						}
					}
				}
			}
		}
	}
}


/**
 *	The digest helper: an empty expectation places no constraint, the
 *	comparison is case-insensitive, and a shorter actual never agrees.
 */
TEST( ReloadScriptPlan_DigestMatches )
{
	const char * quoted = "0123456789ABCDEF0123456789ABCDEF";

	// No expectation agrees with anything, even with nothing loaded.
	CHECK( ReloadScript::digestMatches( "", quoted ) );
	CHECK( ReloadScript::digestMatches( "", "" ) );
	CHECK( ReloadScript::digestMatches( NULL, quoted ) );

	// The same digest in either case agrees.
	CHECK( ReloadScript::digestMatches( quoted, quoted ) );
	CHECK( ReloadScript::digestMatches(
		"0123456789abcdef0123456789abcdef", quoted ) );

	// Anything else disagrees, including a shorter or longer actual.
	CHECK( !ReloadScript::digestMatches(
		"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", quoted ) );
	CHECK( !ReloadScript::digestMatches( quoted, "" ) );
	CHECK( !ReloadScript::digestMatches( quoted, NULL ) );
	CHECK( !ReloadScript::digestMatches( quoted, quoted + 1 ) );
}


/**
 *	A load whose schema digest disagrees with the request takes the failure
 *	path: the UDO load is skipped, no migrate step runs, and on a full reload
 *	the registered data types were already wiped (F8). The scripts-only case
 *	also recovers the old scripts, because it had any.
 */
TEST( ReloadScriptPlan_DigestMismatchRunsNoMigrate )
{
	const ReloadScript::PlanInputs full = { true, true, true, false };
	const ReloadScript::ReloadPlan fullPlan = ReloadScript::planReload( full );

	CHECK_EQUAL( false, fullPlan.succeeded );
	CHECK_EQUAL( true, fullPlan.dataTypesWiped );
	CHECK_EQUAL( 5, fullPlan.count );

	CHECK_EQUAL( -1, indexOf( fullPlan, ReloadScript::LOAD_USER_DATA_OBJECTS ) );
	CHECK_EQUAL( -1, indexOf( fullPlan, ReloadScript::MIGRATE_ENTITY_TYPES ) );
	CHECK_EQUAL( -1, indexOf( fullPlan, ReloadScript::MIGRATE_ENTITIES ) );
	CHECK_EQUAL( -1, indexOf( fullPlan, ReloadScript::MIGRATED_ALL ) );
	CHECK_EQUAL( -1, indexOf( fullPlan, ReloadScript::RECOVER_OLD_SCRIPTS ) );
	CHECK_EQUAL( ReloadScript::CLEANUP_AFTER_RELOAD, fullPlan.actions[ 3 ] );
	CHECK_EQUAL( ReloadScript::DESTROY_NEW_INTERPRETER,
		fullPlan.actions[ 4 ] );

	const ReloadScript::PlanInputs scriptsOnly = { false, true, true, false };
	const ReloadScript::ReloadPlan scriptsOnlyPlan =
		ReloadScript::planReload( scriptsOnly );

	CHECK_EQUAL( false, scriptsOnlyPlan.succeeded );
	CHECK_EQUAL( false, scriptsOnlyPlan.dataTypesWiped );
	CHECK_EQUAL( 6, scriptsOnlyPlan.count );

	CHECK_EQUAL( -1, indexOf( scriptsOnlyPlan,
		ReloadScript::LOAD_USER_DATA_OBJECTS ) );
	CHECK_EQUAL( -1, indexOf( scriptsOnlyPlan,
		ReloadScript::MIGRATE_ENTITIES ) );
	CHECK_EQUAL( ReloadScript::RECOVER_OLD_SCRIPTS,
		scriptsOnlyPlan.actions[ 3 ] );
}


/**
 *	A load whose digest agrees is judged successful: the UDO load runs and
 *	the migrate chain follows, exactly as the pre-barrier plan.
 */
TEST( ReloadScriptPlan_DigestMatchKeepsTheSuccessTail )
{
	const ReloadScript::PlanInputs inputs = { true, true, true, true };
	const ReloadScript::ReloadPlan plan = ReloadScript::planReload( inputs );

	CHECK( plan.succeeded );
	CHECK_EQUAL( ReloadScript::LOAD_USER_DATA_OBJECTS, plan.actions[ 2 ] );
	CHECK_EQUAL( ReloadScript::DESTROY_NEW_INTERPRETER, plan.actions[ 4 ] );
	CHECK_EQUAL( ReloadScript::MIGRATE_ENTITY_TYPES, plan.actions[ 5 ] );
}


/**
 *	A full dry run stops straight after the loads: swap back and destroy the
 *	scratch interpreter, with no cleanup (which would break the live dual-
 *	version window), no migration and no old-script recovery. It still
 *	repairs nothing, so the full reload's data-type wipe is reported.
 */
TEST( ReloadScriptPlan_DryRunStopsAfterTheLoads )
{
	const ReloadScript::PlanInputs inputs = { true, true, true, true, true };
	const ReloadScript::ReloadPlan plan = ReloadScript::planReload( inputs );

	CHECK( plan.succeeded );
	CHECK_EQUAL( true, plan.dataTypesWiped );
	CHECK_EQUAL( 5, plan.count );

	CHECK_EQUAL( ReloadScript::SWAP_IN_INTERPRETER, plan.actions[ 0 ] );
	CHECK_EQUAL( ReloadScript::LOAD_ENTITY_DEFS, plan.actions[ 1 ] );
	CHECK_EQUAL( ReloadScript::LOAD_USER_DATA_OBJECTS, plan.actions[ 2 ] );
	CHECK_EQUAL( ReloadScript::SWAP_BACK_INTERPRETER, plan.actions[ 3 ] );
	CHECK_EQUAL( ReloadScript::DESTROY_NEW_INTERPRETER, plan.actions[ 4 ] );

	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::CLEANUP_AFTER_RELOAD ) );
	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::MIGRATE_ENTITY_TYPES ) );
	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::MIGRATE_USER_DATA_OBJECT_TYPES ) );
	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::MIGRATE_MAIL_BOXES ) );
	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::MIGRATE_ENTITIES ) );
	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::MIGRATED_ALL ) );
	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::RECOVER_OLD_SCRIPTS ) );
}


/**
 *	A dry run honours the schema-digest barrier like a real reload: a rejected
 *	digest skips the UDO load and the probe still stops after the swap back,
 *	but without the scripts-only failure path's recovery step, because the
 *	old interpreter was never touched.
 */
TEST( ReloadScriptPlan_DryRunHonoursTheDigestBarrier )
{
	const ReloadScript::PlanInputs inputs = { false, true, true, false, true };
	const ReloadScript::ReloadPlan plan = ReloadScript::planReload( inputs );

	CHECK_EQUAL( false, plan.succeeded );
	CHECK_EQUAL( false, plan.dataTypesWiped );
	CHECK_EQUAL( 4, plan.count );

	CHECK_EQUAL( ReloadScript::LOAD_ENTITY_DEFS, plan.actions[ 1 ] );
	CHECK_EQUAL( ReloadScript::SWAP_BACK_INTERPRETER, plan.actions[ 2 ] );
	CHECK_EQUAL( ReloadScript::DESTROY_NEW_INTERPRETER, plan.actions[ 3 ] );

	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::LOAD_USER_DATA_OBJECTS ) );
	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::CLEANUP_AFTER_RELOAD ) );
	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::RECOVER_OLD_SCRIPTS ) );
}


/**
 *	A positive reload window takes the old() link break out of the reload's
 *	own tick: nothing in the plan breaks the links, they are deferred to a
 *	one-shot timer instead. Everything else in the tail is unchanged, so what
 *	is in flight during the window still sees the loaded type table.
 */
TEST( ReloadScriptPlan_PositiveWindowDefersTheOldLinkBreak )
{
	const ReloadScript::PlanInputs inputs = { true, true, true, true, false,
		5.f };
	const ReloadScript::ReloadPlan plan = ReloadScript::planReload( inputs );

	CHECK( plan.succeeded );
	CHECK( plan.oldLinksDeferred );
	CHECK_EQUAL( 11, plan.count );

	// Every step the deferred plan runs is the windowless plan's step in the
	// same place, just without the trailing link break.
	const ReloadScript::PlanInputs windowless = { true, true, true, true };
	const ReloadScript::ReloadPlan windowlessPlan =
		ReloadScript::planReload( windowless );

	CHECK_EQUAL( windowlessPlan.count - 1, plan.count );
	for (int i = 0; i < plan.count; ++i)
	{
		CHECK_EQUAL( windowlessPlan.actions[ i ], plan.actions[ i ] );
	}

	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::BREAK_OLD_LINKS ) );
	CHECK_EQUAL( ReloadScript::BREAK_OLD_LINKS,
		windowlessPlan.actions[ windowlessPlan.count - 1 ] );
}


/**
 *	Only a successful full reload can defer the link break: a scripts-only
 *	reload has no new types to bridge, and the failure tails never migrated
 *	anything, so both still break the links in their own tick.
 */
TEST( ReloadScriptPlan_WindowNeverDefersWithoutMigration )
{
	const ReloadScript::PlanInputs scriptsOnly = { false, true, true, true,
		false, 5.f };
	const ReloadScript::PlanInputs failedUdo = { true, true, false, true,
		false, 5.f };
	const ReloadScript::PlanInputs rejectedDigest = { true, true, true,
		false, false, 5.f };

	const ReloadScript::ReloadPlan scriptsOnlyPlan =
		ReloadScript::planReload( scriptsOnly );
	const ReloadScript::ReloadPlan failedUdoPlan =
		ReloadScript::planReload( failedUdo );
	const ReloadScript::ReloadPlan rejectedDigestPlan =
		ReloadScript::planReload( rejectedDigest );

	CHECK_EQUAL( false, scriptsOnlyPlan.oldLinksDeferred );
	CHECK_EQUAL( false, failedUdoPlan.oldLinksDeferred );
	CHECK_EQUAL( false, rejectedDigestPlan.oldLinksDeferred );

	CHECK_EQUAL( -1,
		indexOf( scriptsOnlyPlan, ReloadScript::BREAK_OLD_LINKS ) );
	CHECK_EQUAL( -1,
		indexOf( failedUdoPlan, ReloadScript::BREAK_OLD_LINKS ) );
	CHECK_EQUAL( -1,
		indexOf( rejectedDigestPlan, ReloadScript::BREAK_OLD_LINKS ) );
}


/**
 *	A dry run never defers the link break either: the probe migrates nothing,
 *	and the window is about in-flight dispatch during and after a real
 *	reload, so a probe reload leaves no links up at all.
 */
TEST( ReloadScriptPlan_DryRunNeverDefersTheOldLinkBreak )
{
	const ReloadScript::PlanInputs inputs = { true, true, true, true, true,
		5.f };
	const ReloadScript::ReloadPlan plan = ReloadScript::planReload( inputs );

	CHECK( plan.succeeded );
	CHECK_EQUAL( false, plan.oldLinksDeferred );
	CHECK_EQUAL( 5, plan.count );
	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::BREAK_OLD_LINKS ) );
	CHECK_EQUAL( -1, indexOf( plan, ReloadScript::CLEANUP_AFTER_RELOAD ) );
}


// test_reload_script_plan.cpp

BW_END_NAMESPACE
