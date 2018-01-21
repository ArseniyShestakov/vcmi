/*
 * CScenarioInfoScreen.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "CScenarioInfoScreen.h"
#include "OptionsTab.h"

#include "../CPlayerInterface.h"
#include "../gui/CGuiHandler.h"
#include "../widgets/Buttons.h"

#include "../../CCallback.h"

#include "../CGameInfo.h"
#include "../../lib/CGeneralTextHandler.h"

CScenarioInfoScreen::CScenarioInfoScreen()
{
	OBJ_CONSTRUCTION_CAPTURING_ALL;
	center(pos);

	assert(LOCPLINT);
	localSi = LOCPLINT->cb->getStartInfo();
	localMi = new CMapInfo();
	localMi->mapHeader = std::unique_ptr<CMapHeader>(new CMapHeader(*LOCPLINT->cb->getMapHeader()));

	screenType = CMenuScreen::scenarioInfo;

	card = new InfoCard();
	opt = new OptionsTab();
	opt->recreate();
	card->changeSelection();

	card->difficulty->setSelected(getCurrentDifficulty());
	back = new CButton(Point(584, 535), "SCNRBACK.DEF", CGI->generaltexth->zelp[105], std::bind(&CGuiHandler::popIntTotally, &GH, this), SDLK_ESCAPE);
}

CScenarioInfoScreen::~CScenarioInfoScreen()
{
	vstd::clear_pointer(localSi);
	vstd::clear_pointer(localMi);
}

const CMapInfo * CScenarioInfoScreen::getMapInfo()
{
	return localMi;
}

const StartInfo * CScenarioInfoScreen::getStartInfo()
{
	return localSi;
}
