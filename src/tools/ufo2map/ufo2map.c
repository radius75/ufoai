/**
 * @file ufo2map.c
 * @brief Starting point for map compiler
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

#define VERSION "1.2.3"

/* valid -nolighting parameters */
#define	LIGHTING_NONE			1
#define	LIGHTING_DAY_ONLY		2
#define	LIGHTING_NIGHT_ONLY	3

#include "lighting.h"
#include "bsp.h"
#include "check/check.h"
#include "../../shared/shared.h"
#include "ufo2map.h"

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

mapConfig_t config;
static char mapFilename[MAX_OSPATH];

struct memPool_s *com_genericPool;
struct memPool_s *com_fileSysPool;

char baseFilename[MAX_OSPATH]; /**< This is used for extra file output functions */

typedef struct usagePair_s {
	const char *flags;	/**< The command line flag (and maybe an alias), or a full line (in which case the desc should be NULL */
	const char *desc;/**< a description of the functionality provided by the flag, or NULL if the flags is actually a full line  */
} usagePair_t;

static const usagePair_t usageArray[] = {
	{"Usage: ufo2map <param1 <subparam1> <subparam2> <...>> <param2> <...> [map]", NULL},
#ifdef _WIN32
	{"Even on Windows, use / slashes in the path", NULL},
#endif
	{"\nGeneral options:",NULL},
	{" -h --help", "print (this) help and exit"},
#ifndef _WIN32
	{" -nice <prio>","priority level [unix nice level from -20 to 19 where 19 is the lowest priority]"},
#else
	{" -nice <prio>","priority level [0 = HIGH, 1 = NORMAL, 2 = IDLE]"},
#endif
	{" -nofootstep","don't generate a footstep file"},
	{" -tracefile","generate two csv files describing the floors and walls found by the trace functions"},
	{" -debugfile (TODO)","generate a trace debug file. The client can load the file to highlight map obstructions"},
	{" -onlynewer","only proceed when the map is newer than the bsp"},
	{" -stats --statistics","print statistics and quit. may be used with -check or -fix"},
	{" -v --verbosity <int>","set verbosity. higher <int> gives more output"},
	{NULL, "if it is required, this should be the first option"},
	{NULL, "0 - no stdout, 1 - only check/fix messages, 2  - (compile) only mapname"},
	{NULL, "2 - (check/fix) mapname if findings, 4 - normal output,"},
	{NULL, "5 - extra output, 6 - dump (a lot extra from BSPing)"},
	{" -material","generate a material (.mat) file, do not proceed to compilation"},
	{"\nLighting options:", NULL},
	{" -extra","extra light samples"},
	{" -nolighting TYPE","don't perform the lighting calculations, where TYPE is one of day, night, all"},
	{NULL, "default is all"},
	{" -quant","lightquant - lightmap resolution downscale (e.g. 4 = 1 << 4) (values between 1 and 6)"},
	{" -scale","global light scale factor"},
	{" -saturation","saturation factor (e.g. 1.5 - default is 1.0)"},
	{" -contrast","contrast factor (e.g. 1.05, default is 1.0)"},
	{" -t --threads","thread amount"},
	{"\nBinary space partitioning (BSPing) options:", NULL},
	{" -block <xl> <yl>",""},
	{" -blocks <xl> <yl> <xh> <yh>",""},
	{" -subdivide","subdivide brushes for better light effects (but higher polycount)"},
	{" -surface","surface light scaling (float value)"},
	{" -entity","entity light scaling (float value)"},
	{" -fulldetail","don't treat details (and trans surfaces) as details"},
	{" -info","print bsp file info"},
	{" -micro <float>","warn if a brush has a volume lower than the specified float."},
	{NULL, "brushes are tested after CSG."},
	{" -nobackclip","draw downward pointing faces. (so actors cannot see up through floors"},
	{NULL,"in first person view). default is to set SURF_NODRAW to downard faces."},
	{" -nocsg",""},
	{" -nodetail","skip detail brushes"},
	{" -nomerge","skip node face merging"},
	{" -noprune","don't prune (or cut) nodes"},
	{" -noshare",""},
	{" -notjunc",""},
	{" -nowater","skip water brushes in compilation"},
	{" -noweld",""},
	{" -onlyents","modify existing bsp file with entities from map file"},
	{" -verboseentities","also be verbose about submodels (entities)"},
	{"\nMapping options:", NULL},
	{"\n These options operate on map file only. No bsp file is created.", NULL},
	{" Output prefixed by an asterisk (*) indicates operations that would change the map file.", NULL},
	{" -check","check source map, only print information."},
	{" -fix","same subparameters as -check, changes the source map file."},
	{" \n subparameters for -check and -fix", NULL},
	{"    all","performs all checks and fixes. This is the default."},
	{"    bru brushes","includes 'lvl tex mfc mbr'. Performs all checks and fixes associated with brushes."},
	{"    ent entities","performs all checks and fixes associated with entities."},
	{"    con contained","checks for brushes contained entirely within other brushes. includes coincident duplicates."},
	{"    isc intersection","report intersection between optimisable brushes from worldspawn and func_group entities"},
	{NULL, "this is not included in all or bru as it is not always a bad thing"},
	{"    mbr microbrush <float> ","test for brushes smaller than <float> unit^3. this is done without the csg"},
	{NULL, "step, unlike the bsp -micro option. default 1 unit^3."},
	{"    lvl levelflags","if no levelflags for a brush or entity are set, all of them are set"},
	{"    flv filllevelflags","ensure set levelflag bits are uninterrupted"},
	{"    ndr nodraws","assigns SURF_NODRAW to hidden faces"},
	{"    tex textures","warns when no texture or error texture is assigned."},
	{NULL, "ensures special textures and content/surface flags are consistent."},
	{"    mfc mixedfacecontents","ensures the contentflags are the same on each face of each brush."},
	{"    zft zfighting","intersecting brushes with a common face: prevent textures shimmering together"},

	{NULL, NULL}
};

/**
 * @brief print usage information.
 */
static void Usage (void)
{
	const usagePair_t *v;
	int maxFlagsLen = 0;

	/* run through to find the length of the longest
	 * flags string */
	for (v = usageArray; v->flags || v->desc; v++)
		if (v->flags && v->desc) {
			const int len = strlen(v->flags);
			maxFlagsLen = len > maxFlagsLen ? len : maxFlagsLen;
		}

	for (v = usageArray; v->flags || v->desc; v++) {
		if (v->flags && v->desc)
			Com_Printf("%-*s: %s\n", maxFlagsLen, v->flags, v->desc);
		else if (v->desc)
			Com_Printf("%*s  %s\n", maxFlagsLen, "", v->desc);
		else /* must be v->flags only, a full line not describing a flag */
			Com_Printf("%s\n",v->flags);
	}
}

/**
 * @brief
 */
void Com_Printf (const char *format, ...)
{
	char out_buffer[4096];
	va_list argptr;

	va_start(argptr, format);
	Q_vsnprintf(out_buffer, sizeof(out_buffer), format, argptr);
	va_end(argptr);

	printf("%s", out_buffer);
}

/**
 * @brief return nonzero if printing should be aborted based on the command line verbosity
 * level and the importance of the message
 * @param msgVerbLevel insignificance of the message. Larger numbers mean the message is
 * less important. The message will first be printed if the msgVerbLevel is equal to the config.verbosity.
 * @sa verbosityLevel_t
 */
qboolean AbortPrint (const verbosityLevel_t msgVerbLevel)
{
	return (msgVerbLevel > config.verbosity);
}

/**
 * @brief decides wether to proceed with output based on verbosity level
 * @sa Com_Printf, Check_Printf, AbortPrint
 */
void Verb_Printf (const verbosityLevel_t msgVerbLevel, const char *format, ...)
{
	if (AbortPrint(msgVerbLevel))
		return;

	{
		char out_buffer[4096];
		va_list argptr;

		va_start(argptr, format);
		Q_vsnprintf(out_buffer, sizeof(out_buffer), format, argptr);
		va_end(argptr);

		printf("%s", out_buffer);
	}
}

/**
 * @brief Check for bsping, lighting and checking/fixing command line parameters
 */
static void U2M_Parameter (int argc, const char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "-verbosity")) {
			/* arg to -v should be a single digit. if it is not a number
			 * atoi will return 0, and no warning will be given. so check that
			 * it looks like the arg for -v first */
			if (strlen(argv[i+1]) == 1)
				config.verbosity = atoi(argv[++i]);
			Verb_Printf(VERB_LESS, "verbosity = %i\n", config.verbosity);
		} else if (!strcmp(argv[i], "-noweld")) {
			/* make every point unique */
			Verb_Printf(VERB_LESS, "noweld = true\n");
			config.noweld = qtrue;
		} else if (!strcmp(argv[i], "--statistics") || !strcmp(argv[i], "-stats")) {
			Verb_Printf(VERB_LESS, "statistics mode\n");
			config.stats = qtrue;
			config.performMapCheck = qtrue;
		} else if (!strcmp(argv[i], "-check") || !strcmp(argv[i], "-fix")) {
			/* check for subparameters terminate loop before last arg (path) or
			 * when we hit a param (as opposed to a subparam).
			 * full parameters are prefixed with "-". */
			const int iInitial = i;

			if (!strcmp(argv[i], "-check")) {
				Verb_Printf(VERB_LESS, "check = true\n");
				config.performMapCheck = qtrue;
			}
			if (!strcmp(argv[i], "-fix")) {
				Verb_Printf(VERB_LESS, "fix = true\n");
				config.fixMap = qtrue;
			}
			while (++i < (argc - 1) && argv[i][0] != '-') {
				if (!strcmp(argv[i], "entities") || !strcmp(argv[i], "ent")) {
					Verb_Printf(VERB_LESS, "  %s entities\n", config.fixMap ? "fixing" : "checking");
					config.chkEntities = qtrue;
				} else if (!strcmp(argv[i], "brushes") || !strcmp(argv[i], "bru")) {
					Verb_Printf(VERB_LESS, "  %s brushes\n", config.fixMap ? "fixing" : "checking");
					config.chkBrushes = qtrue;
				} else if (!strcmp(argv[i], "contained") || !strcmp(argv[i], "con")) {
					Verb_Printf(VERB_LESS, "  %s contained brushes\n", config.fixMap ? "fixing" : "checking");
					config.chkContained = qtrue;
				} else if (!strcmp(argv[i], "filllevelflags") || !strcmp(argv[i], "flv")) {
					Verb_Printf(VERB_LESS, "  %s filllevelflags\n", config.fixMap ? "fixing" : "checking");
					config.chkFillLevelFlags = qtrue;
				} else if (!strcmp(argv[i], "levelflags") || !strcmp(argv[i], "lvl")) {
					Verb_Printf(VERB_LESS, "  %s levelflags\n", config.fixMap ? "fixing" : "checking");
					config.chkLevelFlags = qtrue;
				} else if (!strcmp(argv[i], "textures") || !strcmp(argv[i], "tex")) {
					Verb_Printf(VERB_LESS, "  %s textures\n", config.fixMap ? "fixing" : "checking");
					config.chkTextures = qtrue;
				} else if (!strcmp(argv[i], "nodraws") || !strcmp(argv[i], "ndr")) {
					Verb_Printf(VERB_LESS, "  %s nodraws\n", config.fixMap ? "fixing" : "checking");
					config.chkNodraws = qtrue;
				} else if (!strcmp(argv[i], "intersection") || !strcmp(argv[i], "isc")) {
					Verb_Printf(VERB_LESS, "  %s intersection\n", config.fixMap ? "fixing" : "checking");
					config.chkIntersection = qtrue;
				} else if (!strcmp(argv[i], "mixedfacecontents") || !strcmp(argv[i], "mfc")) {
					Verb_Printf(VERB_LESS, "  %s mixedfacecontents\n", config.fixMap ? "fixing" : "checking");
					config.chkMixedFaceContents = qtrue;
				} else if (!strcmp(argv[i], "microbrush") || !strcmp(argv[i], "mbr")) {
					config.chkMMicro = qtrue;
					if (atof(argv[i + 1]) > 0.0001) {
						config.mapMicrovol = atof(argv[i + 1]);
						i++;
					}
					Verb_Printf(VERB_LESS, "  checking map for microbrushes smaller than %f unit^3\n", config.mapMicrovol);
				} else if (!strcmp(argv[i], "zfighting") || !strcmp(argv[i], "zft")) {
					Verb_Printf(VERB_LESS, "  %s for z-fighting\n", config.fixMap ? "fixing" : "checking");
					config.chkZFight = qtrue;
				} else if (!strcmp(argv[i], "all")) {
					Verb_Printf(VERB_LESS, "  %s all (entites brushes)\n", config.fixMap ? "fixing" : "checking");
					config.chkAll = qtrue;
				} else {
					Verb_Printf(VERB_LESS, "  WARNING: %s subparameter not understood:%s  try --help for more info\n", config.fixMap ? "fix" : "check", argv[i]);
				}
			}
			i--;
			/* if no subparams set, assume all */
			if (i == iInitial) {
				Verb_Printf(VERB_LESS, "  no %s subparameters set, assuming all\n", config.fixMap ? "fix" : "check");
				config.chkAll = qtrue;
			}
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			Usage();
			exit(0);
		} else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "-threads")) {
			threadstate.numthreads = atoi(argv[++i]);
			Verb_Printf(VERB_LESS, "threads: #%i\n", threadstate.numthreads);
		} else if (!strcmp(argv[i], "-info")) {
			config.info = qtrue;
		} else if (!strcmp(argv[i], "-nocsg")) {
			Verb_Printf(VERB_LESS, "nocsg = true\n");
			config.nocsg = qtrue;
		} else if (!strcmp(argv[i], "-noshare")) {
			Verb_Printf(VERB_LESS, "noshare = true\n");
			config.noshare = qtrue;
		} else if (!strcmp(argv[i], "-onlynewer")) {
			Verb_Printf(VERB_LESS, "onlynewer = true\n");
			config.onlynewer = qtrue;
		} else if (!strcmp(argv[i], "-notjunc")) {
			Verb_Printf(VERB_LESS, "notjunc = true\n");
			config.notjunc = qtrue;
		} else if (!strcmp(argv[i], "-nowater")) {
			Verb_Printf(VERB_LESS, "nowater = true\n");
			config.nowater = qtrue;
		} else if (!strcmp(argv[i], "-nice")) {
#ifdef HAVE_SETPRIORITY
			config.nice = atoi(argv[++i]);
			Verb_Printf(VERB_LESS, "nice = %i\n", config.nice);
			if (setpriority(PRIO_PROCESS, 0, config.nice))
				Verb_Printf(VERB_LESS, "failed to set nice level of %i\n", config.nice);
#elif defined _WIN32
			HANDLE proc = GetCurrentProcess();
			config.nice = atoi(argv[++i]);
			Verb_Printf(VERB_LESS, "nice = %i\n", config.nice);
			switch (config.nice) {
			case 0:
				SetPriorityClass(proc, HIGH_PRIORITY_CLASS);
				Verb_Printf(VERB_LESS, "Priority changed to HIGH\n");
				break;
			case 1:
				SetPriorityClass(proc, NORMAL_PRIORITY_CLASS);
				Verb_Printf(VERB_LESS, "Priority changed to NORMAL\n");
				break;
			default:
				SetPriorityClass(proc, IDLE_PRIORITY_CLASS);
				Verb_Printf(VERB_LESS, "Priority changed to IDLE\n");
				break;
			}
			CloseHandle(proc);
#else
			Verb_Printf(VERB_LESS, "nice not implemented for this arch\n");
			i++;
#endif
		} else if (!strcmp(argv[i], "-noprune")) {
			Verb_Printf(VERB_LESS, "noprune = true\n");
			config.noprune = qtrue;
		} else if (!strcmp(argv[i],"-nofootstep")) {
			config.generateFootstepFile = qfalse;
			Verb_Printf(VERB_LESS, "generateFootstepFile = false\n");
		} else if (!strcmp(argv[i],"-tracefile")) {
			config.generateTraceFile = qtrue;
			Verb_Printf(VERB_NORMAL, "generateTraceFile = true\n");
		} else if (!strcmp(argv[i],"-debugtrace")) {
			config.generateDebugTrace = qtrue;
			Verb_Printf(VERB_NORMAL, "generateDebugTrace = true\n");
		} else if (!strcmp(argv[i],"-material")) {
			config.generateMaterialFile = qtrue;
			Verb_Printf(VERB_LESS, "generateMaterialFile = true\n");
		} else if (!strcmp(argv[i], "-nomerge")) {
			Verb_Printf(VERB_LESS, "nomerge = true\n");
			config.nomerge = qtrue;
		} else if (!strcmp(argv[i], "-nosubdiv")) {
			Verb_Printf(VERB_LESS, "nosubdiv = true\n");
			config.nosubdiv = qtrue;
		} else if (!strcmp(argv[i], "-nodetail")) {
			Verb_Printf(VERB_LESS, "nodetail = true\n");
			config.nodetail = qtrue;
		} else if (!strcmp(argv[i], "-fulldetail")) {
			Verb_Printf(VERB_LESS, "fulldetail = true\n");
			config.fulldetail = qtrue;
		} else if (!strcmp(argv[i], "-onlyents")) {
			Verb_Printf(VERB_LESS, "onlyents = true\n");
			config.onlyents = qtrue;
		} else if (!strcmp(argv[i], "-micro")) {
			config.microvolume = atof(argv[i + 1]);
			Verb_Printf(VERB_LESS, "microvolume = %f\n", config.microvolume);
			i++;
		} else if (!strcmp(argv[i], "-verboseentities")) {
			Verb_Printf(VERB_LESS, "verboseentities = true\n");
			config.verboseentities = qtrue;
		} else if (!strcmp(argv[i], "-subdivide")) {
			config.subdivideSize = atof(argv[i + 1]);
			Verb_Printf(VERB_LESS, "subdivide_size = %f\n", config.subdivideSize);
			i++;
		} else if (!strcmp(argv[i], "-block")) {
			config.block_xl = config.block_xh = atoi(argv[i + 1]);
			config.block_yl = config.block_yh = atoi(argv[i + 2]);
			Verb_Printf(VERB_LESS, "block: %i,%i\n", config.block_xl, config.block_yl);
			i += 2;
		} else if (!strcmp(argv[i], "-blocks")) {
			config.block_xl = atoi(argv[i + 1]);
			config.block_yl = atoi(argv[i + 2]);
			config.block_xh = atoi(argv[i + 3]);
			config.block_yh = atoi(argv[i + 4]);
			Verb_Printf(VERB_LESS, "blocks: %i,%i to %i,%i\n",
				config.block_xl, config.block_yl, config.block_xh, config.block_yh);
			i += 4;
		} else if (!strcmp(argv[i], "-nobackclip")) {
			Verb_Printf(VERB_LESS, "nobackclip = true\n");
			config.nobackclip = qtrue;
		} else if (!strcmp(argv[i],"-extra")) {
			config.extrasamples = qtrue;
			Verb_Printf(VERB_LESS, "extrasamples = true\n");
		} else if (!strcmp(argv[i],"-quant")) {
			config.lightquant = (byte)atoi(argv[i + 1]);
			if (config.lightquant < 1 || config.lightquant > 6) {
				config.lightquant = 4;
				Verb_Printf(VERB_LESS, "lightquant must be between 1 and 6\n");
			}
			i++;
		} else if (!strcmp(argv[i],"-scale")) {
			config.brightness = atof(argv[i + 1]);
			i++;
		} else if (!strcmp(argv[i], "-saturation")) {
			config.saturation = atof(argv[i + 1]);
			Verb_Printf(VERB_LESS, "saturation at %f\n", config.saturation);
			i++;
		} else if (!strcmp(argv[i], "-contrast")) {
			config.contrast = atof(argv[i + 1]);
			Verb_Printf(VERB_LESS, "contrast at %f\n", config.contrast);
			i++;
		} else if (!strcmp(argv[i],"-surface")) {
			config.surface_scale *= atof(argv[i + 1]);
			Verb_Printf(VERB_LESS, "surface light scaling at %f\n", config.surface_scale);
			i++;
		} else if (!strcmp(argv[i],"-entity")) {
			config.entity_scale *= atof(argv[i + 1]);
			Verb_Printf(VERB_LESS, "entity light scaling at %f\n", config.entity_scale);
			i++;
		} else if (!strcmp(argv[i], "-nolighting")) {
			if (argc > i + 1) {
				if (!strcmp(argv[i + 1], "day")) {
					Verb_Printf(VERB_LESS, "nolighting = day\n");
					config.nolighting = LIGHTING_NIGHT_ONLY;
					i++;
				} else if (!strcmp(argv[i + 1], "night")) {
					Verb_Printf(VERB_LESS, "nolighting = night\n");
					config.nolighting = LIGHTING_DAY_ONLY;
					i++;
				} else {
					Verb_Printf(VERB_LESS, "nolighting = none\n");
					config.nolighting = LIGHTING_NONE;
				}
			} else {
				Sys_Error("invalid parameter count\n");
			}
		} else if (i < (argc - 1)) {
			/* Last param is the map path, every other param should have been caught by now. */
			Verb_Printf(VERB_LESS, "*** parameter not understood: %s try --help for more info\n", argv[i]);
		}
	}

	if (config.fixMap && config.performMapCheck) {
		Sys_Error("do not specify both -fix and -check\n");
	}

	/* if any check or fix option is active then skip footsteps and materials */
	if (config.performMapCheck || config.fixMap) {
		config.generateFootstepFile = qfalse;
		config.generateMaterialFile = qfalse;
	}
}

/**
 * @brief Set default values
 */
static void U2M_SetDefaultConfigValues (void)
{
	config.verbosity = VERB_NORMAL;

	config.subdivideSize = 1024.0f; /* bsp subdiv */
	config.block_xl = -8;
	config.block_xh = 7;
	config.block_yl = -8;
	config.block_yh = 7;
	config.microvolume = 1.0f;
	config.mapMicrovol = 1.0f; /* this value is up for debate blondandy */

	/* lightmap night values */
	VectorSet(config.sun_ambient_color[LIGHTMAP_NIGHT], 0.07, 0.07, 0.08);
	config.sun_intensity[LIGHTMAP_NIGHT] = 70;
	Vector2Set(config.sun_angles[LIGHTMAP_NIGHT], -80, 220);
	VectorSet(config.sun_color[LIGHTMAP_NIGHT], 0.65, 0.75, 0.95);
	ColorNormalize(config.sun_color[LIGHTMAP_NIGHT], config.sun_color[LIGHTMAP_NIGHT]);
	AngleVectors(config.sun_angles[LIGHTMAP_NIGHT], config.sun_normal[LIGHTMAP_NIGHT], NULL, NULL);

	/* lightmap day values */
	VectorSet(config.sun_ambient_color[LIGHTMAP_DAY], 0.26, 0.26, 0.26);
	config.sun_intensity[LIGHTMAP_DAY] = 280;
	Vector2Set(config.sun_angles[LIGHTMAP_DAY], -75, 100);
	VectorSet(config.sun_color[LIGHTMAP_DAY], 0.90, 0.75, 0.65);
	ColorNormalize(config.sun_color[LIGHTMAP_DAY], config.sun_color[LIGHTMAP_DAY]);
	AngleVectors(config.sun_angles[LIGHTMAP_DAY], config.sun_normal[LIGHTMAP_DAY], NULL, NULL);

	config.saturation = 1.0f;
	config.contrast = 1.0f;
	config.brightness = 1.0;
	config.lightquant = 4;
	config.surface_scale = 0.4f;
	config.entity_scale = 1.0f;

	config.generateFootstepFile = qtrue;

	config.generateTraceFile = qfalse;
	config.generateDebugTrace = qfalse;
}


#ifdef HAVE_SYS_STAT_H
static int CheckTimeDiff (const char *map, const char *bsp)
{
	struct stat mapStat, bspStat;
	if (stat(map, &mapStat) == -1)
		return 0;
	if (stat(bsp, &bspStat) == -1)
		return 0;
	if (difftime(mapStat.st_mtime, bspStat.st_mtime) < 0)
		return 1;
	/* not up-to-date - recompile */
	return 0;
}
#elif defined (_WIN32)
static int CheckTimeDiff (const char *map, const char *bsp)
{
	FILETIME ftCreate, ftAccess, ftWriteMap, ftWriteBsp;
	HANDLE hMapFile, hBspFile;
	int retval = 0;
	/* open the files */
	hMapFile = CreateFile(map, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hMapFile == INVALID_HANDLE_VALUE)
		return 0;
	hBspFile = CreateFile(bsp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hBspFile == INVALID_HANDLE_VALUE){
		CloseHandle(hMapFile);
		return 0;
	}

	/** This is done as two if statements to ensure that ftWriteMap and ftWriteBsp are populated first. */
	if (GetFileTime(hMapFile, &ftCreate, &ftAccess, &ftWriteMap)
		&& GetFileTime(hBspFile, &ftCreate, &ftAccess, &ftWriteBsp)) {
		if (CompareFileTime(&ftWriteMap, &ftWriteBsp) == -1) {
			retval = 1;
		}
	}

	CloseHandle(hMapFile);
	CloseHandle(hBspFile);

	return retval;
}
#endif

/**
 * @brief print name in concise form for lower verbosity levels.
 * verbosity check done before calling this function.
 */
void PrintMapName (void)
{
	const char *mode = NULL;

	if (config.performMapCheck) {
		mode = "[check]";
	} else if (config.fixMap) {
		mode = "[fix]";
	} else {
		mode = "[compile]";
	}
	Com_Printf("%s %s\n", mode, mapFilename);
}


int main (int argc, const char **argv)
{
	char bspFilename[MAX_OSPATH];
	double begin, start, end;
	long size;

	memset(&config, 0, sizeof(config));
	/* init thread state */
	memset(&threadstate, 0, sizeof(threadstate));

	U2M_SetDefaultConfigValues();

	U2M_Parameter(argc, argv);

	Verb_Printf(VERB_NORMAL, "---- ufo2map "VERSION" ----\n");

	if (argc < 2) {
		Usage();
		Sys_Error("At least provide 1 argument: the map filename.");
	}

	Swap_Init();

	start = time(NULL);

	com_genericPool = Mem_CreatePool("ufo2map");
	com_fileSysPool = Mem_CreatePool("ufo2map filesys");

	Verb_Printf(VERB_NORMAL, "path: '%s'\n", argv[argc - 1]);
	FS_InitFilesystem();

	COM_StripExtension(argv[argc - 1], mapFilename, sizeof(mapFilename));
	strncpy(baseFilename, mapFilename, sizeof(baseFilename) - 1);
	strncpy(bspFilename, mapFilename, sizeof(bspFilename) - 1);
	COM_DefaultExtension(mapFilename, sizeof(mapFilename), ".map");
	COM_DefaultExtension(bspFilename, sizeof(bspFilename), ".bsp");

	if (config.info) {
		LoadBSPFile(bspFilename);
		PrintBSPFileSizes();
		Mem_Shutdown();
		return 0;
	}

	Verb_Printf(VERB_NORMAL, "...map: '%s'\n", mapFilename);
	if (!(config.performMapCheck || config.fixMap))
		Verb_Printf(VERB_NORMAL, "...bsp: '%s'\n", bspFilename);

	if (config.verbosity == VERB_MAPNAME && !(config.performMapCheck || config.fixMap))
		PrintMapName();

#if defined (HAVE_SYS_STAT_H) || defined (_WIN32)
	if (config.onlynewer && CheckTimeDiff(mapFilename, bspFilename)) {
		Verb_Printf(VERB_LESS, "bsp file is up-to-date\n");
		Mem_Shutdown();
		return 0;
	}
#endif

	/* if onlyents just grab the entites and resave */
	if (config.onlyents) {
		LoadBSPFile(bspFilename);
		num_entities = 0; /* use the map source entities */

		LoadMapFile(mapFilename);
		SetModelNumbers();

		UnparseEntities();

		WriteBSPFile(bspFilename);
	} else if (config.performMapCheck || config.fixMap) {
		LoadMapFile(mapFilename);
		/* level flags must be fixed before mixed face contents, or they swamp the
		 * console with output, as levelflags are contentflags */
		if (config.chkLevelFlags || config.chkBrushes || config.chkAll)
			CheckLevelFlags();
		if (config.chkFillLevelFlags || config.chkBrushes || config.chkAll)
			CheckFillLevelFlags();
		/* this must be before mfc check, as otherwise mfc warnings are given
		 * which are auto-fixed based on textures */
		if (config.chkTextures || config.chkBrushes || config.chkAll)
			CheckFlagsBasedOnTextures();
		/* mixed face contents check may remove contentflags. this should be done
		 * before tex based on flags check, as tex may replace tex on the basis
		 * of contentflags.*/
		if (config.chkMixedFaceContents || config.chkBrushes || config.chkAll)
			CheckMixedFaceContents();
		if (config.chkTextures || config.chkBrushes || config.chkAll)
			CheckTexturesBasedOnFlags();
		if (config.chkMMicro || config.chkBrushes || config.chkAll)
			CheckMapMicro();
		if (config.chkContained || config.chkBrushes || config.chkAll)
			Check_ContainedBrushes();
		if (config.chkBrushes || config.chkAll)
			CheckBrushes();
		if (config.chkNodraws || config.chkAll)
			CheckNodraws();
		if (config.chkZFight  || config.chkAll || config.chkBrushes)
			CheckZFighting();
		if (config.chkEntities || config.chkAll)
			CheckEntities();
		/* not included in bru or all by design */
		if (config.chkIntersection)
			Check_BrushIntersection();

		if (config.stats)
			Check_Stats();

		if (config.fixMap) {
			/* update dentdata */
			UnparseEntities();
			WriteMapFile(GetScriptFile());
		}
		Mem_Shutdown();
		/** @todo remove these once they all use the memory pool */
		Check_Free();
		FreeEpairs();
		FreeWindings();
		return 0;
	} else if (config.generateMaterialFile) {
		/* start from scratch */
		LoadMapFile(mapFilename);
		config.nolighting = LIGHTING_NONE;
	} else {
		/* start from scratch */
		LoadMapFile(mapFilename);

		CheckNodraws();
		Check_Free();

		SetModelNumbers();

		ProcessModels(bspFilename);
	}

	end = time(NULL);
	Verb_Printf(VERB_LESS, "%5.0f seconds elapsed\n", end - start);
	begin = start;

	if (!config.onlyents && config.nolighting != LIGHTING_NONE) {
		Verb_Printf(VERB_LESS, "----- Lighting ----\n");

		CalcTextureReflectivity();

		if (config.nolighting != LIGHTING_DAY_ONLY) {
			/* compile night version */
			start = time(NULL);
			LightWorld();
			end = time(NULL);
			Verb_Printf(VERB_LESS, "%5.0f seconds elapsed\n", end - start);
		}

		if (config.nolighting != LIGHTING_NIGHT_ONLY) {
			/* compile day version */
			config.compile_for_day = 1;
			start = time(NULL);
			LightWorld();
			end = time(NULL);
			Verb_Printf(VERB_LESS, "%5.0f seconds elapsed\n", end - start);
		}

		Verb_Printf(VERB_LESS, "writing %s\n", bspFilename);
		size = WriteBSPFile(bspFilename);
	} else {
		/* build per-vertex normals for phong shading */
		BuildVertexNormals();
		size = WriteBSPFile(bspFilename);
		end = time(NULL);
	}
	FreeWindings(); /** @todo remove this once the windings are using the memory pool */
	Verb_Printf(VERB_LESS, "sum: %5.0f seconds elapsed - %.1g MB (%li bytes)\n\n", end - begin, (float)size / (1024.0f * 1024.0f), size);

	Com_Printf("%u bytes of memory freed\n", Mem_Shutdown());

	return 0;
}
