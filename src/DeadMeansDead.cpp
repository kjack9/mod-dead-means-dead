/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Config.h"
#include "Chat.h"

// Options
static bool enable, announce;
static bool enableDungeons, enableRaids, enableWorld;
static float multiplierGlobal, multiplierDungeon, multiplierRaid, multiplierWorld;
static uint32 respawnTimeOriginalMin, respawnTimeAdjustedMin, respawnTimeAdjustedMax;
static bool filterOnlyKilledByPlayers;

enum DeadMeansDead_MapType
{
    MAP_TYPE_UNKNOWN,
    MAP_TYPE_DUNGEON,
    MAP_TYPE_RAID,
    MAP_TYPE_BATTLEGROUND,
    MAP_TYPE_ARENA,
    MAP_TYPE_WORLD
};

class DeadMeansDead_CreatureData : public DataMap::Base
{
public:
    DeadMeansDead_CreatureData() {}

    bool respawnDelayAltered = false;                    // Whether or not the respawn delay has been altered at least once
    uint32 respawnDelayOriginal = 0;                     // The original respawn delay
};

class DeadMeansDead_WorldScript : public WorldScript
{
public:
    DeadMeansDead_WorldScript() : WorldScript("DeadMeansDead_WorldScript") { }

    void OnBeforeConfigLoad(bool /* reload */) override
    {
        // load options
        enable = sConfigMgr->GetOption<bool>("DeadMeansDead.Enable", true);
        announce = sConfigMgr->GetOption<bool>("DeadMeansDead.Announce", true);

        // enables
        enableDungeons = sConfigMgr->GetOption<bool>("DeadMeansDead.Enable.Dungeons", true);
        enableRaids = sConfigMgr->GetOption<bool>("DeadMeansDead.Enable.Raids", true);
        enableWorld = sConfigMgr->GetOption<bool>("DeadMeansDead.Enable.World", false);

        // multipliers
        multiplierGlobal = sConfigMgr->GetOption<float>("DeadMeansDead.RespawnTime.Multiplier.Global", 1.0f);
        multiplierDungeon = sConfigMgr->GetOption<float>("DeadMeansDead.RespawnTime.Multiplier.Dungeons", 1.0f);
        multiplierRaid = sConfigMgr->GetOption<float>("DeadMeansDead.RespawnTime.Multiplier.Raids", 1.0f);
        multiplierWorld = sConfigMgr->GetOption<float>("DeadMeansDead.RespawnTime.Multiplier.World", 1.0f);

        // min/max
        respawnTimeOriginalMin = sConfigMgr->GetOption<uint32>("DeadMeansDead.RespawnTime.Original.Min", 300);
        respawnTimeAdjustedMin = sConfigMgr->GetOption<uint32>("DeadMeansDead.RespawnTime.Adjusted.Min", 300);
        respawnTimeAdjustedMax = sConfigMgr->GetOption<uint32>("DeadMeansDead.RespawnTime.Adjusted.Max", 86400);

        // filter
        filterOnlyKilledByPlayers = sConfigMgr->GetOption<bool>("DeadMeansDead.Filter.OnlyKilledByPlayers", true);
    }
};

class DeadMeansDead_PlayerScript : public PlayerScript
{
public:
    DeadMeansDead_PlayerScript() : PlayerScript("DeadMeansDead_PlayerScript") { }

    void OnLogin(Player* player) override
    {
        if (enable && announce)
        {
            ChatHandler(player->GetSession()).PSendSysMessage("DeadMeansDead is enabled.");
        }
    }
};

class DeadMeansDead_UnitScript : public UnitScript
{
public:
    DeadMeansDead_UnitScript() : UnitScript("DeadMeansDead_UnitScript") { }

    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        // make sure we're enabled
        if (!enable)
        {
            return;
        }
        
        // check to be sure this is a creature
        if (!unit || !unit->ToCreature())
        {
            return;
        }

        Creature* creature = unit->ToCreature();

        // Construct the killer's ID
        std::string killerIdStr;
        if (killer->IsPlayer())
        {
            killerIdStr = "Player";
        }
        else if (killer->GetEntry())
        {
            killerIdStr = std::to_string(killer->GetEntry());
        }
        else
        {
            killerIdStr = "Unknown ID";
        }
        
        // LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | killed by {} ({})",
        //     creature->GetName(),
        //     creature->GetEntry(),
        //     creature->GetSpawnId(),
        //     killer ? killer->GetName() : "Unknown",
        //     killerIdStr
        // );
        
        // check to be sure the creature is in a map
        if (!creature->GetMap())
        {
            return;
        }

        Map* map = creature->GetMap();
        DeadMeansDead_MapType mapType = getMapType(map);
        std::string mapTypeString = 
            mapType == MAP_TYPE_DUNGEON ? "Dungeon" : 
            mapType == MAP_TYPE_RAID ? "Raid" : 
            mapType == MAP_TYPE_BATTLEGROUND ? "Battleground" : 
            mapType == MAP_TYPE_ARENA ? "Arena" : 
            mapType == MAP_TYPE_WORLD ? "World" : 
            "Unknown";

        // check to be sure the creature is in one of the area types we want to adjust
        if 
        (
            (mapType == MAP_TYPE_RAID && enableRaids) ||
            (mapType == MAP_TYPE_DUNGEON && enableDungeons) ||
            (mapType == MAP_TYPE_WORLD && enableWorld)
        )
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | is in an area ({}) of type ({}), which is ENABLED for adjustments.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                map->GetMapName(),
                mapTypeString
            );
        }
        else
        {
            // LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | is in an area ({}) of type ({}), which is DISABLED for adjustments. No changes.",
            //     creature->GetName(),
            //     creature->GetEntry(),
            //     creature->GetSpawnId(),
            //     map->GetMapName(),
            //     mapTypeString
            // );

            return;
        }

        // check to be sure the creature was killed by a player (if enabled)
        if (filterOnlyKilledByPlayers && (!killer || !killer->ToPlayer()))
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | was not killed by a player and the player filter is enabled. No changes.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId()
            );
            
            return;
        }

        // check to be sure the creature has a respawn time (delay)
        if (!creature->GetRespawnDelay())
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | has no respawn time. No changes.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId()
            );

            return;
        }

        // check to be sure the creature's original respawn time is greater than the minimum
        if (creature->GetRespawnDelay() < respawnTimeOriginalMin)
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | has an original respawn time ({}) less than the minimum ({}). No changes.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                creature->GetRespawnTime(),
                respawnTimeOriginalMin
            );

            return;
        }
        // check to be sure the creature's original respawn time is less than the maximum
        else if (creature->GetRespawnDelay() > respawnTimeAdjustedMax)
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | has an original respawn time ({}) greater than the maximum ({}). No changes.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                creature->GetRespawnTime(),
                respawnTimeAdjustedMax
            );

            return;
        }

        // determine the original respawn time for this creature
        DeadMeansDead_CreatureData *creatureData = creature->CustomData.GetDefault<DeadMeansDead_CreatureData>("DeadMeansDead_CreatureData");
        uint32 originalRespawnDelay = 0;

        if (creatureData->respawnDelayAltered)
        {
            originalRespawnDelay = creatureData->respawnDelayOriginal;
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | respawn time already altered. Using original ({}).",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                creatureData->respawnDelayOriginal
            );
        }
        else
        {
            creatureData->respawnDelayOriginal = creature->GetRespawnDelay();
            originalRespawnDelay = creature->GetRespawnDelay();
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | respawn time not altered. Using current ({}) and saving to creature.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                originalRespawnDelay
            );
        }
        
        // calculate the new respawn time
        uint32 newRespawnTime;
        float multiplier = 
            mapType == MAP_TYPE_DUNGEON ? multiplierDungeon :
            mapType == MAP_TYPE_RAID ? multiplierRaid :
            mapType == MAP_TYPE_WORLD ? multiplierWorld :
            1.0f;

        newRespawnTime = (uint32)((float)originalRespawnDelay * multiplierGlobal * multiplier);
        
        LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | newRespawnTime ({}) = originalRespawnDelay ({}) * multiplierGlobal ({}) * {} ({})",
            creature->GetName(),
            creature->GetEntry(),
            creature->GetSpawnId(),
            newRespawnTime,
            originalRespawnDelay,
            multiplierGlobal,
            mapTypeString,
            multiplier
        );

        // if the newRespawnTime is 0, disable respawning for this creature
        if (newRespawnTime == 0)
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | newRespawnTime ({}) is 0. Disabling respawn.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                newRespawnTime
            );

            newRespawnTime = 315360000; // 10 years
        }
        // check to be sure the new respawn time is greater than the minimum
        else if (newRespawnTime < respawnTimeAdjustedMin)
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | newRespawnTime ({}) is less than the minimum ({}). Adjusting to minimum.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                newRespawnTime,
                respawnTimeAdjustedMin
            );
        }
        // check to be sure the new respawn time is less than the maximum
        else if (newRespawnTime > respawnTimeAdjustedMax)
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | newRespawnTime ({}) is greater than the maximum ({}). Adjusting to maximum.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                newRespawnTime,
                respawnTimeAdjustedMax
            );
        }

        // actually adjust the respawn time
        LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::OnUnitDeath: Creature {} (ID: {}, Spawn: {}) | respawn time adjusted from ({}) to ({}).",
            creature->GetName(),
            creature->GetEntry(),
            creature->GetSpawnId(),
            originalRespawnDelay,
            newRespawnTime
        );

        creatureData->respawnDelayAltered = true;
        creature->SetRespawnDelay(newRespawnTime);
        creature->SetRespawnTime(newRespawnTime);
    }
private:
    DeadMeansDead_MapType getMapType(Map* map)
    {
        if (!map)
        {
            return MAP_TYPE_UNKNOWN;
        }
        else if (map->IsDungeon() && !map->IsRaid())
        {
            return MAP_TYPE_DUNGEON;
        }
        else if (map->IsRaid())
        {
            return MAP_TYPE_RAID;
        }
        else if (map->IsBattleground())
        {
            return MAP_TYPE_BATTLEGROUND;
        }
        else if (map->IsBattleArena())
        {
            return MAP_TYPE_ARENA;
        }
        else
        {
            return MAP_TYPE_WORLD;
        }
    }
};

void AddDeadMeansDeadScripts()
{
    new DeadMeansDead_WorldScript();
    new DeadMeansDead_PlayerScript();
    new DeadMeansDead_UnitScript();
}
