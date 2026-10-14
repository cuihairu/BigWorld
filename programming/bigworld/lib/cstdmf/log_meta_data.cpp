#include "pch.hpp"

#include "log_meta_data.hpp"

#include "concurrency.hpp"
#include "memory_stream.hpp"

BW_BEGIN_NAMESPACE

/*static*/ const char * LogMetaData::key_stackTrace = "__bw_stack";
/*static*/ const char * LogMetaData::key_jsonString = "__bw_json";

/*static*/ const LogMetaData::MessageLoggerValueType LogMetaData::MLVT_JSON_STRING	= 'j';
/*static*/ const LogMetaData::MessageLoggerValueType LogMetaData::MLVT_INT			= 'i';
/*static*/ const LogMetaData::MessageLoggerValueType LogMetaData::MLVT_UNSIGNED_INT	= 'u';
/*static*/ const LogMetaData::MessageLoggerValueType LogMetaData::MLVT_LONG_LONG_INT	= 'l';
/*static*/ const LogMetaData::MessageLoggerValueType LogMetaData::MLVT_STRING			= 's';
/*static*/ const LogMetaData::MessageLoggerValueType LogMetaData::MLVT_FLOAT			= 'f';
/*static*/ const LogMetaData::MessageLoggerValueType LogMetaData::MLVT_DOUBLE			= 'd';


/**
 *	Default Constructor.
 */
LogMetaData::LogMetaData() :
	pData_( nullptr ), pRawJSON_( nullptr )
{ }


/**
 *	Destructor.
 */
LogMetaData::~LogMetaData()
{
	bw_safe_delete( pData_ );
}


/**
 *	This method streams the provided string as a special JSON string.
 *
 *	This method is only intended to be utilised as a support to the Python
 *	API layer which currently passes JSON through directly.
 *
 *	@param pJSON A pointer to the JSON string to stream. Must remain allocated
 *  for the lifetime of this LogMetaData object.
 */
void LogMetaData::addJSON( const char * pJSON )
{
	// TODO: This could possibly become an MF_ASSERT_DEV?
	if (!pJSON)
	{
		return;
	}

	// TODO: if an empty string is sent through, it seems to break the ML
	//       receiving side. Need to do a GDB here to track down what is
	//       causing this. for now just don't send it.
	if (strlen( pJSON ) == 0)
	{
		return;
	}

	// Store the raw string value in case an older message logger wants it
	if (pRawJSON_)
	{
		WARNING_MSG( "LogMetaData::addJSON: raw JSON has already been added, "
				"ignoring additional attempts. Existing value is: %s\n",
				pRawJSON_ );
		return;
	}
	// Not taking a copy because the Python call currently guarantees that it
	// will be allocated for as long as we need it.
	pRawJSON_ = pJSON;

	if (pData_ == NULL)
	{
		pData_ = new MemoryOStream;
	}


	(*pData_) <<
		// The key
		MLVT_STRING << LogMetaData::key_jsonString <<
		// The value
		MLVT_JSON_STRING << pJSON;
}

/**
 * This method returns a pointer to the raw JSON string passed in via addJSON.
 * Only returns a value if one was passed in, and does not translate
 * entries that were added via streamValue.
 *
 * @return A pointer to the string.
 */
const char * LogMetaData::getRawJSON() const
{
	return pRawJSON_;
}

/**
 *	This method applies the specified key to the metadata stream.
 *
 *	@param pKey The key to stream into the metadata.
 */
void LogMetaData::streamKey( const char * pKey )
{
	if (pData_ == NULL)
	{
		pData_ = new MemoryOStream;
	}

	(*pData_) << MLVT_STRING << pKey;
}


/**
 *	This method streams the provided integer to the metadata stream.
 *
 *	@param value The value to stream into the metadata.
 */
void LogMetaData::streamValue( int value )
{
	(*pData_) << MLVT_INT << value;
}


/**
 *	This method streams the provided long long to the metadata stream.
 *
 *	@param value The value to stream into the metadata.
 */
void LogMetaData::streamValue( long long value )
{
	(*pData_) << MLVT_LONG_LONG_INT << value;
}


/**
 *	This method streams the provided unsigned integer to the metadata stream.
 *
 *	@param value The value to stream into the metadata.
 */
void LogMetaData::streamValue( unsigned int value )
{
	(*pData_) << MLVT_UNSIGNED_INT << value;
}


/**
 *	This method streams the provided string to the metadata stream.
 *
 *	If a NULL pointer is provided, this method will stream an empty string
 *	in its place.
 *
 *	@param value The value to stream into the metadata.
 */
void LogMetaData::streamValue( const char * pValue )
{
	if (pValue)
	{
		(*pData_) << MLVT_STRING << pValue;
	}
	else
	{
		// TODO: potentially should consider this an MF_ASSERT_DEV case
		//       however, we are already within a message log stack at
		//       this point. do we want to start a new stack process?
		(*pData_) << MLVT_STRING << "";
	}
}


/**
 *	This method streams the provided float to the metadata stream.
 *
 *	@param value The value to stream into the metadata.
 */
void LogMetaData::streamValue( float value )
{
	(*pData_) << MLVT_FLOAT << value;
}


/**
 *	This method streams the provided double to the metadata stream.
 *
 *	@param value The value to stream into the metadata.
 */
void LogMetaData::streamValue( double value )
{
	(*pData_) << MLVT_DOUBLE << value;
}


/**
 *	This method returns a pointer to the MemoryOStream containing the metadata.
 *
 *	@return A pointer to the metadata stream.
 */
const MemoryOStream * LogMetaData::pData() const
{
	return pData_;
}


// -----------------------------------------------------------------------------
// Section: LogCorrelation
// -----------------------------------------------------------------------------

namespace
{
// Trivially constructible storage so the context stays in static
// thread-local storage (see THREADLOCAL in concurrency.hpp).
THREADLOCAL( LogCorrelation::Field ) s_correlationFields[
	LogCorrelation::MAX_FIELDS ];
THREADLOCAL( int ) s_correlationFieldCount = 0;


/**
 *	This function finds the field with the given key in this thread's context.
 *
 *	@return The field, or NULL if no field has this key.
 */
LogCorrelation::Field * findCorrelationField( const char * pKey )
{
	const int count = s_correlationFieldCount;

	for (int i = 0; i < count; ++i)
	{
		if (strcmp( s_correlationFields[ i ].key, pKey ) == 0)
		{
			return &s_correlationFields[ i ];
		}
	}

	return NULL;
}

} // anonymous namespace


const int LogCorrelation::MAX_FIELDS;
const int LogCorrelation::MAX_KEY_LENGTH;


/**
 *	This static method sets (or replaces) the field with the given key on
 *	this thread's context.
 *
 *	@param pKey		The field key. Empty keys are ignored.
 *	@param value	The field value.
 */
void LogCorrelation::set( const char * pKey, long long value )
{
	// Match LogMetaData::add and ignore empty keys.
	if ((pKey == NULL) || (pKey[0] == '\0'))
	{
		return;
	}

	Field * pField = findCorrelationField( pKey );

	if (pField == NULL)
	{
		if (s_correlationFieldCount >= MAX_FIELDS)
		{
			WARNING_MSG( "LogCorrelation::set: context is full (%d fields), "
					"dropping '%s'\n",
				MAX_FIELDS, pKey );
			return;
		}

		pField = &s_correlationFields[ s_correlationFieldCount++ ];
	}

	// Keys are expected to be short literals; anything longer is truncated.
	size_t keyLength = strlen( pKey );
	if (keyLength > MAX_KEY_LENGTH)
	{
		keyLength = MAX_KEY_LENGTH;
	}

	memcpy( pField->key, pKey, keyLength );
	pField->key[ keyLength ] = '\0';
	pField->value = value;
}


/**
 *	This static method removes the field with the given key.
 *
 *	@param pKey		The field key. Absent keys are a no-op.
 */
void LogCorrelation::clear( const char * pKey )
{
	if (pKey == NULL)
	{
		return;
	}

	Field * pField = findCorrelationField( pKey );

	if (pField != NULL)
	{
		// Move the last field over the removed one.
		--s_correlationFieldCount;
		*pField = s_correlationFields[ s_correlationFieldCount ];
	}
}


/**
 *	This static method removes all fields from this thread's context.
 */
void LogCorrelation::clearAll()
{
	s_correlationFieldCount = 0;
}


/**
 *	This static method returns whether any field is set on this thread's
 *	context.
 *
 *	@return True if any field is set, false otherwise.
 */
bool LogCorrelation::hasFields()
{
	return s_correlationFieldCount > 0;
}


/**
 *	This static method appends this thread's context fields to the given
 *	metadata. An empty context leaves the metadata untouched.
 *
 *	@param metaData	The metadata to append to.
 */
void LogCorrelation::appendTo( LogMetaData & metaData )
{
	const int count = s_correlationFieldCount;

	for (int i = 0; i < count; ++i)
	{
		metaData.add( s_correlationFields[ i ].key,
			s_correlationFields[ i ].value );
	}
}


/**
 *	This static method snapshots the context, for ScopedLogCorrelation.
 *
 *	@param pFields	Receives the fields. Must have room for MAX_FIELDS.
 *	@param count	Receives the number of fields copied out.
 */
void LogCorrelation::save( Field * pFields, int & count )
{
	count = s_correlationFieldCount;
	memcpy( pFields, s_correlationFields, sizeof( Field ) * count );
}


/**
 *	This static method restores a snapshot taken by save().
 *
 *	@param pFields	The fields to restore.
 *	@param count	The number of fields to restore.
 */
void LogCorrelation::restore( const Field * pFields, int count )
{
	memcpy( s_correlationFields, pFields, sizeof( Field ) * count );
	s_correlationFieldCount = count;
}


// -----------------------------------------------------------------------------
// Section: ScopedLogCorrelation
// -----------------------------------------------------------------------------

/**
 *	Constructor. Snapshots this thread's correlation context.
 */
ScopedLogCorrelation::ScopedLogCorrelation()
{
	LogCorrelation::save( savedFields_, savedCount_ );
}


/**
 *	Destructor. Restores the snapshotted correlation context.
 */
ScopedLogCorrelation::~ScopedLogCorrelation()
{
	LogCorrelation::restore( savedFields_, savedCount_ );
}

BW_END_NAMESPACE

// log_meta_data.cpp
