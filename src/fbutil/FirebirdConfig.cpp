#include "FirebirdConfig.h"

#include "fred.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif


//-----------------------------------------------------------------------------
//
//  The instance of the static class variable.
//


FirebirdConfig FirebirdConfig::sysConfig;


//-----------------------------------------------------------------------------
//
//
//

void stripLeadingWhiteSpace(string& s)
{
	if (!s.size()) {
		return;
	}

	const string::size_type startPos = s.find_first_not_of(" \t");
	if (startPos == string::npos) {
		s.erase();	// nothing but air
	} else if (startPos) {
		s = s.substr(startPos);
	}
}

//-----------------------------------------------------------------------------
//
//
//

void stripTrailingWhiteSpace(string& s)
{
	if (!s.size()) {
		return;
	}

	string::size_type endPos = s.find_last_not_of(" \t");
	if (endPos != string::npos) {
		// Note that endPos is the index to the last non-ws char
		// why we have to inc. it
		++endPos;
		s = s.substr(0, endPos);
	}
}

//-----------------------------------------------------------------------------
//
//
//

void stripComments(string& s)
{
	// Note that this is only a hack. It won't work in case inputLine
	// contains hash-marks embedded in quotes! Not that I know if we
	// should care about that case.
	const string::size_type commentPos = s.find('#');
	if (commentPos != string::npos) {
		s = s.substr(0, commentPos);
	}
}

//-----------------------------------------------------------------------------
//
//
//

string FirebirdConfig::getSysString(const string& key) {
  return sysConfig.getString(key);
}

//-----------------------------------------------------------------------------
//
//
//

int    FirebirdConfig::getSysInt(const string& key) {
  return sysConfig.getInt(key);
}


//-----------------------------------------------------------------------------
//
//
//

string FirebirdConfig::getString(const string& key) {

    mymap_t::iterator lookup;

    lookup = parameters.find(key);

    if (lookup != parameters.end()) {
      return lookup->second;
    }

    return string();
}

//-----------------------------------------------------------------------------
//
//
//

int    FirebirdConfig::getInt(const string& key) {
    string data = getString(key);

    if (data.empty()) {
        return 0;
    }

    return atoi(data.data());
}



//-----------------------------------------------------------------------------
//
//
//

string parseKeyFrom(const string& inputLine, string::size_type& endPos) {

    endPos = inputLine.find_first_of("= \t");
    if (endPos == string::npos) {
        return inputLine;
    }

    return inputLine.substr(0, endPos);
}



//-----------------------------------------------------------------------------
//
//
//

string parseValueFrom(string inputLine, string::size_type initialPos) {

    if (initialPos == string::npos) {
        return string();
    }

    // skip leading white spaces
    int startPos = inputLine.find_first_not_of("= \t", initialPos);
    if (startPos == string::npos) {
        return string();
    }

    stripTrailingWhiteSpace(inputLine);
    return inputLine.substr(startPos);
    
}


//-----------------------------------------------------------------------------
//
//
//

void FirebirdConfig::loadSysConfig() {
  sysConfig.loadConfig();
}

//-----------------------------------------------------------------------------
//
//
//

void FirebirdConfig::loadConfig() {

    ifstream configFile("/etc/firebird.conf");

    string inputLine;
    
    while (!configFile.eof()) {
        getline(configFile, inputLine);

		stripComments(inputLine);
		stripLeadingWhiteSpace(inputLine);
		
		if (!inputLine.size()) {
			continue;	// comment-line or empty line
		}

        //        cout << "read \"" << inputLine << "\"\n";

        if (inputLine.find('=') == string::npos) {
            cerr << "illegal line \"" << inputLine << "\"" << endl;
            continue;
        }

        string::size_type endPos;

        string key   = parseKeyFrom(inputLine, endPos);
        string value = parseValueFrom(inputLine, endPos);

        cout << "adding \"" << key << "\" \"" << value << "\"" << endl;
//        parameters.insert(pair<string, string>(key, value));
// Just to display yet another template function
        parameters.insert(make_pair(key, value));
    }
}

