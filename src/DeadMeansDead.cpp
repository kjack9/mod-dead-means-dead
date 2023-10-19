/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Config.h"
#include "Chat.h"
#include <vector>
#include <boost/algorithm/string.hpp>

//
// Module-defined Classes and Enums
//
class DeadMeansDead_Options : public DataMap::Base
{
public:
    DeadMeansDead_Options() {}

    bool enable, announce;
    bool enableDungeons, enableRaids, enableWorld;

    float multiplierGlobal, multiplierDungeon, multiplierRaid, multiplierWorld;

    uint32 respawnTimeOriginalMin, respawnTimeAdjustedMin, respawnTimeAdjustedMax;
    
    bool filterKilledByPlayer;
    std::vector<uint32> filterAlwaysInstanceIDs, filterNeverInstanceIDs;
    std::vector<uint32> filterAlwaysCreatureIDs, filterNeverCreatureIDs;

    void LoadOptions()
    {
        enable = sConfigMgr->GetOption<bool>("DeadMeansDead.Enable", true);
        announce = sConfigMgr->GetOption<bool>("DeadMeansDead.Announce", true);
        
        enableDungeons = sConfigMgr->GetOption<bool>("DeadMeansDead.Enable.Dungeons", true);
        enableRaids = sConfigMgr->GetOption<bool>("DeadMeansDead.Enable.Raids", true);
        enableWorld = sConfigMgr->GetOption<bool>("DeadMeansDead.Enable.World", false);

        multiplierGlobal = sConfigMgr->GetOption<float>("DeadMeansDead.RespawnTime.Multiplier.Global", 1.0f);
        multiplierDungeon = sConfigMgr->GetOption<float>("DeadMeansDead.RespawnTime.Multiplier.Dungeons", 1.0f);
        multiplierRaid = sConfigMgr->GetOption<float>("DeadMeansDead.RespawnTime.Multiplier.Raids", 1.0f);
        multiplierWorld = sConfigMgr->GetOption<float>("DeadMeansDead.RespawnTime.Multiplier.World", 1.0f);

        respawnTimeOriginalMin = sConfigMgr->GetOption<uint32>("DeadMeansDead.RespawnTime.Original.Min", 300);
        respawnTimeAdjustedMin = sConfigMgr->GetOption<uint32>("DeadMeansDead.RespawnTime.Adjusted.Min", 300);
        respawnTimeAdjustedMax = sConfigMgr->GetOption<uint32>("DeadMeansDead.RespawnTime.Adjusted.Max", 86400);

        filterKilledByPlayer = sConfigMgr->GetOption<bool>("DeadMeansDead.Filter.KilledByPlayer", true);

        filterAlwaysInstanceIDs =
            _space_delim_str_to_uint32_list(sConfigMgr->GetOption<std::string>("DeadMeansDead.Filter.AlwaysAdjust.InstanceID", ""));
        filterNeverInstanceIDs = 
            _space_delim_str_to_uint32_list(sConfigMgr->GetOption<std::string>("DeadMeansDead.Filter.NeverAdjust.InstanceID", ""));
        
        filterAlwaysCreatureIDs = 
            _space_delim_str_to_uint32_list(sConfigMgr->GetOption<std::string>("DeadMeansDead.Filter.AlwaysAdjust.CreatureID", ""));
        filterNeverCreatureIDs = 
            _space_delim_str_to_uint32_list(sConfigMgr->GetOption<std::string>("DeadMeansDead.Filter.NeverAdjust.CreatureID", ""));
    }

private:
    std::vector<uint32> _space_delim_str_to_uint32_list(std::string str)
    {
        if (str.empty())
        {
            return std::vector<uint32>();
        }
        
        std::vector<uint32> list;
        std::vector<std::string> strList;
        boost::split(strList, str, boost::is_any_of(" "));

        for (std::string strValue : strList)
        {
            list.push_back(std::stoul(strValue));
        }

        return list;
    }
};
DeadMeansDead_Options options;

class DeadMeansDead_CreatureData : public DataMap::Base
{
public:
    DeadMeansDead_CreatureData() {}

    bool respawnDelayAltered = false;                    // Whether or not the respawn delay has been altered at least once
    uint32 respawnDelayOriginal = 0;                     // The original respawn delay
};

enum DeadMeansDead_MapType
{
    MAP_TYPE_UNKNOWN,
    MAP_TYPE_DUNGEON,
    MAP_TYPE_RAID,
    MAP_TYPE_BATTLEGROUND,
    MAP_TYPE_ARENA,
    MAP_TYPE_WORLD
};


//
// Helper Functions
//
bool is_uint32_in_list(uint32 value, std::vector<uint32> list)
{
    for (uint32 listValue : list)
    {
        if (value == listValue)
        {
            return true;
        }
    }

    return false;
}


//
// Script Hook classes
//
class DeadMeansDead_WorldScript : public WorldScript
{
public:
    DeadMeansDead_WorldScript() : WorldScript("DeadMeansDead_WorldScript") { }

    void OnBeforeConfigLoad(bool /* reload */) override
    {
        // load options
        options.LoadOptions();
    }
};

class DeadMeansDead_PlayerScript : public PlayerScript
{
public:
    DeadMeansDead_PlayerScript() : PlayerScript("DeadMeansDead_PlayerScript") { }

    void OnLogin(Player* player) override
    {
        if (options.enable && options.announce)
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
        if (!options.enable)
        {
            return;
        }

        // only adjust the creature if it passes checks
        if (_shouldUnitBeAdjusted(unit, killer))
        {
            _adjustCreature(unit->ToCreature());
        }

        return;
        
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

    bool _shouldUnitBeAdjusted(Unit* unit, Unit* killer)
    {
        // check to be sure this is a creature
        if (!unit || !unit->ToCreature())
        {
            return false;
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
        
        // LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_shouldUnitBeAdjusted: Creature {} (ID: {}, Spawn: {}) | killed by {} ({})",
        //     creature->GetName(),
        //     creature->GetEntry(),
        //     creature->GetSpawnId(),
        //     killer ? killer->GetName() : "Unknown",
        //     killerIdStr
        // );
        
        // check to be sure the creature is in a map
        if (!creature->GetMap())
        {
            return false;
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

        // never adjust if the creature's map is in the filterNeverInstanceIDs list
        if(is_uint32_in_list(map->GetId(), options.filterNeverInstanceIDs))
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_shouldUnitBeAdjusted: Creature {} (ID: {}, Spawn: {}) | is in map {} ({}), which is in the NEVER instance list. No changes.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                map->GetMapName(),
                map->GetId()
            );

            return false;
        }
        // always adjust if the creature's map is in the filterAlwaysInstanceIDs list
        else if(is_uint32_in_list(map->GetId(), options.filterAlwaysInstanceIDs))
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_shouldUnitBeAdjusted: Creature {} (ID: {}, Spawn: {}) | is in map {} ({}), which is in the ALWAYS instance list. ENABLED for adjustments.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                map->GetMapName(),
                map->GetId()
            );

            // continue to creature checks
        }
        // check to be sure the creature is in one of the area types we want to adjust
        else if 
        (
            (mapType == MAP_TYPE_RAID && options.enableRaids) ||
            (mapType == MAP_TYPE_DUNGEON && options.enableDungeons) ||
            (mapType == MAP_TYPE_WORLD && options.enableWorld)
        )
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_shouldUnitBeAdjusted: Creature {} (ID: {}, Spawn: {}) | is in an area ({}) of type ({}), which is ENABLED for adjustments.",
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

            return false;
        }

        // never adjust if the creature's ID is in the filterNeverCreatureIDs list
        if (is_uint32_in_list(creature->GetEntry(), options.filterNeverCreatureIDs))
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_shouldUnitBeAdjusted: Creature {} (ID: {}, Spawn: {}) | is in the NEVER creature list. No changes.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId()
            );

            // the rest of the checks don't matter, don't adjust
            return false;
        }
        // always adjust if the creature's ID is in the filterAlwaysCreatureIDs list
        else if (is_uint32_in_list(creature->GetEntry(), options.filterAlwaysCreatureIDs))
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_shouldUnitBeAdjusted: Creature {} (ID: {}, Spawn: {}) | is in the ALWAYS creature list. ENABLED for adjustments.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId()
            );

            // the rest of the checks don't matter, force adjust
            return true;
        }
        
        // check to be sure the creature was killed by a player (if enabled)
        if (options.filterKilledByPlayer && (!killer || !killer->ToPlayer()))
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_shouldUnitBeAdjusted: Creature {} (ID: {}, Spawn: {}) | was not killed by a player and the player filter is enabled. No changes.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId()
            );
            
            return false;
        }

        // check to be sure the creature's original respawn time is greater than the minimum
        if (creature->GetRespawnDelay() < options.respawnTimeOriginalMin)
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_shouldUnitBeAdjusted: Creature {} (ID: {}, Spawn: {}) | has an original respawn time ({}) less than the minimum ({}). No changes.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                creature->GetRespawnTime(),
                options.respawnTimeOriginalMin
            );

            return false;
        }
        // check to be sure the creature's original respawn time is less than the maximum
        else if (creature->GetRespawnDelay() > options.respawnTimeAdjustedMax)
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_shouldUnitBeAdjusted: Creature {} (ID: {}, Spawn: {}) | has an original respawn time ({}) greater than the maximum ({}). No changes.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                creature->GetRespawnTime(),
                options.respawnTimeAdjustedMax
            );

            return false;
        }

        // survived to here, so we should adjust the creature
        LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_shouldUnitBeAdjusted: Creature {} (ID: {}, Spawn: {}) | should be adjusted.",
            creature->GetName(),
            creature->GetEntry(),
            creature->GetSpawnId()
        );
        return true;
    }

    void _adjustCreature(Creature* creature)
    {
        Map* map = creature->GetMap();
        DeadMeansDead_MapType mapType = getMapType(map);
        std::string mapTypeString = 
            mapType == MAP_TYPE_DUNGEON ? "Dungeon" : 
            mapType == MAP_TYPE_RAID ? "Raid" : 
            mapType == MAP_TYPE_BATTLEGROUND ? "Battleground" : 
            mapType == MAP_TYPE_ARENA ? "Arena" : 
            mapType == MAP_TYPE_WORLD ? "World" : 
            "Unknown";

        // determine the original respawn time for this creature
        DeadMeansDead_CreatureData *creatureData = creature->CustomData.GetDefault<DeadMeansDead_CreatureData>("DeadMeansDead_CreatureData");
        uint32 originalRespawnDelay = 0;

        if (creatureData->respawnDelayAltered)
        {
            originalRespawnDelay = creatureData->respawnDelayOriginal;
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_adjustCreature: Creature {} (ID: {}, Spawn: {}) | respawn time already altered. Using original ({}).",
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
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_adjustCreature: Creature {} (ID: {}, Spawn: {}) | respawn time not altered. Using current ({}) and saving to creature.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                originalRespawnDelay
            );
        }
        
        // calculate the new respawn time
        uint32 newRespawnTime;
        float multiplier = 
            mapType == MAP_TYPE_DUNGEON ? options.multiplierDungeon :
            mapType == MAP_TYPE_RAID ? options.multiplierRaid :
            mapType == MAP_TYPE_WORLD ? options.multiplierWorld :
            1.0f;

        newRespawnTime = (uint32)((float)originalRespawnDelay * options.multiplierGlobal * multiplier);
        
        LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_adjustCreature: Creature {} (ID: {}, Spawn: {}) | newRespawnTime ({}) = originalRespawnDelay ({}) * multiplierGlobal ({}) * {} ({})",
            creature->GetName(),
            creature->GetEntry(),
            creature->GetSpawnId(),
            newRespawnTime,
            originalRespawnDelay,
            options.multiplierGlobal,
            mapTypeString,
            multiplier
        );

        // if the newRespawnTime is 0, disable respawning for this creature
        if (newRespawnTime == 0)
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_adjustCreature: Creature {} (ID: {}, Spawn: {}) | newRespawnTime ({}) is 0. Disabling respawn.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                newRespawnTime
            );

            newRespawnTime = 315360000; // 10 years
        }
        // check to be sure the new respawn time is greater than the minimum
        else if (newRespawnTime < options.respawnTimeAdjustedMin)
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_adjustCreature: Creature {} (ID: {}, Spawn: {}) | newRespawnTime ({}) is less than the minimum ({}). Adjusting to minimum.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                newRespawnTime,
                options.respawnTimeAdjustedMin
            );
        }
        // check to be sure the new respawn time is less than the maximum
        else if (newRespawnTime > options.respawnTimeAdjustedMax)
        {
            LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_adjustCreature: Creature {} (ID: {}, Spawn: {}) | newRespawnTime ({}) is greater than the maximum ({}). Adjusting to maximum.",
                creature->GetName(),
                creature->GetEntry(),
                creature->GetSpawnId(),
                newRespawnTime,
                options.respawnTimeAdjustedMax
            );
        }

        // actually adjust the respawn time
        LOG_DEBUG("module.DeadMeansDead", "DeadMeansDead_UnitScript::_adjustCreature: Creature {} (ID: {}, Spawn: {}) | respawn time adjusted from ({}) to ({}).",
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
};

void AddDeadMeansDeadScripts()
{
    new DeadMeansDead_WorldScript();
    new DeadMeansDead_PlayerScript();
    new DeadMeansDead_UnitScript();
}
