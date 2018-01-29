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
#include "CSelectionBase.h"
#include "CLobbyScreen.h"

#include "OptionsTab.h"
#include "OptionsTab.h"
#include "RandomMapTab.h"
#include "SelectionTab.h"

#include "../CServerHandler.h"
#include "../CGameInfo.h"
#include "../gui/CGuiHandler.h"
#include "../widgets/Buttons.h"
#include "../../lib/NetPacks.h"
#include "../../lib/serializer/Connection.h"


void startGame();

void LobbyClientConnected::applyOnLobby(CLobbyScreen * lobby)
{
	if(uuid == CSH->c->uuid)
	{
		CSH->c->connectionID = clientId;
		CSH->hostClientId = hostClientId;
	}
	else
	{
		lobby->card->setChat(true);
	}
	GH.totalRedraw();
}

bool LobbyClientDisconnected::applyOnLobbyImmidiately(CLobbyScreen * lobby)
{
	if(clientId != c->connectionID)
		return false;

	vstd::clear_pointer(CSH->threadConnectionToServer);
	return true;
}

void LobbyClientDisconnected::applyOnLobby(CLobbyScreen * lobby)
{
	GH.popIntTotally(lobby);
	CSH->stopServerConnection();
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

bool LobbyStartGame::applyOnLobbyImmidiately(CLobbyScreen * lobby)
{
	CSH->pauseNetpackRetrieving = true;
	CSH->si = initializedStartInfo;
	return true;
}

void LobbyStartGame::applyOnLobby(CLobbyScreen * lobby)
{
	CGP->showLoadingScreen(std::bind(&startGame));
}

void LobbyChangeHost::applyOnLobby(CLobbyScreen * lobby)
{
	bool old = CSH->isHost();
	CSH->hostClientId = newHostConnectionId;
	if(old != CSH->isHost())
		lobby->toggleMode(CSH->isHost());
}

void LobbyUpdateState::applyOnLobby(CLobbyScreen * lobby)
{
	if(mapInfo)
		CSH->mi = mapInfo;
	else
		CSH->mi.reset();

	CSH->playerNames = playerNames;
	CSH->si = startInfo;
	if(CSH->mi && lobby->screenType != CMenuScreen::campaignList)
		lobby->tabOpt->recreate();

	lobby->card->changeSelection();
	lobby->card->difficulty->setSelected(startInfo->difficulty);

	if(lobby->curTab == lobby->tabRand && startInfo->mapGenOptions)
		lobby->tabRand->setMapGenOptions(startInfo->mapGenOptions);

	GH.totalRedraw();
}
