/**
 * @file cl_team.c
 * @brief Team management, name generation and parsing.
 */

/*
Copyright (C) 2002-2007 UFO: Alien Invasion team.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.m

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "client.h"
#include "cl_global.h"
#include "cl_game.h"
#include "cl_team.h"
#include "multiplayer/mp_team.h"
#include "cl_actor.h"
#include "cl_rank.h"
#include "cl_ugv.h"
#include "menu/node/m_node_container.h"

linkedList_t *employeeList;	/* @sa E_GetEmployeeByMenuIndex */
int employeesInCurrentList;

static void CL_MarkTeam_f(void);

/** @brief List of currently displayed or equipeable characters. */
static chrList_t chrDisplayList;

/**
 * @brief Translate the skin id to skin name
 * @param[in] id The id of the skin
 * @return Translated skin name
 */
const char* CL_GetTeamSkinName (int id)
{
	switch(id) {
	case 0:
		return _("Urban");
		break;
	case 1:
		return _("Jungle");
		break;
	case 2:
		return _("Desert");
		break;
	case 3:
		return _("Arctic");
		break;
	case 4:
		return _("Yellow");
		break;
	case 5:
		return _("CCCP");
		break;
	default:
		Sys_Error("CL_GetTeamSkinName: Unknown skin id %i - max is %i\n", id, NUM_TEAMSKINS-1);
		break;
	}
	return NULL; /* never reached */
}

/**
 * @brief Returns the aircraft for the team and soldier selection menus
 * @note Multiplayer and skirmish are using @c cls.missionaircraft, campaign mode is
 * using the current selected aircraft in base
 */
static aircraft_t *CL_GetTeamAircraft (base_t *base)
{
	if (!base)
		Sys_Error("CL_GetTeamAircraft: Called without base");
	return base->aircraftCurrent;
}

/**
 * @brief Returns the storage for the team and soldier selection menus
 * @note Multiplayer and skirmish are using @c ccs.eMission, campaign mode is
 * using the storage from the given base
 */
static equipDef_t *CL_GetStorage (base_t *base)
{
	if (GAME_IsCampaign()) {
		if (!base)
			Sys_Error("CL_GetStorage: Called without base");
		return &base->storage;
	} else {
		return &ccs.eMission;
	}
}

/**
 * @sa CL_LoadItem
 */
static void CL_SaveItem (sizebuf_t *buf, item_t item, int container, int x, int y)
{
	assert(item.t);
/*	Com_Printf("Add item %s to container %i (t=%i:a=%i:m=%i) (x=%i:y=%i)\n", csi.ods[item.t].id, container, item.t, item.a, item.m, x, y);*/
	MSG_WriteFormat(buf, "bbbbbl", item.a, container, x, y, item.rotated, item.amount);
	MSG_WriteString(buf, item.t->id);
	if (item.a > NONE_AMMO)
		MSG_WriteString(buf, item.m->id);
}

/**
 * @sa CL_SaveItem
 * @sa CL_LoadInventory
 */
void CL_SaveInventory (sizebuf_t *buf, const inventory_t *i)
{
	int j, nr = 0;
	invList_t *ic;

	for (j = 0; j < csi.numIDs; j++)
		for (ic = i->c[j]; ic; ic = ic->next)
			nr++;

	Com_DPrintf(DEBUG_CLIENT, "CL_SaveInventory: Send %i items\n", nr);
	MSG_WriteShort(buf, nr);
	for (j = 0; j < csi.numIDs; j++)
		for (ic = i->c[j]; ic; ic = ic->next)
			CL_SaveItem(buf, ic->item, j, ic->x, ic->y);
}

/**
 * @sa CL_SaveItem
 */
static void CL_LoadItem (sizebuf_t *buf, item_t *item, int *container, int *x, int *y)
{
	const char *itemID;

	/* reset */
	memset(item, 0, sizeof(*item));
	item->a = NONE_AMMO;

	MSG_ReadFormat(buf, "bbbbbl", &item->a, container, x, y, &item->rotated, &item->amount);
	itemID = MSG_ReadString(buf);
	item->t = INVSH_GetItemByID(itemID);
	if (item->a > NONE_AMMO) {
		itemID = MSG_ReadString(buf);
		item->m = INVSH_GetItemByID(itemID);
	}
}

/**
 * @sa CL_SaveInventory
 * @sa CL_LoadItem
 * @sa Com_AddToInventory
 */
void CL_LoadInventory (sizebuf_t *buf, inventory_t *i)
{
	item_t item;
	int container, x, y;
	int nr = MSG_ReadShort(buf);

	Com_DPrintf(DEBUG_CLIENT, "CL_LoadInventory: Read %i items\n", nr);
	assert(nr < MAX_INVLIST);
	for (; nr-- > 0;) {
		CL_LoadItem(buf, &item, &container, &x, &y);
		if (!Com_AddToInventory(i, item, &csi.ids[container], x, y, 1))
			Com_Printf("Could not add item '%s' to inventory\n", item.t ? item.t->id : "NULL");
	}
}

/**
 * @sa G_WriteItem
 * @sa G_ReadItem
 * @note The amount of the item_t struct should not be needed here - because
 * the amount is only valid for idFloor and idEquip
 */
static void CL_NetSendItem (struct dbuffer *buf, item_t item, int container, int x, int y)
{
	const int ammoIdx = item.m ? item.m->idx : NONE;
	assert(item.t);
	Com_DPrintf(DEBUG_CLIENT, "CL_NetSendItem: Add item %s to container %i (t=%i:a=%i:m=%i) (x=%i:y=%i)\n",
		item.t->id, container, item.t->idx, item.a, ammoIdx, x, y);
	NET_WriteFormat(buf, ev_format[EV_INV_TRANSFER], item.t->idx, item.a, ammoIdx, container, x, y, item.rotated);
}

/**
 * @sa G_SendInventory
 */
void CL_NetSendInventory (struct dbuffer *buf, const inventory_t *i)
{
	int j, nr = 0;
	const invList_t *ic;

	for (j = 0; j < csi.numIDs; j++)
		for (ic = i->c[j]; ic; ic = ic->next)
			nr++;

	NET_WriteShort(buf, nr * INV_INVENTORY_BYTES);
	for (j = 0; j < csi.numIDs; j++)
		for (ic = i->c[j]; ic; ic = ic->next)
			CL_NetSendItem(buf, ic->item, j, ic->x, ic->y);
}

/**
 * @sa G_WriteItem
 * @sa G_ReadItem
 * @note The amount of the item_t struct should not be needed here - because
 * the amount is only valid for idFloor and idEquip
 */
void CL_NetReceiveItem (struct dbuffer *buf, item_t *item, int *container, int *x, int *y)
{
	/* reset */
	int t, m;
	item->t = item->m = NULL;
	item->a = NONE_AMMO;
	NET_ReadFormat(buf, ev_format[EV_INV_TRANSFER], &t, &item->a, &m, container, x, y, &item->rotated);

	if (t != NONE)
		item->t = &csi.ods[t];

	if (m != NONE)
		item->m = &csi.ods[m];
}

/**
 * @brief Test the names in team_*.ufo
 *
 * This is a console command to test the names that were defined in team_*.ufo
 * Usage: givename <gender> <teamid> [num]
 * valid genders are male, female, neutral
 */
static void CL_GiveName_f (void)
{
	const char *name;
	int i, j, num;

	if (Cmd_Argc() < 3) {
		Com_Printf("Usage: %s <gender> <teamid> [num]\n", Cmd_Argv(0));
		return;
	}

	/* get gender */
	for (i = 0; i < NAME_LAST; i++)
		if (!Q_strcmp(Cmd_Argv(1), name_strings[i]))
			break;

	if (i == NAME_LAST) {
		Com_Printf("'%s' isn't a gender! (male and female are)\n", Cmd_Argv(1));
		return;
	}

	if (Cmd_Argc() > 3) {
		num = atoi(Cmd_Argv(3));
		if (num < 1)
			num = 1;
		if (num > 20)
			num = 20;
	} else
		num = 1;

	for (j = 0; j < num; j++) {
		/* get first name */
		name = Com_GiveName(i, Cmd_Argv(2));
		if (!name) {
			Com_Printf("No first name in team '%s'\n", Cmd_Argv(2));
			return;
		}
		Com_Printf("%s", name);

		/* get last name */
		name = Com_GiveName(i + LASTNAME, Cmd_Argv(2));
		if (!name) {
			Com_Printf("\nNo last name in team '%s'\n", Cmd_Argv(2));
			return;
		}

		/* print out name */
		Com_Printf(" %s\n", name);
	}
}

/**
 * @brief Return a given ugv_t pointer
 * @param[in] ugvID Which base the employee should be hired in.
 * @return ugv_t pointer or NULL if not found.
 * @note If there ever is a problem because an id with the name "NULL" isn't found then this is because NULL pointers in E_Save/Employee_t are stored like that (duh) ;).
 */
ugv_t *CL_GetUgvByID (const char *ugvID)
{
	int i;

	for (i = 0; i < gd.numUGV; i++) {
		if (!Q_strcmp(gd.ugvs[i].id, ugvID)) {
			return &gd.ugvs[i];
		}
	}

	Com_Printf("CL_GetUgvByID: No ugv_t entry found for id '%s' in %i entries.\n", ugvID, gd.numUGV);
	return NULL;
}


/**
 * @brief Generates the skills and inventory for a character and for a 2x2 unit
 *
 * @param[in] employee The employee to create character data for.
 * @param[in] team Which team to use for creation.
 * @todo fix the assignment of ucn??
 * @todo fix the WholeTeam stuff
 */
void CL_GenerateCharacter (character_t *chr, const char *team, employeeType_t employeeType, const ugv_t *ugvType)
{
	char teamDefName[MAX_VAR];
	int teamValue;

	memset(chr, 0, sizeof(*chr));

	/* link inventory */
	INVSH_DestroyInventory(&chr->inv);

	/* get ucn */
	chr->ucn = gd.nextUCN++;

	/* if not human - then we are TEAM_ALIEN */
	if (strstr(team, "human"))
		teamValue = TEAM_PHALANX;
	else if (strstr(team, "alien"))
		teamValue = TEAM_ALIEN;
	else
		teamValue = TEAM_CIVILIAN;

	/* Set default reaction-mode for all character-types to "once".
	 * AI actor (includes aliens if one doesn't play AS them) are set in @sa G_SpawnAIPlayer */
	chr->reservedTus.reserveReaction = STATE_REACTION_ONCE;

	CL_CharacterSetShotSettings(chr, -1, -1, -1);

	/* Generate character stats, models & names. */
	switch (employeeType) {
	case EMPL_SOLDIER:
		chr->score.rank = CL_GetRankIdx("rifleman");
		/* Create attributes. */
		CHRSH_CharGenAbilitySkills(chr, teamValue, employeeType, GAME_IsMultiplayer());
		Q_strncpyz(teamDefName, team, sizeof(teamDefName));
		break;
	case EMPL_SCIENTIST:
		chr->score.rank = CL_GetRankIdx("scientist");
		/* Create attributes. */
		CHRSH_CharGenAbilitySkills(chr, teamValue, employeeType, GAME_IsMultiplayer());
		Com_sprintf(teamDefName, sizeof(teamDefName), "%s_scientist", team);
		break;
	case EMPL_PILOT:
		chr->score.rank = CL_GetRankIdx("pilot");
		/* Create attributes. */
		CHRSH_CharGenAbilitySkills(chr, teamValue, employeeType, GAME_IsMultiplayer());
		Com_sprintf(teamDefName, sizeof(teamDefName), "%s_pilot", team);
		break;
	case EMPL_WORKER:
		chr->score.rank = CL_GetRankIdx("worker");
		/* Create attributes. */
		CHRSH_CharGenAbilitySkills(chr, teamValue, employeeType, GAME_IsMultiplayer());
		Com_sprintf(teamDefName, sizeof(teamDefName), "%s_worker", team);
		break;
	case EMPL_ROBOT:
		if (!ugvType)
			Sys_Error("CL_GenerateCharacter: no type given for generation of EMPL_ROBOT employee.\n");

		chr->score.rank = CL_GetRankIdx("ugv");

		/* Create attributes. */
		/** @todo get the min/max values from ugv_t def? */
		CHRSH_CharGenAbilitySkills(chr, teamValue, employeeType, GAME_IsMultiplayer());

		Com_sprintf(teamDefName, sizeof(teamDefName), "%s%s", team, ugvType->actors);
		break;
	default:
		Sys_Error("Unknown employee type\n");
		break;
	}
	chr->skin = Com_GetCharacterValues(teamDefName, chr);
}

static selectBoxOptions_t skinlist[] = {
	{"urban", N_("Urban"), "team_changeskin;", "0", NULL, NULL, qfalse},
	{"jungle", N_("Jungle"), "team_changeskin;", "1", NULL, NULL, qfalse},
	{"desert", N_("Desert"), "team_changeskin;", "2", NULL, NULL, qfalse},
	{"arctic", N_("Arctic"), "team_changeskin;", "3", NULL, NULL, qfalse},
	{"multionly_yellow", N_("Yellow"), "team_changeskin;", "4", NULL, NULL, qfalse},
	{"multionly_cccp", N_("CCCP"), "team_changeskin;", "5", NULL, NULL, qfalse},
};

/**
 * @brief Init skins into the GUI
 */
static void CL_InitSkin_f (void)
{
	menuNode_t *node = MN_GetNodeByPath("equipment.skins");
	assert(node);

	/** link all elements */
	if (node->u.option.first == NULL) {
		int i;
		for (i = 0; i < NUM_TEAMSKINS - 1; i++)
			skinlist[i].next = &skinlist[i + 1];
		node->u.option.first = skinlist;
	}

	/** link/unlink multiplayer skins */
	if (GAME_IsSingleplayer()) {
		skinlist[NUM_TEAMSKINS_SINGLEPLAYER - 1].next = NULL;
		node->u.option.count = NUM_TEAMSKINS_SINGLEPLAYER;
	} else {
		skinlist[NUM_TEAMSKINS_SINGLEPLAYER - 1].next = &skinlist[NUM_TEAMSKINS_SINGLEPLAYER];
		node->u.option.count = NUM_TEAMSKINS;
	}
}

/**
 * @brief Change the skin of the selected actor.
 */
static void CL_ChangeSkin_f (void)
{
	const int sel = cl_selected->integer;

	if (sel >= 0 && sel < chrDisplayList.num) {
		int newSkin = Cvar_VariableInteger("mn_skin");
		if (newSkin >= NUM_TEAMSKINS || newSkin < 0)
			newSkin = 0;

		/* don't allow all skins in singleplayer */
		if (GAME_IsSingleplayer() && newSkin >= NUM_TEAMSKINS_SINGLEPLAYER)
			newSkin = 0;

		if (chrDisplayList.chr[sel]) {
			chrDisplayList.chr[sel]->skin = newSkin;

			Cvar_SetValue("mn_skin", newSkin);
			Cvar_Set("mn_skinname", CL_GetTeamSkinName(newSkin));
		}
	}
}

/**
 * @brief Use current skin for all team members onboard.
 */
static void CL_ChangeSkinOnBoard_f (void)
{
	int newSkin, i;

	/* Get selected skin and fall back to default skin if it is not valid. */
	newSkin = Cvar_VariableInteger("mn_skin");
	if (newSkin >= NUM_TEAMSKINS || newSkin < 0)
		newSkin = 0;

	/* don't allow all skins in singleplayer */
	if (GAME_IsSingleplayer() && newSkin >= NUM_TEAMSKINS_SINGLEPLAYER)
		newSkin = 0;

	/**
	 * Apply new skin to all (shown/dsiplayed) team-members.
	 * @todo What happens if a model of a team member does not have the selected skin?
	 */
	for (i = 0; i < chrDisplayList.num; i++) {
		assert(chrDisplayList.chr[i]);
		chrDisplayList.chr[i]->skin = newSkin;
	}
}

/**
 * @sa CL_ReloadAndRemoveCarried
 */
static item_t CL_AddWeaponAmmo (equipDef_t * ed, item_t item)
{
	int i;
	objDef_t *type = item.t;

	assert(ed->num[type->idx] > 0);
	ed->num[type->idx]--;

	if (type->weapons[0]) {
		/* The given item is ammo or self-contained weapon (i.e. It has firedefinitions. */
		if (type->oneshot) {
			/* "Recharge" the oneshot weapon. */
			item.a = type->ammo;
			item.m = item.t; /* Just in case this hasn't been done yet. */
			Com_DPrintf(DEBUG_CLIENT, "CL_AddWeaponAmmo: oneshot weapon '%s'.\n", type->id);
			return item;
		} else {
			/* No change, nothing needs to be done to this item. */
			return item;
		}
	} else if (!type->reload) {
		/* The given item is a weapon but no ammo is needed,
		 * so fire definitions are in t (the weapon). Setting equal. */
		item.m = item.t;
		return item;
	} else if (item.a) {
		assert(item.m);
		/* The item is a weapon and it was reloaded one time. */
		if (item.a == type->ammo) {
			/* Fully loaded, no need to reload, but mark the ammo as used. */
			if (ed->num[item.m->idx] > 0) {
				ed->num[item.m->idx]--;
				return item;
			} else {
				/* Your clip has been sold; give it back. */
				item.a = NONE_AMMO;
				return item;
			}
		}
	}

	/* Check for complete clips of the same kind */
	if (item.m && ed->num[item.m->idx] > 0) {
		ed->num[item.m->idx]--;
		item.a = type->ammo;
		return item;
	}

	/* Search for any complete clips. */
	/** @todo We may want to change this to use the type->ammo[] info. */
	for (i = 0; i < csi.numODs; i++) {
		if (INVSH_LoadableInWeapon(&csi.ods[i], type)) {
			if (ed->num[i] > 0) {
				ed->num[i]--;
				item.a = type->ammo;
				item.m = &csi.ods[i];
				return item;
			}
		}
	}

	/** @todo on return from a mission with no clips left
	 * and one weapon half-loaded wielded by soldier
	 * and one empty in equip, on the first opening of equip,
	 * the empty weapon will be in soldier hands, the half-full in equip;
	 * this should be the other way around. */

	/* Failed to find a complete clip - see if there's any loose ammo
	 * of the same kind; if so, gather it all in this weapon. */
	if (item.m && ed->numLoose[item.m->idx] > 0) {
		item.a = ed->numLoose[item.m->idx];
		ed->numLoose[item.m->idx] = 0;
		return item;
	}

	/* See if there's any loose ammo */
	/** @todo We may want to change this to use the type->ammo[] info. */
	item.a = NONE_AMMO;
	for (i = 0; i < csi.numODs; i++) {
		if (INVSH_LoadableInWeapon(&csi.ods[i], type)
		&& (ed->numLoose[i] > item.a)) {
			if (item.a > 0) {
				/* We previously found some ammo, but we've now found other
				 * loose ammo of a different (but appropriate) type with
				 * more bullets.  Put the previously found ammo back, so
				 * we'll take the new type. */
				assert(item.m);
				ed->numLoose[item.m->idx] = item.a;
				/* We don't have to accumulate loose ammo into clips
				 * because we did it previously and we create no new ammo */
			}
			/* Found some loose ammo to load the weapon with */
			item.a = ed->numLoose[i];
			ed->numLoose[i] = 0;
			item.m = &csi.ods[i];
		}
	}
	return item;
}

/**
 * @brief Reloads weapons and removes "not assigned" ones from containers.
 * @param[in] aircraft Pointer to an aircraft for given team.
 * @param[in] ed equipDef_t pointer to equipment of given character in a team.
 * @sa CL_AddWeaponAmmo
 * @todo campaign mode only function
 */
void CL_ReloadAndRemoveCarried (aircraft_t *aircraft, equipDef_t * ed)
{
	invList_t *ic, *next;
	int p, container;

	/** Iterate through in container order (right hand, left hand, belt,
	 * holster, backpack) at the top level, i.e. each squad member reloads
	 * her right hand, then each reloads his left hand, etc. The effect
	 * of this is that when things are tight, everyone has the opportunity
	 * to get their preferred weapon(s) loaded before anyone is allowed
	 * to keep her spares in the backpack or on the floor. We don't want
	 * the first person in the squad filling their backpack with spare ammo
	 * leaving others with unloaded guns in their hands... */

	assert(aircraft);

	Com_DPrintf(DEBUG_CLIENT, "CL_ReloadAndRemoveCarried:aircraft idx: %i, team size: %i\n",
		aircraft->idx, aircraft->teamSize);

	/* Auto-assign weapons to UGVs/Robots if they have no weapon yet. */
	for (p = 0; p < aircraft->maxTeamSize; p++) {
		if (aircraft->acTeam[p] && aircraft->acTeam[p]->ugv) {
			/** This is an UGV */
			character_t *chr = &aircraft->acTeam[p]->chr;
			assert(chr);

			/* Check if there is a weapon and add it if there isn't. */
			if (!chr->inv.c[csi.idRight] || !chr->inv.c[csi.idRight]->item.t)
				INVSH_EquipActorRobot(&chr->inv, chr, INVSH_GetItemByID(aircraft->acTeam[p]->ugv->weapon));
		}
	}

	for (container = 0; container < csi.numIDs; container++) {
		for (p = 0; p < aircraft->maxTeamSize; p++) {
			if (aircraft->acTeam[p]) {
				character_t *chr = &aircraft->acTeam[p]->chr;
				assert(chr);
				for (ic = chr->inv.c[container]; ic; ic = next) {
					next = ic->next;
					if (ed->num[ic->item.t->idx] > 0) {
						ic->item = CL_AddWeaponAmmo(ed, ic->item);
					} else {
						/* Drop ammo used for reloading and sold carried weapons. */
						Com_RemoveFromInventory(&chr->inv, &csi.ids[container], ic);
					}
				}
			}
		}
	}
}

/**
 * @brief Clears all containers that are temp containers (see script definition).
 * @sa CL_UpdateEquipmentMenuParameters_f
 * @sa MP_SaveTeamMultiplayerInfo
 * @sa GAME_SendCurrentTeamSpawningInfo
 * @todo campaign mode only function
 */
void CL_CleanTempInventory (base_t* base)
{
	int i, k;

	for (i = 0; i < MAX_EMPLOYEES; i++)
		for (k = 0; k < csi.numIDs; k++)
			if (csi.ids[k].temp) {
				/* idFloor and idEquip are temp */
				gd.employees[EMPL_SOLDIER][i].chr.inv.c[k] = NULL;
				gd.employees[EMPL_ROBOT][i].chr.inv.c[k] = NULL;
			}

	if (!base)
		return;

	INVSH_DestroyInventory(&base->bEquipment);
}

/**
 * @brief Displays actor equipment and unused items in proper (filter) category.
 * @note This function is called everytime the equipment screen for the team pops up.
 * @todo Do we allow EMPL_ROBOTs to be equipable? Or is simple buying of ammo enough (similar to original UFO/XCOM)?
 * In the first case the EMPL_SOLDIER stuff below needs to be changed.
 * @todo campaign mode only function
 */
static void CL_UpdateEquipmentMenuParameters_f (void)
{
	equipDef_t unused;
	int i, p;
	aircraft_t *aircraft;
	int team;

	if (!GAME_IsCampaign() || !baseCurrent)
		return;

	aircraft = CL_GetTeamAircraft(baseCurrent);
	if (!aircraft)
		return;

	/* no soldiers are assigned to the current aircraft. */
	if (!aircraft->teamSize) {
		MN_PopMenu(qfalse);
		return;
	}

	/* Get team. */
	if (strstr(Cvar_VariableString("team"), "alien")) {
		team = TEAM_ALIEN;
		Com_DPrintf(DEBUG_CLIENT, "CL_UpdateEquipmentMenuParameters_f: team alien, id: %i\n", team);
	} else {
		team = TEAM_PHALANX;
		Com_DPrintf(DEBUG_CLIENT, "CL_UpdateEquipmentMenuParameters_f: team human, id: %i\n", team);
	}

	Cvar_ForceSet("cl_selected", "0");

	/** @todo Skip EMPL_ROBOT (i.e. ugvs) for now . */
	p = CL_UpdateActorAircraftVar(aircraft, EMPL_SOLDIER);
	for (; p < MAX_ACTIVETEAM; p++) {
		Cvar_ForceSet(va("mn_name%i", p), "");
		Cbuf_AddText(va("equipdisable%i\n", p));
	}

	if (chrDisplayList.num > 0)
		menuInventory = &chrDisplayList.chr[0]->inv;
	else
		menuInventory = NULL;
	selActor = NULL;

	/* reset description */
	Cvar_Set("mn_itemname", "");
	Cvar_Set("mn_item", "");
	MN_MenuTextReset(TEXT_STANDARD);

	/* manage inventory */
	unused = *CL_GetStorage(aircraft->homebase); /* copied, including arrays inside! */

	CL_CleanTempInventory(aircraft->homebase);
	CL_ReloadAndRemoveCarried(aircraft, &unused);

	/* a 'tiny hack' to add the remaining equipment (not carried)
	 * correctly into buy categories, reloading at the same time;
	 * it is valid only due to the following property: */
	assert(MAX_CONTAINERS >= FILTER_AIRCRAFT);

	for (i = 0; i < csi.numODs; i++) {
		/* Don't allow to show armour for other teams in the menu. */
		if (!Q_strcmp(csi.ods[i].type, "armour") && csi.ods[i].useable != team)
			continue;

		/* Don't allow to show unresearched items. */
		if (!RS_IsResearched_ptr(csi.ods[i].tech))
			continue;

		while (unused.num[i]) {
			const item_t item = {NONE_AMMO, NULL, &csi.ods[i], 0, 0};
			inventory_t *i = &aircraft->homebase->bEquipment;
			if (!Com_AddToInventory(i, CL_AddWeaponAmmo(&unused, item), &csi.ids[csi.idEquip], NONE, NONE, 1))
				break; /* no space left in menu */
		}
	}

	/* First-time linking of menuInventory. */
	if (!menuInventory->c[csi.idEquip]) {
		menuInventory->c[csi.idEquip] = baseCurrent->bEquipment.c[csi.idEquip];
	}
}

static void CL_ActorEquipmentSelect_f (void)
{
	int num;
	character_t *chr;

	/* check syntax */
	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}

	num = atoi(Cmd_Argv(1));
	if (num >= chrDisplayList.num)
		return;

	/* update menu inventory */
	if (menuInventory && menuInventory != &chrDisplayList.chr[num]->inv) {
		chrDisplayList.chr[num]->inv.c[csi.idEquip] = menuInventory->c[csi.idEquip];
		/* set 'old' idEquip to NULL */
		menuInventory->c[csi.idEquip] = NULL;
	}
	menuInventory = &chrDisplayList.chr[num]->inv;
	chr = chrDisplayList.chr[num];

	/* deselect current selected soldier and select the new one */
	MN_ExecuteConfunc("equipdeselect%i", cl_selected->integer);
	MN_ExecuteConfunc("equipselect%i", num);

	/* now set the cl_selected cvar to the new actor id */
	Cvar_ForceSet("cl_selected", va("%i", num));

	/* set info cvars */
	if (chr->emplType == EMPL_ROBOT)
		CL_UGVCvars(chr);
	else
		CL_CharacterCvars(chr);
}

static void CL_ActorSoldierSelect_f (void)
{
	const menu_t *activeMenu = MN_GetActiveMenu();
	int num;

	/* check syntax */
	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}

	num = atoi(Cmd_Argv(1));

	/* check whether we are connected (tactical mission) */
	if (CL_OnBattlescape()) {
		CL_ActorSelectList(num);
		return;
	}

	/* we are still in the menu */
	if (!Q_strcmp(activeMenu->name, "employees")) {
		/* this is hire menu: we can select soldiers, worker, pilots, or researcher */
		/** @todo remove this test, employee_list_click can do the same better */
		if (num < employeesInCurrentList) {
			Cmd_ExecuteString(va("employee_list_click %i", num));
		}
	} else if (!Q_strcmp(activeMenu->name, "team")) {
		Cmd_ExecuteString(va("team_select %i", num));
	} else if (!Q_strcmp(activeMenu->name, "equipment")) {
		Cmd_ExecuteString(va("equip_select %i", num));
	}
}

static void CL_ActorTeamSelect_f (void)
{
	employee_t *employee;
	character_t *chr;
	int num;
	const employeeType_t employeeType = cls.displayHeavyEquipmentList
			? EMPL_ROBOT : EMPL_SOLDIER;
	base_t *base = GAME_IsCampaign() ? baseCurrent : NULL;

	if (GAME_IsCampaign() && !base)
		return;

	/* check syntax */
	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}

	num = atoi(Cmd_Argv(1));
	if (num >= E_CountHired(base, employeeType))
		return;

	employee = E_GetEmployeeByMenuIndex(num);
	if (!employee)
		Sys_Error("CL_ActorTeamSelect_f: No employee at list-pos %i (base: %i)\n", num, base ? base->idx : -1);

	chr = &employee->chr;
	if (!chr)
		Sys_Error("CL_ActorTeamSelect_f: No hired character at list-pos %i (base: %i)\n", num, base ? base->idx : -1);

	/* deselect current selected soldier and select the new one */
	MN_ExecuteConfunc("teamdeselect%i", cl_selected->integer);
	MN_ExecuteConfunc("teamselect%i", num);

	/* now set the cl_selected cvar to the new actor id */
	Cvar_ForceSet("cl_selected", va("%i", num));

	/* set info cvars */
	if (chr->emplType == EMPL_ROBOT)
		CL_UGVCvars(chr);
	else
		CL_CharacterCvars(chr);
}

/**
 * @brief implements the "nextsoldier" command
 */
static void CL_NextSoldier_f (void)
{
	if (CL_OnBattlescape()) {
		CL_ActorSelectNext();
	}
}

/**
 * @brief implements the reselect command
 */
static void CL_ThisSoldier_f (void)
{
	if (CL_OnBattlescape()) {
		CL_ActorSelectList(cl_selected->integer);
	}
}

/**
 * @brief Update the GUI with the selected item
 */
static void CL_UpdateObject_f (void)
{
	int num;
	objDef_t *obj;
	int filter;
	cvar_t *var;
	qboolean mustWeChangeTab = qtrue;

	/* check syntax */
	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <objectid> <mustwechangetab>\n", Cmd_Argv(0));
		return;
	}

	if (Cmd_Argc() == 3)
		mustWeChangeTab = atoi(Cmd_Argv(2)) >= 1;

	num = atoi(Cmd_Argv(1));
	if (num < 0 || num >= csi.numODs) {
		Com_Printf("Id %i out of range 0..%i\n", num, csi.numODs);
		return;
	}
	obj = &csi.ods[num];

	/* update item description */
	UP_ItemDescription(obj);

	/* update tab */
	if (mustWeChangeTab) {
		var = Cvar_FindVar("mn_equiptype");
		filter = INV_GetFilterFromItem(obj);
		if (var->integer != filter) {
			Cvar_SetValue("mn_equiptype", filter);
			MN_ExecuteConfunc("update_item_list\n");
		}
	}
}

/**
 * @brief Update the skin of the current soldier
 */
static void CL_UpdateSoldier_f (void)
{
	const int num = cl_selected->integer;

	/* We are in the base or multiplayer inventory */
	if (num < chrDisplayList.num) {
		assert(chrDisplayList.chr[num]);
		if (chrDisplayList.chr[num]->emplType == EMPL_ROBOT)
			CL_UGVCvars(chrDisplayList.chr[num]);
		else
			CL_CharacterCvars(chrDisplayList.chr[num]);
	}
}

/**
 * @brief Updates data about teams in aircraft.
 * @param[in] aircraft Pointer to an aircraft for a desired team.
 * @returns the number of employees that are in the aircraft and were added to
 * the character list
 */
int CL_UpdateActorAircraftVar (aircraft_t *aircraft, employeeType_t employeeType)
{
	int i;

	assert(aircraft);

	Cvar_Set("mn_hired", va(_("%i of %i"), aircraft->teamSize, aircraft->maxTeamSize));

	/* update chrDisplayList list (this is the one that is currently displayed) */
	chrDisplayList.num = 0;
	for (i = 0; i < aircraft->maxTeamSize; i++) {
		assert(chrDisplayList.num < MAX_ACTIVETEAM);
		if (!aircraft->acTeam[i])
			continue; /* Skip unused team-slot. */

		if (aircraft->acTeam[i]->type != employeeType)
			continue;

		chrDisplayList.chr[chrDisplayList.num] = &aircraft->acTeam[i]->chr;

		/* Sanity check(s) */
		if (!chrDisplayList.chr[chrDisplayList.num])
			Sys_Error("CL_UpdateActorAircraftVar: Could not get employee character with idx: %i\n", chrDisplayList.num);
		Com_DPrintf(DEBUG_CLIENT, "add %s to chrDisplayList (pos: %i)\n", chrDisplayList.chr[chrDisplayList.num]->name, chrDisplayList.num);
		Cvar_ForceSet(va("mn_name%i", chrDisplayList.num), chrDisplayList.chr[chrDisplayList.num]->name);

		/* Update number of displayed team-members. */
		chrDisplayList.num++;
	}

	for (i = chrDisplayList.num; i < MAX_ACTIVETEAM; i++)
		chrDisplayList.chr[i] = NULL;	/* Just in case */

	return chrDisplayList.num;
}

/**
 * @brief Init the teamlist checkboxes
 * @sa CL_UpdateActorAircraftVar
 * @todo Make this function use a temporary list with all list-able employees
 * instead of using gd.employees[][] directly. See also CL_Select_f->SELECT_MODE_TEAM
 * @todo don't use employees here, directly but only the character_t data - maybe also make
 * this a callback in cl_game.c
 * @todo campaign mode only function
 */
static void CL_MarkTeam_f (void)
{
	int j, k = 0;
	qboolean alreadyInOtherShip = qfalse;
	aircraft_t *aircraft;
	linkedList_t *emplList;

	const employeeType_t employeeType =
		cls.displayHeavyEquipmentList
			? EMPL_ROBOT
			: EMPL_SOLDIER;

	/* Check if we are allowed to be here.
	 * We are only allowed to be here if we already set up a base. */
	if (!baseCurrent) {
		Com_Printf("No base set up\n");
		MN_PopMenu(qfalse);
		return;
	}

	aircraft = CL_GetTeamAircraft(baseCurrent);
	if (!aircraft) {
		MN_PopMenu(qfalse);
		return;
	}

	CL_UpdateActorAircraftVar(aircraft, employeeType);

	/* Populate employeeList - base might be NULL for none-campaign mode games */
	E_GenerateHiredEmployeesList(aircraft->homebase);
	emplList = employeeList;
	while (emplList) {
		const employee_t *employee = (employee_t*)emplList->data;
		assert(employee->hired);
		assert(!employee->transfer);

		/* Search all aircraft except the current one. */
		alreadyInOtherShip = qfalse;
		for (j = 0; j < gd.numAircraft; j++) {
			if (j == aircraft->idx)
				continue;
			/* already on another aircraft */
			if (AIR_IsEmployeeInAircraft(employee, AIR_AircraftGetFromIDX(j)))
				alreadyInOtherShip = qtrue;
		}

		/* Set name of the employee. */
		Cvar_ForceSet(va("mn_ename%i", k), employee->chr.name);
		/* Change the buttons */
		MN_ExecuteConfunc("listdel %i", k);
		if (!alreadyInOtherShip && AIR_IsEmployeeInAircraft(employee, aircraft))
			MN_ExecuteConfunc("listadd %i", k);
		else if (alreadyInOtherShip)
			/* Disable the button - the soldier is already on another aircraft */
			MN_ExecuteConfunc("listdisable %i", k);

		/* Check if the employee has something equipped. */
		for (j = 0; j < csi.numIDs; j++) {
			/** @todo Wouldn't it be better here to check for temp containers */
			if (j != csi.idFloor && j != csi.idEquip && employee->chr.inv.c[j])
				break;
		}
		if (j < csi.numIDs)
			MN_ExecuteConfunc("listholdsequip %i", k);
		else
			MN_ExecuteConfunc("listholdsnoequip %i", k);

		k++;
		if (k >= cl_numnames->integer)
			break;

		emplList = emplList->next;
	}

	for (;k < cl_numnames->integer; k++) {
		MN_ExecuteConfunc("listdisable %i", k);
		Cvar_ForceSet(va("mn_name%i", k), "");
		MN_ExecuteConfunc("listholdsnoequip %i", k);
	}
}

/**
 * @brief changes the displayed list from soldiers to heavy equipment (e.g. tanks)
 * @note console command: team_toggle_list
 */
static void CL_ToggleTeamList_f (void)
{
	if (cls.displayHeavyEquipmentList) {
		Com_DPrintf(DEBUG_CLIENT, "Changing to soldier-list.\n");
		cls.displayHeavyEquipmentList = qfalse;
		MN_ExecuteConfunc("toggle_show_heavybutton");
	} else {
		if (gd.numEmployees[EMPL_ROBOT] > 0) {
			Com_DPrintf(DEBUG_CLIENT, "Changing to heavy equipment (tank) list.\n");
			cls.displayHeavyEquipmentList = qtrue;
			MN_ExecuteConfunc("toggle_show_soldiersbutton");
		} else {
			/* Nothing to display/assign - staying in soldier-list. */
			Com_DPrintf(DEBUG_CLIENT, "No heavy equipment available.\n");
		}
	}
	Cbuf_AddText("team_mark;team_select 0\n");
}

#ifdef DEBUG
static void CL_TeamListDebug_f (void)
{
	int i;
	base_t *base;
	aircraft_t *aircraft;

	base = CP_GetMissionBase();
	aircraft = cls.missionaircraft;

	if (!base) {
		Com_Printf("Build and select a base first\n");
		return;
	}

	if (!aircraft) {
		Com_Printf("Buy/build an aircraft first.\n");
		return;
	}

	Com_Printf("%i members in the current team", aircraft->teamSize);
	for (i = 0; i < aircraft->maxTeamSize; i++) {
		if (aircraft->acTeam[i]) {
			const character_t *chr = &aircraft->acTeam[i]->chr;
			Com_Printf("ucn %i - employee->idx: %i\n", chr->ucn, aircraft->acTeam[i]->idx);
		}
	}
}
#endif

/**
 * @brief Adds or removes a soldier to/from an aircraft.
 * @sa E_EmployeeHire_f
 * @todo campaign mode only function
 */
static void CL_AssignSoldier_f (void)
{
	base_t *base = baseCurrent;
	aircraft_t *aircraft;
	int num;
	const employeeType_t employeeType =
		cls.displayHeavyEquipmentList
			? EMPL_ROBOT
			: EMPL_SOLDIER;

	/* check syntax */
	if (Cmd_Argc() < 2) {
		Com_Printf("Usage: %s <num>\n", Cmd_Argv(0));
		return;
	}
	num = atoi(Cmd_Argv(1));

	/* In case we didn't populate the list with E_GenerateHiredEmployeesList before. */
	if (!employeeList)
		return;

	/* baseCurrent is checked here */
	if (num >= E_CountHired(base, employeeType) || num >= cl_numnames->integer) {
		/*Com_Printf("num: %i, max: %i\n", num, E_CountHired(baseCurrent, employeeType));*/
		return;
	}

	aircraft = CL_GetTeamAircraft(base);
	if (!aircraft)
		return;

	AIM_AddEmployeeFromMenu(aircraft, num);
}

void TEAM_InitStartup (void)
{
	Cmd_AddCommand("givename", CL_GiveName_f, "Give the team members names from the team_*.ufo files");
	Cmd_AddCommand("genequip", CL_UpdateEquipmentMenuParameters_f, NULL);
	Cmd_AddCommand("team_mark", CL_MarkTeam_f, NULL);
	Cmd_AddCommand("team_hire", CL_AssignSoldier_f, "Add/remove already hired actor to the aircraft");
	Cmd_AddCommand("team_select", CL_ActorTeamSelect_f, "Select a soldier in the team creation menu");
	Cmd_AddCommand("team_initskin", CL_InitSkin_f, "Init skin according to the game mode");
	Cmd_AddCommand("team_changeskin", CL_ChangeSkin_f, "Change the skin of the soldier");
	Cmd_AddCommand("team_changeskinteam", CL_ChangeSkinOnBoard_f, "Change the skin for the hole team in the current aircraft");
	Cmd_AddCommand("equip_select", CL_ActorEquipmentSelect_f, "Select a soldier in the equipment menu");
	Cmd_AddCommand("soldier_select", CL_ActorSoldierSelect_f, _("Select a soldier from list"));
	Cmd_AddCommand("soldier_reselect", CL_ThisSoldier_f, _("Reselect the current soldier"));
	Cmd_AddCommand("soldier_updatecurrent", CL_UpdateSoldier_f, _("Update a soldier"));
	Cmd_AddCommand("object_update", CL_UpdateObject_f, _("Update a soldier"));
	Cmd_AddCommand("nextsoldier", CL_NextSoldier_f, _("Toggle to next soldier"));
	Cmd_AddCommand("team_toggle_list", CL_ToggleTeamList_f, "Changes between assignment-list for soldiers and heavy equipment (e.g. Tanks)");
#ifdef DEBUG
	Cmd_AddCommand("teamlist", CL_TeamListDebug_f, "Debug function to show all hired and assigned teammembers");
#endif
}

typedef struct {
	int ucn;
	int HP;
	int STUN;
	int morale;

	chrScoreGlobal_t chrscore;
} updateCharacter_t;

/**
 * @brief Parses the character data which was send by G_EndGame using G_SendCharacterData
 * @param[in] msg The network buffer message. If this is NULL the character is updated, if this
 * is not NULL the data is stored in a temp buffer because the player can choose to retry
 * the mission and we have to catch this situation to not update the character data in this case.
 * @sa G_SendCharacterData
 * @sa GAME_SendCurrentTeamSpawningInfo
 * @sa G_EndGame
 * @sa E_Save
 */
void CL_ParseCharacterData (struct dbuffer *msg)
{
	static updateCharacter_t updateCharacterArray[MAX_WHOLETEAM];
	static int num = 0;
	int i, j;
	character_t* chr;

	if (!msg) {
		for (i = 0; i < num; i++) {
			employee_t *employee = E_GetEmployeeFromChrUCN(updateCharacterArray[i].ucn);
			if (!employee) {
				Com_Printf("Warning: Could not get character with ucn: %i.\n", updateCharacterArray[i].ucn);
				continue;
			}
			chr = &employee->chr;
			chr->HP = updateCharacterArray[i].HP;
			chr->STUN = updateCharacterArray[i].STUN;
			chr->morale = updateCharacterArray[i].morale;

			/** Scores @sa inv_shared.h:chrScoreGlobal_t */
			memcpy(chr->score.experience, updateCharacterArray[i].chrscore.experience, sizeof(chr->score.experience));
			memcpy(chr->score.skills, updateCharacterArray[i].chrscore.skills, sizeof(chr->score.skills));
			memcpy(chr->score.kills, updateCharacterArray[i].chrscore.kills, sizeof(chr->score.kills));
			memcpy(chr->score.stuns, updateCharacterArray[i].chrscore.stuns, sizeof(chr->score.stuns));
			chr->score.assignedMissions = updateCharacterArray[i].chrscore.assignedMissions;
			chr->score.rank = updateCharacterArray[i].chrscore.rank;
		}
		num = 0;
	} else {
		/* invalidate ucn in the array first */
		for (i = 0; i < MAX_WHOLETEAM; i++) {
			updateCharacterArray[i].ucn = -1;
		}
		/* number of soldiers */
		num = NET_ReadByte(msg);
		if (num > MAX_WHOLETEAM)
			Sys_Error("CL_ParseCharacterData: num exceeded MAX_WHOLETEAM\n");
		else if (num < 0)
			Sys_Error("CL_ParseCharacterData: NET_ReadShort error (%i)\n", num);

		for (i = 0; i < num; i++) {
			/* updateCharacter_t */
			updateCharacterArray[i].ucn = NET_ReadShort(msg);
			updateCharacterArray[i].HP = NET_ReadShort(msg);
			updateCharacterArray[i].STUN = NET_ReadByte(msg);
			updateCharacterArray[i].morale = NET_ReadByte(msg);

			/** Scores @sa inv_shared.h:chrScoreGlobal_t */
			for (j = 0; j < SKILL_NUM_TYPES + 1; j++)
				updateCharacterArray[i].chrscore.experience[j] = NET_ReadLong(msg);
			for (j = 0; j < SKILL_NUM_TYPES; j++)
				updateCharacterArray[i].chrscore.skills[j] = NET_ReadByte(msg);
			for (j = 0; j < KILLED_NUM_TYPES; j++)
				updateCharacterArray[i].chrscore.kills[j] = NET_ReadShort(msg);
			for (j = 0; j < KILLED_NUM_TYPES; j++)
				updateCharacterArray[i].chrscore.stuns[j] = NET_ReadShort(msg);
			updateCharacterArray[i].chrscore.assignedMissions = NET_ReadShort(msg);
			updateCharacterArray[i].chrscore.rank = NET_ReadByte(msg);
		}
	}
}

/**
 * @brief Reads mission result data from server
 * @sa EV_RESULTS
 * @sa G_EndGame
 * @sa GAME_CP_Results_f
 */
void CL_ParseResults (struct dbuffer *msg)
{
	int winner;
	int i, j, num;
	int num_spawned[MAX_TEAMS];
	int num_alive[MAX_TEAMS];
	int num_kills[MAX_TEAMS][MAX_TEAMS];
	int num_stuns[MAX_TEAMS][MAX_TEAMS];

	memset(num_spawned, 0, sizeof(num_spawned));
	memset(num_alive, 0, sizeof(num_alive));
	memset(num_kills, 0, sizeof(num_kills));
	memset(num_stuns, 0, sizeof(num_stuns));

	/* get number of teams */
	num = NET_ReadByte(msg);
	if (num > MAX_TEAMS)
		Sys_Error("Too many teams in result message\n");

	Com_DPrintf(DEBUG_CLIENT, "Receiving results with %i teams.\n", num);

	/* get winning team */
	winner = NET_ReadByte(msg);

	if (cls.team > num)
		Sys_Error("Team number %d too high (only %d teams)\n", cls.team, num);

	/* get spawn and alive count */
	for (i = 0; i < num; i++) {
		num_spawned[i] = NET_ReadByte(msg);
		num_alive[i] = NET_ReadByte(msg);
	}

	/* get kills */
	for (i = 0; i < num; i++)
		for (j = 0; j < num; j++)
			num_kills[i][j] = NET_ReadByte(msg);

	/* get stuns */
	for (i = 0; i < num; i++)
		for (j = 0; j < num; j++)
			num_stuns[i][j] = NET_ReadByte(msg);

	CL_ParseCharacterData(msg);

	GAME_HandleResults(winner, num_spawned, num_alive, num_kills, num_stuns);
}

