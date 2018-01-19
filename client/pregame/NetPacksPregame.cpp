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
#include "../widgets/Buttons.h"
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
	if(!CSH->ongoingClosing)
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
		CSH->playerNames.insert(player);

		//put new player in first slot with AI
		for(auto & elem : CSH->si.playerInfos)
		{
			if(!elem.second.connectedPlayerID && !elem.second.compOnly)
			{
				CSH->setPlayer(elem.second, player.first);
				selScreen->tabOpt->entries[elem.second.color]->selectButtons();
				break;
			}
		}
	}

	CSH->propagateNames();
	CSH->propagateOptions();
//	selScreen->toggleTab(selScreen->curTab);
	if(connectionID != CSH->c->connectionID)
		selScreen->card->setChat(true);

	GH.totalRedraw();
}

void SelectMap::apply(CSelectionScreen * selScreen)
{
	CSH->setCurrentMap(mapInfo, mapGenOpts);
	CSH->propagateOptions();
	selScreen->card->changeSelection();
	if(selScreen->screenType != CMenuScreen::campaignList)
	{
		selScreen->tabOpt->recreate();
	}
}

void UpdateStartOptions::apply(CSelectionScreen * selScreen)
{
	CSH->si = *si;
	if(CSH->current)
		selScreen->tabOpt->recreate(); //will force to recreate using current sInfo

	selScreen->card->difficulty->setSelected(si->difficulty);

	// MPTODO: idea is to always apply any changes on guest as well as on host
	// Though applying of randMapTab options cause crash if host just decide to open it
	if(selScreen->curTab == selScreen->tabRand && si->mapGenOptions)
		selScreen->tabRand->setMapGenOptions(si->mapGenOptions);

	GH.totalRedraw();
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
		selScreen->toggleTab(selScreen->tabOpt);
		break;
	case OPEN_SCENARIO_LIST:
		selScreen->toggleTab(selScreen->tabSel);
		break;
	case OPEN_RANDOM_MAP_OPTIONS:
		selScreen->toggleTab(selScreen->tabRand);
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
		CSH->optionNextCastle(color, direction);
		break;
	case HERO:
		CSH->optionNextHero(color, direction);
		break;
	case BONUS:
		CSH->optionNextBonus(color, direction);
		break;
	}
	CSH->propagateOptions();
}

void PlayerLeft::apply(CSelectionScreen * selScreen)
{
	if(CSH->isGuest())
		return;

	for(auto & pair : CSH->playerNames)
	{
		if(pair.second.connection != connectionID)
			continue;

		CSH->playerNames.erase(pair.first);

		if(PlayerSettings * s = CSH->si.getPlayersSettings(pair.first)) //it's possible that player was unallocated
		{
			CSH->setPlayer(*s, 0);
			selScreen->tabOpt->entries[s->color]->selectButtons();
		}
	}

	CSH->propagateNames();
	CSH->propagateOptions();
	GH.totalRedraw();
}

void PlayersNames::apply(CSelectionScreen * selScreen)
{
	if(CSH->isGuest())
		CSH->playerNames = playerNames;
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
	if(!CSH->ongoingClosing)
	{
		*CSH->c << this; //resend to confirm
	}
	vstd::clear_pointer(CSH->serverHandlingThread); //detach us

	CGP->showLoadingScreen(std::bind(&startGame));
	throw 666; //EVIL, EVIL, EVIL workaround to kill thread (does "goto catch" outside listening loop)
}

void WelcomeClient::apply(CSelectionScreen * selScreen)
{
	CSH->c->connectionID = connectionId;
	CSH->host = giveHost;
	selScreen->toggleMode(giveHost);
}
