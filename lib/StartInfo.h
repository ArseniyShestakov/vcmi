/*
 * StartInfo.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "GameConstants.h"

class CMapGenOptions;
class CCampaignState;
class CMapInfo;

/// Struct which describes the name, the color, the starting bonus of a player
struct PlayerSettings
{
	enum { PLAYER_AI = 0 }; // for use in playerID

	enum Ebonus {
		NONE     = -2,
		RANDOM   = -1,
		ARTIFACT =  0,
		GOLD     =  1,
		RESOURCE =  2
	};

	Ebonus bonus;
	si16 castle;
	si32 hero,
		 heroPortrait; //-1 if default, else ID

	std::string heroName;
	PlayerColor color; //from 0 -
	enum EHandicap {NO_HANDICAP, MILD, SEVERE};
	EHandicap handicap;//0-no, 1-mild, 2-severe
	TeamID team;

	std::string name;
	ui8 connectedPlayerID; //0 - AI, non-0 serves as player id
	bool compOnly; //true if this player is a computer only player; required for RMG
	template <typename Handler>
	void serialize(Handler &h, const int version)
	{
		h & castle;
		h & hero;
		h & heroPortrait;
		h & heroName;
		h & bonus;
		h & color;
		h & handicap;
		h & name;
		h & connectedPlayerID;
		h & team;
		h & compOnly;
	}

	PlayerSettings() : bonus(RANDOM), castle(NONE), hero(RANDOM), heroPortrait(RANDOM),
		color(0), handicap(NO_HANDICAP), team(0), connectedPlayerID(PLAYER_AI), compOnly(false)
	{

	}
};

/// Struct which describes the difficulty, the turn time,.. of a heroes match.
struct StartInfo
{
	enum EMode {NEW_GAME, LOAD_GAME, CAMPAIGN, INVALID = 255};

	EMode mode;
	ui8 difficulty; //0=easy; 4=impossible

	typedef std::map<PlayerColor, PlayerSettings> TPlayerInfos;
	TPlayerInfos playerInfos; //color indexed

	ui32 seedToBeUsed; //0 if not sure (client requests server to decide, will be send in reply pack)
	ui32 seedPostInit; //so we know that game is correctly synced at the start; 0 if not known yet
	ui32 mapfileChecksum; //0 if not relevant
	ui8 turnTime; //in minutes, 0=unlimited
	std::string mapname; // empty for random map, otherwise name of the map or savegame
	bool createRandomMap() const { return mapGenOptions.get() != nullptr; }
	std::shared_ptr<CMapGenOptions> mapGenOptions;

	std::shared_ptr<CCampaignState> campState;

	PlayerSettings & getIthPlayersSettings(PlayerColor no)
	{
		if(playerInfos.find(no) != playerInfos.end())
			return playerInfos[no];
		logGlobal->error("Cannot find info about player %s. Throwing...", no.getStr());
		throw std::runtime_error("Cannot find info about player");
	}
	const PlayerSettings & getIthPlayersSettings(PlayerColor no) const
	{
		return const_cast<StartInfo&>(*this).getIthPlayersSettings(no);
	}

	PlayerSettings * getPlayersSettings(const ui8 connectedPlayerID)
	{
		for(auto & elem : playerInfos)
		{
			if(elem.second.connectedPlayerID == connectedPlayerID)
				return &elem.second;
		}

		return nullptr;
	}

	template <typename Handler>
	void serialize(Handler &h, const int version)
	{
		h & mode;
		h & difficulty;
		h & playerInfos;
		h & seedToBeUsed;
		h & seedPostInit;
		h & mapfileChecksum;
		h & turnTime;
		h & mapname;
		h & mapGenOptions;
		h & campState;
	}

	StartInfo() : mode(INVALID), difficulty(0), seedToBeUsed(0), seedPostInit(0),
		mapfileChecksum(0), turnTime(0)
	{

	}
};

struct ClientPlayer
{
	int connection;
	ui8 color;
	std::string name;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & connection;
		h & color;
		h & name;
	}
};

struct ServerCapabilities
{
	bool allowMapUpload;
	bool allowMapDownload;
	bool allowMapCreation;

	bool allowModsUpload;
	bool allowModsDownload;

	bool allowReconnect;
	bool allowSpectating;

	template <typename Handler>
	void serialize(Handler & h, const int version)
	{
		h & allowMapUpload;
		h & allowMapDownload;
		h & allowMapCreation;

		h & allowModsUpload;
		h & allowModsDownload;

		h & allowReconnect;
		h & allowSpectating;
	}
};

struct LobbyInfo
{
	std::shared_ptr<StartInfo> si;
	std::shared_ptr<CMapInfo> mi;
	std::map<ui8, ClientPlayer> playerNames; // id of player <-> player name; 0 is reserved as ID of AI "players"

	LobbyInfo() : mi(nullptr), si(new StartInfo()) {}
};
