/*
 * Copyright (C) 1999-2002 Inter7 Internet Technologies, Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include "config.h"
#include "vpopmail.h"
#include "vauth.h"


#define MAX_BUFF 500

char Email[MAX_BUFF];
char User[MAX_BUFF];
char Domain[MAX_BUFF];
char Passwd[MAX_BUFF];
char TmpBuf1[MAX_BUFF];
int apop;

void usage();
void get_options(int argc,char **argv);

int main(argc,argv)
 int argc;
 char *argv[];
{
 int i;

	get_options(argc,argv);

        if ( (i = parse_email( Email, User, Domain, MAX_BUFF)) != 0 ) {
            printf("Error: %s\n", verror(i));
            vexit(i);
        }

	if ( strlen(Passwd) <= 0 ) {
		strncpy( Passwd, vgetpasswd(Email), MAX_BUFF);
	}

	if ( (i=vpasswd( User, Domain, Passwd, apop )) != 0 ) {
		printf("Error: %s\n", verror(i));
		vexit(i);
	}
	return(vexit(0));

}

void usage()
{
	printf("vpasswd: usage: [options] email_address [password]\n");
	printf("options: -v (print version number)\n");
}

void get_options(int argc,char **argv)
{
 int c;
 int errflag;
 extern char *optarg;
 extern int optind;

	memset(Email, 0, MAX_BUFF);
	memset(Passwd, 0, MAX_BUFF);
	memset(Domain, 0, MAX_BUFF);
	memset(TmpBuf1, 0, MAX_BUFF);
	apop = USE_POP;

	errflag = 0;
    while( !errflag && (c=getopt(argc,argv,"v")) != -1 ) {
		switch(c) {
			case 'v':
				printf("version: %s\n", VERSION);
				break;
			default:
				errflag = 1;
				break;
		}
	}

	if ( optind < argc ) strncpy(Email, argv[optind], MAX_BUFF);
	++optind;
	if ( optind < argc ) strncpy(Passwd, argv[optind], MAX_BUFF);
	++optind;

	if ( Email[0] == 0 ) { 
		usage();
		vexit(-1);
	}
}
