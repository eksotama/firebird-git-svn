/*
 *	PROGRAM:		Firebird interface.
 *	MODULE:			ImplementHelper.h
 *	DESCRIPTION:	Tools to help create interfaces.
 *
 *  The contents of this file are subject to the Initial
 *  Developer's Public License Version 1.0 (the "License");
 *  you may not use this file except in compliance with the
 *  License. You may obtain a copy of the License at
 *  http://www.ibphoenix.com/main.nfs?a=ibphoenix&page=ibp_idpl.
 *
 *  Software distributed under the License is distributed AS IS,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *  See the License for the specific language governing rights
 *  and limitations under the License.
 *
 *  The Original Code was created by Alex Peshkov
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2010 Alex Peshkov <peshkoff at mail.ru>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 *
 *
 */

#ifndef FB_COMMON_CLASSES_IMPLEMENT_HELPER
#define FB_COMMON_CLASSES_IMPLEMENT_HELPER

#include "FirebirdPluginApi.h"
#include "Timer.h"
#include "ProviderInterface.h"
#include "../common/classes/alloc.h"
#include "gen/iberror.h"
#include "../yvalve/gds_proto.h"
#include "../common/classes/init.h"
#include "../common/classes/auto.h"
#include "../common/classes/RefCounted.h"
#include "consts_pub.h"

namespace Firebird {

// If you need interface on stack, use template AutoPtr<YourInterface, AutoDisposable>
// as second parameter to store it.
class AutoDisposable
{
public:
	static void clear(IDisposable* ptr)
	{
		if (ptr)
		{
			ptr->dispose();
		}
	}
};


// Implement standard interface and plugin functions

// Helps to implement generic versioned interfaces
template <class C, int V>
class VersionedIface : public C
{
public:
	VersionedIface() { }

	int FB_CARG getVersion()
	{
		return V;
	}

private:
	VersionedIface(const VersionedIface&);
	VersionedIface& operator=(const VersionedIface&);
};


// Helps to implement versioned interfaces on stack or static
template <class C, int V>
class AutoIface : public VersionedIface<C, V>
{
public:
	AutoIface() { }

	void* operator new(size_t, void* memory) throw()
	{
		return memory;
	}
};

// Helps to implement disposable interfaces
template <class C, int V>
class DisposeIface : public VersionedIface<C, V>, public GlobalStorage
{
public:
	DisposeIface() { }
};

// Helps to implement standard interfaces
template <class C, int V>
class RefCntIface : public VersionedIface<C, V>, public GlobalStorage
{
public:
	RefCntIface() : refCounter(0) { }

#ifdef DEV_BUILD
	~RefCntIface()
	{
		fb_assert(refCounter.value() == 0);
	}
#endif

	void FB_CARG addRef()
	{
		++refCounter;
	}

protected:
	AtomicCounter refCounter;
};


// Helps to implement plugins
template <class C, int V>
class StdPlugin : public RefCntIface<C, V>
{
private:
	IRefCounted* owner;

public:
	StdPlugin() : owner(NULL)
	{ }

	IRefCounted* FB_CARG getOwner()
	{
		return owner;
	}

	void FB_CARG setOwner(IRefCounted* iface)
	{
		owner = iface;
	}
};


// Trivial factory
template <class P>
class SimpleFactoryBase : public AutoIface<IPluginFactory, FB_PLUGIN_FACTORY_VERSION>
{
public:
	IPluginBase* FB_CARG createPlugin(IPluginConfig* factoryParameter)
	{
		P* p = new P(factoryParameter);
		p->addRef();
		return p;
	}
};

template <class P>
class SimpleFactory : public Static<SimpleFactoryBase<P> >
{
};


// Base for interface type independent accessors
template <typename C>
class AccessAutoInterface
{
public:
	AccessAutoInterface(C* aPtr)
		: ptr(aPtr)
	{ }

	operator C*()
	{
		return ptr;
	}

	C* operator->()
	{
		return ptr;
	}

private:
	C* ptr;
};

// Master interface access
class MasterInterfacePtr : public AccessAutoInterface<IMaster>
{
public:
	MasterInterfacePtr()
		: AccessAutoInterface<IMaster>(fb_get_master_interface())
	{ }
};


// Generic plugins interface access
class PluginManagerInterfacePtr : public AccessAutoInterface<IPluginManager>
{
public:
	PluginManagerInterfacePtr()
		: AccessAutoInterface<IPluginManager>(MasterInterfacePtr()->getPluginManager())
	{ }
	PluginManagerInterfacePtr(IMaster* master)
		: AccessAutoInterface<IPluginManager>(master->getPluginManager())
	{ }
};


// Control timer interface access
class TimerInterfacePtr : public AccessAutoInterface<ITimerControl>
{
public:
	TimerInterfacePtr()
		: AccessAutoInterface<ITimerControl>(fb_get_master_interface()->getTimerControl())
	{ }
};


// Distributed transactions coordinator access
class DtcInterfacePtr : public AccessAutoInterface<IDtc>
{
public:
	DtcInterfacePtr()
		: AccessAutoInterface<IDtc>(fb_get_master_interface()->getDtc())
	{ }
};


// When process exits, dynamically loaded modules (for us plugin modules)
// are unloaded first. As the result all global variables in plugin are already destroyed
// when yvalve is starting fb_shutdown(). This causes almost unavoidable segfault.
// To avoid it this class is added - it detects spontaneous (not by PluginManager)
// module unload and notifies PluginManager about this said fact.
class UnloadDetectorHelper : public VersionedIface<IPluginModule, FB_PLUGIN_MODULE_VERSION>
{
public:
	typedef void VoidNoParam();

	explicit UnloadDetectorHelper(MemoryPool&)
		: cleanup(NULL), flagOsUnload(true)
	{ }

	~UnloadDetectorHelper()
	{
		if (flagOsUnload)
		{
			PluginManagerInterfacePtr pi;
			pi->unregisterModule(this);

			doClean();
		}
	}

	bool unloadStarted()
	{
		return !flagOsUnload;
	}

	void setCleanup(VoidNoParam* function)
	{
		cleanup = function;
	}

private:
	VoidNoParam* cleanup;
	bool flagOsUnload;

	void FB_CARG doClean()
	{
		flagOsUnload = false;

		if (cleanup)
		{
			cleanup();
			cleanup = NULL;
		}
	}
};

typedef GlobalPtr<UnloadDetectorHelper, InstanceControl::PRIORITY_DETECT_UNLOAD> UnloadDetector;


class InternalMessageBuffer : public FbMessage
{
public:
	InternalMessageBuffer(unsigned aBlrLength, const unsigned char* aBlr,
		unsigned aBufferLength, unsigned char* aBuffer)
	{
		blrLength = aBlrLength;
		blr = aBlr;
		bufferLength = aBufferLength;
		buffer = aBuffer;
	}
};


} // namespace Firebird


#endif // FB_COMMON_CLASSES_IMPLEMENT_HELPER
