/**
 * @file cl_transfer.h
 * @brief Header file for Transfer stuff.
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

#ifndef CLIENT_CL_TRANSFER_H
#define CLIENT_CL_TRANSFER_H

/** @brief Determine transfer type. */
typedef enum {
	TR_STUFF,			/**< Cargo transfer, items, aliens or personel. */
	TR_AIRCRAFT			/**< Transfer of aircraft. */
} transferType_t;

typedef struct transferlist_s {
	transferType_t type;		/**< Type of transfer to determine what to do with load/unload. */
	int itemAmount[MAX_OBJDEFS];	/**< Amount of given item [csi.ods[idx]]. */
	int alienLiveAmount[MAX_CARGO];	/**< Alive alien amount of given alien [aliensCont_t->idx]. */
	int alienBodyAmount[MAX_CARGO];	/**< Alien body amount of given alien [aliensCont_t->idx]. */
	int employees[MAX_EMPLOYEES];	/**< Employee index. */
	int destBase;			/**< Index of destination base. */
} transferlist_t;

void TR_TransferAircraftMenu(aircraft_t* aircraft);
void TR_TransferEnd(aircraft_t* aircraft);
void TR_EmptyTransferCargo (aircraft_t *aircraft);

#endif /* CLIENT_CL_TRANSFER_H */

