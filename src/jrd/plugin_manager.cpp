/*
 *  plugin_manager.cpp
 *  firebird_test
 *
 *  Created by john on Wed Jan 09 2002.
 *  Copyright (c) 2001 __MyCompanyName__. All rights reserved.
 *
 */

#include "../jrd/plugin_manager.h"
#include "../jrd/os/path_utils.h"
#include "../jrd/gds_proto.h"

PluginManager::Plugin PluginManager::findPlugin(const Firebird::PathName &name)
{
	for (Module *itr = moduleList; itr; itr = itr->next)
	{
		if (itr->name() == name)
			return itr;
	}
	
	Module *result = 0;
	
	result = loadPluginModule(name);
	if (!result)
	{
		result = loadBuiltinModule(name);
		if (!result)
		{
			// Throw a module not found error here?
			return 0;
		}
	}
	
	// Link the new module into the module list
	result->aquire();
	if (moduleList)
	{
		moduleList->prev = &(result->next);
	}
	result->next = moduleList;
	result->prev = &moduleList;
	moduleList = result;
	return Plugin(result);
}

void PluginManager::loadAllPlugins()
{
	char fb_lib_path[MAXPATHLEN];
	gds__prefix(fb_lib_path, "");
	Firebird::PathName fbLibPath(fb_lib_path);
	Firebird::PathName checkDir;
	
	for (spIterator pathItr = searchPaths.begin(); pathItr != searchPaths.end(); ++pathItr)
	{
		if (pathItr->second)	// This path is fb relative
		{
			PathUtils::concatPath(checkDir, fbLibPath, pathItr->first);
		}
		else
		{
			checkDir = pathItr->first;
		}
		
		PathUtils::dir_iterator *dirItr = 
			PathUtils::newDirItr(getPool(), checkDir);
		while (*dirItr)
		{
			// See if we have already loaded this module
			bool alreadyLoaded = false;
			for (Module *itr = moduleList; itr; itr = itr->next)
			{
				if (itr->name() == **dirItr)
				{
					alreadyLoaded = true;
					break;
				}
			}
			
			// Check to see if the module has been explicitly excluded from loading
			if (!alreadyLoaded && ignoreModules.getCount() > 0)
			{
				Firebird::PathName pathComponent, modName;
				PathUtils::splitLastComponent(pathComponent, modName, **dirItr);
				for (Firebird::ObjectsArray<Firebird::PathName>::iterator itr2 = ignoreModules.begin();
						itr2 != ignoreModules.end(); ++itr2)
				{
					if (modName == *itr2)
					{
						alreadyLoaded = true;  // a harmless fib
						break;
					}
				}
			}
			
			// If we haven't already loaded, and the module is loadable
			// as defined by the host os, then by all means load it!
			if (!alreadyLoaded && ModuleLoader::isLoadableModule(**dirItr))
			{
				Module *mod = FB_NEW(getPool()) PluginModule(getPool(), **dirItr,
						ModuleLoader::loadModule(**dirItr));
				if (moduleList)
				{
					moduleList->prev = &(mod->next);
				}
				mod->next = moduleList;
				mod->prev = &moduleList;
				moduleList = mod;
			}		
			++(*dirItr);
		}
		delete dirItr;
	}
}

PluginManager::Module *PluginManager::loadPluginModule(const Firebird::PathName& name)
{
	char fb_lib_path[MAXPATHLEN];
	gds__prefix(fb_lib_path, "");
	Firebird::PathName fbLibPath(fb_lib_path);
	Firebird::PathName checkPath;
	
	// Check to see if the module name was specified as a relative path
	//	from one of our search paths.  This only makes sense if the name
	//	of the module is relative.
	if (PathUtils::isRelative(name))
	{
		for (spIterator itr = searchPaths.begin(); itr != searchPaths.end(); ++itr)
		{
			if (itr->second)	// This path is fb relative
			{
				PathUtils::concatPath(checkPath, fbLibPath, itr->first);
				PathUtils::concatPath(checkPath, checkPath, name);
			}
			else
			{
				PathUtils::concatPath(checkPath, itr->first, name);
			}
			
			if (ModuleLoader::isLoadableModule(checkPath))
			{
				return FB_NEW(getPool()) PluginModule(getPool(), name,
						ModuleLoader::loadModule(checkPath));
			}
			ModuleLoader::doctorModuleExtention(checkPath);
			if (ModuleLoader::isLoadableModule(checkPath))
			{
				return FB_NEW(getPool()) PluginModule(getPool(), checkPath,
						ModuleLoader::loadModule(checkPath));
			}
		}
	}
	
	// If we get this far we know the module isn't given as a relative path.
	// Check to see if it is a valid absolute path that happens to fall in one
	// of our search paths.  This only makes sense if the name of the module
	// is absolute.

	if (!PathUtils::isRelative(name))
	{
		for (spIterator itr = searchPaths.begin(); itr != searchPaths.end(); ++itr)
		{
			Firebird::PathName::size_type pos = 0;
			Firebird::PathName::size_type checkPos;
			
			if (itr->second)	// use fb path prefix
			{
				checkPos = name.find(fbLibPath, pos);
				if (checkPos == Firebird::PathName::npos || checkPos != pos)
					continue;	// The fb path prefix isn't a prefix for this module.  Opps.
				pos += fbLibPath.length();
			}
			checkPos = name.find(itr->first, pos);
			if (checkPos == Firebird::PathName::npos || checkPos != pos)
				continue;	// The search path isn't a prefix for this module.  Opps.
			// OK, the module has the correct prefix path, lets try to load it.
			if (ModuleLoader::isLoadableModule(name))
			{
				return FB_NEW(getPool()) PluginModule(getPool(), name,
						ModuleLoader::loadModule(name));
			}
			checkPath = name;
			ModuleLoader::doctorModuleExtention(checkPath);
			if (ModuleLoader::isLoadableModule(checkPath))
			{
				return FB_NEW(getPool()) PluginModule(getPool(), checkPath,
						ModuleLoader::loadModule(checkPath));
			}
		}
	}
	
	// If we made it this far there is nothing left we can try.
	//	The module _must_ not be an on-disk module :-)
	return 0;
}

PluginManager::Module *PluginManager::loadBuiltinModule(const Firebird::PathName& name)
{
	return 0;
}

void PluginManager::addSearchPath(const Firebird::PathName& path, bool isFBRelative)
{
	for (spIterator itr = searchPaths.begin(); itr != searchPaths.end(); ++itr)
	{
		if (itr->first == path && itr->second == isFBRelative)
			return;
	}
	
	searchPaths.push(Path(path, isFBRelative));
}

void PluginManager::removeSearchPath(const Firebird::PathName& path, bool isFBRelative)
{
	for (spIterator itr = searchPaths.begin(); itr != searchPaths.end(); ++itr)
	{
		if (itr->first == path && itr->second == isFBRelative)
		{
			searchPaths.remove(itr);
			return;
		}
	}
}

const PluginManager::Plugin& PluginManager::Plugin::operator=(const Plugin& other)
{
	if (this != &other)
	{
		if (module != 0)
			module->release();
		module = other.module;
		if (module != 0)
			module->aquire();
	}
	return *this;
}

PluginManager::~PluginManager()
{
	while (moduleList)
	{
#if defined(DEV_BUILD)
		if (moduleList->refCnt != 1)
		{
			printf("Freeing loadable module with reference count != 1: %s (%d)\n",
					moduleList->module_name.c_str(), moduleList->refCnt);
		}
#endif
		delete moduleList;
	}
}

PluginManager::Module::~Module()
{
	if (next)
		next->prev = prev;
	*prev = next;

	unload_module();
}

void *PluginManager::BuiltinModule::lookupSymbol(const Firebird::string& symbol)
{
	int n;
	if (! symbols.find(symbol, n))
		return 0;
	return symbols[n].second;
}

void *PluginManager::PluginModule::lookupSymbol(const Firebird::string& symbol)
{
	if (module != 0)
		return module->findSymbol(symbol);
	return 0;
}

void PluginManager::PluginModule::unload_module()
{
	if (module != 0)
		delete module;
	module = 0;
}



// Code that handles loading the generic firebird/plugin directory

const char *PluginManager::ENGINE_PLUGIN_DIR = "plugins";

#ifdef DARWIN
const char *PluginManager::ENGINE_PLUGIN_REGISTRATION_ENTRYPOINT =  "_register_plugin";
#else
const char *PluginManager::ENGINE_PLUGIN_REGISTRATION_ENTRYPOINT =  "register_plugin";
#endif

void PluginManager::load_engine_plugins()
{
	enginePluginManager().addSearchPath(ENGINE_PLUGIN_DIR);
	enginePluginManager().loadAllPlugins();
	
	Firebird::string entryPoint(ENGINE_PLUGIN_REGISTRATION_ENTRYPOINT);
	for (PluginManager::iterator itr = enginePluginManager().begin();
	    itr != enginePluginManager().end(); ++itr)
	{
		engineRegistrationFuncType regFunc = (engineRegistrationFuncType)
			(*itr).lookupSymbol(entryPoint);
		if (!regFunc)
			continue;
		PluginManager::Plugin p = *itr;
		(*regFunc)(&p);
	}
}

Firebird::InitInstance<PluginManager> PluginManager::enginePluginManager;
