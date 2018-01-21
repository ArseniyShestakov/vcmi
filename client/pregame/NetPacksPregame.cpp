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
#include "CSelectionBase.h"
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

void ChatMessage::apply(CLobbyScreen * lobby)
{
	lobby->card->chat->addNewMessage(playerName + ": " + message);
	GH.totalRedraw();
}

void QuitMenuWithoutStarting::apply(CLobbyScreen * lobby)
{
	if(!CSH->ongoingClosing)
	{
		*CSH->c << this; //resend to confirm
		GH.popIntTotally(lobby); //will wait with deleting us before this thread ends
	}
	CSH->stopConnection();
}

void PlayerJoined::apply(CLobbyScreen * lobby)
{
//	lobby->toggleTab(lobby->curTab);
	if(connectionID != CSH->c->connectionID)
		lobby->card->setChat(true);

	GH.totalRedraw();
}

void SelectMap::apply(CLobbyScreen * lobby)
{
	if(mapInfo)
		CSH->mi = std::make_shared<CMapInfo>(*mapInfo);
	else
		CSH->mi.reset();
	lobby->card->changeSelection();
	if(lobby->screenType != CMenuScreen::campaignList)
	{
		lobby->tabOpt->recreate();
	}
}

void UpdateStartOptions::apply(CLobbyScreen * lobby)
{
	CSH->si = std::shared_ptr<StartInfo>(startInfo);
	if(CSH->mi)
		lobby->tabOpt->recreate(); //will force to recreate using current sInfo

	lobby->card->difficulty->setSelected(startInfo->difficulty);

	// MPTODO: idea is to always apply any changes on guest as well as on host
	// Though applying of randMapTab options cause crash if host just decide to open it
	if(lobby->curTab == lobby->tabRand && startInfo->mapGenOptions)
		lobby->tabRand->setMapGenOptions(startInfo->mapGenOptions);

	GH.totalRedraw();
}

void PregameGuiAction::apply(CLobbyScreen * lobby)
{
	if(!CSH->isGuest())
		return;

	switch(action)
	{
	case NO_TAB:
		lobby->toggleTab(lobby->curTab);
		break;
	case OPEN_OPTIONS:
		lobby->toggleTab(lobby->tabOpt);
		break;
	case OPEN_SCENARIO_LIST:
		lobby->toggleTab(lobby->tabSel);
		break;
	case OPEN_RANDOM_MAP_OPTIONS:
		lobby->toggleTab(lobby->tabRand);
		break;
	}
}

void PlayerLeft::apply(CLobbyScreen * lobby)
{
}

void PlayersNames::apply(CLobbyScreen * lobby)
{
	CSH->playerNames = playerNames;
}

void PassHost::apply(CLobbyScreen * lobby)
{
	bool old = CSH->isHost();
	if(CSH->c->connectionID == toConnection)
		CSH->host = true;
	else if(CSH->host)
		CSH->host = false;

	if(old != CSH->isHost())
		lobby->toggleMode(CSH->host);
}

void StartWithCurrentSettings::apply(CLobbyScreen * lobby)
{
	if(!CSH->ongoingClosing)
	{
		*CSH->c << this; //resend to confirm
	}
	vstd::clear_pointer(CSH->serverHandlingThread); //detach us

	CGP->showLoadingScreen(std::bind(&startGame));
	throw 666; //EVIL, EVIL, EVIL workaround to kill thread (does "goto catch" outside listening loop)
}

void WelcomeClient::apply(CLobbyScreen * lobby)
{
	CSH->c->connectionID = connectionId;
	CSH->host = giveHost;
	lobby->toggleMode(giveHost);
}
