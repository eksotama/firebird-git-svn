/*
 *	PROGRAM:	Client/Server Common Code
 *	MODULE:		QualifiedName.h
 *	DESCRIPTION:	Qualified metadata name holder.
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
 *  The Original Code was created by Adriano dos Santos Fernandes
 *  for the Firebird Open Source RDBMS project.
 *
 *  Copyright (c) 2009 Adriano dos Santos Fernandes <adrianosf@uol.com.br>
 *  and all contributors signed below.
 *
 *  All Rights Reserved.
 *  Contributor(s): ______________________________________.
 */

#ifndef COMMON_QUALIFIEDNAME_H
#define COMMON_QUALIFIEDNAME_H

#include "MetaName.h"
#include "array.h"

namespace Firebird {

class QualifiedName
{
public:
	QualifiedName(MemoryPool& p, const MetaName& aIdentifier, const MetaName& aQualifier)
		: identifier(p, aIdentifier),
		  qualifier(p, aQualifier)
	{
	}

	QualifiedName(const MetaName& aIdentifier, const MetaName& aQualifier)
		: identifier(aIdentifier),
		  qualifier(aQualifier)
	{
	}

	QualifiedName(MemoryPool& p)
		: identifier(p),
		  qualifier(p)
	{
	}

	QualifiedName()
	{
	}

	QualifiedName(MemoryPool& p, const QualifiedName& src)
		: identifier(p, src.identifier),
		  qualifier(p, src.qualifier)
	{
	}

public:
	bool operator >(const QualifiedName& m) const
	{
		return qualifier > m.qualifier || (qualifier == m.qualifier && identifier > m.identifier);
	}

	bool operator ==(const QualifiedName& m) const
	{
		return identifier == m.identifier && qualifier == m.qualifier;
	}

public:
	string toString() const
	{
		string s;
		if (qualifier.hasData())
		{
			s = qualifier.c_str();
			s.append(".");
		}
		s.append(identifier.c_str());
		return s;
	}

public:
	MetaName identifier;
	MetaName qualifier;
};

} // namespace Firebird

#endif // COMMON_QUALIFIEDNAME_H
