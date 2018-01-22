/*
 * CVCMIServer.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include <boost/asio.hpp>

#include "../lib/filesystem/Filesystem.h"
#include "../lib/mapping/CCampaignHandler.h"
#include "../lib/CThreadHelper.h"
#include "../lib/serializer/Connection.h"
#include "../lib/CModHandler.h"
#include "../lib/CArtHandler.h"
#include "../lib/CGeneralTextHandler.h"
#include "../lib/CHeroHandler.h"
#include "../lib/CTownHandler.h"
#include "../lib/CBuildingHandler.h"
#include "../lib/spells/CSpellHandler.h"
#include "../lib/CCreatureHandler.h"
#include "zlib.h"
#include "CVCMIServer.h"
#include "../lib/StartInfo.h"
#include "../lib/mapping/CMap.h"
#include "../lib/rmg/CMapGenOptions.h"
#ifdef VCMI_ANDROID
#include "lib/CAndroidVMHelper.h"
#else
#include "../lib/Interprocess.h"
#endif
#include "../lib/VCMI_Lib.h"
#include "../lib/VCMIDirs.h"
#include "CGameHandler.h"
#include "../lib/mapping/CMapInfo.h"
#include "../lib/GameConstants.h"
#include "../lib/logging/CBasicLogConfigurator.h"
#include "../lib/CConfigHandler.h"
#include "../lib/ScopeGuard.h"

#include "../lib/UnlockGuard.h"

// for applier
#include "../lib/registerTypes/RegisterTypes.h"

// UUID generation
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#if defined(__GNUC__) && !defined(__MINGW32__) && !defined(VCMI_ANDROID)
#include <execinfo.h>
#endif

template<typename T> class CApplyOnServer;

class CBaseForServerApply
{
public:
	virtual bool applyOnServerBefore(CVCMIServer * srv, std::shared_ptr<CConnection> c, void * pack) const =0;
	virtual void applyOnServerAfter(CVCMIServer * srv, std::shared_ptr<CConnection> c, void * pack) const =0;
	virtual ~CBaseForServerApply() {}
	template<typename U> static CBaseForServerApply * getApplier(const U * t = nullptr)
	{
		return new CApplyOnServer<U>();
	}
};

template <typename T> class CApplyOnServer : public CBaseForServerApply
{
public:
	bool applyOnServerBefore(CVCMIServer * srv, std::shared_ptr<CConnection> c, void * pack) const override
	{
		T * ptr = static_cast<T *>(pack);
		ptr->applied = true;
		if(!ptr->c && c)
			ptr->c = std::shared_ptr<CConnection>(c);

		if(ptr->checkClientPermissions(srv))
			return ptr->applyOnServer(srv);
		else
			return false;
	}

	void applyOnServerAfter(CVCMIServer * srv, std::shared_ptr<CConnection> c, void * pack) const override
	{
		T * ptr = static_cast<T *>(pack);
		ptr->c = c;
		ptr->applyServerAfter(srv);
	}
};

template <>
class CApplyOnServer<CPack> : public CBaseForServerApply
{
public:
	bool applyOnServerBefore(CVCMIServer * srv, std::shared_ptr<CConnection> c, void * pack) const override
	{
		logGlobal->error("Cannot apply plain CPack!");
		assert(0);
		return false;
	}
	void applyOnServerAfter(CVCMIServer * srv, std::shared_ptr<CConnection> c, void * pack) const override
	{
		logGlobal->error("Cannot apply plain CPack!");
		assert(0);
	}
};

static CApplier<CBaseForServerApply> * applier = nullptr;

std::string NAME_AFFIX = "server";
std::string NAME = GameConstants::VCMI_VERSION + std::string(" (") + NAME_AFFIX + ')';

std::atomic<bool> CVCMIServer::shuttingDown;

CVCMIServer::CVCMIServer(boost::program_options::variables_map & opts)
	: LobbyInfo(), port(3030), io(new boost::asio::io_service()), shm(nullptr), listeningThreads(0), upcomingConnection(nullptr), state(RUNNING), cmdLineOptions(opts), currentPlayerId(1)
{
	uuid = boost::uuids::to_string(boost::uuids::random_generator()());
	logNetwork->trace("CVCMIServer created! UUID: %s", uuid);
	applier = new CApplier<CBaseForServerApply>();
	registerTypesPregamePacks(*applier);

	if(cmdLineOptions.count("port"))
		port = cmdLineOptions["port"].as<ui16>();
	logNetwork->info("Port %d will be used", port);
	try
	{
		acceptor = new TAcceptor(*io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port));
	}
	catch(...)
	{
		logNetwork->info("Port %d is busy, trying to use random port instead", port);
		if(cmdLineOptions.count("run-by-client") && !cmdLineOptions.count("enable-shm"))
		{
			logNetwork->error("Cant pass port number to client without shared memory!", port);
			exit(0);
		}
		acceptor = new TAcceptor(*io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
		port = acceptor->local_endpoint().port();
	}
	logNetwork->info("Listening for connections at port %d", port);
}

CVCMIServer::~CVCMIServer()
{
	delete acceptor;
	delete upcomingConnection;

	for(CPackForLobby * pack : announceQueue)
		delete pack;

	announceQueue.clear();

	//TODO pregameconnections
}

void CVCMIServer::start()
{
#ifndef VCMI_ANDROID
	if(cmdLineOptions.count("enable-shm"))
	{
		std::string sharedMemoryName = "vcmi_memory";
		if(cmdLineOptions.count("enable-shm-uuid") && cmdLineOptions.count("uuid"))
		{
			sharedMemoryName += "_" + cmdLineOptions["uuid"].as<std::string>();
		}
		shm = new SharedMemory(sharedMemoryName);
	}
#endif

	startAsyncAccept();
	if(shm)
	{
		shm->sr->setToReadyAndNotify(port);
	}

	while(state == RUNNING)
	{
		{
			boost::unique_lock<boost::recursive_mutex> myLock(mx);
			while(!announceQueue.empty())
			{
				processPack(announceQueue.front());
				announceQueue.pop_front();
			}
			if(state != RUNNING)
			{
				logNetwork->info("Stopping listening for connections...");
				acceptor->close();
			}

			if(acceptor)
			{
				acceptor->get_io_service().reset();
				acceptor->get_io_service().poll();
			}
		} //frees lock

		boost::this_thread::sleep(boost::posix_time::milliseconds(50));
	}

	logNetwork->info("Thread handling connections ended");

	if(state == ENDING_WITHOUT_START)
	{
		return;
	}

	if(state == ENDING_AND_STARTING_GAME)
	{
		logNetwork->info("Waiting for listening thread to finish...");
		while(listeningThreads)
			boost::this_thread::sleep(boost::posix_time::milliseconds(50));

		startGame();
	}
}

void CVCMIServer::startGame()
{
	logNetwork->info("Preparing new game");
	CGameHandler gh;

	switch(si->mode)
	{
	case StartInfo::NEW_GAME:
		gh.init(si.get());
		break;

	case StartInfo::LOAD_GAME:
		CLoadFile lf(*CResourceHandler::get("local")->getResourceName(ResourceID(si->mapname, EResType::SERVER_SAVEGAME)), MINIMAL_SERIALIZATION_VERSION);
		gh.loadCommonState(lf);
		lf >> gh;
		break;
	}
	gh.conns = connections;
	for(auto c : gh.conns) // MPTODO: should we need to do that if we sent gamestate?
		c->addStdVecItems(gh.gs);

	gh.run(si->mode == StartInfo::LOAD_GAME, this);
}

void CVCMIServer::startAsyncAccept()
{
	assert(!upcomingConnection);
	assert(acceptor);

	upcomingConnection = new TSocket(acceptor->get_io_service());
	acceptor->async_accept(*upcomingConnection, std::bind(&CVCMIServer::connectionAccepted, this, _1));
}

void CVCMIServer::connectionAccepted(const boost::system::error_code & ec)
{
	if(ec)
	{
		if(state != ENDING_WITHOUT_START)
			logNetwork->info("Something wrong during accepting: %s", ec.message());
		return;
	}

	try
	{
		logNetwork->info("We got a new connection! :)");
		auto pc = std::make_shared<CConnection>(upcomingConnection, NAME, uuid);
		upcomingConnection = nullptr;
		connections.insert(pc);
		startListeningThread(pc);
	}
	catch(std::exception & e)
	{
		upcomingConnection = nullptr;
		logNetwork->info("I guess it was just my imagination!");
	}

	startAsyncAccept();
}

void CVCMIServer::startListeningThread(std::shared_ptr<CConnection> pc)
{
	listeningThreads++;
	pc->enterPregameConnectionMode();
	pc->handler = new boost::thread(&CVCMIServer::handleConnection, this, pc);
}

void CVCMIServer::handleConnection(std::shared_ptr<CConnection> cpc)
{
	setThreadName("CVCMIServer::handleConnection");
	try
	{
		while(!cpc->receivedStop)
		{
			CPackForLobby * pack = nullptr;
			*cpc >> pack;
			logNetwork->info("Got package to announce %s from %s", typeid(*pack).name(), cpc->toString());
			CBaseForServerApply * apply = applier->getApplier(typeList.getTypeID(pack)); //find the applier
			if(apply->applyOnServerBefore(this, cpc, pack))
			{
				addToAnnounceQueue(pack);
				apply->applyOnServerAfter(this, cpc, pack);
			}
			else
				delete pack;
		}
	}
	catch(const std::exception & e)
	{
		boost::unique_lock<boost::recursive_mutex> queueLock(mx);
		logNetwork->error("%s dies... \nWhat happened: %s", cpc->toString(), e.what());
	}

	boost::unique_lock<boost::recursive_mutex> queueLock(mx);
	if(state != ENDING_AND_STARTING_GAME)
	{
		auto lcd = new LobbyClientDisconnected();
		lcd->c = cpc;
		addToAnnounceQueue(lcd, true);
	}

	logNetwork->info("Thread listening for %s ended", cpc->toString());
	listeningThreads--;
	vstd::clear_pointer(cpc->handler);
}

void CVCMIServer::processPack(CPackForLobby * pack)
{
	if(!pack->applied)
	{
		CBaseForServerApply * apply = applier->getApplier(typeList.getTypeID(pack));
		if(apply->applyOnServerBefore(this, nullptr, pack))
		{
			apply->applyOnServerAfter(this, nullptr, pack);
		}
	}
	announcePack(*pack);
	delete pack;
}

void CVCMIServer::sendPack(std::shared_ptr<CConnection> pc, const CPackForLobby & pack)
{
	if(!pc->sendStop)
	{
		logNetwork->info("\tSending pack of type %s to %s", typeid(pack).name(), pc->toString());
		*pc << &pack;
	}

	if(dynamic_ptr_cast<QuitMenuWithoutStarting>(&pack))
	{
		pc->sendStop = true;
	}
	else if(dynamic_ptr_cast<LobbyStartGame>(&pack))
	{
		pc->sendStop = true;
	}
}

void CVCMIServer::announcePack(const CPackForLobby & pack)
{
	for(auto pc : connections)
		sendPack(pc, pack);
}

void CVCMIServer::announceTxt(const std::string & txt, const std::string & playerName)
{
	logNetwork->info("%s says: %s", playerName, txt);
	auto cm = new LobbyChatMessage();
	cm->playerName = playerName;
	cm->message = txt;
	addToAnnounceQueue(cm);
}

void CVCMIServer::addToAnnounceQueue(CPackForLobby * pack, bool front)
{
	boost::unique_lock<boost::recursive_mutex> queueLock(mx);
	if(front)
		announceQueue.push_front(pack);
	else
		announceQueue.push_back(pack);
}

void CVCMIServer::passHost(int toConnectionId)
{
	for(auto c : connections)
	{
		if(c->connectionID != toConnectionId)
			continue;
		if(hostClient == c)
			return;

		hostClient = c;
		hostConnectionId = c->connectionID;
		announceTxt(boost::str(boost::format("Pass host to connection %d") % toConnectionId));
		auto ph = new LobbyChangeHost();
		ph->newHostConnectionId = toConnectionId;
		addToAnnounceQueue(ph);
		return;
	}
}

void CVCMIServer::clientConnected(std::shared_ptr<CConnection> c)
{
	for(auto & name : c->names)
	{
		ui8 id = currentPlayerId++;

		ClientPlayer cp;
		cp.connection = c->connectionID;
		cp.name = name;
		cp.color = 255;
		playerNames.insert(std::make_pair(id, cp));


		//put new player in first slot with AI
		for(auto & elem : si->playerInfos)
		{
			if(elem.second.isControlledByAI() && !elem.second.compOnly)
			{
				setPlayerConnectedId(elem.second, id);
				break;
			}
		}
	}
}

void CVCMIServer::clientDisconnected(std::shared_ptr<CConnection> c)
{
	for(auto & pair : playerNames)
	{
		if(pair.second.connection != c->connectionID)
			continue;

		playerNames.erase(pair.first);

		// Reset in-game players client used back to AI
		if(PlayerSettings * s = si->getPlayersSettings(pair.first))
		{
			setPlayerConnectedId(*s, PlayerSettings::PLAYER_AI);
		}
	}
}








void CVCMIServer::setPlayerConnectedId(PlayerSettings & pset, ui8 player) const
{
	if(vstd::contains(playerNames, player))
		pset.name = playerNames.find(player)->second.name;
	else
		pset.name = VLC->generaltexth->allTexts[468]; //Computer

	pset.connectedPlayerIDs.clear();
	if(player != PlayerSettings::PLAYER_AI)
		pset.connectedPlayerIDs.insert(player);
}

void CVCMIServer::updateStartInfo()
{
	si->playerInfos.clear();
	if(!mi)
		return;

	si->mapname = mi->fileURI;

	auto namesIt = playerNames.cbegin();

	for(int i = 0; i < mi->mapHeader->players.size(); i++)
	{
		const PlayerInfo & pinfo = mi->mapHeader->players[i];

		//neither computer nor human can play - no player
		if(!(pinfo.canHumanPlay || pinfo.canComputerPlay))
			continue;

		PlayerSettings & pset = si->playerInfos[PlayerColor(i)];
		pset.color = PlayerColor(i);
		if(pinfo.canHumanPlay && namesIt != playerNames.cend())
		{
			setPlayerConnectedId(pset, namesIt++->first);
		}
		else
		{
			setPlayerConnectedId(pset, PlayerSettings::PLAYER_AI);
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

void CVCMIServer::propagateOptions()
{
	// Update player settings for RMG
	// MPTODO: find appropriate location for this code
	if(si->mapGenOptions && si->mode == StartInfo::NEW_GAME)
	{
		for(const auto & psetPair : si->playerInfos)
		{
			const auto & pset = psetPair.second;
			si->mapGenOptions->setStartingTownForPlayer(pset.color, pset.castle);
			if(pset.isControlledByHuman())
			{
				si->mapGenOptions->setPlayerTypeForStandardPlayer(pset.color, EPlayerType::HUMAN);
			}
		}
	}

	auto ups = new LobbyUpdateState();
	ups->playerNames = playerNames;
	ups->startInfo = si.get();
	addToAnnounceQueue(ups);
}













void CVCMIServer::optionNextCastle(PlayerColor player, int dir)
{
	PlayerSettings & s = si->playerInfos[player];
	si16 & cur = s.castle;
	auto & allowed = mi->mapHeader->players[s.color.getNum()].allowedFactions;
	const bool allowRandomTown = mi->mapHeader->players[s.color.getNum()].isFactionRandom;

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

	if(s.hero >= 0 && !mi->mapHeader->players[s.color.getNum()].hasCustomMainHero()) // remove hero unless it set to fixed one in map editor
	{
		s.hero = PlayerSettings::RANDOM;
	}
	if(cur < 0 && s.bonus == PlayerSettings::RESOURCE)
		s.bonus = PlayerSettings::RANDOM;
}

void CVCMIServer::optionNextHero(PlayerColor player, int dir)
{
	PlayerSettings & s = si->playerInfos[player];
	if(s.castle < 0 || s.hero == PlayerSettings::NONE)
		return;

	if(s.hero == PlayerSettings::RANDOM) // first/last available
	{
		int max = VLC->heroh->heroes.size(),
			min = 0;
		s.hero = nextAllowedHero(player, min, max, 0, dir);
	}
	else
	{
		if(dir > 0)
			s.hero = nextAllowedHero(player, s.hero, VLC->heroh->heroes.size(), 1, dir);
		else
			s.hero = nextAllowedHero(player, -1, s.hero, 1, dir); // min needs to be -1 -- hero at index 0 would be skipped otherwise
	}
}

int CVCMIServer::nextAllowedHero(PlayerColor player, int min, int max, int incl, int dir)
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

void CVCMIServer::optionNextBonus(PlayerColor player, int dir)
{
	PlayerSettings & s = si->playerInfos[player];
	PlayerSettings::Ebonus & ret = s.bonus = static_cast<PlayerSettings::Ebonus>(static_cast<int>(s.bonus) + dir);

	if(s.hero == PlayerSettings::NONE &&
		!mi->mapHeader->players[s.color.getNum()].heroesNames.size() &&
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

bool CVCMIServer::canUseThisHero(PlayerColor player, int ID)
{
	return VLC->heroh->heroes.size() > ID
		&& si->playerInfos[player].castle == VLC->heroh->heroes[ID]->heroClass->faction
		&& !vstd::contains(getUsedHeroes(), ID)
		&& mi->mapHeader->allowedHeroes[ID];
}

std::vector<int> CVCMIServer::getUsedHeroes()
{
	std::vector<int> heroIds;
	for(auto & p : si->playerInfos)
	{
		const auto & heroes = mi->mapHeader->players[p.first.getNum()].heroesNames;
		for(auto & hero : heroes)
			if(hero.heroId >= 0) //in VCMI map format heroId = -1 means random hero
				heroIds.push_back(hero.heroId);

		if(p.second.hero != PlayerSettings::RANDOM)
			heroIds.push_back(p.second.hero);
	}
	return heroIds;
}







ui8 CVCMIServer::getIdOfFirstUnallocatedPlayer() //MPTODO: must be const
{
	for(auto i = playerNames.cbegin(); i != playerNames.cend(); i++)
	{
		if(!si->getPlayersSettings(i->first))
			return i->first;
	}

	return 0;
}











#if defined(__GNUC__) && !defined(__MINGW32__) && !defined(VCMI_ANDROID)
void handleLinuxSignal(int sig)
{
	const int STACKTRACE_SIZE = 100;
	void * buffer[STACKTRACE_SIZE];
	int ptrCount = backtrace(buffer, STACKTRACE_SIZE);
	char * * strings;

	logGlobal->error("Error: signal %d :", sig);
	strings = backtrace_symbols(buffer, ptrCount);
	if(strings == nullptr)
	{
		logGlobal->error("There are no symbols.");
	}
	else
	{
		for(int i = 0; i < ptrCount; ++i)
		{
			logGlobal->error(strings[i]);
		}
		free(strings);
	}

	_exit(EXIT_FAILURE);
}
#endif

static void handleCommandOptions(int argc, char * argv[], boost::program_options::variables_map & options)
{
	namespace po = boost::program_options;
	po::options_description opts("Allowed options");
	opts.add_options()
	("help,h", "display help and exit")
	("version,v", "display version information and exit")
	("run-by-client", "indicate that server launched by client on same machine")
	("uuid", po::value<std::string>(), "")
	("enable-shm-uuid", "use UUID for shared memory identifier")
	("enable-shm", "enable usage of shared memory")
	("port", po::value<ui16>(), "port at which server will listen to connections from client");

	if(argc > 1)
	{
		try
		{
			po::store(po::parse_command_line(argc, argv, opts), options);
		}
		catch(std::exception & e)
		{
			std::cerr << "Failure during parsing command-line options:\n" << e.what() << std::endl;
		}
	}

	po::notify(options);
	if(options.count("help"))
	{
		auto time = std::time(0);
		printf("%s - A Heroes of Might and Magic 3 clone\n", GameConstants::VCMI_VERSION.c_str());
		printf("Copyright (C) 2007-%d VCMI dev team - see AUTHORS file\n", std::localtime(&time)->tm_year + 1900);
		printf("This is free software; see the source for copying conditions. There is NO\n");
		printf("warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");
		printf("\n");
		std::cout << opts;
		exit(0);
	}

	if(options.count("version"))
	{
		printf("%s\n", GameConstants::VCMI_VERSION.c_str());
		std::cout << VCMIDirs::get().genHelpString();
		exit(0);
	}
}

int main(int argc, char * argv[])
{
#ifndef VCMI_ANDROID
	// Correct working dir executable folder (not bundle folder) so we can use executable relative paths
	boost::filesystem::current_path(boost::filesystem::system_complete(argv[0]).parent_path());
#endif
	// Installs a sig sev segmentation violation handler
	// to log stacktrace
    #if defined(__GNUC__) && !defined(__MINGW32__) && !defined(VCMI_ANDROID)
	signal(SIGSEGV, handleLinuxSignal);
    #endif

	console = new CConsoleHandler();
	CBasicLogConfigurator logConfig(VCMIDirs::get().userCachePath() / "VCMI_Server_log.txt", console);
	logConfig.configureDefault();
	logGlobal->info(NAME);

	boost::program_options::variables_map opts;
	handleCommandOptions(argc, argv, opts);
	preinitDLL(console);
	settings.init();
	logConfig.configure();

	loadDLLClasses();
	srand((ui32)time(nullptr));
	try
	{
		boost::asio::io_service io_service;
		CVCMIServer server(opts);

		try
		{
			while(!CVCMIServer::shuttingDown)
			{
				server.start();
			}
			io_service.run();
		}
		catch(boost::system::system_error & e) //for boost errors just log, not crash - probably client shut down connection
		{
			logNetwork->error(e.what());
			CVCMIServer::shuttingDown = true;
		}
		catch(...)
		{
			handleException();
		}
	}
	catch(boost::system::system_error & e)
	{
		logNetwork->error(e.what());
		//catch any startup errors (e.g. can't access port) errors
		//and return non-zero status so client can detect error
		throw;
	}
#ifdef VCMI_ANDROID
	CAndroidVMHelper envHelper;
	envHelper.callStaticVoidMethod(CAndroidVMHelper::NATIVE_METHODS_DEFAULT_CLASS, "killServer");
#endif
	vstd::clear_pointer(VLC);
	CResourceHandler::clear();
	return 0;
}

#ifdef VCMI_ANDROID
void CVCMIServer::create()
{
	const char * foo[1] = {"android-server"};
	main(1, const_cast<char * *>(foo));
}
#endif
