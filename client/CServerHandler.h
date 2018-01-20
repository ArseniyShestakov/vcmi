/*
 * CServerHandler.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

struct SharedMemory;
class CConnection;
class PlayerColor;
struct StartInfo;

struct ServerCapabilities;
class CMapInfo;
struct ClientPlayer;
class CMapHeader;
struct PregameGuiAction;
struct PlayerInfo;
struct CPackForSelectionScreen;

#include "../lib/CStopWatch.h"

#include "../lib/StartInfo.h"

class IServerAPI
{
public:
	virtual void setMapInfo(std::shared_ptr<CMapInfo> to, CMapGenOptions * mapGenOpts = nullptr) =0;
	virtual void setPlayerOption(ui8 what, ui8 dir, PlayerColor player) =0;
	virtual void setPlayer(PlayerColor color) =0;
	virtual void setTurnLength(int npos) =0;
	virtual void setDifficulty(int to) =0;
	virtual void sendMessage(const std::string & txt) =0;
	virtual void startGame() =0;
	virtual void stopServer() =0;
};

/// structure to handle running server and connecting to it
class CServerHandler : public IServerAPI
{
public:
	CStopWatch th;
	bool verbose; //whether to print log msgs

	boost::thread * localServerThread; //thread that called system to run server
	boost::thread * serverHandlingThread;
	SharedMemory * shm;
	std::string uuid;
	CConnection * c;
	ServerCapabilities * cap;

	CServerHandler();
	virtual ~CServerHandler();

	// Reset for lobby state
	void prepareForLobby(const StartInfo::EMode mode, const std::vector<std::string> * names);

	// Lobby state storage
	bool host;
	StartInfo si;
	std::vector<std::string> myNames;
	std::shared_ptr<CMapInfo> current;
	std::map<ui8, ClientPlayer> playerNames; // id of player <-> player name; 0 is reserved as ID of AI "players"
	// Helpers for lobby state access
	const PlayerSettings * getPlayerSettings(ui8 connectedPlayerId) const;
	PlayerInfo getPlayerInfo(int color) const;
	std::set<PlayerColor> getPlayers();
	std::set<PlayerColor> getHumanColors();
	PlayerColor myFirstColor() const;
	bool isMyColor(PlayerColor color) const;
	ui8 myFirstId() const;
	bool isMyId(ui8 id) const;
	std::vector<ui8> getMyIds() const;
	ui8 getIdOfFirstUnallocatedPlayer(); //returns 0 if none

	// Lobby server API for UI
	void setMapInfo(std::shared_ptr<CMapInfo> to, CMapGenOptions * mapGenOpts = nullptr) override;
	void setPlayer(PlayerColor color) override;
	void setPlayerOption(ui8 what, ui8 dir, PlayerColor player) override;
	void setTurnLength(int npos) override;
	void setDifficulty(int to) override;
	void sendMessage(const std::string & txt) override;
	void startGame() override;
	void stopServer() override;

	// Code only called from internals and netpacks. Should be moved on server-side
	void setPlayerConnectedId(PlayerSettings & pset, ui8 player) const;
	void updateStartInfo();
	void setCurrentMap(CMapInfo * mapInfo, CMapGenOptions * mapGenOpts);
	void optionNextHero(PlayerColor player, int dir); //dir == -1 or +1
	int nextAllowedHero(PlayerColor player, int min, int max, int incl, int dir);
	bool canUseThisHero(PlayerColor player, int ID);
	std::vector<int> getUsedHeroes();
	void optionNextBonus(PlayerColor player, int dir); //dir == -1 or +1
	void optionNextCastle(PlayerColor player, int dir); //dir == -1 or +1

	// Some functions we need to get rid of since only server will propagate options
	void propagateNames() const;
	void propagateOptions(); //MPTODO: should be const;
	void propagateGuiAction(PregameGuiAction & pga);

	// Connection to exist server
	boost::recursive_mutex * mx;
	std::list<CPackForSelectionScreen *> upcomingPacks; //protected by mx
	std::atomic<bool> ongoingClosing;

	void justConnectToServer(const std::string &addr = "", const ui16 port = 0); //connects to given host without taking any other actions (like setting up server)
	static ui16 getDefaultPort();
	static std::string getDefaultPortStr();
	void threadHandleConnection();
	void processPacks();
	void stopServerConnection();
	void stopConnection();

	bool isHost() const;
	bool isGuest() const;

	// Functions for setting up local server
	void startLocalServerAndConnect();
	bool isServerLocal() const;

private:
	void threadRunServer();
};

class mapMissingException : public std::exception {};
class noHumanException : public std::exception {};
class noTemplateException : public std::exception {};

extern CServerHandler * CSH;
