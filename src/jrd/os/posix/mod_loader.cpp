/*
 *  mod_loader.cpp
 *
 */

#include "../jrd/os/mod_loader.h"
#include "common.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>

/// This is the POSIX (dlopen) implementation of the mod_loader abstraction.

class DlfcnModule : public ModuleLoader::Module
{
public:
	DlfcnModule(void *m) : module(m) {}
	~DlfcnModule();
	void *findSymbol(const Firebird::string&);
	
private:
	void *module;
};

bool ModuleLoader::isLoadableModule(const Firebird::string& module)
{
	struct stat sb;
	if (-1 == stat(module.c_str(), &sb))
		return false;
	if ( ! (sb.st_mode & S_IFREG) )		// Make sure it is a plain file
		return false;
	if ( -1 == access(module.c_str(), R_OK | X_OK))
		return false;
	return true;
}

void ModuleLoader::doctorModuleExtention(Firebird::string& name)
{
	Firebird::string::size_type pos = name.rfind(".so");
	if (pos != Firebird::string::npos && pos == name.length() - 3)
		return;		// No doctoring necessary
	name += ".so";
}

ModuleLoader::Module *ModuleLoader::loadModule(const Firebird::string& modPath)
{
	void *module;
	
	module = dlopen(modPath.c_str(), RTLD_LAZY);
	if (module == NULL)
		return 0;
	
	return new(*getDefaultMemoryPool()) DlfcnModule(module);
}

DlfcnModule::~DlfcnModule()
{
	if (module)
		dlclose(module);
}

void *DlfcnModule::findSymbol(const Firebird::string& symName)
{
	void *result;  

	result = dlsym(module, symName.c_str());
	if (result == NULL)
	{
		Firebird::string newSym = '_' + symName;

		result = dlsym(module, newSym.c_str());
	}
	return result;
}
