//============================================================================
// Name        : Script.h
// Copyright   : DataSoft Corporation 2011-2012
//	Nova is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
//   Nova is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with Nova.  If not, see <http://www.gnu.org/licenses/>.
// Description : Represents a single honeyd script that can be bound to a port
//		to emulate a network service.
//============================================================================

#ifndef SCRIPT_H_
#define SCRIPT_H_

#include "../HashMap.h"
#include "../HashMapStructs.h"

#include <string>
#include <vector>

namespace Nova
{

typedef Nova::HashMap<std::string, std::vector<std::string>,  std::hash<std::string>, eqstr > scriptConfigurationOptions;

class Script
{

public:

	//Unique identifier of the script
	std::string m_name;

	std::string m_service;
	std::string m_osclass;
	std::string m_path;

	// Configuration options for the script
	bool m_isConfigurable;
	scriptConfigurationOptions options;

};

}


#endif /* SCRIPT_H_ */
