// test_migration_rollout.cpp
//
// The collect-decide migrate pass with its rollback half, driven end to end
// on fake entities (EntityMigration::rollout in entity_migration_plan.hpp).
// The swap/class semantics themselves are pinned in
// test_entity_type_migration.cpp; what is pinned here is the loop policy:
//
//   * a stopped pass stops migrating and rolls the touched entities back in
//     reverse migration order;
//   * the two type-level steps run once per (new, old) pair, on the first
//     record of the pair in rollback order;
//   * a first-entity failure still rolls itself back (the touched counter,
//     not the migrated counter, decides);
//   * a guard rejection touches nothing and rolls nothing back;
//   * CONTINUE_ON_FAILURE completes without a rollback;
//   * entities destroyed mid-pass are skipped by both loops.
//
// The fake migrate op models Entity::migrate's state transitions: a full
// migration (or a raised hook) leaves the entity on the replacement type
// with the pair linked, a rejected class swap restores the type pointer but
// leaves the pair linked, and a guard rejection touches nothing.

#include "pch.hpp"

#include "../entity_migration_plan.hpp"

#include "cstdmf/bw_map.hpp"
#include "cstdmf/bw_string.hpp"

#include <utility>


BW_BEGIN_NAMESPACE

namespace
{

// A fake type: old types and their replacements are told apart by pointer,
// the label only names them in the assertions.
struct FakeType
{
	explicit FakeType( const char * label ): label_( label ) {}

	const char * label_;
};

typedef FakeType * FakeTypePtr;

// A fake entity: the two state tracks mirror Entity's pEntityType_ and its
// script class; the flag pair records the pair-level steps the rollback ran.
struct FakeEntity
{
	FakeEntity( const char * label, FakeTypePtr pType ):
		label_( label ),
		pCurrentType_( pType ),
		pScriptClass_( pType ),
		destroyed_( false ),
		linkBroken_( false ),
		descriptionStripped_( false ) {}

	const char * label_;
	FakeTypePtr pCurrentType_;
	FakeTypePtr pScriptClass_;
	bool destroyed_;
	bool linkBroken_;			///< the fake pNewType->old( NULL ) ran
	bool descriptionStripped_;	///< the fake unsupersede ran
};

// The scripted outcomes the fake migrate op reports, in population order.
// SCRIPT_NONE records a scripted pass with no outcome for that entity.
enum ScriptedOutcome
{
	SCRIPT_NONE,
	SCRIPT_SUCCESS,
	SCRIPT_HOOK_RAISED,
	SCRIPT_SWAP_REJECTED,
	SCRIPT_GUARD_REJECTED
};

const char * outcomeLabel( ScriptedOutcome outcome )
{
	switch (outcome)
	{
		case SCRIPT_SUCCESS:
			return "success";
		case SCRIPT_HOOK_RAISED:
			return "hook";
		case SCRIPT_SWAP_REJECTED:
			return "swap";
		case SCRIPT_GUARD_REJECTED:
			return "guard";
		case SCRIPT_NONE:
		default:
			return "none";
	}
}

/**
 * The fake migrate op: pops the scripted outcome, applies the matching
 * state transition, and journals "migrate:<entity>:<outcome>".
 */
class FakeMigrateOp
{
public:
	typedef FakeTypePtr TypePtr;

	FakeMigrateOp( FakeTypePtr pReplacement,
			const BW::vector< ScriptedOutcome > & script,
			BW::vector< BW::string > & journal,
			bool linkPair = true ):
		pReplacement_( pReplacement ),
		script_( script ),
		next_( 0 ),
		journal_( journal ),
		linkPair_( linkPair ) {}

	/// Registers a per-old-type replacement, so a pass over entities of
	/// distinct old types keeps their (new, old) pairs distinct.
	void mapReplacement( FakeTypePtr pOld, FakeTypePtr pNew )
	{
		mappings_.push_back( std::make_pair( pOld, pNew ) );
	}

	EntityMigration::FailureClass operator()( FakeEntity * pEntity,
		FakeTypePtr & pOldType, FakeTypePtr & pNewType )
	{
		const ScriptedOutcome outcome =
			(next_ < script_.size()) ? script_[ next_++ ] : SCRIPT_NONE;

		pOldType = pEntity->pCurrentType_;

		switch (outcome)
		{
			case SCRIPT_SUCCESS:
			case SCRIPT_HOOK_RAISED:
				// Full migration: entity moves to the replacement and the
				// pair is linked (unless the reload is script-only).
				if (linkPair_)
				{
					pEntity->pCurrentType_ = this->replacementFor( pOldType );
					pEntity->pScriptClass_ = pEntity->pCurrentType_;
					pNewType = pEntity->pCurrentType_;
				}
				break;

			case SCRIPT_SWAP_REJECTED:
				// The pair was linked before the swap was refused, so the
				// pair owner is reported but the entity restored itself.
				if (linkPair_)
				{
					pNewType = this->replacementFor( pOldType );
				}
				break;

			case SCRIPT_GUARD_REJECTED:
			case SCRIPT_NONE:
			default:
				break;
		}

		journal_.push_back( BW::string( "migrate:" ) + pEntity->label_ +
			":" + outcomeLabel( outcome ) );

		return EntityMigration::classify( outcome == SCRIPT_GUARD_REJECTED,
			outcome == SCRIPT_SWAP_REJECTED,
			outcome == SCRIPT_HOOK_RAISED );
	}

private:
	FakeTypePtr replacementFor( FakeTypePtr pOldType ) const
	{
		for (size_t i = 0; i < mappings_.size(); ++i)
		{
			if (mappings_[ i ].first == pOldType)
			{
				return mappings_[ i ].second;
			}
		}

		return pReplacement_;
	}

	FakeTypePtr pReplacement_;
	const BW::vector< ScriptedOutcome > & script_;
	size_t next_;
	BW::vector< BW::string > & journal_;
	bool linkPair_;
	BW::vector< std::pair< FakeTypePtr, FakeTypePtr > > mappings_;
};

/**
 * The fake population accessor: a plain range of entity pointers.
 */
class FakeGetEntity
{
public:
	typedef FakeEntity * EntityPtr;

	FakeEntity * operator()( FakeEntity * pEntity ) const
	{
		return pEntity;
	}
};

/**
 * The fake liveness check.
 */
class FakeAliveOp
{
public:
	bool operator()( FakeEntity * pEntity ) const
	{
		return !pEntity->destroyed_;
	}
};

/**
 * The fake rollback op: runs the plan's steps on the fake state tracks and
 * journals "rollback:<entity>:<steps>", with r for the entity restore, s
 * for the supersede strip, c for the old-link break.
 */
class FakeRollbackOp
{
public:
	FakeRollbackOp( BW::vector< BW::string > & journal ):
		journal_( journal ) {}

	void operator()( FakeEntity * pEntity, FakeTypePtr pOldType,
		FakeTypePtr /*pNewType*/, const EntityMigration::RollbackPlan & plan )
	{
		BW::string steps( "rollback:" );
		steps += pEntity->label_;
		steps += ":";

		if (plan.restoreEntity)
		{
			pEntity->pCurrentType_ = pOldType;
			pEntity->pScriptClass_ = pOldType;
			steps += "r";
		}
		if (plan.revertSupersede)
		{
			pEntity->descriptionStripped_ = true;
			steps += "s";
		}
		if (plan.clearOldLink)
		{
			pEntity->linkBroken_ = true;
			steps += "c";
		}

		journal_.push_back( steps );
	}

private:
	BW::vector< BW::string > & journal_;
};

} // anonymous namespace


/**
 *	A fully successful pass migrates everything and rolls nothing back.
 */
TEST( MigrationRollout_AllSuccess )
{
	FakeType oldType( "old" );
	FakeType newType( "new" );
	FakeEntity a( "a", &oldType );
	FakeEntity b( "b", &oldType );
	FakeEntity c( "c", &oldType );
	BW::vector< FakeEntity * > population;
	population.push_back( &a );
	population.push_back( &b );
	population.push_back( &c );

	BW::vector< ScriptedOutcome > script;
	script.push_back( SCRIPT_SUCCESS );
	script.push_back( SCRIPT_SUCCESS );
	script.push_back( SCRIPT_SUCCESS );
	BW::vector< BW::string > journal;

	const EntityMigration::LoopState state = EntityMigration::rollout(
		population, FakeGetEntity(), EntityMigration::STOP_ON_FAILURE,
		FakeMigrateOp( &newType, script, journal ), FakeAliveOp(),
		FakeRollbackOp( journal ) );

	CHECK_EQUAL( 3, state.migrated );
	CHECK_EQUAL( 0, state.failed );
	CHECK_EQUAL( 0, state.touched );
	CHECK( !state.stopped );
	CHECK( !state.rollbackNeeded );

	// Every entity runs the new type and class; the journal has the three
	// migrations and no rollback entries.
	CHECK( a.pCurrentType_ == &newType );
	CHECK( b.pCurrentType_ == &newType );
	CHECK( c.pCurrentType_ == &newType );
	CHECK_EQUAL( 3, int( journal.size() ) );
}


/**
 *	A stopped pass stops migrating and rolls the touched entities back in
 *	reverse migration order. Both rolled-back entities share one (new, old)
 *	pair, so the pair-level steps run once, on the first record of the pair
 *	in rollback order (the later-migrated entity), and that entity's own
 *	SCRIPT_EXCEPTION means it fully migrated and is restored like a success.
 */
TEST( MigrationRollout_StopRollsBackInReverseOrder )
{
	FakeType oldType( "old" );
	FakeType newType( "new" );
	FakeEntity a( "a", &oldType );
	FakeEntity b( "b", &oldType );
	FakeEntity c( "c", &oldType );
	BW::vector< FakeEntity * > population;
	population.push_back( &a );
	population.push_back( &b );
	population.push_back( &c );

	// a migrates, b fully migrates but its scripted hook raises the stop,
	// c is never reached.
	BW::vector< ScriptedOutcome > script;
	script.push_back( SCRIPT_SUCCESS );
	script.push_back( SCRIPT_HOOK_RAISED );
	script.push_back( SCRIPT_SUCCESS );	// c: after the stop, never runs
	BW::vector< BW::string > journal;

	const EntityMigration::LoopState state = EntityMigration::rollout(
		population, FakeGetEntity(), EntityMigration::STOP_ON_FAILURE,
		FakeMigrateOp( &newType, script, journal ), FakeAliveOp(),
		FakeRollbackOp( journal ) );

	CHECK_EQUAL( 1, state.migrated );
	CHECK_EQUAL( 1, state.failed );
	CHECK_EQUAL( 1, state.touched );
	CHECK( state.stopped );
	CHECK( state.rollbackNeeded );
	CHECK_EQUAL( EntityMigration::SCRIPT_EXCEPTION, state.firstFailure );

	// c was never migrated: it is still on the old type and class.
	CHECK( c.pCurrentType_ == &oldType );
	CHECK( c.pScriptClass_ == &oldType );

	// Two migrations and then the rollback in reverse order (b first: its
	// restore plus the pair's strip and link break; a: restore only, the
	// pair steps already ran on b).
	CHECK_EQUAL( 4, int( journal.size() ) );
	CHECK_EQUAL( BW::string( "migrate:a:success" ), journal[ 0 ] );
	CHECK_EQUAL( BW::string( "migrate:b:hook" ), journal[ 1 ] );
	CHECK_EQUAL( BW::string( "rollback:b:rsc" ), journal[ 2 ] );
	CHECK_EQUAL( BW::string( "rollback:a:r" ), journal[ 3 ] );

	CHECK( a.pCurrentType_ == &oldType );
	CHECK( b.pCurrentType_ == &oldType );
	CHECK( b.descriptionStripped_ );
	CHECK( b.linkBroken_ );
	CHECK( !a.descriptionStripped_ );
	CHECK( !a.linkBroken_ );
}


/**
 *	A failure on the very first entity still rolls that entity back: the
 *	touched counter, not the migrated counter, decides (the loop-level
 *	regression for the ledger's CR-P2-20).
 */
TEST( MigrationRollout_FirstEntityHookFailureRollsItselfBack )
{
	FakeType oldType( "old" );
	FakeType newType( "new" );
	FakeEntity a( "a", &oldType );
	BW::vector< FakeEntity * > population;
	population.push_back( &a );

	BW::vector< ScriptedOutcome > script;
	script.push_back( SCRIPT_HOOK_RAISED );
	BW::vector< BW::string > journal;

	const EntityMigration::LoopState state = EntityMigration::rollout(
		population, FakeGetEntity(), EntityMigration::STOP_ON_FAILURE,
		FakeMigrateOp( &newType, script, journal ), FakeAliveOp(),
		FakeRollbackOp( journal ) );

	CHECK_EQUAL( 0, state.migrated );
	CHECK_EQUAL( 1, state.failed );
	CHECK_EQUAL( 1, state.touched );
	CHECK( state.stopped );
	CHECK( state.rollbackNeeded );

	// The half-migrated entity is restored despite nothing having migrated
	// before it.
	CHECK_EQUAL( 2, int( journal.size() ) );
	CHECK_EQUAL( BW::string( "rollback:a:rsc" ), journal[ 1 ] );
	CHECK( a.pCurrentType_ == &oldType );
	CHECK( a.pScriptClass_ == &oldType );
}


/**
 *	A guard rejection touches nothing: the pass stops with an empty rollback
 *	set and no rollback entries.
 */
TEST( MigrationRollout_GuardRejectionRollsNothingBack )
{
	FakeType oldType( "old" );
	FakeType newType( "new" );
	FakeEntity a( "a", &oldType );
	BW::vector< FakeEntity * > population;
	population.push_back( &a );

	BW::vector< ScriptedOutcome > script;
	script.push_back( SCRIPT_GUARD_REJECTED );
	BW::vector< BW::string > journal;

	const EntityMigration::LoopState state = EntityMigration::rollout(
		population, FakeGetEntity(), EntityMigration::STOP_ON_FAILURE,
		FakeMigrateOp( &newType, script, journal ), FakeAliveOp(),
		FakeRollbackOp( journal ) );

	CHECK_EQUAL( 0, state.migrated );
	CHECK_EQUAL( 1, state.failed );
	CHECK_EQUAL( 0, state.touched );
	CHECK( state.stopped );
	CHECK( !state.rollbackNeeded );

	CHECK_EQUAL( 1, int( journal.size() ) );	// the migration entry only
	CHECK( a.pCurrentType_ == &oldType );
}


/**
 *	A script-only reload (the replacement is the old type itself) links no
 *	pair, so a rollback restores the entities but runs no pair-level step.
 */
TEST( MigrationRollout_ScriptOnlyReloadRollsBackWithoutPairSteps )
{
	FakeType oldType( "old" );
	FakeEntity a( "a", &oldType );
	FakeEntity b( "b", &oldType );
	BW::vector< FakeEntity * > population;
	population.push_back( &a );
	population.push_back( &b );

	// a migrates in place (script-only), b's class swap is refused to
	// trigger the stop - with no pair linked on either side.
	BW::vector< ScriptedOutcome > script;
	script.push_back( SCRIPT_SUCCESS );
	script.push_back( SCRIPT_SWAP_REJECTED );
	BW::vector< BW::string > journal;

	const EntityMigration::LoopState state = EntityMigration::rollout(
		population, FakeGetEntity(), EntityMigration::STOP_ON_FAILURE,
		FakeMigrateOp( &oldType, script, journal, /*linkPair=*/false ),
		FakeAliveOp(), FakeRollbackOp( journal ) );

	CHECK( state.stopped );
	CHECK( state.rollbackNeeded );

	// b: rejected swap restored itself, no pair to strip or clear - so its
	// rollback entry carries no steps. a: restore only.
	CHECK_EQUAL( 4, int( journal.size() ) );
	CHECK_EQUAL( BW::string( "rollback:b:" ), journal[ 2 ] );
	CHECK_EQUAL( BW::string( "rollback:a:r" ), journal[ 3 ] );
	CHECK( a.pCurrentType_ == &oldType );
}


/**
 *	CONTINUE_ON_FAILURE completes the whole pass and rolls nothing back:
 *	recording is the point of the continue policy.
 */
TEST( MigrationRollout_ContinuePolicyCompletesWithoutRollback )
{
	FakeType oldType( "old" );
	FakeType newType( "new" );
	FakeEntity a( "a", &oldType );
	FakeEntity b( "b", &oldType );
	FakeEntity c( "c", &oldType );
	BW::vector< FakeEntity * > population;
	population.push_back( &a );
	population.push_back( &b );
	population.push_back( &c );

	BW::vector< ScriptedOutcome > script;
	script.push_back( SCRIPT_SUCCESS );
	script.push_back( SCRIPT_SWAP_REJECTED );
	script.push_back( SCRIPT_SUCCESS );
	BW::vector< BW::string > journal;

	const EntityMigration::LoopState state = EntityMigration::rollout(
		population, FakeGetEntity(), EntityMigration::CONTINUE_ON_FAILURE,
		FakeMigrateOp( &newType, script, journal ), FakeAliveOp(),
		FakeRollbackOp( journal ) );

	CHECK_EQUAL( 2, state.migrated );
	CHECK_EQUAL( 1, state.failed );
	CHECK_EQUAL( 1, state.touched );
	CHECK( !state.stopped );
	CHECK( !state.rollbackNeeded );
	CHECK_EQUAL( 3, int( journal.size() ) );	// no rollback entries
}


/**
 *	Entities destroyed mid-pass are skipped by both the migration loop and
 *	the rollback loop. The liveness op dies entities on their Nth check, so
 *	the same entity can be alive for its migration (check 1) and dead for
 *	the rollback (check 2) - a hook destroying a neighbour between the two
 *	loops.
 */
TEST( MigrationRollout_DestroyedEntitiesSkipped )
{
	FakeType oldType( "old" );
	FakeType newType( "new" );
	FakeEntity a( "a", &oldType );
	FakeEntity b( "b", &oldType );
	FakeEntity c( "c", &oldType );
	BW::vector< FakeEntity * > population;
	population.push_back( &a );
	population.push_back( &b );
	population.push_back( &c );

	// a migrates and is destroyed before the rollback reaches it (dies on
	// its second liveness check), b is destroyed before its turn (dies on
	// the first), c raises the stop and is rolled back.
	BW::map< FakeEntity *, int > dieOnCall;
	dieOnCall[ &a ] = 2;
	dieOnCall[ &b ] = 1;
	dieOnCall[ &c ] = 100;

	class CallCountAliveOp
	{
	public:
		CallCountAliveOp( BW::map< FakeEntity *, int > & dieOnCall ):
			dieOnCall_( dieOnCall ) {}

		bool operator()( FakeEntity * pEntity ) const
		{
			int & calls = calls_[ pEntity ];
			++calls;
			BW::map< FakeEntity *, int >::iterator it =
				dieOnCall_.find( pEntity );
			return it != dieOnCall_.end() ? calls < it->second : true;
		}

	private:
		BW::map< FakeEntity *, int > & dieOnCall_;
		mutable BW::map< FakeEntity *, int > calls_;
	};

	BW::vector< ScriptedOutcome > script;
	script.push_back( SCRIPT_SUCCESS );		// a
	script.push_back( SCRIPT_HOOK_RAISED );	// c (b is skipped, no entry)
	BW::vector< BW::string > journal;

	const EntityMigration::LoopState state = EntityMigration::rollout(
		population, FakeGetEntity(), EntityMigration::STOP_ON_FAILURE,
		FakeMigrateOp( &newType, script, journal ),
		CallCountAliveOp( dieOnCall ), FakeRollbackOp( journal ) );

	CHECK_EQUAL( 1, state.migrated );	// a, before it died
	CHECK( state.stopped );

	// Journal: a's migration, b skipped (no entry), c's migration and c's
	// rollback; a is in the rollback set but the liveness check skips it.
	CHECK_EQUAL( 3, int( journal.size() ) );
	CHECK_EQUAL( BW::string( "migrate:a:success" ), journal[ 0 ] );
	CHECK_EQUAL( BW::string( "migrate:c:hook" ), journal[ 1 ] );
	CHECK_EQUAL( BW::string( "rollback:c:rsc" ), journal[ 2 ] );

	// a died on the new type: nothing restored it.
	CHECK( a.pCurrentType_ == &newType );
}


/**
 *	Distinct (new, old) pairs attribute their pair-level steps independently:
 *	each pair's first record in rollback order runs them.
 */
TEST( MigrationRollout_PairStepsRunPerPair )
{
	FakeType oldA( "oldA" );
	FakeType oldB( "oldB" );
	FakeType newA( "newA" );
	FakeType newB( "newB" );
	FakeEntity a1( "a1", &oldA );
	FakeEntity b1( "b1", &oldB );
	FakeEntity b2( "b2", &oldB );
	BW::vector< FakeEntity * > population;
	population.push_back( &a1 );
	population.push_back( &b1 );
	population.push_back( &b2 );

	BW::vector< ScriptedOutcome > script;
	script.push_back( SCRIPT_SUCCESS );			// a1: pair (newA, oldA)
	script.push_back( SCRIPT_SUCCESS );			// b1: pair (newB, oldB)
	script.push_back( SCRIPT_HOOK_RAISED );		// b2: the stop
	BW::vector< BW::string > journal;

	FakeMigrateOp migrateOp( &newA, script, journal );
	migrateOp.mapReplacement( &oldA, &newA );
	migrateOp.mapReplacement( &oldB, &newB );

	const EntityMigration::LoopState state = EntityMigration::rollout(
		population, FakeGetEntity(), EntityMigration::STOP_ON_FAILURE,
		migrateOp, FakeAliveOp(), FakeRollbackOp( journal ) );

	CHECK( state.stopped );
	CHECK_EQUAL( 2, state.migrated );
	CHECK_EQUAL( 1, state.failed );

	// Rollback order: b2 (restore + its pair's strip and link break), then
	// b1 (restore only: its pair's steps ran on b2), then a1 (restore plus
	// its own pair's steps).
	CHECK_EQUAL( 6, int( journal.size() ) );
	CHECK_EQUAL( BW::string( "rollback:b2:rsc" ), journal[ 3 ] );
	CHECK_EQUAL( BW::string( "rollback:b1:r" ), journal[ 4 ] );
	CHECK_EQUAL( BW::string( "rollback:a1:rsc" ), journal[ 5 ] );

	CHECK( b2.descriptionStripped_ && b2.linkBroken_ );
	CHECK( !b1.descriptionStripped_ && !b1.linkBroken_ );
	CHECK( a1.descriptionStripped_ && a1.linkBroken_ );
}

// test_migration_rollout.cpp

BW_END_NAMESPACE
