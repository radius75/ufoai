/**
 * @file inv_shared.c
 * @brief Common object-, inventory-, container- and firemode-related functions.
 * @note Shared inventory management functions prefix: INVSH_
 * @note Shared firemode management functions prefix: FIRESH_
 * @note Shared character generating functions prefix: CHRSH_
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

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "inv_shared.h"
#include "../shared/shared.h"

/*================================
INVENTORY MANAGEMENT FUNCTIONS
================================*/

static csi_t *CSI;
static invList_t *invUnused;
static item_t cacheItem = {NONE_AMMO, NULL, NULL, 0, 0}; /* to crash as soon as possible */

/**
 * @brief Initializes csi_t *CSI pointer.
 * @param[in] import
 * @sa G_Init
 * @sa Com_ParseScripts
 */
void INVSH_InitCSI (csi_t * import)
{
	CSI = import;
}

/**
 * @brief Get the fire defintion for a given object
 * @param[in] obj The object to get the firedef for
 * @param[in] weapFdsIdx
 * @param[in] fdIdx
 * @return Will never return NULL
 */
inline const fireDef_t* FIRESH_GetFiredef (const objDef_t *obj, const int weapFdsIdx, const int fdIdx)
{
#ifdef DEBUG
	if (!obj) \
		Sys_Error("FIRESH_GetFiredef: no obj given.\n");
	if (weapFdsIdx < 0 || weapFdsIdx >= MAX_WEAPONS_PER_OBJDEF)
		Sys_Error("FIRESH_GetFiredef: weapFdsIdx out of bounds [%i]\n", weapFdsIdx);
	if (fdIdx < 0 || fdIdx >= MAX_FIREDEFS_PER_WEAPON)
		Sys_Error("FIRESH_GetFiredef: fdIdx out of bounds [%i]\n", fdIdx);
#endif
	return &obj->fd[weapFdsIdx & (MAX_WEAPONS_PER_OBJDEF-1)][fdIdx & (MAX_FIREDEFS_PER_WEAPON-1)];
}

/**
 * @brief Inits the inventory definition by linking the ->next pointers properly.
 * @param[in] invList Pointer to invList_t definition being inited.
 * @sa G_Init
 * @sa CL_ResetSinglePlayerData
 * @sa CL_InitLocal
 */
void INVSH_InitInventory (invList_t * invList)
{
	int i;

	assert(invList);

	invUnused = invList;
	/* first entry doesn't have an ancestor: invList[0]->next = NULL */
	invUnused->next = NULL;

	/* now link the invList_t next pointers
	 * invList[1]->next = invList[0]
	 * invList[2]->next = invList[1]
	 * invList[3]->next = invList[2]
	 * ... and so on
	 */
	for (i = 0; i < MAX_INVLIST - 1; i++) {
		invList_t *last = invUnused++;
		invUnused->next = last;
	}
}

static int cache_Com_CheckToInventory = INV_DOES_NOT_FIT;

/**
 * @brief Will check if the item-shape is colliding with something else in the container-shape at position x/y.
 * @note The function expects an already rotated shape for itemShape. Use Com_ShapeRotate if needed.
 * @param[in] shape Pointer to 'uint32_t shape[SHAPE_BIG_MAX_HEIGHT]'
 * @param[in] itemShape
 * @param[in] x
 * @param[in] y
 */
static qboolean Com_CheckShapeCollision (const uint32_t *shape, const uint32_t itemShape, const int x, const int y)
{
	int i;

	/* Negative positions not allowed (all items are supposed to have at least one bit set in the first row and column) */
	if (x < 0 || y < 0) {
		Com_DPrintf(DEBUG_SHARED, "Com_CheckShapeCollision: x or y value negative: x=%i y=%i!\n", x, y);
		return qtrue;
	}

	for (i = 0; i < SHAPE_SMALL_MAX_HEIGHT; i++) {
		const int itemRow = (itemShape >> (i * SHAPE_SMALL_MAX_WIDTH)) & 0xFF; /**< 0xFF is the length of one row in a "small shape" i.e. SHAPE_SMALL_MAX_WIDTH */
		const uint32_t itemRowShifted = itemRow << x;	/**< Result has to be limited to 32bit (SHAPE_BIG_MAX_WIDTH) */

		/* Check x maximum. */
		if (itemRowShifted >> x != itemRow)
			/* Out of bounds (32bit; a few bits of this row in itemShape were truncated) */
			return qtrue;

		/* Check y maximum. */
		if (y + i >= SHAPE_BIG_MAX_HEIGHT
		 && itemRow)
			/* This row (row "i" in itemShape) is outside of the max. bound and has bits in it. */
			return qtrue;


		/* Check for collisions of the item with the big mask. */
		if (itemRowShifted & shape[y + i])
			return qtrue;
	}

	return qfalse;
}

/**
 * @brief Checks if an item-shape can be put into a container at a certain position... ignores any 'special' types.
 * @param[in] i
 * @param[in] container The container (index) to look into.
 * @param[in] itemShape The shape info of an item to fit into the container.
 * @param[in] x The x value in the container (1 << x in the shape bitmask)
 * @param[in] y The x value in the container (SHAPE_BIG_MAX_HEIGHT is the max)
 * @sa Com_CheckToInventory
 * @return qfalse if the item does not fit, qtrue if it fits.
 */
static qboolean Com_CheckToInventory_shape (const inventory_t * const i, const invDef_t * container, const uint32_t itemShape, const int x, const int y)
{
	int j;
	invList_t *ic;
	static uint32_t mask[SHAPE_BIG_MAX_HEIGHT];

	assert(container);

	/* check bounds */
	if (x < 0 || y < 0 || x >= SHAPE_BIG_MAX_WIDTH || y >= SHAPE_BIG_MAX_HEIGHT)
		return qfalse;

	if (!cache_Com_CheckToInventory) {
		/* extract shape info */
		for (j = 0; j < SHAPE_BIG_MAX_HEIGHT; j++)
			mask[j] = ~container->shape[j];

		/* Add other items to mask. (i.e. merge their shapes at their location into the generated mask) */
		for (ic = i->c[container->id]; ic; ic = ic->next) {
			if (ic->item.rotated)
				Com_MergeShapes(mask, Com_ShapeRotate(ic->item.t->shape), ic->x, ic->y);
			else
				Com_MergeShapes(mask, ic->item.t->shape, ic->x, ic->y);
		}
	}

	/* Test for collisions with newly generated mask. */
	if (Com_CheckShapeCollision(mask, itemShape, x, y))
		return qfalse;

	/* Everything ok. */
	return qtrue;
}

/**
 * @param[in] i The inventory to check the item in.
 * @param[in] od The item to check in the inventory.
 * @param[in] container The index of the container in the inventory to theck the item in.
 * @param[in] x The x value in the container (1 << x in the shape bitmask)
 * @param[in] y The x value in the container (SHAPE_BIG_MAX_HEIGHT is the max)
 * @sa Com_CheckToInventory_mask
 * @return INV_DOES_NOT_FIT if the item does not fit
 * @return INV_FITS if it fits and
 * @return INV_FITS_ONLY_ROTATED if it fits only when rotated 90 degree (to the left).
 * @return INV_FITS_BOTH if it fits either normally or when rotated 90 degree (to the left).
 */
int Com_CheckToInventory (const inventory_t * const i, const objDef_t *od, const invDef_t * container, const int x, const int y)
{
	int fits;
	assert(i);
	assert(container);
	assert(od);

	/* armour vs item */
	if (!Q_strncmp(od->type, "armour", MAX_VAR)) {
		if (!container->armour && !container->all) {
			return INV_DOES_NOT_FIT;
		}
	} else if (!od->extension && container->extension) {
		return INV_DOES_NOT_FIT;
	} else if (!od->headgear && container->headgear) {
		return INV_DOES_NOT_FIT;
	} else if (container->armour) {
		return INV_DOES_NOT_FIT;
	}

	/* twohanded item */
	if (od->holdTwoHanded) {
		if ((container->id == CSI->idRight && i->c[CSI->idLeft])
			 || container->id == CSI->idLeft)
			return INV_DOES_NOT_FIT;
	}

	/* left hand is busy if right wields twohanded */
	if (container->id == CSI->idLeft) {
		if (i->c[CSI->idRight] && i->c[CSI->idRight]->item.t->holdTwoHanded)
			return INV_DOES_NOT_FIT;

		/* can't put an item that is 'fireTwoHanded' into the left hand */
		if (od->fireTwoHanded)
			return INV_DOES_NOT_FIT;
	}

	/* Single item containers, e.g. hands, extension or headgear. */
	if (container->single) {
		if (i->c[container->id]) {
			/* There is already an item. */
			return INV_DOES_NOT_FIT;
		} else {
			fits = INV_DOES_NOT_FIT; /* equals 0 */

			if (Com_CheckToInventory_shape(i, container, od->shape, x, y))
				fits |= INV_FITS;
			if (Com_CheckToInventory_shape(i, container, Com_ShapeRotate(od->shape), x, y))
				fits |= INV_FITS_ONLY_ROTATED;

			if (fits != INV_DOES_NOT_FIT)
				return fits;	/**< Return INV_FITS_BOTH if both if statements where true above. */

			Com_DPrintf(DEBUG_SHARED, "Com_CheckToInventory: INFO: Moving to 'single' container but item would not fit normally.\n");
			return INV_FITS; /**< We are returning with status qtrue (1) if the item does not fit at all - unlikely but not impossible. */
		}
	}

	/* Check 'grid' containers. */
	fits = INV_DOES_NOT_FIT; /* equals 0 */
	if (Com_CheckToInventory_shape(i, container, od->shape, x, y))
		fits |= INV_FITS;
	if (Com_CheckToInventory_shape(i, container, Com_ShapeRotate(od->shape), x, y)
		&& container->id != CSI->idEquip
		&& container->id != CSI->idFloor)
		fits |= INV_FITS_ONLY_ROTATED;

	return fits;	/**< Return INV_FITS_BOTH if both if statements where true above. */
}

/**
 * @brief Check if the (physical) information of 2 items is exactly the same.
 * @param[in] item1 First item to compare.
 * @param[in] item2 Second item to compare.
 * @return qtrue if they are identical or qfalse otherwise.
 */
qboolean Com_CompareItem (item_t *item1, item_t *item2)
{
	if ((item1->t == item2->t)
	 && (item1->m == item2->m)
	 && (item1->a == item2->a))
		return qtrue;

	return qfalse;
}

/**
 * @brief Check if a position in a conteiner is used by an item (i.e. collides with the shape).
 * @param[in] ic A pointer to an invList_t struct. The position is checked against its contained item.
 * @param[in] x The x location in the container.
 * @param[in] y The x location in the container.
 */
static qboolean Com_ShapeCheckPosition(const invList_t *ic, const int x, const int y)
{
	assert(ic);

	/* Check if the given location is inside the (max) bounds of the items. */
	if (x >= ic->x
	 && y >= ic->y
	 && x < ic->x + SHAPE_SMALL_MAX_WIDTH
	 && y < ic->y + SHAPE_SMALL_MAX_HEIGHT) {
	 	/* Check if the position is inside the shape (depending on rotation value) of the item. */
		if (ic->item.rotated) {
	 		if (((Com_ShapeRotate(ic->item.t->shape) >> (x - ic->x) >> (y - ic->y) * SHAPE_SMALL_MAX_WIDTH)) & 1)
	 			return qtrue;
		} else {
	 		if (((ic->item.t->shape >> (x - ic->x) >> (y - ic->y) * SHAPE_SMALL_MAX_WIDTH)) & 1)
	 			return qtrue;
	 	}
	}

	/* Position is out of bounds or position not inside item-shape. */
	return qfalse;
}

#if 0
/**
 * @brief Calculates the first "true" bit in the shape and returns its position in the container.
 * @note Use this got get the first "grab-able" grid-location of an item.
 * @param[in] ic A pointer to an invList_t struct.
 * @param[in] x The x location in the container.
 * @param[in] y The x location in the container.
 * @sa Com_CheckToInventory
 */
void Com_GetFirstShapePosition (const invList_t *ic, int* const x, int* const y)
{
	int tempX, tempY;
	int checkedTo = 0;

	assert(ic);

	for (tempX = 0; tempX < SHAPE_SMALL_MAX_WIDTH; tempY++)
		for (tempY = 0; tempY < SHAPE_SMALL_MAX_HEIGHT; tempX++)
			if (Com_ShapeCheckPosition(ic, tempX, tempY) {
				*px = tempX;
				*py = tempY;
				return;
			}

	*px = *py = NONE;
}
#endif

/**
 * @brief Searches if there is a specific item already in the inventory&container.
 * @param[in] inv Pointer to the inventory where we will search.
 * @param[in] container Container in the inventory.
 * @param[in] item The item to search for.
 * @return qtrue if there already is at least one item of this type, otherwise qfalse.
 */
qboolean Com_ExistsInInventory (const inventory_t* const inv, const invDef_t * container, item_t item)
{
	invList_t *ic;

	for (ic = inv->c[container->id]; ic; ic = ic->next)
		if (Com_CompareItem(&ic->item, &item)) {
			return qtrue;
		}

	return qfalse;
}

/**
 * @brief Searches if there is an item at location (x,y) in a container.
 * @param[in] i Pointer to the inventory where we will search.
 * @param[in] container Container in the inventory.
 * @param[in] x X position that you want to check.
 * @param[in] y Y position that you want to check.
 * @return invList_t Pointer to the container in given inventory, where we can place new item.
 */
invList_t *Com_SearchInInventory (const inventory_t* const i, const invDef_t * container, int x, int y)
{
	invList_t *ic;

	assert(container);

	/* Only a single item. */
	if (container->single)
		return i->c[container->id];

	/* More than one item - search for the item that is located at location x/y in this container. */
	for (ic = i->c[container->id]; ic; ic = ic->next)
		if (Com_ShapeCheckPosition(ic, x ,y))
			return ic;

	/* Found nothing */
	return NULL;
}


/**
 * @brief Add an item to a specified container in a given inventory.
 * @note Set x and y to NONE if the item should get added but not displayed.
 * @param[in] i Pointer to inventory definition, to which we will add item.
 * @param[in] item Item to add to given container (needs to have "rotated" tag already set/checked, this is NOT checked here!)
 * @param[in] container Container in given inventory definition, where the new item will be stored.
 * @param[in] x The x location in the container.
 * @param[in] y The x location in the container.
 * @param[in] amount How many items of this type should be added. (this will overwrite the amount as defined in "item.amount")
 * @sa Com_RemoveFromInventory
 * @sa Com_RemoveFromInventoryIgnore
 */
invList_t *Com_AddToInventory (inventory_t * const i, item_t item, const invDef_t * container, int x, int y, int amount)
{
	invList_t *ic;

	if (!item.t)
		return NULL;

	if (!invUnused)
		Sys_Error("Com_AddToInventory: No free inventory space!\n");

	if (amount <= 0)
		return NULL;

	assert(i);
	assert(container);

	if (x < 0 || y < 0 || x >= SHAPE_BIG_MAX_WIDTH || y >= SHAPE_BIG_MAX_HEIGHT) {
		/* No (sane) position in container given as parameter - find free space on our own. */
		Com_FindSpace(i, &item, container, &x, &y);
	}

	/**
	 * What we are doing here.
	 * invList_t array looks like that: [u]->next = [w]; [w]->next = [x]; [...]; [z]->next = NULL.
	 * i->c[container->id] as well as ic are such invList_t.
	 * Now we want to add new item to this container and that means, we need to create some [t]
	 * and make sure, that [t]->next points to [u] (so the [t] will be the first in array).
	 * ic = i->c[container->id];
	 * So, we are storing old value of i->c[container->id] in ic to remember what was in the original
	 * container. If the i->c[container->id]->next pointed to [abc], the ic->next will also point to [abc].
	 * The ic is our [u] and [u]->next still points to our [w].
	 * i->c[container->id] = invUnused;
	 * Now we are creating new container - the "original" i->c[container->id] is being set to empty invUnused.
	 * This is our [t].
	 * invUnused = invUnused->next;
	 * Now we need to make sure, that our [t] will point to next free slot in our inventory. Remember, that
	 * invUnused was empty before, so invUnused->next will point to next free slot.
	 * i->c[container->id]->next = ic;
	 * We assigned our [t]->next to [u] here. Thanks to that we still have the correct ->next chain in our
	 * inventory list.
	 * ic = i->c[container->id];
	 * And now ic will be our [t], that is the newly added container.
	 * After that we can easily add the item (item.t, x and y positions) to our [t] being ic.
	 */

	/* idEquip and idFloor */
	/** @sa MN_DrawMenus -> MN_CONTAINER */
	if (container->temp) {
		for (ic = i->c[container->id]; ic; ic = ic->next)
			if (Com_CompareItem(&ic->item, &item)) {
				ic->item.amount += amount;
				Com_DPrintf(DEBUG_SHARED, "Com_AddToInventory: Amount of '%s': %i\n",
					ic->item.t->name, ic->item.amount);
				return ic;
			}
	}

	/* Temporary store the pointer to the first item in this list. */
	/* not found - add a new one */
	ic = i->c[container->id];

	/* Set/overwrite first item-entry in the container to a yet empty one. */
	i->c[container->id] = invUnused;

	/* Ensure, that for later usage in other (or this) function(s) invUnused will be the next empty/free slot.
	 * It is not used _here_ anymore. */
	invUnused = invUnused->next;

	/* Set the "next" link of the new "first item"  to the original "first item" we stored previously. */
	i->c[container->id]->next = ic;

	/* Set point ic to the new "first item" (i.e. the yet empty entry). */
	ic = i->c[container->id];
/*	Com_Printf("Add to container %i: %s\n", container, item.t->id);*/

	/* Set the data in the new entry to the data we got via function-parameters.*/
	ic->item = item;
	ic->item.amount = amount;
	ic->x = x;
	ic->y = y;

	if (container->single && i->c[container->id]->next)
		Com_Printf("Com_AddToInventory: Error: single container %s has many items.\n", container->name);

	return ic;
}

/**
 * @param[in] i The inventory the container is in.
 * @param[in] container The container where the item should be removed.
 * @param[in] x The x position of the item to be removed.
 * @param[in] y The y position of the item to be removed.
 * @return qtrue If removal was successful.
 * @return qfalse If nothing was removed or an error occured.
 * @sa Com_RemoveFromInventoryIgnore
 * @sa Com_AddToInventory
 */
qboolean Com_RemoveFromInventory (inventory_t* const i, const invDef_t * container, int x, int y)
{
	return Com_RemoveFromInventoryIgnore(i, container, x, y, qfalse);
}

/**
 * @param[in] i The inventory the container is in.
 * @param[in] container The container where the item should be removed.
 * @param[in] x The x position of the item to be removed.
 * @param[in] y The y position of the item to be removed.
 * @param[in] ignore_type Ignores the type of container (only used for a workaround in the base-equipment see CL_MoveMultiEquipment) HACKHACK
 * @return qtrue If removal was successful.
 * @return qfalse If nothing was removed or an error occured.
 * @sa Com_RemoveFromInventory
 */
qboolean Com_RemoveFromInventoryIgnore (inventory_t* const i, const invDef_t * container, int x, int y, qboolean ignore_type)
{
	invList_t *ic, *oldUnused, *previous;

	assert(i);
	assert(container);

	ic = i->c[container->id];
	if (!ic) {
#ifdef PARANOID
		Com_DPrintf(DEBUG_SHARED, "Com_RemoveFromInventoryIgnore - empty container %s\n", container->name);
#endif
		return qfalse;
	}

	/** @todo the problem here is, that in case of a move inside the same container
	 * the item don't just get updated x and y values but it is tried to remove
	 * one of the items => crap - maybe we have to change the inv move function
	 * to check for this case of move and only update the x and y coords instead
	 * of calling the add and remove functions */
	if (!ignore_type && (container->single || (ic->x == x && ic->y == y))) {
		cacheItem = ic->item;
		/* temp container like idEquip and idFloor */
		if (container->temp && ic->item.amount > 1) {
			ic->item.amount--;
			Com_DPrintf(DEBUG_SHARED, "Com_RemoveFromInventoryIgnore: Amount of '%s': %i\n",
				ic->item.t->name, ic->item.amount);
			return qtrue;
		}
		/* an item in other containers as idFloor and idEquip should always
		 * have an amount value of 1 */
		assert(ic->item.amount == 1);
		oldUnused = invUnused;
		invUnused = ic;
		i->c[container->id] = ic->next;

		if (container->single && ic->next)
			Com_Printf("Com_RemoveFromInventoryIgnore: Error: single container %s has many items.\n", container->name);

		invUnused->next = oldUnused;
		return qtrue;
	}

	for (previous = i->c[container->id]; ic; ic = ic->next) {
		if (Com_ShapeCheckPosition(ic, x ,y)) {
			cacheItem = ic->item;
			/* temp container like idEquip and idFloor */
			if (!ignore_type && ic->item.amount > 1 && container->temp) {
				ic->item.amount--;
				Com_DPrintf(DEBUG_SHARED, "Com_RemoveFromInventoryIgnore: Amount of '%s': %i\n",
					ic->item.t->name, ic->item.amount);
				return qtrue;
			}
			oldUnused = invUnused;
			invUnused = ic;
			if (ic == i->c[container->id]) {
				i->c[container->id] = i->c[container->id]->next;
			} else {
				previous->next = ic->next;
			}
			invUnused->next = oldUnused;
			return qtrue;
		}
		previous = ic;
	}
	return qfalse;
}

/**
 * @brief Conditions for moving items between containers.
 * @param[in] i an item
 * @param[in] from source container
 * @param[in] fx x for source container
 * @param[in] fy y for source container
 * @param[in] to destination container
 * @param[in] tx x for destination container
 * @param[in] ty y for destination container
 * @param[in] TU amount of TU needed to move an item
 * @param[out] icp
 * @return IA_NOTIME when not enough TU
 * @return IA_NONE if no action possible
 * @return IA_NORELOAD if you cannot reload a weapon
 * @return IA_RELOAD_SWAP in case of exchange of ammo in a weapon
 * @return IA_RELOAD when reloading
 * @return IA_ARMOUR when placing an armour on the actor
 * @return IA_MOVE when just moving an item
 */
int Com_MoveInInventory (inventory_t* const i, const invDef_t * from, int fx, int fy, const invDef_t * to, int tx, int ty, int *TU, invList_t ** icp)
{
	return Com_MoveInInventoryIgnore(i, from, fx, fy, to, tx, ty, TU, icp, qfalse);
}

/**
 * @brief Conditions for moving items between containers.
 * @param[in] i
 * @param[in] from source container
 * @param[in] fx x for source container (This is actually the mouse coordinate, not the origin of the item.)
 * @param[in] fy y for source container (This is actually the mouse coordinate. not the origin of the item.)
 * @param[in] to destination container
 * @param[in] tx x for destination container
 * @param[in] ty y for destination container
 * @param[in] TU amount of TU needed to move an item
 * @param[out] icp
 * @param[in] ignore_type Ignores the type of container (only used for a workaround in the base-equipemnt see CL_MoveMultiEquipment) HACKHACK
 * @return IA_NOTIME when not enough TU
 * @return IA_NONE if no action possible
 * @return IA_NORELOAD if you cannot reload a weapon
 * @return IA_RELOAD_SWAP in case of exchange of ammo in a weapon
 * @return IA_RELOAD when reloading
 * @return IA_ARMOUR when placing an armour on the actor
 * @return IA_MOVE when just moving an item
 */
int Com_MoveInInventoryIgnore (inventory_t* const i, const invDef_t * from, int fx, int fy, const invDef_t * to, int tx, int ty, int *TU, invList_t ** icp, qboolean ignore_type)
{
	invList_t *ic;
	const invList_t *icFrom;
	int cacheFromX, cacheFromY;
	int time;
	int checkedTo = INV_DOES_NOT_FIT;

	assert(to);
	assert(from);

	if (icp)
		*icp = NULL;

	if (from == to && fx == tx && fy == ty)
		return IA_NONE;

	time = from->out + to->in;
	if (from == to)
		time /= 2;
	if (TU && *TU < time)
		return IA_NOTIME;

	assert(i);

	/* Break if source item is not removeable. */

	/** @todo imo in tactical missions (idFloor) there should be not packing of items
	 * they should be available one by one */

	/* special case for moving an item within the same container */
	if (from == to) {
		ic = i->c[from->id];
		for (; ic; ic = ic->next) {
			if (ic->x == fx && ic->y == fy) {
				if (ic->item.amount > 1) {
					checkedTo = Com_CheckToInventory(i, ic->item.t, to, tx, ty);
					if (checkedTo & INV_FITS) {
						ic->x = tx;
						ic->y = ty;
						if (icp)
							*icp = ic;
						return IA_MOVE;
					}
					return IA_NONE;
				}
			}
		}
	}

	/** Store x/y origin coordinates of removed (source) item. If we need to
	 * re-add it we can use this. We can't use fx/fy in that case, because it's
	 * the mouse coordinate, not the item-origin. */
	icFrom = Com_SearchInInventory(i, from, fx, fy);	/* Get the source-invlist (e.g. a weapon) */
	if (icFrom) {
		cacheFromX = icFrom->x;
		cacheFromY = icFrom->y;
	} else {
		cacheFromX = -1;
		cacheFromY = -1;
	}

	/* Actually remove the source item from its container. */
	if (!Com_RemoveFromInventoryIgnore(i, from, fx, fy, ignore_type))
		return IA_NONE;

	if (!cacheItem.t)
		return IA_NONE;

	/* We are in base-equip screen (multi-ammo workaround) and can skip a lot of checks. */
	if (ignore_type) {
		Com_TryAddToBuyType(i, cacheItem, to->id, cacheItem.amount); /* get target coordinates */
		/* return data */
		/*if (icp)
			*icp = ic;*/
		return IA_NONE;
	}

	/* If weapon is twohanded and is moved from hand to hand do nothing. */
	/* Twohanded weapon are only in CSI->idRight. */
	if (cacheItem.t->fireTwoHanded && to->id == CSI->idLeft && from->id == CSI->idRight) {
		Com_AddToInventory(i, cacheItem, from, cacheFromX, cacheFromY, 1);
		return IA_NONE;
	}

	/* If non-armour moved to an armour slot then
	 * move item back to source location and break.
	 * Same for non extension items when moved to an extension slot. */
	if ((to->armour && Q_strcmp(cacheItem.t->type, "armour"))
	 || (to->extension && !cacheItem.t->extension)
	 || (to->headgear && !cacheItem.t->headgear)) {
		Com_AddToInventory(i, cacheItem, from, cacheFromX, cacheFromY, 1);
		return IA_NONE;
	}

	/* Check if the target is a blocked inv-armour and source!=dest. */
	if (to->single)
		checkedTo = Com_CheckToInventory(i, cacheItem.t, to, 0, 0);
	else
		checkedTo = Com_CheckToInventory(i, cacheItem.t, to, tx, ty);

	if (to->armour && from != to && !checkedTo) {
		const item_t cacheItem2 = cacheItem; /* Save/cache (source) item. */

		/* Move the destination item to the source */
		Com_MoveInInventory(i, to, tx, ty, from, cacheFromX, cacheFromY, TU, icp);

		/* Reset the cached item (source) (It'll be move to container emptied by destination item later.) */
		cacheItem = cacheItem2;
	} else if (!checkedTo) {
		ic = Com_SearchInInventory(i, to, tx, ty);	/* Get the target-invlist (e.g. a weapon) */

		if (ic && INVSH_LoadableInWeapon(cacheItem.t, ic->item.t)) {
			/* A target-item was found and the dragged item (implicitly ammo)
			 * can be loaded in it (implicitly weapon). */

			/** @todo (or do this in two places in cl_menu.c):
			if (!RS_IsResearched_ptr(ic->item.t->tech)
				 || !RS_IsResearched_ptr(cacheItem.t->tech)) {
				return IA_NORELOAD;
			} */
			if (ic->item.a >= ic->item.t->ammo
				&& ic->item.m == cacheItem.t) {
				/* Weapon already fully loaded with the same ammunition.
				 * => back to source location. */
				Com_AddToInventory(i, cacheItem, from, cacheFromX, cacheFromY, 1);
				return IA_NORELOAD;
			}
			time += ic->item.t->reload;
			if (!TU || *TU >= time) {
				if (TU)
					*TU -= time;
				if (ic->item.a >= ic->item.t->ammo) {
					/* exchange ammo */
					item_t item = {NONE_AMMO, NULL, ic->item.m, 0, 0};

					/* Add the currently used ammo in a free place of the "from" container. */
					/**
					 * @note If 'from' is idEquip OR idFloor AND the item is of buytype BUY_MULTI_AMMO:
					 * We need to make sure it goes into the correct display: PRIMARY _OR_ SECONDARY only (e.g. no saboted slugs in SEC).
					 * This is currently done via the IA_RELOAD_SWAP return-value in cl_inventory.c:INV_MoveItem (ONLY there).
					 * It checks and moved the last-added item (1 item) only.
					 * @sa The BUY_MULTI_AMMO stuff in cl_inventory.c:INV_MoveItem.
					 */
					Com_AddToInventory(i, item, from, -1, -1, 1);

					ic->item.m = cacheItem.t;
					if (icp)
						*icp = ic;
					return IA_RELOAD_SWAP;
				} else {
					ic->item.m = cacheItem.t;
					/* loose ammo of type ic->item.m saved on server side */
					ic->item.a = ic->item.t->ammo;
					if (icp)
						*icp = ic;
					return IA_RELOAD;
				}
			}
			/* not enough time - back to source location */
			Com_AddToInventory(i, cacheItem, from, cacheFromX, cacheFromY, 1);
			return IA_NOTIME;
		}

		/* temp container like idEquip and idFloor */
		if (ic && to->temp) {
			/** We are moving to a blocked location container but it's the base-equipment loor of a battlsecape floor
			 * We add the item anyway but it'll not be displayed (yet)
			 * @todo change the other code to browse trough these things. */
			Com_FindSpace(i, &cacheItem, to, &tx, &ty);	/**< Returns a free place or NONE for x&y if no free space is available elsewhere.
												 * This is then used in Com_AddToInventory below.*/
			if (tx == NONE || ty == NONE) {
				Com_DPrintf(DEBUG_SHARED, "Com_MoveInInventory - item will be added non-visible\n");
			}
		} else {
			/* impossible move - back to source location */
			Com_AddToInventory(i, cacheItem, from, cacheFromX, cacheFromY, 1);
			return IA_NONE;
		}
	}

	/* twohanded exception - only CSI->idRight is allowed for fireTwoHanded weapons */
	if (cacheItem.t->fireTwoHanded && to->id == CSI->idLeft) {
#ifdef DEBUG
		Com_DPrintf(DEBUG_SHARED, "Com_MoveInInventory - don't move the item to CSI->idLeft it's fireTwoHanded\n");
#endif
		to = &CSI->ids[CSI->idRight];
	}
#ifdef PARANOID
	else if (cacheItem.t->fireTwoHanded)
		Com_DPrintf(DEBUG_SHARED, "Com_MoveInInventory: move fireTwoHanded item to container: %s\n", to->name);
#endif

	/* successful */
	if (TU)
		*TU -= time;

	if (checkedTo == INV_FITS_ONLY_ROTATED) {
		/* Set rotated tag */
		Com_DPrintf(DEBUG_SHARED, "Com_MoveInInventoryIgnore: setting rotate tag.\n");
		cacheItem.rotated = qtrue;
	} else if (cacheItem.rotated) {
		/* Remove rotated tag */
		Com_DPrintf(DEBUG_SHARED, "Com_MoveInInventoryIgnore: removing rotate tag.\n");
		cacheItem.rotated = qfalse;
	}

	assert(cacheItem.t);
	ic = Com_AddToInventory(i, cacheItem, to, tx, ty, 1);

	/* return data */
	if (icp) {
		assert(ic);
		*icp = ic;
	}

	if (to->id == CSI->idArmour) {
		assert(!Q_strcmp(cacheItem.t->type, "armour"));
		return IA_ARMOUR;
	} else
		return IA_MOVE;
}

/**
 * @brief Clears the linked list of a container - removes all items from this container.
 * @param[in] i The inventory where the container is located.
 * @param[in] container Index of the container which will be cleared.
 * @sa INVSH_DestroyInventory
 * @note This should only be called for temp containers if the container is really a temp container
 * e.g. the container of a dropped weapon in tactical mission (ET_ITEM)
 * in every other case just set the pointer to NULL for a temp container like idEquip or idFloor
 */
void INVSH_EmptyContainer (inventory_t* const i, const invDef_t * container)
{
	invList_t *ic, *old;
#ifdef DEBUG
	int cnt = 0;

	assert(container);

	if (container->temp)
		Com_DPrintf(DEBUG_SHARED, "INVSH_EmptyContainer: Emptying temp container %s.\n", container->name);
#endif

	assert(i);

	ic = i->c[container->id];

	while (ic) {
		old = ic;
		ic = ic->next;
		old->next = invUnused;
		invUnused = old;
#ifdef DEBUG
		if (cnt >= MAX_INVLIST) {
			Com_Printf("Error: There are more than the allowed entries in container %s (cnt:%i, MAX_INVLIST:%i) (INVSH_EmptyContainer)\n",
				container->name, cnt, MAX_INVLIST);
			break;
		}
		cnt++;
#endif
	}
	i->c[container->id] = NULL;
}

/**
 * @brief Destroys inventory.
 * @param[in] i Pointer to the invetory which should be erased.
 * @note Loops through all containers in inventory, NULL for temp containers and INVSH_EmptyContainer() call for real containers.
 * @sa INVSH_EmptyContainer
 */
void INVSH_DestroyInventory (inventory_t* const i)
{
	int k;

	if (!i)
		return;

	for (k = 0; k < CSI->numIDs; k++)
		if (CSI->ids[k].temp)
			i->c[k] = NULL;
		else
			INVSH_EmptyContainer(i, &CSI->ids[k]);
}

/**
 * @brief Finds space for item in inv at container
 * @param[in] inv The inventory to search space in
 * @param[in] item The item to check the space for
 * @param[in] container The container to search in
 * @param[out] px The x position in the container
 * @param[out] py The y position in the container
 * @sa Com_CheckToInventory
 */
void Com_FindSpace (const inventory_t* const inv, const item_t *item, const invDef_t * container, int* const px, int* const py)
{
	int x, y;
	int checkedTo = 0;

	assert(inv);
	assert(container);
	assert(!cache_Com_CheckToInventory);

	for (y = 0; y < SHAPE_BIG_MAX_HEIGHT; y++) {
		for (x = 0; x < SHAPE_BIG_MAX_WIDTH; x++) {
			checkedTo = Com_CheckToInventory(inv, item->t, container, x, y);
			if (checkedTo) {
				cache_Com_CheckToInventory = INV_DOES_NOT_FIT;
				*px = x;
				*py = y;
				return;
			} else {
				cache_Com_CheckToInventory = INV_FITS;
			}
		}
	}
	cache_Com_CheckToInventory = INV_DOES_NOT_FIT;

#ifdef PARANOID
	Com_DPrintf(DEBUG_SHARED, "Com_FindSpace: no space for %s: %s in %s\n", item->t->type, item->t->id, container->name);
#endif
	*px = *py = NONE;
}

/**
 * @brief Tries to add an item to a container (in the inventory inv).
 * @param[in] inv Inventory pointer to add the item.
 * @param[in] item Item to add to inventory.
 * @param[in] container Container id.
 * @sa Com_FindSpace
 * @sa Com_AddToInventory
 */
qboolean Com_TryAddToInventory (inventory_t* const inv, item_t item, const invDef_t * container)
{
	int x, y;

	Com_FindSpace(inv, &item, container, &x, &y);

	if (x == NONE) {
		assert(y == NONE);
		return qfalse;
	} else {
		const int checkedTo = Com_CheckToInventory(inv, item.t, container, x, y);
		if (!checkedTo)
			return qfalse;
		else if (checkedTo == INV_FITS_ONLY_ROTATED)
			item.rotated = qtrue;
		else
			item.rotated = qfalse;

		Com_AddToInventory(inv, item, container, x, y, 1);
		return qtrue;
	}
}

/**
 * @brief Tries to add item to buytype inventory inv at container
 * @param[in] inv Inventory pointer to add the item
 * @param[in] item Item to add to inventory
 * @param[in] buytypeContainer The "Container" id. The container-id is equal to the buytype in this case (buytype hack).
 * @param[in] amount How many items to add.
 * @sa Com_FindSpace
 * @sa Com_AddToInventory
 */
int Com_TryAddToBuyType (inventory_t* const inv, item_t item, int buytypeContainer, int amount)
{
	int x, y;
	inventory_t hackInv;

	if (amount <= 0)
		return 0;

	/* link the temp container */
	hackInv.c[CSI->idEquip] = inv->c[buytypeContainer];

	Com_FindSpace(&hackInv, &item, &CSI->ids[CSI->idEquip], &x, &y);
	if (x == NONE) {
		assert(y == NONE);
		return 0;
	} else {
		Com_AddToInventory(&hackInv, item, &CSI->ids[CSI->idEquip], x, y, amount);
		inv->c[buytypeContainer] = hackInv.c[CSI->idEquip];
		return 1;
	}
}

/**
 * @brief Debug function to print the inventory items for a given inventory_t pointer.
 * @param[in] i The inventory you want to see on the game console.
 */
void INVSH_PrintContainerToConsole (inventory_t* const i)
{
	int container;
	invList_t *ic;

	assert(i);

	for (container = 0; container < CSI->numIDs; container++) {
		ic = i->c[container];
		Com_Printf("Container: %i\n", container);
		while (ic) {
			Com_Printf(".. item.t: %i, item.m: %i, item.a: %i, x: %i, y: %i\n", (ic->item.t ? ic->item.t->idx : NONE), (ic->item.m ? ic->item.m->idx : NONE), ic->item.a, ic->x, ic->y);
			if (ic->item.t)
				Com_Printf(".... weapon: %s\n", ic->item.t->id);
			if (ic->item.m)
				Com_Printf(".... ammo:   %s (%i)\n", ic->item.m->id, ic->item.a);
			ic = ic->next;
		}
	}
}

#define AKIMBO_CHANCE		0.3
#define WEAPONLESS_BONUS	0.4
#define PROB_COMPENSATION   3.0

/**
 * @brief Pack a weapon, possibly with some ammo
 * @param[in] inv The inventory that will get the weapon
 * @param[in] weapon The weapon type index in gi.csi->ods
 * @param[in] equip The equipment that shows how many clips to pack
 * @param[in] name The name of the equipment for debug messages
 * @param[in] missedPrimary
 * @sa INVSH_LoadableInWeapon
 */
static int INVSH_PackAmmoAndWeapon (inventory_t* const inv, objDef_t* weapon, const int equip[MAX_OBJDEFS], int missedPrimary, const char *name)
{
	objDef_t *ammo;
	item_t item = {NONE_AMMO, NULL, NULL, 0, 0};
	int i, maxPrice, prevPrice;
	objDef_t *obj;
	qboolean allowLeft;
	qboolean packed;
	int ammoMult = 1;

#ifdef PARANOID
	if (weapon == NULL) {
		Com_Printf("Error in INVSH_PackAmmoAndWeapon - weapon is invalid\n");
	}
#endif

	assert(Q_strcmp(weapon->type, "armour"));
	item.t = weapon;

	/* are we going to allow trying the left hand */
	allowLeft = !(inv->c[CSI->idRight] && inv->c[CSI->idRight]->item.t->fireTwoHanded);

	if (!weapon->reload) {
		item.m = item.t; /* no ammo needed, so fire definitions are in t */
	} else {
		if (weapon->oneshot) {
			/* The weapon provides its own ammo (i.e. it is charged or loaded in the base.) */
			item.a = weapon->ammo;
			item.m = weapon;
			Com_DPrintf(DEBUG_SHARED, "INVSH_PackAmmoAndWeapon: oneshot weapon '%s' in equipment '%s'.\n", weapon->id, name);
		} else {
			maxPrice = 0;
			ammo = NULL;
			/* find some suitable ammo for the weapon */
			for (i = CSI->numODs - 1; i >= 0; i--) {
				objDef_t *ammoTemp = &CSI->ods[i];
				if (equip[i] && INVSH_LoadableInWeapon(ammoTemp, weapon)
				 && (ammoTemp->price > maxPrice)) {
					ammo = ammoTemp;
					maxPrice = ammoTemp->price;
				}
			}

			if (!ammo) {
				Com_DPrintf(DEBUG_SHARED, "INVSH_PackAmmoAndWeapon: no ammo for sidearm or primary weapon '%s' in equipment '%s'.\n", weapon->id, name);
				return 0;
			}
			/* load ammo */
			item.a = weapon->ammo;
			item.m = ammo;
		}
	}

	if (!item.m) {
		Com_Printf("INVSH_PackAmmoAndWeapon: no ammo for sidearm or primary weapon '%s' in equipment '%s'.\n", weapon->id, name);
		return 0;
	}

	/* now try to pack the weapon */
	packed = Com_TryAddToInventory(inv, item, &CSI->ids[CSI->idRight]);
	if (packed)
		ammoMult = 3;
	if (!packed && allowLeft)
		packed = Com_TryAddToInventory(inv, item, &CSI->ids[CSI->idLeft]);
	if (!packed)
		packed = Com_TryAddToInventory(inv, item, &CSI->ids[CSI->idBelt]);
	if (!packed)
		packed = Com_TryAddToInventory(inv, item, &CSI->ids[CSI->idHolster]);
	if (!packed)
		return 0;

	maxPrice = INT_MAX;
	do {
		/* search for the most expensive matching ammo in the equipment */
		prevPrice = maxPrice;
		maxPrice = 0;
		ammo = NULL;
		for (i = 0; i < CSI->numODs; i++) {
			obj = &CSI->ods[i];
			if (equip[i] && INVSH_LoadableInWeapon(obj, weapon)) {
				if (obj->price > maxPrice && obj->price < prevPrice) {
					maxPrice = obj->price;
					ammo = obj;
				}
			}
		}
		/* see if there is any */
		if (maxPrice) {
			int num;
			int numpacked = 0;

			/* how many clips? */
			num = min(
				equip[ammo->idx] / equip[weapon->idx]
				+ (equip[ammo->idx] % equip[weapon->idx] > rand() % equip[weapon->idx])
				+ (PROB_COMPENSATION > 40 * frand())
				+ (float) missedPrimary * (1 + frand() * PROB_COMPENSATION) / 40.0, 20);

			assert(num >= 0);
			/* pack some more ammo */
			while (num--) {
				item_t mun = {NONE_AMMO, NULL, NULL, 0, 0};

				mun.t = ammo;
				/* ammo to backpack; belt is for knives and grenades */
				numpacked += Com_TryAddToInventory(inv, mun, &CSI->ids[CSI->idBackpack]);
				/* no problem if no space left; one ammo already loaded */
				if (numpacked > ammoMult || numpacked*weapon->ammo > 11)
					break;
			}
		}
	} while (maxPrice);

	return qtrue;
}


/**
 * @brief Equip melee actor with item defined per teamDefs.
 * @param[in] inv The inventory that will get the weapon.
 * @param[in] chr Pointer to character data.
 * @note Weapons assigned here cannot be collected in any case. These are dummy "actor weapons".
 */
void INVSH_EquipActorMelee (inventory_t* const inv, character_t* chr)
{
	objDef_t *obj;
	item_t item;

	assert(chr);
	assert(!chr->weapons);
	assert(chr->teamDef);
	assert(chr->teamDef->onlyWeapon);

	/* Get weapon */
	obj = chr->teamDef->onlyWeapon;
	Com_DPrintf(DEBUG_SHARED, "INVSH_EquipActorMelee: team %i: %s, weapon: %s\n",
	chr->teamDef->idx, chr->teamDef->id, obj->id);

	/* Prepare item. This kind of item has no ammo, fire definitions are in item.t. */
	item.t = obj;
	item.m = item.t;
	/* Every melee actor weapon definition is firetwohanded, add to right hand. */
	if (!obj->fireTwoHanded)
		Sys_Error("INVSH_EquipActorMelee: melee weapon %s for team %s is not firetwohanded!\n", obj->id, chr->teamDef->id);
	Com_TryAddToInventory(inv, item, &CSI->ids[CSI->idRight]);
}

/**
 * @brief Equip robot actor with default weapon. (defined in ugv_t->weapon)
 * @param[in] inv The inventory that will get the weapon.
 * @param[in] chr Pointer to character data.
 */
void INVSH_EquipActorRobot (inventory_t* const inv, character_t* chr, objDef_t* weapon)
{
	item_t item;

	assert(chr);
	assert(weapon);
	assert(chr->emplType == EMPL_ROBOT);
	/** assert(!chr->weapons); @todo remove/change? */
	/** assert(chr->teamDef->onlyWeapon); @todo remove/change? */

	Com_DPrintf(DEBUG_SHARED, "INVSH_EquipActorMelee: team %i: %s, weapon %i: %s\n",
		chr->teamDef->idx, chr->teamDef->id, weapon->idx, weapon->id);

	/* Prepare weapon in item. */
	item.t = weapon;

	/* Get ammo for item/weapon. */
	assert(weapon->numAmmos > 0);	/**< There _has_ to be at least one ammo-type. */
	assert(weapon->ammos[0]);
	item.m = weapon->ammos[0];

	Com_TryAddToInventory(inv, item, &CSI->ids[CSI->idRight]);
}


/**
 * @brief Fully equip one actor
 * @param[in] inv The inventory that will get the weapon.
 * @param[in] equip The equipment that shows what is available - a list of obj ids.
 * @param[in] numEquip The number of object ids in the field.
 * @param[in] name The name of the equipment for debug messages.
 * @param[in] chr Pointer to character data - to get the weapon and armour bools.
 * @note The code below is a complete implementation
 * of the scheme sketched at the beginning of equipment_missions.ufo.
 * Beware: If two weapons in the same category have the same price,
 * only one will be considered for inventory.
 */
void INVSH_EquipActor (inventory_t* const inv, const int *equip, int numEquip, const char *name, character_t* chr)
{
	int i, maxPrice, prevPrice;
	int hasWeapon = 0, hasArmour = 0, repeat = 0, missedPrimary = 0;
	int primary = 2; /* 0 particle or normal, 1 other, 2 no primary weapon */
	objDef_t *obj;
	objDef_t *weapon;

	if (chr->weapons) {
		/* Primary weapons */
		maxPrice = INT_MAX;
		do {
			int lastPos = min(CSI->numODs - 1, numEquip - 1);
			/* Search for the most expensive primary weapon in the equipment. */
			prevPrice = maxPrice;
			maxPrice = 0;
			weapon = NULL;
			for (i = lastPos; i >= 0; i--) {
				obj = &CSI->ods[i];
				if (equip[i] && obj->weapon && BUY_PRI(obj->buytype) && obj->fireTwoHanded) {
					if (frand() < 0.15) { /* Small chance to pick any weapon. */
						weapon = obj;
						maxPrice = obj->price;
						lastPos = i - 1;
						break;
					} else if (obj->price > maxPrice && obj->price < prevPrice) {
						maxPrice = obj->price;
						weapon = obj;
						lastPos = i - 1;
					}
				}
			}
			/* See if there is any. */
			if (maxPrice) {
				/* See if the actor picks it. */
				if (equip[weapon->idx] >= (28 - PROB_COMPENSATION) * frand()) {
					/* Not decrementing equip[weapon]
					* so that we get more possible squads. */
					hasWeapon += INVSH_PackAmmoAndWeapon(inv, weapon, equip, 0, name);
					if (hasWeapon) {
						int ammo;

						/* Find the first possible ammo to check damage type. */
						for (ammo = 0; ammo < CSI->numODs; ammo++)
							if (equip[ammo] && INVSH_LoadableInWeapon(&CSI->ods[ammo], weapon))
								break;
						if (ammo < CSI->numODs) {
							primary =
								/* To avoid two particle weapons. */
								!(CSI->ods[ammo].dmgtype == CSI->damParticle)
								/* To avoid SMG + Assault Rifle */
								&& !(CSI->ods[ammo].dmgtype == CSI->damNormal);
								/* fd[0][0] Seems to be ok here since we just check the damage type and they are the same for all fds i've found. */
						}
						maxPrice = 0; /* One primary weapon is enough. */
						missedPrimary = 0;
					}
				} else {
					missedPrimary += equip[weapon->idx];
				}
			}
		} while (maxPrice);

		/* Sidearms (secondary weapons with reload). */
		if (!hasWeapon)
			repeat = WEAPONLESS_BONUS > frand();
		else
			repeat = 0;
		do {
			maxPrice = primary ? INT_MAX : 0;
			do {
				prevPrice = maxPrice;
				weapon = NULL;
				/* If primary is a particle or normal damage weapon,
				 * we pick cheapest sidearms first. */
				maxPrice = primary ? 0 : INT_MAX;
				for (i = 0; i < CSI->numODs; i++) {
					obj = &CSI->ods[i];
					if (equip[i] && obj->weapon
						&& BUY_SEC(obj->buytype) && obj->reload) {
						if (primary
							? obj->price > maxPrice && obj->price < prevPrice
							: obj->price < maxPrice && obj->price > prevPrice) {
							maxPrice = obj->price;
							weapon = obj;
						}
					}
				}
				if (!(maxPrice == (primary ? 0 : INT_MAX))) {
					if (equip[weapon->idx] >= 40 * frand()) {
						hasWeapon += INVSH_PackAmmoAndWeapon(inv, weapon, equip, missedPrimary, name);
						if (hasWeapon) {
							/* Try to get the second akimbo pistol. */
							if (primary == 2
								&& !weapon->fireTwoHanded
								&& frand() < AKIMBO_CHANCE) {
								INVSH_PackAmmoAndWeapon(inv, weapon, equip, 0, name);
							}
							/* Enough sidearms */
							maxPrice = primary ? 0 : INT_MAX;
						}
					}
				}
			} while (!(maxPrice == (primary ? 0 : INT_MAX)));
		} while (!hasWeapon && repeat--);

		/* Misc items and secondary weapons without reload. */
		if (!hasWeapon)
			repeat = WEAPONLESS_BONUS > frand();
		else
			repeat = 0;
		do {
			maxPrice = INT_MAX;
			do {
				prevPrice = maxPrice;
				maxPrice = 0;
				weapon = NULL;
				for (i = 0; i < CSI->numODs; i++) {
					obj = &CSI->ods[i];
					if (equip[i] && ((obj->weapon && BUY_SEC(obj->buytype) && !obj->reload)
							|| obj->buytype == BUY_MISC)) {
						if (obj->price > maxPrice && obj->price < prevPrice) {
							maxPrice = obj->price;
							weapon = obj;
						}
					}
				}
				if (maxPrice) {
					int num;

					num = equip[weapon->idx] / 40 + (equip[weapon->idx] % 40 >= 40 * frand());
					while (num--)
						hasWeapon += INVSH_PackAmmoAndWeapon(inv, weapon, equip, 0, name);
				}
			} while (maxPrice);
		} while (repeat--); /* Gives more if no serious weapons. */

		/* If no weapon at all, bad guys will always find a blade to wield. */
		if (!hasWeapon) {
			Com_DPrintf(DEBUG_SHARED, "INVSH_EquipActor: no weapon picked in equipment '%s', defaulting to the most expensive secondary weapon without reload.\n", name);
			maxPrice = 0;
			for (i = 0; i < CSI->numODs; i++) {
				obj = &CSI->ods[i];
				if (equip[i] && obj->weapon && BUY_SEC(obj->buytype) && !obj->reload) {
					if (obj->price > maxPrice && obj->price < prevPrice) {
						maxPrice = obj->price;
						weapon = obj;
					}
				}
			}
			if (maxPrice)
				hasWeapon += INVSH_PackAmmoAndWeapon(inv, weapon, equip, 0, name);
		}
		/* If still no weapon, something is broken, or no blades in equipment. */
		if (!hasWeapon)
			Com_DPrintf(DEBUG_SHARED, "INVSH_EquipActor: cannot add any weapon; no secondary weapon without reload detected for equipment '%s'.\n", name);

		/* Armour; especially for those without primary weapons. */
		repeat = (float) missedPrimary * (1 + frand() * PROB_COMPENSATION) / 40.0;
	} else {
		/** @todo: For melee actors we should not be able to get into this function, this can be removed. */
		Com_DPrintf(DEBUG_SHARED, "INVSH_EquipActor: character '%s' may not carry weapons\n", chr->name);
		return;
	}

	if (!chr->armour) {
		Com_DPrintf(DEBUG_SHARED, "INVSH_EquipActor: character '%s' may not carry armour\n", chr->name);
		return;
	}

	do {
		maxPrice = INT_MAX;
		do {
			prevPrice = maxPrice;
			maxPrice = 0;
			for (i = 0; i < CSI->numODs; i++) {
				obj = &CSI->ods[i];
				if (equip[i] && obj->buytype == BUY_ARMOUR) {
					if (obj->price > maxPrice && obj->price < prevPrice) {
						maxPrice = obj->price;
						weapon = obj;
					}
				}
			}
			if (maxPrice) {
				if (equip[weapon->idx] >= 40 * frand()) {
					item_t item = {NONE_AMMO, NULL, NULL, 0, 0};

					item.t = weapon;
					if (Com_TryAddToInventory(inv, item, &CSI->ids[CSI->idArmour])) {
						hasArmour++;
						maxPrice = 0; /* One armour is enough. */
					}
				}
			}
		} while (maxPrice);
	} while (!hasArmour && repeat--);
}

/*
================================
CHARACTER GENERATING FUNCTIONS
================================
*/

/**
 * @brief Translate the team string to the team int value
 * @sa TEAM_ALIEN, TEAM_CIVILIAN, TEAM_PHALANX
 * @param[in] teamString
 */
int Com_StringToTeamNum (const char* teamString)
{
	if (!Q_strncmp(teamString, "TEAM_PHALANX", MAX_VAR))
		return TEAM_PHALANX;
	if (!Q_strncmp(teamString, "TEAM_CIVILIAN", MAX_VAR))
		return TEAM_CIVILIAN;
	if (!Q_strncmp(teamString, "TEAM_ALIEN", MAX_VAR))
		return TEAM_ALIEN;
	Com_Printf("Com_StringToTeamNum: Unknown teamString: '%s'\n", teamString);
	return -1;
}

/**
 * @brief Determines the maximum amount of XP per skill that can be gained from any one mission.
 * @param[in] skill The skill for which to fetch the maximum amount of XP.
 * @sa G_UpdateCharacterSkills
 * @sa G_GetEarnedExperience
 * @note Explanation of the values here:
 * There is a maximum speed at which skills may rise over the course of 100 missions (the predicted career length of a veteran soldier).
 * For example, POWER will, at best, rise 10 points over 100 missions. If the soldier gets max XP every time.
 * Because the increase is given as experience^0.6, that means that the maximum XP cap x per mission is given as
 * log 10 / log x = 0.6
 * log x = log 10 / 0.6
 * x = 10 ^ (log 10 / 0.6)
 * x = 46
 * The division by 100 happens in G_UpdateCharacterSkills
 */
int CHRSH_CharGetMaxExperiencePerMission (abilityskills_t skill)
{
	switch (skill) {
	case ABILITY_POWER:
		return 46;
	case ABILITY_SPEED:
		return 91;
	case ABILITY_ACCURACY:
		return 290;
	case ABILITY_MIND:
		return 290;
	case SKILL_CLOSE:
		return 680;
	case SKILL_HEAVY:
		return 680;
	case SKILL_ASSAULT:
		return 680;
	case SKILL_SNIPER:
		return 680;
	case SKILL_EXPLOSIVE:
		return 680;
	case SKILL_NUM_TYPES: /* This is health. */
		return 2154;
	default:
		Com_DPrintf(DEBUG_GAME, "G_GetMaxExperiencePerMission: invalid skill type\n");
		return 0;
	}
}

/**
 * @brief Templates for the different unit types. Each element of the array is a tuple that
 * indicates the minimum and the maximum value for the relevant ability or skill.
 */
static const int commonSoldier[][2] =
	{{15, 25}, /* Strength */
	 {15, 25}, /* Speed */
	 {20, 30}, /* Accuracy */
	 {20, 35}, /* Mind */
	 {15, 25}, /* Close */
	 {15, 25}, /* Heavy */
	 {15, 25}, /* Assault */
	 {15, 25}, /* Sniper */
	 {15, 25}, /* Explosives */
	 {80, 110}}; /* Health */

static const int closeSoldier[][2] =
	{{15, 25}, /* Strength */
	 {15, 25}, /* Speed */
	 {20, 30}, /* Accuracy */
	 {20, 35}, /* Mind */
	 {25, 40}, /* Close */
	 {13, 23}, /* Heavy */
	 {13, 23}, /* Assault */
	 {13, 23}, /* Sniper */
	 {13, 23}, /* Explosives */
	 {80, 110}}; /* Health */

static const int heavySoldier[][2] =
	{{15, 25}, /* Strength */
	 {15, 25}, /* Speed */
	 {20, 30}, /* Accuracy */
	 {20, 35}, /* Mind */
	 {13, 23}, /* Close */
	 {25, 40}, /* Heavy */
	 {13, 23}, /* Assault */
	 {13, 23}, /* Sniper */
	 {13, 23}, /* Explosives */
	 {80, 110}}; /* Health */

static const int assaultSoldier[][2] =
	{{15, 25}, /* Strength */
	 {15, 25}, /* Speed */
	 {20, 30}, /* Accuracy */
	 {20, 35}, /* Mind */
	 {13, 23}, /* Close */
	 {13, 23}, /* Heavy */
	 {25, 40}, /* Assault */
	 {13, 23}, /* Sniper */
	 {13, 23}, /* Explosives */
	 {80, 110}}; /* Health */

static const int sniperSoldier[][2] =
	{{15, 25}, /* Strength */
	 {15, 25}, /* Speed */
	 {20, 30}, /* Accuracy */
	 {20, 35}, /* Mind */
	 {13, 23}, /* Close */
	 {13, 23}, /* Heavy */
	 {13, 23}, /* Assault */
	 {25, 40}, /* Sniper */
	 {13, 23}, /* Explosives */
	 {80, 110}}; /* Health */

static const int blastSoldier[][2] =
	{{15, 25}, /* Strength */
	 {15, 25}, /* Speed */
	 {20, 30}, /* Accuracy */
	 {20, 35}, /* Mind */
	 {13, 23}, /* Close */
	 {13, 23}, /* Heavy */
	 {13, 23}, /* Assault */
	 {13, 23}, /* Sniper */
	 {25, 40}, /* Explosives */
	 {80, 110}}; /* Health */

static const int eliteSoldier[][2] =
	{{25, 35}, /* Strength */
	 {25, 35}, /* Speed */
	 {30, 40}, /* Accuracy */
	 {30, 45}, /* Mind */
	 {25, 40}, /* Close */
	 {25, 40}, /* Heavy */
	 {25, 40}, /* Assault */
	 {25, 40}, /* Sniper */
	 {25, 40}, /* Explosives */
	 {100, 130}}; /* Health */

static const int civilSoldier[][2] =
	{{5, 10}, /* Strength */
	 {5, 10}, /* Speed */
	 {10, 15}, /* Accuracy */
	 {10, 15}, /* Mind */
	 {5, 10}, /* Close */
	 {5, 10}, /* Heavy */
	 {5, 10}, /* Assault */
	 {5, 10}, /* Sniper */
	 {5, 10}, /* Explosives */
	 {5, 10}}; /* Health */

static const int alienSoldier[][2] =
	{{25, 35}, /* Strength */
	 {25, 35}, /* Speed */
	 {30, 40}, /* Accuracy */
	 {30, 45}, /* Mind */
	 {25, 40}, /* Close */
	 {25, 40}, /* Heavy */
	 {25, 40}, /* Assault */
	 {25, 40}, /* Sniper */
	 {25, 40}, /* Explosives */
	 {100, 130}}; /* Health */

static const int robotSoldier[][2] =
	{{55, 55}, /* Strength */
	 {40, 40}, /* Speed */
	 {50, 50}, /* Accuracy */
	 {0, 0}, /* Mind */
	 {50, 50}, /* Close */
	 {50, 50}, /* Heavy */
	 {50, 50}, /* Assault */
	 {50, 50}, /* Sniper */
	 {50, 50}, /* Explosives */
	 {200, 200}}; /* Health */

/** @brief For multiplayer characters ONLY! */
static const int MPSoldier[][2] =
	{{25, 75}, /* Strength */
	 {25, 35}, /* Speed */
	 {20, 75}, /* Accuracy */
	 {30, 75}, /* Mind */
	 {20, 75}, /* Close */
	 {20, 75}, /* Heavy */
	 {20, 75}, /* Assault */
	 {20, 75}, /* Sniper */
	 {20, 75}, /* Explosives */
	 {80, 130}}; /* Health */

/**
 * @brief Generates a skill and ability set for any character.
 * @param[in] chr Pointer to the character, for which we generate stats.
 * @param[in] team Index of team (TEAM_ALIEN, TEAM_CIVILIAN, ...).
 * @param[in] type This is the employee type you want to generate the skills for. This is MAX_EMPL for aliens.
 * @param[in] multiplayer If this is true we use the skill values from MPSoldier
 * mulitplayer is a special case here
 */
void CHRSH_CharGenAbilitySkills (character_t * chr, int team, employeeType_t type, qboolean multiplayer)
{
	int i, abilityWindow, temp;
	const int (*soldierTemplate)[2] = MPSoldier;

	assert(chr);

	if (team == TEAM_ALIEN) {
		/* Aliens get special treatment. */
		soldierTemplate = alienSoldier;
		/* Add modifiers for difficulty setting here! */
	} else if (team == TEAM_CIVILIAN) {
		/* Civilian team gets special treatment. */
		soldierTemplate = civilSoldier;
	} else if (!multiplayer) {
		float soldierRoll;
		/* Determine which soldier template to use.
		 * 25% of the soldiers will be specialists (5% chance each).
		 * 1% of the soldiers will be elite.
		 * 74% of the soldiers will be commmon. */
		switch (type) {
		case EMPL_SOLDIER:
			soldierRoll = frand();
			if (soldierRoll <= 0.01f)
				soldierTemplate = eliteSoldier;
			else if (soldierRoll <= 0.06)
				soldierTemplate = closeSoldier;
			else if (soldierRoll <= 0.11)
				soldierTemplate = heavySoldier;
			else if (soldierRoll <= 0.16)
				soldierTemplate = assaultSoldier;
			else if (soldierRoll <= 0.22)
				soldierTemplate = sniperSoldier;
			else if (soldierRoll <= 0.26)
				soldierTemplate = blastSoldier;
			else
				soldierTemplate = commonSoldier;
			break;
		case EMPL_PILOT:
		case EMPL_SCIENTIST:
		case EMPL_WORKER:
			soldierTemplate = civilSoldier;
			break;
		case EMPL_ROBOT:
			soldierTemplate = robotSoldier;
			break;
		default:
			soldierTemplate = commonSoldier;
			break;
		}
	}

	assert(soldierTemplate);

	/* Abilities and skills -- random within the range */
	for (i = 0; i < SKILL_NUM_TYPES; i++) {
		abilityWindow = soldierTemplate[i][1] - soldierTemplate[i][0];
		/* Reminder: In case if abilityWindow==0 the ability will get set to the lower limit. */
		temp = (frand() * abilityWindow) + soldierTemplate[i][0];
		chr->score.skills[i] = temp;
		chr->score.initialSkills[i] = temp;
	}

	/* Health. */
	abilityWindow = soldierTemplate[i][1] - soldierTemplate[i][0];
	temp = (frand() * abilityWindow) + soldierTemplate[i][0];
	chr->score.initialSkills[SKILL_NUM_TYPES] = temp;
	chr->maxHP = temp;
	chr->HP = temp;

	/* Morale */
	chr->morale = GET_MORALE(chr->score.skills[ABILITY_MIND]);
	if (chr->morale >= MAX_SKILL)
		chr->morale = MAX_SKILL;

	/* Initialize the experience values */
	for (i = 0; i <= SKILL_NUM_TYPES; i++) {
		chr->score.experience[i] = 0;
	}
}

/** @brief Used in CHRSH_CharGetHead and CHRSH_CharGetBody to generate the model path. */
static char CHRSH_returnModel[MAX_VAR];

/**
 * @brief Returns the body model for the soldiers for armoured and non armoured soldiers
 * @param chr Pointer to character struct
 * @sa CHRSH_CharGetBody
 */
char *CHRSH_CharGetBody (const character_t * const chr)
{
	char id[MAX_VAR];
	char *underline;

	assert(chr);
	assert(chr->inv);

	/* models of UGVs don't change - because they are already armoured */
	if (chr->inv->c[CSI->idArmour] && chr->fieldSize == ACTOR_SIZE_NORMAL) {
		assert(!Q_strcmp(chr->inv->c[CSI->idArmour]->item.t->type, "armour"));
/*		Com_Printf("CHRSH_CharGetBody: Use '%s' as armour\n", chr->inv->c[CSI->idArmour]->item.t->id);*/

		/* check for the underline */
		Q_strncpyz(id, chr->inv->c[CSI->idArmour]->item.t->id, sizeof(id));
		underline = strchr(id, '_');
		if (underline)
			*underline = '\0';

		Com_sprintf(CHRSH_returnModel, sizeof(CHRSH_returnModel), "%s%s/%s", chr->path, id, chr->body);
	} else
		Com_sprintf(CHRSH_returnModel, sizeof(CHRSH_returnModel), "%s/%s", chr->path, chr->body);
	return CHRSH_returnModel;
}

/**
 * @brief Returns the head model for the soldiers for armoured and non armoured soldiers
 * @param chr Pointer to character struct
 * @sa CHRSH_CharGetBody
 */
char *CHRSH_CharGetHead (const character_t * const chr)
{
	char id[MAX_VAR];
	char *underline;

	assert(chr);
	assert(chr->inv);

	/* models of UGVs don't change - because they are already armoured */
	if (chr->inv->c[CSI->idArmour] && chr->fieldSize == ACTOR_SIZE_NORMAL) {
		assert(!Q_strcmp(chr->inv->c[CSI->idArmour]->item.t->type, "armour"));
/*		Com_Printf("CHRSH_CharGetHead: Use '%s' as armour\n", chr->inv->c[CSI->idArmour]->item.t->id);*/

		/* check for the underline */
		Q_strncpyz(id, chr->inv->c[CSI->idArmour]->item.t->id, sizeof(id));
		underline = strchr(id, '_');
		if (underline)
			*underline = '\0';

		Com_sprintf(CHRSH_returnModel, sizeof(CHRSH_returnModel), "%s%s/%s", chr->path, id, chr->head);
	} else
		Com_sprintf(CHRSH_returnModel, sizeof(CHRSH_returnModel), "%s/%s", chr->path, chr->head);
	return CHRSH_returnModel;
}

/**
 * @brief Prints a description of an object
 * @param[in] od object to display the info for
 */
void INVSH_PrintItemDescription (const objDef_t *od)
{
	int i;

	if (!od)
		return;

	Com_Printf("Item: %s\n", od->id);
	Com_Printf("... name          -> %s\n", od->name);
	Com_Printf("... type          -> %s\n", od->type);
	Com_Printf("... category      -> %i\n", od->animationIndex);
	Com_Printf("... weapon        -> %i\n", od->weapon);
	Com_Printf("... holdtwohanded -> %i\n", od->holdTwoHanded);
	Com_Printf("... firetwohanded -> %i\n", od->fireTwoHanded);
	Com_Printf("... thrown        -> %i\n", od->thrown);
	Com_Printf("... usable for weapon (if type is ammo):\n");
	for (i = 0; i < od->numWeapons; i++) {
		if (od->weapons[i])
			Com_Printf("    ... %s\n", od->weapons[i]->name);
	}
	Com_Printf("\n");
}

/**
 * @brief Returns the index of this item in the inventory.
 * @note id may not be null or empty
 * @note previously known as RS_GetItem
 * @param[in] id the item id in our object definition array (csi.ods)
 */
objDef_t *INVSH_GetItemByID (const char *id)
{
	int i;
	objDef_t *item;

#ifdef DEBUG
	if (!id || !*id) {
		Com_Printf("INVSH_GetItemByID: Called with empty id\n");
		return NULL;
	}
#endif

	for (i = 0; i < CSI->numODs; i++) {	/* i = item index */
		item = &CSI->ods[i];
		if (!Q_strncmp(id, item->id, MAX_VAR)) {
			return item;
		}
	}

	Com_Printf("INVSH_GetItemByID: Item \"%s\" not found.\n", id);
	return NULL;
}

/**
 * @brief Checks if an item can be used to reload a weapon.
 * @param[in] od The object definition of the ammo.
 * @param[in] weapon The weapon (in the inventory) to check the item with.
 * @return qboolean Returns qtrue if the item can be used in the given weapon, otherwise qfalse.
 * @sa INVSH_PackAmmoAndWeapon
 */
qboolean INVSH_LoadableInWeapon (const objDef_t *od, const objDef_t *weapon)
{
	int i;
	qboolean usable = qfalse;

#ifdef DEBUG
	if (!od) {
		Com_DPrintf(DEBUG_SHARED, "INVSH_LoadableInWeapon: No pointer given for 'od'.\n");
		return qfalse;
	}
	if (!weapon) {
		Com_DPrintf(DEBUG_SHARED, "INVSH_LoadableInWeapon: No weapon pointer given.\n");
		return qfalse;
	}
#endif

	if (od && od->numWeapons == 1
	 && od->weapons[0]
	 && od->weapons[0] == od) {
		/* The weapon is only linked to itself. */
		return qfalse;
	}

	for (i = 0; i < od->numWeapons; i++) {
#ifdef DEBUG
		if (!od->weapons[i]) {
			Com_DPrintf(DEBUG_SHARED, "INVSH_LoadableInWeapon: No weapon pointer set for the %i. entry found in item '%s'.\n", i, od->id);
			break;
		}
#endif
		if (weapon == od->weapons[i]) {
			usable = qtrue;
			break;
		}
	}
#if 0
	Com_DPrintf(DEBUG_SHARED, "INVSH_LoadableInWeapon: item '%s' usable/unusable (%i) in weapon '%s'.\n", od->id, usable, weapon->id);
#endif
	return usable;
}

/*
===============================
FIREMODE MANAGEMENT FUNCTIONS
===============================
*/

/**
 * @brief Returns the index of the array that has the firedefinitions for a given weapon/ammo (-index)
 * @param[in] od The object definition of the ammo item.
 * @param[in] weapon The weapon (in the inventory) to check the ammo item with.
 * @return int Returns the index in the fd array. -1 if the weapon-idx was not found. 0 (equals the default firemode) if an invalid or unknown weapon idx was given.
 * @note the return value of -1 is in most cases a fatal error (except the scripts are not parsed while e.g. maptesting)
 */
int FIRESH_FiredefsIDXForWeapon (const objDef_t *od, const objDef_t *weapon)
{
	int i;

	if (!od) {
		Com_DPrintf(DEBUG_SHARED, "FIRESH_FiredefsIDXForWeapon: object definition is NULL.\n");
		return -1;
	}

	if (!weapon) {
		Com_DPrintf(DEBUG_SHARED, "FIRESH_FiredefsIDXForWeapon: no weapon given (item '%s') using default weapon/firemodes.\n", od->id);
		return 0;
	}

	for (i = 0; i < od->numWeapons; i++) {
		if (weapon == od->weapons[i])
			return i;
	}

	/* No firedef index found for this weapon/ammo. */
#ifdef DEBUG
	Com_DPrintf(DEBUG_SHARED, "FIRESH_FiredefsIDXForWeapon: No firedef index found for weapon. od:%s weapIdx:%i).\n", od->id, weapon->idx);
#endif
	return -1;
}

/**
 * @brief Returns the default reaction firemode for a given ammo in a given weapon.
 * @param[in] ammo The ammo(or weapon-)object that contains the firedefs
 * @param[in] weaponFdsIdx The index in objDef[x]
 * @return Default reaction-firemode index in objDef->fd[][x]. -1 if an error occurs or no firedefs exist.
 */
int FIRESH_GetDefaultReactionFire (const objDef_t *ammo, int weaponFdsIdx)
{
	int fdIdx;
	if (weaponFdsIdx >= MAX_WEAPONS_PER_OBJDEF) {
		Com_Printf("FIRESH_GetDefaultReactionFire: bad weaponFdsIdx (%i) Maximum is %i.\n", weaponFdsIdx, MAX_WEAPONS_PER_OBJDEF-1);
		return -1;
	}
	if (weaponFdsIdx < 0) {
		Com_Printf("FIRESH_GetDefaultReactionFire: Negative weaponFdsIdx given.\n");
		return -1;
	}

	if (ammo->numFiredefs[weaponFdsIdx] == 0) {
		Com_Printf("FIRESH_GetDefaultReactionFire: Probably not an ammo-object: %s\n", ammo->id);
		return -1;
	}

	for (fdIdx = 0; fdIdx < ammo->numFiredefs[weaponFdsIdx]; fdIdx++) {
		if (ammo->fd[weaponFdsIdx][fdIdx].reaction)
			return fdIdx;
	}

	return -1; /* -1 = undef firemode. Default for objects without a reaction-firemode */
}

/**
 * @brief Will merge the second shape (=itemShape) into the first one (=big container shape) on the position x/y.
 * @note The function expects an already rotated shape for itemShape. Use Com_ShapeRotate if needed.
 * @param[in] shape Pointer to 'uint32_t shape[SHAPE_BIG_MAX_HEIGHT]'
 * @param[in] itemshape
 * @param[in] x
 * @param[in] y
 */
void Com_MergeShapes (uint32_t *shape, const uint32_t itemShape, const int x, const int y)
{
	int i;

	for (i = 0; (i < SHAPE_SMALL_MAX_HEIGHT) && (y + i < SHAPE_BIG_MAX_HEIGHT); i++)
		shape[y + i] |= ((itemShape >> i * SHAPE_SMALL_MAX_WIDTH) & 0xFF) << x;
}

/**
 * @brief Checks the shape if there is a 1-bit on the position x/y.
 * @param[in] shape Pointer to 'uint32_t shape[SHAPE_BIG_MAX_HEIGHT]'
 * @param[in] x
 * @param[in] y
 */
qboolean Com_CheckShape (const uint32_t *shape, const int x, const int y)
{
	const uint32_t row = shape[y];
	int position = pow(2, x);

	if (y >= SHAPE_BIG_MAX_HEIGHT || x >= SHAPE_BIG_MAX_WIDTH || x < 0 || y < 0) {
		Com_Printf("Com_CheckShape: Bad x or y value: (x=%i, y=%i)\n", x, y);
		return qfalse;
	}

	if ((row & position) == 0)
		return qfalse;
	else
		return qtrue;
}

/**
 * @brief Checks the shape if there is a 1-bit on the position x/y.
 * @param[in] shape The shape to check in. (8x4)
 * @param[in] x
 * @param[in] y
 */
static qboolean Com_CheckShapeSmall (const uint32_t shape, const int x, const int y)
{
	if (y >= SHAPE_BIG_MAX_HEIGHT || x >= SHAPE_BIG_MAX_WIDTH || x < 0 || y < 0) {
		Com_Printf("Com_CheckShapeSmall: Bad x or y value: (x=%i, y=%i)\n", x, y);
		return qfalse;
	}

	return shape & (0x01 << (y * SHAPE_SMALL_MAX_WIDTH + x));
}

/**
 * @brief Counts the used bits in a shape (item shape).
 * @param[in] shape The shape to count the bits in.
 * @return Number of bits.
 * @note Used to calculate the real field usage in the inventory
 */
int Com_ShapeUsage (const uint32_t shape)
{
 	int bitCounter = 0;
	int i;

	for (i = 0; i < SHAPE_SMALL_MAX_HEIGHT * SHAPE_SMALL_MAX_WIDTH; i++)
		if (shape & (1 << i))
			bitCounter++;

	return bitCounter;
}

/**
 * @brief Sets one bit in a shape to true/1
 * @note Only works for V_SHAPE_SMALL!
 * If the bit is already set the shape is not changed.
 * @param[in] shape The shape to modify. (8x4)
 * @param[in] x The x (width) position of the bit to set.
 * @param[in] y The y (height) position of the bit to set.
 * @return The new shape.
 */
static uint32_t Com_ShapeSetBit (uint32_t shape, const int x, const int y)
{
	if (x >= SHAPE_SMALL_MAX_WIDTH || y >= SHAPE_SMALL_MAX_HEIGHT || x < 0 || y < 0) {
		Com_Printf("Com_ShapeSetBit: Bad x or y value: (x=%i, y=%i)\n", x,y);
		return shape;
	}

	shape |= 0x01 << (y * SHAPE_SMALL_MAX_WIDTH + x);
	return shape;
}


/**
 * @brief Rotates a shape definition 90 degree to the left (CCW)
 * @note Only works for V_SHAPE_SMALL!
 * @param[in] shape The shape to rotate.
 * @return The new shape.
 */
uint32_t Com_ShapeRotate (const uint32_t shape)
{
	int h, w;
	uint32_t shapeNew = 0;
	int maxWidth = -1;

	for (w = SHAPE_SMALL_MAX_WIDTH - 1; w >= 0; w--) {
		for (h = 0; h < SHAPE_SMALL_MAX_HEIGHT; h++) {
			if (Com_CheckShapeSmall(shape, w, h)) {
				if (w >= SHAPE_SMALL_MAX_HEIGHT) {
					/* Object can't be rotated (code-wise), it is longer than SHAPE_SMALL_MAX_HEIGHT allows. */
					return shape;
				}

				if (maxWidth < 0)
					maxWidth = w;

				shapeNew = Com_ShapeSetBit(shapeNew, h, maxWidth - w);
			}
		}
	}

#if 0
	Com_Printf("\n");
	Com_Printf("<normal>\n");
	Com_ShapePrint(shape);
	Com_Printf("<rotated>\n");
	Com_ShapePrint(shapeNew);
#endif
	return shapeNew;
}

#ifdef DEBUG
/**
 * @brief Prints the shape. (mainly debug)
 * @note Only works for V_SHAPE_SMALL!
 */
void Com_ShapePrint (const uint32_t shape)
{
	int h, w;
	int row;

	for (h = 0; h < SHAPE_SMALL_MAX_HEIGHT; h++) {
		row = (shape >> (h * SHAPE_SMALL_MAX_WIDTH)); /* Shift the mask to the right to remove leading rows */
		row &= 0xFF;		/* Mask out trailing rows */
		Com_Printf("<");
			for (w = 0; w < SHAPE_SMALL_MAX_WIDTH; w++) {
				if (row & (0x01 << w)) { /* Bit number 'w' in this row set? */
					Com_Printf("#");
				} else {
					Com_Printf(".");
				}
			}
		Com_Printf(">\n");
	}
}
#endif
