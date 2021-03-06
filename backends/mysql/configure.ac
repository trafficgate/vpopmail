#
# $Id$
# Copyright (C) 1999-2009 Inter7 Internet Technologies, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
#

AC_PREREQ([2.52])
AC_INIT(vmysql,1.0.0)
AC_CONFIG_HEADER(vmysql.h)
AC_CANONICAL_HOST

#
# Set default compiler and linker flags
#

AC_SUBST(CFLAGS, "$CFLAGS -I../.. -fPIC")

#
# Programs
#

AC_PROG_CC
AC_PROG_INSTALL
AC_ISC_POSIX

#
# Files
#

AC_MSG_CHECKING(for vpopmail build)

if ! test -r ../module.mk; then
   AC_MSG_RESULT(not found)
   AC_MSG_ERROR(vpopmail is not compiled -- please build the vpopmail source before trying to build modules)
fi

AC_MSG_RESULT(found)

#
# Libraries
#

AC_CHECK_LIB(m, floor)
AC_CHECK_LIB(z, compress)

libdir=""
for f in /usr/local/mysql/lib/mysql /usr/local/mysql/lib  /usr/local/lib/mysql /usr/lib/mysql
do
    if test -d $f
    then
        libdir=$f
		LDFLAGS="$LDFLAGS -L$libdir"
        break
    fi
done

AC_ARG_ENABLE(libdir,
    [  --enable-libdir=DIR     Your MySQL 'lib' directory.],
	[
	  libdir="$enableval"
	  LDFLAGS="$LDFLAGS -L$libdir"
    ]
)

AC_SUBST(libdir)
AC_CHECK_LIB(mysqlclient, mysql_real_connect, [], [ AC_MSG_ERROR(MySQL library not found.  Please use --enable-libdir=/path to specify location.) ])

#
# Authentication logging
#

AC_MSG_CHECKING(if authentication logging is disabled)

AC_ARG_ENABLE(auth-logging,
    [  --disable-auth-logging  Disable authentication logging],
	[
	   AC_MSG_RESULT(yes)
    ],
	[
	  AC_MSG_RESULT(no)
	  AC_DEFINE_UNQUOTED(ENABLE_AUTH_LOGGING,1,"")
    ],
	[
	  AC_MSG_RESULT(no)
	  AC_DEFINE_UNQUOTED(ENABLE_AUTH_LOGGING,1,"")
    ]
)

#
# SQL logging
#

AC_MSG_CHECKING(if SQL logging is enabled)

AC_ARG_ENABLE(sql-logging,
    [  --enable-sql-logging    Enable SQL logging],
	[
	  AC_MSG_RESULT(yes)
	  AC_DEFINE_UNQUOTED(ENABLE_SQL_LOGGING,1,"")
    ],
	[
	   AC_MSG_RESULT(no)
    ]
)

#
# MySQL limits
#

AC_MSG_CHECKING(if MySQL limits is enabled)

AC_ARG_ENABLE(mysql-limits,
    [  --enable-mysql-limits   Enable MySQL limits],
	[
	  AC_MSG_RESULT(yes)
	  AC_DEFINE_UNQUOTED(ENABLE_MYSQL_LIMITS,1,"")
    ],
	[
	   AC_MSG_RESULT(no)
    ]
)

#
# Many domains
#

AC_MSG_CHECKING(for many domains configuration)

AC_ARG_ENABLE(many-domains,
   [  --enable-many-domains   Store domains a single table rather than one table per domain ],
   [
	  AC_MSG_RESULT(yes)
	  AC_DEFINE_UNQUOTED(MANY_DOMAINS,1,"")
   ],
   [
	  AC_MSG_RESULT(no)
   ]
)

#
# Headers
#

incdir=""
for f in /usr/local/mysql/include/mysql /usr/local/mysql/include /usr/local/include/mysql /usr/include/mysql
do
    if test -d $f
    then
        incdir=$f
        break
    fi
done

AC_ARG_ENABLE(incdir,
    [  --enable-incdir=DIR     Your MySQL 'include' directory.],
    incdir="$enableval",
)

AC_SUBST(incdir)

#
# Options
#

#
# IP alias domains
#

AC_MSG_CHECKING(whether ip alias domains is enabled)
AC_ARG_ENABLE(ip-alias-domains,
			  [  --enable-ip-alias-domains         Enable mappings of IPs to domain names ],
			  [ 
			    AC_DEFINE_UNQUOTED(IP_ALIAS_DOMAINS,1,"")
			    AC_MSG_RESULT(yes)
			  ], AC_MSG_RESULT(no))

#
# valias
#

AC_MSG_CHECKING(whether to use native valias)
AC_ARG_ENABLE(valias,
			  [  --enable-valias         Enable native valias, storage of dotqmail data in the MySQL database ],
			  [ 
			    AC_DEFINE_UNQUOTED(VALIAS,1,"")
			    AC_MSG_RESULT(yes)
			  ], AC_MSG_RESULT(no))

#
# Output
#

AC_OUTPUT(Makefile)

echo ""
echo "(Type 'make' to compile)"
echo ""
