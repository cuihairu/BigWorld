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
		PyDict_SetItemString( pModules, "sys", pSysModule ); // Steals a reference
		Py_DECREF( pSysModule );
	}
}

// entity_type.ipp
