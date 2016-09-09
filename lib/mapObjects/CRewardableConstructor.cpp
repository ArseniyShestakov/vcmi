#include "StdInc.h"
#include "CRewardableConstructor.h"

#include "../CRandomGenerator.h"
#include "../StringConstants.h"
#include "../CCreatureHandler.h"
#include "JsonRandom.h"
#include "../IGameCallback.h"

/*
 * CRewardableConstructor.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

namespace {
	MetaString loadMessage(const JsonNode & value)
	{
		MetaString ret;
		if (value.getType() == JsonNode::DATA_FLOAT)
			ret.addTxt(MetaString::ADVOB_TXT, value.Float());
		else
			ret << value.String();
		return ret;
	}

	bool testForKey(const JsonNode & value, const std::string & key)
	{
		for( auto & reward : value["rewards"].Vector() )
		{
			if (!reward[key].isNull())
				return true;
		}
		return false;
	}
}

void CRandomRewardObjectInfo::init(const JsonNode & objectConfig)
{
	parameters = objectConfig;
}

void CRandomRewardObjectInfo::configureObject(CRewardableObject * object, CRandomGenerator & rand) const
{
	std::map<si32, si32> thrownDice;

	for (const JsonNode & reward : parameters["rewards"].Vector())
	{
		if (!reward["appearChance"].isNull())
		{
			JsonNode chance = reward["appearChance"];
			si32 diceID = chance["dice"].Float();

			if (thrownDice.count(diceID) == 0)
				thrownDice[diceID] = rand.getIntRange(1, 100)();

			if (!chance["min"].isNull())
			{
				int min = chance["min"].Float();
				if (min > thrownDice[diceID])
					continue;
			}
			if (!chance["max"].isNull())
			{
				int max = chance["max"].Float();
				if (max < thrownDice[diceID])
					continue;
			}
		}

		const JsonNode & limiter = reward["limiter"];
		CVisitInfo info;
		// load limiter
		info.limiter.numOfGrants = JsonRandom::loadValue(limiter["numOfGrants"], rand);
		info.limiter.dayOfWeek = JsonRandom::loadValue(limiter["dayOfWeek"], rand);
		info.limiter.minLevel = JsonRandom::loadValue(limiter["minLevel"], rand);
		info.limiter.resources = JsonRandom::loadResources(limiter["resources"], rand);

		info.limiter.primary = JsonRandom::loadPrimary(limiter["primary"], rand);
		info.limiter.secondary = JsonRandom::loadSecondary(limiter["secondary"], rand);
		info.limiter.artifacts = JsonRandom::loadArtifacts(limiter["artifacts"], rand);
		info.limiter.creatures = JsonRandom::loadCreatures(limiter["creatures"], rand);

		info.reward.resources = JsonRandom::loadResources(reward["resources"], rand);

		info.reward.gainedExp = JsonRandom::loadValue(reward["gainedExp"], rand);
		info.reward.gainedLevels = JsonRandom::loadValue(reward["gainedLevels"], rand);

		info.reward.manaDiff = JsonRandom::loadValue(reward["manaPoints"], rand);
		info.reward.manaPercentage = JsonRandom::loadValue(reward["manaPercentage"], rand, -1);

		info.reward.movePoints = JsonRandom::loadValue(reward["movePoints"], rand);
		info.reward.movePercentage = JsonRandom::loadValue(reward["movePercentage"], rand, -1);

		//FIXME: compile this line on Visual
		//info.reward.bonuses = JsonRandom::loadBonuses(reward["bonuses"]);

		info.reward.primary = JsonRandom::loadPrimary(reward["primary"], rand);
		info.reward.secondary = JsonRandom::loadSecondary(reward["secondary"], rand);

		std::vector<SpellID> spells;
		for (size_t i=0; i<6; i++)
			IObjectInterface::cb->getAllowedSpells(spells, i);

		info.reward.artifacts = JsonRandom::loadArtifacts(reward["artifacts"], rand);
		info.reward.spells = JsonRandom::loadSpells(reward["spells"], rand, spells);
		info.reward.creatures = JsonRandom::loadCreatures(reward["creatures"], rand);

		info.message = loadMessage(reward["message"]);
		info.selectChance = JsonRandom::loadValue(reward["selectChance"], rand);
	}

	object->onSelect  = loadMessage(parameters["onSelectMessage"]);
	object->onVisited = loadMessage(parameters["onVisitedMessage"]);
	object->onEmpty   = loadMessage(parameters["onEmptyMessage"]);

	//TODO: visitMode and selectMode

	object->soundID = parameters["soundID"].Float();
	object->resetDuration = parameters["resetDuration"].Float();
	object->canRefuse =parameters["canRefuse"].Bool();
}

bool CRandomRewardObjectInfo::givesResources() const
{
	return testForKey(parameters, "resources");
}

bool CRandomRewardObjectInfo::givesExperience() const
{
	return testForKey(parameters, "gainedExp") || testForKey(parameters, "gainedLevels");
}

bool CRandomRewardObjectInfo::givesMana() const
{
	return testForKey(parameters, "manaPoints") || testForKey(parameters, "manaPercentage");
}

bool CRandomRewardObjectInfo::givesMovement() const
{
	return testForKey(parameters, "movePoints") || testForKey(parameters, "movePercentage");
}

bool CRandomRewardObjectInfo::givesPrimarySkills() const
{
	return testForKey(parameters, "primary");
}

bool CRandomRewardObjectInfo::givesSecondarySkills() const
{
	return testForKey(parameters, "secondary");
}

bool CRandomRewardObjectInfo::givesArtifacts() const
{
	return testForKey(parameters, "artifacts");
}

bool CRandomRewardObjectInfo::givesCreatures() const
{
	return testForKey(parameters, "spells");
}

bool CRandomRewardObjectInfo::givesSpells() const
{
	return testForKey(parameters, "creatures");
}

bool CRandomRewardObjectInfo::givesBonuses() const
{
	return testForKey(parameters, "bonuses");
}

CRewardableConstructor::CRewardableConstructor()
{
}

void CRewardableConstructor::initTypeData(const JsonNode & config)
{
	AObjectTypeHandler::init(config);
	objectInfo.init(config);
}

CGObjectInstance * CRewardableConstructor::create(const ObjectTemplate & tmpl) const
{
	auto ret = new CRewardableObject();
	preInitObject(ret);
	ret->appearance = tmpl;
	return ret;
}

void CRewardableConstructor::configureObject(CGObjectInstance * object, CRandomGenerator & rand) const
{
	objectInfo.configureObject(dynamic_cast<CRewardableObject*>(object), rand);
}

std::unique_ptr<IObjectInfo> CRewardableConstructor::getObjectInfo(const ObjectTemplate & tmpl) const
{
	return std::unique_ptr<IObjectInfo>(new CRandomRewardObjectInfo(objectInfo));
}
