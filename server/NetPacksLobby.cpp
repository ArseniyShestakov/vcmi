/*
 * NetPacksLobby.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "CVCMIServer.h"

#include "../lib/NetPacks.h"
#include "../lib/serializer/Connection.h"
#include "../lib/StartInfo.h"
#include "../lib/mapping/CMapInfo.h"
#include "../lib/rmg/CMapGenOptions.h"

bool CLobbyPackToServer::checkClientPermissions(CVCMIServer * srv) const
{
	return srv->isClientHost(c->connectionID);
}

void CLobbyPackToServer::applyOnServerAfterAnnounce(CVCMIServer * srv)
{
	// Propogate options after every CLobbyPackToServer
	srv->updateAndPropagateLobbyState();
}

bool LobbyClientConnected::checkClientPermissions(CVCMIServer * srv) const
{
	return true;
}

bool LobbyClientConnected::applyOnServer(CVCMIServer * srv)
{
	c->names = names;
	c->uuid = uuid;

	if(c->connectionID == 1)
	{
		srv->hostClient = c;
		srv->hostConnectionId = 1;
		srv->si->mode = mode;
	}
	else
	{
		mode = srv->si->mode;
	}
	connectionId = c->connectionID;
	capabilities = srv->capabilities;
	hostConnectionId = c->connectionID;

	logNetwork->info("Connection with client %d established. UUID: %s", c->connectionID, c->uuid);
	for(auto & name : c->names)
		logNetwork->info("Client %d player: %s", c->connectionID, name);

	srv->clientConnected(c);
	return true;
}

void LobbyClientConnected::applyOnServerAfterAnnounce(CVCMIServer * srv)
{
	for(auto & player : srv->playerNames)
	{
		int id = player.first;
		if(player.second.connection == c->connectionID)
			srv->announceTxt(boost::str(boost::format("%s (pid %d cid %d) joins the game") % player.second.name % id % c->connectionID));
	}
	srv->updateAndPropagateLobbyState();
}

bool LobbyClientDisconnected::checkClientPermissions(CVCMIServer * srv) const
{
	if(shutdownServer)
	{
		if(!srv->cmdLineOptions.count("run-by-client"))
			return false;

		if(c->uuid != srv->cmdLineOptions["uuid"].as<std::string>())
			return false;
	}

	return true;
}

bool LobbyClientDisconnected::applyOnServer(CVCMIServer * srv)
{
	if(c) // Only set when client actually sent this netpack
	{
		c->receivedStop = true;
		if(!c->sendStop)
			srv->sendPack(c, *this);

		if(c == srv->hostClient)
			return true;
		else
			return false;
	}

	srv->connections -= c;

	//notify other players about leaving
	for(auto & name : c->names)
		srv->announceTxt(boost::str(boost::format("%s(%d) left the game") % name % c->connectionID));

	srv->clientDisconnected(c);

	if(srv->connections.empty())
	{
		logNetwork->error("Last connection lost, server will close itself...");
		boost::this_thread::sleep(boost::posix_time::seconds(2)); //we should never be hasty when networking
		srv->state = CVCMIServer::ENDING_WITHOUT_START;
		return true;
	}
	else if(shutdownServer)
	{
		return true;
	}
	else if(c == srv->hostClient)
	{
		auto newHost = *RandomGeneratorUtil::nextItem(srv->connections, CRandomGenerator::getDefault());
		srv->passHost(newHost->connectionID);
	}
	srv->updateAndPropagateLobbyState();
	return true;
}

void LobbyClientDisconnected::applyOnServerAfterAnnounce(CVCMIServer * srv)
{
	if(shutdownServer)
	{
		CVCMIServer::shuttingDown = shutdownServer;
		srv->state = CVCMIServer::ENDING_WITHOUT_START;
	}
}

bool LobbyChatMessage::checkClientPermissions(CVCMIServer * srv) const
{
	return true;
}

bool LobbySetMap::applyOnServer(CVCMIServer * srv)
{
	srv->mi = mapInfo;

	if(srv->mi && srv->si->mode == StartInfo::LOAD_GAME)
		srv->si->difficulty = srv->mi->scenarioOpts->difficulty;

	srv->updateStartInfoOnMapChange();
	if(srv->si->mode == StartInfo::NEW_GAME)
	{
		if(srv->mi && srv->mi->isRandomMap)
		{
			srv->si->mapGenOptions = std::shared_ptr<CMapGenOptions>(mapGenOpts);
		}
		else
		{
			srv->si->mapGenOptions.reset();
		}
	}
	return true;
}

bool LobbyGuiAction::checkClientPermissions(CVCMIServer * srv) const
{
	return srv->isClientHost(c->connectionID);
}

bool LobbyStartGame::checkClientPermissions(CVCMIServer * srv) const
{
	return srv->isClientHost(c->connectionID);
}

bool LobbyStartGame::applyOnServer(CVCMIServer * srv)
{
	c->receivedStop = true;
	//MPTODO it's should work without it
	// But for some reason not all guests get pack if it's not announced from there
	if(!c->sendStop)
		srv->announcePack(*this);

	if(c == srv->hostClient)
		return true;
	else
		return false;
}

void LobbyStartGame::applyOnServerAfterAnnounce(CVCMIServer * srv)
{
	//MOTODO: this need more thinking!
	srv->state = CVCMIServer::ENDING_AND_STARTING_GAME;
	//wait for sending thread to announce start
	while(srv->state == CVCMIServer::RUNNING)
		boost::this_thread::sleep(boost::posix_time::milliseconds(50));
}

bool LobbyChangeHost::checkClientPermissions(CVCMIServer * srv) const
{
	return srv->isClientHost(c->connectionID);
}

bool LobbyChangeHost::applyOnServer(CVCMIServer * srv)
{
	srv->passHost(newHostConnectionId);
	return true;
}

bool LobbyChangePlayerOption::checkClientPermissions(CVCMIServer * srv) const
{
	if(srv->isClientHost(c->connectionID))
		return true;

	if(vstd::contains(srv->getAllClientPlayers(c->connectionID), color))
		return true;

	return false;
}

bool LobbyChangePlayerOption::applyOnServer(CVCMIServer * srv)
{
	switch(what)
	{
	case TOWN:
		srv->optionNextCastle(color, direction);
		break;
	case HERO:
		srv->optionNextHero(color, direction);
		break;
	case BONUS:
		srv->optionNextBonus(color, direction);
		break;
	}
	return true;
}

bool LobbySetPlayer::applyOnServer(CVCMIServer * srv)
{
	struct PlayerToRestore
	{
		PlayerColor color;
		int id;
		void reset() { id = -1; color = PlayerColor::CANNOT_DETERMINE; }
		PlayerToRestore(){ reset(); }
	} playerToRestore;

	// MPTODO: this should use PlayerSettings::controlledByAI / controlledByHuman
	// Though it's need to be done carefully and tested
	PlayerSettings & clicked = srv->si->playerInfos[clickedColor];
	PlayerSettings * old = nullptr;

	//identify clicked player
	int clickedNameID = *(clicked.connectedPlayerIDs.begin()); //human is a number of player, zero means AI
	if(clickedNameID > 0 && playerToRestore.id == clickedNameID) //player to restore is about to being replaced -> put him back to the old place
	{
		PlayerSettings & restPos = srv->si->playerInfos[playerToRestore.color];
		srv->setPlayerConnectedId(restPos, playerToRestore.id);
		playerToRestore.reset();
	}

	int newPlayer; //which player will take clicked position

	//who will be put here?
	if(!clickedNameID) //AI player clicked -> if possible replace computer with unallocated player
	{
		newPlayer = srv->getIdOfFirstUnallocatedPlayer();
		if(!newPlayer) //no "free" player -> get just first one
			newPlayer = srv->playerNames.begin()->first;
	}
	else //human clicked -> take next
	{
		auto i = srv->playerNames.find(clickedNameID); //clicked one
		i++; //player AFTER clicked one

		if(i != srv->playerNames.end())
			newPlayer = i->first;
		else
			newPlayer = 0; //AI if we scrolled through all players
	}

	srv->setPlayerConnectedId(clicked, newPlayer); //put player

	//if that player was somewhere else, we need to replace him with computer
	if(newPlayer) //not AI
	{
		for(auto i = srv->si->playerInfos.begin(); i != srv->si->playerInfos.end(); i++)
		{
			int curNameID = *(i->second.connectedPlayerIDs.begin());
			if(i->first != clickedColor && curNameID == newPlayer)
			{
				assert(i->second.connectedPlayerIDs.size());
				playerToRestore.color = i->first;
				playerToRestore.id = newPlayer;
				srv->setPlayerConnectedId(i->second, PlayerSettings::PLAYER_AI); //set computer
				old = &i->second;
				break;
			}
		}
	}
	return true;
}

bool LobbySetTurnTime::applyOnServer(CVCMIServer * srv)
{
	srv->si->turnTime = turnTime;
	return true;
}

bool LobbySetDifficulty::applyOnServer(CVCMIServer * srv)
{
	srv->si->difficulty = difficulty;
	return true;
}

bool LobbyForceSetPlayer::applyOnServer(CVCMIServer * srv)
{
	srv->si->playerInfos[targetPlayerColor].connectedPlayerIDs.insert(targetConnectedPlayer);
	return true;
}
