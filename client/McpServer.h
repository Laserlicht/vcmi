/*
 * McpServer.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "StdInc.h"

namespace mcp { class server; }

VCMI_LIB_NAMESPACE_BEGIN
class CGameState;
VCMI_LIB_NAMESPACE_END

class McpServer : boost::noncopyable
{
	std::unique_ptr<mcp::server> server;
	bool enabled;
public:
	McpServer();
	~McpServer();
};
