// entity_type.ipp

#ifdef CODE_INLINE
#define INLINE	inline
#else
#define INLINE
#endif

// -----------------------------------------------------------------------------
// Section: EntityType
// -----------------------------------------------------------------------------

/**
 *	This method returns the volatile info associated with this entity type. The
 *	volatile info indicates what data associated with entities of this type
 *	changes regularly and so should be sent regularly (such as position and
 *	direction).
 */
INLINE
const VolatileInfo & EntityType::volatileInfo() const
{
	return entityDescription_.volatileInfo();
}


/**
 *	This method returns an object that describes this entity type. The
 *	description includes things such as the properties and method supported by
 *	entities of this type.
 */
INLINE
const EntityDescription & EntityType::description() const
{
	return entityDescription_;
}


/**
 *	This method returns the name associated with the entity type.
 */
INLINE
const char * EntityType::name() const
{
	return entityDescription_.name().c_str();
}


/**
 *	This method returns the ID of the client entity type associated with this
 *	server entity type. Often, this is the same as this object's ID but may not
 *	be so. For example, an entity of type NPC on the server may be of type
 *	Avatar on the client.
 */
INLINE
EntityTypeID EntityType::clientTypeID() const
{
	return entityDescription_.clientIndex();
}


// -----------------------------------------------------------------------------
// Section: Module swapping for script reload
// -----------------------------------------------------------------------------

/**
 *	This static method swaps a reloaded set of Python modules into the given
 *	sys.modules dictionary: the dictionary is replaced by the reloaded one, the
 *	sys module is kept (it has remapped stdout/stderr) and BigWorld is
 *	re-exposed on __main__. Extracted from migrate() so that the module swap
 *	semantics can be asserted without a running CellApp.
 */
INLINE
void EntityType::swapInModules( PyObject * pModules, PyObject * pNewModules )
{
	PyObject * pSysModule = PyDict_GetItemString( pModules, "sys" ); // borrowed

	// The clear below drops the reference this dictionary holds on the sys
	// module, so the borrowed pointer has to be upgraded to an owned one now:
	// if this dictionary were the last holder, pSysModule would be a dangling
	// pointer for the rest of the function.
	if (pSysModule != NULL)
	{
		Py_INCREF( pSysModule );
	}

	PyDict_Clear( pModules );
	PyDict_Update( pModules, pNewModules );

	/* BIGWORLD(3.13 migration): PyInterpreterState is opaque in 3.x, so
	 * interp->builtins can no longer be reassigned here (it used to be
	 * pointed at the __builtin__ module's dict). Nothing else needs it:
	 * executed code picks builtins up from its globals, and the builtins
	 * module itself has just been restored into sys.modules by the update
	 * above. */

	PyObject * pBigWorld = PyDict_GetItemString( pModules, "BigWorld" ); // borrowed
	if (pBigWorld)
	{
		Py_INCREF( pBigWorld ); // AddObject steals a reference
		PyModule_AddObject( PyDict_GetItemString( pModules, "__main__" ),
							"BigWorld", pBigWorld );
	}

	// Restore original sys module since it has remapped stdout/stderr.
	if (pSysModule)
	{
		// PyDict_SetItemString does not steal: it INCREFs the value, so the
		// reference owned since the INCREF above must be released here.
		PyDict_SetItemString( pModules, "sys", pSysModule );
		Py_DECREF( pSysModule );
	}
}


// -----------------------------------------------------------------------------
// Section: Type table swap for script reload
// -----------------------------------------------------------------------------

/**
 *	This static method migrates the collection of entity types to
 *	their reloaded version.
 *
 *	Lives here (not in entity_type.cpp) for the same reason swapInModules
 *	does: the reload semantics can be asserted without a running CellApp.
 *	The full reload's swap takes the type-table mutex exclusively while the
 *	lookups (getType/getDescription/nameToIndex) hold it shared, so a reader
 *	never sees the live table mid-rewrite. CR-P2-07.
 */
INLINE
void EntityType::migrate( bool isFullReload )
{
	PyObject * pModules = PySys_GetObject( "modules" );
	if (pModules != s_pNewModules_)
	{
		swapInModules( pModules, s_pNewModules_ );

		// note this migration is still a bit dodgy but a lot better than it was
		// probably should migrate the whole interpreter state from the
		// reloading thread... although currently it is missing some stuff done
		// by init time jobs (e.g. stdout/stderr redirection).
	}

	if (isFullReload)
	{
		std::unique_lock< std::shared_mutex > lock( s_typesMutex_ );

		s_curTypes_ = s_newTypes_;

		s_nameToIndexMap_.clear();
		for (uint i = 0; i < s_curTypes_.size(); i++)
			s_nameToIndexMap_[ s_curTypes_[i]->name() ] = i;
	}
	// TODO: could match up all old and new types here...
}


/**
 *	This static method returns the index of the type with the input name. If no
 *	such type exists, -1 is returned.
 *
 *	Holds the type-table mutex shared, the counterpart of the exclusive hold
 *	the full reload's swap takes in migrate().
 *
 *	@return	On success, the index of the type found, otherwise EntityType::INVALID_INDEX.
 */
INLINE
EntityTypeID EntityType::nameToIndex( const char * name )
{
	std::shared_lock< std::shared_mutex > lock( s_typesMutex_ );

	NameToIndexMap::iterator iter = s_nameToIndexMap_.find( name );

	return (iter != s_nameToIndexMap_.end()) ? iter->second : INVALID_INDEX;
}

// entity_type.ipp
