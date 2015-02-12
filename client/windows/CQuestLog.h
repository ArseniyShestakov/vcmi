#pragma once

#include "../widgets/AdventureMapClasses.h"
#include "../widgets/TextControls.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/Images.h"
#include "CWindowObject.h"

#include "../widgets/CComponent.h"

/*
 * CQuestLog.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

class CCreature;
class CStackInstance;
class CButton;
class CGHeroInstance;
class LRClickableAreaWText;
class CButton;
class CPicture;
class CCreaturePic;
class LRClickableAreaWTextComp;
class CSlider;
class CLabel;
struct QuestInfo;

const int QUEST_COUNT = 7;

class CQuestLabel : public LRClickableAreaWText, public CMultiLineLabel
{
public:
	std::function<void()> callback;

	CQuestLabel (Rect position, EFonts Font = FONT_SMALL, EAlignment Align = TOPLEFT, const SDL_Color &Color = Colors::WHITE, const std::string &Text =  "")
		: CMultiLineLabel (position, FONT_SMALL, TOPLEFT, Colors::WHITE, Text){};
	void clickLeft(tribool down, bool previousState);
	void showAll(SDL_Surface * to);
};

class CQuestIcon : public CAnimImage
{
public:
	std::function<void()> callback; //TODO: merge with other similar classes?

	CQuestIcon (const std::string &defname, int index, int x=0, int y=0);

	void clickLeft(tribool down, bool previousState);
	void showAll(SDL_Surface * to);
};

class CQuestMinimap : public CMinimap
{
	std::vector <CQuestIcon *> icons;

	void clickLeft(tribool down, bool previousState){}; //minimap ignores clicking on its surface
	void iconClicked();
	void mouseMoved (const SDL_MouseMotionEvent & sEvent){};

public:

	const QuestInfo * currentQuest;

	CQuestMinimap (const Rect & position);
	//should be called to invalidate whole map - different player or level
	void update();
	void addQuestMarks (const QuestInfo * q);

	void showAll(SDL_Surface * to);
};

class CQuestLog : public CWindowObject
{
	CComponentBox * box;
	std::vector<Component> components;
	int questIndex;
	const QuestInfo * currentQuest;

	const std::vector<QuestInfo> quests;
	std::vector<CQuestLabel *> labels;
	CInfoWindow * desc;
	CTextBox * description;
	CQuestMinimap * minimap;
	CSlider * slider; //scrolls quests
	CButton *ok;
	int ignoredQuests;

	void init ();
public:

	CQuestLog (const std::vector<QuestInfo> & Quests);

	~CQuestLog(){};

	void selectQuest (int which, int labelId);
	void updateMinimap (int which){};
	void printDescription (int which){};
	void sliderMoved (int newpos);
	void recreateQuestList (int pos);
	void showAll (SDL_Surface * to);
};
