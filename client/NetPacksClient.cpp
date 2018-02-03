/*
 * NetPacksClient.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../lib/NetPacks.h"

#include "../lib/filesystem/Filesystem.h"
#include "../lib/filesystem/FileInfo.h"
#include "../CCallback.h"
#include "Client.h"
#include "CPlayerInterface.h"
#include "CGameInfo.h"
#include "../lib/serializer/Connection.h"
#include "../lib/serializer/BinarySerializer.h"
#include "../lib/CGeneralTextHandler.h"
#include "../lib/CHeroHandler.h"
#include "../lib/VCMI_Lib.h"
#include "../lib/mapping/CMap.h"
#include "../lib/VCMIDirs.h"
#include "../lib/spells/CSpellHandler.h"
#include "../lib/CSoundBase.h"
#include "../lib/StartInfo.h"
#include "mapHandler.h"
#include "windows/GUIClasses.h"
#include "../lib/CConfigHandler.h"
#include "gui/SDL_Extensions.h"
#include "battle/CBattleInterface.h"
#include "../lib/mapping/CCampaignHandler.h"
#include "../lib/CGameState.h"
#include "../lib/CStack.h"
#include "../lib/battle/BattleInfo.h"
#include "../lib/GameConstants.h"
#include "../lib/CPlayerState.h"
#include "gui/CGuiHandler.h"
#include "widgets/MiscWidgets.h"
#include "widgets/AdventureMapClasses.h"
#include "CMT.h"
#include "CServerHandler.h"

//macros to avoid code duplication - calls given method with given arguments if interface for specific player is present
//awaiting variadic templates...

#define _CALL_IN_PRIVILAGED_INTS(function, ...)										\
	do																				\
	{																				\
		for(auto &ger : cl->privilagedGameEventReceivers)	\
			ger->function(__VA_ARGS__);												\
	} while(0)

#define _CALL_ONLY_THAT_INTERFACE(player, function, ...)		\
		do													\
		{													\
		if(vstd::contains(cl->playerint,player))			\
			cl->playerint[player]->function(__VA_ARGS__);	\
		}while(0)

#define _INTERFACE_CALL_IF_PRESENT(player,function,...) 				\
		do															\
		{															\
			_CALL_ONLY_THAT_INTERFACE(player, function, __VA_ARGS__);\
			_CALL_IN_PRIVILAGED_INTS(function, __VA_ARGS__);			\
		} while(0)

#define _CALL_ONLY_THAT_BATTLE_INTERFACE(player,function, ...) 	\
	do															\
	{															\
		if(vstd::contains(cl->battleints,player))				\
			cl->battleints[player]->function(__VA_ARGS__);		\
																\
		if(cl->additionalBattleInts.count(player))				\
		{														\
			for(auto bInt : cl->additionalBattleInts[player])\
				bInt->function(__VA_ARGS__);					\
		}														\
	} while (0);

#define _BATTLE_INTERFACE_CALL_RECEIVERS(function,...) 	\
	do															\
	{															\
		for(auto & ber : cl->privilagedBattleEventReceivers)\
			ber->function(__VA_ARGS__);							\
	} while(0)

#define _BATTLE_INTERFACE_CALL_IF_PRESENT(player,function,...) 	\
	do															\
	{															\
		_CALL_ONLY_THAT_INTERFACE(player, function, __VA_ARGS__);\
		_BATTLE_INTERFACE_CALL_RECEIVERS(function, __VA_ARGS__);	\
	} while(0)

//calls all normal interfaces and privilaged ones, playerints may be updated when iterating over it, so we need a copy
#define _CALL_IN_ALL_INTERFACES(function, ...)							\
	do																	\
	{																	\
		auto ints = cl->playerint;			\
		for(auto i = ints.begin(); i != ints.end(); i++)\
			_CALL_ONLY_THAT_INTERFACE(i->first, function, __VA_ARGS__);	\
	} while(0)


//calls all normal interfaces and privilaged ones, playerints may be updated when iterating over it, so we need a copy

#define _BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(function,...) 				\
	_CALL_ONLY_THAT_BATTLE_INTERFACE(GS(cl)->curB->sides[0].color, function, __VA_ARGS__)	\
	_CALL_ONLY_THAT_BATTLE_INTERFACE(GS(cl)->curB->sides[1].color, function, __VA_ARGS__)	\
	if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool() && LOCPLINT->battleInt)	\
	{																					\
		_CALL_ONLY_THAT_BATTLE_INTERFACE(PlayerColor::SPECTATOR, function, __VA_ARGS__)	\
	}																					\
	_BATTLE_INTERFACE_CALL_RECEIVERS(function, __VA_ARGS__)


template<typename T, typename ... Args, typename ... Args2>
void CALL_IN_PRIVILAGED_INTS(CClient * cl, void (T::*ptr)(Args...), Args2 ...args)
{
	for(auto &ger : cl->privilagedGameEventReceivers)
		ger->*ptr(args...);
}

template<typename T, typename ... Args, typename ... Args2>
void CALL_ONLY_THAT_INTERFACE(CClient * cl, PlayerColor player, void (T::*ptr)(Args...), Args2 ...args)
{
	if(vstd::contains(cl->playerint, player))
		cl->playerint[player]->*ptr(args ...);
}

template<typename T, typename ... Args, typename ... Args2>
void INTERFACE_CALL_IF_PRESENT(CClient * cl, PlayerColor player, void (T::*ptr)(Args...), Args2 ...args)
{
	CALL_ONLY_THAT_INTERFACE(cl, player, ptr, args...);
	CALL_IN_PRIVILAGED_INTS(cl, ptr, args ...);
}

template<typename T, typename ... Args, typename ... Args2>
void CALL_ONLY_THAT_BATTLE_INTERFACE(CClient * cl, PlayerColor player, void (T::*ptr)(Args...), Args2 ...args)
{
	if(vstd::contains(cl->battleints,player))
		cl->battleints[player]->*ptr(args...);

	if(cl->additionalBattleInts.count(player))
	{
		for(auto bInt : cl->additionalBattleInts[player])
			bInt->*ptr(args...);
	}
}

template<typename T, typename ... Args, typename ... Args2>
void BATTLE_INTERFACE_CALL_RECEIVERS(CClient * cl, void (T::*ptr)(Args...), Args2 ...args)
{
	for(auto & ber : cl->privilagedBattleEventReceivers)
		ber->*ptr(args...);
}

template<typename T, typename ... Args, typename ... Args2>
void BATTLE_INTERFACE_CALL_IF_PRESENT(CClient * cl, PlayerColor player, void (T::*ptr)(Args...), Args2 ...args)
{
	CALL_ONLY_THAT_INTERFACE(cl, player, ptr, args...);
	BATTLE_INTERFACE_CALL_RECEIVERS(cl, ptr, args...);
}

//calls all normal interfaces and privilaged ones, playerints may be updated when iterating over it, so we need a copy
template<typename T, typename ... Args, typename ... Args2>
void CALL_IN_ALL_INTERFACES(CClient * cl, void (T::*ptr)(Args...), Args2 ...args)
{
	auto ints = cl->playerint;
	for(auto i = ints.begin(); i != ints.end(); i++)
		CALL_ONLY_THAT_INTERFACE(cl, i->first, ptr, args...);
}

//calls all normal interfaces and privilaged ones, playerints may be updated when iterating over it, so we need a copy
template<typename T, typename ... Args, typename ... Args2>
void BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(CClient * cl, void (T::*ptr)(Args...), Args2 ...args)
{
	CALL_ONLY_THAT_BATTLE_INTERFACE(cl, cl->gameState()->curB->sides[0].color, ptr, args...);
	CALL_ONLY_THAT_BATTLE_INTERFACE(cl, cl->gameState()->curB->sides[1].color, ptr, args...);
	if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool() && LOCPLINT->battleInt)
	{
		CALL_ONLY_THAT_BATTLE_INTERFACE(cl, PlayerColor::SPECTATOR, ptr, args...);
	}
	BATTLE_INTERFACE_CALL_RECEIVERS(cl, ptr, args...);
}


void SetResources::applyCl(CClient *cl)
{
	//todo: inform on actual resource set transfered
	INTERFACE_CALL_IF_PRESENT(cl, player, &IGameEventsReceiver::receivedResource);
}

void SetPrimSkill::applyCl(CClient *cl)
{
	const CGHeroInstance *h = cl->getHero(id);
	if(!h)
	{
		logNetwork->error("Cannot find hero with ID %d", id.getNum());
		return;
	}
	INTERFACE_CALL_IF_PRESENT(cl, h->tempOwner, &IGameEventsReceiver::heroPrimarySkillChanged,h,which,val);
}

void SetSecSkill::applyCl(CClient *cl)
{
	const CGHeroInstance *h = cl->getHero(id);
	if(!h)
	{
		logNetwork->error("Cannot find hero with ID %d", id.getNum());
		return;
	}
	INTERFACE_CALL_IF_PRESENT(cl, h->tempOwner, &IGameEventsReceiver::heroSecondarySkillChanged,h,which,val);
}

void HeroVisitCastle::applyCl(CClient *cl)
{
	const CGHeroInstance *h = cl->getHero(hid);

	if(start())
	{
		INTERFACE_CALL_IF_PRESENT(cl, h->tempOwner, &IGameEventsReceiver::heroVisitsTown, h, GS(cl)->getTown(tid));
	}
}

void ChangeSpells::applyCl(CClient *cl)
{
	//TODO: inform interface?
}

void SetMana::applyCl(CClient *cl)
{
	const CGHeroInstance *h = cl->getHero(hid);
	INTERFACE_CALL_IF_PRESENT(cl, h->tempOwner, &IGameEventsReceiver::heroManaPointsChanged, h);
}

void SetMovePoints::applyCl(CClient *cl)
{
	const CGHeroInstance *h = cl->getHero(hid);
	cl->invalidatePaths();
	INTERFACE_CALL_IF_PRESENT(cl, h->tempOwner, &IGameEventsReceiver::heroMovePointsChanged, h);
}

void FoWChange::applyCl(CClient *cl)
{
	for(auto &i : cl->playerint)
	{
		if(cl->getPlayerRelations(i.first, player) == PlayerRelations::SAME_PLAYER && waitForDialogs && LOCPLINT == i.second.get())
		{
			LOCPLINT->waitWhileDialog();
		}
		if(cl->getPlayerRelations(i.first, player) != PlayerRelations::ENEMIES)
		{
			if(mode)
				i.second->tileRevealed(tiles);
			else
				i.second->tileHidden(tiles);
		}
	}
	cl->invalidatePaths();
}

void SetAvailableHeroes::applyCl(CClient *cl)
{
	//TODO: inform interface?
}

void ChangeStackCount::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, sl.army->tempOwner, &IGameEventsReceiver::stackChagedCount, sl, count, absoluteValue);
}

void SetStackType::applyCl(CClient *cl)
{
	_INTERFACE_CALL_IF_PRESENT(sl.army->tempOwner, stackChangedType, sl, *type);
}

void EraseStack::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, sl.army->tempOwner, &IGameEventsReceiver::stacksErased, sl);
}

void SwapStacks::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, sl1.army->tempOwner, &IGameEventsReceiver::stacksSwapped, sl1, sl2);
	if(sl1.army->tempOwner != sl2.army->tempOwner)
		INTERFACE_CALL_IF_PRESENT(cl, sl2.army->tempOwner, &IGameEventsReceiver::stacksSwapped, sl1, sl2);
}

void InsertNewStack::applyCl(CClient *cl)
{
	_INTERFACE_CALL_IF_PRESENT(sl.army->tempOwner, newStackInserted, sl, *sl.getStack());
}

void RebalanceStacks::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, src.army->tempOwner, &IGameEventsReceiver::stacksRebalanced, src, dst, count);
	if(src.army->tempOwner != dst.army->tempOwner)
		INTERFACE_CALL_IF_PRESENT(cl, dst.army->tempOwner, &IGameEventsReceiver::stacksRebalanced, src, dst, count);
}

void PutArtifact::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, al.owningPlayer(), &IGameEventsReceiver::artifactPut, al);
}

void EraseArtifact::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, al.owningPlayer(), &IGameEventsReceiver::artifactRemoved, al);
}

void MoveArtifact::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, src.owningPlayer(), &IGameEventsReceiver::artifactMoved, src, dst);
	if(src.owningPlayer() != dst.owningPlayer())
		INTERFACE_CALL_IF_PRESENT(cl, dst.owningPlayer(), &IGameEventsReceiver::artifactMoved, src, dst);
}

void AssembledArtifact::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, al.owningPlayer(), &IGameEventsReceiver::artifactAssembled, al);
}

void DisassembledArtifact::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, al.owningPlayer(), &IGameEventsReceiver::artifactDisassembled, al);
}

void HeroVisit::applyCl(CClient *cl)
{
	assert(hero);
	INTERFACE_CALL_IF_PRESENT(cl, player, &IGameEventsReceiver::heroVisit, hero, obj, starting);
}

void NewTurn::applyCl(CClient *cl)
{
	cl->invalidatePaths();
}


void GiveBonus::applyCl(CClient *cl)
{
	cl->invalidatePaths();
	switch(who)
	{
	case HERO:
		{
			const CGHeroInstance *h = GS(cl)->getHero(ObjectInstanceID(id));
			INTERFACE_CALL_IF_PRESENT(cl, h->tempOwner, &IGameEventsReceiver::heroBonusChanged, h, *h->getBonusList().back(),true);
		}
		break;
	case PLAYER:
		{
			const PlayerState *p = GS(cl)->getPlayer(PlayerColor(id));
			INTERFACE_CALL_IF_PRESENT(cl, PlayerColor(id), &IGameEventsReceiver::playerBonusChanged, *p->getBonusList().back(), true);
		}
		break;
	}
}

void ChangeObjPos::applyFirstCl(CClient *cl)
{
	CGObjectInstance *obj = GS(cl)->getObjInstance(objid);
	if(flags & 1 && CGI->mh)
		CGI->mh->hideObject(obj);
}
void ChangeObjPos::applyCl(CClient *cl)
{
	CGObjectInstance *obj = GS(cl)->getObjInstance(objid);
	if(flags & 1 && CGI->mh)
		CGI->mh->printObject(obj);

	cl->invalidatePaths();
}

void PlayerEndsGame::applyCl(CClient *cl)
{
	CALL_IN_ALL_INTERFACES(cl, &IGameEventsReceiver::gameOver, player, victoryLossCheckResult);

	// In auto testing mode we always close client if red player won or lose
	if(!settings["session"]["testmap"].isNull() && player == PlayerColor(0))
		handleQuit(settings["session"]["spectate"].Bool()); // if spectator is active ask to close client or not
}

void RemoveBonus::applyCl(CClient *cl)
{
	cl->invalidatePaths();
	switch(who)
	{
	case HERO:
		{
			const CGHeroInstance *h = GS(cl)->getHero(ObjectInstanceID(id));
			INTERFACE_CALL_IF_PRESENT(cl, h->tempOwner, &IGameEventsReceiver::heroBonusChanged, h, bonus,false);
		}
		break;
	case PLAYER:
		{
			//const PlayerState *p = GS(cl)->getPlayer(id);
			INTERFACE_CALL_IF_PRESENT(cl, PlayerColor(id), &IGameEventsReceiver::playerBonusChanged, bonus, false);
		}
		break;
	}
}

void UpdateCampaignState::applyCl(CClient *cl)
{
	cl->stopConnection();
	cl->campaignMapFinished(camp);
}

void PrepareForAdvancingCampaign::applyCl(CClient *cl)
{
	CSH->c->prepareForSendingHeroes();
}

void RemoveObject::applyFirstCl(CClient *cl)
{
	const CGObjectInstance *o = cl->getObj(id);

	if(CGI->mh)
		CGI->mh->hideObject(o, true);

	//notify interfaces about removal
	for(auto i=cl->playerint.begin(); i!=cl->playerint.end(); i++)
	{
		if(GS(cl)->isVisible(o, i->first))
			i->second->objectRemoved(o);
	}
}

void RemoveObject::applyCl(CClient *cl)
{
	cl->invalidatePaths();
}

void TryMoveHero::applyFirstCl(CClient *cl)
{
	CGHeroInstance *h = GS(cl)->getHero(id);

	//check if playerint will have the knowledge about movement - if not, directly update maphandler
	for(auto i=cl->playerint.begin(); i!=cl->playerint.end(); i++)
	{
		auto ps = GS(cl)->getPlayer(i->first);
		if(ps && (GS(cl)->isVisible(start - int3(1, 0, 0), i->first) || GS(cl)->isVisible(end - int3(1, 0, 0), i->first)))
		{
			if(ps->human)
				humanKnows = true;
		}
	}

	if(!CGI->mh)
		return;

	if(result == TELEPORTATION  ||  result == EMBARK  ||  result == DISEMBARK  ||  !humanKnows)
		CGI->mh->hideObject(h, result == EMBARK && humanKnows);

	if(result == DISEMBARK)
		CGI->mh->printObject(h->boat);
}

void TryMoveHero::applyCl(CClient *cl)
{
	const CGHeroInstance *h = cl->getHero(id);
	cl->invalidatePaths();

	if(CGI->mh)
	{
		if(result == TELEPORTATION  ||  result == EMBARK  ||  result == DISEMBARK)
			CGI->mh->printObject(h, result == DISEMBARK);

		if(result == EMBARK)
			CGI->mh->hideObject(h->boat);
	}

	PlayerColor player = h->tempOwner;

	for(auto &i : cl->playerint)
		if(cl->getPlayerRelations(i.first, player) != PlayerRelations::ENEMIES)
			i.second->tileRevealed(fowRevealed);

	//notify interfaces about move
	for(auto i=cl->playerint.begin(); i!=cl->playerint.end(); i++)
	{
		if(GS(cl)->isVisible(start - int3(1, 0, 0), i->first)
			|| GS(cl)->isVisible(end - int3(1, 0, 0), i->first))
		{
			i->second->heroMoved(*this);
		}
	}

	//maphandler didn't get update from playerint, do it now
	//TODO: restructure nicely
	if(!humanKnows && CGI->mh)
		CGI->mh->printObject(h);
}

bool TryMoveHero::stopMovement() const
{
	return result != SUCCESS && result != EMBARK && result != DISEMBARK && result != TELEPORTATION;
}

void NewStructures::applyCl(CClient *cl)
{
	CGTownInstance *town = GS(cl)->getTown(tid);
	for(const auto & id : bid)
	{
		if(vstd::contains(cl->playerint,town->tempOwner))
			cl->playerint[town->tempOwner]->buildChanged(town,id,1);
	}
}
void RazeStructures::applyCl (CClient *cl)
{
	CGTownInstance *town = GS(cl)->getTown(tid);
	for(const auto & id : bid)
	{
		if(vstd::contains (cl->playerint,town->tempOwner))
			cl->playerint[town->tempOwner]->buildChanged (town,id,2);
	}
}

void SetAvailableCreatures::applyCl(CClient *cl)
{
	const CGDwelling *dw = static_cast<const CGDwelling*>(cl->getObj(tid));

	//inform order about the change

	PlayerColor p;
	if(dw->ID == Obj::WAR_MACHINE_FACTORY) //War Machines Factory is not flaggable, it's "owned" by visitor
		p = cl->getTile(dw->visitablePos())->visitableObjects.back()->tempOwner;
	else
		p = dw->tempOwner;

	INTERFACE_CALL_IF_PRESENT(cl, p, &IGameEventsReceiver::availableCreaturesChanged, dw);
}

void SetHeroesInTown::applyCl(CClient *cl)
{
	CGTownInstance *t = GS(cl)->getTown(tid);
	CGHeroInstance *hGarr  = GS(cl)->getHero(this->garrison);
	CGHeroInstance *hVisit = GS(cl)->getHero(this->visiting);

	//inform all players that see this object
	for(auto i = cl->playerint.cbegin(); i != cl->playerint.cend(); ++i)
	{
		if(i->first >= PlayerColor::PLAYER_LIMIT)
			continue;

		if(GS(cl)->isVisible(t, i->first) ||
			(hGarr && GS(cl)->isVisible(hGarr, i->first)) ||
			(hVisit && GS(cl)->isVisible(hVisit, i->first)))
		{
			cl->playerint[i->first]->heroInGarrisonChange(t);
		}
	}
}

void HeroRecruited::applyCl(CClient *cl)
{
	CGHeroInstance *h = GS(cl)->map->heroesOnMap.back();
	if(h->subID != hid)
	{
		logNetwork->error("Something wrong with hero recruited!");
	}

	bool needsPrinting = true;
	if(vstd::contains(cl->playerint, h->tempOwner))
	{
		cl->playerint[h->tempOwner]->heroCreated(h);
		if(const CGTownInstance *t = GS(cl)->getTown(tid))
		{
			cl->playerint[h->tempOwner]->heroInGarrisonChange(t);
			needsPrinting = false;
		}
	}
	if(needsPrinting && CGI->mh)
		CGI->mh->printObject(h);
}

void GiveHero::applyCl(CClient *cl)
{
	CGHeroInstance *h = GS(cl)->getHero(id);
	if(CGI->mh)
		CGI->mh->printObject(h);
	cl->playerint[h->tempOwner]->heroCreated(h);
}

void GiveHero::applyFirstCl(CClient *cl)
{
	if(CGI->mh)
		CGI->mh->hideObject(GS(cl)->getHero(id));
}

void InfoWindow::applyCl(CClient *cl)
{
	std::vector<Component*> comps;
	for(auto & elem : components)
	{
		comps.push_back(&elem);
	}
	std::string str;
	text.toString(str);

	if(vstd::contains(cl->playerint,player))
		cl->playerint.at(player)->showInfoDialog(str,comps,(soundBase::soundID)soundID);
	else
		logNetwork->warn("We received InfoWindow for not our player...");
}

void SetObjectProperty::applyCl(CClient *cl)
{
	//inform all players that see this object
	for(auto it = cl->playerint.cbegin(); it != cl->playerint.cend(); ++it)
	{
		if(GS(cl)->isVisible(GS(cl)->getObjInstance(id), it->first))
			INTERFACE_CALL_IF_PRESENT(cl, it->first, &IGameEventsReceiver::objectPropertyChanged, this);
	}
}

void HeroLevelUp::applyCl(CClient *cl)
{
	//INTERFACE_CALL_IF_PRESENT(cl, h->tempOwner, heroGotLevel, h, primskill, skills, id);
	if(vstd::contains(cl->playerint,hero->tempOwner))
	{
		cl->playerint[hero->tempOwner]->heroGotLevel(hero, primskill, skills, queryID);
	}
	//else
	//	cb->selectionMade(0, queryID);
}

void CommanderLevelUp::applyCl(CClient *cl)
{
	const CCommanderInstance * commander = hero->commander;
	assert (commander);
	PlayerColor player = hero->tempOwner;
	if (commander->armyObj && vstd::contains(cl->playerint, player)) //is it possible for Commander to exist beyond armed instance?
	{
		cl->playerint[player]->commanderGotLevel(commander, skills, queryID);
	}
}

void BlockingDialog::applyCl(CClient *cl)
{
	std::string str;
	text.toString(str);

	if(vstd::contains(cl->playerint,player))
		cl->playerint.at(player)->showBlockingDialog(str,components,queryID,(soundBase::soundID)soundID,selection(),cancel());
	else
		logNetwork->warn("We received YesNoDialog for not our player...");
}

void GarrisonDialog::applyCl(CClient *cl)
{
	const CGHeroInstance *h = cl->getHero(hid);
	const CArmedInstance *obj = static_cast<const CArmedInstance*>(cl->getObj(objid));

	if(!vstd::contains(cl->playerint,h->getOwner()))
		return;

	cl->playerint.at(h->getOwner())->showGarrisonDialog(obj,h,removableUnits,queryID);
}

void ExchangeDialog::applyCl(CClient *cl)
{
	assert(heroes[0] && heroes[1]);
	INTERFACE_CALL_IF_PRESENT(cl, heroes[0]->tempOwner, &IGameEventsReceiver::heroExchangeStarted, heroes[0]->id, heroes[1]->id, queryID);
}

void TeleportDialog::applyCl(CClient *cl)
{
	CALL_ONLY_THAT_INTERFACE(cl, hero->tempOwner, &CGameInterface::showTeleportDialog, channel, exits, impassable, queryID);
}

void MapObjectSelectDialog::applyCl(CClient * cl)
{
	CALL_ONLY_THAT_INTERFACE(cl, player, &CGameInterface::showMapObjectSelectDialog, queryID, icon, title, description, objects);
}

void BattleStart::applyFirstCl(CClient *cl)
{
	//Cannot use the usual macro because curB is not set yet
	CALL_ONLY_THAT_BATTLE_INTERFACE(cl, info->sides[0].color, &IBattleEventsReceiver::battleStartBefore, info->sides[0].armyObject, info->sides[1].armyObject,
		info->tile, info->sides[0].hero, info->sides[1].hero);
	CALL_ONLY_THAT_BATTLE_INTERFACE(cl, info->sides[1].color, &IBattleEventsReceiver::battleStartBefore, info->sides[0].armyObject, info->sides[1].armyObject,
		info->tile, info->sides[0].hero, info->sides[1].hero);
	CALL_ONLY_THAT_BATTLE_INTERFACE(cl, PlayerColor::SPECTATOR, &IBattleEventsReceiver::battleStartBefore, info->sides[0].armyObject, info->sides[1].armyObject,
		info->tile, info->sides[0].hero, info->sides[1].hero);
	BATTLE_INTERFACE_CALL_RECEIVERS(cl, &IBattleEventsReceiver::battleStartBefore, info->sides[0].armyObject, info->sides[1].armyObject,
		info->tile, info->sides[0].hero, info->sides[1].hero);
}

void BattleStart::applyCl(CClient *cl)
{
	cl->battleStarted(info);
}

void BattleNextRound::applyFirstCl(CClient *cl)
{
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleNewRoundFirst,round);
}

void BattleNextRound::applyCl(CClient *cl)
{
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleNewRound,round);
}

void BattleSetActiveStack::applyCl(CClient *cl)
{
	if(!askPlayerInterface)
		return;

	const CStack *activated = GS(cl)->curB->battleGetStackByID(stack);
	PlayerColor playerToCall; //player that will move activated stack
	if (activated->hasBonusOfType(Bonus::HYPNOTIZED))
	{
		playerToCall = (GS(cl)->curB->sides[0].color == activated->owner
			? GS(cl)->curB->sides[1].color
			: GS(cl)->curB->sides[0].color);
	}
	else
	{
		playerToCall = activated->owner;
	}

	cl->startPlayerBattleAction(playerToCall);
}

void BattleTriggerEffect::applyCl(CClient * cl)
{
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleTriggerEffect, *this);
}

void BattleObstaclePlaced::applyCl(CClient * cl)
{
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleObstaclePlaced, *obstacle);
}

void BattleUpdateGateState::applyFirstCl(CClient * cl)
{
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleGateStateChanged, state);
}

void BattleResult::applyFirstCl(CClient *cl)
{
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleEnd,this);
	cl->battleFinished();
}

void BattleStackMoved::applyFirstCl(CClient *cl)
{
	const CStack * movedStack = GS(cl)->curB->battleGetStackByID(stack);
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleStackMoved,movedStack,tilesToMove,distance);
}

//void BattleStackAttacked::(CClient *cl)
void BattleStackAttacked::applyFirstCl(CClient *cl)
{
	std::vector<BattleStackAttacked> bsa;
	bsa.push_back(*this);

	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleStacksAttacked,bsa);
}

void BattleAttack::applyFirstCl(CClient *cl)
{
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleAttack,this);
	for (auto & elem : bsa)
	{
		for (int z=0; z<elem.healedStacks.size(); ++z)
		{
			elem.healedStacks[z].applyCl(cl);
		}
	}
}

void BattleAttack::applyCl(CClient *cl)
{
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleStacksAttacked,bsa);
}

void StartAction::applyFirstCl(CClient *cl)
{
	cl->curbaction = boost::make_optional(ba);
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::actionStarted, ba);
}

void BattleSpellCast::applyCl(CClient *cl)
{
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleSpellCast,this);
}

void SetStackEffect::applyCl(CClient *cl)
{
	//informing about effects
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleStacksEffectsSet,*this);
}

void StacksInjured::applyCl(CClient *cl)
{
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleStacksAttacked,stacks);
}

void BattleResultsApplied::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, player1, &IGameEventsReceiver::battleResultsApplied);
	INTERFACE_CALL_IF_PRESENT(cl, player2, &IGameEventsReceiver::battleResultsApplied);
	INTERFACE_CALL_IF_PRESENT(cl, PlayerColor::SPECTATOR, &IGameEventsReceiver::battleResultsApplied);
}

void StacksHealedOrResurrected::applyCl(CClient * cl)
{
	std::vector<std::pair<ui32, ui32> > shiftedHealed;
	for(auto & elem : healedStacks)
	{
		shiftedHealed.push_back(std::make_pair(elem.stackId, (ui32)elem.delta));
	}
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleStacksHealedRes, shiftedHealed, lifeDrain, tentHealing, drainedFrom);
}

void ObstaclesRemoved::applyCl(CClient *cl)
{
	//inform interfaces about removed obstacles
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleObstaclesRemoved, obstacles);
}

void CatapultAttack::applyCl(CClient *cl)
{
	//inform interfaces about catapult attack
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleCatapultAttacked, *this);
}

void BattleStacksRemoved::applyFirstCl(CClient * cl)
{
	//inform interfaces about removed stacks
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleStacksRemoved, *this);
}

void BattleStackAdded::applyCl(CClient *cl)
{
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::battleNewStackAppeared, GS(cl)->curB->stacks.back());
}

CGameState* CPackForClient::GS(CClient *cl)
{
	return cl->gs;
}

void EndAction::applyCl(CClient *cl)
{
	BATTLE_INTERFACE_CALL_IF_PRESENT_FOR_BOTH_SIDES(cl, &IBattleEventsReceiver::actionFinished, *cl->curbaction);
	cl->curbaction.reset();
}

void PackageApplied::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, player, &IGameEventsReceiver::requestRealized, this);
	if(!CClient::waitingRequest.tryRemovingElement(requestID))
		logNetwork->warn("Surprising server message! PackageApplied for unknown requestID!");
}

void SystemMessage::applyCl(CClient *cl)
{
	std::ostringstream str;
	str << "System message: " << text;

	logNetwork->error(str.str()); // usually used to receive error messages from server
	if(LOCPLINT && !settings["session"]["hideSystemMessages"].Bool())
		LOCPLINT->cingconsole->print(str.str());
}

void PlayerBlocked::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, player, &IGameEventsReceiver::playerBlocked,reason, startOrEnd==BLOCKADE_STARTED);
}

void YourTurn::applyCl(CClient *cl)
{
	logNetwork->debug("Server gives turn to %s", player.getStr());

	CALL_IN_ALL_INTERFACES(cl, &IGameEventsReceiver::playerStartsTurn, player);
	CALL_ONLY_THAT_INTERFACE(cl, player, &CGameInterface::yourTurn);
}

void SaveGameClient::applyCl(CClient *cl)
{
	const auto stem = FileInfo::GetPathStem(fname);
	CResourceHandler::get("local")->createResource(stem.to_string() + ".vcgm1");

	try
	{
		CSaveFile save(*CResourceHandler::get()->getResourceName(ResourceID(stem.to_string(), EResType::CLIENT_SAVEGAME)));
		cl->saveCommonState(save);
		save << *cl;
	}
	catch(std::exception &e)
	{
		logNetwork->error("Failed to save game:%s", e.what());
	}
}

void PlayerMessageClient::applyCl(CClient *cl)
{
	logNetwork->debug("Player %s sends a message: %s", player.getStr(), text);

	std::ostringstream str;
	if(player.isSpectator())
		str << "Spectator: " << text;
	else
		str << cl->getPlayer(player)->nodeName() <<": " << text;
	if(LOCPLINT)
		LOCPLINT->cingconsole->print(str.str());
}

void ShowInInfobox::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, player, &IGameEventsReceiver::showComp, c, text.toString());
}

void AdvmapSpellCast::applyCl(CClient *cl)
{
	cl->invalidatePaths();
	//consider notifying other interfaces that see hero?
	INTERFACE_CALL_IF_PRESENT(cl, caster->getOwner(), &IGameEventsReceiver::advmapSpellCast, caster, spellID);
}

void ShowWorldViewEx::applyCl(CClient * cl)
{
	CALL_ONLY_THAT_INTERFACE(cl, player, &CGameInterface::showWorldViewEx, objectPositions);
}

void OpenWindow::applyCl(CClient *cl)
{
	switch(window)
	{
	case RECRUITMENT_FIRST:
	case RECRUITMENT_ALL:
		{
			const CGDwelling *dw = dynamic_cast<const CGDwelling*>(cl->getObj(ObjectInstanceID(id1)));
			const CArmedInstance *dst = dynamic_cast<const CArmedInstance*>(cl->getObj(ObjectInstanceID(id2)));
			INTERFACE_CALL_IF_PRESENT(cl, dst->tempOwner,&IGameEventsReceiver::showRecruitmentDialog, dw, dst, window == RECRUITMENT_FIRST ? 0 : -1);
		}
		break;
	case SHIPYARD_WINDOW:
		{
			const IShipyard *sy = IShipyard::castFrom(cl->getObj(ObjectInstanceID(id1)));
			INTERFACE_CALL_IF_PRESENT(cl, sy->o->tempOwner, &IGameEventsReceiver::showShipyardDialog, sy);
		}
		break;
	case THIEVES_GUILD:
		{
			//displays Thieves' Guild window (when hero enters Den of Thieves)
			const CGObjectInstance *obj = cl->getObj(ObjectInstanceID(id2));
			INTERFACE_CALL_IF_PRESENT(cl, PlayerColor(id1), &IGameEventsReceiver::showThievesGuildWindow, obj);
		}
		break;
	case UNIVERSITY_WINDOW:
		{
			//displays University window (when hero enters University on adventure map)
			const IMarket *market = IMarket::castFrom(cl->getObj(ObjectInstanceID(id1)));
			const CGHeroInstance *hero = cl->getHero(ObjectInstanceID(id2));
			INTERFACE_CALL_IF_PRESENT(cl, hero->tempOwner,&IGameEventsReceiver::showUniversityWindow, market, hero);
		}
		break;
	case MARKET_WINDOW:
		{
			//displays Thieves' Guild window (when hero enters Den of Thieves)
			const CGObjectInstance *obj = cl->getObj(ObjectInstanceID(id1));
			const CGHeroInstance *hero = cl->getHero(ObjectInstanceID(id2));
			const IMarket *market = IMarket::castFrom(obj);
			INTERFACE_CALL_IF_PRESENT(cl, cl->getTile(obj->visitablePos())->visitableObjects.back()->tempOwner, &IGameEventsReceiver::showMarketWindow, market, hero);
		}
		break;
	case HILL_FORT_WINDOW:
		{
			//displays Hill fort window
			const CGObjectInstance *obj = cl->getObj(ObjectInstanceID(id1));
			const CGHeroInstance *hero = cl->getHero(ObjectInstanceID(id2));
			INTERFACE_CALL_IF_PRESENT(cl, cl->getTile(obj->visitablePos())->visitableObjects.back()->tempOwner, &IGameEventsReceiver::showHillFortWindow, obj, hero);
		}
		break;
	case PUZZLE_MAP:
		{
			INTERFACE_CALL_IF_PRESENT(cl, PlayerColor(id1), &IGameEventsReceiver::showPuzzleMap);
		}
		break;
	case TAVERN_WINDOW:
		const CGObjectInstance *obj1 = cl->getObj(ObjectInstanceID(id1)),
								*obj2 = cl->getObj(ObjectInstanceID(id2));
		INTERFACE_CALL_IF_PRESENT(cl, obj1->tempOwner, &IGameEventsReceiver::showTavernWindow, obj2);
		break;
	}

}

void CenterView::applyCl(CClient *cl)
{
	INTERFACE_CALL_IF_PRESENT(cl, player, &IGameEventsReceiver::centerView, pos, focusTime);
}

void NewObject::applyCl(CClient *cl)
{
	cl->invalidatePaths();

	const CGObjectInstance *obj = cl->getObj(id);
	if(CGI->mh)
		CGI->mh->printObject(obj, true);

	for(auto i=cl->playerint.begin(); i!=cl->playerint.end(); i++)
	{
		if(GS(cl)->isVisible(obj, i->first))
			i->second->newObject(obj);
	}
}

void SetAvailableArtifacts::applyCl(CClient *cl)
{
	if(id < 0) //artifact merchants globally
	{
		for(auto & elem : cl->playerint)
			elem.second->availableArtifactsChanged(nullptr);
	}
	else
	{
		const CGBlackMarket *bm = dynamic_cast<const CGBlackMarket *>(cl->getObj(ObjectInstanceID(id)));
		assert(bm);
		INTERFACE_CALL_IF_PRESENT(cl, cl->getTile(bm->visitablePos())->visitableObjects.back()->tempOwner, &IGameEventsReceiver::availableArtifactsChanged, bm);
	}
}
