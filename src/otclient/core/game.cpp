/*
 * Copyright (c) 2010-2012 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "game.h"
#include "localplayer.h"
#include "map.h"
#include "tile.h"
#include "creature.h"
#include "statictext.h"
#include <framework/core/eventdispatcher.h>
#include <framework/ui/uimanager.h>
#include <otclient/luascript/luavaluecasts.h>
#include <otclient/net/protocolgame.h>

Game g_game;

void Game::loginWorld(const std::string& account, const std::string& password, const std::string& worldHost, int worldPort, const std::string& characterName)
{
    m_dead = false;
    m_protocolGame = ProtocolGamePtr(new ProtocolGame);
    m_protocolGame->login(account, password, worldHost, (uint16)worldPort, characterName);
}

void Game::cancelLogin()
{
    processLogout();
}

void Game::logout(bool force)
{
    if(!m_protocolGame || !isOnline())
        return;

    m_protocolGame->sendLogout();

    if(force)
        processLogout();
}

void Game::processLoginError(const std::string& error)
{
    g_lua.callGlobalField("Game", "onLoginError", error);
}

void Game::processConnectionError(const boost::system::error_code& error)
{
    // connection errors only have meaning if we still have a protocol
    if(m_protocolGame) {
        if(error != asio::error::eof)
            g_lua.callGlobalField("Game", "onConnectionError", error.message());

        processLogout();
    }
}

void Game::processGameStart(const LocalPlayerPtr& localPlayer, int serverBeat)
{
    m_localPlayer = localPlayer;
    m_serverBeat = serverBeat;

    // synchronize fight modes with the server
    m_fightMode = Otc::FightBalanced;
    m_chaseMode = Otc::DontChase;
    m_safeFight = true;
    m_protocolGame->sendFightTatics(m_fightMode, m_chaseMode, m_safeFight);

    // NOTE: the entire map description and local player informations is not known yet
    g_lua.callGlobalField("Game", "onGameStart");
}

void Game::processLogin()
{
    if(!isOnline())
        return;

    g_lua.callGlobalField("Game", "onLogin", m_localPlayer);
}

void Game::processLogout()
{
    if(isOnline()) {
        g_lua.callGlobalField("Game", "onLogout", m_localPlayer);

        m_localPlayer = nullptr;

        g_lua.callGlobalField("Game", "onGameEnd");
    }

    if(m_protocolGame) {
        m_protocolGame->disconnect();
        m_protocolGame = nullptr;
    }

    //g_map.save();
}

void Game::processDeath()
{
    m_dead = true;
    m_localPlayer->stopWalk();
    g_lua.callGlobalField("Game","onDeath");
}

void Game::processPlayerStats(double health, double maxHealth,
                              double freeCapacity, double experience,
                              double level, double levelPercent,
                              double mana, double maxMana,
                              double magicLevel, double magicLevelPercent,
                              double soul, double stamina)
{
    if(m_localPlayer->getHealth() != health ||
       m_localPlayer->getMaxHealth() != maxHealth) {
        m_localPlayer->setStatistic(Otc::Health, health);
        m_localPlayer->setStatistic(Otc::MaxHealth, maxHealth);
        g_lua.callGlobalField("Game", "onHealthChange", health, maxHealth);

        // cannot walk while dying
        if(health == 0) {
            if(m_localPlayer->isPreWalking())
                m_localPlayer->stopWalk();
            m_localPlayer->lockWalk();
        }
    }

    if(m_localPlayer->getStatistic(Otc::FreeCapacity) != freeCapacity) {
        m_localPlayer->setStatistic(Otc::FreeCapacity, freeCapacity);
        g_lua.callGlobalField("Game", "onFreeCapacityChange", freeCapacity);
    }

    if(m_localPlayer->getStatistic(Otc::Experience) != experience) {
        m_localPlayer->setStatistic(Otc::Experience, experience);
        g_lua.callGlobalField("Game", "onExperienceChange", experience);
    }

    if(m_localPlayer->getStatistic(Otc::Level) != level ||
       m_localPlayer->getStatistic(Otc::LevelPercent) != levelPercent) {
        m_localPlayer->setStatistic(Otc::Level, level);
        m_localPlayer->setStatistic(Otc::LevelPercent, levelPercent);
        g_lua.callGlobalField("Game", "onLevelChange", level, levelPercent);
    }

    if(m_localPlayer->getStatistic(Otc::Mana) != mana ||
       m_localPlayer->getStatistic(Otc::MaxMana) != maxMana) {
        m_localPlayer->setStatistic(Otc::Mana, mana);
        m_localPlayer->setStatistic(Otc::MaxMana, maxMana);
        g_lua.callGlobalField("Game", "onManaChange", mana, maxMana);
    }

    if(m_localPlayer->getStatistic(Otc::MagicLevel) != magicLevel ||
       m_localPlayer->getStatistic(Otc::MagicLevelPercent) != magicLevelPercent) {
        m_localPlayer->setStatistic(Otc::MagicLevel, magicLevel);
        m_localPlayer->setStatistic(Otc::MagicLevelPercent, magicLevelPercent);
        g_lua.callGlobalField("Game", "onMagicLevelChange", magicLevel, magicLevelPercent);
    }

    if(m_localPlayer->getStatistic(Otc::Soul) != soul) {
        m_localPlayer->setStatistic(Otc::Soul, soul);
        g_lua.callGlobalField("Game", "onSoulChange", soul);
    }

    if(m_localPlayer->getStatistic(Otc::Stamina) != stamina) {
        m_localPlayer->setStatistic(Otc::Stamina, stamina);
        g_lua.callGlobalField("Game", "onStaminaChange", stamina);
    }
}

void Game::processTextMessage(const std::string& type, const std::string& message)
{
    g_lua.callGlobalField("Game","onTextMessage", type, message);
}

void Game::processCreatureSpeak(const std::string& name, int level, Otc::SpeakType type, const std::string& message, int channelId, const Position& creaturePos)
{
    if(creaturePos.isValid() && (type == Otc::SpeakSay || type == Otc::SpeakWhisper || type == Otc::SpeakYell || type == Otc::SpeakMonsterSay || type == Otc::SpeakMonsterYell)) {
        StaticTextPtr staticText = StaticTextPtr(new StaticText);
        staticText->addMessage(name, type, message);
        g_map.addThing(staticText, creaturePos);
    }

    g_lua.callGlobalField("Game", "onCreatureSpeak", name, level, type, message, channelId, creaturePos);
}

void Game::processContainerAddItem(int containerId, const ItemPtr& item)
{
    if(item)
        item->setPosition(Position(65535, containerId + 0x40, 0));

    g_lua.callGlobalField("Game", "onContainerAddItem", containerId, item);
}

void Game::processInventoryChange(int slot, const ItemPtr& item)
{
    if(item)
        item->setPosition(Position(65535, slot, 0));

    g_lua.callGlobalField("Game","onInventoryChange", slot, item);
}

void Game::processCreatureMove(const CreaturePtr& creature, const Position& oldPos, const Position& newPos)
{
    // animate walk
    if(oldPos.isInRange(newPos, 1, 1))
        creature->walk(oldPos, newPos);
}

void Game::processCreatureTeleport(const CreaturePtr& creature)
{
    // stop walking on creature teleports
    creature->stopWalk();

    if(creature == m_localPlayer)
        m_localPlayer->lockWalk();
}

void Game::processAttackCancel()
{
    if(m_localPlayer->isAttacking())
        m_localPlayer->setAttackingCreature(nullptr);
}

void Game::processWalkCancel(Otc::Direction direction)
{
    m_localPlayer->cancelWalk(direction);
}

void Game::walk(Otc::Direction direction)
{
    if(!isOnline() || !m_localPlayer->isKnown() || isDead() || !checkBotProtection())
        return;

    if(m_localPlayer->isFollowing())
        cancelFollow();

    if(!m_localPlayer->canWalk(direction))
        return;

    // only do prewalks to walkable tiles
    TilePtr toTile = g_map.getTile(m_localPlayer->getPosition().translatedToDirection(direction));
    if(toTile && toTile->isWalkable())
        m_localPlayer->preWalk(direction);
    else
        m_localPlayer->lockWalk();

    forceWalk(direction);
}

void Game::forceWalk(Otc::Direction direction)
{
    if(!isOnline() || !checkBotProtection())
        return;

    switch(direction) {
    case Otc::North:
        m_protocolGame->sendWalkNorth();
        break;
    case Otc::East:
        m_protocolGame->sendWalkEast();
        break;
    case Otc::South:
        m_protocolGame->sendWalkSouth();
        break;
    case Otc::West:
        m_protocolGame->sendWalkWest();
        break;
    case Otc::NorthEast:
        m_protocolGame->sendWalkNorthEast();
        break;
    case Otc::SouthEast:
        m_protocolGame->sendWalkSouthEast();
        break;
    case Otc::SouthWest:
        m_protocolGame->sendWalkSouthWest();
        break;
    case Otc::NorthWest:
        m_protocolGame->sendWalkNorthWest();
        break;
    }
}

void Game::turn(Otc::Direction direction)
{
    if(!isOnline())
        return;

    switch(direction) {
    case Otc::North:
        m_protocolGame->sendTurnNorth();
        break;
    case Otc::East:
        m_protocolGame->sendTurnEast();
        break;
    case Otc::South:
        m_protocolGame->sendTurnSouth();
        break;
    case Otc::West:
        m_protocolGame->sendTurnWest();
        break;
    }
}

void Game::look(const ThingPtr& thing)
{
    if(!isOnline() || !thing || !checkBotProtection())
        return;

    m_protocolGame->sendLookAt(thing->getPosition(), thing->getId(), thing->getStackpos());
}

void Game::open(const ItemPtr& item, int containerId)
{
    if(!isOnline() || !item || !checkBotProtection())
        return;

    m_protocolGame->sendUseItem(item->getPosition(), item->getId(), item->getStackpos(), containerId);
}

void Game::use(const ThingPtr& thing)
{
    if(!isOnline() || !thing || !checkBotProtection())
        return;

    Position pos = thing->getPosition();
    if(!pos.isValid()) // virtual item
        pos = Position(0xFFFF, 0, 0); // means that is a item in inventory

    m_localPlayer->lockWalk();

    m_protocolGame->sendUseItem(pos, thing->getId(), thing->getStackpos(), 0);
}

void Game::useInventoryItem(int itemId)
{
    if(!isOnline() || !checkBotProtection())
        return;

    Position pos = Position(0xFFFF, 0, 0); // means that is a item in inventory
    m_localPlayer->lockWalk();

    m_protocolGame->sendUseItem(pos, itemId, 0, 0);
}

void Game::useWith(const ItemPtr& item, const ThingPtr& toThing)
{
    if(!isOnline() || !item || !toThing || !checkBotProtection())
        return;

    Position pos = item->getPosition();
    if(!pos.isValid()) // virtual item
        pos = Position(0xFFFF, 0, 0); // means that is a item in inventory

    m_localPlayer->lockWalk();

    if(CreaturePtr creature = toThing->asCreature())
        m_protocolGame->sendUseOnCreature(pos, item->getId(), item->getStackpos(), creature->getId());
    else
        m_protocolGame->sendUseItemEx(pos, item->getId(), item->getStackpos(), toThing->getPosition(), toThing->getId(), toThing->getStackpos());
}

void Game::useInventoryItemWith(int itemId, const ThingPtr& toThing)
{
    if(!isOnline() || !toThing || !checkBotProtection())
        return;

    m_localPlayer->lockWalk();

    Position pos = Position(0xFFFF, 0, 0); // means that is a item in inventory

    if(CreaturePtr creature = toThing->asCreature())
        m_protocolGame->sendUseOnCreature(pos, itemId, 0, creature->getId());
    else
        m_protocolGame->sendUseItemEx(pos, itemId, 0, toThing->getPosition(), toThing->getId(), toThing->getStackpos());
}

void Game::move(const ThingPtr& thing, const Position& toPos, int count)
{
    if(!isOnline() || !thing || !checkBotProtection() || thing->getPosition() == toPos || count <= 0)
        return;

    m_localPlayer->lockWalk();

    m_protocolGame->sendThrow(thing->getPosition(), thing->getId(), thing->getStackpos(), toPos, count);
}

void Game::setChaseMode(Otc::ChaseModes chaseMode)
{
    if(!isOnline() || !checkBotProtection())
        return;

    m_chaseMode = chaseMode;
    m_protocolGame->sendFightTatics(m_fightMode, m_chaseMode, m_safeFight);
}

void Game::setFightMode(Otc::FightModes fightMode)
{
    if(!isOnline() || !checkBotProtection())
        return;

    m_fightMode = fightMode;
    m_protocolGame->sendFightTatics(m_fightMode, m_chaseMode, m_safeFight);
}

void Game::setSafeFight(bool on)
{
    if(!isOnline() || !checkBotProtection())
        return;

    m_safeFight = on;
    m_protocolGame->sendFightTatics(m_fightMode, m_chaseMode, m_safeFight);
}

void Game::attack(const CreaturePtr& creature)
{
    if(!isOnline() || !creature || !checkBotProtection())
        return;

    m_localPlayer->lockWalk();

    if(m_localPlayer->isFollowing())
        cancelFollow();

    m_localPlayer->setAttackingCreature(creature);
    m_protocolGame->sendAttack(creature->getId());
}

void Game::cancelAttack()
{
    m_localPlayer->lockWalk();

    m_localPlayer->setAttackingCreature(nullptr);
    m_protocolGame->sendAttack(0);
}

void Game::follow(const CreaturePtr& creature)
{
    if(!isOnline() || !creature || !checkBotProtection())
        return;

    if(m_localPlayer->isAttacking())
        cancelAttack();

    m_localPlayer->setFollowingCreature(creature);
    m_protocolGame->sendFollow(creature->getId());
}

void Game::cancelFollow()
{
    if(!isOnline() || !checkBotProtection())
        return;

    m_localPlayer->setFollowingCreature(nullptr);
    m_protocolGame->sendFollow(0);
}

void Game::rotate(const ThingPtr& thing)
{
    if(!isOnline() || !thing || !checkBotProtection())
        return;

    m_protocolGame->sendRotateItem(thing->getPosition(), thing->getId(), thing->getStackpos());
}

void Game::talk(const std::string& message)
{
    talkChannel(Otc::SpeakSay, 0, message);
}

void Game::talkChannel(Otc::SpeakType speakType, int channelId, const std::string& message)
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendTalk(speakType, channelId, "", message);
}

void Game::talkPrivate(Otc::SpeakType speakType, const std::string& receiver, const std::string& message)
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendTalk(speakType, 0, receiver, message);
}

void Game::openPrivateChannel(const std::string& receiver)
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendOpenPrivateChannel(receiver);
}

void Game::requestChannels()
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendGetChannels();
}

void Game::joinChannel(int channelId)
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendJoinChannel(channelId);
}

void Game::leaveChannel(int channelId)
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendLeaveChannel(channelId);
}

void Game::closeNpcChannel()
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendCloseNpcChannel();
}


void Game::partyInvite(int creatureId)
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendInviteToParty(creatureId);
}

void Game::partyJoin(int creatureId)
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendJoinParty(creatureId);
}

void Game::partyRevokeInvitation(int creatureId)
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendRevokeInvitation(creatureId);
}


void Game::partyPassLeadership(int creatureId)
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendPassLeadership(creatureId);
}

void Game::partyLeave()
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendLeaveParty();
}

void Game::partyShareExperience(bool active)
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendShareExperience(active, 0);
}

void Game::requestOutfit()
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendGetOutfit();
}

void Game::setOutfit(const Outfit& outfit)
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendSetOutfit(outfit);
}

void Game::addVip(const std::string& name)
{
    if(!isOnline() || name.empty() || !checkBotProtection())
        return;
    m_protocolGame->sendAddVip(name);
}

void Game::removeVip(int playerId)
{
    if(!isOnline() || !checkBotProtection())
        return;
    m_protocolGame->sendRemoveVip(playerId);
}

bool Game::checkBotProtection()
{
#ifdef BOT_PROTECTION
    if(g_lua.isInCppCallback() && !g_ui.isOnInputEvent()) {
        logError("caught a lua call to a bot protected game function, the call was canceled");
        return false;
    }
#endif
    return true;
}
