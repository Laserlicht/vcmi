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
#include "../lib/networkPacks/NetPacksBase.h"

#include "mcp/EventJournal.h"
#include "mcp/RequestTracker.h"
#include "mcp/QueryRegistry.h"
#include "mcp/JournalVisitor.h"

#include "mcp/InfoTools.h"
#include "mcp/AdventureInfoTools.h"
#include "mcp/ActionTools.h"
#include "mcp/ArmyTools.h"
#include "mcp/ArtifactTools.h"
#include "mcp/TownTools.h"
#include "mcp/BattleTools.h"
#include "mcp/QueryTools.h"

McpServer::McpServer() :
	enabled(settings["mcp"]["enabled"].Bool())
{
	int journalSize = static_cast<int>(settings["mcp"]["journalSize"].Integer());
	journalInstance = std::make_unique<mcptool::EventJournal>(journalSize > 0 ? journalSize : 4096);
	requestTrackerInstance = std::make_unique<mcptool::RequestTracker>();
	queryRegistryInstance = std::make_unique<mcptool::QueryRegistry>();
	visitorInstance = std::make_unique<JournalVisitor>(*journalInstance, *requestTrackerInstance, *queryRegistryInstance);

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
	registerAdventureInfoTools(srv.get());
	registerActionTools(srv.get());
	registerArmyTools(srv.get());
	registerArtifactTools(srv.get());
	registerTownTools(srv.get());
	registerBattleTools(srv.get());
	registerQueryTools(srv.get());

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

void McpServer::onPackApplied(CPackForClient & pack)
{
	if(!enabled)
		return;

	pack.visit(*visitorInstance);
}
