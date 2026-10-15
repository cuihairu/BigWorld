#include "authenticate_account_handler.hpp"

#include "cstdmf/binary_stream.hpp"

#include "network/network_interface.hpp"
#include "network/smart_bundles.hpp"
#include "network/unpacked_message_header.hpp"

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

// -----------------------------------------------------------------------------
// Section: AuthenticateAccountHandler
// -----------------------------------------------------------------------------

/**
 *	This method handles the DBAppInterface::authenticateAccount message.
 */
bool AuthenticateAccountHandler::handle( const Mercury::Address & srcAddr,
			const Mercury::UnpackedMessageHeader & header,
			BinaryIStream & data,
			BillingSystem & billingSystem,
			Mercury::NetworkInterface & interface )
{
	BW::string username;
	BW::string password;

	data >> username >> password;

	Mercury::Address clientAddr( 0, 0 );

	AuthenticateAccountHandler * pHandler =
		new AuthenticateAccountHandler( srcAddr, header.replyID, interface );

	billingSystem.getEntityKeyForAccount( username, password, clientAddr,
			*pHandler );

	return true;
}


/**
 *	Constructor.
 */
AuthenticateAccountHandler::AuthenticateAccountHandler(
		const Mercury::Address & replyAddr, Mercury::ReplyID replyID,
		Mercury::NetworkInterface & interface ) :
	replyAddr_( replyAddr ),
	replyID_( replyID ),
	interface_( interface )
{
}


/**
 *
 */
void AuthenticateAccountHandler::onGetEntityKeyForAccountSuccess(
		const EntityKey & ekey,
		const BW::string & dataForClient,
		const BW::string & dataForBaseEntity )
{
	// TODO: Send billingData back in the reply?
	Mercury::AutoSendBundle spBundle( interface_, replyAddr_ );

	spBundle->startReply( replyID_ );
	(*spBundle) << uint8( LogOnStatus::LOGGED_ON ) <<
		ekey.typeID << ekey.dbID;
}


/**
 *
 */
void AuthenticateAccountHandler::onGetEntityKeyForAccountLoadFromUsername(
		EntityTypeID typeID,
		const BW::string & username,
		bool shouldCreateNewOnLoadFailure,
		bool shouldRememberUnknown,
		const BW::string & dataForClient,
		const BW::string & dataForBaseEntity )
{
	this->sendFailure( LogOnStatus::LOGIN_REJECTED_DB_GENERAL_FAILURE,
			"Authenticate via Base is configured" );
	delete this;
}


/**
 *
 */
void AuthenticateAccountHandler::onGetEntityKeyForAccountCreateNew(
		EntityTypeID typeID, bool shouldRemember,
		const BW::string & dataForClient, const BW::string & dataForBaseEntity )
{
	this->sendFailure( LogOnStatus::LOGIN_REJECTED_NO_SUCH_USER,
			"Requested to created a new user" );
	delete this;
}


/**
 *
 */
void AuthenticateAccountHandler::onGetEntityKeyForAccountFailure(
		LogOnStatus status, const BW::string & errorMsg )
{
	this->sendFailure( status, errorMsg );
	delete this;
}


/**
 *
 */
void AuthenticateAccountHandler::sendFailure( LogOnStatus status,
		const BW::string & errorMsg )
{
	MF_ASSERT( status != LogOnStatus::LOGGED_ON );

	Mercury::AutoSendBundle spBundle( interface_, replyAddr_ );

	spBundle->startReply( replyID_ );
	*spBundle << uint8( status.value() ) << errorMsg;
}

BW_END_NAMESPACE

// authenticate_user_handler.cpp

#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
