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

#ifndef BFG_NETWORKEVENT_H
#define BFG_NETWORKEVENT_H

#include <EventSystem/Event.h>
#include <Network/Event_fwd.h>

namespace BFG {
namespace Network {

std::string NETWORK_API debug(const NetworkPacketEvent& e)
{
	std::stringstream ss;
		
	ss << "e.mId: " << e.getId() << "\n";
	ss << "e.mDestination: " << e.mDestination << "\n";
	ss << "e.mSender: " << e.mSender << "\n";

	const NetworkPayloadType& payload = e.getData();
	ss << "payload.AppId: " << payload.get<0>() << "\n";
	ss << "payload.Destination: " << payload.get<1>() << "\n";
	ss << "payload.Sender: " << payload.get<2>() << "\n";
	ss << "payload.PacketSize: " << payload.get<3>();
	
	return ss.str();
}

} // namespace Network
} // namespace BFG

#endif
