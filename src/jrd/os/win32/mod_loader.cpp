/*
 *  mod_loader.cpp
 *
 */

#include "firebird.h"
#include "../jrd/os/mod_loader.h"
#include <windows.h>

typedef Firebird::string string;
typedef Firebird::PathName PathName;

/// This is the Win32 implementation of the mod_loader abstraction.

class Win32Module : public ModuleLoader::Module
{
public:
	Win32Module(HMODULE m) : module(m) {}
	~Win32Module();
	void *findSymbol(const string&);
	
private:
	HMODULE module;
};

bool ModuleLoader::isLoadableModule(const PathName& module)
{
	LPCSTR pszName = module.c_str();
	HINSTANCE hMod = LoadLibraryEx(pszName, 0,
		LOAD_WITH_ALTERED_SEARCH_PATH | LOAD_LIBRARY_AS_DATAFILE);

	if (hMod) {
		FreeLibrary((HMODULE)hMod);
	}
	return hMod != 0;
}

void ModuleLoader::doctorModuleExtention(Firebird::PathName& name)
{
	PathName::size_type pos = name.rfind(".dll");
	if (pos != PathName::npos && pos == name.length() - 4)
		return;
	name += ".dll";
}

ModuleLoader::Module *ModuleLoader::loadModule(const Firebird::PathName& modPath)
{
	HMODULE module = LoadLibraryEx(modPath.c_str(), 0, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!module)
		return 0;
	
	return FB_NEW(*getDefaultMemoryPool()) Win32Module(module);
}

Win32Module::~Win32Module()
{
	if (module)
		FreeLibrary(module);
}

void *Win32Module::findSymbol(const Firebird::string& symName)
{
	FARPROC result = GetProcAddress(module, symName.c_str());
	if (!result)
	{
		string newSym = '_' + symName;
		result = GetProcAddress(module, newSym.c_str());
	}
	return (void*) result;
}
