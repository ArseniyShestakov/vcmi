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

#include "CBonusSelection.h"
#include "../widgets/TextControls.h"

void startGame();

void LobbyClientConnected::applyOnLobby(CLobbyScreen * lobby)
{
	if(uuid == CSH->c->uuid)
	{
		CSH->c->connectionID = clientId;
		CSH->hostClientId = hostClientId;

		lobby->tabSel->restoreLastSelection();
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
	if(CSH->si->mode != StartInfo::LOAD_GAME)
	{
		CSH->si = initializedStartInfo;
	}
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
	bool campaign = false;
	if(CSH->si->campState)
		campaign = true;
	static_cast<LobbyState &>(*CSH) = state;

	if(!campaign && CSH->si->campState)
	{
		lobby->bonusSel = new CBonusSelection(CSH->mi->fileURI);
		GH.pushInt(lobby->bonusSel);
	}

	if(lobby->bonusSel)
	{
		// From ::selectMap
		// initialize restart / start button
		if(!lobby->bonusSel->getCampaign()->currentMap || *lobby->bonusSel->getCampaign()->currentMap != CSH->campaignMap)
		{
			// draw start button
			lobby->bonusSel->buttonRestart->disable();
			lobby->bonusSel->buttonStart->enable();
			if(!lobby->bonusSel->getCampaign()->mapsConquered.empty())
				lobby->bonusSel->buttonBack->block(true);
			else
				lobby->bonusSel->buttonBack->block(false);
		}
		else
		{
			// draw restart button
			lobby->bonusSel->buttonStart->disable();
			lobby->bonusSel->buttonRestart->enable();
			lobby->bonusSel->buttonBack->block(false);
		}

		lobby->bonusSel->mapDescription->setText(CSH->mi->mapHeader->description);
		lobby->bonusSel->updateBonusSelection();

		// From ::selectBonus
		lobby->bonusSel->updateStartButtonState(CSH->campaignBonus);
	}
	else
	{
		if(CSH->mi && lobby->screenType != CMenuScreen::campaignList)
			lobby->tabOpt->recreate();

		lobby->card->changeSelection();
		if(lobby->card->difficulty)
			lobby->card->difficulty->setSelected(CSH->si->difficulty);

		if(lobby->curTab == lobby->tabRand && CSH->si->mapGenOptions)
			lobby->tabRand->setMapGenOptions(CSH->si->mapGenOptions);
	}
	GH.totalRedraw();
}
