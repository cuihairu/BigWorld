#include "pch.hpp"

#include "fixed_dict_data_instance.hpp"

#include "entitydef/data_types/fixed_dict_data_type.hpp"

#include "entitydef/script_data_sink.hpp"


BW_BEGIN_NAMESPACE

namespace // (anonymous)
{

PyMappingMethods g_fixedDictMappingMethods =
{
	PyFixedDictDataInstance::pyGetLength,		// mp_length
	PyFixedDictDataInstance::pyGetFieldByKey,	// mp_subscript
	PyFixedDictDataInstance::pySetFieldByKey	// mp_ass_subscript
};

} // end namespace (anonymous)

PY_TYPEOBJECT_SPECIALISE_CMP( PyFixedDictDataInstance,
	PyFixedDictDataInstance::pyCompare )
PY_TYPEOBJECT_SPECIALISE_MAP( PyFixedDictDataInstance,
	&g_fixedDictMappingMethods )
PY_TYPEOBJECT( PyFixedDictDataInstance )

PY_BEGIN_METHODS( PyFixedDictDataInstance )
	PY_METHOD( has_key )
	PY_METHOD( keys )
	PY_METHOD( items )
	PY_METHOD( values )
	PY_METHOD( getFieldNameForIndex )
	PY_PICKLING_METHOD()
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( PyFixedDictDataInstance )
PY_END_ATTRIBUTES()

PY_UNPICKLING_FACTORY( PyFixedDictDataInstance, FixedDict )

/**
 *	Default constructor. Field values are NULL.
 */
PyFixedDictDataInstance::PyFixedDictDataInstance(
		FixedDictDataType* pDataType ) :
	IntermediatePropertyOwner( &s_type_ ),
	pDataType_( pDataType ),
	fieldValues_( pDataType->getNumFields() )
{}

/**
 *	Copy constructor...sort of.
 */
PyFixedDictDataInstance::PyFixedDictDataInstance(
		FixedDictDataType* pDataType,
		const PyFixedDictDataInstance& other ) :
	IntermediatePropertyOwner( &s_type_ ),
	pDataType_( pDataType ),
	fieldValues_( pDataType->getNumFields() )
{
	uint fieldCount = static_cast<uint>(fieldValues_.size());
	for (uint i = 0; i < fieldCount; ++i)
	{
		this->initFieldValue( i, other.fieldValues_[i] );
		MF_ASSERT_DEV( fieldValues_[i] );
	}
}

/**
 *	Destructor
 */
PyFixedDictDataInstance::~PyFixedDictDataInstance()
{
	for (uint i = 0; i < fieldValues_.size(); ++i)
	{
		if (fieldValues_[i])
		{
			pDataType_->getFieldDataType(i).detach( fieldValues_[i] );
		}
	}
}


/**
 *
 */
void PyFixedDictDataInstance::initFieldValue( int index, ScriptObject val )
{
	fieldValues_[index] =
		pDataType_->getFieldDataType(index).attach( val, this, index );

	if (!fieldValues_[ index ])
	{
		PyObjectPtr pStr( PyObject_Str( val.get() ),
				PyObjectPtr::STEAL_REFERENCE );
		ERROR_MSG( "PyFixedDictDataInstance::initFieldValue: "
					"Failed to initialise index %d with value %s\n",
				index, PyUnicode_AsUTF8( pStr.get() ) );
	}
}


/**
 *	Someone wants to know how we have divided our property
 */
int PyFixedDictDataInstance::getNumOwnedProperties() const
{
	return static_cast<int>(fieldValues_.size());
}


/**
 *
 */
bool PyFixedDictDataInstance::isSameType( ScriptObject pValue,
		const FixedDictDataType & dataType )
{
	return PyFixedDictDataInstance::Check( pValue.get() ) &&
		(&static_cast<PyFixedDictDataInstance*>(pValue.get())->dataType() == &dataType);
}


/**
 *	Someone wants to know if this property is an owner in its own right.
 */
PropertyOwnerBase *
	PyFixedDictDataInstance::getChildPropertyOwner( int ref ) const
{
	if (uint(ref) >= fieldValues_.size())
	{
		ERROR_MSG( "PyFixedDictDataInstance::getChildPropertyOwner: "
					"Bad index %d. size = %zd\n",
				ref, fieldValues_.size() );
		return NULL;
	}

	DataType & pFieldDataType = pDataType_->getFieldDataType( ref );

	return pFieldDataType.asOwner( fieldValues_[ref] );
}


/**
 *	Someone wants us to change the value of this property.
 */
ScriptObject PyFixedDictDataInstance::setOwnedProperty( int ref,
	BinaryIStream & data )
{
	DataType& fieldType = pDataType_->getFieldDataType( ref );
	ScriptDataSink sink;
	if (!fieldType.createFromStream( data, sink, false ))
	{
		return ScriptObject();
	}

	ScriptObject pNewValue = sink.finalise();

	ScriptObject & valRef = fieldValues_[ref];
	ScriptObject pOldValue = valRef;

	if (valRef != pNewValue)
	{
		// detach old value and attach new one
		fieldType.detach( valRef );
		valRef = fieldType.attach( pNewValue, this, ref );
	}

	return pOldValue;
}


/**
 *	This method returns a Python object representing the child at the given
 *	index.
 */
ScriptObject PyFixedDictDataInstance::getPyIndex( int index ) const
{
	return ScriptString::create( pDataType_->getFields()[ index ].name_ );
}


/**
 *	Python get attribute method
 */
ScriptObject PyFixedDictDataInstance::pyGetAttribute( 
	const ScriptString & attrObj )
{
	const char * attr = attrObj.c_str();

	ScriptObject pResult = PropertyOwner::pyGetAttribute( attrObj );

	if (!pResult)
	{
		PyErr_Clear();
		pResult = ScriptObject(
			this->getFieldByKey( attr ),
			ScriptObject::FROM_NEW_REFERENCE );
	}

	return pResult;
}


/**
 *	Python set attribute method
 */
bool PyFixedDictDataInstance::pySetAttribute( const ScriptString & attrObj,
	const ScriptObject & value )
{
	bool result = PropertyOwner::pySetAttribute( attrObj, value );

	if (!result)
	{
		PyErr_Clear();
		result = (this->setFieldByKey( attrObj.c_str(), value.get() ) == 0);
	}

	return result;
}

/**
 *	Python mapping interface. Returns the number of items in the map.
 */
Py_ssize_t PyFixedDictDataInstance::pyGetLength( PyObject* self )
{
	return static_cast<PyFixedDictDataInstance*>(self)->getNumFields();
}

/**
 *	Python mapping interface. Returns the value keyed by given key.
 */
PyObject* PyFixedDictDataInstance::pyGetFieldByKey( PyObject* self,
		PyObject* key )
{
	return static_cast<PyFixedDictDataInstance*>(self)->getFieldByKey( key );
}

/**
 *	Python mapping interface. Sets the value keyed by given key.
 */
int PyFixedDictDataInstance::pySetFieldByKey( PyObject* self, PyObject* key,
		PyObject* value )
{
	return static_cast<PyFixedDictDataInstance*>(self)->
					setFieldByKey( key, value );
}

/**
 *	This method gets the value keyed by the input key.
 */
PyObject* PyFixedDictDataInstance::getFieldByKey( PyObject* key )
{
	if (!PyUnicode_Check( key ))
	{
		PyErr_Format( PyExc_KeyError,
				"Key type must be a string, %s given",
				key->ob_type->tp_name );
		return NULL;
	}

	return this->getFieldByKey( PyUnicode_AsUTF8( key ) );
}


/**
 *	This method gets the value keyed by the input key.
 */
PyObject* PyFixedDictDataInstance::getFieldByKey( const char * keyString )
{
	PyObject * pValue = NULL;

	int idx = pDataType_->getFieldIndex( keyString );
	if (idx >= 0)
	{
		MF_ASSERT( fieldValues_[idx] &&
				"Likely caused by a script error in data type streaming" );

		pValue = fieldValues_[idx].get();
		Py_INCREF( pValue );
	}
	else
	{
		PyObject * pKey = PyUnicode_FromString( keyString );
		PyErr_SetObject( PyExc_KeyError, pKey );
		Py_DECREF( pKey );
		pValue = NULL;
	}

	return pValue;
}


/**
 *	Sets the value keyed by given key.
 */
int PyFixedDictDataInstance::setFieldByKey( PyObject* key, PyObject* value )
{
	if (!PyUnicode_Check( key ))
	{
		PyErr_Format( PyExc_KeyError,
				"Key type must be a string, %s given",
				key->ob_type->tp_name );
		return -1;
	}

	return this->setFieldByKey( PyUnicode_AsUTF8( key ), value );
}

/**
 *	Sets the value keyed by given key.
 */
int PyFixedDictDataInstance::setFieldByKey( const char * keyString,
		PyObject* value )
{
	if (value == NULL)
	{
		PyErr_Format( PyExc_TypeError,
				"Cannot delete entry %s from FIXED_DICT",
				keyString );
		return -1;
	}

	int idx = pDataType_->getFieldIndex( keyString );

	if (idx < 0)
	{
		PyObject * pKey = PyUnicode_FromString( keyString );
		PyErr_SetObject( PyExc_KeyError, pKey );
		Py_DECREF( pKey );
		return -1;
	}

	DataType & fieldDataType = pDataType_->getFieldDataType( idx );

	if (!this->changeOwnedProperty( fieldValues_[ idx ], ScriptObject( value,
		ScriptObject::FROM_BORROWED_REFERENCE ), fieldDataType, idx ))
	{
		if (!PyErr_Occurred())
		{
			PyErr_Format( PyExc_TypeError,
				"Value keyed by '%s' must be set to type '%s'",
				keyString, fieldDataType.typeName().c_str() );
		}
		return -1;
	}

	return 0;
}


/**
 * 	This method returns true if the given entry exists.
 */
PyObject * PyFixedDictDataInstance::py_has_key( PyObject* args )
{
	char * key;

	if (!PyArg_ParseTuple( args, "s", &key ))
	{
		PyErr_SetString( PyExc_TypeError, "Expected a string argument." );
		return NULL;
	}

	if (pDataType_->getFieldIndex( key ) >= 0)
		Py_RETURN_TRUE;
	else
		Py_RETURN_FALSE;
}


/**
 * 	This method returns a list of all the keys of this object.
 */
PyObject* PyFixedDictDataInstance::py_keys(PyObject* /*args*/)
{
	const FixedDictDataType::Fields& fields = pDataType_->getFields();

	PyObject* pyKeyList = PyList_New( fields.size() );
	if (pyKeyList)
	{
		for ( FixedDictDataType::Fields::const_iterator i = fields.begin();
				i < fields.end(); ++i )
		{
			PyList_SET_ITEM( pyKeyList, i - fields.begin(),
							PyUnicode_FromString( i->name_.c_str() ) );
		}
	}

	return pyKeyList;
}


/**
 * 	This method returns a list of all the values of this object.
 */
PyObject* PyFixedDictDataInstance::py_values( PyObject* /*args*/ )
{
	PyObject* pyValueList = PyList_New( fieldValues_.size() );
	if (pyValueList)
	{
		for ( FieldValues::iterator i = fieldValues_.begin();
				i < fieldValues_.end(); ++i )
		{
			PyObject* pyFieldValue = i->get();
			Py_INCREF( pyFieldValue );
			PyList_SET_ITEM( pyValueList, i - fieldValues_.begin(),
								pyFieldValue );
		}
	}
	return pyValueList;
}


/**
 * 	This method returns a list of tuples of all the keys and values of this
 *	object.
 */
PyObject* PyFixedDictDataInstance::py_items( PyObject* /*args*/ )
{
	const FixedDictDataType::Fields& fields = pDataType_->getFields();

	PyObject* pyItemList = PyList_New( fields.size() );
	if (pyItemList)
	{
		for ( FixedDictDataType::Fields::size_type i = 0; i < fields.size();
				++i )
		{
			PyObject* pyTuple = Py_BuildValue( "(sO)", fields[i].name_.c_str(),
										fieldValues_[i].get() );
			PyList_SET_ITEM( pyItemList, i, pyTuple );
		}
	}

	return pyItemList;
}

/**
 *	Python pickling helper. Returns the second element of the tuple expected of
 * 	Python's __reduce__ method. Declared in PY_PICKLING_METHOD_DECLARE.
 */
PyObject * PyFixedDictDataInstance::pyPickleReduce()
{
	// Make an equivalent dictionary.
	const FixedDictDataType::Fields& fields = pDataType_->getFields();
	PyObject* pDict = PyDict_New();
	for ( FixedDictDataType::Fields::size_type i = 0; i < fields.size();
			++i )
	{
		PyDict_SetItemString( pDict, fields[i].name_.c_str(),
							fieldValues_[i].get() );
	}

	// Make a tuple (method arguments for PickleResolve())
	PyObject * pArgs = PyTuple_New( 1 );
	PyTuple_SET_ITEM( pArgs, 0, pDict );

	return pArgs;
}

/**
 *	Python unpickling helper. Creates a PyFixedDictDataInstance from the
 *	information returned by PyFixedDictDataInstance::pyPickleReduce().
 * 	Required by PY_AUTO_UNPICKLING_FACTORY_DECLARE.
 */
PyObject * PyFixedDictDataInstance::PickleResolve( ScriptObject pDict )
{
	// NOTE: Currently doesn't return a PyFixedDictDataInstance
	// since it's too hard to find the right FixedDictDataType for pDataType_.
	// We'd also need pOwner_ and ownerRef_ and re-attach
	// PyFixedDictDataInstance to the right entity here.

	// Simply return the equivalent dictionary for now.
	Py_XINCREF( pDict.get() );

	return pDict.get();
}


/**
 *	Compare two instances based on their keys only.
 */
/* static */
int PyFixedDictDataInstance::pyCompareKeys( 
		PyFixedDictDataInstance * pFixedDictA, 
		PyFixedDictDataInstance * pFixedDictB )
{
	// Optimisation: check if the instances point to the actual same type
	// object.
	if (&(pFixedDictA->dataType()) == &(pFixedDictB->dataType()))
	{
		// The two instances belong to the same data type.
		return 0;
	}

	// Otherwise, we'll have to check that the FIXED_DICT fields are
	// consistent manually.

	size_t numKeysA = pFixedDictA->getNumFields();
	size_t numKeysB = pFixedDictB->getNumFields();

	if (numKeysA != numKeysB)
	{
		// The quantity of keys is inconsistent, the one with less keys comes
		// first.
		return (numKeysA < numKeysB) ? -1 : 1;
	}

	// OK, the number of keys is consistent, check the name of the keys.

	for (size_t i = 0; i < numKeysA; ++i)
	{
		const BW::string & fieldNameA = 
			pFixedDictA->dataType().getFieldName( static_cast<int>(i) );
		const BW::string & fieldNameB = 
			pFixedDictB->dataType().getFieldName( static_cast<int>(i) );

		if (fieldNameA != fieldNameB)
		{
			// Found a mismatched key, compare lexicographically.
			return (fieldNameA < fieldNameB) ? -1 : 1;
		}
	}

	// Looks good.

	return 0;
}


/**
 *	Compare two instances based on their values, assuming that they have the
 *	same keys.
 */
/* static */
bool PyFixedDictDataInstance::pyCompareValuesWithEqualKeys( 
		PyFixedDictDataInstance * pFixedDictA, 
		PyFixedDictDataInstance * pFixedDictB, 
		int & compareResult )
{
	compareResult = 0;

	size_t numFields = pFixedDictA->getNumFields();
	size_t i = 0;

	while (compareResult == 0 && i < numFields)
	{
		PyObject * pElementA = pFixedDictA->fieldValues_[i].get();
		PyObject * pElementB = pFixedDictB->fieldValues_[i].get();

		/* BIGWORLD(3.13 migration): was PyObject_Cmp(), removed in 3.x. */
		int lt = PyObject_RichCompareBool( pElementA, pElementB, Py_LT );
		if (lt == -1)
		{
			// Error state raised by the rich comparison.
			return false;
		}
		else if (lt == 1)
		{
			compareResult = -1;
		}
		else
		{
			int gt = PyObject_RichCompareBool( pElementA, pElementB, Py_GT );
			if (gt == -1)
			{
				// Error state raised by the rich comparison.
				return false;
			}
			compareResult = (gt == 1) ? 1 : 0;
		}
		++i;
	}

	return true;
}

/**
 *	Python comparison function for PyFixedDictDataInstances.
 */
/* static */
int PyFixedDictDataInstance::pyCompare( PyObject * a, PyObject * b )
{
	/* BIGWORLD_BEGIN(3.13 migration)
	 * Same defect as PyArrayDataInstance::pyCompare: Python does NOT
	 * guarantee both operands have this type, because when the left operand's
	 * __lt__/__eq__ returns NotImplemented Python calls the reflected
	 * operation with the foreign type on the left (e.g. `dict < fixedDict`
	 * reaches us as fixedDict.__gt__(dict)). Asserting here aborted the
	 * process for any such comparison.
	 *
	 * Report "ordered, and not equal" with no exception set, so that
	 * ScriptObject::compareTo() sees a plain inequality and can fall back to
	 * its conversion paths rather than an incomparable-types error.
	 */
	if (!PyFixedDictDataInstance::Check( a ) ||
		!PyFixedDictDataInstance::Check( b ))
	{
		return -1;
	}
	/* BIGWORLD_END */

	PyFixedDictDataInstance * pFixedDictA = 
		static_cast< PyFixedDictDataInstance * >( a );
	PyFixedDictDataInstance * pFixedDictB = 
		static_cast< PyFixedDictDataInstance * >( b );

	int compareResult = PyFixedDictDataInstance::pyCompareKeys( 
		pFixedDictA, pFixedDictB );

	if (compareResult != 0)
	{
		return compareResult;
	}

	if (!PyFixedDictDataInstance::pyCompareValuesWithEqualKeys( 
			pFixedDictA, pFixedDictB, compareResult ))
	{
		// Error state raised, the caller has to check if error occurred.
		return -1;
	}

	return compareResult;
}


/**
 *	This method is called for repr() and str() on FixedDict instances.
 */
PyObject * PyFixedDictDataInstance::pyRepr()
{
	Py_ssize_t count = Py_ReprEnter( this );
	if (count != 0) {
		/* BIGWORLD(3.13 migration): this was a port of the 2.7 dictrepr
		 * built on PyString_Concat/_PyString_Join, both gone in 3.x.
		 * Assemble the UTF-8 payload field by field and hand it over as
		 * a single unicode object instead. */
		return count > 0 ? PyUnicode_FromString( "{...}" ) : NULL;
	}

	BW::string repr = "{";

	int index = 0;
	for (FieldValues::iterator i = this->fieldValues_.begin();
			i != this->fieldValues_.end();
			++i, ++index)
	{
		if (index > 0)
		{
			repr += ", ";
		}

		repr += this->pDataType_->getFieldName( index );
		repr += ": ";

		PyObjectPtr value = *i;
		PyObjectPtr valueRepr( PyObject_Repr( value.get() ),
			PyObjectPtr::STEAL_REFERENCE );
		if (valueRepr == NULL)
		{
			Py_ReprLeave( this );
			return NULL;
		}

		const char * valueReprStr = PyUnicode_AsUTF8( valueRepr.get() );
		if (valueReprStr == NULL)
		{
			Py_ReprLeave( this );
			return NULL;
		}

		repr += valueReprStr;
	}

	repr += "}";

	PyObject * result = PyUnicode_FromString( repr.c_str() );

	Py_ReprLeave( this );
	return result;
}


/**
 *	This method returns the field name associated with an index.
 */
PyObject * PyFixedDictDataInstance::py_getFieldNameForIndex( PyObject * args )
{
	unsigned int index;

	if (!PyArg_ParseTuple(args, "i", &index))
	{
		return NULL;
	}

	if (index >= pDataType_->getNumFields())
	{
		PyErr_SetString( PyExc_IndexError, "index out of bounds" );
		return NULL;
	}

	return PyUnicode_FromString(pDataType_->getFieldName(index).c_str());
}

BW_END_NAMESPACE

// fixed_dict_data_instance.cpp
