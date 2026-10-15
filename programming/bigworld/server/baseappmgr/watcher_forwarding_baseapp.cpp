#include "watcher_forwarding_baseapp.hpp"
#include "cstdmf/watcher_path_request.hpp"
#include "server/watcher_forwarding_collector.hpp"

#include "baseappmgr.hpp"
#include "baseapp.hpp"
#include "baseapp/baseapp_int_interface.hpp"

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

// Overridden from ForwardingCollector
ForwardingCollector *BAForwardingWatcher::newCollector(
	WatcherPathRequestV2 & pathRequest,
	const BW::string & destWatcher,
	const BW::string & targetInfo )
{
	ForwardingCollector *collector =
		new ForwardingCollector( pathRequest, destWatcher );
	if (collector == NULL)
	{
		ERROR_MSG( "newCollector: Failed to create new watcher "
					"ForwardingCollector.\n" );
		return NULL;
	}

	AddressList *targetList = this->getComponentAddressList( targetInfo );

	if (!collector->init( BaseAppIntInterface::callWatcher,
		BaseAppMgr::instance().interface(), targetList ))
	{
		ERROR_MSG( "BAForwardingWatcher::newCollector: Failed to initialise "
					"ForwardingCollector.\n" );
		delete collector;
		collector = NULL;

		if (targetList)
			delete targetList;
	}

	return collector;
}


/**
 * Generate a list of BaseApp(s) (Component ID, Address pair) as determined by
 * the target information for the forwarding watchers.
 * 
 * @param targetInfo A string describing which component(s) to add to the list.
 *
 * @returns A list of AddressPair's to forward a watcher request to.
 */
AddressList * BAForwardingWatcher::getComponentAddressList(
	const BW::string & targetInfo )
{
	if (targetInfo == TARGET_LEAST_LOADED)
	{
		return this->listLeastLoaded(
				true /*useBaseApp*/, false /*useServiceApp*/ );
	}
	else if (targetInfo == TARGET_BASE_SERVICE_APPS)
	{
		return this->listAll(
				true /*useBaseApp*/, true /*useServiceApp*/ );
	}
	else if (targetInfo == TARGET_BASE_APPS)
	{
		return this->listAll(
				true /*useBaseApp*/, false /*useServiceApp*/ );
	}
	else if (targetInfo == TARGET_SERVICE_APPS)
	{
		return this->listAll(
				false /*useBaseApp*/, true /*useServiceApp*/ );
	}

	return NULL;
}


/**
 *	This method lists the least loaded BaseApp and/or ServiceApp.
 *
 *	@return A list containing the least loaded app.
 */
AddressList * BAForwardingWatcher::listLeastLoaded( bool useBaseApp,
		bool useServiceApp ) const
{
	AddressList * list = new AddressList;

	BaseAppMgr & baseAppMgr = BaseAppMgr::instance();
	const BaseApps & baseApps = baseAppMgr.baseApps();
	BaseApps::const_iterator iter = baseApps.begin();

	const BaseApp * pLeast = NULL;

	while (iter != baseApps.end())
	{
		BaseAppPtr pCurr = iter->second;
		++iter;

		if ((!pCurr->isServiceApp() && !useBaseApp) ||
			(pCurr->isServiceApp() && !useServiceApp))
		{
			continue;
		}

		if (pLeast == NULL || pCurr->load() <= pLeast->load())
		{
			pLeast = pCurr.get();
			if (almostZero( pLeast->load() ))
			{
				break;
			}
		}
	}

	if (pLeast != NULL)
	{
		list->push_back( this->addressPair( *pLeast ) );
	}

	return list;
}


/**
 *	This method returns a list of all BaseApps and/or ServiceApps.
 *
 *	@return List of all BaseApps and/or ServiceApps.
 */
AddressList * BAForwardingWatcher::listAll( bool useBaseApp,
		bool useServiceApp ) const
{
	AddressList * list = new AddressList;

	BaseAppMgr & baseAppMgr = BaseAppMgr::instance();
	const BaseApps & baseApps = baseAppMgr.baseApps();
	BaseApps::const_iterator iter = baseApps.begin();

	while (iter != baseApps.end())
	{
		BaseApp & app = *iter->second;

		if ((!app.isServiceApp() && useBaseApp) ||
			(app.isServiceApp() && useServiceApp))
		{
			list->push_back( this->addressPair( app ) );
		}

		++iter;
	}

	return list;
}


/**
 *	This method returns an AddressPair for a BaseApp.
 */
AddressPair BAForwardingWatcher::addressPair( const BaseApp & baseApp ) const
{
	return AddressPair( baseApp.addr(),
			Component(
				baseApp.isServiceApp() ? SERVICE_APP : BASE_APP,
				baseApp.id() ) );
}


BW_END_NAMESPACE

// watcher_forwarding_baseapp.cpp

#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
