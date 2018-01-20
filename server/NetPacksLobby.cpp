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


bool WelcomeServer::applyServerBefore(CVCMIServer * srv, CConnection * c)
{
	c->names = names;
	c->uuid = uuid;

	WelcomeClient wc;
	wc.connectionId = c->connectionID;
	wc.capabilities = srv->capabilities;
	if(srv->hostClient->connectionID == c->connectionID)
	{
		srv->hostClient = c;
		wc.giveHost = true;
	}
	srv->sendPack(c, wc);

	logNetwork->info("Connection with client %d established. UUID: %s", c->connectionID, c->uuid);
	for(auto & name : c->names)
		logNetwork->info("Client %d player: %s", c->connectionID, name);

	PlayerJoined pj;
	pj.connectionID = c->connectionID;
	for(auto & name : c->names)
	{
		srv->announceTxt(boost::str(boost::format("%s(%d) joins the game") % name % c->connectionID));

		ClientPlayer cp;
		cp.connection = c->connectionID;
		cp.name = name;
		cp.color = 255;
		pj.players.insert(std::make_pair(srv->currentPlayerId++, cp));
	}
	srv->addToAnnounceQueue(&pj);
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
	vstd::clear_pointer(srv->curmap);
	srv->curmap = mapInfo;
	return true;
}

bool UpdateStartOptions::applyServerBefore(CVCMIServer *srv, CConnection *c)
{
	vstd::clear_pointer(srv->curStartInfo);
	srv->curStartInfo = si;
	return true;
}
