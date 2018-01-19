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
#include "pregame/CSelectionScreen.h"

template<typename T> class CApplyOnPG;

class CBaseForPGApply
{
public:
	virtual void applyOnPG(CSelectionScreen * selScr, void * pack) const = 0;
	virtual ~CBaseForPGApply(){};
	template<typename U> static CBaseForPGApply * getApplier(const U * t = nullptr)
	{
		return new CApplyOnPG<U>();
	}
};

template<typename T> class CApplyOnPG : public CBaseForPGApply
{
public:
	void applyOnPG(CSelectionScreen * selScr, void * pack) const override
	{
		T * ptr = static_cast<T *>(pack);
		ptr->apply(selScr);
	}
};

template<> class CApplyOnPG<CPack>: public CBaseForPGApply
{
public:
	void applyOnPG(CSelectionScreen * selScr, void * pack) const override
	{
		logGlobal->error("Cannot apply on PG plain CPack!");
		assert(0);
	}
};

static CApplier<CBaseForPGApply> * applier = nullptr;

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
	: c(nullptr), serverThread(nullptr), shm(nullptr), verbose(true), host(false), serverHandlingThread(nullptr), mx(new boost::recursive_mutex), ongoingClosing(false)
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
		serverHandlingThread = new boost::thread(&CServerHandler::handleConnection, this);
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

void CServerHandler::stopServerConnection()
{
	ongoingClosing = true;
	if(serverHandlingThread)
	{
		while(!serverHandlingThread->timed_join(boost::posix_time::milliseconds(50)))
			processPacks();
		serverHandlingThread->join();
		delete serverHandlingThread;
	}

	vstd::clear_pointer(applier);
	delete mx;
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

void CServerHandler::updateStartInfo()
{
	si.playerInfos.clear();
	if(!current)
		return;

	si.mapname = current->fileURI;

	auto namesIt = playerNames.cbegin();

	for(int i = 0; i < current->mapHeader->players.size(); i++)
	{
		const PlayerInfo & pinfo = current->mapHeader->players[i];

		//neither computer nor human can play - no player
		if(!(pinfo.canHumanPlay || pinfo.canComputerPlay))
			continue;

		PlayerSettings & pset = si.playerInfos[PlayerColor(i)];
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

void CServerHandler::requestPlayerOptionChange(ui8 what, ui8 dir, PlayerColor player)
{
	RequestOptionsChange roc(what, dir, si.playerInfos[player].connectedPlayerID);
	*c << &roc;
}

void CServerHandler::optionNextCastle(PlayerColor player, int dir)
{
	PlayerSettings & s = si.playerInfos[player];
	si16 & cur = s.castle;
	auto & allowed = current->mapHeader->players[s.color.getNum()].allowedFactions;
	const bool allowRandomTown = current->mapHeader->players[s.color.getNum()].isFactionRandom;

	if(cur == PlayerSettings::NONE) //no change
		return;

	if(cur == PlayerSettings::RANDOM) //first/last available
	{
		if(dir > 0)
			cur = *allowed.begin(); //id of first town
		else
			cur = *allowed.rbegin(); //id of last town

	}
	else // next/previous available
	{
		if((cur == *allowed.begin() && dir < 0) || (cur == *allowed.rbegin() && dir > 0))
		{
			if(allowRandomTown)
			{
				cur = PlayerSettings::RANDOM;
			}
			else
			{
				if(dir > 0)
					cur = *allowed.begin();
				else
					cur = *allowed.rbegin();
			}
		}
		else
		{
			assert(dir >= -1 && dir <= 1); //othervice std::advance may go out of range
			auto iter = allowed.find(cur);
			std::advance(iter, dir);
			cur = *iter;
		}
	}

	if(s.hero >= 0 && !current->mapHeader->players[s.color.getNum()].hasCustomMainHero()) // remove hero unless it set to fixed one in map editor
	{
		s.hero = PlayerSettings::RANDOM;
	}
	if(cur < 0 && s.bonus == PlayerSettings::RESOURCE)
		s.bonus = PlayerSettings::RANDOM;
}

void CServerHandler::optionNextHero(PlayerColor player, int dir)
{
	PlayerSettings & s = si.playerInfos[player];
	if(s.castle < 0 || s.connectedPlayerID == PlayerSettings::PLAYER_AI || s.hero == PlayerSettings::NONE)
		return;

	if(s.hero == PlayerSettings::RANDOM) // first/last available
	{
		int max = CGI->heroh->heroes.size(),
			min = 0;
		s.hero = nextAllowedHero(player, min, max, 0, dir);
	}
	else
	{
		if(dir > 0)
			s.hero = nextAllowedHero(player, s.hero, CGI->heroh->heroes.size(), 1, dir);
		else
			s.hero = nextAllowedHero(player, -1, s.hero, 1, dir); // min needs to be -1 -- hero at index 0 would be skipped otherwise
	}
}

int CServerHandler::nextAllowedHero(PlayerColor player, int min, int max, int incl, int dir)
{
	if(dir > 0)
	{
		for(int i = min + incl; i <= max - incl; i++)
			if(canUseThisHero(player, i))
				return i;
	}
	else
	{
		for(int i = max - incl; i >= min + incl; i--)
			if(canUseThisHero(player, i))
				return i;
	}
	return -1;
}

void CServerHandler::optionNextBonus(PlayerColor player, int dir)
{
	PlayerSettings & s = si.playerInfos[player];
	PlayerSettings::Ebonus & ret = s.bonus = static_cast<PlayerSettings::Ebonus>(static_cast<int>(s.bonus) + dir);

	if(s.hero == PlayerSettings::NONE &&
		!current->mapHeader->players[s.color.getNum()].heroesNames.size() &&
		ret == PlayerSettings::ARTIFACT) //no hero - can't be artifact
	{
		if(dir < 0)
			ret = PlayerSettings::RANDOM;
		else
			ret = PlayerSettings::GOLD;
	}

	if(ret > PlayerSettings::RESOURCE)
		ret = PlayerSettings::RANDOM;
	if(ret < PlayerSettings::RANDOM)
		ret = PlayerSettings::RESOURCE;

	if(s.castle == PlayerSettings::RANDOM && ret == PlayerSettings::RESOURCE) //random castle - can't be resource
	{
		if(dir < 0)
			ret = PlayerSettings::GOLD;
		else
			ret = PlayerSettings::RANDOM;
	}
}

bool CServerHandler::canUseThisHero(PlayerColor player, int ID)
{
	return CGI->heroh->heroes.size() > ID
		&& si.playerInfos[player].castle == CGI->heroh->heroes[ID]->heroClass->faction
		&& !vstd::contains(getUsedHeroes(), ID)
		&& current->mapHeader->allowedHeroes[ID];
}

std::vector<int> CServerHandler::getUsedHeroes()
{
	std::vector<int> heroIds;
	for(auto & p : si.playerInfos)
	{
		const auto & heroes = current->mapHeader->players[p.first.getNum()].heroesNames;
		for(auto & hero : heroes)
			if(hero.heroId >= 0) //in VCMI map format heroId = -1 means random hero
				heroIds.push_back(hero.heroId);

		if(p.second.hero != PlayerSettings::RANDOM)
			heroIds.push_back(p.second.hero);
	}
	return heroIds;
}

void CServerHandler::reset(StartInfo::EMode mode)
{
	playerNames.clear();
	si.difficulty = 1;
	si.mode = mode;
	si.turnTime = 0;
	myNames.clear();
}

void CServerHandler::propagateNames() const
{
	if(isGuest() || !c)
		return;

	PlayersNames pn;
	pn.playerNames = playerNames;
	*c << &pn;
}

void CServerHandler::propagateOptions()
{
	if(isGuest() || !c)
		return;

	UpdateStartOptions ups;
	ups.si = &si;
	*c << &ups;
}

void CServerHandler::propagateGuiAction(PregameGuiAction & pga)
{
	if(isGuest() || !c)
		return;

	*c << &pga;
}

void CServerHandler::tryStartGame()
{
	if(!current)
		throw mapMissingException();

	//there must be at least one human player before game can be started
	std::map<PlayerColor, PlayerSettings>::const_iterator i;
	for(i = si.playerInfos.cbegin(); i != si.playerInfos.cend(); i++)
		if(i->second.connectedPlayerID != PlayerSettings::PLAYER_AI)
			break;

	if(i == si.playerInfos.cend() && !settings["session"]["onlyai"].Bool())
		throw noHumanException();

	if(si.mapGenOptions && si.mode == StartInfo::NEW_GAME)
	{
		// Update player settings for RMG
		for(const auto & psetPair : si.playerInfos)
		{
			const auto & pset = psetPair.second;
			si.mapGenOptions->setStartingTownForPlayer(pset.color, pset.castle);
			if(pset.connectedPlayerID != PlayerSettings::PLAYER_AI)
			{
				si.mapGenOptions->setPlayerTypeForStandardPlayer(pset.color, EPlayerType::HUMAN);
			}
		}

		if(!si.mapGenOptions->checkOptions())
			throw noTemplateException();

		propagateOptions();
	}
	ongoingClosing = true;
	StartWithCurrentSettings swcs;
	*c << &swcs;
}

void CServerHandler::postChatMessage(const std::string & txt)
{
	std::istringstream readed;
	readed.str(txt);
	std::string command;
	readed >> command;
	if(command == "!passhost")
	{
		std::string id;
		readed >> id;
		PassHost ph;
		if(id.length())
		{
			ph.toConnection = boost::lexical_cast<int>(id);
			*c << &ph;
		}
	}
	else
	{
		ChatMessage cm;
		cm.message = txt;
		cm.playerName = playerNames[myFirstId()].name;
		*c << &cm;
	}
}

void CServerHandler::quitWithoutStarting()
{
	ongoingClosing = true;
	QuitMenuWithoutStarting qmws;
	*c << &qmws;
}

PlayerInfo CServerHandler::getPlayerInfo(int color) const
{
	return current->mapHeader->players[color];
}


void CServerHandler::handleConnection()
{
	setThreadName("CServerHandler::handleConnection");

	while(!c)
		boost::this_thread::sleep(boost::posix_time::milliseconds(5));

	applier = new CApplier<CBaseForPGApply>();
	registerTypesPregamePacks(*applier);
	welcomeServer();

	if(isHost())
	{
		if(current)
		{
//			propagateMap();
			propagateOptions();
		}
	}

	try
	{
		while(c)
		{
			CPackForSelectionScreen * pack = nullptr;
			*c >> pack;
			logNetwork->trace("Received a pack of type %s", typeid(*pack).name());
			assert(pack);
			if(QuitMenuWithoutStarting * endingPack = dynamic_cast<QuitMenuWithoutStarting *>(pack))
			{
				endingPack->apply(static_cast<CSelectionScreen *>(SEL));
			}
			else if(StartWithCurrentSettings * endingPack = dynamic_cast<StartWithCurrentSettings *>(pack))
			{
				endingPack->apply(static_cast<CSelectionScreen *>(SEL));
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
	if(!serverHandlingThread)
		return;

	boost::unique_lock<boost::recursive_mutex> lock(*mx);
	while(!upcomingPacks.empty())
	{
		CPackForSelectionScreen * pack = upcomingPacks.front();
		upcomingPacks.pop_front();
		CBaseForPGApply * apply = applier->getApplier(typeList.getTypeID(pack)); //find the applier
		apply->applyOnPG(static_cast<CSelectionScreen *>(SEL), pack);
		delete pack;
	}
}

void CServerHandler::optionsFlagPressed(PlayerColor color)
{
	struct PlayerToRestore
	{
		PlayerColor color;
		int id;
		void reset() { id = -1; color = PlayerColor::CANNOT_DETERMINE; }
		PlayerToRestore(){ reset(); }
	} playerToRestore;

	PlayerSettings & clicked = si.playerInfos[color];
	PlayerSettings * old = nullptr;

	//identify clicked player
	int clickedNameID = clicked.connectedPlayerID; //human is a number of player, zero means AI
	if(clickedNameID > 0 && playerToRestore.id == clickedNameID) //player to restore is about to being replaced -> put him back to the old place
	{
		PlayerSettings & restPos = si.playerInfos[playerToRestore.color];
		setPlayer(restPos, playerToRestore.id);
		playerToRestore.reset();
	}

	int newPlayer; //which player will take clicked position

	//who will be put here?
	if(!clickedNameID) //AI player clicked -> if possible replace computer with unallocated player
	{
		newPlayer = getIdOfFirstUnallocatedPlayer();
		if(!newPlayer) //no "free" player -> get just first one
			newPlayer = playerNames.begin()->first;
	}
	else //human clicked -> take next
	{
		auto i = playerNames.find(clickedNameID); //clicked one
		i++; //player AFTER clicked one

		if(i != playerNames.end())
			newPlayer = i->first;
		else
			newPlayer = 0; //AI if we scrolled through all players
	}

	setPlayer(clicked, newPlayer); //put player

	//if that player was somewhere else, we need to replace him with computer
	if(newPlayer) //not AI
	{
		for(auto i = si.playerInfos.begin(); i != si.playerInfos.end(); i++)
		{
			int curNameID = i->second.connectedPlayerID;
			if(i->first != color && curNameID == newPlayer)
			{
				assert(i->second.connectedPlayerID);
				playerToRestore.color = i->first;
				playerToRestore.id = newPlayer;
				setPlayer(i->second, 0); //set computer
				old = &i->second;
				break;
			}
		}
	}
	propagateOptions();
}

void CServerHandler::optionSetTurnLength(int npos)
{
	vstd::amin(npos, ARRAY_COUNT(GameConstants::POSSIBLE_TURNTIME) - 1);
	si.turnTime = GameConstants::POSSIBLE_TURNTIME[npos];
	propagateOptions();
}

void CServerHandler::requestChangeSelection(std::shared_ptr<CMapInfo> to)
{
	if(isGuest() || !c || current == to)
		return;

	SelectMap sm;
	sm.mapInfo = to.get();
	*c << &sm;
}
