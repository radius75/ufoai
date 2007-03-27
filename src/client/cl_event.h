/**
 * @file cl_event.h
 * @brief Header for geoscape event related stuff.
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

#ifndef CLIENT_CL_EVENT
#define CLIENT_CL_EVENT

#define MAX_EVENTMAILS 64

/**
 * @brief available mails for a tech - mail and mail_pre in script files
 * @sa techMail_t
 * @note Parsed via CL_ParseEventMails
 * @note You can add a mail to the message system and mail client
 * by using e.g. the mission triggers with the script command 'addeventmail <id>'
 */
typedef struct eventMail_s {
	char id[MAX_VAR];			/**< script id */
	char from[MAX_VAR];			/**< sender (_mail_from_paul_navarre, _mail_from_dr_connor) */
	char to[MAX_VAR];			/**< recipient (_mail_to_base_commander) */
	char cc[MAX_VAR];			/**< copy recipient (_mail_to_base_commander) */
	char subject[MAX_VAR];		/**< mail subject line - if mail and mail_pre are available
								 * this will be filled with Proposal: (mail_pre) and Re: (mail)
								 * automatically */
	char date[MAX_VAR];			/**< date string, if empty use the date of research */
	qboolean read;				/**< already read the mail? */
} eventMail_t;

extern void CL_EventAddMail_f(void);
extern void CL_ParseEventMails(const char *name, char **text);

#endif /* CLIENT_CL_EVENT */
