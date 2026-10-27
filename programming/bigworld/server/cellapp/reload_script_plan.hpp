// reload_script_plan.hpp
//
// The CellApp::reloadScript action sequence, as data.
//
// CellApp::reloadScript (cellapp.cpp:2322) hard-codes an ordered sequence of
// interpreter/entity-def/UDO/mailbox/entity steps whose shape depends on three
// facts that are only known while it runs: whether this is a full reload, and
// whether the entity-def load and the UDO load succeeded. The branch policy and
// the ordering are the part that has historically gone wrong (the hot-reload
// productionization design, docs/upgrade-plan/hot-reload-productionization.md,
// records the ordering facts as failure modes F1/F3/F8), so the sequence lives
// here as a pure function the unit tests can pin down:
//
//   * on success the scratch interpreter is destroyed BEFORE the migrate chain
//     runs (cellapp.cpp:2351 precedes :2353-2362);
//   * on failure the scratch interpreter is destroyed AFTER cleanup
//     (cellapp.cpp:2383 follows :2382);
//   * the old-scripts recovery (EntityType::reloadScript(true),
//     cellapp.cpp:2379) runs on every failure path except a failed full
//     reload - a full reload has no old scripts to fall back to;
//   * a failed full reload has wiped the registered data types before it can
//     fail: EntityType::init(true) calls DataType::clearStaticsForReload()
//     (entity_type.cpp:262) before parsing, and the failure path never
//     reloads them (failure mode F8). planReload() reports that as
//     dataTypesWiped so tests and future dry-run work can see it;
//   * the batch-migration-complete hook fires after every entity has migrated
//     and before cleanup (failure mode F6: Entity::migratedAll had no caller
//     anywhere in the tree).
//
// The head of the sequence (swap in the scratch interpreter, load entity defs,
// load UDOs) cannot be driven from a plan that is only complete once its own
// outcomes are known, so CellApp::reloadScript executes the head inline and
// drives everything from the swap-back on through planReload()'s output. The
// head entries are still part of the sequence data: the tests assert the whole
// ordering, including the head, so the data and the code cannot drift apart
// silently.

#ifndef RELOAD_SCRIPT_PLAN_HPP
#define RELOAD_SCRIPT_PLAN_HPP

BW_BEGIN_NAMESPACE

namespace ReloadScript
{

/// One step of CellApp::reloadScript, in execution order.
enum Action
{
	SWAP_IN_INTERPRETER,		///< Script::createInterpreter + swapInterpreter
	LOAD_ENTITY_DEFS,			///< EntityType::init(true) or ::reloadScript()
	LOAD_USER_DATA_OBJECTS,		///< UserDataObjectType::load
	SWAP_BACK_INTERPRETER,		///< restore the original interpreter
	DESTROY_NEW_INTERPRETER,	///< Script::destroyInterpreter on the scratch
	MIGRATE_ENTITY_TYPES,		///< EntityType::migrate(isFullReload)
	MIGRATE_USER_DATA_OBJECT_TYPES,
	MIGRATE_MAIL_BOXES,			///< ServerEntityMailBox::migrateMailBoxes
	MIGRATE_ENTITIES,			///< per-entity Entity::migrate, MF_VERIFY'd
	MIGRATED_ALL,				///< per-entity Entity::migratedAll, after the
								///< migration loop (failure mode F6: the
								///< onMigratedAll hook was declared but never
								///< called by anything in the tree)
	CLEANUP_AFTER_RELOAD,		///< EntityType::cleanupAfterReload(isFullReload)
	RECOVER_OLD_SCRIPTS,		///< EntityType::reloadScript(true), failure only
	ACTION_COUNT
};

/// The outcomes the plan's shape depends on.
struct PlanInputs
{
	bool isFullReload;
	bool defsLoaded;
	bool udoLoaded;
};

/// The ordered action sequence for one reload attempt.
struct ReloadPlan
{
	Action actions[ ACTION_COUNT ];
	int count;

	/// Whether the attempt as a whole succeeds (both loads went through).
	bool succeeded;

	/// Failure mode F8: a full reload clears the registered data types before
	/// it can fail, and nothing on the failure path reloads them.
	bool dataTypesWiped;
};

/**
 *	Builds the action sequence CellApp::reloadScript performs for the given
 *	outcomes. udoLoaded is only consulted when defsLoaded is true - the UDO
 *	load is never attempted after a failed defs load.
 */
inline ReloadPlan planReload( const PlanInputs & inputs )
{
	ReloadPlan plan;
	plan.count = 0;
	plan.succeeded = inputs.defsLoaded && inputs.udoLoaded;
	plan.dataTypesWiped = inputs.isFullReload && !inputs.defsLoaded;

	// Head: the scratch interpreter is swapped in and the loads run inside it.
	plan.actions[ plan.count++ ] = SWAP_IN_INTERPRETER;
	plan.actions[ plan.count++ ] = LOAD_ENTITY_DEFS;
	if (inputs.defsLoaded)
	{
		plan.actions[ plan.count++ ] = LOAD_USER_DATA_OBJECTS;
	}

	// The UDO load is skipped after a failed defs load, so its result is not
	// consulted on that path.
	if (inputs.defsLoaded && inputs.udoLoaded)
	{
		// Success tail: destroy the scratch interpreter before the migrate
		// chain, then clean up (cellapp.cpp:2350-2364).
		plan.actions[ plan.count++ ] = SWAP_BACK_INTERPRETER;
		plan.actions[ plan.count++ ] = DESTROY_NEW_INTERPRETER;
		plan.actions[ plan.count++ ] = MIGRATE_ENTITY_TYPES;
		plan.actions[ plan.count++ ] = MIGRATE_USER_DATA_OBJECT_TYPES;
		plan.actions[ plan.count++ ] = MIGRATE_MAIL_BOXES;
		plan.actions[ plan.count++ ] = MIGRATE_ENTITIES;
		plan.actions[ plan.count++ ] = MIGRATED_ALL;
		plan.actions[ plan.count++ ] = CLEANUP_AFTER_RELOAD;
	}
	else
	{
		// Failure tail: restore, recover the old scripts (unless this was a
		// full reload), clean up, and only then destroy the scratch
		// interpreter (cellapp.cpp:2371-2383).
		plan.actions[ plan.count++ ] = SWAP_BACK_INTERPRETER;
		if (!inputs.isFullReload)
		{
			plan.actions[ plan.count++ ] = RECOVER_OLD_SCRIPTS;
		}
		plan.actions[ plan.count++ ] = CLEANUP_AFTER_RELOAD;
		plan.actions[ plan.count++ ] = DESTROY_NEW_INTERPRETER;
	}

	return plan;
}

} // namespace ReloadScript

BW_END_NAMESPACE

#endif // RELOAD_SCRIPT_PLAN_HPP
