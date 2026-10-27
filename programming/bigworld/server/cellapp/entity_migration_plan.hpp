// entity_migration_plan.hpp
//
// The per-entity Entity::migrate outcome, and the collect-decide policy the
// CellApp::reloadScript migrate loop is to apply to it, as data.
//
// CellApp::reloadScript migrates every entity with MF_VERIFY(iter->second->
// migrate()) (cellapp.cpp:2454), so the first failed entity aborts the
// process and leaves everything migrated before it half-migrated. The
// hot-reload productionization design (docs/upgrade-plan/
// hot-reload-productionization.md, section 4 "Failure rollback") replaces
// that with collect-decide: record each entity's failure class, keep going
// or stop by a policy bit, and roll the migrated entities back. The default
// decision is "stop immediately + roll back", preserving the acceptance
// criterion "no half-migrated state on failure".
//
// Entity::migrate (entity.cpp) fails three different ways, only two of which
// its bool return can tell apart, and one of which it reports as success:
//
//   * GUARD_OBSOLETE - the replacement type is absent or its type id
//     mismatched; migrate() returns before touching the entity;
//   * CLASS_SWAP_REJECTED - Python rejected the __class__ swap; migrate()
//     restores pEntityType_ and returns false;
//   * SCRIPT_EXCEPTION - the onMigrate hook raised; Script::call's return
//     value is discarded (F2), so the entity still reports success.
//
// The rollback half of the design cannot be written from the state
// Entity::migrate leaves, and that fact is recorded here rather than guessed
// around: pNewType->old(pOldType) renames the old description's cell methods
// with an old_ prefix (entity_description.cpp:892; the rename and its
// stacking are pinned by entitydef_test EntityDescription_supersede*), and
// EntityType::old (entity_type.cpp:178) returns without renaming once
// pOldSelf_ is already set. A failure observed on the second entity of a
// type therefore did not cause the rename it would have to undo, so the
// inverse cannot attribute its REVERT_SUPERSEDE_RENAME step; one more step
// of the inverse, CLEAR_OLD_TYPE_LINK, has no setter at all.

#ifndef ENTITY_MIGRATION_PLAN_HPP
#define ENTITY_MIGRATION_PLAN_HPP

BW_BEGIN_NAMESPACE

namespace EntityMigration
{

/// One class of Entity::migrate failure. The class, not the bool return, is
/// what the collect-decide loop decides on.
enum FailureClass
{
	NONE,					///< the migration succeeded
	GUARD_OBSOLETE,			///< F1: replacement type absent or type id
							///< mismatch; nothing was touched
	CLASS_SWAP_REJECTED,	///< F1: Python rejected the __class__ swap;
							///< pEntityType_ was restored
	SCRIPT_EXCEPTION,		///< F2: the onMigrate hook raised; migrate()
							///< still reports success
	FAILURE_CLASS_COUNT
};

/// The reload request's per-entity policy bit.
enum Policy
{
	STOP_ON_FAILURE,		///< stop the rollout at the first failure
	CONTINUE_ON_FAILURE		///< record the failure and keep migrating
};

/// What the migrate loop does with the next entity.
enum Decision
{
	MIGRATE_NEXT,
	STOP_AND_ROLLBACK
};

/// One step of the inverse of Entity::migrate, which a rollout rollback runs
/// on every entity it migrates back.
enum RollbackStep
{
	RESTORE_TYPE_POINTER,	///< pEntityType_ = pOldType
	RESTORE_SCRIPT_CLASS,	///< __class__ = pOldType->pPyType()
	REVERT_SUPERSEDE_RENAME,///< undo EntityType::old's supersede()
	CLEAR_OLD_TYPE_LINK,	///< pNewType->pOldSelf_ = NULL
	ROLLBACK_STEP_COUNT
};

/// The outcome facts Entity::migrate observes. guardBlocked: the type-id
/// guard rejected the replacement. classSwapRejected: PyObject_SetAttrString
/// refused the new class. onMigrateRaised: the onMigrate hook call failed
/// (Script::call returned false; an absent hook returns true).
inline FailureClass classify( bool guardBlocked, bool classSwapRejected,
	bool onMigrateRaised )
{
	if (guardBlocked)
	{
		return GUARD_OBSOLETE;
	}
	if (classSwapRejected)
	{
		return CLASS_SWAP_REJECTED;
	}
	if (onMigrateRaised)
	{
		return SCRIPT_EXCEPTION;
	}
	return NONE;
}

/// Whether this entity's migration renamed the old description's cell
/// methods: migrate() calls pNewType->old(pOldType) only when the replacement
/// is a distinct type, so a script-only reload renames nothing.
inline bool renamedOldDescription( FailureClass failure, bool distinctType )
{
	return failure != GUARD_OBSOLETE && distinctType;
}

/// Whether a rollback step can be expressed in today's engine. The two
/// pointer/class restores are; the supersede rename has no inverse and stacks
/// prefixes, and the old-type link is a private member without a setter.
inline bool revertible( RollbackStep step )
{
	return step == RESTORE_TYPE_POINTER || step == RESTORE_SCRIPT_CLASS;
}

/// Whether the whole rollback inverse is expressible. It is not, which is
/// why the migrate loop still MF_VERIFYs instead of stopping and rolling
/// back.
inline bool rollbackInverseComplete()
{
	return revertible( RESTORE_TYPE_POINTER ) &&
		revertible( RESTORE_SCRIPT_CLASS ) &&
		revertible( REVERT_SUPERSEDE_RENAME ) &&
		revertible( CLEAR_OLD_TYPE_LINK );
}

/// Collect-decide state for the per-entity migrate loop.
struct LoopState
{
	Policy policy;
	bool stopped;
	int migrated;
	int failed;
	FailureClass firstFailure;///< NONE until a failure is observed
	bool rollbackNeeded;	///< a stopped rollout migrated something first
};

inline LoopState initState( Policy policy )
{
	LoopState state;
	state.policy = policy;
	state.stopped = false;
	state.migrated = 0;
	state.failed = 0;
	state.firstFailure = NONE;
	state.rollbackNeeded = false;
	return state;
}

/// Fold one entity's migration outcome into the loop state, returning what
/// the loop does with the next entity. Outcomes after the stop are refused:
/// a stopped loop rolls back, it does not migrate.
inline Decision advance( LoopState & state, FailureClass failure )
{
	if (state.stopped)
	{
		return STOP_AND_ROLLBACK;
	}

	if (failure == NONE)
	{
		++state.migrated;
		return MIGRATE_NEXT;
	}

	++state.failed;
	if (state.firstFailure == NONE)
	{
		state.firstFailure = failure;
	}

	if (state.policy == CONTINUE_ON_FAILURE)
	{
		return MIGRATE_NEXT;
	}

	state.stopped = true;
	state.rollbackNeeded = state.migrated > 0;
	return STOP_AND_ROLLBACK;
}

} // namespace EntityMigration

BW_END_NAMESPACE

#endif // ENTITY_MIGRATION_PLAN_HPP
