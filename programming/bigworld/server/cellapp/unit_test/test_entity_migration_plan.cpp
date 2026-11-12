// test_entity_migration_plan.cpp
//
// The Entity::migrate outcome classes and the collect-decide policy the
// CellApp::reloadScript migrate loop applies to them, as testable data
// (entity_migration_plan.hpp; failure modes F1 and F2 in docs/upgrade-plan/
// hot-reload-productionization.md). The loop-with-rollback itself is pinned
// end to end on fake entities in test_migration_rollout.cpp; what is pinned
// here is the data layer:
//
//   * a guard rejection touches nothing, a rejected class swap restores the
//     type pointer, and a raised onMigrate hook still reports success: the
//     bool return alone cannot tell the three apart;
//   * STOP_ON_FAILURE stops the rollout at the first failure and needs a
//     rollback only when something was touched (the touched counter, not the
//     migrated counter - a half-migrated first failure is in the rollback
//     set, CR-P2-20);
//   * CONTINUE_ON_FAILURE records and keeps migrating.

#include "pch.hpp"

#include "../entity_migration_plan.hpp"


BW_BEGIN_NAMESPACE


/**
 *	classify()'s precedence is migrate()'s exit order: the type-id guard
 *	rejects before anything is touched, the class swap before the onMigrate
 *	hook runs. Every combination of the three outcome facts is covered.
 */
TEST( EntityMigrationPlan_ClassifyPrecedence )
{
	for (int guard = 0; guard <= 1; ++guard)
	{
		for (int swap = 0; swap <= 1; ++swap)
		{
			for (int hook = 0; hook <= 1; ++hook)
			{
				const EntityMigration::FailureClass failure =
					EntityMigration::classify( guard != 0, swap != 0,
						hook != 0 );

				if (guard)
				{
					CHECK_EQUAL( EntityMigration::GUARD_OBSOLETE, failure );
				}
				else if (swap)
				{
					CHECK_EQUAL( EntityMigration::CLASS_SWAP_REJECTED,
						failure );
				}
				else if (hook)
				{
					CHECK_EQUAL( EntityMigration::SCRIPT_EXCEPTION,
						failure );
				}
				else
				{
					CHECK_EQUAL( EntityMigration::NONE, failure );
				}
			}
		}
	}
}


/**
 *	STOP_ON_FAILURE stops the rollout at the first failure, and the rollback
 *	is needed because two entities migrated before it.
 */
TEST( EntityMigrationPlan_StopOnFailure )
{
	EntityMigration::LoopState state =
		EntityMigration::initState( EntityMigration::STOP_ON_FAILURE );

	CHECK_EQUAL( EntityMigration::MIGRATE_NEXT,
		EntityMigration::advance( state, EntityMigration::NONE ) );
	CHECK_EQUAL( EntityMigration::MIGRATE_NEXT,
		EntityMigration::advance( state, EntityMigration::NONE ) );
	CHECK_EQUAL( 2, state.migrated );

	CHECK_EQUAL( EntityMigration::STOP_AND_ROLLBACK,
		EntityMigration::advance( state,
			EntityMigration::CLASS_SWAP_REJECTED ) );
	CHECK( state.stopped );
	CHECK( state.rollbackNeeded );
	CHECK_EQUAL( 2, state.migrated );
	CHECK_EQUAL( 1, state.failed );
	CHECK_EQUAL( EntityMigration::CLASS_SWAP_REJECTED, state.firstFailure );
}


/**
 *	A guard rejection still stops the rollout: it touched nothing (touched 0),
 *	so there is nothing to roll back.
 */
TEST( EntityMigrationPlan_StopWithNothingMigrated )
{
	EntityMigration::LoopState state =
		EntityMigration::initState( EntityMigration::STOP_ON_FAILURE );

	CHECK_EQUAL( EntityMigration::STOP_AND_ROLLBACK,
		EntityMigration::advance( state, EntityMigration::GUARD_OBSOLETE ) );
	CHECK( state.stopped );
	CHECK( !state.rollbackNeeded );
	CHECK_EQUAL( 0, state.migrated );
	CHECK_EQUAL( 0, state.touched );
	CHECK_EQUAL( 1, state.failed );
	CHECK_EQUAL( EntityMigration::GUARD_OBSOLETE, state.firstFailure );
}


/**
 *	A touching failure on the very first entity still leaves the rollback set
 *	non-empty: the touched counter, not the migrated counter, decides
 *	rollbackNeeded (CR-P2-20 - the half-migrated first entity is in the set).
 */
TEST( EntityMigrationPlan_FirstFailureTouchesRollsBack )
{
	EntityMigration::LoopState state =
		EntityMigration::initState( EntityMigration::STOP_ON_FAILURE );

	CHECK_EQUAL( EntityMigration::STOP_AND_ROLLBACK,
		EntityMigration::advance( state, EntityMigration::SCRIPT_EXCEPTION ) );
	CHECK( state.stopped );
	CHECK( state.rollbackNeeded );
	CHECK_EQUAL( 0, state.migrated );
	CHECK_EQUAL( 1, state.touched );
	CHECK_EQUAL( 1, state.failed );
}


/**
 *	CONTINUE_ON_FAILURE records every failure and keeps migrating; the first
 *	failure is the one reported.
 */
TEST( EntityMigrationPlan_ContinueOnFailure )
{
	EntityMigration::LoopState state =
		EntityMigration::initState( EntityMigration::CONTINUE_ON_FAILURE );

	CHECK_EQUAL( EntityMigration::MIGRATE_NEXT,
		EntityMigration::advance( state,
			EntityMigration::CLASS_SWAP_REJECTED ) );
	CHECK_EQUAL( EntityMigration::MIGRATE_NEXT,
		EntityMigration::advance( state, EntityMigration::NONE ) );
	CHECK_EQUAL( EntityMigration::MIGRATE_NEXT,
		EntityMigration::advance( state, EntityMigration::SCRIPT_EXCEPTION ) );

	CHECK( !state.stopped );
	CHECK_EQUAL( 1, state.migrated );
	CHECK_EQUAL( 2, state.failed );
	CHECK_EQUAL( EntityMigration::CLASS_SWAP_REJECTED, state.firstFailure );
}


/**
 *	A stopped loop refuses further migrations: it rolls back. Nothing is
 *	folded into the state after the stop.
 */
TEST( EntityMigrationPlan_StopIsSticky )
{
	EntityMigration::LoopState state =
		EntityMigration::initState( EntityMigration::STOP_ON_FAILURE );

	EntityMigration::advance( state, EntityMigration::NONE );
	EntityMigration::advance( state, EntityMigration::GUARD_OBSOLETE );

	CHECK_EQUAL( EntityMigration::STOP_AND_ROLLBACK,
		EntityMigration::advance( state, EntityMigration::NONE ) );
	CHECK_EQUAL( 1, state.migrated );
	CHECK_EQUAL( 1, state.failed );
}


/**
 *	Only a distinct replacement type renames the old description's cell
 *	methods: a script-only reload renames nothing, and the guard rejects
 *	before the old-type link is taken.
 */
TEST( EntityMigrationPlan_RenamedOldDescription )
{
	CHECK( EntityMigration::renamedOldDescription(
		EntityMigration::CLASS_SWAP_REJECTED, true ) );
	CHECK( EntityMigration::renamedOldDescription(
		EntityMigration::NONE, true ) );
	CHECK( !EntityMigration::renamedOldDescription(
		EntityMigration::CLASS_SWAP_REJECTED, false ) );
	CHECK( !EntityMigration::renamedOldDescription(
		EntityMigration::GUARD_OBSOLETE, true ) );
}


/**
 *	All four rollback primitives exist since batch 53: the two pointer and
 *	class restores are plain assignments, the old-type link is cleared
 *	through the public setter old(NULL) (EntityType::old early-returns only
 *	when pOldSelf_ and the argument are both non-NULL, so a NULL argument
 *	always clears), and the supersede rename's inverse is
 *	EntityDescription::unsupersede, one stacked prefix per call. Since batch
 *	54 the attribution decision is modeled too (planRollback), so both
 *	completeness facts hold; the loop now runs them through
 *	EntityMigration::rollout (pinned in test_migration_rollout.cpp).
 */
TEST( EntityMigrationPlan_RollbackDecisionModeled )
{
	CHECK( EntityMigration::revertible( EntityMigration::RESTORE_TYPE_POINTER ) );
	CHECK( EntityMigration::revertible( EntityMigration::RESTORE_SCRIPT_CLASS ) );
	CHECK( EntityMigration::revertible(
		EntityMigration::REVERT_SUPERSEDE_RENAME ) );
	CHECK( EntityMigration::revertible( EntityMigration::CLEAR_OLD_TYPE_LINK ) );
	CHECK_EQUAL( true, EntityMigration::rollbackInverseComplete() );
	CHECK_EQUAL( true, EntityMigration::rollbackAttributionComplete() );
}


/**
 *	A guard-rejected entity never reaches the old() call, so it is not in
 *	the rollback set; every other outcome touched the entity (or at least
 *	the pair) and is rolled back.
 */
TEST( EntityMigrationPlan_TouchedEntity )
{
	CHECK( !EntityMigration::touchedEntity( EntityMigration::GUARD_OBSOLETE ) );
	CHECK( EntityMigration::touchedEntity( EntityMigration::NONE ) );
	CHECK( EntityMigration::touchedEntity(
		EntityMigration::CLASS_SWAP_REJECTED ) );
	CHECK( EntityMigration::touchedEntity(
		EntityMigration::SCRIPT_EXCEPTION ) );
}


/**
 *	planRollback() is the pair-pass attribution: the two type-level steps run
 *	once per (new, old) type pair, on the first touched entity of the pair in
 *	rollback order, so a later entity of the same pair inherits nothing and a
 *	failure observed on it does not have to undo a rename it did not cause.
 */
TEST( EntityMigrationPlan_PlanRollbackPairPass )
{
	const EntityMigration::RollbackPlan first = EntityMigration::planRollback(
		EntityMigration::NONE, /*pairRenamed=*/true,
		/*alreadyStripped=*/false, /*alreadyCleared=*/false );
	CHECK( first.restoreEntity );
	CHECK( first.revertSupersede );
	CHECK( first.clearOldLink );

	// A later entity of the same renamed pair runs no pair-level step.
	const EntityMigration::RollbackPlan later = EntityMigration::planRollback(
		EntityMigration::NONE, /*pairRenamed=*/true,
		/*alreadyStripped=*/true, /*alreadyCleared=*/true );
	CHECK( later.restoreEntity );
	CHECK( !later.revertSupersede );
	CHECK( !later.clearOldLink );

	// A pair that was never renamed gets no strip and no link break, no
	// matter which entity of it is rolled back.
	const EntityMigration::RollbackPlan unrenamed =
		EntityMigration::planRollback( EntityMigration::NONE,
			/*pairRenamed=*/false, /*alreadyStripped=*/false,
			/*alreadyCleared=*/false );
	CHECK( unrenamed.restoreEntity );
	CHECK( !unrenamed.revertSupersede );
	CHECK( !unrenamed.clearOldLink );
}


/**
 *	An entity whose class swap was rejected restored pEntityType_ itself, so
 *	the loop restoring it again is a no-op; but it still touched the pair
 *	and can be the entity that runs the strip. A raised onMigrate hook
 *	reports success yet fully migrated, so it is restored like any success.
 */
TEST( EntityMigrationPlan_PlanRollbackPerOutcome )
{
	const EntityMigration::RollbackPlan rejected =
		EntityMigration::planRollback(
			EntityMigration::CLASS_SWAP_REJECTED, /*pairRenamed=*/true,
			/*alreadyStripped=*/false, /*alreadyCleared=*/false );
	CHECK( !rejected.restoreEntity );
	CHECK( rejected.revertSupersede );
	CHECK( rejected.clearOldLink );

	const EntityMigration::RollbackPlan hookRaised =
		EntityMigration::planRollback( EntityMigration::SCRIPT_EXCEPTION,
			/*pairRenamed=*/true, /*alreadyStripped=*/false,
			/*alreadyCleared=*/false );
	CHECK( hookRaised.restoreEntity );
	CHECK( hookRaised.revertSupersede );
	CHECK( hookRaised.clearOldLink );
}


// test_entity_migration_plan.cpp

BW_END_NAMESPACE
