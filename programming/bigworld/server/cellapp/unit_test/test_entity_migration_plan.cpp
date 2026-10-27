// test_entity_migration_plan.cpp
//
// The Entity::migrate outcome classes and the collect-decide policy the
// CellApp::reloadScript migrate loop is to apply to them, as testable data
// (entity_migration_plan.hpp; failure modes F1 and F2 in docs/upgrade-plan/
// hot-reload-productionization.md). The loop itself is only reachable inside
// a live CellApp - it MF_VERIFYs each migrate() at cellapp.cpp:2454 - so the
// branch policy and the rollback boundary are pinned here:
//
//   * a guard rejection touches nothing, a rejected class swap restores the
//     type pointer, and a raised onMigrate hook still reports success: the
//     bool return alone cannot tell the three apart;
//   * STOP_ON_FAILURE stops the rollout at the first failure and needs a
//     rollback only when something migrated before it;
//   * CONTINUE_ON_FAILURE records and keeps migrating;
//   * the rollback inverse is only half expressible, which is why the loop
//     still aborts instead of stopping and rolling back.

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
 *	A failure before anything migrated still stops the rollout: there is no
 *	partial state, so there is nothing to roll back.
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
	CHECK_EQUAL( 1, state.failed );
	CHECK_EQUAL( EntityMigration::GUARD_OBSOLETE, state.firstFailure );
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
 *	The rollback inverse is half expressible. The two pointer and class
 *	restores are plain assignments; the supersede rename has no inverse and
 *	its old_ prefix stacks, and the old-type link is a private member without
 *	a setter. So rollbackInverseComplete() is false and the loop keeps
 *	aborting instead of stopping and rolling back.
 */
TEST( EntityMigrationPlan_RollbackInverseIncomplete )
{
	CHECK( EntityMigration::revertible( EntityMigration::RESTORE_TYPE_POINTER ) );
	CHECK( EntityMigration::revertible( EntityMigration::RESTORE_SCRIPT_CLASS ) );
	CHECK( !EntityMigration::revertible(
		EntityMigration::REVERT_SUPERSEDE_RENAME ) );
	CHECK( !EntityMigration::revertible( EntityMigration::CLEAR_OLD_TYPE_LINK ) );
	CHECK_EQUAL( false, EntityMigration::rollbackInverseComplete() );
}


// test_entity_migration_plan.cpp

BW_END_NAMESPACE
