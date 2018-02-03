/*
 * CServerHandler.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../lib/CStopWatch.h"

#include "../lib/StartInfo.h"

struct SharedMemory;
class CConnection;
class PlayerColor;
struct StartInfo;

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
	virtual void sendClientConnecting() =0;
	virtual void sendClientDisconnecting() =0;
	virtual void setMapInfo(std::shared_ptr<CMapInfo> to, std::shared_ptr<CMapGenOptions> mapGenOpts = {}) =0;
	virtual void setPlayer(PlayerColor color) =0;
	virtual void setPlayerOption(ui8 what, ui8 dir, PlayerColor player) =0;
	virtual void setDifficulty(int to) =0;
	virtual void setTurnLength(int npos) =0;
	virtual void sendMessage(const std::string & txt) =0;
	virtual void sendGuiAction(ui8 action) =0; // TODO: possibly get rid of it?
	virtual void sendStartGame() =0;
};

class CClient; //MPTODO: rework
/// structure to handle running server and connecting to it
class CServerHandler : public IServerAPI, public LobbyInfo
{
	SharedMemory * shm;

	CApplier<CBaseForLobbyApply> * applier;

	boost::recursive_mutex * mx;
	std::list<CPackForLobby *> incomingPacks; //protected by mx

	std::vector<std::string> myNames;

	void threadHandleConnection();
	void threadRunServer();

public:
	std::unique_ptr<CStopWatch> th;
	boost::thread * threadRunLocalServer;
	boost::thread * threadConnectionToServer;
	std::atomic<bool> disconnecting;
	std::atomic<bool> pauseNetpackRetrieving;

	std::shared_ptr<CConnection> c;
	CClient * client; //MPTODO: rework

	CServerHandler();
	virtual ~CServerHandler();

	void resetStateForLobby(const StartInfo::EMode mode, const std::vector<std::string> * names = nullptr);
	void startLocalServerAndConnect();
	void justConnectToServer(const std::string &addr = "", const ui16 port = 0);
	void processIncomingPacks();
	void stopServerConnection();

	// Helpers for lobby state access
	std::set<PlayerColor> getHumanColors();
	PlayerColor myFirstColor() const;
	bool isMyColor(PlayerColor color) const;
	ui8 myFirstId() const; // Used by chat only!

	bool isServerLocal() const;
	bool isHost() const;
	bool isGuest() const;

	static ui16 getDefaultPort();
	static std::string getDefaultPortStr();

	// Lobby server API for UI
	void sendClientConnecting() override;
	void sendClientDisconnecting() override;
	void setCampaignState(std::shared_ptr<CCampaignState> newCampaign);
	void setCampaignMap(int mapId);
	void setCampaignBonus(int bonusId);
	void setMapInfo(std::shared_ptr<CMapInfo> to, std::shared_ptr<CMapGenOptions> mapGenOpts = {}) override;
	void setPlayer(PlayerColor color) override;
	void setPlayerOption(ui8 what, ui8 dir, PlayerColor player) override;
	void setDifficulty(int to) override;
	void setTurnLength(int npos) override;
	void sendMessage(const std::string & txt) override;
	void sendGuiAction(ui8 action) override;
	void sendStartGame() override;

	void startGameplay();
	void endGameplay();
};

extern CServerHandler * CSH;
