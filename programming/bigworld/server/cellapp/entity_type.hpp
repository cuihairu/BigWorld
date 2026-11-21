#ifndef ENTITY_TYPE_HPP
#define ENTITY_TYPE_HPP

#include <Python.h>

#include "cstdmf/bw_string.hpp"

#include "cstdmf/stringmap.hpp"

#include "entitydef/entity_description.hpp"
#include "entitydef/method_description.hpp"

#include "network/basictypes.hpp"

#include "server/entity_type_profiler.hpp"

#include <mutex>
#include <shared_mutex>

BW_BEGIN_NAMESPACE

class RealEntity;
class Entity;
class BinaryIStream;
class BinaryOStream;

enum CreateRealInfo
{
	CREATE_REAL_FROM_INIT,
	CREATE_REAL_FROM_OFFLOAD,
	CREATE_REAL_FROM_RESTORE
};


typedef SmartPointer<Entity> EntityPtr;

class EntityType;
typedef SmartPointer<EntityType> EntityTypePtr;
typedef BW::vector< EntityTypePtr > EntityTypes;

/**
 *	This class is used to represent an entity type.
 */
class EntityType : public ReferenceCount
{
public:
	static const EntityTypeID INVALID_INDEX = -1;

	EntityType( const EntityDescription& entityDescription,
			PyTypeObject * pType );

	~EntityType();

	Entity * newEntity() const;

	const VolatileInfo & volatileInfo() const;

	DataDescription * description( const char * attr, 
								const char * component) const;
	const EntityDescription & description() const;

	uint propCountGhost() const						{ return propCountGhost_; }
	uint propCountGhostPlusReal() const				{ return static_cast< uint >( propDescs_.size() );}
	DataDescription * propIndex( int index ) const	{ return propDescs_[index];}

	uint propCountClientServer() const
	{
		return entityDescription_.clientServerPropertyCount();
	}

	PyTypeObject * pPyType() const 	{ return pPyType_; }
	void setPyType( PyTypeObject * pPyType );

	const char * name() const;

	bool canBeOnBase() const  { return entityDescription_.canBeOnBase(); }
	bool canBeOnCell() const  { return entityDescription_.canBeOnCell(); }

	bool expectsNearbyEntity() const { return expectsNearbyEntity_; }

	EntityTypeID typeID() const			{ return entityDescription_.index(); }
	EntityTypeID clientTypeID() const;

	void addDataToStream( Entity * pEntity,
			BinaryOStream & stream,
			EntityDataFlags dataType );

	EntityTypePtr old() const	{ return pOldSelf_; }
	void old( EntityTypePtr pOldType );

	/**
	 *	This method undoes the supersede rename that old() applied to the
	 *	old description's cell methods - one stacked old_ prefix per call,
	 *	so it pairs with exactly one old() first-setter. The rollback half
	 *	of the hot-reload rollout runs it once per (new, old) type pair.
	 */
	void unsupersedeOldDescription()
	{
		entityDescription_.unsupersede( MethodDescription::CELL );
	}

	// profiling
	const EntityTypeProfiler & profiler() const { return profiler_; }
	EntityTypeProfiler & profiler() { return profiler_; }
	static void tickProfilers( float & totalEntityLoad,
								float & totalAddedLoad );

	/// @name static methods
	//@{
	static bool init( bool isReload = false );
	static bool reloadScript( bool isRecover = false );
	static void migrate( bool isFullReload = true );
	static void cleanupAfterReload( bool isFullReload = true );

	/**
	 *	The quoted MD5 schema digest of the entity definitions loaded by the
	 *	last init, "" before anything has loaded. Captured while the parsed
	 *	map is in hand - before any migrate runs - so it cannot be polluted
	 *	by the old_ rename that supersede() applies to the old descriptions.
	 *	Non-reload reloadScript() re-imports classes only and leaves it alone.
	 */
	static const BW::string & schemaDigestQuote();

	/**
	 *	This static method swaps a reloaded set of Python modules into the given
	 *	sys.modules dictionary: the dictionary is replaced by the reloaded one,
	 *	the sys module is kept (it has remapped stdout/stderr) and BigWorld is
	 *	re-exposed on __main__. Extracted from migrate() so that the module
	 *	swap semantics can be asserted without a running CellApp.
	 *
	 *	@param pModules		The sys.modules dictionary to update in place.
	 *	@param pNewModules	The modules dictionary of the reloaded scripts.
	 */
	static void swapInModules( PyObject * pModules, PyObject * pNewModules );

	static void clearStatics();

	static EntityTypePtr getType( EntityTypeID typeID );
	static EntityTypePtr getType( const char * className );
	static const EntityDescription * getDescription( EntityTypeID typeID );
	static EntityTypeID nameToIndex( const char * name );

	static EntityTypes& getTypes();
	//@}

#if ENABLE_WATCHERS
	static WatcherPtr pWatcher();

	class Stats
	{
	public:
		Stats() : detailedPositionDescription_( "detailedPosition" ) {}

		void countSentToOwnClient( int bytes )
							{ sentToOwnClient_.count( bytes ); }

		void countNonVolatileSentToOtherClients( int bytes ) 
							{ nonVolatileSentToOtherClients_.count( bytes ); }

		void countVolatileSentToOtherClients(int bytes) 
							{ volatileSentToOtherClients_.count( bytes ); }

		void countAddedToHistoryQueue( int bytes )
							{ addedToHistoryQueue_.count( bytes ); }

		void countSentToGhosts( int bytes ) { sentToGhosts_.count( bytes ); }
		void countSentToBase( int bytes ) { sentToBase_.count( bytes ); }

		MemberDescription & detailedPositionDescription()
		{
			return detailedPositionDescription_;
		}

		const MemberDescription & detailedPositionDescription() const
		{
			return detailedPositionDescription_;
		}

		EntityMemberStats & createEntityCounter()
		{
			return createEntity_;
		}
		EntityMemberStats & enterAoICounter()
		{
			return enterAoI_;
		}
		static WatcherPtr pWatcher();

	private:
		EntityMemberStats::Stat sentToOwnClient_;
		EntityMemberStats::Stat nonVolatileSentToOtherClients_;
		EntityMemberStats::Stat volatileSentToOtherClients_;
		EntityMemberStats::Stat addedToHistoryQueue_;
		EntityMemberStats::Stat sentToGhosts_;
		EntityMemberStats::Stat sentToBase_;

		MemberDescription detailedPositionDescription_;

		EntityMemberStats createEntity_;
		EntityMemberStats enterAoI_;
	};

	Stats & stats() { return stats_; }
	MemberDescription & detailedPositionDescription()
	{
		return stats_.detailedPositionDescription();
	}
	const MemberDescription & detailedPositionDescription() const
	{
		return stats_.detailedPositionDescription();
	}

private:
	Stats stats_;

#else
	MemberDescription & detailedPositionDescription()
	{
		return detailedPositionDescription_;
	}
	const MemberDescription & detailedPositionDescription() const
	{
		return detailedPositionDescription_;
	}

private:
	MemberDescription detailedPositionDescription_;

#endif

	EntityDescription entityDescription_;
	PyTypeObject * pPyType_;

	uint							propCountGhost_;
	BW::vector<DataDescription*>	propDescs_;

	EntityTypePtr	pOldSelf_;

	bool expectsNearbyEntity_;

	// profiling
	EntityTypeProfiler profiler_;

	// Inline statics: the reload seam (migrate/nameToIndex) lives in
	// entity_type.ipp so the swap semantics can be asserted without linking
	// the whole cellapp component, and the test TU needs these objects
	// without entity_type.o. See the .ipp notes.
	inline static EntityTypes s_curTypes_, s_newTypes_;

	typedef StringHashMap< EntityTypeID > NameToIndexMap;
	inline static NameToIndexMap s_nameToIndexMap_;

	/// Serialises the live type table (s_curTypes_ and s_nameToIndexMap_)
	/// against the reload's full swap. The lookups take it shared; only the
	/// swap in migrate() holds it exclusively. CR-P2-07.
	inline static std::shared_mutex s_typesMutex_;

	inline static PyObject * s_pInitModules_, * s_pNewModules_;

	/// Test access to the reload seam (the mutex and the new-modules pointer).
	friend struct EntityTypeTestAccess;

	/// The schema digest quote of the last init (see schemaDigestQuote()).
	static BW::string s_schemaDigestQuote_;
};

typedef SmartPointer<EntityType> EntityTypePtr;


#ifdef CODE_INLINE
#include "entity_type.ipp"
#endif

BW_END_NAMESPACE

#endif // ENTITY_TYPE_HPP
// entity_type.hpp
