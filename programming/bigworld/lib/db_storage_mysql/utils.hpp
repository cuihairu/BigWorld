#include "cstdmf/bw_string.hpp"

#include "mappings/property_mapping.hpp"


BW_BEGIN_NAMESPACE

class BinaryOStream;

BW::string buildCommaSeparatedQuestionMarks( int num );

void defaultSequenceToStream( BinaryOStream & strm, int seqSize,
		const PropertyMappingPtr & pChildMapping_ );

BW::string createInsertStatement( const BW::string & tbl,
		const PropertyMappings & properties );

BW::string createExplicitInsertStatement( const BW::string & tbl,
		const PropertyMappings & properties );

BW::string createUpdateStatement( const BW::string & tbl,
		const PropertyMappings & properties );

BW::string createSelectStatement( const BW::string & tbl,
		const PropertyMappings & properties,
		const BW::string & where );

/**
 *	Tests whether the given string is a valid MySQL identifier that can be
 *	spliced directly into a query statement: letters, digits and underscores
 *	only, starting with a letter. Used to keep table names and entity type
 *	names that come from the database out of statement text (CR-P2-24).
 */
bool isValidIdentifier( const BW::string & name );

BW_END_NAMESPACE

// mysql_utils.hpp
