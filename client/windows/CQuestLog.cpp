#include "StdInc.h"
#include "CQuestLog.h"

#include "CAdvmapInterface.h"

#include "../CBitmapHandler.h"
#include "../CDefHandler.h"
#include "../CGameInfo.h"
#include "../CPlayerInterface.h"
#include "../Graphics.h"

#include "../gui/CGuiHandler.h"
#include "../gui/SDL_Extensions.h"

#include "../../CCallback.h"
#include "../../lib/CArtHandler.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/CGameState.h"
#include "../../lib/CGeneralTextHandler.h"
#include "../../lib/NetPacksBase.h"

/*
 * CQuestLog.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

const int DESCRIPTION_HEIGHT_MAX = 355;

struct QuestInfo;
class CAdvmapInterface;

void CQuestLabel::clickLeft(tribool down, bool previousState)
{
	if (down)
		callback();
}

void CQuestLabel::showAll(SDL_Surface * to)
{
	if (active)
		CMultiLineLabel::showAll (to);
}

CQuestIcon::CQuestIcon (const std::string &defname, int index, int x, int y) :
	CAnimImage(defname, index, 0, x, y)
{
	addUsedEvents(LCLICK);
}

void CQuestIcon::clickLeft(tribool down, bool previousState)
{
	if (down)
		callback();
}

void CQuestIcon::showAll(SDL_Surface * to)
{
	CSDL_Ext::CClipRectGuard guard(to, parent->pos);
	CAnimImage::showAll(to);
}

CQuestMinimap::CQuestMinimap (const Rect & position) :
CMinimap (position),
	currentQuest (nullptr)
{
}

void CQuestMinimap::addQuestMarks (const QuestInfo * q)
{
	OBJ_CONSTRUCTION_CAPTURING_ALL;
	for (auto icon : icons)
		delete icon;
	icons.clear();

	int3 tile;
	if (q->obj)
	{
		tile = q->obj->pos;
	}
	else
	{
		tile = q->tile;
	}
	int x,y;
	minimap->tileToPixels (tile, x, y);

	if (level != tile.z)
		setLevel(tile.z);

	CQuestIcon * pic = new CQuestIcon ("VwSymbol.def", 3, x, y);

	pic->moveBy (Point ( -pic->pos.w/2, -pic->pos.h/2));
	pic->callback = std::bind (&CQuestMinimap::iconClicked, this);

	icons.push_back(pic);
}

void CQuestMinimap::update()
{
	CMinimap::update();
	if (currentQuest)
	{
		addQuestMarks (currentQuest);
	}
}

void CQuestMinimap::iconClicked()
{
	if (currentQuest->obj)
		adventureInt->centerOn (currentQuest->obj->pos);
	//moveAdvMapSelection();
}

void CQuestMinimap::showAll(SDL_Surface * to)
{
	CIntObject::showAll(to); // blitting IntObject directly to hide radar
	for (auto pic : icons)
		pic->showAll(to);
}

CQuestLog::CQuestLog (const std::vector<QuestInfo> & Quests) :
	CWindowObject(PLAYER_COLORED | BORDERED, "questDialog.pcx"),
	questIndex(0),
	currentQuest(nullptr),
	quests (Quests),
	slider(nullptr),
	box(nullptr)
{
	OBJ_CONSTRUCTION_CAPTURING_ALL;
	init();
}

void CQuestLog::init()
{
	minimap = new CQuestMinimap (Rect (12, 12, 169, 169));
	// TextBox have it's own 4 pixel padding from top at least for English.To achieve 10px padding from both left and top only leave 6px padding
	description = new CTextBox ("", Rect(205, 18, 385, DESCRIPTION_HEIGHT_MAX), CSlider::BROWN, FONT_MEDIUM, TOPLEFT, Colors::WHITE);
	ok = new CButton(Point(539, 398), "IOKAY.DEF", CGI->generaltexth->zelp[445], boost::bind(&CQuestLog::close,this), SDLK_RETURN);

	for (int i = 0; i < quests.size(); ++i)
	{
		// Quests with MISSION_NONE type don't have text for them and can't be displayed
		if (quests[i].quest->missionType == CQuest::MISSION_NONE)
			continue;

		MetaString text;
		quests[i].quest->getRolloverText (text, false);
		if (quests[i].obj)
			text.addReplacement (quests[i].obj->getObjectName()); //get name of the object
		CQuestLabel * label = new CQuestLabel (Rect(13, 195, 149,31), FONT_SMALL, TOPLEFT, Colors::WHITE, text.toString());
		label->disable();

		int currentLabel = labels.size();
		label->callback = boost::bind(&CQuestLog::selectQuest, this, i, currentLabel);
		labels.push_back(label);

		// Select latest active quest
		if (quests[i].quest->progress != CQuest::COMPLETE) // TODO: there need to be difference between active and completed quests
			selectQuest(i, currentLabel);
	}
	recreateQuestList (0);

	slider = new CSlider(Point(166, 195), 191, std::bind (&CQuestLog::sliderMoved, this, _1), QUEST_COUNT, labels.size(), false, CSlider::BROWN);
	if (labels.size() > QUEST_COUNT)
	{
		slider->moveToMax();
	}
	else
		slider->block(true);
}

void CQuestLog::showAll(SDL_Surface * to)
{
	CWindowObject::showAll (to);
	if (labels.size() && labels[questIndex]->active)
	{
		Rect rect = Rect::around(labels[questIndex]->pos);
		rect.x -= 2;
		rect.w += 2;
		CSDL_Ext::drawBorder(to, rect, int3(Colors::METALLIC_GOLD.r, Colors::METALLIC_GOLD.g, Colors::METALLIC_GOLD.b));
	}
}

void CQuestLog::recreateQuestList (int newpos)
{
	for (int i = 0; i < labels.size(); ++i)
	{
		labels[i]->pos = Rect (pos.x + 14, pos.y + 195 + (i-newpos) * 32, 151, 31);
		if (i >= newpos && i < newpos + QUEST_COUNT)
		{
			labels[i]->enable();
		}
		else
		{
			labels[i]->disable();
		}
	}
	minimap->update();
}

void CQuestLog::selectQuest (int which, int labelId)
{
	questIndex = labelId;
	currentQuest = &quests[which];
	minimap->currentQuest = currentQuest;

	MetaString text;
	std::vector<Component> components;
	currentQuest->quest->getVisitText (text, components, currentQuest->quest->isCustomFirst, true);
	if (description->slider)
		description->slider->moveToMin();
	description->setText (text.toString()); //TODO: use special log entry text

	delete box;
	int componentsSize = components.size();
	if (componentsSize)
	{
		CComponent::ESize imageSize = CComponent::large;

		int boxPosX = 202;
		int boxSize = 391;
		int descriptionHeight = DESCRIPTION_HEIGHT_MAX-15;
		switch (currentQuest->quest->missionType)
		{
			case CQuest::MISSION_ARMY:
			{
				if (componentsSize > 4)
					descriptionHeight -= 195;
				else
					descriptionHeight -= 100;

				break;
			}
			case CQuest::MISSION_ART:
			{
				if (componentsSize > 4)
					descriptionHeight -= 190;
				else
					descriptionHeight -= 90;

				break;
			}
			case CQuest::MISSION_PRIMARY_STAT:
			case CQuest::MISSION_RESOURCES:
			{
				boxPosX = 204;
				boxSize = 388;

				if (componentsSize > 4)
				{
					imageSize = CComponent::small;
					descriptionHeight -= 140;
				}
				else
					descriptionHeight -= 125;

				break;
			}

			default:
				descriptionHeight -= 115;

				break;
		}

		OBJ_CONSTRUCTION_CAPTURING_ALL;
		std::vector<CComponent *> comps;
		for (auto & component : components)
		{
			comps.push_back(new CComponent(component, imageSize));
		}
		description->resize(Point(390, descriptionHeight));

		box = new CComponentBox(comps, Rect(boxPosX, 20+descriptionHeight+15, boxSize, 350-(20+descriptionHeight)));
	}

	minimap->update();
	redraw();
}

void CQuestLog::sliderMoved (int newpos)
{
	recreateQuestList (newpos); //move components
	redraw();
}
