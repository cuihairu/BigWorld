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
// The rollback half of the design cannot be run yet, and the reason is
// recorded here rather than guessed around. All four inverse steps have
// primitives (rollbackInverseComplete()): the two pointer/class restores
// are plain assignments, the old-type link is cleared through the public
// setter old(NULL) (EntityType::old, entity_type.hpp:87 - the early return
// only fires when both pOldSelf_ and the argument are non-NULL, and
// cleanupAfterReload already uses old(NULL) to break the link), and the
// supersede rename's inverse is EntityDescription::unsupersede, which
// strips exactly one leading old_ prefix per call, pairing with
// supersede()'s one-per-call stacking (entitydef_test
// EntityDescription_unsupersede*; batch 53 - batch 51 wrongly recorded the
// rename as having no inverse and the link as having no setter). What is
// still missing is attribution: the rename happens once per (new, old)
// type pair - EntityType::old (entity_type.cpp:178) returns without
// renaming once pOldSelf_ is already set - so only the first migrated
// entity of a pair renames, and a failure observed on a later entity of
// the same pair did not cause the rename it would have to undo. The loop
// would have to track, per type pair, whether the strip still needs doing
// and by which entity; LoopState does not model that, which is what keeps
// the migrate loop MF_VERIFYing instead of stopping and rolling back
// (rollbackAttributionComplete() is false).

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
	CLEAR_OLD_TYPE_LINK,	///< pNewType->old(NULL), clears pOldSelf_
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

/// Whether a rollback step's primitive exists in today's engine. The two
/// pointer/class restores are plain assignments, the old-type link is
/// cleared through the public setter old(NULL), and the supersede rename's
/// inverse is EntityDescription::unsupersede (batch 53): it strips exactly
/// one leading old_ prefix per call, pairing with supersede()'s one-per-
/// call stacking.
inline bool revertible( RollbackStep step )
{
	return step == RESTORE_TYPE_POINTER || step == RESTORE_SCRIPT_CLASS ||
		step == CLEAR_OLD_TYPE_LINK || step == REVERT_SUPERSEDE_RENAME;
}

/// Whether the whole rollback inverse is expressible. Since batch 53 all
/// four primitives exist, so this is true; what keeps the migrate loop
/// MF_VERIFYing is rollbackAttributionComplete() below.
inline bool rollbackInverseComplete()
{
	return revertible( RESTORE_TYPE_POINTER ) &&
		revertible( RESTORE_SCRIPT_CLASS ) &&
		revertible( REVERT_SUPERSEDE_RENAME ) &&
		revertible( CLEAR_OLD_TYPE_LINK );
}

/// Whether the rollback can decide who runs each step. It cannot: the
/// supersede rename happens once per (new, old) type pair, so only the
/// first migrated entity of a pair renames and a failure observed on a
/// later entity of the same pair did not cause the rename it would have to
/// undo. The loop would have to track, per type pair, whether the strip
/// still needs doing and by which entity; LoopState does not model that
/// yet, which is why the migrate loop still MF_VERIFYs instead of stopping
/// and rolling back.
inline bool rollbackAttributionComplete()
{
	return false;
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
