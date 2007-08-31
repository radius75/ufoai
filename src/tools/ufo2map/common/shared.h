/**
 * @brief
 * @file shared.h
 */

#ifndef _SHARED_H
#define _SHARED_H

#if defined _WIN64
# define UFO_SIZE_T "%I64u"
#elif defined _WIN32
# define UFO_SIZE_T "%u"
#else
# define UFO_SIZE_T "%zu"
#endif

/* to support the gnuc __attribute__ command */
#ifndef __GNUC__
#  define  __attribute__(x)  /*NOTHING*/
#endif


#ifdef _MSC_VER
#  pragma warning(disable : 4244)     /* MIPS */
#  pragma warning(disable : 4136)     /* X86 */
#  pragma warning(disable : 4051)     /* ALPHA */
#  pragma warning(disable : 4018)     /* signed/unsigned mismatch */
#  pragma warning(disable : 4305)     /* truncate from double to float */
#endif	/* _MSC_VER */

#if defined __STDC_VERSION__
#  if __STDC_VERSION__ < 199901L
#    if defined __GNUC__
/* if we are using ansi - the compiler doesn't know about inline */
#      define inline __inline__
#    elif defined _MSVC
#      define inline __inline
#    else
#      define inline
#    endif
#  endif
#else
#  define inline
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <stddef.h>
#include <assert.h>
#include <limits.h>

#ifndef errno
extern int errno;
#endif

/*========================================================================
The .pak files are just a linear collapse of a directory tree
========================================================================*/

#define MAX_QPATH 64
#define MAX_OSPATH 128

typedef struct qFILE_s {
	void *z; /**< in case of the file being a zip archive */
	FILE *f; /**< in case the file being part of a pak or the actual file */
	char name[MAX_OSPATH];
	unsigned long filepos;
	unsigned long size;
} qFILE;

#ifndef __BYTEBOOL__
#define __BYTEBOOL__
typedef enum {qfalse, qtrue} qboolean;
typedef unsigned char byte;
#endif

typedef struct mapConfig_s {
	float subdivideSize;
	int nice;
	/**< convert function pointer */
	int (*convertFunc) (const char *);
	qboolean verbose;
	qboolean noprune;
	qboolean nodetail;
	qboolean fulldetail;
	qboolean onlyents;
	qboolean nomerge;
	qboolean nowater;
	qboolean nofill;
	qboolean nocsg;
	qboolean noweld;
	qboolean noshare;
	qboolean nosubdiv;
	qboolean notjunc;
	qboolean verboseentities;
	qboolean nobackclip;
	qboolean noradiosity;
	int block_xl, block_xh, block_yl, block_yh;
	float microvolume;
	unsigned int numbounce;
	qboolean extrasamples;
	float subdiv;
	qboolean dumppatches;
	float ambient_red;
	float ambient_green;
	float ambient_blue;
	float maxlight;
	float lightscale;
	byte lightquant;
	float direct_scale;
	float entity_scale;
} mapConfig_t;

extern mapConfig_t config;

void U2M_ProgressBar(void (*func) (unsigned int cnt), unsigned int count, qboolean showProgress, const char *id);

#include "../../../qcommon/qfiles.h"

#if defined(_WIN32)
# ifndef snprintf
#  define snprintf _snprintf
# endif
#endif

extern qboolean verbose;

#define SYS_VRB 0 /* verbose support (on/off) */
#define SYS_STD 1 /* standard print level */
#define SYS_WRN 2 /* warnings */
#define SYS_ERR 3 /* error */

/* the dec offsetof macro doesnt work very well... */
#define myoffsetof(type,identifier) ((size_t)&((type *)0)->identifier)

/* set these before calling CheckParm */
extern int myargc;
extern char **myargv;

extern char qdir[1024];
extern char gamedir[1024];

extern qboolean archive;
extern char archivedir[1024];

void Error(const char *error, ...) __attribute__((noreturn, format(printf, 1, 2)));
void Sys_Printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void Sys_FPrintf(int flag, const char *text, ...) __attribute__((format(printf, 2, 3)));

#endif /* _SHARED_H */

