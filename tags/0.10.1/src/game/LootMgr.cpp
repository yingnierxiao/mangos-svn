/* 
 * Copyright (C) 2005-2008 MaNGOS <http://www.mangosproject.org/>
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

#include "LootMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ProgressBar.h"
#include "World.h"

using std::remove_copy_if;

LootStore LootTemplates_Creature(     "creature_loot_template");
LootStore LootTemplates_Disenchant(   "disenchant_loot_template");
LootStore LootTemplates_Fishing(      "fishing_loot_template");
LootStore LootTemplates_Gameobject(   "gameobject_loot_template");
LootStore LootTemplates_Item(         "item_loot_template");
LootStore LootTemplates_Pickpocketing("pickpocketing_loot_template");
LootStore LootTemplates_Skinning(     "skinning_loot_template");
LootStore LootTemplates_Prospecting(  "prospecting_loot_template");

class LootTemplate::LootGroup                               // A set of loot definitions for items (refs are not allowed)
{
    public:
        void AddEntry(LootStoreItem& item);                 // Adds an entry to the group (at loading stage)
        bool HasQuestDrop() const;                          // True if group includes at least 1 quest drop entry
        bool HasQuestDropForPlayer(Player const * player) const;
                                                            // The same for active quests of the player
        void Process(Loot& loot) const;                     // Rolls an item from the group (if any) and adds the item to the loot
        float TotalChance() const;                          // Overall chance for the group

    private:
        LootStoreItemList ExplicitlyChanced;                // Entries with chances defined in DB
        LootStoreItemList EqualChanced;                     // Zero chances - every entry takes the same chance

        LootStoreItem const * Roll() const;                 // Rolls an item from the group, returns NULL if all miss their chances
};

// Storage for loot conditions. First element (index 0) is reserved for zero-condition (nothing required)
typedef std::vector<LootCondition> LootConditionStore;
static LootConditionStore LootConditions;

// Searches for the same condition already in LootConditions store
// Returns Id if found, else checks condition for validity, adds it to LootConditions and returns Id
// Wrong conditions are reported to DbErrors logfile and ignored
uint16 GetConditionId(LootConditionType condition, uint32 value1, uint32 value2)
{
    LootCondition lc = LootCondition(condition, value1, value2);
    for (uint16 i=0; i < LootConditions.size(); ++i)
    {
        if (lc == LootConditions[i])
            return i;
    }
    if ( lc.IsValid() )
    {
        LootConditions.push_back(lc);
        return LootConditions.size() - 1;
    }
    return 0;
}

//Remove all data and free all memory
void LootStore::Clear()
{
    for (LootTemplateMap::const_iterator itr=m_LootTemplates.begin(); itr != m_LootTemplates.end(); ++itr)
        delete itr->second;
    m_LootTemplates.clear();                         
}


// Checks validity of the loot store 
// Actual checks are done within LootTemplate::Verify() which is called for every template
void LootStore::Verify() const
{
    for (LootTemplateMap::const_iterator i = m_LootTemplates.begin(); i != m_LootTemplates.end(); ++i )
        i->second->Verify(m_LootTemplates, i->first);
}

// Loads a *_loot_template DB table into loot store
// All checks of the loaded template are called from here, no error reports at loot generation required
void LootStore::LoadLootTable()
{
    LootTemplateMap::iterator tab;
    uint32 count = 0;

    // Clearing store (for reloading case)
    Clear();                         

    sLog.outString( "%s :", GetName());

    //                                                 0      1     2                     3       4              5          6          7              8                 9
    QueryResult *result = WorldDatabase.PQuery("SELECT entry, item, ChanceOrQuestChance, `group`, mincountOrRef, maxcount, freeforall, lootcondition, condition_value1, condition_value2 FROM %s",GetName());

    if (result)
    {
        barGoLink bar(result->GetRowCount());

        do
        {
            Field *fields = result->Fetch();
            bar.step();

            uint32 entry               = fields[0].GetUInt32();
            uint32 item                = fields[1].GetUInt32();
            float  chanceOrQuestChance = fields[2].GetFloat();
            uint8  group               = fields[3].GetUInt8();
            int32  mincountOrRef       = fields[4].GetInt32();
            uint8  maxcount            = fields[5].GetUInt8();
            bool   freeforall          = fields[6].GetBool();
            LootConditionType condition= (LootConditionType)fields[7].GetUInt8();
            uint32 cond_value1         = fields[8].GetUInt32();
            uint32 cond_value2         = fields[9].GetUInt32();

            // (condition + cond_value1/2) are converted into single conditionId
            uint16 conditionId = GetConditionId(condition, cond_value1, cond_value2);

            LootStoreItem storeitem = LootStoreItem(item, chanceOrQuestChance, group, freeforall, conditionId, mincountOrRef, maxcount);

            if (!storeitem.IsValid(entry))                  // Validity checks
                continue;

            // Looking for the template of the entry
            if (m_LootTemplates.empty() || tab->first != entry)   // often entries are put together 
            {
                // Searching the template (in case template Id changed)
                tab = m_LootTemplates.find(entry);
                if ( tab == m_LootTemplates.end() )
                {
                    std::pair< LootTemplateMap::iterator, bool > pr = m_LootTemplates.insert(LootTemplateMap::value_type(entry, new LootTemplate));
                    tab = pr.first;
                }
            }
            // else is empty - template Id and iter are the same 
            // finally iter refers to already existed or just created <entry, LootTemplate>

            // Adds current row to the template
            tab->second->AddEntry(storeitem);
            ++count;

        } while (result->NextRow());

        delete result;

        Verify();                                           // Checks validity of the loot store 

        sLog.outString();
        sLog.outString( ">> Loaded %u loot definitions (%d templates)", count, m_LootTemplates.size());
    }
    else
    {
        sLog.outString();
        sLog.outErrorDb( ">> Loaded 0 loot definitions. DB table `%s` is empty.",GetName() );
    }
}

bool LootStore::HaveQuestLootFor(uint32 loot_id) const
{
    LootTemplateMap::const_iterator itr = m_LootTemplates.find(loot_id);
    if(itr == m_LootTemplates.end())
        return false;

    // scan loot for quest items
    return itr->second->HasQuestDrop(m_LootTemplates);
}

bool LootStore::HaveQuestLootForPlayer(uint32 loot_id,Player* player) const
{
    LootTemplateMap::const_iterator tab = m_LootTemplates.find(loot_id);
    if (tab != m_LootTemplates.end())
        if (tab->second->HasQuestDropForPlayer(m_LootTemplates, player))
            return true;

    return false;
}

LootTemplate const* LootStore::GetLootFor(uint32 loot_id) const
{
    LootTemplateMap::const_iterator tab = m_LootTemplates.find(loot_id);

    if (tab == m_LootTemplates.end())
        return NULL;

    return tab->second;
}

void LoadLootTables()
{
    // Only zero condition left, others will be added while loading loot tables
    LootConditions.resize(1);    

    LootTemplates_Creature.LoadLootTable();
    LootTemplates_Disenchant.LoadLootTable();
    LootTemplates_Fishing.LoadLootTable();
    LootTemplates_Gameobject.LoadLootTable();
    LootTemplates_Item.LoadLootTable();
    LootTemplates_Pickpocketing.LoadLootTable();
    LootTemplates_Skinning.LoadLootTable();
    LootTemplates_Prospecting.LoadLootTable();
}

//
// --------- LootStoreItem ---------
//

// Checks if the entry (quest, non-quest, reference) takes it's chance (at loot generation)
// RATE_DROP_ITEMS is used for all types of entries
bool LootStoreItem::Roll() const        
{ 
    return rand_chance() < chance*sWorld.getRate(RATE_DROP_ITEMS); 
}

// Checks correctness of values
bool LootStoreItem::IsValid(uint32 entry) const
{
    if (mincountOrRef == 0)
    {
        sLog.outErrorDb("Entry %d item %d: wrong mincountOrRef (%d) - skipped", entry, itemid, mincountOrRef);
        return false;
    }
 
    if( mincountOrRef > 0 )                                 // item (quest or non-quest) entry, maybe grouped
    {
        ItemPrototype const *proto = objmgr.GetItemPrototype(itemid);
        if(!proto)
        {
            sLog.outErrorDb("Entry %d item %d: wrong item id - skipped", entry, itemid);
            return false;
        }

        if( chance == 0 && group == 0)                      // Zero chance is allowed for grouped entries only
        {
            sLog.outErrorDb("Entry %d item %d: equal-chanced grouped entry, but group not defined - skipped", entry, itemid);
            return false;
        }

        if( chance != 0 && chance < 0.000001f )             // loot with low chance
        {
            sLog.outErrorDb("Entry %d item %d: low chance (%d) - skipped", entry, itemid, chance);
            return false;
        }
    }
    else                                                    // mincountOrRef < 0
    {
        if (needs_quest)
            sLog.outErrorDb("Entry %d item %d: quest chance will be treated as non-quest chance", entry, itemid);
        else if( chance == 0 )                              // no chance for the reference
        {
            sLog.outErrorDb("Entry %d item %d: zero chance is specified for a reference, skipped", entry, itemid);
            return false;
        }
    }
    return true;                                            // Referenced template existence is checked at whole store level
}

//
// --------- LootItem ---------
//

// Constructor, copies most fields from LootStoreItem and generates random count
LootItem::LootItem(LootStoreItem const& li)
{
    itemid      = li.itemid;
    conditionId = li.conditionId;
    freeforall  = li.freeforall;
    needs_quest = li.needs_quest;

    count       = urand(li.mincountOrRef, li.maxcount);     // constructor called for mincountOrRef > 0 only
    randomSuffix = GenerateEnchSuffixFactor(itemid);
    randomPropertyId = Item::GenerateItemRandomPropertyId(itemid);
    is_looted = 0;
    is_blocked = 0;
    is_underthreshold = 0;
    is_counted = 0;
}

// Basic checks for player/item compatibility - if false no chance to see the item in the loot
bool LootItem::AllowedForPlayer(Player const * player) const
{
    // DB conditions check
    if ( !LootConditions[conditionId].Meets(player) )
        return false;

    // Checking quests for quest drop
    if ( needs_quest && !player->HasQuestForItem(itemid) )
        return false;

    // Checking quest starting items for already accepted non-repeatable quests
    ItemPrototype const *pProto = objmgr.GetItemPrototype(itemid);
    if (pProto && pProto->StartQuest && player->GetQuestStatus(pProto->StartQuest) != QUEST_STATUS_NONE )
        return false;

    return true;
}

//
// --------- LootCondition ---------
//

// Checks if player meets the condition
bool LootCondition::Meets(Player const * player) const
{
    if( !player )
        return false;                                       // player not present, return false

    switch (condition)
    {
        case CONDITION_NONE:
            return true;                                    // empty condition, always met
        case CONDITION_AURA:
            return player->HasAura(value1, value2);
        case CONDITION_ITEM:
            return player->HasItemCount(value1, value2);
        case CONDITION_ITEM_EQUIPPED:
            return player->GetItemOrItemWithGemEquipped(value1) != NULL;
        case CONDITION_ZONEID:
            return player->GetZoneId() == value1;
        case CONDITION_REPUTATION_RANK:
        {
            FactionEntry const* faction = sFactionStore.LookupEntry(value1);
            return faction && player->GetReputationRank(faction) >= value2;
        }
        case CONDITION_TEAM:
            return player->GetTeam() == value1;
        case CONDITION_SKILL:
            return player->HasSkill(value1) && player->GetBaseSkillValue(value1) >= value2;
        case CONDITION_QUESTREWARDED:
            return player->GetQuestRewardStatus(value1);
        case CONDITION_QUESTTAKEN:
        {
            QuestStatus status = player->GetQuestStatus(value1);
            return (status == QUEST_STATUS_INCOMPLETE);
        }
        default:
            return false;
    }
}

// Verification of condition values validity
bool LootCondition::IsValid() const
{
    if( condition >= MAX_CONDITION)                         // Wrong condition type
    {
        sLog.outErrorDb("Condition has bad type of %u, skipped ", condition );
        return false;
    }

    switch (condition)
    {
        case CONDITION_AURA:
        {
            if(!sSpellStore.LookupEntry(value1))
            {
                sLog.outErrorDb("Aura condition requires to have non existing spell (Id: %d), skipped", value1);
                return false;
            }
            if(value2 > 2)
            {
                sLog.outErrorDb("Aura condition requires to have non existing effect index (%u) (must be 0..2), skipped", value2);
                return false;
            }
            break;
        }
        case CONDITION_ITEM:
        {
            ItemPrototype const *proto = objmgr.GetItemPrototype(value1);
            if(!proto)
            {
                sLog.outErrorDb("Item condition requires to have non existing item (%u), skipped", value1);
                return false;
            }
            break;
        }
        case CONDITION_ITEM_EQUIPPED:
        {
            ItemPrototype const *proto = objmgr.GetItemPrototype(value1);
            if(!proto)
            {
                sLog.outErrorDb("ItemEquipped condition requires to have non existing item (%u) equipped, skipped", value1);
                return false;
            }
            break;
        }
        case CONDITION_ZONEID:
        {
            AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(value1);
            if(!areaEntry)
            {
                sLog.outErrorDb("Zone condition requires to be in non existing area (%u), skipped", value1);
                return false;
            }
            if(areaEntry->zone != 0)
            {
                sLog.outErrorDb("Zone condition requires to be in area (%u) which is a subzone but zone expected, skipped", value1);
                return false;
            }
            break;
        }
        case CONDITION_REPUTATION_RANK:
        {
            FactionEntry const* factionEntry = sFactionStore.LookupEntry(value1);
            if(!factionEntry)
            {
                sLog.outErrorDb("Reputation condition requires to have reputation non existing faction (%u), skipped", value1);
                return false;
            }
            break;
        }
        case CONDITION_TEAM:
        {
            if (value1 != ALLIANCE && value1 != HORDE)
            {
                sLog.outErrorDb("Team condition specifies unknown team (%u), skipped", value1);
                return false;
            }
            break;
        }
        case CONDITION_SKILL:
        {
            SkillLineEntry const *pSkill = sSkillLineStore.LookupEntry(value1);
            if (!pSkill)
            {
                sLog.outErrorDb("Skill condition specifies non-existing skill (%u), skipped", value1);
                return false;
            }
            if (value2 < 1 || value2 > sWorld.GetConfigMaxSkillValue() )
            {
                sLog.outErrorDb("Skill condition specifies invalid skill value (%u), skipped", value2);
                return false;
            }
            break;
        }
        case CONDITION_QUESTREWARDED:
        case CONDITION_QUESTTAKEN:
        {
            Quest const *Quest = objmgr.GetQuestTemplate(value1);
            if (!Quest)
            {
                sLog.outErrorDb("Quest condition specifies non-existing quest (%u), skipped", value1);
                return false;
            }
            if(value2)
                sLog.outErrorDb("Quest condition has useless data in value2 (%u)!", value2);
            break;
        }
    }
    return true;
}

//
// --------- Loot ---------
//

// Inserts the item into the loot (called by LootTemplate processors)
void Loot::AddItem(LootStoreItem const & item)
{
    if (item.needs_quest)                                   // Quest drop
    {
        if (quest_items.size() < MAX_NR_QUEST_ITEMS)
            quest_items.push_back(LootItem(item));
    }
    else if (items.size() < MAX_NR_LOOT_ITEMS)              // Non-quest drop
    {
        items.push_back(LootItem(item));

        // non-conditional one-player only items are counted here,
        // free for all items are counted in FillFFALoot(),
        // non-ffa conditionals are counted in FillNonQuestNonFFAConditionalLoot()
        if( ! item.freeforall && ! item.conditionId)
            ++unlootedCount;
    }
}

// Calls processor of corresponding LootTemplate (which handles everything including references)
void Loot::FillLoot(uint32 loot_id, LootStore const& store, Player* loot_owner)
{
    LootTemplate const* tab = store.GetLootFor(loot_id);

    if (!tab)
    {
        sLog.outErrorDb("Loot id #%u used but it doesn't have records in '%s' table.",loot_id,store.GetName());
        return;
    }

    items.reserve(MAX_NR_LOOT_ITEMS);
    quest_items.reserve(MAX_NR_QUEST_ITEMS);

    tab->Process(*this, store);                             // Processing is done there, callback via Loot::AddItem()

    // Setting access rights fow group-looting case
    if(!loot_owner)
        return;
    Group * pGroup=loot_owner->GetGroup();
    if(!pGroup)
        return;
    for(GroupReference *itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
    {
        //fill the quest item map for every player in the recipient's group
        Player* pl = itr->getSource();
        if(!pl)
            continue;
        uint32 plguid = pl->GetGUIDLow();
        QuestItemMap::iterator qmapitr = PlayerQuestItems.find(plguid);
        if (qmapitr == PlayerQuestItems.end())
        {
            FillQuestLoot(pl, this);
        }
        qmapitr = PlayerFFAItems.find(plguid);
        if (qmapitr == PlayerFFAItems.end())
        {
            FillFFALoot(pl, this);
        }
        qmapitr = PlayerNonQuestNonFFAConditionalItems.find(plguid);
        if (qmapitr == PlayerNonQuestNonFFAConditionalItems.end())
        {
            FillNonQuestNonFFAConditionalLoot(pl, this);
        }
    }
}

QuestItemList* FillFFALoot(Player* player, Loot *loot)
{
    QuestItemList *ql = new QuestItemList();

    for(uint8 i = 0; i < loot->items.size(); i++)
    {
        LootItem &item = loot->items[i];
        if(!item.is_looted && item.freeforall && item.AllowedForPlayer(player) )
        {
            ql->push_back(QuestItem(i));
            ++loot->unlootedCount;
        }
    }
    if (ql->empty())
    {
        delete ql;
        return NULL;
    }

    loot->PlayerFFAItems[player->GetGUIDLow()] = ql;
    return ql;
}

QuestItemList* FillQuestLoot(Player* player, Loot *loot)
{
    if (loot->items.size() == MAX_NR_LOOT_ITEMS) return NULL;
    QuestItemList *ql = new QuestItemList();

    for(uint8 i = 0; i < loot->quest_items.size(); i++)
    {
        LootItem &item = loot->quest_items[i];
        if(!item.is_looted && item.AllowedForPlayer(player) )
        {
            ql->push_back(QuestItem(i));

            // questitems get blocked when they first apper in a
            // player's quest vector
            //
            // increase once if one looter only, looter-times if free for all
            if (item.freeforall || !item.is_blocked)
                ++loot->unlootedCount;

            item.is_blocked = true;

            if (loot->items.size() + ql->size() == MAX_NR_LOOT_ITEMS)
                break;
        }
    }
    if (ql->empty())
    {
        delete ql;
        return NULL;
    }

    loot->PlayerQuestItems[player->GetGUIDLow()] = ql;
    return ql;
}

QuestItemList* FillNonQuestNonFFAConditionalLoot(Player* player, Loot *loot)
{
    QuestItemList *ql = new QuestItemList();

    for(uint8 i = 0; i < loot->items.size(); i++)
    {
        LootItem &item = loot->items[i];
        if(!item.is_looted && !item.freeforall && item.conditionId && item.AllowedForPlayer(player))
        {
            ql->push_back(QuestItem(i));
            if(!item.is_counted)
            {
                ++loot->unlootedCount;
                item.is_counted=true;
            }
        }
    }
    if (ql->empty())
    {
        delete ql;
        return NULL;
    }

    loot->PlayerNonQuestNonFFAConditionalItems[player->GetGUIDLow()] = ql;
    return ql;
}

//===================================================

void Loot::NotifyItemRemoved(uint8 lootIndex)
{
    // notify all players that are looting this that the item was removed
    // convert the index to the slot the player sees
    std::set<uint64>::iterator i_next;
    for(std::set<uint64>::iterator i = PlayersLooting.begin(); i != PlayersLooting.end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        if(Player* pl = ObjectAccessor::FindPlayer(*i))
            pl->SendNotifyLootItemRemoved(lootIndex);
        else
            PlayersLooting.erase(i);
    }
}

void Loot::NotifyMoneyRemoved()
{
    // notify all players that are looting this that the money was removed
    std::set<uint64>::iterator i_next;
    for(std::set<uint64>::iterator i = PlayersLooting.begin(); i != PlayersLooting.end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        if(Player* pl = ObjectAccessor::FindPlayer(*i))
            pl->SendNotifyLootMoneyRemoved();
        else
            PlayersLooting.erase(i);
    }
}

void Loot::NotifyQuestItemRemoved(uint8 questIndex)
{
    // when a free for all questitem is looted
    // all players will get notified of it being removed
    // (other questitems can be looted by each group member)
    // bit inefficient but isnt called often

    std::set<uint64>::iterator i_next;
    for(std::set<uint64>::iterator i = PlayersLooting.begin(); i != PlayersLooting.end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        if(Player* pl = ObjectAccessor::FindPlayer(*i))
        {
            QuestItemMap::iterator pq = PlayerQuestItems.find(pl->GetGUIDLow());
            if (pq != PlayerQuestItems.end() && pq->second)
            {
                // find where/if the player has the given item in it's vector
                QuestItemList& pql = *pq->second;

                uint8 j;
                for (j = 0; j < pql.size(); ++j)
                    if (pql[j].index == questIndex)
                        break;

                if (j < pql.size())
                    pl->SendNotifyLootItemRemoved(items.size()+j);
            }
        }
        else
            PlayersLooting.erase(i);
    }
}

bool Loot::isLooted()
{
    return gold == 0 && unlootedCount == 0;
}

void Loot::generateMoneyLoot( uint32 minAmount, uint32 maxAmount )
{
    if (maxAmount > 0)
    {
        if (maxAmount <= minAmount)
            gold = uint32(maxAmount * sWorld.getRate(RATE_DROP_MONEY));
        else if ((maxAmount - minAmount) < 32700)
            gold = uint32(urand(minAmount, maxAmount) * sWorld.getRate(RATE_DROP_MONEY));
        else
            gold = uint32(urand(minAmount >> 8, maxAmount >> 8) * sWorld.getRate(RATE_DROP_MONEY)) << 8;
    }
}

ByteBuffer& operator<<(ByteBuffer& b, LootItem const& li)
{
    b << uint32(li.itemid);
    b << uint32(li.count);                                  // nr of items of this type
    b << uint32(objmgr.GetItemPrototype(li.itemid)->DisplayInfoID);
    b << uint32(li.randomSuffix);
    b << uint32(li.randomPropertyId);
    //b << uint8(0);                                        // slot type - will send after this function call
    return b;
}

ByteBuffer& operator<<(ByteBuffer& b, LootView const& lv)
{
    Loot &l = lv.loot;

    uint8 itemsShown = 0;

    //gold
    b << uint32(lv.permission!=NONE_PERMISSION ? l.gold : 0);

    size_t count_pos = b.wpos();                            // pos of item count byte
    b << uint8(0);                                          // item count placeholder

    switch (lv.permission)
    {
        case GROUP_PERMISSION:
        {
            // You are not the items proprietary, so you can only see
            // blocked rolled items and quest items, and !ffa items
            for (uint8 i = 0; i < l.items.size(); ++i)
            {
                if (!l.items[i].is_looted && !l.items[i].freeforall && !l.items[i].conditionId && l.items[i].AllowedForPlayer(lv.viewer))
                {
                    uint8 slot_type = (l.items[i].is_blocked || l.items[i].is_underthreshold) ? 0 : 1;

                    b << uint8(i) << l.items[i];            //send the index and the item if it's not looted, and blocked or under threshold, free for all items will be sent later, only one-player loots here
                    b << uint8(slot_type);                  // 0 - get 1 - look only
                    ++itemsShown;
                }
            }
            break;
        }
        case ALL_PERMISSION:
        case MASTER_PERMISSION:
        {
            uint8 slot_type = (lv.permission==MASTER_PERMISSION) ? 2 : 0;
            for (uint8 i = 0; i < l.items.size(); ++i)
            {
                if (!l.items[i].is_looted && !l.items[i].freeforall && !l.items[i].conditionId && l.items[i].AllowedForPlayer(lv.viewer))
                {
                    b << uint8(i) << l.items[i];            //only send one-player loot items now, free for all will be sent later
                    b << uint8(slot_type);                  // 0 - get 2 - master selection
                    ++itemsShown;
                }
            }
            break;
        }
        case NONE_PERMISSION:
        default:
            return b;                                       // nothing output more
    }

    if (lv.qlist)
    {
        for (QuestItemList::iterator qi = lv.qlist->begin() ; qi != lv.qlist->end(); ++qi)
        {
            LootItem &item = l.quest_items[qi->index];
            if (!qi->is_looted && !item.is_looted)
            {
                b << uint8(l.items.size() + (qi - lv.qlist->begin()));
                b << item;
                b << uint8(0);                              // allow loot
                ++itemsShown;
            }
        }
    }

    if (lv.ffalist)
    {
        for (QuestItemList::iterator fi = lv.ffalist->begin() ; fi != lv.ffalist->end(); ++fi)
        {
            LootItem &item = l.items[fi->index];
            if (!fi->is_looted && !item.is_looted)
            {
                b << uint8(fi->index) << item;
                b << uint8(0);                              // allow loot
                ++itemsShown;
            }
        }
    }

    if (lv.conditionallist)
    {
        for (QuestItemList::iterator ci = lv.conditionallist->begin() ; ci != lv.conditionallist->end(); ++ci)
        {
            LootItem &item = l.items[ci->index];
            if (!ci->is_looted && !item.is_looted)
            {
                b << uint8(ci->index) << item;
                b << uint8(0);                              // allow loot
                ++itemsShown;
            }
        }
    }

    //update number of items shown
    b.put<uint8>(count_pos,itemsShown);

    return b;
}

//
// --------- LootTemplate::LootGroup ---------
//

// Adds an entry to the group (at loading stage)
void LootTemplate::LootGroup::AddEntry(LootStoreItem& item)
{
    if (item.chance != 0)                             
        ExplicitlyChanced.push_back(item);
    else
        EqualChanced.push_back(item);
}

// Rolls an item from the group, returns NULL if all miss their chances
LootStoreItem const * LootTemplate::LootGroup::Roll() const
{
    if (ExplicitlyChanced.size() > 0)                           // First explicitly chanced entries are checked
    {
        float Roll = rand_chance();

        for (uint32 i=0; i<ExplicitlyChanced.size(); ++i)
        {
            Roll -= ExplicitlyChanced[i].chance;
            if (Roll < 0)
                return &ExplicitlyChanced[i];
        }
    }
    if (EqualChanced.size() > 0)                                // If nothing selected yet - an item is taken from equal-chanced part
        return &EqualChanced[irand(0, EqualChanced.size()-1)];

    return NULL;                                                // Empty drop from the group
}

// True if group includes at least 1 quest drop entry
bool LootTemplate::LootGroup::HasQuestDrop() const   
{
    for (LootStoreItemList::const_iterator i=ExplicitlyChanced.begin(); i != ExplicitlyChanced.end(); ++i)
        if (i->needs_quest)
            return true;
    for (LootStoreItemList::const_iterator i=EqualChanced.begin(); i != EqualChanced.end(); ++i)
        if (i->needs_quest)
            return true;
    return false;
}

// True if group includes at least 1 quest drop entry for active quests of the player
bool LootTemplate::LootGroup::HasQuestDropForPlayer(Player const * player) const
{
    for (LootStoreItemList::const_iterator i=ExplicitlyChanced.begin(); i != ExplicitlyChanced.end(); ++i)
        if (player->HasQuestForItem(i->itemid))
            return true;
    for (LootStoreItemList::const_iterator i=EqualChanced.begin(); i != EqualChanced.end(); ++i)
        if (player->HasQuestForItem(i->itemid))
            return true;
    return false;
}

// Rolls an item from the group (if any takes its chance) and adds the item to the loot
void LootTemplate::LootGroup::Process(Loot& loot) const
{
    LootStoreItem const * item = Roll();
    if (item != NULL)
        loot.AddItem(*item);
}

// Overall chance for the group
float LootTemplate::LootGroup::TotalChance() const
{
    float result = 0;
    
    for (LootStoreItemList::const_iterator i=ExplicitlyChanced.begin(); i != ExplicitlyChanced.end(); ++i)
        if ( !i->needs_quest )
            result += i->chance;

    if (EqualChanced.size() >  0 && result < 100.0f)
        return 100.0f;

    return result;
}

//
// --------- LootTemplate ---------
//

// Adds an entry to the group (at loading stage)
void LootTemplate::AddEntry(LootStoreItem& item)
{
    if (item.group > 0 && item.mincountOrRef > 0)           // Group
    {
        if (item.group >= Groups.size())
            Groups.resize(item.group);                      // Adds new group the the loot template if needed
        Groups[item.group-1].AddEntry(item);                // Adds new entry to the group
    }
    else                                                    // Non-grouped entries and references are stored together
        Entries.push_back(item);
}

// Rolls for every item in the template and adds the rolled items the the loot
void LootTemplate::Process(Loot& loot, LootStore const& store, uint8 groupId) const
{
    if (groupId)                                            // Group reference uses own processing of the group
    {
        if (groupId > Groups.size())   
            return;                                         // Error message already printed at loading stage

        Groups[groupId-1].Process(loot);
        return;
    }

    // Rolling non-grouped items
    for (LootStoreItemList::const_iterator i = Entries.begin() ; i != Entries.end() ; i++ )
    {
        if ( !i->Roll() )       
            continue;                                       // Bad luck for the entry

        if (i->mincountOrRef < 0)                           // References processing
        {
            LootTemplate const* Referenced = store.GetLootFor(-i->mincountOrRef);

            if(!Referenced)
                continue;                                   // Error message already printed at loading stage

            for (uint32 loop=0; loop < i->maxcount; ++loop )        // Ref multiplicator 
                Referenced->Process(loot, store, i->group); // Ref processing
        }
        else                                                // Plain entries (not a reference, not grouped)
            loot.AddItem(*i);                               // Chance is already checked, just add
    }

    // Now processing groups
    for (LootGroups::const_iterator i = Groups.begin( ) ; i != Groups.end( ) ; i++ )
        i->Process(loot);
}

// True if template includes at least 1 quest drop entry
bool LootTemplate::HasQuestDrop(LootTemplateMap const& store, uint8 groupId) const   
{
    if (groupId)                                            // Group reference
    {
        if (groupId > Groups.size())   
            return false;                                   // Error message [should be] already printed at loading stage
        return Groups[groupId-1].HasQuestDrop();
    }

    for (LootStoreItemList::const_iterator i = Entries.begin(); i != Entries.end(); ++i )
    {
        if (i->mincountOrRef < 0)                           // References
        {
            LootTemplateMap::const_iterator Referenced = store.find(-i->mincountOrRef);
            if( Referenced ==store.end() )
                continue;           // Error message [should be] already printed at loading stage
            if (Referenced->second->HasQuestDrop(store, i->group) )
                return true;
        }
        else if ( i->needs_quest )       
            return true;                  // quest drop found
    }

    // Now processing groups
    for (LootGroups::const_iterator i = Groups.begin() ; i != Groups.end() ; i++ )
        if (i->HasQuestDrop())
            return true;

    return false;
}

// True if template includes at least 1 quest drop for an active quest of the player
bool LootTemplate::HasQuestDropForPlayer(LootTemplateMap const& store, Player const* player, uint8 groupId) const
{
    if (groupId)                                            // Group reference
    {
        if (groupId > Groups.size())   
            return false;                                   // Error message already printed at loading stage
        return Groups[groupId-1].HasQuestDropForPlayer(player);
    }

    // Checking non-grouped entries
    for (LootStoreItemList::const_iterator i = Entries.begin() ; i != Entries.end() ; i++ )
    {
        if (i->mincountOrRef < 0)                           // References processing
        {
            LootTemplateMap::const_iterator Referenced = store.find(-i->mincountOrRef);
            if (Referenced == store.end() )
                continue;                                   // Error message already printed at loading stage
            if (Referenced->second->HasQuestDropForPlayer(store, player, i->group) )
                return true;
        }
        else if ( player->HasQuestForItem(i->itemid) )       
            return true;                                    // active quest drop found
    }

    // Now checking groups
    for (LootGroups::const_iterator i = Groups.begin(); i != Groups.end(); ++i )
        if (i->HasQuestDrop())
            return true;

    return false;
}

// Checks integrity of the template
void LootTemplate::Verify(LootTemplateMap const& lootstore, uint32 id) const
{
    // Checking group chances
    for (uint32 i=0; i < Groups.size(); ++i)
    {
        float chance = Groups[i].TotalChance();
        if (chance > 101.0f)                                // TODO: replace with 100% when DBs will be ready
        {
            sLog.outErrorDb("Template %d group %d has total chance > 100%% (%f)", id, i+1, chance);
        }
    }
    // TODO: References validity checks
}
