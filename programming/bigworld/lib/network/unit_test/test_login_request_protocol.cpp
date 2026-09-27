#include "pch.hpp"

#include "connection/baseapp_login_request_protocol.hpp"
#include "connection/login_request_protocol.hpp"
#include "connection/loginapp_login_request_protocol.hpp"

#include <string.h>


BW_BEGIN_NAMESPACE


// Each accessor hands back a process-wide singleton, and the two are
// distinct protocols: the LoginApp one must never be confused with the
// BaseApp one.
TEST( LoginRequestProtocol_singletons )
{
	LoginRequestProtocolPtr pForLoginApp =
		LoginRequestProtocol::getForLoginApp();
	LoginRequestProtocolPtr pForBaseApp =
		LoginRequestProtocol::getForBaseApp();

	CHECK( pForLoginApp );
	CHECK( pForBaseApp );

	// Repeated calls return the very same object, not an equal one.
	LoginRequestProtocolPtr pForLoginAppAgain =
		LoginRequestProtocol::getForLoginApp();
	LoginRequestProtocolPtr pForBaseAppAgain =
		LoginRequestProtocol::getForBaseApp();

	CHECK( pForLoginApp.get() == pForLoginAppAgain.get() );
	CHECK( pForBaseApp.get() == pForBaseAppAgain.get() );

	// The two are not the same object, and each is its own protocol.
	CHECK( pForLoginApp.get() != pForBaseApp.get() );
	CHECK( strcmp( pForLoginApp->appName(), "LoginApp" ) == 0 );
	CHECK( strcmp( pForBaseApp->appName(), "BaseApp" ) == 0 );

	// Reference counting is what keeps them alive between calls, so
	// dropping every local handle must not destroy them.
	pForLoginApp = LoginRequestProtocolPtr();
	pForLoginAppAgain = LoginRequestProtocolPtr();
	pForBaseApp = LoginRequestProtocolPtr();
	pForBaseAppAgain = LoginRequestProtocolPtr();

	LoginRequestProtocolPtr pAfterRelease =
		LoginRequestProtocol::getForLoginApp();
	CHECK( pAfterRelease );
	CHECK( strcmp( pAfterRelease->appName(), "LoginApp" ) == 0 );
}

BW_END_NAMESPACE

// test_login_request_protocol.cpp
