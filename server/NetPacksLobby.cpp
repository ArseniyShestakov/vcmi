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


bool WelcomeServer::applyServerBefore(CVCMIServer * srv, CConnection * c)
{
	c->names = names;
	c->uuid = uuid;

	WelcomeClient wc;
	wc.connectionId = c->connectionID;
	wc.capabilities = srv->capabilities;
	if(srv->hostClient->connectionID == c->connectionID)
	{
		srv->si->mode = mode;
		srv->hostClient = c;
		wc.giveHost = true;
	}
	srv->sendPack(c, wc);

	logNetwork->info("Connection with client %d established. UUID: %s", c->connectionID, c->uuid);
	for(auto & name : c->names)
		logNetwork->info("Client %d player: %s", c->connectionID, name);

	srv->playerJoined(c);
	return true;
}

bool PassHost::applyServerBefore(CVCMIServer * srv, CConnection * c)
{
	srv->passHost(toConnection);
	return true;
}

bool StartWithCurrentSettings::applyServerBefore(CVCMIServer * srv, CConnection * c)
{
	c->receivedStop = true;
	if(!c->sendStop)
		srv->sendPack(c, *this);

	if(c == srv->hostClient)
		return true;
	else
		return false;
}

void StartWithCurrentSettings::applyServerAfter(CVCMIServer * srv, CConnection * c)
{
	//MOTODO: this need more thinking!
	srv->state = CVCMIServer::ENDING_AND_STARTING_GAME;
	//wait for sending thread to announce start
	while(srv->state == CVCMIServer::RUNNING)
		boost::this_thread::sleep(boost::posix_time::milliseconds(50));

}

bool QuitMenuWithoutStarting::applyServerBefore(CVCMIServer * srv, CConnection * c)
{
	c->receivedStop = true;
	if(!c->sendStop)
		srv->sendPack(c, *this);

	if(c == srv->hostClient)
		return true;
	else
		return false;
}

void QuitMenuWithoutStarting::applyServerAfter(CVCMIServer * srv, CConnection * c)
{
	CVCMIServer::shuttingDown = true;
}

bool SelectMap::applyServerBefore(CVCMIServer *srv, CConnection *c)
{
	srv->mi = std::shared_ptr<CMapInfo>(mapInfo);

	if(srv->mi && srv->si->mode == StartInfo::LOAD_GAME)
		srv->si->difficulty = srv->mi->scenarioOpts->difficulty;

	srv->updateStartInfo();
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
	srv->propagateOptions();
	return true;
}

bool ChangePlayerOptions::applyServerBefore(CVCMIServer * srv, CConnection * c)
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
	srv->propagateOptions();
	return true;
}

bool SetPlayer::applyServerBefore(CVCMIServer * srv, CConnection * c)
{
	struct PlayerToRestore
	{
		PlayerColor color;
		int id;
		void reset() { id = -1; color = PlayerColor::CANNOT_DETERMINE; }
		PlayerToRestore(){ reset(); }
	} playerToRestore;

	PlayerSettings & clicked = srv->si->playerInfos[color];
	PlayerSettings * old = nullptr;

	//identify clicked player
	int clickedNameID = clicked.connectedPlayerID; //human is a number of player, zero means AI
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
			int curNameID = i->second.connectedPlayerID;
			if(i->first != color && curNameID == newPlayer)
			{
				assert(i->second.connectedPlayerID);
				playerToRestore.color = i->first;
				playerToRestore.id = newPlayer;
				srv->setPlayerConnectedId(i->second, 0); //set computer
				old = &i->second;
				break;
			}
		}
	}
	srv->propagateOptions();
	return true;
}

bool SetTurnTime::applyServerBefore(CVCMIServer * srv, CConnection * c)
{
	srv->si->turnTime = turnTime;
	srv->propagateOptions();
	return true;
}

bool SetDifficulty::applyServerBefore(CVCMIServer * srv, CConnection * c)
{
	srv->si->difficulty = difficulty;
	srv->propagateOptions();
	return true;
}
