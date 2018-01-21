/*
 * CSelectionScreen.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CSelectionScreen.h"
#include "CCampaignScreen.h"
#include "CPreGame.h"
#include "OptionsTab.h"
#include "RandomMapTab.h"
#include "SelectionTab.h"

#include "../../CCallback.h"

#include "../CGameInfo.h"
#include "../CPlayerInterface.h"
#include "../CMessage.h"
#include "../CBitmapHandler.h"
#include "../CMusicHandler.h"
#include "../CVideoHandler.h"
#include "../CPlayerInterface.h"
#include "../CServerHandler.h"
#include "../gui/CAnimation.h"
#include "../gui/CGuiHandler.h"
#include "../widgets/CComponent.h"
#include "../widgets/Buttons.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/ObjectLists.h"
#include "../widgets/TextControls.h"
#include "../windows/GUIClasses.h"
#include "../windows/InfoWindows.h"

#include "../../lib/NetPacks.h"
#include "../../lib/CGeneralTextHandler.h"
#include "../../lib/CHeroHandler.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/serializer/Connection.h"

void startGame();

ISelectionScreenInfo::ISelectionScreenInfo()
{
	screenType = CMenuScreen::mainMenu;
	assert(!SEL);
	SEL = this;
}

ISelectionScreenInfo::~ISelectionScreenInfo()
{
	assert(SEL == this);
	SEL = nullptr;
}

/**
 * Stores the current name of the savegame.
 *
 * TODO better solution for auto-selection when saving already saved games.
 * -> CSelectionScreen should be divided into CLoadGameScreen, CSaveGameScreen,...
 * The name of the savegame can then be stored non-statically in CGameState and
 * passed separately to CSaveGameScreen.
 */

CSelectionScreen::CSelectionScreen(CMenuScreen::EState Type, CMenuScreen::EGameMode gameMode)
	: ISelectionScreenInfo()
{
	CGPreGame::create(); //we depend on its graphics
	screenType = Type;

	OBJ_CONSTRUCTION_CAPTURING_ALL;

	IShowActivatable::type = BLOCK_ADV_HOTKEYS;
	pos.w = 762;
	pos.h = 584;
	if(Type == CMenuScreen::saveGame)
	{
		bordered = false;
		center(pos);
	}
	else if(Type == CMenuScreen::campaignList)
	{
		bordered = false;
		bg = new CPicture("CamCust.bmp", 0, 0);
		pos = bg->center();
	}
	else
	{
		bordered = true;
		//load random background
		const JsonVector & bgNames = CGPreGameConfig::get().getConfig()["game-select"].Vector();
		bg = new CPicture(RandomGeneratorUtil::nextItem(bgNames, CRandomGenerator::getDefault())->String(), 0, 0);
		pos = bg->center();
	}
	curTab = nullptr;
	tabRand = nullptr;

	card = new InfoCard(); //right info card
	if(screenType == CMenuScreen::campaignList)
	{
		tabOpt = nullptr;
	}
	else
	{
		tabOpt = new OptionsTab(); //scenario options tab
		tabOpt->recActions = DISPOSE;
	}
	tabSel = new SelectionTab(screenType, gameMode); //scenario selection tab
	tabSel->recActions = DISPOSE; // MPTODO
	tabSel->recActions = 255;
	curTab = tabSel;

	buttonSelect = nullptr;
	buttonRMG = nullptr;
	buttonOptions = nullptr;

	auto initLobby = [&]()
	{
		tabSel->onSelect = std::bind(&IServerAPI::setMapInfo, CSH, _1, nullptr);

		buttonSelect = new CButton(Point(411, 80), "GSPBUTT.DEF", CGI->generaltexth->zelp[45], 0, SDLK_s);
		buttonSelect->addCallback([&]()
		{
			toggleTab(tabSel);
			CSH->setMapInfo(tabSel->getSelectedMapInfo());
		});

		buttonOptions = new CButton(Point(411, 510), "GSPBUTT.DEF", CGI->generaltexth->zelp[46], std::bind(&CSelectionScreen::toggleTab, this, tabOpt), SDLK_a);

		CButton * buttonChat = new CButton(Point(619, 83), "GSPBUT2.DEF", CGI->generaltexth->zelp[48], std::bind(&InfoCard::toggleChat, card), SDLK_h);
		buttonChat->addTextOverlay(CGI->generaltexth->allTexts[531], FONT_SMALL);

		toggleMode(gameMode == CMenuScreen::MULTI_NETWORK_HOST);
	};

	switch(screenType)
	{
	case CMenuScreen::newGame:
	{
		tabRand = new RandomMapTab();
		tabRand->getMapInfoChanged() += std::bind(&IServerAPI::setMapInfo, CSH, _1, _2);
		tabRand->recActions = DISPOSE;
		buttonRMG = new CButton(Point(411, 105), "GSPBUTT.DEF", CGI->generaltexth->zelp[47], 0, SDLK_r);
		buttonRMG->addCallback([&]()
		{
			toggleTab(tabRand);
			tabRand->updateMapInfoByHost(); // MPTODO: This is only needed to force-update mapInfo in CSH when tab is opened
		});

		card->difficulty->addCallback(std::bind(&IServerAPI::setDifficulty, CSH, _1));
		card->difficulty->setSelected(1);

		buttonStart = new CButton(Point(411, 535), "SCNRBEG.DEF", CGI->generaltexth->zelp[103], std::bind(&CSelectionScreen::startScenario, this), SDLK_b);
		initLobby();
		break;
	}
	case CMenuScreen::loadGame:
	{
		buttonStart = new CButton(Point(411, 535), "SCNRLOD.DEF", CGI->generaltexth->zelp[103], std::bind(&CSelectionScreen::startScenario, this), SDLK_l);
		initLobby();
		break;
	}
	case CMenuScreen::saveGame:
		tabSel->onSelect = std::bind(&CSelectionScreen::changeSelectionSave, this, _1);
		buttonStart = new CButton(Point(411, 535), "SCNRSAV.DEF", CGI->generaltexth->zelp[103], std::bind(&CSelectionScreen::saveGame, this), SDLK_s);
		break;
	case CMenuScreen::campaignList:
		buttonStart = new CButton(Point(411, 535), "SCNRLOD.DEF", CButton::tooltip(), std::bind(&CSelectionScreen::startCampaign, this), SDLK_b);
		break;
	}

	buttonStart->assignedKeys.insert(SDLK_RETURN);

	buttonBack = new CButton(Point(581, 535), "SCNRBACK.DEF", CGI->generaltexth->zelp[105], std::bind(&CGuiHandler::popIntTotally, &GH, this), SDLK_ESCAPE);
}

CSelectionScreen::~CSelectionScreen()
{
	CSH->stopServerConnection();
}

void CSelectionScreen::showAll(SDL_Surface * to)
{
	CIntObject::showAll(to);
	if(bordered && (pos.h != to->h || pos.w != to->w))
		CMessage::drawBorder(PlayerColor(1), to, pos.w + 28, pos.h + 30, pos.x - 14, pos.y - 15);
}

void CSelectionScreen::toggleTab(CIntObject * tab)
{
	if(screenType != CMenuScreen::saveGame)
	{
		PregameGuiAction pga;
		if(tab == curTab)
			pga.action = PregameGuiAction::NO_TAB;
		else if(tab == tabOpt)
			pga.action = PregameGuiAction::OPEN_OPTIONS;
		else if(tab == tabSel)
			pga.action = PregameGuiAction::OPEN_SCENARIO_LIST;
		else if(tab == tabRand)
			pga.action = PregameGuiAction::OPEN_RANDOM_MAP_OPTIONS;

		CSH->propagateGuiAction(pga);
	}

	if(curTab && curTab->active)
	{
		curTab->deactivate();
		curTab->recActions = DISPOSE;
	}

	if(curTab != tab)
	{
		tab->recActions = 255;
		tab->activate();
		curTab = tab;
	}
	else
	{
		curTab = nullptr;
	}
	GH.totalRedraw();
}

void CSelectionScreen::changeSelectionSave(std::shared_ptr<CMapInfo> to)
{
	if(CSH->mi == to)
		return;

	CSH->mi = to;

	if(CSH->mi)
		CSH->si->difficulty = CSH->mi->scenarioOpts->difficulty;
/*
	if(screenType != CMenuScreen::campaignList)
	{
		CSH->updateStartInfo();
	}
*/
	card->changeSelection();
}

void CSelectionScreen::startCampaign()
{
	if(CSH->mi)
		GH.pushInt(new CBonusSelection(CSH->mi->fileURI));
}

void CSelectionScreen::startScenario()
{
	try
	{
		CSH->startGame();
		buttonStart->block(true);
	}
	catch(mapMissingException & e)
	{

	}
	catch(noHumanException & e) //for boost errors just log, not crash - probably client shut down connection
	{
		GH.pushInt(CInfoWindow::create(CGI->generaltexth->allTexts[530])); // You must position yourself prior to starting the game.
	}
	catch(noTemplateException & e)
	{
		GH.pushInt(CInfoWindow::create(CGI->generaltexth->allTexts[751]));
	}
	catch(...)
	{

	}
}

void CSelectionScreen::saveGame()
{
	if(!(tabSel && tabSel->txt && tabSel->txt->text.size()))
		return;

	std::string path = "Saves/" + tabSel->txt->text;

	auto overWrite = [&]() -> void
	{
		Settings lastSave = settings.write["session"]["lastSave"];
		lastSave->String() = path;
		LOCPLINT->cb->save(path);
		GH.popIntTotally(this);
	};

	if(CResourceHandler::get("local")->existsResource(ResourceID(path, EResType::CLIENT_SAVEGAME)))
	{
		std::string hlp = CGI->generaltexth->allTexts[493]; //%s exists. Overwrite?
		boost::algorithm::replace_first(hlp, "%s", tabSel->txt->text);
		LOCPLINT->showYesNoDialog(hlp, overWrite, 0, false);
	}
	else
	{
		overWrite();
	}
}

void CSelectionScreen::toggleMode(bool host)
{
	auto getColor = [=]()
	{
		return host ? Colors::WHITE : Colors::ORANGE;
	};
	buttonSelect->addTextOverlay(CGI->generaltexth->allTexts[500], FONT_SMALL, getColor());
	buttonOptions->addTextOverlay(CGI->generaltexth->allTexts[501], FONT_SMALL, getColor());
	if(buttonRMG)
	{
		buttonRMG->addTextOverlay(CGI->generaltexth->allTexts[740], FONT_SMALL, getColor());
		buttonRMG->block(!host);
	}
	buttonSelect->block(!host);
	buttonOptions->block(!host);
	buttonStart->block(!host);

	tabSel->toggleMode(host ? CMenuScreen::MULTI_NETWORK_HOST : CMenuScreen::MULTI_NETWORK_GUEST);
	if(CSH->mi)
		tabOpt->recreate();
}

CSavingScreen::CSavingScreen()
	: CSelectionScreen(CMenuScreen::saveGame, (LOCPLINT->cb->getStartInfo()->mode == StartInfo::CAMPAIGN ? CMenuScreen::SINGLE_CAMPAIGN : CMenuScreen::MULTI_NETWORK_HOST))
{
//MPTODO	CSH->si = &(*LOCPLINT->cb->getStartInfo());
}

CSavingScreen::~CSavingScreen()
{

}

InfoCard::InfoCard()
	: sizes(nullptr), bg(nullptr), showChat(true), chat(nullptr), playerListBg(nullptr), difficulty(nullptr)
{
	OBJ_CONSTRUCTION_CAPTURING_ALL;
	CIntObject::type |= REDRAW_PARENT;
	pos.x += 393;
	pos.y += 6;
	addUsedEvents(RCLICK);
	mapDescription = nullptr;

	Rect descriptionRect(26, 149, 320, 115);
	mapDescription = new CTextBox("", descriptionRect, 1);

	if(SEL->screenType == CMenuScreen::campaignList)
	{
		CSelectionScreen * ss = static_cast<CSelectionScreen *>(parent);
		mapDescription->addChild(new CPicture(*ss->bg, descriptionRect + Point(-393, 0)), true); //move subpicture bg to our description control (by default it's our (Infocard) child)
	}
	else
	{
		bg = new CPicture("GSELPOP1.bmp", 0, 0);
		parent->addChild(bg);
		auto it = vstd::find(parent->children, this); //our position among parent children
		parent->children.insert(it, bg); //put BG before us
		parent->children.pop_back();
		pos.w = bg->pos.w;
		pos.h = bg->pos.h;
		sizes = new CAnimImage("SCNRMPSZ", 4, 0, 318, 22); //let it be custom size (frame 4) by default
		sizes->recActions &= ~(SHOWALL | UPDATE); //explicit draw

		sFlags = std::make_shared<CAnimation>("ITGFLAGS.DEF");
		sFlags->load();
		difficulty = new CToggleGroup(0);
		{
			static const char * difButns[] = {"GSPBUT3.DEF", "GSPBUT4.DEF", "GSPBUT5.DEF", "GSPBUT6.DEF", "GSPBUT7.DEF"};

			for(int i = 0; i < 5; i++)
			{
				auto button = new CToggleButton(Point(110 + i * 32, 450), difButns[i], CGI->generaltexth->zelp[24 + i]);

				difficulty->addToggle(i, button);
				if(SEL->screenType != CMenuScreen::newGame)
					button->block(true);
			}
		}

		playerListBg = new CPicture("CHATPLUG.bmp", 16, 276);
		chat = new CChatBox(Rect(26, 132, 340, 132));
		setChat(false); // FIXME: This shouln't be needed if chat / description wouldn't overlay each other on init
	}

	victory = new CAnimImage("SCNRVICT", 0, 0, 24, 302);
	victory->recActions &= ~(SHOWALL | UPDATE); //explicit draw
	loss = new CAnimImage("SCNRLOSS", 0, 0, 24, 359);
	loss->recActions &= ~(SHOWALL | UPDATE); //explicit draw
}

InfoCard::~InfoCard()
{
	if(sFlags)
		sFlags->unload();
}

void InfoCard::showAll(SDL_Surface * to)
{
	CIntObject::showAll(to);

	//blit texts
	if(SEL->screenType != CMenuScreen::campaignList)
	{
		printAtLoc(CGI->generaltexth->allTexts[390] + ":", 24, 400, FONT_SMALL, Colors::WHITE, to); //Allies
		printAtLoc(CGI->generaltexth->allTexts[391] + ":", 190, 400, FONT_SMALL, Colors::WHITE, to); //Enemies
		printAtLoc(CGI->generaltexth->allTexts[494], 33, 430, FONT_SMALL, Colors::YELLOW, to); //"Map Diff:"
		printAtLoc(CGI->generaltexth->allTexts[492] + ":", 133, 430, FONT_SMALL, Colors::YELLOW, to); //player difficulty
		printAtLoc(CGI->generaltexth->allTexts[218] + ":", 290, 430, FONT_SMALL, Colors::YELLOW, to); //"Rating:"
		printAtLoc(CGI->generaltexth->allTexts[495], 26, 22, FONT_SMALL, Colors::YELLOW, to); //Scenario Name:
		if(!showChat)
		{
			printAtLoc(CGI->generaltexth->allTexts[496], 26, 132, FONT_SMALL, Colors::YELLOW, to); //Scenario Description:
			printAtLoc(CGI->generaltexth->allTexts[497], 26, 283, FONT_SMALL, Colors::YELLOW, to); //Victory Condition:
			printAtLoc(CGI->generaltexth->allTexts[498], 26, 339, FONT_SMALL, Colors::YELLOW, to); //Loss Condition:
		}
		else
		{
			int playerLeft = 0; // Players with assigned colors
			int playersRight = 0;
			for(auto & p : CSH->playerNames)
			{
				const auto pset = CSH->si->getPlayersSettings(p.first);
				int pid = p.first;
				if(pset)
				{
					auto name = boost::str(boost::format("%s (%d-%d %s)") % p.second.name % p.second.connection % pid % pset->color.getStr());
					printAtLoc(name, 24, 285 + playerLeft++ *graphics->fonts[FONT_SMALL]->getLineHeight(), FONT_SMALL, Colors::WHITE, to);
				}
				else
				{
					auto name = boost::str(boost::format("%s (%d-%d)") % p.second.name % p.second.connection % pid);
					printAtLoc(name, 193, 285 + playersRight++ *graphics->fonts[FONT_SMALL]->getLineHeight(), FONT_SMALL, Colors::WHITE, to);
				}
			}
		}
	}

	if(CSH->mi)
	{
		if(SEL->screenType != CMenuScreen::campaignList)
		{
			if(!showChat)
			{
				CMapHeader * header = CSH->mi->mapHeader.get();
				//victory conditions
				printAtLoc(header->victoryMessage, 60, 307, FONT_SMALL, Colors::WHITE, to);
				victory->setFrame(header->victoryIconIndex);
				victory->showAll(to);
				//loss conditoins
				printAtLoc(header->defeatMessage, 60, 366, FONT_SMALL, Colors::WHITE, to);
				loss->setFrame(header->defeatIconIndex);
				loss->showAll(to);
			}

			//difficulty
			assert(CSH->mi->mapHeader->difficulty <= 4);
			std::string & diff = CGI->generaltexth->arraytxt[142 + CSH->mi->mapHeader->difficulty];
			printAtMiddleLoc(diff, 62, 472, FONT_SMALL, Colors::WHITE, to);

			//selecting size icon
			switch(CSH->mi->mapHeader->width)
			{
			case 36:
				sizes->setFrame(0);
				break;
			case 72:
				sizes->setFrame(1);
				break;
			case 108:
				sizes->setFrame(2);
				break;
			case 144:
				sizes->setFrame(3);
				break;
			default:
				sizes->setFrame(4);
				break;
			}
			sizes->showAll(to);

			if(SEL->screenType == CMenuScreen::loadGame)
				printToLoc(CSH->mi->date, 308, 34, FONT_SMALL, Colors::WHITE, to);

			//print flags
			int fx = 34 + graphics->fonts[FONT_SMALL]->getStringWidth(CGI->generaltexth->allTexts[390]);
			int ex = 200 + graphics->fonts[FONT_SMALL]->getStringWidth(CGI->generaltexth->allTexts[391]);

			TeamID myT;

			if(CSH->myFirstColor() < PlayerColor::PLAYER_LIMIT)
				myT = CSH->getPlayerInfo(CSH->myFirstColor().getNum()).team;
			else
				myT = TeamID::NO_TEAM;

			for(auto i = CSH->si->playerInfos.cbegin(); i != CSH->si->playerInfos.cend(); i++)
			{
				int * myx = ((i->first == CSH->myFirstColor() || CSH->getPlayerInfo(i->first.getNum()).team == myT) ? &fx : &ex);
				IImage * flag = sFlags->getImage(i->first.getNum(), 0);
				flag->draw(to, pos.x + *myx, pos.y + 399);
				*myx += flag->width();
				flag->decreaseRef();
			}

			std::string tob;
			switch(CSH->si->difficulty)
			{
			case 0:
				tob = "80%";
				break;
			case 1:
				tob = "100%";
				break;
			case 2:
				tob = "130%";
				break;
			case 3:
				tob = "160%";
				break;
			case 4:
				tob = "200%";
				break;
			}
			printAtMiddleLoc(tob, 311, 472, FONT_SMALL, Colors::WHITE, to);
		}

		//blit description
		std::string name;

		if(SEL->screenType == CMenuScreen::campaignList)
		{
			name = CSH->mi->campaignHeader->name;
		}
		else
		{
			name = CSH->mi->mapHeader->name;
		}

		//name
		if(name.length())
			printAtLoc(name, 26, 39, FONT_BIG, Colors::YELLOW, to);
		else
			printAtLoc("Unnamed", 26, 39, FONT_BIG, Colors::YELLOW, to);
	}
}

void InfoCard::clickRight(tribool down, bool previousState)
{
	static const Rect flagArea(19, 397, 335, 23);
	if(CSH->mi && down && CSH->mi && isItInLoc(flagArea, GH.current->motion.x, GH.current->motion.y))
		showTeamsPopup();
}

void InfoCard::changeSelection()
{
	if(CSH->mi && mapDescription)
	{
		if(SEL->screenType == CMenuScreen::campaignList)
			mapDescription->setText(CSH->mi->campaignHeader->description);
		else
			mapDescription->setText(CSH->mi->mapHeader->description);

		mapDescription->label->scrollTextTo(0);
		if(mapDescription->slider)
			mapDescription->slider->moveToMin();

		if(SEL->screenType != CMenuScreen::newGame && SEL->screenType != CMenuScreen::campaignList)
		{
			//difficulty->block(true);
			difficulty->setSelected(CSH->si->difficulty);
		}
	}
	redraw();
}

void InfoCard::showTeamsPopup()
{
	SDL_Surface * bmp = CMessage::drawDialogBox(256, 90 + 50 * CSH->mi->mapHeader->howManyTeams);

	graphics->fonts[FONT_MEDIUM]->renderTextCenter(bmp, CGI->generaltexth->allTexts[657], Colors::YELLOW, Point(128, 30));

	for(int i = 0; i < CSH->mi->mapHeader->howManyTeams; i++)
	{
		std::vector<ui8> flags;
		std::string hlp = CGI->generaltexth->allTexts[656]; //Team %d
		hlp.replace(hlp.find("%d"), 2, boost::lexical_cast<std::string>(i + 1));

		graphics->fonts[FONT_SMALL]->renderTextCenter(bmp, hlp, Colors::WHITE, Point(128, 65 + 50 * i));

		for(int j = 0; j < PlayerColor::PLAYER_LIMIT_I; j++)
		{
			if((CSH->getPlayerInfo(j).canHumanPlay || CSH->getPlayerInfo(j).canComputerPlay)
				&& CSH->getPlayerInfo(j).team == TeamID(i))
			{
				flags.push_back(j);
			}
		}

		int curx = 128 - 9 * flags.size();
		for(auto & flag : flags)
		{
			IImage * icon = sFlags->getImage(flag, 0);
			icon->draw(bmp, curx, 75 + 50 * i);
			icon->decreaseRef();
			curx += 18;
		}
	}

	GH.pushInt(new CInfoPopup(bmp, true));
}

void InfoCard::toggleChat()
{
	setChat(!showChat);
}

void InfoCard::setChat(bool activateChat)
{
	if(showChat == activateChat)
		return;

//	assert(active); // FIXME: This shouln't be needed if chat / description wouldn't overlay each other on init

	if(activateChat)
	{
		mapDescription->disable();
		chat->enable();
		playerListBg->enable();
	}
	else
	{
		mapDescription->enable();
		chat->disable();
		playerListBg->disable();
	}

	showChat = activateChat;
	GH.totalRedraw();
}

CChatBox::CChatBox(const Rect & rect)
{
	OBJ_CONSTRUCTION;
	pos += rect;
	addUsedEvents(KEYBOARD | TEXTINPUT);
	captureAllKeys = true;
	type |= REDRAW_PARENT;

	const int height = graphics->fonts[FONT_SMALL]->getLineHeight();
	inputBox = new CTextInput(Rect(0, rect.h - height, rect.w, height));
	inputBox->removeUsedEvents(KEYBOARD);
	chatHistory = new CTextBox("", Rect(0, 0, rect.w, rect.h - height), 1);

	chatHistory->label->color = Colors::GREEN;
}

void CChatBox::keyPressed(const SDL_KeyboardEvent & key)
{
	if(key.keysym.sym == SDLK_RETURN && key.state == SDL_PRESSED && inputBox->text.size())
	{
		CSH->sendMessage(inputBox->text);
		inputBox->setText("");
	}
	else
		inputBox->keyPressed(key);
}

void CChatBox::addNewMessage(const std::string & text)
{
	CCS->soundh->playSound("CHAT");
	chatHistory->setText(chatHistory->label->text + text + "\n");
	if(chatHistory->slider)
		chatHistory->slider->moveToMax();
}

CScenarioInfo::CScenarioInfo()
{
	OBJ_CONSTRUCTION_CAPTURING_ALL;
	pos.w = 762;
	pos.h = 584;
	center(pos);

	assert(LOCPLINT);
//MPTODO	CSH->si = &(*LOCPLINT->cb->getStartInfo());

	screenType = CMenuScreen::scenarioInfo;

	card = new InfoCard();
	opt = new OptionsTab();
	opt->recreate();
	card->changeSelection();

	card->difficulty->setSelected(CSH->si->difficulty);
	back = new CButton(Point(584, 535), "SCNRBACK.DEF", CGI->generaltexth->zelp[105], std::bind(&CGuiHandler::popIntTotally, &GH, this), SDLK_ESCAPE);
}

CScenarioInfo::~CScenarioInfo()
{
	CSH->mi.reset();
}
