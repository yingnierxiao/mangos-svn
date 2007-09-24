/* 
 * Copyright (C) 2005,2006,2007 MaNGOS <http://www.mangosproject.org/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Common.h"
#include "Player.h"
#include "BattleGroundMgr.h"
#include "BattleGroundAV.h"
#include "BattleGroundAB.h"
#include "BattleGroundEY.h"
#include "BattleGroundWS.h"
#include "BattleGroundNA.h"
#include "BattleGroundBE.h"
#include "BattleGroundAA.h"
#include "BattleGroundRL.h"
#include "SharedDefines.h"
#include "Policies/SingletonImp.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "ProgressBar.h"
#include "World.h"

INSTANTIATE_SINGLETON_1( BattleGroundMgr );

BattleGroundMgr::BattleGroundMgr()
{
    m_BattleGrounds.clear();
}

BattleGroundMgr::~BattleGroundMgr()
{
    for(std::map<uint32, BattleGround*>::iterator itr = m_BattleGrounds.begin(); itr != m_BattleGrounds.end(); ++itr)
        delete itr->second;
    m_BattleGrounds.clear();
}

void BattleGroundMgr::Update(time_t diff)
{
    for(BattleGroundSet::iterator itr = m_BattleGrounds.begin(); itr != m_BattleGrounds.end(); ++itr)
        itr->second->Update(diff);
}

void BattleGroundMgr::BuildBattleGroundStatusPacket(WorldPacket *data, BattleGround *bg, uint32 team, uint8 StatusID, uint32 Time1, uint32 Time2)
{
    // we can be in 3 queues in same time...
    if(StatusID == 0)
    {
        data->Initialize(SMSG_BATTLEFIELD_STATUS, 4*3);
        *data << uint32(0);                                 // queue id (0...2)
        *data << uint32(0);
        *data << uint32(0);
        return;
    }

    data->Initialize(SMSG_BATTLEFIELD_STATUS, (4+1+1+4+2+4+1+4+4+4));
    *data << uint32(0x0);                                   // queue id (0...2)
    *data << uint8(bg->GetArenaType());                     // team type (0=BG, 2=2x2, 3=3x3, 5=5x5), for arenas
    switch(bg->GetID())                                     // value depends on bg id
    {
        case BATTLEGROUND_AV:
            *data << uint8(0);
            break;
        case BATTLEGROUND_WS:
            *data << uint8(2);
            break;
        case BATTLEGROUND_AB:
            *data << uint8(1);
            break;
        case BATTLEGROUND_NA:
        case BATTLEGROUND_BE:
        case BATTLEGROUND_AA:
        case BATTLEGROUND_RL:
            *data << uint8(5);
            break;
        default:                                            // unknown
            *data << uint8(0);
            break;
    }

    if(bg->isArena() && (StatusID == STATUS_WAIT_QUEUE))
        *data << uint32(BATTLEGROUND_AA);                   // all arenas
    else
        *data << uint32(bg->GetID());                       // BG id from DBC

    *data << uint16(0x1F90);                                // unk value 8080
    *data << uint32(bg->GetInstanceID());                   // instance id

    if(bg->isBattleGround())
        *data << uint8(bg->GetTeamIndexByTeamId(team));     // team
    else
        *data << uint8(bg->isRated());                      // is rated battle

    *data << uint32(StatusID);                              // status
    switch(StatusID)
    {
        case STATUS_WAIT_QUEUE:                             // status_in_queue
            *data << uint32(Time1);                         // wait time, milliseconds
            *data << uint32(Time2);                         // time in queue, updated every minute?
            break;
        case STATUS_WAIT_JOIN:                              // status_invite
            *data << uint32(bg->GetMapId());                // map id
            *data << uint32(Time1);                         // time to remove from queue, milliseconds
            break;
        case STATUS_IN_PROGRESS:                            // status_in_progress
            *data << uint32(bg->GetMapId());                // map id
            *data << uint32(Time1);                         // 0 at bg start, 120000 after bg end, time to bg auto leave, milliseconds
            *data << uint32(Time2);                         // time from bg start, milliseconds
            *data << uint8(0x1);                            // unk
            break;
        default:
            sLog.outError("Unknown BG status!");
            break;
    }
}

void BattleGroundMgr::BuildPvpLogDataPacket(WorldPacket *data, BattleGround *bg)
{
    uint8 type = (bg->isArena() ? 1 : 0);
                                                            // checked on 2.1.3
    data->Initialize(MSG_PVP_LOG_DATA, (1+1+4+40*bg->GetPlayerScoresSize()));
    *data << uint8(type);                                   // seems to be type (battleground=0/arena=1)
    if(type == 1)                                           // arena
    {
        for(uint8 i = 0; i < 2; i++)
        {
            *data << uint32(3000+1+i);                      // rating change: showed value - 3000
            *data << uint8(0);                              // string
        }
    }

    if(bg->GetWinner() < 2)                                 // we have winner
    {
        *data << uint8(1);                                  // bg ended
        *data << uint8(bg->GetWinner());                    // who win
    }
    else                                                    // no winner yet
    {
        *data << uint8(0);                                  // bg in progress
    }

    *data << uint32(bg->GetPlayerScoresSize());

    for(std::map<uint64, BattleGroundScore>::const_iterator itr = bg->GetPlayerScoresBegin(); itr != bg->GetPlayerScoresEnd(); ++itr)
    {
        *data << (uint64)itr->first;
        *data << (uint32)itr->second.KillingBlows;
        if(type == 0)
        {
            *data << (uint32)itr->second.BonusHonor;
            *data << (uint32)itr->second.HonorableKills;
            *data << (uint32)itr->second.Deaths;
        }
        else
        {
            // that part probably wrong
            Player *plr = objmgr.GetPlayer(itr->first);
            if(plr)
            {
                if(plr->GetTeam() == HORDE)
                    *data << uint8(0);
                else if(plr->GetTeam() == ALLIANCE)
                    *data << uint8(1);
                else
                    *data << uint8(0);
            }
            else
                *data << uint8(0);
        }
        *data << (uint32)itr->second.HealingDone;
        *data << (uint32)itr->second.DamageDone;
        switch(bg->GetID())                                 // battleground specific things
        {
            case BATTLEGROUND_AV:
                *data << (uint32)0x00000005;                // count of next fields
                *data << (uint32)itr->second.FlagCaptures;  // unk
                *data << (uint32)itr->second.FlagReturns;   // unk
                *data << (uint32)itr->second.FlagCaptures;  // unk
                *data << (uint32)itr->second.FlagReturns;   // unk
                *data << (uint32)itr->second.FlagCaptures;  // unk
                break;
            case BATTLEGROUND_WS:
                *data << (uint32)0x00000002;                // count of next fields
                *data << (uint32)itr->second.FlagCaptures;  // flag captures
                *data << (uint32)itr->second.FlagReturns;   // flag returns
                break;
            case BATTLEGROUND_AB:
                *data << (uint32)0x00000002;                // count of next fields
                *data << (uint32)itr->second.FlagCaptures;  // unk
                *data << (uint32)itr->second.FlagReturns;   // unk
                break;
            case BATTLEGROUND_NA:
            case BATTLEGROUND_BE:
            case BATTLEGROUND_AA:
            case BATTLEGROUND_RL:
                *data << (uint32)0;                         // 0
                break;
            default:
                sLog.outDebug("Unhandled MSG_PVP_LOG_DATA for BG id %u", bg->GetID());
                *data << (uint32)0;
                break;
        }
    }
}

void BattleGroundMgr::BuildGroupJoinedBattlegroundPacket(WorldPacket *data, uint32 bgid)
{
    /*bgid is:
    0 - Your group has joined a battleground queue, but you are not iligible
    1 - Your group has joined the queue for AV
    2 - Your group has joined the queue for WSG
    3 - Your group has joined the queue for AB
    4 - Your group has joined the queue for NA
    5 - Your group has joined the queue for BE Arena
    6 - Your group has joined the queue for All Arenas
    7 - Your group has joined the queue for EotS*/
    data->Initialize(SMSG_GROUP_JOINED_BATTLEGROUND, 4);
    *data << uint32(bgid);
}

void BattleGroundMgr::BuildUpdateWorldStatePacket(WorldPacket *data, uint32 field, uint32 value)
{
    data->Initialize(SMSG_UPDATE_WORLD_STATE, 4+4);
    *data << uint32(field);
    *data << uint32(value);
}

void BattleGroundMgr::BuildPlaySoundPacket(WorldPacket *data, uint32 soundid)
{
    data->Initialize(SMSG_PLAY_SOUND, 4);
    *data << uint32(soundid);
}

void BattleGroundMgr::BuildPlayerLeftBattleGroundPacket(WorldPacket *data, Player *plr)
{
    data->Initialize(SMSG_BATTLEGROUND_PLAYER_LEFT, 8);
    *data << uint64(plr->GetGUID());
}

void BattleGroundMgr::BuildPlayerJoinedBattleGroundPacket(WorldPacket *data, Player *plr)
{
    data->Initialize(SMSG_BATTLEGROUND_PLAYER_JOINED, 8);
    *data << uint64(plr->GetGUID());
}

uint32 BattleGroundMgr::CreateBattleGround(uint32 bg_ID, uint32 MaxPlayersPerTeam, uint32 LevelMin, uint32 LevelMax, char* BattleGroundName, uint32 MapID, float Team1StartLocX, float Team1StartLocY, float Team1StartLocZ, float Team1StartLocO, float Team2StartLocX, float Team2StartLocY, float Team2StartLocZ, float Team2StartLocO, uint8 type)
{
    // Create the BG
    BattleGround *bg = NULL;

    switch(bg_ID)
    {
        case BATTLEGROUND_AV: bg = new BattleGroundAV; break;
        case BATTLEGROUND_WS: bg = new BattleGroundWS; break;
        case BATTLEGROUND_AB: bg = new BattleGroundAB; break;
        case BATTLEGROUND_NA: bg = new BattleGroundNA; break;
        case BATTLEGROUND_BE: bg = new BattleGroundBE; break;
        case BATTLEGROUND_AA: bg = new BattleGroundAA; break;
        case BATTLEGROUND_EY: bg = new BattleGroundEY; break;
        case BATTLEGROUND_RL: bg = new BattleGroundRL; break;
        default:bg = new BattleGround;   break;             // placeholder for non implemented BG
    }

    bg->SetMapId(MapID);
    if(!bg->SetupBattleGround())
    {
        delete bg;
        return 0;
    }

    bg->SetBattleGroundType(type);

    if(bg->isArena())
        bg->SetArenaType(ARENA_TYPE_2v2);                   // 2x2
    else
        bg->SetArenaType(0);                                // battleground

    bg->SetID(bg_ID);
    bg->SetInstanceID(bg_ID);                               // temporary
    bg->SetMinPlayersPerTeam(MaxPlayersPerTeam/2);
    bg->SetMaxPlayersPerTeam(MaxPlayersPerTeam);
    bg->SetMinPlayers(MaxPlayersPerTeam);
    bg->SetMaxPlayers(MaxPlayersPerTeam*2);
    bg->SetName(BattleGroundName);
    bg->SetTeamStartLoc(ALLIANCE, Team1StartLocX, Team1StartLocY, Team1StartLocZ, Team1StartLocO);
    bg->SetTeamStartLoc(HORDE,    Team2StartLocX, Team2StartLocY, Team2StartLocZ, Team2StartLocO);
    bg->SetLevelRange(LevelMin, LevelMax);

    AddBattleGround(bg_ID, bg);
    //sLog.outDetail("BattleGroundMgr: Created new battleground: %u %s (Map %u, %u players per team, Levels %u-%u)", bg_ID, bg->m_Name, bg->m_MapId, bg->m_MaxPlayersPerTeam, bg->m_LevelMin, bg->m_LevelMax);
    return bg_ID;
}

void BattleGroundMgr::CreateInitialBattleGrounds()
{
    float AStartLoc[4];
    float HStartLoc[4];
    uint32 MaxPlayersPerTeam,MinLvl,MaxLvl,start1,start2;
    BattlemasterListEntry const *bl;
    WorldSafeLocsEntry const *start;

    uint32 count = 0;

    //                                            0     1                   2        3        4                  5                6               7
    QueryResult *result = sDatabase.Query("SELECT `id`, `MaxPlayersPerTeam`,`MinLvl`,`MaxLvl`,`AllianceStartLoc`,`AllianceStartO`,`HordeStartLoc`,`HordeStartO` FROM `battleground_template`");

    if(!result)
    {
        barGoLink bar(1);

        bar.step();

        sLog.outString();
        sLog.outErrorDb(">> Loaded 0 battlegrounds. DB table `battleground_template` is empty.");
        return;
    }

    barGoLink bar(result->GetRowCount());

    do
    {
        Field *fields = result->Fetch();
        bar.step();

        uint32 bg_ID = fields[0].GetUInt32();

        // can be overwrited by values from DB
        bl = sBattlemasterListStore.LookupEntry(bg_ID);
        if(!bl)
        {
            sLog.outError("Battleground ID %u not found in BattlemasterList.dbc. Battleground not created.",bg_ID);
            continue;
        }

        MaxPlayersPerTeam = bl->maxplayersperteam;
        MinLvl = bl->minlvl;
        MaxLvl = bl->maxlvl;

        if(fields[1].GetUInt32())
            MaxPlayersPerTeam = fields[1].GetUInt32();

        if(fields[2].GetUInt32())
            MinLvl = fields[2].GetUInt32();

        if(fields[3].GetUInt32())
            MaxLvl = fields[3].GetUInt32();

        start1 = fields[4].GetUInt32();

        start = sWorldSafeLocsStore.LookupEntry(start1);
        if(start)
        {
            AStartLoc[0] = start->x;
            AStartLoc[1] = start->y;
            AStartLoc[2] = start->z;
            AStartLoc[3] = fields[5].GetFloat();
        }
        else if(bg_ID == BATTLEGROUND_AA)
        {
            AStartLoc[0] = 0;
            AStartLoc[1] = 0;
            AStartLoc[2] = 0;
            AStartLoc[3] = fields[5].GetFloat();
        }
        else
        {
            sLog.outErrorDb("Table `battleground_template` for id %u have non-existed WorldSafeLocs.dbc id %u in field `AllianceStartLoc`. BG not created.",bg_ID,start1);
            continue;
        }

        start2 = fields[6].GetUInt32();

        start = sWorldSafeLocsStore.LookupEntry(start2);
        if(start)
        {
            HStartLoc[0] = start->x;
            HStartLoc[1] = start->y;
            HStartLoc[2] = start->z;
            HStartLoc[3] = fields[7].GetFloat();
        }
        else if(bg_ID == BATTLEGROUND_AA)
        {
            HStartLoc[0] = 0;
            HStartLoc[1] = 0;
            HStartLoc[2] = 0;
            HStartLoc[3] = fields[7].GetFloat();
        }
        else
        {
            sLog.outErrorDb("Table `battleground_template` for id %u have non-existed WorldSafeLocs.dbc id %u in field `HordeStartLoc`. BG not created.",bg_ID,start2);
            continue;
        }

        //sLog.outDetail("Creating battleground %s, %u-%u", bl->name[sWorld.GetDBClang()], MinLvl, MaxLvl);
        if(!CreateBattleGround(bg_ID, MaxPlayersPerTeam, MinLvl, MaxLvl, bl->name[sWorld.GetDBClang()], bl->mapid[0], AStartLoc[0], AStartLoc[1], AStartLoc[2], AStartLoc[3], HStartLoc[0], HStartLoc[1], HStartLoc[2], HStartLoc[3], bl->type))
            continue;

        count++;
    } while (result->NextRow());

    delete result;

    sLog.outString();
    sLog.outString( ">> Loaded %u battlegrounds", count );
}

void BattleGroundMgr::BuildBattleGroundListPacket(WorldPacket *data, uint64 guid, Player* plr, uint32 bgId)
{
    uint32 PlayerLevel = 10;

    if(plr)
        PlayerLevel = plr->getLevel();

    data->Initialize(SMSG_BATTLEFIELD_LIST);
    *data << uint64(guid);                                  // battlemaster guid
    *data << uint32(bgId);                                  // battleground id
    if(bgId == BATTLEGROUND_AA)                             // arenas
    {
        *data << uint8(5);                                  // unk
        *data << uint32(0);                                 // unk
    }
    else                                                    // battleground
    {
        *data << uint8(0x00);                               // unk

        std::list<uint32> SendList;
        for(std::map<uint32, BattleGround*>::iterator itr = m_BattleGrounds.begin(); itr != m_BattleGrounds.end(); ++itr)
        {
            if(itr->second->GetID() == bgId && (PlayerLevel >= itr->second->GetMinLevel()) && (PlayerLevel <= itr->second->GetMaxLevel()))
            {
                SendList.push_back(itr->second->GetInstanceID());
            }
        }

        *data << uint32(SendList.size());                   // number of bg instances

        for(std::list<uint32>::iterator i = SendList.begin(); i != SendList.end(); ++i)
        {
            *data << uint32(*i);                            // bg instance id
        }
        SendList.clear();
    }
}

void BattleGroundMgr::SendToBattleGround(Player *pl, uint32 bgId)
{
    BattleGround *bg = GetBattleGround(bgId);
    if(bg)
    {
        uint32 mapid = bg->GetMapId();
        float x, y, z, O;
        bg->GetTeamStartLoc(pl->GetTeam(), x, y, z, O);

        sLog.outDetail("BATTLEGROUND: Sending %s to %f, %f, %f, %f", pl->GetName(), x, y, z, O);
        pl->TeleportTo(mapid, x, y, z, O);
    }
}