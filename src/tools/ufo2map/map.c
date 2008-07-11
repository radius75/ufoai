/**
 * @file map.c
 */

/*
Copyright (C) 1997-2001 Id Software, Inc.

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


#include "bsp.h"

extern qboolean onlyents;

int			nummapbrushes;
mapbrush_t	mapbrushes[MAX_MAP_BRUSHES];

int			nummapbrushsides;
side_t		brushsides[MAX_MAP_SIDES];
brush_texture_t side_brushtextures[MAX_MAP_SIDES];

int			nummapplanes;
plane_t		mapplanes[MAX_MAP_PLANES];

#define	PLANE_HASHES	1024
static plane_t *planehash[PLANE_HASHES];

static vec3_t map_mins, map_maxs;
static int c_boxbevels = 0;
static int c_edgebevels = 0;

/*
=============================================================================
PLANE FINDING
=============================================================================
*/

/**
 * @brief Set the type of the plane according to it's normal vector
 */
static inline int PlaneTypeForNormal (vec3_t normal)
{
	vec_t ax, ay, az;

	/* NOTE: should these have an epsilon around 1.0?		 */
	if (normal[0] == 1.0 || normal[0] == -1.0)
		return PLANE_X;
	if (normal[1] == 1.0 || normal[1] == -1.0)
		return PLANE_Y;
	if (normal[2] == 1.0 || normal[2] == -1.0)
		return PLANE_Z;

	ax = fabs(normal[0]);
	ay = fabs(normal[1]);
	az = fabs(normal[2]);

	if (ax >= ay && ax >= az)
		return PLANE_ANYX;
	if (ay >= ax && ay >= az)
		return PLANE_ANYY;
	return PLANE_ANYZ;
}

static inline qboolean PlaneEqual (plane_t *p, vec3_t normal, vec_t dist)
{
	if (fabs(p->normal[0] - normal[0]) < NORMAL_EPSILON
	 && fabs(p->normal[1] - normal[1]) < NORMAL_EPSILON
	 && fabs(p->normal[2] - normal[2]) < NORMAL_EPSILON
	 && fabs(p->dist - dist) < MAP_DIST_EPSILON)
		return qtrue;
	return qfalse;
}

static inline void AddPlaneToHash (plane_t *p)
{
	int hash;

	hash = (int)fabs(p->dist) / 8;
	hash &= (PLANE_HASHES - 1);

	p->hash_chain = planehash[hash];
	planehash[hash] = p;
}

static inline int CreateNewFloatPlane (vec3_t normal, vec_t dist)
{
	plane_t *p, temp;

	if (VectorLength(normal) < 0.5)
		Sys_Error("FloatPlane: bad normal (%.3f:%.3f:%.3f)", normal[0], normal[1], normal[2]);
	/* create a new plane */
	if (nummapplanes + 2 > MAX_MAP_PLANES)
		Sys_Error("MAX_MAP_PLANES (%i)", nummapplanes + 2);

	p = &mapplanes[nummapplanes];
	VectorCopy(normal, p->normal);
	p->dist = dist;
	p->type = (p + 1)->type = PlaneTypeForNormal(p->normal);

	VectorSubtract(vec3_origin, normal, (p + 1)->normal);
	(p + 1)->dist = -dist;

	nummapplanes += 2;

	/* always put axial planes facing positive first */
	if (p->type < 3) {
		if (p->normal[0] < 0 || p->normal[1] < 0 || p->normal[2] < 0) {
			/* flip order */
			temp = *p;
			*p = *(p + 1);
			*(p + 1) = temp;

			AddPlaneToHash(p);
			AddPlaneToHash(p + 1);
			return nummapplanes - 1;
		}
	}

	AddPlaneToHash(p);
	AddPlaneToHash(p + 1);
	return nummapplanes - 2;
}

/**
 * @brief Round the vector to int values
 * @note Can be used to save net bandwidth
 */
static inline void SnapVector (vec3_t normal)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (fabs(normal[i] - 1) < NORMAL_EPSILON) {
			VectorClear(normal);
			normal[i] = 1;
			break;
		}
		if (fabs(normal[i] - -1) < NORMAL_EPSILON) {
			VectorClear(normal);
			normal[i] = -1;
			break;
		}
	}
}

static inline void SnapPlane (vec3_t normal, vec_t *dist)
{
	SnapVector(normal);

	if (fabs(*dist - Q_rint(*dist)) < DIST_EPSILON)
		*dist = Q_rint(*dist);
}

int FindFloatPlane (vec3_t normal, vec_t dist)
{
	int i;
	plane_t *p;
	int hash, h;

	SnapPlane(normal, &dist);
	hash = (int)fabs(dist) / 8;
	hash &= (PLANE_HASHES - 1);

	/* search the border bins as well */
	for (i = -1; i <= 1; i++) {
		h = (hash + i) & (PLANE_HASHES - 1);
		for (p = planehash[h]; p; p = p->hash_chain) {
			if (PlaneEqual(p, normal, dist))
				return p - mapplanes;
		}
	}

	return CreateNewFloatPlane(normal, dist);
}

/**
 * @param[in] p0 A vector with plane coordinates
 * @param[in] p1 A vector with plane coordinates
 * @param[in] p2 A vector with plane coordinates
 */
static int PlaneFromPoints (const mapbrush_t *b, int *p0, int *p1, int *p2)
{
	vec3_t t1, t2, normal;
	vec_t dist;

	VectorSubtract(p0, p1, t1);
	VectorSubtract(p2, p1, t2);
	CrossProduct(t1, t2, normal);
	VectorNormalize(normal);

	dist = DotProduct(p0, normal);

	if (!VectorNotEmpty(normal))
		Sys_Error("PlaneFromPoints: Bad normal (null) for brush %i", b->brushnum);

	return FindFloatPlane(normal, dist);
}


/*==================================================================== */


static int BrushContents (mapbrush_t *b)
{
	int contentFlags, i, trans;
	side_t *s;

	s = &b->original_sides[0];
	contentFlags = s->contentFlags;
	trans = curTile->texinfo[s->texinfo].surfaceFlags;
	for (i = 1; i < b->numsides; i++, s++) {
		s = &b->original_sides[i];
		trans |= curTile->texinfo[s->texinfo].surfaceFlags;
		if (s->contentFlags != contentFlags) {
			Sys_FPrintf(SYS_VRB, "Entity %i, Brush %i: mixed face contents (f: %i, %i)\n"
				, b->entitynum, b->brushnum, s->contentFlags, contentFlags);
			break;
		}
	}

	/* if any side is translucent, mark the contents
	 * and change solid to window */
	if (trans & (SURF_TRANS33 | SURF_TRANS66 | SURF_ALPHATEST)) {
		contentFlags |= CONTENTS_TRANSLUCENT;
		if (contentFlags & CONTENTS_SOLID) {
			contentFlags &= ~CONTENTS_SOLID;
			contentFlags |= CONTENTS_WINDOW;
		}
	}

	return contentFlags;
}

byte GetLevelFlagsFromBrush (const mapbrush_t *brush)
{
	const byte levelflags = (brush->contentFlags >> 8) & 0xFF;
	return levelflags;
}

/*============================================================================ */

/**
 * @brief Adds any additional planes necessary to allow the brush to be expanded
 * against axial bounding boxes
 */
static void AddBrushBevels (mapbrush_t *b)
{
	int axis, dir;
	int i, j, l, order;
	side_t *s, *s2;
	vec3_t normal;

	/* add the axial planes */
	order = 0;
	for (axis = 0; axis < 3; axis++) {
		for (dir = -1; dir <= 1; dir += 2, order++) {
			/* see if the plane is already present */
			for (i = 0, s = b->original_sides; i < b->numsides; i++, s++) {
				if (mapplanes[s->planenum].normal[axis] == dir)
					break;
			}

			if (i == b->numsides) {	/* add a new side */
				float dist;
				if (nummapbrushsides == MAX_MAP_BRUSHSIDES)
					Sys_Error("MAX_MAP_BRUSHSIDES (%i)", nummapbrushsides);
				nummapbrushsides++;
				b->numsides++;
				VectorClear(normal);
				normal[axis] = dir;
				if (dir == 1)
					dist = b->maxs[axis];
				else
					dist = -b->mins[axis];
				s->planenum = FindFloatPlane(normal, dist);
				s->texinfo = b->original_sides[0].texinfo;
				s->contentFlags = b->original_sides[0].contentFlags;
				s->bevel = qtrue;
				c_boxbevels++;
			}

			/* if the plane is not in it canonical order, swap it */
			if (i != order) {
				const ptrdiff_t index = b->original_sides - brushsides;
				side_t sidetemp = b->original_sides[order];
				brush_texture_t tdtemp = side_brushtextures[index + order];

				b->original_sides[order] = b->original_sides[i];
				b->original_sides[i] = sidetemp;

				side_brushtextures[index + order] = side_brushtextures[index + i];
				side_brushtextures[index + i] = tdtemp;
			}
		}
	}

	/* add the edge bevels */
	if (b->numsides == 6)
		return;		/* pure axial */

	/* test the non-axial plane edges */
	for (i = 6; i < b->numsides; i++) {
		winding_t *w;

		s = b->original_sides + i;
		w = s->winding;
		if (!w)
			continue;

		for (j = 0; j < w->numpoints; j++) {
			int k = (j + 1) % w->numpoints;
			vec3_t vec;

			VectorSubtract(w->p[j], w->p[k], vec);
			if (VectorNormalize(vec) < 0.5)
				continue;

			SnapVector(vec);

			for (k = 0; k < 3; k++)
				if (vec[k] == -1 || vec[k] == 1
				 || (vec[k] == 0.0f && vec[(k + 1) % 3] == 0.0f))
					break;	/* axial */
			if (k != 3)
				continue;	/* only test non-axial edges */

			/* try the six possible slanted axials from this edge */
			for (axis = 0; axis < 3; axis++) {
				for (dir = -1; dir <= 1; dir += 2) {
					/* construct a plane */
					vec3_t vec2 = {0, 0, 0};
					float dist;

					vec2[axis] = dir;
					CrossProduct(vec, vec2, normal);
					if (VectorNormalize(normal) < 0.5)
						continue;
					dist = DotProduct(w->p[j], normal);

					/* if all the points on all the sides are
					 * behind this plane, it is a proper edge bevel */
					for (k = 0; k < b->numsides; k++) {
						winding_t *w2;
						float minBack;

						/* @note: This leads to different results on different archs
						 * due to float rounding/precision errors - use the -ffloat-store
						 * feature of gcc to 'fix' this */
						/* if this plane has already been used, skip it */
						if (PlaneEqual(&mapplanes[b->original_sides[k].planenum], normal, dist))
							break;

						w2 = b->original_sides[k].winding;
						if (!w2)
							continue;
						minBack = 0.0f;
						for (l = 0; l < w2->numpoints; l++) {
							const float d = DotProduct(w2->p[l], normal) - dist;
							if (d > 0.1)
								break;	/* point in front */
							if (d < minBack)
								minBack = d;
						}
						/* if some point was at the front */
						if (l != w2->numpoints)
							break;
						/* if no points at the back then the winding is on the bevel plane */
						if (minBack > -0.1f)
							break;
					}

					if (k != b->numsides)
						continue;	/* wasn't part of the outer hull */
					/* add this plane */
					if (nummapbrushsides == MAX_MAP_BRUSHSIDES)
						Sys_Error("MAX_MAP_BRUSHSIDES (%i)", nummapbrushsides);
					nummapbrushsides++;
					s2 = &b->original_sides[b->numsides];
					s2->planenum = FindFloatPlane(normal, dist);
					s2->texinfo = b->original_sides[0].texinfo;
					s2->contentFlags = b->original_sides[0].contentFlags;
					s2->bevel = qtrue;
					c_edgebevels++;
					b->numsides++;
				}
			}
		}
	}
}

/**
 * @brief makes basewindigs for sides and mins / maxs for the brush
 */
static qboolean MakeBrushWindings (mapbrush_t *brush)
{
	int i, j;
	side_t *side;

	ClearBounds(brush->mins, brush->maxs);

	for (i = 0; i < brush->numsides; i++) {
		const plane_t *plane = &mapplanes[brush->original_sides[i].planenum];
		winding_t *w = BaseWindingForPlane(plane->normal, plane->dist);
		for (j = 0; j < brush->numsides && w; j++) {
			if (i == j)
				continue;
			/* back side clipaway */
			if (brush->original_sides[j].planenum == (brush->original_sides[j].planenum ^ 1))
				continue;
			if (brush->original_sides[j].bevel)
				continue;
			plane = &mapplanes[brush->original_sides[j].planenum ^ 1];
			ChopWindingInPlace(&w, plane->normal, plane->dist, 0); /*CLIP_EPSILON); */
		}

		side = &brush->original_sides[i];
		side->winding = w;
		if (w) {
			side->visible = qtrue;
			for (j = 0; j < w->numpoints; j++)
				AddPointToBounds(w->p[j], brush->mins, brush->maxs);
		}
	}

	for (i = 0; i < 3; i++) {
		if (brush->mins[0] < -MAX_WORLD_WIDTH || brush->maxs[0] > MAX_WORLD_WIDTH)
			Com_Printf("entity %i, brush %i: bounds out of world range\n", brush->entitynum, brush->brushnum);
		if (brush->mins[0] > MAX_WORLD_WIDTH || brush->maxs[0] < -MAX_WORLD_WIDTH) {
			Com_Printf("entity %i, brush %i: no visible sides on brush\n", brush->entitynum, brush->brushnum);
			VectorClear(brush->mins);
			VectorClear(brush->maxs);
		}
	}

	return qtrue;
}

/**
 * @brief Sets surface flags dependent on assigned texture
 * @sa ParseBrush
 * @sa CheckFlags
 */
static void SetImpliedFlags (side_t *side, const char *tex, const mapbrush_t *brush)
{
	const int initSurf = side->surfaceFlags;
	const int initCont = side->contentFlags;
	const char *flagsDescription = NULL;
	qboolean checkOrFix = config.performMapCheck || config.fixMap ;

	if (!strcmp(tex, "tex_common/actorclip")) {
		side->contentFlags |= CONTENTS_ACTORCLIP;
		flagsDescription = "CONTENTS_ACTORCLIP";
	} else if (!strcmp(tex, "tex_common/caulk")) {
		side->surfaceFlags |= SURF_NODRAW;
		flagsDescription = "SURF_NODRAW";
	} else if (!strcmp(tex, "tex_common/hint")) {
		side->surfaceFlags |= SURF_HINT;
		flagsDescription = "SURF_HINT";
	} else if (!strcmp(tex, "tex_common/nodraw")) {
		side->surfaceFlags |= SURF_NODRAW;
		flagsDescription = "SURF_NODRAW";
	} else if (!strcmp(tex, "tex_common/trigger")) {
		side->surfaceFlags |= SURF_NODRAW;
		flagsDescription = "SURF_NODRAW";
	} else if (!strcmp(tex, "tex_common/origin")) {
		side->contentFlags |= CONTENTS_ORIGIN;
		flagsDescription = "CONTENTS_ORIGIN";
	} else if (!strcmp(tex, "tex_common/slick")) {
		side->contentFlags |= SURF_SLICK;
		flagsDescription = "SURF_SLICK";
	} else if (!strcmp(tex, "tex_common/stepon")) {
		side->contentFlags |= CONTENTS_STEPON;
		flagsDescription = "CONTENTS_STEPON";
	} else if (!strcmp(tex, "tex_common/weaponclip")) {
		side->contentFlags |= CONTENTS_WEAPONCLIP;
		flagsDescription = "CONTENTS_WEAPONCLIP";
	}

	if (strstr(tex, "water")) {
/*		side->surfaceFlags |= SURF_WARP;*/
		side->contentFlags |= CONTENTS_WATER;
		side->contentFlags |= CONTENTS_PASSABLE;
		flagsDescription = "CONTENTS_WATER and CONTENTS_PASSABLE";
	}

	/* If in check/fix mode and we have made a change, give output. */
	if (checkOrFix && ((side->contentFlags != initCont) || (side->surfaceFlags != initSurf))) {
		Com_Printf("* entity %i, brush %i: %s implied by %s texture has been set\n",
			brush->entitynum, brush->brushnum, flagsDescription ? flagsDescription : "-", tex);
	}

	/*one additional test, which does not directly depend on tex. */
	if ((side->surfaceFlags & SURF_NODRAW) && (side->surfaceFlags & SURF_PHONG)) {
		/* nodraw never has phong set */
		side->surfaceFlags &= ~SURF_PHONG;
		if (checkOrFix)
			Com_Printf("* entity %i, brush %i: SURF_PHONG unset, as it has SURF_NODRAW set\n",
				brush->entitynum, brush->brushnum);
	}
}

/**
 * @brief Checks whether the surface flags are correct
 * @sa ParseBrush
 * @sa SetImpliedFlags
 */
static inline void CheckFlags (side_t *side, const mapbrush_t *b)
{
	if ((side->contentFlags & CONTENTS_ACTORCLIP) &&
		(side->contentFlags & CONTENTS_STEPON))
		Sys_Error("Brush %i (entity %i) has invalid mix of stepon and actorclip", b->brushnum, b->entitynum);
	if ((side->contentFlags & CONTENTS_ACTORCLIP) &&
		(side->contentFlags & CONTENTS_PASSABLE))
		Sys_Error("Brush %i (entity %i) has invalid mix of passable and actorclip", b->brushnum, b->entitynum);
}

/** @brief How many materials were created for this map */
static int materialsCnt = 0;

/**
 * @brief Generates material files in case the settings can be guessed from map file
 */
static inline void GenerateMaterialFile (const char *filename, int mipTexIndex, mapbrush_t *b, brush_texture_t *t, side_t *s)
{
	FILE *file;
	char fileBase[MAX_OSPATH];

	if (!config.generateMaterialFile)
		return;

	/* we already have a material definition for this texture */
	if (textureref[mipTexIndex].materialMarked)
		return;

	assert(filename);

	COM_StripExtension(filename, fileBase, sizeof(fileBase));

	file = fopen(va("%s.mat", fileBase), "r");
	if (!file) {
		/* create a new file */
		file = fopen(va("%s.mat", fileBase), "a");
		if (!file) {
			Com_Printf("Could not open material file '%s.mat' for writing\n", fileBase);
			config.generateMaterialFile = qfalse;
			return;
		}
		fprintf(file, "// auto generated by ufo2map\n// make sure to tweak the values and fill the placeholder\n");
	} else {
		/* reuse the existing file */
		fclose(file);
		file = fopen(va("%s.mat", fileBase), "a");
		if (!file) {
			Com_Printf("Could not open material file '%s.mat' for writing\n", fileBase);
			config.generateMaterialFile = qfalse;
			return;
		}
	}

	if (b->isTerrain || b->isGenSurf) {
		fprintf(file, "{\n\tmaterial %s\n\t{\n\t\ttexture <fillme>\n\t\tterrain 0 64\n\t\tlightmap\n\t}\n}\n", textureref[mipTexIndex].name);
		textureref[mipTexIndex].materialMarked = qtrue;
		materialsCnt++;
	}

	/* envmap for water surfaces */
	if (s->contentFlags & CONTENTS_WATER) {
		fprintf(file, "{\n\tmaterial %s\n\t{\n\t\tenvmap 0\n\t}\n}\n", textureref[mipTexIndex].name);
		textureref[mipTexIndex].materialMarked = qtrue;
		materialsCnt++;
	}
	/** @todo Check for rock textures and so on */

	fclose(file);
}

/** @brief Amount of footstep surfaces found in this map */
static int footstepsCnt = 0;

/**
 * @brief Generate a list of textures that should have footsteps when walking on them
 * @param[in] filename Add this texture to the list of
 * textures where we should have footstep sounds for
 * @param[in] mipTexIndex The index in the textures array
 * @sa SV_GetFootstepSound
 * @sa Com_GetTerrainType
 */
static inline void GenerateFootstepList (const char *filename, int mipTexIndex)
{
	FILE *file;
	char fileBase[MAX_OSPATH];

	if (!config.generateFootstepFile)
		return;

	if (textureref[mipTexIndex].footstepMarked)
		return;

	assert(filename);

	COM_StripExtension(filename, fileBase, sizeof(fileBase));

	file = fopen(va("%s.footsteps", fileBase), "a");
	if (!file) {
		Com_Printf("Could not open footstep file '%s.footsteps' for writing\n", fileBase);
		config.generateFootstepFile = qfalse;
		return;
	}
#ifdef _WIN32
	fprintf(file, "terrain %s {\n}\n\n", textureref[mipTexIndex].name);
#else
	fprintf(file, "%s\n", textureref[mipTexIndex].name);
#endif
	fclose(file);
	footstepsCnt++;
	textureref[mipTexIndex].footstepMarked = qtrue;
}

/**
 * @brief Parses a brush from the map file
 * @sa FindMiptex
 * @param[in] filename The map filename
 */
static void ParseBrush (entity_t *mapent, const char *filename)
{
	mapbrush_t *b;
	int i, j, k, mt;
	side_t *side;
	int planenum;
	brush_texture_t td;
	int planepts[3][3];
	const int checkOrFix = config.performMapCheck || config.fixMap ;

	if (nummapbrushes == MAX_MAP_BRUSHES)
		Sys_Error("nummapbrushes == MAX_MAP_BRUSHES (%i)", nummapbrushes);

	b = &mapbrushes[nummapbrushes];
	memset(b, 0, sizeof(*b));
	b->original_sides = &brushsides[nummapbrushsides];
	b->entitynum = num_entities - 1;
	b->brushnum = nummapbrushes - mapent->firstbrush;

	b->isTerrain = (!strcmp("func_group", ValueForKey(&entities[b->entitynum], "classname"))
				&& strlen(ValueForKey(&entities[b->entitynum], "terrain")) > 0);
	b->isGenSurf = (!strcmp("func_group", ValueForKey(&entities[b->entitynum], "classname"))
				&& strlen(ValueForKey(&entities[b->entitynum], "gensurf")) > 0);

	do {
		if (!GetToken(qtrue))
			break;
		if (*parsedToken == '}')
			break;

		if (nummapbrushsides == MAX_MAP_BRUSHSIDES)
			Sys_Error("nummapbrushsides == MAX_MAP_BRUSHSIDES (%i)", nummapbrushsides);
		side = &brushsides[nummapbrushsides];

		/* read the three point plane definition */
		for (i = 0; i < 3; i++) {
			if (i != 0)
				GetToken(qtrue);
			if (*parsedToken != '(')
				Sys_Error("parsing brush at line %i", GetScriptLine());

			for (j = 0; j < 3; j++) {
				GetToken(qfalse);
				planepts[i][j] = atoi(parsedToken);
			}

			GetToken(qfalse);
			if (*parsedToken != ')')
				Sys_Error("parsing brush at line %i", GetScriptLine());
		}

		/* read the texturedef */
		GetToken(qfalse);
		if (strlen(parsedToken) >= sizeof(td.name)) {
			Com_Printf("ParseBrush: texture name too long (limit "UFO_SIZE_T"): %s\n", sizeof(td.name), parsedToken);
			if (config.fixMap)
				Sys_Error("Exiting, as -fix is active and saving might corrupt *.map by truncating texture name");
		}
		Q_strncpyz(td.name, parsedToken, sizeof(td.name));

		GetToken(qfalse);
		td.shift[0] = atoi(parsedToken);
		GetToken(qfalse);
		td.shift[1] = atoi(parsedToken);
		GetToken(qfalse);
		td.rotate = atoi(parsedToken);
		GetToken(qfalse);
		td.scale[0] = atof(parsedToken);
		GetToken(qfalse);
		td.scale[1] = atof(parsedToken);

		/* find default flags and values */
		mt = FindMiptex(td.name);
		if (mt >= 0) {
			td.value = textureref[mt].value;
			side->contentFlags = textureref[mt].contentFlags;
			side->surfaceFlags = td.surfaceFlags = textureref[mt].surfaceFlags;
		} else {
			side->surfaceFlags = td.surfaceFlags = side->contentFlags = td.value = 0;
		}

		if (TokenAvailable()) {
			GetToken(qfalse);
			side->contentFlags = atoi(parsedToken);
			GetToken(qfalse);
			side->surfaceFlags = td.surfaceFlags = atoi(parsedToken);
			GetToken(qfalse);
			td.value = atoi(parsedToken);
		} else {
			side->contentFlags = checkOrFix ? 0 : CONTENTS_SOLID;
			side->surfaceFlags = 0;
			td.value = 0;
		}

		/* resolve implicit surface and contents flags */
		SetImpliedFlags(side, td.name, b);

		/* translucent objects are automatically classified as detail */
		if (side->surfaceFlags & (SURF_TRANS33 | SURF_TRANS66 | SURF_ALPHATEST))
			side->contentFlags |= CONTENTS_DETAIL;
		if (config.fulldetail)
			side->contentFlags &= ~CONTENTS_DETAIL;
		if (!checkOrFix)
			if (!(side->contentFlags & (LAST_VISIBLE_CONTENTS - 1)))
				side->contentFlags |= CONTENTS_SOLID;

		/* hints and skips are never detail, and have no content */
		if (side->surfaceFlags & (SURF_HINT | SURF_SKIP)) {
			side->contentFlags = 0;
			side->surfaceFlags &= ~CONTENTS_DETAIL;
		}

		/* check whether the flags are ok */
		CheckFlags(side, b);

		/* generate a list of textures that should have footsteps when walking on them */
		if (mt > 0 && side->surfaceFlags & SURF_FOOTSTEP)
			GenerateFootstepList(filename, mt);
		GenerateMaterialFile(filename, mt, b, &td, side);

		/* find the plane number */
		planenum = PlaneFromPoints(b, planepts[0], planepts[1], planepts[2]);
		if (planenum == PLANENUM_LEAF) {
			Com_Printf("Entity %i, Brush %i: plane with no normal at line %i\n", b->entitynum, b->brushnum, GetScriptLine());
			continue;
		}

		for (j = 0; j < 3; j++)
			VectorCopy(planepts[j], mapplanes[planenum].planeVector[j]);

		/* see if the plane has been used already */
		for (k = 0; k < b->numsides; k++) {
			const side_t *s2 = b->original_sides + k;
			if (s2->planenum == planenum) {
				Com_Printf("Entity %i, Brush %i: duplicate plane at line %i\n", b->entitynum, b->brushnum, GetScriptLine());
				break;
			}
			if (s2->planenum == (planenum ^ 1) ) {
				Com_Printf("Entity %i, Brush %i: mirrored plane at line %i\n", b->entitynum, b->brushnum, GetScriptLine());
				break;
			}
		}
		if (k != b->numsides)
			continue;		/* duplicated */

		/* keep this side */
		side = b->original_sides + b->numsides;
		side->planenum = planenum;
		side->texinfo = TexinfoForBrushTexture(&mapplanes[planenum],
			&td, vec3_origin, b->isTerrain);
		side->brush = b;

		/* save the td off in case there is an origin brush and we
		 * have to recalculate the texinfo */
		side_brushtextures[nummapbrushsides] = td;

		nummapbrushsides++;
		b->numsides++;
	} while (1);

	/* get the content for the entire brush */
	b->contentFlags = BrushContents(b);

	/* allow detail brushes to be removed  */
	if (config.nodetail && (b->contentFlags & CONTENTS_DETAIL)) {
		b->numsides = 0;
		return;
	}

	/* allow water brushes to be removed */
	if (config.nowater && (b->contentFlags & CONTENTS_WATER)) {
		b->numsides = 0;
		return;
	}

	/* create windings for sides and bounds for brush */
	MakeBrushWindings(b);

	/* origin brushes are removed, but they set
	 * the rotation origin for the rest of the brushes (like func_door)
	 * in the entity. After the entire entity is parsed, the planenums
	 * and texinfos will be adjusted for the origin brush */
	if (!config.fixMap && b->contentFlags & CONTENTS_ORIGIN) {
		char string[32];
		vec3_t origin;

		if (num_entities == 1) {
			Sys_Error("Entity %i, Brush %i: origin brushes not allowed in world"
				, b->entitynum, b->brushnum);
			return;
		}

		VectorCenterFromMinsMaxs(b->mins, b->maxs, origin);

		Com_sprintf(string, sizeof(string), "%i %i %i", (int)origin[0], (int)origin[1], (int)origin[2]);
		SetKeyValue(&entities[b->entitynum], "origin", string);
		Sys_FPrintf(SYS_VRB, "Entity %i, Brush %i: set origin to %s\n", b->entitynum, b->brushnum, string);

		VectorCopy(origin, entities[b->entitynum].origin);

		/* don't keep this brush */
		b->numsides = 0;

		return;
	}

	if (!checkOrFix)
		AddBrushBevels(b);

	nummapbrushes++;
	mapent->numbrushes++;
}

/**
 * @brief Takes all of the brushes from the current entity and adds them to the world's brush list.
 * @note Used by func_group
 */
static void MoveBrushesToWorld (entity_t *mapent)
{
	int newbrushes, worldbrushes, i;
	mapbrush_t *temp;

	/* this is pretty gross, because the brushes are expected to be
	 * in linear order for each entity */

	newbrushes = mapent->numbrushes;
	worldbrushes = entities[0].numbrushes;

	temp = malloc(newbrushes * sizeof(mapbrush_t));
	memcpy(temp, mapbrushes + mapent->firstbrush, newbrushes * sizeof(mapbrush_t));

	/* make space to move the brushes (overlapped copy) */
	memmove(mapbrushes + worldbrushes + newbrushes,
		mapbrushes + worldbrushes,
		sizeof(mapbrush_t) * (nummapbrushes - worldbrushes - newbrushes));

	/* copy the new brushes down */
	memcpy(mapbrushes + worldbrushes, temp, sizeof(mapbrush_t) * newbrushes);

	/* fix up indexes */
	entities[0].numbrushes += newbrushes;
	for (i = 1; i < num_entities; i++)
		entities[i].firstbrush += newbrushes;
	free(temp);

	mapent->numbrushes = 0;
}

static void AdjustBrushesForOrigin (const entity_t *ent)
{
	int i, j;

	for (i = 0; i < ent->numbrushes; i++) {
		mapbrush_t *b = &mapbrushes[ent->firstbrush + i];
		for (j = 0; j < b->numsides; j++) {
			side_t *s = &b->original_sides[j];
			const ptrdiff_t index = s - brushsides;
			const vec_t newdist = mapplanes[s->planenum].dist -
				DotProduct(mapplanes[s->planenum].normal, ent->origin);
			s->planenum = FindFloatPlane(mapplanes[s->planenum].normal, newdist);
			s->texinfo = TexinfoForBrushTexture(&mapplanes[s->planenum],
				&side_brushtextures[index], ent->origin, qfalse);
			s->brush = b;
		}
		/* create windings for sides and bounds for brush */
		MakeBrushWindings(b);
	}
}

/**
 * @brief Checks whether this entity is an inline model that should have brushes
 * @param[in] entName
 * @returns true if the name of the entity implies, that this is an inline model
 */
static inline qboolean IsInlineModelEntity (const char *entName)
{
	qboolean inlineModelEntity = (!strcmp("func_breakable", entName)
			|| !strcmp("func_door", entName)
			|| !strcmp("func_rotating", entName));
	return inlineModelEntity;
}

entity_t *FindTargetEntity (const char *target)
{
	int i;

	for (i = 0; i < num_entities; i++) {
		const char *n = ValueForKey(&entities[i], "targetname");
		if (!strcmp(n, target))
			return &entities[i];
	}

	return NULL;
}

/**
 * @brief Parsed map entites and brushes
 * @sa ParseBrush
 * @param[in] filename The map filename
 */
static qboolean ParseMapEntity (const char *filename)
{
	entity_t *mapent;
	epair_t *e;
	const char *entName;
	int startbrush, startsides;
	static int worldspawnCount = 0;

	if (!GetToken(qtrue))
		return qfalse;

	if (*parsedToken != '{')
		Sys_Error("ParseMapEntity: { not found");

	if (num_entities == MAX_MAP_ENTITIES)
		Sys_Error("num_entities == MAX_MAP_ENTITIES (%i)", num_entities);

	startbrush = nummapbrushes;
	startsides = nummapbrushsides;

	mapent = &entities[num_entities++];
	memset(mapent, 0, sizeof(*mapent));
	mapent->firstbrush = nummapbrushes;
	mapent->numbrushes = 0;

	do {
		if (!GetToken(qtrue))
			Sys_Error("ParseMapEntity: EOF without closing brace");
		if (*parsedToken == '}')
			break;
		if (*parsedToken == '{')
			ParseBrush(mapent, filename);
		else {
			e = ParseEpair();
			e->next = mapent->epairs;
			mapent->epairs = e;
		}
	} while (qtrue);

	GetVectorForKey(mapent, "origin", mapent->origin);

	/* if there was an origin brush, offset all of the planes and texinfo - e.g. func_door or func_rotating */
	if (VectorNotEmpty(mapent->origin))
		AdjustBrushesForOrigin(mapent);

	/* group entities are just for editor convenience
	 * toss all brushes into the world entity */
	entName = ValueForKey(mapent, "classname");
	if (!config.performMapCheck && !config.fixMap && !strcmp("func_group", entName)) {
		MoveBrushesToWorld(mapent);
		mapent->numbrushes = 0;
		num_entities--;
	} else if (IsInlineModelEntity(entName)) {
		if (mapent->numbrushes == 0) {
			num_entities--;
			Com_Printf("Warning: %s has no brushes assigned\n", entName);
		}
	} else if (!strcmp("worldspawn", entName)) {
		worldspawnCount++;
		if (worldspawnCount > 1)
			Com_Printf("Warning: more than one %s in one map\n", entName);
	}
	return qtrue;
}

/**
 * @brief Recurse down the epair list
 * @note First writes the last element
 */
static inline void WriteMapEntities (FILE *f, const epair_t *e)
{
	if (!e)
		return;

	if (e->next)
		WriteMapEntities(f, e->next);

	fprintf(f, "\"%s\" \"%s\"\n", e->key, e->value);
}

/**
 * @sa LoadMapFile
 * @sa FixErrors
 */
void WriteMapFile (const char *filename)
{
	FILE *f;
	int i, j, k;
	int removed;

	Com_Printf("writing map: '%s'\n", filename);

	f = fopen(filename, "wb");

	removed = 0;
	fprintf(f, "\n");
	for (i = 0; i < num_entities; i++) {
		const entity_t *mapent = &entities[i];
		const epair_t *e = mapent->epairs;

		/* maybe we don't want to write it back into the file */
		if (mapent->skip) {
			removed++;
			continue;
		}
		fprintf(f, "// entity %i\n{\n", i - removed);
		WriteMapEntities(f, e);

		for (j = 0; j < mapent->numbrushes; j++) {
			const mapbrush_t *brush = &mapbrushes[mapent->firstbrush + j];
			fprintf(f, "// brush %i\n{\n", j);
			for (k = 0; k < brush->numsides; k++) {
				const side_t *side = &brush->original_sides[k];
				const ptrdiff_t index = side - brushsides;
				const brush_texture_t *t = &side_brushtextures[index];
				const plane_t *p = &mapplanes[side->planenum];
				fprintf(f, "( %i %i %i ) ", p->planeVector[0][0], p->planeVector[0][1], p->planeVector[0][2]);
				fprintf(f, "( %i %i %i ) ", p->planeVector[1][0], p->planeVector[1][1], p->planeVector[1][2]);
				fprintf(f, "( %i %i %i ) ", p->planeVector[2][0], p->planeVector[2][1], p->planeVector[2][2]);
				fprintf(f, "%s %f %f %f %f %f %i %i %i\n", t->name, t->shift[0], t->shift[1],
					t->rotate, t->scale[0], t->scale[1], side->contentFlags, t->surfaceFlags, t->value);
			}
			fprintf(f, "}\n");
		}
		fprintf(f, "}\n");
	}

	if (removed)
		Com_Printf("removed %i entities\n", removed);
	fclose(f);
}

/**
 * @sa WriteMapFile
 * @sa ParseMapEntity
 */
void LoadMapFile (const char *filename)
{
	int i;

	Sys_FPrintf(SYS_VRB, "--- LoadMapFile ---\n");

	LoadScriptFile(filename);

	nummapbrushsides = 0;
	num_entities = 0;
	/* Create this shortcut to mapTiles[0] */
	curTile = &mapTiles[0];
	/* Set the number of tiles to 1. This is fix for ufo2map right now. */
	numTiles = 1;

	while (ParseMapEntity(filename));

	if (footstepsCnt)
		Com_Printf("Generated footstep file with %i entries\n", footstepsCnt);
	if (materialsCnt)
		Com_Printf("Generated material file with %i entries\n", materialsCnt);

	ClearBounds(map_mins, map_maxs);
	for (i = 0; i < entities[0].numbrushes; i++) {
		if (mapbrushes[i].mins[0] > MAX_WORLD_WIDTH)
			continue;	/* no valid points */
		AddPointToBounds(mapbrushes[i].mins, map_mins, map_maxs);
		AddPointToBounds(mapbrushes[i].maxs, map_mins, map_maxs);
	}

	/* save a copy of the brushes */
	memcpy(mapbrushes + nummapbrushes, mapbrushes, sizeof(mapbrush_t) * nummapbrushes);

	Sys_FPrintf(SYS_VRB, "%5i brushes\n", nummapbrushes);
	Sys_FPrintf(SYS_VRB, "%5i total sides\n", nummapbrushsides);
	Sys_FPrintf(SYS_VRB, "%5i boxbevels\n", c_boxbevels);
	Sys_FPrintf(SYS_VRB, "%5i edgebevels\n", c_edgebevels);
	Sys_FPrintf(SYS_VRB, "%5i entities\n", num_entities);
	Sys_FPrintf(SYS_VRB, "%5i planes\n", nummapplanes);
	Sys_FPrintf(SYS_VRB, "size: %5.0f,%5.0f,%5.0f to %5.0f,%5.0f,%5.0f\n",
		map_mins[0], map_mins[1], map_mins[2], map_maxs[0], map_maxs[1], map_maxs[2]);
}
