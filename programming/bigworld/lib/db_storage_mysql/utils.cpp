#include "script/first_include.hpp"

#include "utils.hpp"

#include "comma_sep_column_names_builder.hpp"

#include "cstdmf/binary_stream.hpp"


BW_BEGIN_NAMESPACE

BW::string buildCommaSeparatedQuestionMarks( int num )
{
	if (num <= 0)
		return BW::string();

	BW::string list;
	list.reserve( (num * 2) - 1 );
	list += '?';

	for ( int i=1; i<num; i++ )
	{
		list += ",?";
	}
	return list;
}


/**
 * 	Stores the default value of a sequence mapping into the stream.
 */
void defaultSequenceToStream( BinaryOStream & strm, int seqSize,
		const PropertyMappingPtr & pChildMapping_ )
{
	if (seqSize == 0)	// Variable-sized sequence
	{
		strm << int(0);	// Default value is no elements
	}
	else
	{
		for ( int i = 0; i < seqSize; ++i )
		{
			pChildMapping_->defaultToStream( strm );
		}
	}
}

BW::string createInsertStatement( const BW::string & tbl,
		const PropertyMappings & properties )
{
	BW::string stmt = "INSERT INTO " + tbl + " (";
	CommaSepColNamesBuilder colNames( properties );
	stmt += colNames.getResult();
	int numColumns = colNames.getCount();
	stmt += ") VALUES (";

	stmt += buildCommaSeparatedQuestionMarks( numColumns );
	stmt += ')';

	return stmt;
}

BW::string createExplicitInsertStatement( const BW::string & tbl,
		const PropertyMappings & properties )
{
	BW::string stmt = "INSERT INTO " + tbl + " (";
	CommaSepColNamesBuilder colNames( properties );
	stmt += colNames.getResult();
	int numColumns = colNames.getCount();
	stmt += ", id) VALUES (";

	stmt += buildCommaSeparatedQuestionMarks( numColumns + 1 );
	stmt += ')';

	return stmt;
}


BW::string createUpdateStatement( const BW::string& tbl,
		const PropertyMappings& properties )
{
	BW::string stmt = "UPDATE " + tbl + " SET ";
	CommaSepColNamesBuilderWithSuffix colNames( properties, "=?" );
	stmt += colNames.getResult();

	if (colNames.getCount() == 0)
	{
		return BW::string();
	}

	stmt += " WHERE id=?";

	return stmt;
}

BW::string createSelectStatement( const BW::string & tbl,
		const PropertyMappings & properties,
		const BW::string & where )
{
	BW::string stmt = "SELECT id,";

	CommaSepColNamesBuilder colNames( properties );
	stmt += colNames.getResult();

	if (colNames.getCount() == 0)
	{
		stmt.resize( stmt.length() - 1 );	// remove comma
	}

	stmt += " FROM " + tbl;

	if (where.length())
	{
		stmt += " WHERE " + where;
	}

	return stmt;
}


bool isValidIdentifier( const BW::string & name )
{
	// Table names are built as "tbl_" + entity type name, so restrict the
	// identifier to what MySQL accepts unquoted: [A-Za-z_][A-Za-z0-9_]*.
	// Plain ASCII checks avoid isalpha()'s undefined behaviour on chars
	// with the high bit set.
	bool isFirst = true;

	for (size_t i = 0; i < name.size(); ++i)
	{
		const unsigned char c = name[ i ];
		const bool isLetter = (97 <= c && c <= 122) || (65 <= c && c <= 90);
		const bool isDigit = (48 <= c && c <= 57);

		if (isFirst)
		{
			if (!isLetter)
			{
				return false;
			}
			isFirst = false;
		}
		else if (!isLetter && !isDigit && (c != '_'))
		{
			return false;
		}
	}

	return !isFirst;
}

BW_END_NAMESPACE

// utils.cpp
