#ifndef TIME_KEEPER_HPP
#define TIME_KEEPER_HPP

#include "network/network_interface.hpp"

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

/**
 *	This class keeps track of tick times and makes sure they are synchronised
 *	with clocks running in other other places around the system.
 */
class TimeKeeper : public TimerHandler,
	public Mercury::ShutdownSafeReplyMessageHandler
{
public:
	TimeKeeper( Mercury::NetworkInterface & interface,
			TimerHandle trackingTimerHandle, GameTime & gameTime,
			int idealTickFrequency );

	TimeKeeper( Mercury::NetworkInterface & interface,
			TimerHandle trackingTimerHandle, GameTime & gameTime,
			int idealTickFrequency,
			const Mercury::Address & masterAddress,
			const Mercury::InterfaceElement * pMasterRequest,
			int staggerIndex = 0, float maxStagger = 0.0f );

	virtual ~TimeKeeper();

	bool inputMasterReading( double reading );

	double readingAtLastTick() const;
	double readingNow() const;
	double readingAtNextTick() const;

	void synchroniseWithPeer( const Mercury::Address & address,
			const Mercury::InterfaceElement & request );
	void synchroniseWithMaster();

	void masterAddress( const Mercury::Address & addr )
	{
		masterAddress_ = addr;
	}

private:
	int64 offsetOfReading( double reading, uint64 stampsAtReceiptExt );

	void scheduleSyncCheck();

	virtual void handleTimeout( TimerHandle handle, void * arg );

	virtual void handleMessage( const Mercury::Address & source,
		Mercury::UnpackedMessageHeader & header, BinaryIStream & data, void * );
	virtual void handleException( const Mercury::NubException &, void * );

	const Mercury::EventDispatcher & dispatcher() const
		{ return interface_.mainDispatcher(); }

	Mercury::EventDispatcher & dispatcher()
		{ return interface_.mainDispatcher(); }

	Mercury::NetworkInterface & interface_;
	TimerHandle trackingTimerHandle_;

	GameTime & gameTime_;
	double idealTickFrequency_;

	uint64 nominalIntervalStamps_;
	TimerHandle syncCheckTimerHandle_;

	bool isMaster_;
	Mercury::Address masterAddress_;
	const Mercury::InterfaceElement	* pMasterRequest_;

	uint64 lastSyncRequestStamps_;

	double staggerOffset_;
};

BW_END_NAMESPACE


#if defined( _WIN32 )
#pragma pop_macro( "interface" )
#endif
#endif // TIME_KEEPER_HPP
