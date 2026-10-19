#ifndef AUTHENTICATE_ACCOUNT_HANDLER_HPP
#define AUTHENTICATE_ACCOUNT_HANDLER_HPP

#include "db_storage/billing_system.hpp"

#include "network/misc.hpp"

#if defined( _WIN32 )
// MSVC's SDK leaves `#define interface struct` (rpcndr.h via d3d9.h / shlobj /
// shlwapi / d3d10) live for the rest of the TU, which rewrites the
// `interface`-named declarations in this file into syntax errors (C2332/C2027
// in the root superproject build). Off for this file only; the pop below
// restores it for whatever SDK COM header comes later in the TU.
#pragma push_macro( "interface" )
#undef interface
#endif


BW_BEGIN_NAMESPACE

class BillingSystem;

namespace Mercury
{
class NetworkInterface;
class UnpackedMessageHeader;
}


/**
 *	This class is used to handle a message to authenticateAccount.
 */
class AuthenticateAccountHandler : public IGetEntityKeyForAccountHandler
{
public:
	virtual ~AuthenticateAccountHandler() {}

	static bool handle( const Mercury::Address & srcAddr,
			const Mercury::UnpackedMessageHeader & header,
			BinaryIStream & data,
			BillingSystem & billingSystem,
			Mercury::NetworkInterface & interface );

private:
	AuthenticateAccountHandler( const Mercury::Address & replyAddr,
			Mercury::ReplyID replyID, Mercury::NetworkInterface & interface );

	// IGetEntityKeyForAccountHandler overrides
	virtual void onGetEntityKeyForAccountSuccess(
			const EntityKey & ekey,
			const BW::string & dataForClient,
			const BW::string & dataForBaseEntity );

	virtual void onGetEntityKeyForAccountLoadFromUsername(
			EntityTypeID typeID,
			const BW::string & username,
			bool shouldCreateNewOnLoadFailure,
			bool shouldRememberNewOnLoadFailure,
			const BW::string & dataForClient,
			const BW::string & dataForBaseEntity );

	virtual void onGetEntityKeyForAccountCreateNew(
			EntityTypeID typeID,
			bool shouldRemember,
			const BW::string & dataForClient,
			const BW::string & dataForBaseEntity );

	virtual void onGetEntityKeyForAccountFailure(
			LogOnStatus status,
			const BW::string & errorMsg );

	void sendFailure( LogOnStatus status, const BW::string & errorMsg );

	Mercury::Address replyAddr_;
	Mercury::ReplyID replyID_;
	Mercury::NetworkInterface & interface_;
};

BW_END_NAMESPACE


#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
#endif // AUTHENTICATE_ACCOUNT_HANDLER_HPP
