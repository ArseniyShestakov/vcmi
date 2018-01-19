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

#include "../lib/CondSh.h"
#include "../lib/CStopWatch.h"

#include "../lib/StartInfo.h"

/// structure to handle running server and connecting to it
class CServerHandler
{
private:
	void callServer(); //calls server via system(), should be called as thread
public:
	CStopWatch th;
	boost::thread * serverThread; //thread that called system to run server
	boost::thread * serverHandlingThread;
	boost::recursive_mutex * mx;
	SharedMemory * shm;
	std::string uuid;
	bool verbose; //whether to print log msgs

	CConnection * c;
	ServerCapabilities * cap;

	static CondSh<bool> serverAlive;  //used to prevent game start from executing if server is already running

	bool host;
	StartInfo si;
	std::vector<std::string> myNames;
	std::shared_ptr<CMapInfo> current;
	std::map<ui8, ClientPlayer> playerNames; // id of player <-> player name; 0 is reserved as ID of AI "players"
	const PlayerSettings * getPlayerSettings(ui8 connectedPlayerId) const;
	std::set<PlayerColor> getPlayers();
	std::set<PlayerColor> getHumanColors();
	void setPlayer(PlayerSettings & pset, ui8 player) const;
	void updateStartInfo();

	PlayerColor myFirstColor() const;
	bool isMyColor(PlayerColor color) const;
	ui8 myFirstId() const;
	bool isMyId(ui8 id) const;
	std::vector<ui8> getMyIds() const;

	ui8 getIdOfFirstUnallocatedPlayer(); //returns 0 if none

	void requestPlayerOptionChange(ui8 what, ui8 dir, PlayerColor player);
	void optionNextCastle(PlayerColor player, int dir); //dir == -1 or +1
	void optionNextHero(PlayerColor player, int dir); //dir == -1 or +1
	int nextAllowedHero(PlayerColor player, int min, int max, int incl, int dir);
	bool canUseThisHero(PlayerColor player, int ID);
	std::vector<int> getUsedHeroes();
	void optionNextBonus(PlayerColor player, int dir); //dir == -1 or +1
	void optionsFlagPressed(PlayerColor color);
	void optionSetTurnLength(int npos);

	void reset(StartInfo::EMode mode);

	void propagateNames() const;
	void propagateOptions(); //MPTODO: should be const;
	void propagateGuiAction(PregameGuiAction & pga);

	void tryStartGame();

	void postChatMessage(const std::string & txt);
	void quitWithoutStarting();

	PlayerInfo getPlayerInfo(int color) const;

	void requestChangeSelection(std::shared_ptr<CMapInfo> to);

	std::list<CPackForSelectionScreen *> upcomingPacks; //protected by mx

	std::atomic<bool> ongoingClosing;

	void handleConnection();
	void processPacks();
	void stopServerConnection();

	bool isHost() const;
	bool isGuest() const;

	//functions setting up local server
	void startServer(); //creates a thread with callServer
	void waitForServer(); //waits till server is ready
	void startServerAndConnect(); //connects to server
	bool isServerLocal();

	//////////////////////////////////////////////////////////////////////////
	void justConnectToServer(const std::string &host = "", const ui16 port = 0); //connects to given host without taking any other actions (like setting up server)
	static ui16 getDefaultPort();
	static std::string getDefaultPortStr();

	CServerHandler();
	virtual ~CServerHandler();

	void welcomeServer();
	void stopConnection();
};

class mapMissingException : public std::exception {};
class noHumanException : public std::exception {};
class noTemplateException : public std::exception {};

extern CServerHandler * CSH;
