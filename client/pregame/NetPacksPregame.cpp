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
#include "CLobbyScreen.h"
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

void LobbyClientConnected::applyOnLobby(CLobbyScreen * lobby)
{
	if(uuid == CSH->c->uuid)
	{
		CSH->c->connectionID = connectionId;
		CSH->hostConnectionId = hostConnectionId;
	}
	else
	{
		lobby->card->setChat(true);
	}
	GH.totalRedraw();
}

void LobbyClientDisconnected::applyOnLobby(CLobbyScreen * lobby)
{
	if(!CSH->ongoingClosing)
	{
		*CSH->c << this; //resend to confirm
		GH.popIntTotally(lobby); //will wait with deleting us before this thread ends
	}
	CSH->stopServerConnection();
//	GH.popIntTotally(lobby);
}

void LobbyChatMessage::applyOnLobby(CLobbyScreen * lobby)
{
	lobby->card->chat->addNewMessage(playerName + ": " + message);
	GH.totalRedraw();
}

void LobbyGuiAction::applyOnLobby(CLobbyScreen * lobby)
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

void LobbyStartGame::applyOnLobby(CLobbyScreen * lobby)
{
	if(!CSH->ongoingClosing)
	{
		*CSH->c << this; //resend to confirm
	}
	vstd::clear_pointer(CSH->serverHandlingThread); //detach us

	CGP->showLoadingScreen(std::bind(&startGame));
	throw 666; //EVIL, EVIL, EVIL workaround to kill thread (does "goto catch" outside listening loop)
}

void LobbyChangeHost::applyOnLobby(CLobbyScreen * lobby)
{
	bool old = CSH->isHost();
	CSH->hostConnectionId = newHostConnectionId;
	if(old != CSH->isHost())
		lobby->toggleMode(CSH->isHost());
}

void LobbyUpdateState::applyOnLobby(CLobbyScreen * lobby)
{
	if(mapInfo)
		CSH->mi = mapInfo;
	else
		CSH->mi.reset();
	lobby->card->changeSelection();
	if(lobby->screenType != CMenuScreen::campaignList)
	{
		lobby->tabOpt->recreate();
	}

	CSH->playerNames = playerNames;
	CSH->si = startInfo;
	if(CSH->mi)
		lobby->tabOpt->recreate(); //will force to recreate using current sInfo

	lobby->card->difficulty->setSelected(startInfo->difficulty);

	// MPTODO: idea is to always apply any changes on guest as well as on host
	// Though applying of randMapTab options cause crash if host just decide to open it
	if(lobby->curTab == lobby->tabRand && startInfo->mapGenOptions)
		lobby->tabRand->setMapGenOptions(startInfo->mapGenOptions);

	GH.totalRedraw();
}
