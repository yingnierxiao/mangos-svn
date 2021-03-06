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

#ifndef _PLAYER_DUMP_H
#define _PLAYER_DUMP_H
/*
#include "Log.h"
#include "Object.h"
#include "Bag.h"
#include "Creature.h"
#include "Player.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "Corpse.h"
#include "QuestDef.h"
#include "Path.h"
#include "ItemPrototype.h"
#include "NPCHandler.h"
#include "Database/DatabaseEnv.h"
#include "AuctionHouseObject.h"
#include "Mail.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "Policies/Singleton.h"
#include "Database/SQLStorage.h"
*/
#include <string>
#include <map>
#include <set>

enum DumpTableType
{
    DTT_CHARACTER,      //                                  // characters

    DTT_CHAR_TABLE,     //                                  // character_action, character_aura, character_homebind, 
                                                            // character_kill, character_queststatus, character_reputation, 
                                                            // character_spell, character_spell_cooldown, character_ticket, 
                                                            // character_tutorial

    DTT_INVENTORY,      //    -> item guids collection      // character_inventory

    DTT_MAIL,           //    -> mail ids collection        // mail
                        //    -> item_text

    DTT_MAIL_ITEM,      // <- mail ids                      // mail_items
                        //    -> item guids collection

    DTT_ITEM,           // <- item guids                    // item_instance
                        //    -> item_text

    DTT_ITEM_GIFT,      // <- item guids                    // character_gifts

    DTT_PET,            //    -> pet guids collection       // character_pet
    DTT_PET_TABLE,      // <- pet guids                     // pet_aura, pet_spell, pet_spell_cooldown
    DTT_ITEM_TEXT,      // <- item_text                     // item_text
};

class PlayerDump
{
    protected:
        PlayerDump() {}
};

class PlayerDumpWriter : public PlayerDump
{
    public:
        PlayerDumpWriter() {}

        std::string GetDump(uint32 guid);
        bool WriteDump(std::string file, uint32 guid);
    private:
        bool DumpTable(std::string& dump, uint32 guid, char const*tableFrom, char const*tableTo, DumpTableType type);

        typedef std::set<uint32> GUIDs;
        GUIDs pets;
        GUIDs mails;
        GUIDs items;
        GUIDs texts;
};

class PlayerDumpReader : public PlayerDump
{
    public:
        PlayerDumpReader() {}

        bool LoadDump(std::string file, uint32 account, std::string name, uint32 guid);
};

#endif
