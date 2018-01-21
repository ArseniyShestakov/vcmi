/*
 * CSelectionBase.h, part of VCMI engine
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

extern ISelectionScreenInfo * SEL;
