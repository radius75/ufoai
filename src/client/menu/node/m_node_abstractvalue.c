/**
 * @file m_node_abstractvalue.c
 * @brief The abstractvalue node is an abstract node (we can't instanciate it).
 * It provide common properties to concrete nodes, to manage a value in a range.
 */

/*
Copyright (C) 2002-2009 UFO: Alien Invasion.

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

#include "../m_nodes.h"
#include "../m_parse.h"
#include "m_node_abstractvalue.h"

#define EXTRADATA(node) (node->u.abstractvalue)

static const nodeBehaviour_t const *localBehaviour;

static inline void MN_InitCvarOrFloat (float** adress, float defaultValue)
{
	if (*adress == NULL) {
		*adress = MN_AllocStaticFloat(1);
		**adress = defaultValue;
	}
}

static void MN_AbstractValueLoaded (menuNode_t * node)
{
	MN_InitCvarOrFloat((float**)&EXTRADATA(node).value, 0);
	MN_InitCvarOrFloat((float**)&EXTRADATA(node).delta, 1);
	MN_InitCvarOrFloat((float**)&EXTRADATA(node).max, 0);
	MN_InitCvarOrFloat((float**)&EXTRADATA(node).min, 0);
}

static void MN_CloneCvarOrFloat (const float*const* source, float** clone)
{
	/* dont update cvar */
	if (!strncmp(*(const char*const*)source, "*cvar", 5))
		return;

	/* clone float */
	*clone = MN_AllocStaticFloat(1);
	**clone = **source;
}

/**
 * @brief Call to update a cloned node
 */
static void MN_AbstractValueClone (const menuNode_t *source, menuNode_t *clone)
{
	localBehaviour->super->clone(source, clone);
	MN_CloneCvarOrFloat((const float*const*)&EXTRADATA(source).value, (float**)&EXTRADATA(clone).value);
	MN_CloneCvarOrFloat((const float*const*)&EXTRADATA(source).delta, (float**)&EXTRADATA(clone).delta);
	MN_CloneCvarOrFloat((const float*const*)&EXTRADATA(source).max, (float**)&EXTRADATA(clone).max);
	MN_CloneCvarOrFloat((const float*const*)&EXTRADATA(source).min, (float**)&EXTRADATA(clone).min);
}

static const value_t properties[] = {
	/* Current value of the node. It should be a cvar */
	{"current", V_CVAR_OR_FLOAT, MN_EXTRADATA_OFFSETOF(abstractValueExtraData_t, value), 0},
	/* Value of a positive step. Must be bigger than 1. */
	{"delta", V_CVAR_OR_FLOAT, MN_EXTRADATA_OFFSETOF(abstractValueExtraData_t, delta), 0},
	/* Maximum value we can set to the node. It can be a cvar. Default value is 0. */
	{"max", V_CVAR_OR_FLOAT, MN_EXTRADATA_OFFSETOF(abstractValueExtraData_t, max), 0},
	/* Minimum value we can set to the node. It can be a cvar. Default value is 1. */
	{"min", V_CVAR_OR_FLOAT, MN_EXTRADATA_OFFSETOF(abstractValueExtraData_t, min), 0},

	/* Callback value set when before calling onChange. It is used to know the change apply by the user
	 * @Deprecated
	 */
	{"lastdiff", V_FLOAT, MN_EXTRADATA_OFFSETOF(abstractValueExtraData_t, lastdiff), MEMBER_SIZEOF(abstractValueExtraData_t, lastdiff)},
	{NULL, V_NULL, 0, 0}
};

void MN_RegisterAbstractValueNode (nodeBehaviour_t *behaviour)
{
	localBehaviour = behaviour;
	behaviour->name = "abstractvalue";
	behaviour->loaded = MN_AbstractValueLoaded;
	behaviour->clone = MN_AbstractValueClone;
	behaviour->isAbstract = qtrue;
	behaviour->properties = properties;
	behaviour->extraDataSize = sizeof(abstractValueExtraData_t);
}
