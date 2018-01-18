/*
 * CServerHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "CServerHandler.h"

#include "../lib/CConfigHandler.h"
#include "../lib/CThreadHelper.h"
#ifndef VCMI_ANDROID
#include "../lib/Interprocess.h"
#endif
#include "../lib/NetPacks.h"
#include "../lib/VCMIDirs.h"
#include "../lib/serializer/Connection.h"

#include <SDL.h>
#include "../lib/StartInfo.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

// MPTODO: for CServerHandler::getStartInfo
#include "../lib/rmg/CMapGenOptions.h"
#include "../lib/mapping/CCampaignHandler.h"

//
#include "CGameInfo.h"
#include "../lib/mapping/CMap.h"
#include "../lib/CGeneralTextHandler.h"

extern std::string NAME;

void CServerHandler::startServer()
{
	if(settings["session"]["donotstartserver"].Bool())
		return;

	th.update();

#ifdef VCMI_ANDROID
	CAndroidVMHelper envHelper;
	envHelper.callStaticVoidMethod(CAndroidVMHelper::NATIVE_METHODS_DEFAULT_CLASS, "startServer", true);
#else
	serverThread = new boost::thread(&CServerHandler::callServer, this); //runs server executable;
#endif
	if(verbose)
		logNetwork->info("Setting up thread calling server: %d ms", th.getDiff());
}

void CServerHandler::waitForServer()
{
	if(settings["session"]["donotstartserver"].Bool())
		return;

	if(serverThread)
		serverThread->join();
	startServer();

	th.update();

#ifndef VCMI_ANDROID
	if(shm)
		shm->sr->waitTillReady();
#else
	logNetwork->info("waiting for server");
	while (!androidTestServerReadyFlag.load())
	{
		logNetwork->info("still waiting...");
		boost::this_thread::sleep(boost::posix_time::milliseconds(1000));
	}
	logNetwork->info("waiting for server finished...");
	androidTestServerReadyFlag = false;
#endif
	if(verbose)
		logNetwork->info("Waiting for server: %d ms", th.getDiff());
}

void CServerHandler::startServerAndConnect()
{
	waitForServer();

	th.update(); //put breakpoint here to attach to server before it does something stupid

#ifndef VCMI_ANDROID
	justConnectToServer(settings["server"]["server"].String(), shm ? shm->sr->port : 0);
#else
	justConnectToServer(settings["server"]["server"].String());
#endif

	if(verbose)
		logNetwork->info("\tConnecting to the server: %d ms", th.getDiff());
}

ui16 CServerHandler::getDefaultPort()
{
	if(settings["session"]["serverport"].Integer())
		return settings["session"]["serverport"].Integer();
	else
		return settings["server"]["port"].Integer();
}

std::string CServerHandler::getDefaultPortStr()
{
	return boost::lexical_cast<std::string>(getDefaultPort());
}

CServerHandler::CServerHandler()
	: c(nullptr), serverThread(nullptr), shm(nullptr), verbose(true), host(false)
{
	uuid = boost::uuids::to_string(boost::uuids::random_generator()());

#ifndef VCMI_ANDROID
	if(settings["session"]["donotstartserver"].Bool() || settings["session"]["disable-shm"].Bool())
		return;

	std::string sharedMemoryName = "vcmi_memory";
	if(settings["session"]["enable-shm-uuid"].Bool())
	{
		//used or automated testing when multiple clients start simultaneously
		sharedMemoryName += "_" + uuid;
	}
	try
	{
		shm = new SharedMemory(sharedMemoryName, true);
	}
	catch(...)
	{
		vstd::clear_pointer(shm);
		logNetwork->error("Cannot open interprocess memory. Continue without it...");
	}
#endif
}

CServerHandler::~CServerHandler()
{
	delete shm;
	delete serverThread; //detaches, not kills thread
}

void CServerHandler::callServer()
{
#ifndef VCMI_ANDROID
	setThreadName("CServerHandler::callServer");
	const std::string logName = (VCMIDirs::get().userCachePath() / "server_log.txt").string();
	std::string comm = VCMIDirs::get().serverPath().string()
		+ " --port=" + getDefaultPortStr()
		+ " --run-by-client"
		+ " --uuid=" + uuid;
	if(shm)
	{
		comm += " --enable-shm";
		if(settings["session"]["enable-shm-uuid"].Bool())
			comm += " --enable-shm-uuid";
	}
	comm += " > \"" + logName + '\"';

	int result = std::system(comm.c_str());
	if (result == 0)
	{
		logNetwork->info("Server closed correctly");
		CServerHandler::serverAlive.setn(false);
	}
	else
	{
		logNetwork->error("Error: server failed to close correctly or crashed!");
		logNetwork->error("Check %s for more info", logName);
		CServerHandler::serverAlive.setn(false);
		// TODO: make client return to main menu if server actually crashed during game.
//		exit(1);// exit in case of error. Othervice without working server VCMI will hang
	}
#endif
}

void CServerHandler::justConnectToServer(const std::string &host, const ui16 port)
{
	while(!c)
	{
		try
		{
			logNetwork->info("Establishing connection...");
			c = new CConnection(	host.size() ? host : settings["server"]["server"].String(),
									port ? port : getDefaultPort(),
									NAME);
			c->connectionID = 1; // TODO: Refactoring for the server so IDs set outside of CConnection
		}
		catch(...)
		{
			logNetwork->error("\nCannot establish connection! Retrying within 2 seconds");
			// MPTODO: remove SDL dependency from server handler
			SDL_Delay(2000);
		}
	}
}

void CServerHandler::welcomeServer()
{
	c->enterPregameConnectionMode();

	WelcomeServer ws(uuid, myNames);
	*c << &ws;
}

void CServerHandler::stopConnection()
{
	vstd::clear_pointer(c);
}

bool CServerHandler::isServerLocal()
{
	if(serverThread)
		return true;

	return false;
}

const PlayerSettings * CServerHandler::getPlayerSettings(ui8 connectedPlayerId) const
{
	for(auto & elem : si.playerInfos)
	{
		if(elem.second.connectedPlayerID == connectedPlayerId)
			return &elem.second;
	}
	return nullptr;
}

std::set<PlayerColor> CServerHandler::getPlayers()
{
	std::set<PlayerColor> players;
	for(auto & elem : si.playerInfos)
	{
		if(isHost() && elem.second.connectedPlayerID == PlayerSettings::PLAYER_AI || vstd::contains(getMyIds(), elem.second.connectedPlayerID))
		{
			players.insert(elem.first); //add player
		}
	}
	if(isHost())
		players.insert(PlayerColor::NEUTRAL);

	return players;
}

std::set<PlayerColor> CServerHandler::getHumanColors()
{
	std::set<PlayerColor> players;
	for(auto & elem : si.playerInfos)
	{
		if(vstd::contains(getMyIds(), elem.second.connectedPlayerID))
		{
			players.insert(elem.first); //add player
		}
	}

	return players;
}

bool CServerHandler::isHost() const
{
	return host;
}

bool CServerHandler::isGuest() const
{
	return !host;
}

void CServerHandler::setPlayer(PlayerSettings & pset, ui8 player) const
{
	if(vstd::contains(playerNames, player))
		pset.name = playerNames.find(player)->second.name;
	else
		pset.name = CGI->generaltexth->allTexts[468]; //Computer

	pset.connectedPlayerID = player;
}

void CServerHandler::updateStartInfo(std::string filename, StartInfo & sInfo, const std::unique_ptr<CMapHeader> & mapHeader) const
{
	sInfo.playerInfos.clear();
	if(!mapHeader.get())
	{
		return;
	}

	sInfo.mapname = filename;

	auto namesIt = playerNames.cbegin();

	for(int i = 0; i < mapHeader->players.size(); i++)
	{
		const PlayerInfo & pinfo = mapHeader->players[i];

		//neither computer nor human can play - no player
		if(!(pinfo.canHumanPlay || pinfo.canComputerPlay))
			continue;

		PlayerSettings & pset = sInfo.playerInfos[PlayerColor(i)];
		pset.color = PlayerColor(i);
		if(pinfo.canHumanPlay && namesIt != playerNames.cend())
		{
			setPlayer(pset, namesIt++->first);
		}
		else
		{
			setPlayer(pset, 0);
			if(!pinfo.canHumanPlay)
			{
				pset.compOnly = true;
			}
		}

		pset.castle = pinfo.defaultCastle();
		pset.hero = pinfo.defaultHero();

		if(pset.hero != PlayerSettings::RANDOM && pinfo.hasCustomMainHero())
		{
			pset.hero = pinfo.mainCustomHeroId;
			pset.heroName = pinfo.mainCustomHeroName;
			pset.heroPortrait = pinfo.mainCustomHeroPortrait;
		}

		pset.handicap = PlayerSettings::NO_HANDICAP;
	}
}


PlayerColor CServerHandler::myFirstColor() const
{
	for(auto & pair : si.playerInfos)
	{
		if(isMyColor(pair.first))
			return pair.first;
	}

	return PlayerColor::CANNOT_DETERMINE;
}

bool CServerHandler::isMyColor(PlayerColor color) const
{
	if(si.playerInfos.find(color) != si.playerInfos.end())
	{
		ui8 id = si.playerInfos.find(color)->second.connectedPlayerID;
		if(c && playerNames.find(id) != playerNames.end())
		{
			if(playerNames.find(id)->second.connection == c->connectionID)
				return true;
		}
	}
	return false;
}

ui8 CServerHandler::myFirstId() const
{
	for(auto & pair : playerNames)
	{
		if(pair.second.connection == c->connectionID)
			return pair.first;
	}

	return 0;
}

bool CServerHandler::isMyId(ui8 id) const
{
	for(auto & pair : playerNames)
	{
		if(pair.second.connection == c->connectionID && pair.second.color == id)
			return true;
	}
	return false;
}

std::vector<ui8> CServerHandler::getMyIds() const
{
	std::vector<ui8> ids;

	for(auto & pair : playerNames)
	{
		if(pair.second.connection == c->connectionID)
		{
			for(auto & elem : si.playerInfos)
			{
				if(elem.second.connectedPlayerID == pair.first)
					ids.push_back(elem.second.connectedPlayerID);
			}
		}
	}
	return ids;
}

ui8 CServerHandler::getIdOfFirstUnallocatedPlayer() //MPTODO: must be const
{
	for(auto i = playerNames.cbegin(); i != playerNames.cend(); i++)
	{
		if(!si.getPlayersSettings(i->first))
			return i->first;
	}

	return 0;
}
