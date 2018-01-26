/*
 * CVCMIServer.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../lib/StartInfo.h"

#include <boost/program_options.hpp>

class CMapInfo;

class CConnection;
struct CPackForLobby;
class CGameHandler;
struct SharedMemory;

namespace boost
{
	namespace asio
	{
		namespace ip
		{
			class tcp;
		}
		class io_service;

		template <typename Protocol> class stream_socket_service;
		template <typename Protocol,typename StreamSocketService>
		class basic_stream_socket;

		template <typename Protocol> class socket_acceptor_service;
		template <typename Protocol,typename SocketAcceptorService>
		class basic_socket_acceptor;
	}
};

typedef boost::asio::basic_socket_acceptor<boost::asio::ip::tcp, boost::asio::socket_acceptor_service<boost::asio::ip::tcp>> TAcceptor;
typedef boost::asio::basic_stream_socket<boost::asio::ip::tcp, boost::asio::stream_socket_service<boost::asio::ip::tcp>> TSocket;

struct StartInfo;
struct ServerCapabilities;
struct LobbyInfo;
class PlayerSettings;
class PlayerColor;

template<typename T> class CApplier;
class CBaseForServerApply;
class CBaseForGHApply;

class CVCMIServer : public LobbyInfo
{
	CApplier<CBaseForServerApply> * applier;

public:
	std::unique_ptr<CGameHandler> gh;
	enum
	{
		INVALID, RUNNING, ENDING_WITHOUT_START, ENDING_AND_STARTING_GAME
	} state;
	ui16 port;
	boost::asio::io_service * io;
	TAcceptor * acceptor;
	SharedMemory * shm;

	boost::program_options::variables_map cmdLineOptions;

	std::atomic<int> listeningThreads;
	std::set<std::shared_ptr<CConnection>> connections;
	std::list<CPackForLobby *> announceQueue;
	boost::recursive_mutex mx;

	TSocket * upcomingConnection;

	void startAsyncAccept();
	void connectionAccepted(const boost::system::error_code & ec);
	void startListeningThread(std::shared_ptr<CConnection> c);
	void handleConnection(std::shared_ptr<CConnection> c);

	void processPack(CPackForLobby * pack);
	void announcePack(const CPackForLobby & pack);
	void passHost(int toConnectionId);

//public:
	static std::atomic<bool> shuttingDown;
	std::atomic<ui8> currentPlayerId;

	CVCMIServer(boost::program_options::variables_map & opts);
	~CVCMIServer();

	void start();
	void startGame();

	std::shared_ptr<CConnection> hostClient;
	ServerCapabilities * capabilities;
	void sendPack(std::shared_ptr<CConnection> c, const CPackForLobby & pack);
	void announceTxt(const std::string & txt, const std::string & playerName = "system");
	void addToAnnounceQueue(CPackForLobby * pack, bool front = false);

	void setPlayerConnectedId(PlayerSettings & pset, ui8 player) const;
	void updateStartInfoOnMapChange();

	void clientConnected(std::shared_ptr<CConnection> c);
	void clientDisconnected(std::shared_ptr<CConnection> c);

	void updateAndPropagateLobbyState();

	// Code only called from internals and netpacks. Should be moved on server-side
	void optionNextHero(PlayerColor player, int dir); //dir == -1 or +1
	int nextAllowedHero(PlayerColor player, int min, int max, int incl, int dir);
	bool canUseThisHero(PlayerColor player, int ID);
	std::vector<int> getUsedHeroes();
	void optionNextBonus(PlayerColor player, int dir); //dir == -1 or +1
	void optionNextCastle(PlayerColor player, int dir); //dir == -1 or +

	ui8 getIdOfFirstUnallocatedPlayer();

#ifdef VCMI_ANDROID
	static void create();
#endif
};
