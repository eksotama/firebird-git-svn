#include "../jrd/os/path_utils.h"
#include <windows.h>

/// The Win32 implementation of the path_utils abstraction.

const char PathUtils::dir_sep = '\\';

class Win32DirItr : public PathUtils::dir_iterator
{
public:
	Win32DirItr(const Firebird::string&);
	~Win32DirItr();
	const PathUtils::dir_iterator& operator++();
	const Firebird::string& operator*() { return file; }
	operator bool() { return !done; }
	
private:
	HANDLE dir;
	WIN32_FIND_DATA fd;
	Firebird::string file;
	int done;
};

Win32DirItr::Win32DirItr(const Firebird::string& path)
	: dir_iterator(path), dir(0), done(0)
{
	Firebird::string dirPrefix2 = dirPrefix;

	if (dirPrefix.length() && dirPrefix[dirPrefix.length()-1] != PathUtils::dir_sep)
		dirPrefix2 = dirPrefix2 + PathUtils::dir_sep;
	dirPrefix2 += "*.*";
	
	dir = FindFirstFile(dirPrefix2.c_str(), &fd);
	if (dir == INVALID_HANDLE_VALUE) {
		dir = 0;
		done = 1;
	}
}

Win32DirItr::~Win32DirItr()
{
	if (dir)
		FindClose(dir);

	dir = 0;
	done = 1;
}

const PathUtils::dir_iterator& Win32DirItr::operator++()
{
	if (done)
		return *this;

	if (!FindNextFile(dir, &fd))
		done = 1;
	else
		PathUtils::concatPath(file, dirPrefix, fd.cFileName);
	
	return *this;
}

PathUtils::dir_iterator *PathUtils::newDirItr(MemoryPool& p, const Firebird::string& path)
{
	return FB_NEW(p) Win32DirItr(path);
}

void PathUtils::splitLastComponent(Firebird::string& path, Firebird::string& file,
		const Firebird::string& orgPath)
{
	Firebird::string::size_type pos;
	
	pos = orgPath.rfind(PathUtils::dir_sep);
	if (pos == Firebird::string::npos)
	{
		path = "";
		file = orgPath;
		return;
	}
	
	path.erase();
	path.append(orgPath, 0, pos);	// skip the directory separator
	file.erase();
	file.append(orgPath, pos+1, orgPath.length() - pos - 1);
}

void PathUtils::concatPath(Firebird::string& result,
		const Firebird::string& first,
		const Firebird::string& second)
{
	if (second.length() == 0)
	{
		result = first;
		return;
	}
	if (first.length() == 0)
	{
		result = second;
		return;
	}
	
	if (first[first.length()-1] != PathUtils::dir_sep &&
		second[0] != PathUtils::dir_sep)
	{
		result = first + PathUtils::dir_sep + second;
		return;
	}
	if (first[first.length()-1] == PathUtils::dir_sep &&
		second[0] == PathUtils::dir_sep)
	{
		result = first;
		result.append(second, 1, second.length() - 1);
		return;
	}
	
	result = first + second;
}

bool PathUtils::isRelative(const Firebird::string& path)
{
	if (path.length() > 0)
		return path[0] != PathUtils::dir_sep;
	return false;
}
