#include "../jrd/os/path_utils.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

/// The POSIX implementation of the path_utils abstraction.

const char PathUtils::dir_sep = '/';
const char* PathUtils::up_dir_link = "..";

class PosixDirItr : public PathUtils::dir_iterator
{
public:
	PosixDirItr(const Firebird::PathName&);
	~PosixDirItr();
	const PosixDirItr& operator++();
	const Firebird::PathName& operator*() { return file; }
	operator bool() { return !done; }
	
private:
	DIR *dir;
	Firebird::PathName file;
	int done;
};

PosixDirItr::PosixDirItr(const Firebird::PathName& path)
	: dir_iterator(path), dir(0), done(0)
{
	dir = opendir(dirPrefix.c_str());
	if (!dir)
		done = 1;
	else
		++(*this);
}

PosixDirItr::~PosixDirItr()
{
	if (dir)
		closedir(dir);
	dir = 0;
	done = 1;
}

const PosixDirItr& PosixDirItr::operator++()
{
	if (done)
		return *this;
	struct dirent *ent = readdir(dir);
	if (ent == NULL)
	{
		done = 1;
	}
	else
	{
		PathUtils::concatPath(file, dirPrefix, ent->d_name);
	}
	return *this;
}

PathUtils::dir_iterator *PathUtils::newDirItr(MemoryPool& p, const Firebird::PathName& path)
{
	return FB_NEW(p) PosixDirItr(path);
}

void PathUtils::splitLastComponent(Firebird::PathName& path, Firebird::PathName& file,
		const Firebird::PathName& orgPath)
{
	Firebird::PathName::size_type pos;
	
	pos = orgPath.rfind(dir_sep);
	if (pos == Firebird::PathName::npos)
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

void PathUtils::concatPath(Firebird::PathName& result,
		const Firebird::PathName& first,
		const Firebird::PathName& second)
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
	
	if (first[first.length()-1] != dir_sep &&
		second[0] != dir_sep)
	{
		result = first + dir_sep + second;
		return;
	}
	if (first[first.length()-1] == dir_sep &&
		second[0] == dir_sep)
	{
		result = first;
		result.append(second, 1, second.length() - 1);
		return;
	}
	
	result = first + second;
}

bool PathUtils::isRelative(const Firebird::PathName& path)
{
	if (path.length() > 0)
		return path[0] != dir_sep;
	return false;
}

bool PathUtils::isSymLink(const Firebird::PathName& path)
{
	struct stat st, lst;
	if (stat(path.c_str(), &st) != 0)
		return false;
	if (lstat(path.c_str(), &lst) != 0)
		return false;
	return st.st_ino != lst.st_ino;
}

bool PathUtils::comparePaths(const Firebird::PathName& path1, 
 							 const Firebird::PathName& path2) {
 	return path1 == path2;
}

bool PathUtils::canAccess(const Firebird::PathName& path, int mode) {
	return access(path.c_str(), mode) == 0;
}
