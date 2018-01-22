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
#include "../lib/mapping/CMapInfo.h"
#include "../lib/CGeneralTextHandler.h"

// For map options
#include "../lib/CHeroHandler.h"

// netpacks serialization
#include "../lib/CCreatureHandler.h"

// Applier
#include "../lib/registerTypes/RegisterTypes.h"
#include "../lib/NetPacks.h"
#include "pregame/CSelectionBase.h"
#include "pregame/CLobbyScreen.h"

template<typename T> class CApplyOnLobby;

class CBaseForLobbyApply
{
public:
	virtual void applyOnLobby(CLobbyScreen * selScr, void * pack) const = 0;
	virtual ~CBaseForLobbyApply(){};
	template<typename U> static CBaseForLobbyApply * getApplier(const U * t = nullptr)
	{
		return new CApplyOnLobby<U>();
	}
};

template<typename T> class CApplyOnLobby : public CBaseForLobbyApply
{
public:
	void applyOnLobby(CLobbyScreen * selScr, void * pack) const override
	{
		T * ptr = static_cast<T *>(pack);
		ptr->applyOnLobby(selScr);
	}
};

template<> class CApplyOnLobby<CPack>: public CBaseForLobbyApply
{
public:
	void applyOnLobby(CLobbyScreen * selScr, void * pack) const override
	{
		logGlobal->error("Cannot apply plain CPack!");
		assert(0);
	}
};

static CApplier<CBaseForLobbyApply> * applier = nullptr;

extern std::string NAME;

void CServerHandler::startLocalServerAndConnect()
{
	if(localServerThread)
		localServerThread->join();

	th.update();

#ifdef VCMI_ANDROID
	CAndroidVMHelper envHelper;
	envHelper.callStaticVoidMethod(CAndroidVMHelper::NATIVE_METHODS_DEFAULT_CLASS, "startServer", true);
#else
	localServerThread = new boost::thread(&CServerHandler::threadRunServer, this); //runs server executable;
#endif
	if(verbose)
		logNetwork->info("Setting up thread calling server: %d ms", th.getDiff());

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
	: LobbyInfo(), localServerThread(nullptr), shm(nullptr), verbose(true), serverHandlingThread(nullptr), mx(new boost::recursive_mutex), ongoingClosing(false)
{
	uuid = boost::uuids::to_string(boost::uuids::random_generator()());
}

CServerHandler::~CServerHandler()
{
	delete shm;
	delete localServerThread; //detaches, not kills thread
}

void CServerHandler::threadRunServer()
{
#ifndef VCMI_ANDROID
	setThreadName("CServerHandler::threadRunServer");
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
	}
	else
	{
		logNetwork->error("Error: server failed to close correctly or crashed!");
		logNetwork->error("Check %s for more info", logName);
		// TODO: make client return to main menu if server actually crashed during game.
//		exit(1);// exit in case of error. Othervice without working server VCMI will hang
	}
#endif
}

void CServerHandler::justConnectToServer(const std::string & addr, const ui16 port)
{
	while(!c)
	{
		try
		{
			logNetwork->info("Establishing connection...");
			c = std::make_shared<CConnection>(
					addr.size() ? addr : settings["server"]["server"].String(),
					port ? port : getDefaultPort(),
					NAME, uuid);
			c->connectionID = 1; // TODO: Refactoring for the server so IDs set outside of CConnection
		}
		catch(...)
		{
			logNetwork->error("\nCannot establish connection! Retrying within 2 seconds");
			// MPTODO: remove SDL dependency from server handler
			SDL_Delay(2000);
		}
	}
	serverHandlingThread = new boost::thread(&CServerHandler::threadHandleConnection, this);
}

void CServerHandler::stopConnection()
{
	c.reset();
}

void CServerHandler::sendPackToServer(CPackForLobby & pack)
{
	logNetwork->trace("Sending a pack of type %s", typeid(pack).name());
	*c << &pack;
}

void CServerHandler::stopServerConnection()
{
	ongoingClosing = true;
	if(serverHandlingThread)
	{
		stopServer();
		while(!serverHandlingThread->timed_join(boost::posix_time::milliseconds(50)))
			processPacks();
		serverHandlingThread->join();
		delete serverHandlingThread;
	}

	vstd::clear_pointer(applier);
	delete mx;
}

bool CServerHandler::isServerLocal() const
{
	if(localServerThread)
		return true;

	return false;
}

std::set<PlayerColor> CServerHandler::getHumanColors()
{
	std::set<PlayerColor> players;
	for(auto & elem : si->playerInfos)
	{
		for(ui8 id : elem.second.connectedPlayerIDs)
		{
			if(vstd::contains(getConnectedPlayerIdsForClient(c->connectionID), id))
			{
				players.insert(elem.first);
			}
		}
	}

	return players;
}

bool CServerHandler::isHost() const
{
	return c && hostConnectionId == c->connectionID;
}

bool CServerHandler::isGuest() const
{
	return !c || hostConnectionId != c->connectionID;
}


PlayerColor CServerHandler::myFirstColor() const
{
	for(auto & pair : si->playerInfos)
	{
		if(isMyColor(pair.first))
			return pair.first;
	}

	return PlayerColor::CANNOT_DETERMINE;
}

bool CServerHandler::isMyColor(PlayerColor color) const
{
	if(si->playerInfos.find(color) != si->playerInfos.end())
	{
		for(ui8 id : si->playerInfos.find(color)->second.connectedPlayerIDs)
		{
			if(c && playerNames.find(id) != playerNames.end())
			{
				if(playerNames.find(id)->second.connection == c->connectionID)
					return true;
			}
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

void CServerHandler::setPlayerOption(ui8 what, ui8 dir, PlayerColor player)
{
	LobbyChangePlayerOption lcpo;
	lcpo.what = what;
	lcpo.direction = dir;
	lcpo.color = player;
	sendPackToServer(lcpo);
}

void CServerHandler::prepareForLobby(const StartInfo::EMode mode, const std::vector<std::string> * names)
{
	if(si)
		si.reset();
	si = std::make_shared<StartInfo>();
	playerNames.clear();
	si->difficulty = 1;
	si->mode = mode;
	si->turnTime = 0;
	myNames.clear();
	if(names && !names->empty()) //if have custom set of player names - use it
		myNames = *names;
	else
		myNames.push_back(settings["general"]["playerName"].String());

#ifndef VCMI_ANDROID
	if(shm)
		vstd::clear_pointer(shm);

	if(!settings["session"]["disable-shm"].Bool())
	{
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
	}
#endif
}

void CServerHandler::propagateGuiAction(LobbyGuiAction & lga)
{
	sendPackToServer(lga);
}

void CServerHandler::startGame()
{
	if(!mi)
		throw mapMissingException();

	//there must be at least one human player before game can be started
	std::map<PlayerColor, PlayerSettings>::const_iterator i;
	for(i = si->playerInfos.cbegin(); i != si->playerInfos.cend(); i++)
		if(i->second.isControlledByHuman())
			break;

	if(i == si->playerInfos.cend() && !settings["session"]["onlyai"].Bool())
		throw noHumanException();

	if(si->mapGenOptions && si->mode == StartInfo::NEW_GAME)
	{
		if(!si->mapGenOptions->checkOptions())
			throw noTemplateException();
	}
	ongoingClosing = true;
	LobbyStartGame lsg;
	sendPackToServer(lsg);
}

void CServerHandler::sendMessage(const std::string & txt)
{
	std::istringstream readed;
	readed.str(txt);
	std::string command;
	readed >> command;
	if(command == "!passhost")
	{
		std::string id;
		readed >> id;
		if(id.length())
		{
			LobbyChangeHost lch;
			lch.newHostConnectionId = boost::lexical_cast<int>(id);
			sendPackToServer(lch);
		}
	}
	else if(command == "!forcep")
	{
		std::string connectedId, playerColorId;
		readed >> connectedId;
		readed >> playerColorId;
		if(connectedId.length(), playerColorId.length())
		{
			ui8 connected = boost::lexical_cast<int>(connectedId);
			auto color = PlayerColor(boost::lexical_cast<int>(playerColorId));
			if(color.isValidPlayer() && playerNames.find(connected) != playerNames.end())
			{
				LobbyForceSetPlayer lfsp;
				lfsp.targetConnectedPlayer = connected;
				lfsp.targetPlayerColor = color;
				sendPackToServer(lfsp);
			}
		}
	}
	else
	{
		LobbyChatMessage lcm;
		lcm.message = txt;
		lcm.playerName = playerNames[myFirstId()].name;
		sendPackToServer(lcm);
	}
}

void CServerHandler::stopServer()
{
	ongoingClosing = true;
	LobbyClientDisconnected lcd;
	lcd.connectionId = c->connectionID;
	lcd.shutdownServer = isServerLocal();
	sendPackToServer(lcd);
}

void CServerHandler::threadHandleConnection()
{
	setThreadName("CServerHandler::threadHandleConnection");
	c->enterPregameConnectionMode();
	applier = new CApplier<CBaseForLobbyApply>();
	registerTypesPregamePacks(*applier);

	try
	{
		LobbyClientConnected lcc;
		lcc.uuid = uuid;
		lcc.names = myNames;
		lcc.mode = si->mode;
		sendPackToServer(lcc);

		while(c)
		{
			CPackForLobby * pack = nullptr;
			*c >> pack;
			logNetwork->trace("Received a pack of type %s", typeid(*pack).name());
			assert(pack);
			if(LobbyClientDisconnected * endingPack = dynamic_cast<LobbyClientDisconnected *>(pack))
			{
				endingPack->applyOnLobby(static_cast<CLobbyScreen *>(SEL));
			}
			else if(LobbyStartGame * endingPack = dynamic_cast<LobbyStartGame *>(pack))
			{
				endingPack->applyOnLobby(static_cast<CLobbyScreen *>(SEL));
			}
			else
			{
				boost::unique_lock<boost::recursive_mutex> lock(*mx);
				upcomingPacks.push_back(pack);
			}
		}
	}
	catch(int i)
	{
		if(i != 666)
			throw;
	}
	catch(...)
	{
		handleException();
		throw;
	}
}

void CServerHandler::processPacks()
{
	boost::unique_lock<boost::recursive_mutex> lock(*mx);
	if(!serverHandlingThread)
		return;

	while(!upcomingPacks.empty())
	{
		CPackForLobby * pack = upcomingPacks.front();
		upcomingPacks.pop_front();
		CBaseForLobbyApply * apply = applier->getApplier(typeList.getTypeID(pack)); //find the applier
		apply->applyOnLobby(static_cast<CLobbyScreen *>(SEL), pack);
		delete pack;
	}
}

void CServerHandler::setPlayer(PlayerColor color)
{
	LobbySetPlayer lsp;
	lsp.clickedColor = color;
	sendPackToServer(lsp);
}

void CServerHandler::setTurnLength(int npos)
{
	vstd::amin(npos, ARRAY_COUNT(GameConstants::POSSIBLE_TURNTIME) - 1);
	LobbySetTurnTime lstt;
	lstt.turnTime = GameConstants::POSSIBLE_TURNTIME[npos];
	sendPackToServer(lstt);
}

void CServerHandler::setMapInfo(std::shared_ptr<CMapInfo> to, CMapGenOptions * mapGenOpts)
{
	LobbySetMap lsm;
	lsm.mapInfo = to;
	lsm.mapGenOpts = mapGenOpts;
	sendPackToServer(lsm);
}

void CServerHandler::setDifficulty(int to)
{
	LobbySetDifficulty lsd;
	lsd.difficulty = to;
	sendPackToServer(lsd);
}
