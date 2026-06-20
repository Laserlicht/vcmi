/*
 * McpServer.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "McpServer.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../lib/CConfigHandler.h"
#include "../lib/GameConstants.h"
#include "../lib/json/JsonNode.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/CPlayerState.h"
#include "../lib/mapping/CMap.h"
#include "../lib/callback/CCallback.h"
#include "../lib/StartInfo.h"

#include "GameInstance.h"
#include "GameEngine.h"
#include "CServerHandler.h"
#include "Client.h"
#include "CPlayerInterface.h"

#include "mcp/InfoTools.h"
#include "mcp/ActionTools.h"

McpServer::McpServer() :
	enabled(settings["mcp"]["enabled"].Bool())
{
#ifdef ENABLE_MCP_SERVER
	if(!enabled)
		return;

	auto config = mcp::server::configuration{};
	config.host = settings["mcp"]["host"].String();
	config.port = static_cast<int>(settings["mcp"]["port"].Integer());
	config.name = "VCMI MCP Server";
	config.version = GameConstants::VCMI_VERSION;
	config.threadpool_size = 2;
	config.max_sessions = 4;
	config.session_timeout = 0;

	if(config.host.empty())
		config.host = "localhost";
	if(config.port == 0)
		config.port = 9100;

	auto srv = std::make_unique<mcp::server>(config);

	srv->set_server_info(config.name, config.version);
	srv->set_capabilities(mcp::json{
		{"tools", mcp::json::object()},
		{"resources", mcp::json::object()}
	});

	registerInfoTools(srv.get());
	registerActionTools(srv.get());

	if(srv->start(false))
	{
		logGlobal->info("MCP server started on %s:%d", config.host, config.port);
		server = std::move(srv);
	}
	else
	{
		logGlobal->warn("MCP server failed to start on %s:%d - port already in use?", config.host, config.port);
	}
#endif
}

McpServer::~McpServer()
{
#ifdef ENABLE_MCP_SERVER
	if(!enabled || !server)
		return;

	server->stop();
#endif
}
