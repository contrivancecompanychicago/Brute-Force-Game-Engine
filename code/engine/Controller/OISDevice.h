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

#ifndef _OISDEVICE_H_
#define _OISDEVICE_H_

#include <iostream>

#include <boost/shared_ptr.hpp>
#include <boost/foreach.hpp>

#include <Base/Logger.h>

#include <Controller/Axis.h>
#include <Controller/Delegater.h>
#include <Controller/OISUtils.h>

namespace BFG {
namespace Controller_ {
namespace Adapters {

namespace detail
{

/** This is a function object which works as a 'deleter' and gets called by a
	boost::shared_ptr<OIS::Object> when it finally deletes its pointer. This
	sounds trivial, but may be problematic for proper destruction, because
	the correct OIS::InputManager is needed to free the OIS::Object (e.g. like
	OIS::Mouse).
*/
struct DeviceDeleter
{
	explicit DeviceDeleter(boost::shared_ptr<OIS::InputManager> mIM) :
		mIM(mIM)
	{
	}

	void operator()(OIS::Object* O)
	{
		mIM->destroyInputObject(O);
	}

private:
	boost::shared_ptr<OIS::InputManager> mIM;
};

//! Convenience overload
template <typename T>
AxisData<T>& operator << (AxisData<T>& lhs, const OIS::Axis& rhs)
{
	lhs.abs = rhs.abs;
	lhs.rel = rhs.rel;
	return lhs;
}

/**	EventCatcher is a class with parameterized inheritance. Only functions
	which are actually called, get generated by the compiler.

	\tparam OISCallbackT This is the device from where events are generated.
	                     At the moment, only OIS::Keyboard, OIS::Mouse and
	                     OIS::Joystick are considered.

	\tparam DelegaterT   See description of template parameter DelegaterT in
	                     class OISDevice.
*/
template
<
	typename OISCallbackT,
	typename DelegaterT
>
struct EventCatcher : public OISCallbackT
{
	EventCatcher()
	{}

	void addTarget(DelegaterT* target)
	{
		assert(target && "Delegater target pointer invalid!");
		
		typename DelegaterContainerT::iterator it =
			std::find(mTargets.begin(), mTargets.end(), target);
		
		assert(it == mTargets.end() && "Delegater Target must not exist yet!");
		
		mTargets.push_back(target);
	}
	
	void removeTarget(DelegaterT* target)
	{
		assert(target && "Delegater target pointer invalid!");

		typename DelegaterContainerT::iterator it =
			std::find(mTargets.begin(), mTargets.end(), target);

		assert(it != mTargets.end() && "Delegater Target must exist!");

		mTargets.erase(it);
	}

private:

	// Mouse
	// =====

	bool mouseMoved(const OIS::MouseEvent& e)
	{
		/** \note Bad OIS design: There's no ID which we could use for
		          distinguishing button events from movement events.
		*/

		assert(!mTargets.empty());

		if (e.state.Z.rel != 0)  // Mouse wheel moved
		{
			AxisData<s32> z(2);
			z << e.state.Z;
			
			delegateToTargets(ID::DT_Mouse, ID::AT_Normal, z);
		}
		else                     // Ordinary movement
		{
			AxisData<s32> x(0);
			AxisData<s32> y(1);
			
			x << e.state.X;
			y << e.state.Y;

			delegateToTargets(ID::DT_Mouse, ID::AT_Normal, x);
			delegateToTargets(ID::DT_Mouse, ID::AT_Normal, y);
		}

		return true;
	}

	bool mousePressed(const OIS::MouseEvent& e, OIS::MouseButtonID id)
	{
		/** \note Again OIS! Why didn't they put the MouseButtonID into
		          the MouseEvent like they did with KeyEvent?
		 */

		assert(!mTargets.empty());

		AxisData<s32> x(0);
		AxisData<s32> y(1);

		x << e.state.X;
		y << e.state.Y;
	
		ButtonCodeT code = static_cast<ButtonCodeT>(id);
	
		delegateToTargets(ID::DT_Mouse, ID::BS_Pressed, code);
		return true;
	}

	bool mouseReleased(const OIS::MouseEvent& e, OIS::MouseButtonID id)
	{
		assert(!mTargets.empty());
		
		AxisData<s32> x(0);
		AxisData<s32> y(1);

		x << e.state.X;
		y << e.state.Y;
		
		ButtonCodeT code = static_cast<ButtonCodeT>(id);
		
		delegateToTargets(ID::DT_Mouse, ID::BS_Released, code);
		return true;
	}

	// Keyboard
	// ========
	
	/**
		e.key is useless, but the only way to get special keys like F1-F12.
		      These special keys need to be translated to values above 0xFF
		      or they would collide with ASCII values.

		e.text is ASCII and localized, but 0 in case of a special character.
		       Oh and btw: e.text is always 0 on keyReleased() that's why
		       we need this lookup table.
	*/
	std::map<OIS::KeyCode, unsigned> mLookupTable;


	bool keyPressed(const OIS::KeyEvent& e)
	{
		assert(!mTargets.empty());
		
		/** \note
			It may happen that OIS tries to set the e.text variable to some
			kind of trade-off. Example: if keypad plus is pressed, this var
			will be set to the ascii value of '+' which hinders us to use
			the special key value (because it's not zero, but set to some
			other unrelated value). That's why we need to try to translate
			e.key first and if we can't, we'll use the ascii value (e.text).
		*/
		
		ID::KeyboardButton kb;
		bool successfully_translated = Utils::translateKey(e.key, kb);
		
		// Cast enum value to universal button type
		ButtonCodeT button = static_cast<ButtonCodeT>(kb);

		if (! successfully_translated)
		{
			if (e.text)
			{
				button = e.text;
			}
			else
			{
				// Got no key code at all
				warnlog << "Controller: No translation for OIS::KeyEvent\n"
				        << " e.key = 0x" << std::hex << (int) e.key << "\n"
						<< " e.text = 0x" << std::hex << (int) e.text;
				return false;
			}
		}

		/** \note
			e.key seems to be unique but if not, it could cause serious VIP
			related errors. If another key with a similiar identifier is found
			in the lookup table we'll ignore the new event till the other key
			has been released. I'm not sure if this bug can occur but prudence
			is the better part of valor.
		*/
		if (mLookupTable.find(e.key) != mLookupTable.end())
		{
			warnlog << "Controller: Prevented lookup table key release bug";
			return false;
		}

		mLookupTable[e.key] = button;

		delegateToTargets(ID::DT_Keyboard, ID::BS_Pressed, button);
		return true;
	}
	
	bool keyReleased(const OIS::KeyEvent& e)
	{
		assert(!mTargets.empty());

		std::map<OIS::KeyCode, unsigned>::iterator it;
		it = mLookupTable.find(e.key);
		
		if (it != mLookupTable.end())
		{
			delegateToTargets(ID::DT_Keyboard, ID::BS_Released, it->second);
			mLookupTable.erase(it);
		}
		else
			warnlog << "Controller: Unable to release previously pressed key\n"
			        << " e.key = 0x" << std::hex << e.key;

		return true;
	}

	// Joystick
	// ========

	// They wrote JoyStick instead of Joystick.
	bool buttonPressed (const OIS::JoyStickEvent&, int button)
	{
		assert(!mTargets.empty());
		
		ButtonCodeT code = static_cast<ButtonCodeT>(button);
		
		delegateToTargets(ID::DT_Joystick, ID::BS_Pressed, code);
		return true;
	}

	bool buttonReleased (const OIS::JoyStickEvent&, int button)
	{
		assert(!mTargets.empty());
		
		ButtonCodeT code = static_cast<ButtonCodeT>(button);
		
		delegateToTargets(ID::DT_Joystick, ID::BS_Released, code);
		return true;
	}

	/**
		\note
		Maybe it won't be obvious to the VIPs what exactly has been moved,
		that is, the axis, the slider, or the pov? So if we can't look this
		information up in the event itself, we'll need to introduce new
		identifiers.
	*/
	bool axisMoved (const OIS::JoyStickEvent& e, int axis)
	{
		assert(!mTargets.empty());

		AxisData<s32> ad(axis);
		ad << e.state.mAxes.at(axis);

		delegateToTargets(ID::DT_Joystick, ID::AT_Normal, ad);
		return true;
	}

	bool sliderMoved (const OIS::JoyStickEvent&, int /*pos*/)
	{
		//! \note
		//! Slider has only `int abX' and `int abY' which are
		//! "true if pushed, false otherwise". This makes no sense.
		return true;
	}

	bool povMoved (const OIS::JoyStickEvent&, int /*pos*/)
	{
		/**	\note
			\verbatim
			
			Don't care about this Pov stuff for now.
			The "Pov" is actually the stick on the left
			of a console-like gaming controller:
			                  
			  ______________/______
			 / ./\.            O O \
			| <(())>          O O   |
			 \ �\/`   ## ##  O O   /
			  =====================
			  
			\endverbatim
		*/
		return true;
	}

private:
	//! Helper function: Calls Delegate() for every observed DelegaterT*
	template <typename Arg1, typename Arg2, typename Arg3>
	void delegateToTargets(const Arg1& a1, const Arg2& a2, const Arg3& a3)
	{
		BOOST_FOREACH(DelegaterT* target, mTargets)
		{
			target->Delegate(a1, a2, a3);
		}
	}

	typedef std::vector<DelegaterT*> DelegaterContainerT;
	DelegaterContainerT mTargets;
};

} // namespace detail

/** Forward declaration for a traits definition which implements
	configuration knowledge for the OIS domain. Required members are:
	
	- enum iType This is the "type identifier" for its particular concrete
	             OIS::Object (e.g. OIS::OISMouse).

	- typeid OISCallbackT Describes the used appropriate OIS device
	                      abstraction (e.g. OIS::MouseListener).
*/

// Device selectors used by class OISDevice
struct Mouse;
struct Keyboard;
struct Joystick;

template <typename DeviceT>
struct OISDeviceTraits;

/// OIS::Mouse configuration implementation
template <>
struct OISDeviceTraits<Mouse>
{
	enum { iType = OIS::OISMouse };

	typedef OIS::Mouse         DeviceT;
	typedef OIS::MouseListener OISCallbackT;

	typedef OIS::MouseButtonID CodeT;
	typedef OIS::MouseEvent    EventT;
};

/// OIS::Keyboard configuration implementation
template <>
struct OISDeviceTraits<Keyboard>
{
	enum { iType = OIS::OISKeyboard };

	typedef OIS::Keyboard    DeviceT;
	typedef OIS::KeyListener OISCallbackT;

	typedef OIS::KeyCode     CodeT;
	typedef OIS::KeyEvent    EventT;
};

/// OIS::Joystick configuration implementation
template <>
struct OISDeviceTraits<Joystick>
{
	enum { iType = OIS::OISJoyStick };

	typedef OIS::JoyStick         DeviceT;
	typedef OIS::JoyStickListener OISCallbackT;

	typedef int                   CodeT;
	typedef OIS::JoyStickEvent    EventT;
};

/** The task of OISDevice is to put an abstraction layer on any OIS or OGRE
	related functionality. Which is, as of this writing, not yet fully done.

	\tparam DeviceSelectorT These help to chose the correct traits.

	\tparam Traits Contains the configuration knowledge for DeviceT.

	\tparam DelegaterT Describes a controller module which has several Delegate
	                   functions which are used to forward events to its VIPs.
*/
template
<
	typename DeviceSelectorT,
	typename Traits = OISDeviceTraits<DeviceSelectorT>,
	typename DelegaterT = Delegater
>
class OISDevice : public DelegaterT
{
public:
	typedef typename Traits::DeviceT      OISObjectT;
	typedef typename Traits::OISCallbackT OISCallbackT;

	OISDevice() :
	mActive(false)
	{
		// Unused
	}

	~OISDevice()
	{
		// Unused
	}

	//! \return false on error
	bool init(boost::shared_ptr<OIS::InputManager> IM,
	          u32 windowWidth,
	          u32 windowHeight)
	{
		mWindowWidth = windowWidth;
		mWindowHeight = windowHeight;

		bool ok = false;
		if (! mDevice)
			ok = initializeDeviceForThisOISType(IM);

		activate();
		return ok;
	}

	bool good() const
	{
		return mDevice;
	}

	void capture()
	{
		assert(mDevice);
		mDevice->capture();
	}

	void activate()
	{
		// Ignore double activate calls gracefully.
		if (! mActive)
		{
			mEventCatcher.addTarget(this);
			mActive = true;
		}
	}
	
	void deactivate()
	{
		// Ignore double deactivate calls gracefully.
		if (mActive)
		{
			mEventCatcher.removeTarget(this);
			mActive = false;
		}
	}

private:
	//! \return false on error
	bool initializeDeviceForThisOISType(boost::shared_ptr<OIS::InputManager> IM)
	{
		try
		{
			assert(! mDevice);

			// Create a new "input object", managed by a shared_ptr.
			mDevice.reset
			(
				static_cast
				<
					OISObjectT*
				>
				(
					IM->createInputObject
					(
						static_cast
						<
							OIS::Type
						>
						(
							Traits::iType
						),
						true
					)
				),
				detail::DeviceDeleter(IM)
			);

			if (mDevice)
			{
				mDevice->setEventCallback(&mEventCatcher);

				extraInitializer(static_cast<OISObjectT*>(NULL));
				return true;
			}
			else
				return false;
		}
		catch(OIS::Exception& ex)
		{
			warnlog << "OIS reported an error: " << ex.eText;
			return false;
		}
	}

	/** This must be static, because OIS allows only one OIS::Object of each
	    type to run concurrently. OIS therefore provides a callback switch
		mechanism for its stakeholder, which we won't use. Instead of that, we
		use only one device per type and decide ourself where our input will
	    be sent to.
	*/
	static boost::shared_ptr<OISObjectT> mDevice;

	/** Static, too, for the aforementioned reasons. Only one EventCatcher per
	    OIS device type is needed.
	*/
	static detail::EventCatcher<OISCallbackT, DelegaterT> mEventCatcher;

	//! Does nothing (for other device types than mouse)
	template <typename T>
	void extraInitializer(const T*)
	{
	}
	
	//! OIS::Mouse needs to be extra configured. OIS is unable to figure out
	//! the window dimensions by itself, resulting in using a small 50x50 pixel
	//! frame for absolute mouse positions.
	void extraInitializer(OIS::Mouse*)
	{
		const OIS::MouseState &ms = mDevice->getMouseState();
		ms.width = mWindowWidth;
		ms.height = mWindowHeight;
	}

	u32 mWindowWidth;
	u32 mWindowHeight;
	
	bool mActive;
};

// Static template class member definitions
// ----------------------------------------

template <typename DeviceT, typename Traits, typename DelegaterT>
boost::shared_ptr<typename Traits::DeviceT> // OISDevice::OISObjectT
OISDevice<DeviceT, Traits, DelegaterT>::mDevice;

template <typename DeviceT, typename Traits, typename DelegaterT>
detail::EventCatcher<typename Traits::OISCallbackT, DelegaterT>
OISDevice<DeviceT, Traits, DelegaterT>::mEventCatcher;

} // namespace Adapters

namespace Devices
{
	typedef Adapters::OISDevice<Adapters::Mouse>    OISMouse;
	typedef Adapters::OISDevice<Adapters::Keyboard> OISKeyboard;
	typedef Adapters::OISDevice<Adapters::Joystick> OISJoystick;
} // namespace Devices

} // namespace Controller_
} // namespace BFG

#endif
