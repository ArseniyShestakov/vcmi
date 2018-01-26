/*
 * CServerHandler.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "../lib/CStopWatch.h"

#include "../lib/StartInfo.h"

struct SharedMemory;
class CConnection;
class PlayerColor;
struct StartInfo;

struct ServerCapabilities;
class CMapInfo;
struct ClientPlayer;
class CMapHeader;
struct LobbyGuiAction;
struct PlayerInfo;
struct CPackForLobby;

template<typename T> class CApplier;
class CBaseForLobbyApply;

class IServerAPI
{
public:
	virtual void clientConnecting() =0;
	virtual void clientDisconnecting() =0;
	virtual void setMapInfo(std::shared_ptr<CMapInfo> to, CMapGenOptions * mapGenOpts = nullptr) =0;
	virtual void setPlayerOption(ui8 what, ui8 dir, PlayerColor player) =0;
	virtual void setPlayer(PlayerColor color) =0;
	virtual void setTurnLength(int npos) =0;
	virtual void setDifficulty(int to) =0;
	virtual void sendMessage(const std::string & txt) =0;
	virtual void startGame() =0;
};

class CClient; //MPTODO: rework
/// structure to handle running server and connecting to it
class CServerHandler : public IServerAPI, public LobbyInfo
{
	CApplier<CBaseForLobbyApply> * applier;
public:
	CClient * client; //MPTODO: rework
	CStopWatch th;
	bool verbose; //whether to print log msgs

	boost::thread * threadRunLocalServer;
	boost::thread * threadConnectionToServer;
	SharedMemory * shm;
	std::shared_ptr<CConnection> c;
	ServerCapabilities * cap;

	CServerHandler();
	virtual ~CServerHandler();

	// Reset for lobby state
	void prepareForLobby(const StartInfo::EMode mode, const std::vector<std::string> * names);

	// Lobby state storage
	std::vector<std::string> myNames;
	// Helpers for lobby state access
	std::set<PlayerColor> getHumanColors();
	PlayerColor myFirstColor() const;
	bool isMyColor(PlayerColor color) const;
	ui8 myFirstId() const; // Used by chat only!

	// Lobby server API for UI
	void clientConnecting() override;
	void clientDisconnecting() override;
	void setMapInfo(std::shared_ptr<CMapInfo> to, CMapGenOptions * mapGenOpts = nullptr) override;
	void setPlayer(PlayerColor color) override;
	void setPlayerOption(ui8 what, ui8 dir, PlayerColor player) override;
	void setTurnLength(int npos) override;
	void setDifficulty(int to) override;
	void sendMessage(const std::string & txt) override;
	void startGame() override;

	// Some functions we need to get rid of since only server will propagate options
	void propagateGuiAction(LobbyGuiAction & lga);

	// Connection to exist server
	boost::recursive_mutex * mx;
	std::list<CPackForLobby *> upcomingPacks; //protected by mx

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
