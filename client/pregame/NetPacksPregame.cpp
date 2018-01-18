/*
 * NetPacksPregame.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CPreGame.h"
#include "CSelectionScreen.h"
#include "OptionsTab.h"
#include "../CServerHandler.h"
#include "../CGameInfo.h"
#include "../gui/CGuiHandler.h"
#include "../../lib/NetPacks.h"
#include "../../lib/serializer/Connection.h"

// MPTODO: Can this be avoided?
#include "OptionsTab.h"
#include "RandomMapTab.h"
#include "SelectionTab.h"

void startGame();

void ChatMessage::apply(CSelectionScreen * selScreen)
{
	selScreen->card->chat->addNewMessage(playerName + ": " + message);
	GH.totalRedraw();
}

void QuitMenuWithoutStarting::apply(CSelectionScreen * selScreen)
{
	if(!selScreen->ongoingClosing)
	{
		*CSH->c << this; //resend to confirm
		GH.popIntTotally(selScreen); //will wait with deleting us before this thread ends
	}
	CSH->stopConnection();
}

void PlayerJoined::apply(CSelectionScreen * selScreen)
{
	for(auto & player : players)
	{
		SEL->playerNames.insert(player);

		//put new player in first slot with AI
		for(auto & elem : CSH->si.playerInfos)
		{
			if(!elem.second.connectedPlayerID && !elem.second.compOnly)
			{
				selScreen->setPlayer(elem.second, player.first);
				selScreen->opt->entries[elem.second.color]->selectButtons();
				break;
			}
		}
	}

	selScreen->propagateNames();
	selScreen->propagateOptions();
//	selScreen->toggleTab(selScreen->curTab);
	if(connectionID != CSH->c->connectionID)
		selScreen->card->setChat(true);

	GH.totalRedraw();
}

void SelectMap::apply(CSelectionScreen * selScreen)
{
	if(CSH->isGuest())
	{
		free = false;
		selScreen->changeSelection(std::make_shared<CMapInfo>(*mapInfo));
	}
}

void UpdateStartOptions::apply(CSelectionScreen * selScreen)
{
	if(!CSH->isGuest())
		return;

	selScreen->setSInfo(*options);
}

void PregameGuiAction::apply(CSelectionScreen * selScreen)
{
	if(!CSH->isGuest())
		return;

	switch(action)
	{
	case NO_TAB:
		selScreen->toggleTab(selScreen->curTab);
		break;
	case OPEN_OPTIONS:
		selScreen->toggleTab(selScreen->opt);
		break;
	case OPEN_SCENARIO_LIST:
		selScreen->toggleTab(selScreen->sel);
		break;
	case OPEN_RANDOM_MAP_OPTIONS:
		selScreen->toggleTab(selScreen->randMapTab);
		break;
	}
}

void RequestOptionsChange::apply(CSelectionScreen * selScreen)
{
	if(!CSH->isHost())
		return;

	PlayerColor color = CSH->si.getPlayersSettings(playerID)->color;

	switch(what)
	{
	case TOWN:
		selScreen->opt->nextCastle(color, direction);
		break;
	case HERO:
		selScreen->opt->nextHero(color, direction);
		break;
	case BONUS:
		selScreen->opt->nextBonus(color, direction);
		break;
	}
}

void PlayerLeft::apply(CSelectionScreen * selScreen)
{
	if(CSH->isGuest())
		return;

	for(auto & pair : selScreen->playerNames)
	{
		if(pair.second.connection != connectionID)
			continue;

		selScreen->playerNames.erase(pair.first);

		if(PlayerSettings * s = CSH->si.getPlayersSettings(pair.first)) //it's possible that player was unallocated
		{
			selScreen->setPlayer(*s, 0);
			selScreen->opt->entries[s->color]->selectButtons();
		}
	}

	selScreen->propagateNames();
	selScreen->propagateOptions();
	GH.totalRedraw();
}

void PlayersNames::apply(CSelectionScreen * selScreen)
{
	if(CSH->isGuest())
		selScreen->playerNames = playerNames;
}

void PassHost::apply(CSelectionScreen *selScreen)
{
	bool old = CSH->isHost();
	if(CSH->c->connectionID == toConnection)
		CSH->host = true;
	else if(CSH->host)
		CSH->host = false;

	if(old != CSH->isHost())
		selScreen->toggleMode(CSH->host);
}

void StartWithCurrentSettings::apply(CSelectionScreen * selScreen)
{
	if(!selScreen->ongoingClosing)
	{
		*CSH->c << this; //resend to confirm
	}
	vstd::clear_pointer(selScreen->serverHandlingThread); //detach us

	CSH->myPlayers = selScreen->getMyIds();
	CGP->showLoadingScreen(std::bind(&startGame));
	throw 666; //EVIL, EVIL, EVIL workaround to kill thread (does "goto catch" outside listening loop)
}

void WelcomeClient::apply(CSelectionScreen * selScreen)
{
	CSH->c->connectionID = connectionId;
	CSH->host = giveHost;
	selScreen->toggleMode(giveHost);
}
