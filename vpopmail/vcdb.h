/*
 * $Id: vcdb.h,v 1.2 2003-10-20 18:59:57 tomcollins Exp $
 * Copyright (C) 1999-2003 Inter7 Internet Technologies, Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef VPOPMAIL_CDB_H
#define VPOPMAIL_CDB_H

#define VPASSWD_FILE         "vpasswd"
#define VPASSWD_BAK_FILE     "vpasswd.bak"
#define VPASSWD_LOCK_FILE    ".vpasswd.lock"
#define VPASSWD_CDB_FILE     "vpasswd.cdb"
#define VPASSWD_CDB_TMP_FILE "cdb.tmp"

int make_vpasswd_cdb(char *domain);
struct vqpasswd *vgetpw(char *, char *, struct vqpasswd *, int);
void set_vpasswd_files( char *);
int vauth_adduser_line(FILE *, char *, char *, char *, char *, char *, int);

#endif
