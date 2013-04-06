/*    ___  _________     ____          __         
     / _ )/ __/ ___/____/ __/___ ___ _/_/___ ___ 
    / _  / _// (_ //___/ _/ / _ | _ `/ // _ | -_)
   /____/_/  \___/    /___//_//_|_, /_//_//_|__/ 
                               /___/             

This file is part of the Brute-Force Game Engine, BFG-Engine

For the latest info, see http://www.brute-force-games.com

Copyright (c) 2011 Brute-Force Games GbR

The BFG-Engine is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The BFG-Engine is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with the BFG-Engine. If not, see <http://www.gnu.org/licenses/>.
*/

#include <Network/Client.h>

#include <boost/crc.hpp>
#include <Base/Logger.h>
#include <Network/Event.h>
#include <Network/TcpModule.h>
#include <Network/UdpModule.h>

namespace BFG {
namespace Network{

Client::Client(EventLoop* loop_) :
Emitter(loop_),
mLocalTime(new Clock::StopWatch(Clock::milliSecond))
{
	dbglog << "Client::Client()";
	mLocalTime->start();

	mResolver.reset(new tcp::resolver(mService));
	
	mTimeSyncTimer.reset(new boost::asio::deadline_timer(mService));

	loop()->connect(ID::NE_CONNECT, this, &Client::controlEventHandler);
	loop()->connect(ID::NE_DISCONNECT, this, &Client::controlEventHandler);
	loop()->connect(ID::NE_SHUTDOWN, this, &Client::controlEventHandler);

	mTcpModule.reset(new TcpModule(loop(), mService, 0, mLocalTime));
}

Client::~Client()
{
	dbglog << "Client::~Client()";
	stop();

	loop()->disconnect(ID::NE_CONNECT, this);
	loop()->disconnect(ID::NE_DISCONNECT, this);
	loop()->disconnect(ID::NE_SHUTDOWN, this);
	loop()->disconnect(ID::NE_RECEIVED, this);

	if (mResolver)
		mResolver->cancel();

	mResolver.reset();
}

void Client::stop()
{
	dbglog << "Client::stop";
	
	mUdpModule.reset();
	mTcpModule.reset();
	
	mService.stop();
	mThread.join();
}

void Client::startConnecting(const std::string& ip, const std::string& port)
{
	dbglog << "TcpModule::startConnecting";
	boost::asio::ip::tcp::resolver::query query(ip, port);
	mResolver->async_resolve(query, boost::bind(&Client::resolveHandler, this, _1, _2));
}

void Client::readHandshake()
{
	dbglog << "Client::readHandshake";
	boost::asio::async_read
	(
		mTcpModule->socket(),
		boost::asio::buffer(mHandshakeBuffer),
		boost::asio::transfer_exactly(Handshake::SerializationT::size()),
		bind(&Client::readHandshakeHandler, this, _1, _2)
	);
}

void Client::resolveHandler(const error_code &ec, tcp::resolver::iterator it)
{ 
	dbglog << "TcpModule::resolveHandler";
	if (!ec)
	{
		mTcpModule->socket().async_connect(*it, bind(&Client::connectHandler, this, _1));
	}
	else
		printErrorCode(ec, "resolveHandler");
}

void Client::connectHandler(const error_code &ec)
{
	dbglog << "Client::connectHandler";
	if (!ec) 
	{
		readHandshake();
	}
	else
	{
		printErrorCode(ec, "connectHandler");
	}
}

//! \brief Helper function for UDP endpoint identification on client-side.
//! \return Always 0
static PeerIdT peerIdZero(const UdpModule::EndpointT&)
{
	return 0;
}

void Client::readHandshakeHandler(const error_code &ec, size_t bytesTransferred)
{
	dbglog << "Client::readHandshakeHandler (" << bytesTransferred << ")";
	if (!ec) 
	{
		Handshake hs;
		hs.deserialize(mHandshakeBuffer);
		
		u16 hsChecksum = hs.mChecksum;
		u16 ownHsChecksum = calculateHandshakeChecksum(hs);
		
		if (ownHsChecksum != hsChecksum)
		{
			warnlog << std::hex << std::uppercase 
				<< "readHandshakeHandler: Got bad PeerId (Own CRC: "
				<< ownHsChecksum
				<< " Rcvd CRC: "
				<< hsChecksum
				<< "). Disconnecting Peer.";

			// Peer sends crap? Bye bye!
			// TODO: Notify Application
			mTcpModule->socket().close();
			return;
		}
		else
		{
			dbglog << "Received peer ID: " << hs.mPeerId;
			mPeerId = hs.mPeerId;

			mTcpModule->startReading();

			boost::asio::ip::tcp::endpoint tcpServerEp = mTcpModule->socket().remote_endpoint();
			boost::asio::ip::udp::endpoint udpServerEp(tcpServerEp.address(), tcpServerEp.port());
			boost::asio::ip::udp::endpoint udpLocalEp(udp::endpoint(udp::v4(), RANDOM_PORT));
			mUdpModule.reset(new UdpModule(loop(), mService, mLocalTime, udpLocalEp, udpServerEp, peerIdZero));

			mUdpModule->useServerEndpointAsRemoteEndpoint();
			mUdpModule->startReading();

			emit<ControlEvent>(ID::NE_CONNECTED, mPeerId);

			mTcpModule->sendTimesyncRequest();
			setTimeSyncTimer(TIME_SYNC_WAIT_TIME);
		}
	}
}

void Client::controlEventHandler(ControlEvent* e)
{
	switch(e->getId())
	{
	case ID::NE_CONNECT:
		onConnect(boost::get<EndpointT>(e->getData()));
		break;
	case ID::NE_DISCONNECT:
	case ID::NE_SHUTDOWN:
		onDisconnect(0);
		break;
	default:
		warnlog << "Client: Can't handle event with ID: "
			<< e->getId();
		break;
	}

}

void Client::onConnect(const EndpointT& endpoint)
{
	startConnecting(endpoint.get<0>().data(), endpoint.get<1>().data());
	mThread = boost::thread(boost::bind(&boost::asio::io_service::run, &mService));
}

void Client::onDisconnect(const PeerIdT& peerId)
{
	stop();
	emit<ControlEvent>(ID::NE_DISCONNECTED, peerId);
}

void Client::setTimeSyncTimer(const long& waitTime_ms)
{
	if (waitTime_ms == 0)
		return;

	mTimeSyncTimer->expires_from_now(boost::posix_time::milliseconds(waitTime_ms));
	mTimeSyncTimer->async_wait(boost::bind(&Client::syncTimerHandler, this, _1));
}

void Client::syncTimerHandler(const error_code &ec)
{
	if (!ec)
	{
		mTcpModule->sendTimesyncRequest();
		setTimeSyncTimer(TIME_SYNC_WAIT_TIME);
	}
	else
	{
		printErrorCode(ec, "timerHandler");
	}
}

u16 Client::calculateHandshakeChecksum(const Handshake& hs)
{
	boost::crc_16_type result;
	result.process_bytes(&(hs.mPeerId), sizeof(PeerIdT));
	return result.checksum();
}

void Client::printErrorCode(const error_code &ec, const std::string& method)
{
	warnlog << "[" << method << "] Error Code: " << ec.value() << ", message: " << ec.message();
}

} // namespace Network
} // namespace BFG
