/*
 * CSelectionScreen.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CPreGame.h"

class CTextBox;
class CTextInput;
class CAnimImage;
class CToggleGroup;
struct CPackForLobby;
class RandomMapTab;
class OptionsTab;
class SelectionTab;
class InfoCard;
class CChatBox;

struct ClientPlayer;

/// Interface for selecting a map.
class ISelectionScreenInfo
{
public:
	CMenuScreen::EState screenType;

	ISelectionScreenInfo(CMenuScreen::EState type = CMenuScreen::mainMenu);
	virtual ~ISelectionScreenInfo();
	virtual const CMapInfo * getMapInfo() = 0;
	virtual const StartInfo * getStartInfo() = 0;

	virtual std::string getMapName();
	virtual std::string getMapDescription();
	virtual int getCurrentDifficulty();
	virtual PlayerInfo getPlayerInfo(int color);

};

/// The actual map selection screen which consists of the options and selection tab
class CSelectionBase : public CIntObject, public ISelectionScreenInfo
{
public:
	bool bordered;

	CPicture * bg; //general bg image
	InfoCard * card;

	CButton * buttonSelect;
	CButton * buttonRMG;
	CButton * buttonOptions;
	CButton * buttonStart;
	CButton * buttonBack;

	SelectionTab * tabSel;
	OptionsTab * tabOpt;
	RandomMapTab * tabRand;
	CIntObject * curTab;

	CSelectionBase(CMenuScreen::EState type);
	void showAll(SDL_Surface * to) override;
	virtual void toggleTab(CIntObject * tab);
};

class InfoCard : public CIntObject
{
	CAnimImage * victory, * loss, * sizes;
	std::shared_ptr<CAnimation> sFlags;

public:
	CPicture * bg;

	bool showChat; //if chat is shown, then description is hidden
	CTextBox * mapDescription;
	CChatBox * chat;
	CPicture * playerListBg;

	CToggleGroup * difficulty;

	InfoCard();
	~InfoCard();
	void showAll(SDL_Surface * to) override;
	void clickRight(tribool down, bool previousState) override;
	void changeSelection();
	void showTeamsPopup();
	void toggleChat();
	void setChat(bool activateChat);
};

/// Implementation of the chat box
class CChatBox : public CIntObject
{
public:
	CTextBox * chatHistory;
	CTextInput * inputBox;

	CChatBox(const Rect & rect);

	void keyPressed(const SDL_KeyboardEvent & key) override;

	void addNewMessage(const std::string & text);
};

/// The actual map selection screen which consists of the options and selection tab
class CLobbyScreen : public CSelectionBase
{
public:
	CLobbyScreen(CMenuScreen::EState type, CMenuScreen::EGameMode gameMode = CMenuScreen::MULTI_NETWORK_HOST);
	~CLobbyScreen();
	void toggleTab(CIntObject * tab) override;
	void startCampaign();
	void startScenario();
	void toggleMode(bool host);

	 const CMapInfo * getMapInfo() override;
	 const StartInfo * getStartInfo() override;
};

/// Save game screen
class CSavingScreen : public CSelectionBase
{
public:
	const StartInfo * localSi;
	CMapInfo * localMi;

	CSavingScreen();
	~CSavingScreen();

	void changeSelection(std::shared_ptr<CMapInfo> to);
	void saveGame();

	const CMapInfo * getMapInfo() override;
	const StartInfo * getStartInfo() override;
};

/// Scenario information screen shown during the game (thus not really a "pre-game" but fits here anyway)
class CScenarioInfo : public CIntObject, public ISelectionScreenInfo
{
public:
	CButton * back;
	InfoCard * card;
	OptionsTab * opt;

	const StartInfo * localSi;
	CMapInfo * localMi;

	CScenarioInfo();
	~CScenarioInfo();

	const CMapInfo * getMapInfo() override;
	const StartInfo * getStartInfo() override;
};

extern ISelectionScreenInfo * SEL;
