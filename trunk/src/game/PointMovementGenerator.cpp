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

#include "PointMovementGenerator.h"
#include "Errors.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "MapManager.h"
#include "DestinationHolderImp.h"

//----- Point Movement Generator
template<class T>
void PointMovementGenerator<T>::Initialize(T &unit)
{
    unit.StopMoving();
    Traveller<T> traveller(unit);
    i_destinationHolder.SetDestination(traveller,x,y,z);
}

void PointMovementGenerator<Creature>::MovementInform(Creature &unit)
{
    unit.AI()->MovementInform(POINT_MOTION_TYPE, id);
}

template<class T>
bool PointMovementGenerator<T>::Update(T &unit, const uint32 &diff)
{
    if(!&unit)
        return false;

    if(unit.hasUnitState(UNIT_STAT_ROOT | UNIT_STAT_STUNDED))
        return true;

    Traveller<T> traveller(unit);

    i_destinationHolder.UpdateTraveller(traveller, diff, false);

    if(i_destinationHolder.HasArrived())
    {
        unit.StopMoving();
        MovementInform(unit);
        return false;    
    }

    return true;
}