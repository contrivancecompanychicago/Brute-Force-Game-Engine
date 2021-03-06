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

#include <Physics/PhysicsManager.h>

#include <sstream>

#include <boost/array.hpp>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>

#include <Base/Logger.h>
#include <Base/Cpp.h>

#include <Physics/Event_fwd.h>
#include <Physics/OdeTriMesh.h>
#include <Physics/PhysicsObject.h>

#include <View/Enums.hh>

namespace BFG {
namespace Physics {

const u32 g_AbsoluteMaximumContactsPerCollision = 512;

void globalOdeNearCollisionCallback(void* additionalData,
                                    dGeomID geo1,
                                    dGeomID geo2)
{
	assert(additionalData);
	PhysicsManager* This = static_cast<PhysicsManager*>(additionalData);
	This->onNearCollision(geo1, geo2);
}

static void odeErrorHandling(int errnum, const char* msg, va_list ap)
{
	boost::array<char, 512> formatted_msg;

	vsnprintf(formatted_msg.data(), formatted_msg.size(), msg, ap);

	errlog << "ODE [errnum: " << errnum << "]: " << formatted_msg.data();
}

static void odeMessageHandling(int errnum, const char* msg, va_list ap)
{
	boost::array<char, 512> formatted_msg;

	vsnprintf(formatted_msg.data(), formatted_msg.size(), msg, ap);

	infolog << "ODE [errnum: " << errnum << "]: " << formatted_msg.data();
}

static void odeDebugHandling(int errnum, const char* msg, va_list ap)
{
	boost::array<char, 512> formatted_msg;

	vsnprintf(formatted_msg.data(), formatted_msg.size(), msg, ap);

	dbglog << "ODE [errnum: " << errnum << "]: " << formatted_msg.data();
}


PhysicsManager::PhysicsManager(Event::Lane& lane, u32 maxContactsPerCollision) :
mLane(lane),
mMaxContactsPerCollision(maxContactsPerCollision),
mSimulationStepSize(0.001f * si::seconds),
mWorldID(0),
mHashSpaceID(0),
mCollisionJointsID(0)
{
	assert(maxContactsPerCollision <= g_AbsoluteMaximumContactsPerCollision);

	dInitODE();

	dSetMessageHandler(odeMessageHandling);
	dSetErrorHandler(odeErrorHandling);
	dSetDebugHandler(odeDebugHandling);

	mWorldID = dWorldCreate();
	
	mHashSpaceID = dHashSpaceCreate(0);
	
	mCollisionJointsID = dJointGroupCreate(0);

	// set ODE Gravity to zero
	dWorldSetGravity(mWorldID, 0.0, 0.0, 0.0f);
	dWorldSetCFM(mWorldID, 1e-5f);
	dWorldSetAutoDisableFlag(mWorldID, true);

	// set error reduction parameter 
	dWorldSetERP(mWorldID, 0.1f);

	registerEvents();

	infolog << "PhysicsManager initialized";
}

PhysicsManager::~PhysicsManager()
{
	// Ensure that this gets done before ODE is closed
	mPhysicsObjects.clear();

	dSpaceDestroy(mHashSpaceID);
	dJointGroupDestroy(mCollisionJointsID);
	dWorldDestroy(mWorldID);
	dCloseODE();

	infolog << "PhysicsManager destroyed";
}

dSpaceID PhysicsManager::getSpaceID() const
{
	return mHashSpaceID;
}

dWorldID PhysicsManager::getWorldID() const
{
	return mWorldID;
}

void PhysicsManager::clear()
{
	mPhysicsObjects.clear();
}

void PhysicsManager::move(Event::TickData tickData)
{
	quantity<si::time, f32> timeSinceLastTick = tickData.timeSinceLastTick();
	
	int steps = int(std::ceil(timeSinceLastTick / mSimulationStepSize));

	/** \note
		On low FPS, time may accumulate which leads to serious timing problems.

		Example:
		- ODE may take a long time, then the next timeSinceLastTick is higher
		- Due to the higher timeSinceLastFrame, Ode takes even longer, etc...
		
		Solutions:
		- At the moment, we're simply limiting the number of ode steps
		- Another solution would be to subtract the time ODE took, from
		  timeSinceLastFrame.
		- Additionally we might increase the mSimulationStepSize on low FPS.
	*/

	// Equal to 5 FPS
	const int maxSteps = 200;
	
	if (steps > maxSteps)
	{
		steps = maxSteps;
	}

	PhysicsObjectMap::iterator it = mPhysicsObjects.begin();
	for (; it != mPhysicsObjects.end(); ++it)
	{
		it->second->performInterpolation(timeSinceLastTick);
	}
	
	for (int i=0; i<steps; i++)
	{
		PhysicsObjectMap::iterator it = mPhysicsObjects.begin();
		for (; it != mPhysicsObjects.end(); ++it)
		{
			it->second->prepareOdeStep(mSimulationStepSize);
		}

		/** \note
			The dSpaceCollide call, which performs collision detection, was
			previously done outside of this `for' loop, which caused collision
			detection errors the lower the framerate was.
			
			Of course, dSpaceCollide should be called after each world step.
			But this is very slow (at least on my machine :-P) so I call
			it every third step for now. For example, this is still better than
			calling it once for 10ms at 100FPS.
			
			Probably, we can optimize a lot here.
		*/
		if (i % 3 == 0)
			dSpaceCollide (mHashSpaceID, this, &globalOdeNearCollisionCallback);

		dWorldQuickStep (mWorldID, mSimulationStepSize.value());

		if (i % 3 == 0)
			dJointGroupEmpty (mCollisionJointsID);
	}

	it = mPhysicsObjects.begin();
	for (; it != mPhysicsObjects.end(); ++it)
	{
		it->second->clearForces();
		it->second->sendDeltas();
		//it->second->sendFullSync();
	}
}

void PhysicsManager::addObject(GameHandle handle,
                               boost::shared_ptr<PhysicsObject> object)
{
	mPhysicsObjects[handle] = object;
}

void PhysicsManager::onNearCollision(dGeomID geo1, dGeomID geo2)
{
	if (dGeomIsSpace (geo1) || dGeomIsSpace (geo2))
	{
		dSpaceCollide2
		(
			geo1,
			geo2,
			this,
			&globalOdeNearCollisionCallback
		);
		return;
	}

	dBodyID b1 = dGeomGetBody(geo1);
	dBodyID b2 = dGeomGetBody(geo2);

	int ConnectedByJoint = dAreConnectedExcluding(b1, b2, dJointTypeContact);

	if (b1 && b2 && ConnectedByJoint == 1)
		return;

	// dCollide does even collide disabled geoms (unlike dSpaceCollide)
	if (!dGeomIsEnabled(geo1) || !dGeomIsEnabled(geo2))
		return;

	collideGeoms(geo1, geo2);
}

void PhysicsManager::collideGeoms(dGeomID geo1, dGeomID geo2) const
{
	dContact contact[g_AbsoluteMaximumContactsPerCollision];
	
	for (u32 i=0; i<mMaxContactsPerCollision; ++i) 
	{
		contact[i].surface.mode = dContactApprox1;

		// 0.1 (metal  on ice) to 0.9 (rubber on pavement), 
		contact[i].surface.mu = 0.25f;
		contact[i].surface.mu2 = 0;
	}

	int collisions = dCollide
	(
		geo1,
		geo2,
		mMaxContactsPerCollision,
		&contact[0].geom,            // Array Ptr
		sizeof(dContact)             // Skip length
	);
	
	if (collisions == 0)
		return;

	dBodyID body1 = dGeomGetBody(geo1);
	dBodyID body2 = dGeomGetBody(geo2);

	GameHandle moduleHandle1 = *reinterpret_cast<GameHandle*>(dGeomGetData(geo1));
	GameHandle moduleHandle2 = *reinterpret_cast<GameHandle*>(dGeomGetData(geo2));

	assert(moduleHandle1 && moduleHandle2);
	
	boost::shared_ptr<PhysicsObject> po1 = findObject(moduleHandle1);
	boost::shared_ptr<PhysicsObject> po2 = findObject(moduleHandle2);
	
	if (po1->getCollisionMode(moduleHandle1) == ID::CM_Ghost &&
	    po2->getCollisionMode(moduleHandle2) == ID::CM_Standard)
	{
		// Causes contact joints to be ignored. No effect for body TWO.
		body2 = NULL;
	}
	else if (po2->getCollisionMode(moduleHandle2) == ID::CM_Ghost &&
	         po1->getCollisionMode(moduleHandle1) == ID::CM_Standard)
	{
		// Causes contact joints to be ignored. No effect for body ONE.
		body1 = NULL;
	}

	// This will apply forces to both bodies at the next world step.
	for (int i=0; i<collisions; ++i) 
	{
		dJointID c = dJointCreateContact
		(
			mWorldID,
			mCollisionJointsID,
			contact+i
		);

		dJointAttach(c, body1, body2);
	}

	float totalPenetrationDepth = 0;
	
	for (int i=0; i<collisions; ++i)
		totalPenetrationDepth += contact[i].geom.depth;

	// If these pointers are valid, then a contact joint was attached and
	// an effect will be issued, therefore we can safely send a notification.
	if (body1)
		po1->notifyAboutCollision(moduleHandle1, moduleHandle2, totalPenetrationDepth);
	
	if (body2)
		po2->notifyAboutCollision(moduleHandle2, moduleHandle1, totalPenetrationDepth);
}

void PhysicsManager::registerEvents()
{
	mLane.connectLoop(this, &PhysicsManager::move);
	mLane.connect(ID::PE_ATTACH_MODULE, this, &PhysicsManager::onAttachModule);
	mLane.connectV(ID::PE_CLEAR, this, &PhysicsManager::clear);
	mLane.connect(ID::PE_CREATE_OBJECT, this, &PhysicsManager::onCreateObject);
	mLane.connect(ID::PE_DELETE_OBJECT, this, &PhysicsManager::onDeleteObject);
	mLane.connect(ID::PE_REMOVE_MODULE, this, &PhysicsManager::onRemoveModule);
	mLane.connect(ID::VE_DELIVER_MESH, this, &PhysicsManager::onMeshDelivery);
	mLane.connect(ID::PE_ATTACH_OBJECT, this, &PhysicsManager::onAttachObject);
    mLane.connect(ID::PE_DETACH_OBJECT, this, &PhysicsManager::onDetachObject);
}

void PhysicsManager::onMeshDelivery(const NamedMesh& namedMesh)
{
	if (mPhysicsObjects.empty())
		return;

	PhysicsObjectMap::iterator it = mPhysicsObjects.begin();

	it->second->onMeshDelivery(namedMesh);

	for (; it != mPhysicsObjects.end(); ++it)
	{
		it->second->createPendingModules();
	}
}

void PhysicsManager::onCreateObject(const ObjectCreationParams& ocp)
{
	GameHandle handle = ocp.get<0>();
	const Location& location = ocp.get<1>();

	boost::shared_ptr<PhysicsObject> po
	(
		new PhysicsObject
		(
			mLane,
			getWorldID(),
			getSpaceID(),
			location
		)
	);
	addObject(handle, po);
	po->sendFullSync();
}

void PhysicsManager::onDeleteObject(GameHandle handle)
{
	PhysicsObjectMap::iterator it = mPhysicsObjects.find(handle);
	assert(it != mPhysicsObjects.end());
	mPhysicsObjects.erase(it);
}

void PhysicsManager::onAttachModule(const ModuleCreationParams& mcp)
{
	PhysicsObjectMap::const_iterator it = mPhysicsObjects.find(mcp.mGoHandle);
	if (it == mPhysicsObjects.end())
	{
		std::stringstream ss;
		ss << "PhysicsObject " << mcp.mGoHandle << " not registered in Manager!";
		throw std::logic_error(ss.str());
	}
	
	boost::shared_ptr<PhysicsObject> po = it->second;
	po->addModule(mcp);
}

void PhysicsManager::onAttachObject(const ObjectAttachmentParams& oap)
{
	boost::shared_ptr<PhysicsObject> parentObject(findObject(oap.get<0>()));
	boost::shared_ptr<PhysicsObject> childObject(findObject(oap.get<1>()));
	parentObject->attachObject(childObject, oap.get<2>(), oap.get<3>());
}

void PhysicsManager::onDetachObject(const ObjectAttachmentParams& oap)
{
	boost::shared_ptr<PhysicsObject> parentObject(findObject(oap.get<0>()));
	boost::shared_ptr<PhysicsObject> childObject(findObject(oap.get<1>()));

	parentObject->detachObject(childObject, oap.get<2>(), oap.get<3>());
}

boost::shared_ptr<PhysicsObject> PhysicsManager::findObject(GameHandle objectHandle) const
{
	boost::shared_ptr<PhysicsObject> result;
	BOOST_FOREACH(const PhysicsObjectMap::value_type& vt, mPhysicsObjects)
	{
		if (vt.second->hasModule(objectHandle))
		{
			result = vt.second;
			break;
		}
	}

	if (! result)
	{
		std::stringstream ss;
		ss << "PhysicsObject " << objectHandle << " not registered in Manager!";
		throw std::logic_error(ss.str());
	}

	return result;
}


void PhysicsManager::onRemoveModule(const ModuleRemovalParams& mcp)
{
	warnlog << "STUB: PhysicsManager::onRemoveModule(const ModuleRemovalParams& mcp)";
}

} // namespace Physics
} // namespace BFG
