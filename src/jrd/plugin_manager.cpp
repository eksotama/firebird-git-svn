/*
 *  plugin_manager.cpp
 *  firebird_test
 *
 *  Created by john on Wed Jan 09 2002.
 *  Copyright (c) 2001 __MyCompanyName__. All rights reserved.
 *
 */

#if defined(_MSC_VER) && _MSC_VER < 1300
// Any Microsoft compiler before MSVC7
#pragma warning(disable: 4786)
#endif

#include "../jrd/plugin_manager.h"
#include "../jrd/os/path_utils.h"

PluginManager::Plugin PluginManager::findPlugin(const Firebird::string &name)
{
	for(Module *itr = moduleList; itr; itr = itr->next)
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
	Firebird::list<Path>::iterator pathItr;
	char fb_lib_path[MAXPATHLEN];
	gds__prefix(fb_lib_path, "");
	Firebird::string fbLibPath(fb_lib_path);
	Firebird::string checkDir;
	
	for(pathItr = searchPaths.begin(); pathItr != searchPaths.end(); ++pathItr)
	{
		if (pathItr->second)	// This path is fb relative
		{
			PathUtils::concatPath(checkDir, fbLibPath, pathItr->first);
		}
		else
		{
			checkDir = pathItr->first;
		}
		
		PathUtils::dir_iterator *dirItr = PathUtils::newDirItr(*getDefaultMemoryPool(), checkDir);
		while(*dirItr)
		{
			// See if we have already loaded this module
			bool alreadyLoaded = false;
			for(Module *itr = moduleList; itr; itr = itr->next)
			{
				if (itr->name() == **dirItr)
				{
					alreadyLoaded = true;
					break;
				}
			}
			
			// Check to see if the module has been explicitly excluded from loading
			if (!alreadyLoaded && ignoreModules.size() > 0)
			{
				Firebird::string pathComponent, modName;
				PathUtils::splitLastComponent(pathComponent, modName, **dirItr);
				for(Firebird::list<Firebird::string>::iterator itr2 = ignoreModules.begin();
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
				Module *mod = new(*getDefaultMemoryPool()) PluginModule(**dirItr,
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
	}
}

PluginManager::Module *PluginManager::loadPluginModule(const Firebird::string& name)
{
	char fb_lib_path[MAXPATHLEN];
	gds__prefix(fb_lib_path, "");
	Firebird::string fbLibPath(fb_lib_path);
	Firebird::string checkPath;
	Firebird::list<Path>::iterator itr;
	
	// Check to see if the module name was specified as a relative path
	//	from one of our search paths.  This only makes sense if the name
	//	of the module is relative.
	if (PathUtils::isRelative(name))
	{
		for(itr = searchPaths.begin(); itr != searchPaths.end(); ++itr)
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
				return new(*getDefaultMemoryPool()) PluginModule(checkPath,
						ModuleLoader::loadModule(checkPath));
			}
			ModuleLoader::doctorModuleExtention(checkPath);
			if (ModuleLoader::isLoadableModule(checkPath))
			{
				return new(*getDefaultMemoryPool()) PluginModule(checkPath,
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
		for(itr = searchPaths.begin(); itr != searchPaths.end(); ++itr)
		{
			Firebird::string::size_type pos = 0;
			Firebird::string::size_type checkPos;
			
			if (itr->second)	// use fb path prefix
			{
				checkPos = name.find(fbLibPath, pos);
				if (checkPos == Firebird::string::npos || checkPos != pos)
					continue;	// The fb path prefix isn't a prefix for this module.  Opps.
				pos += fbLibPath.length();
			}
			checkPos = name.find(itr->first, pos);
			if (checkPos == Firebird::string::npos || checkPos != pos)
				continue;	// The search path isn't a prefix for this module.  Opps.
			// OK, the module has the correct prefix path, lets try to load it.
			if (ModuleLoader::isLoadableModule(name))
			{
				return new(*getDefaultMemoryPool()) PluginModule(name,
						ModuleLoader::loadModule(name));
			}
			checkPath = name;
			ModuleLoader::doctorModuleExtention(checkPath);
			if (ModuleLoader::isLoadableModule(checkPath))
			{
				return new(*getDefaultMemoryPool()) PluginModule(checkPath,
						ModuleLoader::loadModule(checkPath));
			}
		}
	}
	
	// If we made it this far there is nothing left we can try.
	//	The module _must_ not be an on-disk module :-)
	return 0;
}

PluginManager::Module *PluginManager::loadBuiltinModule(const Firebird::string& name)
{
	return 0;
}

void PluginManager::addSearchPath(const Firebird::string& path, bool isFBRelative)
{
	Firebird::list<Path>::iterator itr;
	
	for(itr = searchPaths.begin(); itr != searchPaths.end(); ++itr)
	{
		if (itr->first == path && itr->second == isFBRelative)
			return;
	}
	
	searchPaths.push_back(Path(path, isFBRelative));
}

void PluginManager::removeSearchPath(const Firebird::string& path, bool isFBRelative)
{
	Firebird::list<Path>::iterator itr;
	
	for(itr = searchPaths.begin(); itr != searchPaths.end(); ++itr)
	{
		if (itr->first == path && itr->second == isFBRelative)
		{
			searchPaths.erase(itr);
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

PluginManager::Module::~Module()
{
	if (*prev == this)
		*prev = next;
	else
		(*prev)->next = next;
	unload_module();
}

void *PluginManager::BuiltinModule::lookupSymbol(Firebird::string& symbol)
{
	Firebird::map<Firebird::string, void*>::iterator ptr;
	ptr = symbols.find(symbol);
	if (ptr == symbols.end())
		return 0;
	return ptr->second;
}

void *PluginManager::PluginModule::lookupSymbol(Firebird::string& symbol)
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