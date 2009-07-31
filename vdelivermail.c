/*
 * $Id$
 * Copyright (C) 1999-2009 Inter7 Internet Technologies, Inc.
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

/* code review/cleanup/rewrite October 2004 through March, 2005
 * by Tom Collins <tom@tomlogic.com>
 *
 */

/* include files */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include "config.h"
#include "vpopmail.h"
#include "vauth.h"
#include "maildirquota.h"
#ifdef MAKE_SEEKABLE
#include "seek.h"
#endif
#include "vlimits.h"
#include "vauthmodule.h"
#include "quota.h"

/* Globals */
#define AUTH_SIZE 300
char TheUser[AUTH_SIZE];
char TheUserFull[AUTH_SIZE];
char TheDomain[AUTH_SIZE];
char TheDomainDir[AUTH_SIZE];
uid_t TheDomainUid;
gid_t TheDomainGid;
char TheDir[AUTH_SIZE];
char CurrentDir[AUTH_SIZE];
char DeliveredTo[AUTH_SIZE];
struct vqpasswd *vpw;
ssize_t message_size = 0;
char bounce[AUTH_SIZE];
int CurrentQuotaSizeFd;

#ifdef QMAIL_EXT
/* the User with '-' and following chars out if any */
char TheUserExt[AUTH_SIZE]; 
char TheExt[AUTH_SIZE];
#endif

#define FILE_SIZE 156
char loop_buf[FILE_SIZE];

#ifdef SPAMASSASSIN
int  InHeaders = 1;
int is_spam();
#endif
int  DeleteMail = 0;
int  local = 1;

#define MSG_BUF_SIZE 5000
char msgbuf[MSG_BUF_SIZE];

#define BUFF_SIZE 300
int fdm;

#define EXIT_BOUNCE 100
#define EXIT_DEFER 111
#define EXIT_OK 0
#define EXIT_OVERQUOTA EXIT_BOUNCE

/* Forward declarations */
int process_valias(void);
void get_arguments(int argc, char **argv);
ssize_t get_message_size();
void deliver_mail(char *address, char *quota);
int check_forward_deliver(char *dir);
int is_looping( char *address );
void run_command(char *prog);
void checkuser(void);
void usernotfound(void);
int is_loop_match( const char *dt, const char *address);
int deliver_quota_warning(const char *dir);


/* print an error string and then exit
 * vexit() never returns, so vexiterr() and vexit() should actually return void
 */
int vexiterr (int err, char *errstr)
{
    if (errstr) printf ("%s\n", errstr);
    return vexit(err);
}

/* 
 * The email message comes in on file descriptor 0 - stdin
 * The user to deliver the email to is in the EXT environment variable
 * The domain to deliver the email to is in the HOST environment variable
 */
int main(int argc, char **argv)
{
   int ret;
    char loopcheck[255];

	ret =vauth_load_module(NULL);
	if (!ret)
	  vexiterror(stderr, "could not load authentication module");

    /* get the arguments to the program and setup things */
    get_arguments(argc, argv);
    snprintf (loopcheck, sizeof(loopcheck), "%s@%s", TheUser, TheDomain);
    if ( is_looping( loopcheck ) == 1 ) {
        vexiterr (EXIT_BOUNCE, "mail is looping");
    }
 
#ifdef VALIAS
    /* process valiases if configured */
    if ( process_valias() == 1 )
        vexiterr (EXIT_OK, "vdelivermail: valiases processed");

    /* if the database is down, deferr */
    if ( verrori == VA_NO_AUTH_CONNECTION )
        vexiterr (EXIT_DEFER, "vdelivermail: deferred, database down");
#endif

    /* get the user from vpopmail database */
    if ((vpw=vauth_getpw(TheUser, TheDomain)) != NULL ) {
        checkuser();
    } 

#ifdef QMAIL_EXT
    /* try and find user that matches the QmailEXT address if: no user found,
     * and the QmailEXT address is different, meaning there was an extension 
     */
    else if ( strcmp(TheUser, TheUserExt) != 0 ) {
        /* get the user from vpopmail database */
        if ((vpw=vauth_getpw(TheUserExt, TheDomain)) != NULL ) {
            checkuser();
        } else {
            usernotfound();
        }
    }
#endif

   /* Additions by Steve Fulton - steve@esoteric.ca 
    * - added error code handling for error codes generated by Autoresponder
    * 99 = deliver mail
    * 100 = bounce mail
    */ 

    else if ( verrori == 100) vexit(EXIT_BOUNCE);

    else if ( verrori == 99) vexit(EXIT_OK);

   /* End of additions by Steve Fulton */

    else if ( verrori != 0 ) vexit(EXIT_DEFER);

    else usernotfound();

    /* exit successfully and have qmail delete the email */
    return(vexit(EXIT_OK));
            
}

/* 
 * Get the command line arguments and the environment variables.
 * Force addresses to be lower case and set the default domain
 */
void get_arguments(int argc, char **argv)
{
#ifdef QMAIL_EXT
 int i;
#endif
 char *tmpstr; 

    if (argc != 3) {
        /* exit with temporary error */
        vexiterr (EXIT_DEFER, "vdelivermail: invalid syntax in .qmail-default file");
    }

    /* get the last parameter in the .qmail-default file */
    strncpy(bounce, argv[2], sizeof(bounce)); 

    if ((tmpstr=getenv("EXT")) == NULL ) {
        vexiterr (EXIT_BOUNCE, "vdelivermail: no EXT environment varilable");
    }
    snprintf (TheUser, sizeof(TheUser), "%s", tmpstr);

    if ((tmpstr=getenv("HOST")) == NULL ) {
        vexiterr (EXIT_BOUNCE, "vdelivermail: no HOST environment varilable");
    }
    snprintf (TheDomain, sizeof(TheDomain), "%s", tmpstr);

    lowerit(TheUser);
    lowerit(TheDomain);

    if ( is_username_valid(TheUser) != 0 )
        vexiterr (EXIT_BOUNCE, "invalid username");
    if ( is_domain_valid(TheDomain) != 0 )
        vexiterr (EXIT_BOUNCE, "invalid domain name");

    snprintf (TheUserFull, sizeof(TheUserFull), "%s", TheUser);

#ifdef QMAIL_EXT
    /* !! Shouldn't this work its way backwards, and try all possibilities?
     * e.g., a-b-c-d should try a-b-c then a-b then a, instead of just a?
     */
    /* delete the '-' and following chars if any and store in TheUserExt */
    for(i = 0; (TheUser[i] != 0) && (TheUser[i] != '-' ); i++) {
        TheUserExt[i] = TheUser[i];
    }
    TheUserExt[i] = 0;

    if ( is_username_valid(TheUserExt) != 0 ) {
        vexit(EXIT_BOUNCE);
    }

    strncpy(TheExt, &TheUser[i+1], AUTH_SIZE);
    for (i = 0; (TheExt[i] != 0); i++) {
      if (TheExt[i] == '.') TheExt[i] = ':';
    }

#endif

    vget_assign(TheDomain, TheDomainDir, sizeof(TheDomainDir), &TheDomainUid, &TheDomainGid);

}

#ifdef VALIAS
/* 
 * Process any valiases for this user@domain
 * 
 * This will look up any valiases in vpopmail and
 * deliver the email to the entries
 *
 * Return 1 if aliases found
 * Return 0 if no aliases found 
 */
int process_valias(void)
{
 int found = 0;
 char *tmpstr;
 char tmpuser[350];
 char def[150];
 int i;
 int j=0;

    /* Get the first alias for this user@domain */
    tmpstr = valias_select( TheUser, TheDomain );

    /* check for wildcard if there's no match */
    if(tmpstr == NULL) {
        for(i=strlen(TheUser);i > 0 && j != 1;--i) {
            if(TheUser[i-1]=='-') {
                tmpuser[0] = '\0';
                strncat(tmpuser,TheUser,i); 
                strcat(tmpuser, "default");
                tmpstr = valias_select( tmpuser, TheDomain );
                if(tmpstr != NULL) {
                    sprintf(def, "DEFAULT=%s", &TheUser[i]);
                    putenv(def);
                    j = 1;
                }
            }
        }
    }

    /* tmpstr will be NULL if there are no more aliases */
    while (tmpstr != NULL ) {

        /* We found one */
        found = 1;

        /* deliver the mail */
        deliver_mail(tmpstr, "");

        /* Get the next alias for this user@domain */
        tmpstr = valias_select_next();
    }

#ifdef QMAIL_EXT 
    /* try and find alias that matches the QmailEXT address 
     * if: no alias found, 
     * and the QmailEXT address is different, meaning there was an extension 
     */
    if ( (!found) && ( strcmp(TheUser, TheUserExt) != 0 )  ) {
        /* Get the first alias for this user@domain */
        tmpstr = valias_select( TheUserExt, TheDomain );

        /* tmpstr will be NULL if there are no more aliases */
        while (tmpstr != NULL ) {

            /* We found one */
            found = 1;

            /* deliver the mail */
            deliver_mail(tmpstr, "");

            /* Get the next alias for this user@domain */
            tmpstr = valias_select_next();
        } 
    }    
#endif

    /* Return whether we found an alias or not */
    return(found);
}
#endif

/* Forks off qmail-inject.  Returns PID of child, or 0 for failure. */
pid_t qmail_inject_open(char *address)
{
 int pim[2];
 pid_t pid;
 static char *binqqargs[4];

    if ( pipe(pim) == -1) return 0;

    switch(pid=vfork()){
      case -1:
        close(pim[0]);
        close(pim[1]);
        printf ("Unable to fork: %d.", errno);
        return 0;
      case 0:
        close(pim[1]);
        if (vfd_move(0,pim[0]) == -1 ) _exit(-1);
        binqqargs[0] = QMAILINJECT;
        binqqargs[1] = "--";
        binqqargs[2] = (*address == '&' ? &address[1] : &address[0]);
        execv(*binqqargs, binqqargs);
        printf ("Unable to launch qmail-inject.");
        exit (EXIT_DEFER);    /* child's exit caught later */
    }
    fdm = pim[1];
    close(pim[0]);
    return(pid);
}

int fdcopy (int write_fd, int read_fd, const char *extra_headers, size_t headerlen, char *address)
{
  char msgbuf[MSG_BUF_SIZE];
  ssize_t file_count;
  struct vlimits limits;
#ifdef SPAMASSASSIN
  long unsigned pid;
  int  pim[2];
#endif
#ifdef MAILDROP
  char maildrop_command[256];
#endif

    /* write the Return-Path: and Delivered-To: headers */
    if (headerlen > 0) {
        if (write(write_fd, extra_headers, headerlen) != headerlen) return -1;
    }

    /* fork the SpamAssassin client - based on work by Alex Dupre */
    if(local==1) {
      vget_limits(TheDomain, &limits);
      if ( vpw==NULL ) {
        parse_email(address, TheUser, TheDomain, AUTH_SIZE);
        vpw=vauth_getpw(TheUser, TheDomain);
      }
#ifdef SPAMASSASSIN
      if ( limits.disable_spamassassin==0 && vpw!=NULL &&
           !(vpw->pw_gid & NO_SPAMASSASSIN) ) {

        if (!pipe(pim)) {
          pid = vfork();
          switch (pid) {
           case -1:
            close(pim[0]);
            close(pim[1]);
            break;
           case 0:
            close(pim[0]);
            dup2(pim[1], 1);
            close(pim[1]);
            if (execl(SPAMC_PROG, SPAMC_PROG, "-f", "-u",
                 address, NULL) == -1) {
              while ((file_count = read(0, msgbuf, MSG_BUF_SIZE)) > 0) {
                write(1, msgbuf, file_count);
              }
              _exit(0);
            }
          }
          close(pim[1]);
          dup2(pim[0], 0);
          close(pim[0]);
        }
    }
#endif

#ifdef MAILDROP
      if ( limits.disable_maildrop==0 && vpw!=NULL &&
           !(vpw->pw_gid & NO_MAILDROP) ) {
	sprintf(maildrop_command, "| preline %s", MAILDROP_PROG);
	run_command(maildrop_command);
	DeleteMail = 1;
	return(0);
      }
#endif
    }

    
    /* read it in chunks and write it to the new file */
    while ((file_count = read(read_fd, msgbuf, sizeof(msgbuf))) > 0) {
#ifdef SPAMASSASSIN
        if ( local==1 && InHeaders==1 &&
             (limits.delete_spam==1 || vpw->pw_gid & DELETE_SPAM) ) {
          printf("check is_spam\n");
          if (is_spam(msgbuf) == 1) {
            DeleteMail = 1;
            return(0);
          }
        }
#endif
        if ( write(write_fd, msgbuf, file_count) == -1 ) return -1;
    }
    
    return 0;
}

void read_quota_from_maildir (const char *maildir, char *qbuf, size_t qlen)
{
  FILE *quota_file;
  char quotafn[FILE_SIZE];
  char *newline;

    if (qlen == 0) return;
    
    *qbuf = '\0';
    snprintf (quotafn, sizeof(quotafn), "%s/maildirsize", maildir);
    quota_file = fopen (quotafn, "r");
    if (quota_file == NULL) {
      /* add code here to use current user's quota if maildir matches */
      if (vpw == NULL) {
        // fprintf (stderr, "Don't know quota for %s\n", maildir);
      } else {
        char *usermaildir;
        
        usermaildir = malloc(strlen(vpw->pw_dir) + sizeof("/Maildir") + 1);
        if (usermaildir) {
          sprintf (usermaildir, "%s/Maildir", vpw->pw_dir);
          if (strcmp (usermaildir, maildir) == 0)
          	snprintf (qbuf, qlen, "%s", vpw->pw_shell);
          // fprintf (stderr, "Quota for %s (from user entry) is %s\n", maildir, quota);
          free (usermaildir);
        }
      }
    } else {
      fgets (qbuf, qlen, quota_file);
      fclose (quota_file);
      newline = strchr (qbuf, '\n');
      if (newline != NULL) *newline = '\0';
      // fprintf (stderr, "Quota for %s (from maildirsize) is %s\n", maildir, quota);
    }
}

/* save email in maildir, starting with extra_headers and then msgsize bytes
 * read from read_fd
 *
 * Return: 0 = success, -1 = over quota, -2 = system failure, -3 = looping
 */
 
int deliver_to_maildir (
    const char *maildir,
    const char *extra_headers,
    int read_fd,
    ssize_t msgsize)
{
#ifdef HOST_NAME_MAX
  char hostname[HOST_NAME_MAX];
#else
  char hostname[256];
#endif
  long unsigned pid, tm;
  char local_file_tmp[FILE_SIZE];
  char local_file_new[FILE_SIZE];
  size_t headerlen;
  int write_fd;
  char quota[80];

    headerlen = strlen (extra_headers);
    msgsize += headerlen;
    
    /* Format the email file name */
    /* we get the whole hostname, even though we only use 32 chars of it */
    gethostname(hostname, sizeof(hostname));
    pid = (long unsigned) getpid();
    tm = (long unsigned) time((time_t *) NULL);
    snprintf(local_file_tmp, sizeof(local_file_tmp), "%stmp/%lu.%lu.%.32s,S=%lu",
        maildir, tm, pid, hostname, (long unsigned) msgsize);
    snprintf(local_file_new, sizeof(local_file_new), "%snew/%lu.%lu.%.32s,S=%lu",
        maildir, tm, pid, hostname, (long unsigned) msgsize);

    read_quota_from_maildir (maildir, quota, sizeof(quota));

    /* open the new email file */
    if ((write_fd=open(local_file_tmp, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR)) == -1) {
        if (errno == EDQUOT) return -1;
        printf("can not open new email file errno=%d file=%s\n", 
            errno, local_file_tmp);
        return(-2);
    }

    local = 1;
    if (fdcopy(write_fd, read_fd, extra_headers, headerlen, maildir_to_email(maildir)) != 0) {
        /* Did the write fail because we were over quota? */
        if ( errno == EDQUOT ) {
            close(write_fd);
            unlink (local_file_tmp);
            return -1;
        } else {
            printf ("write failed errno = %d\n", errno);
            close(write_fd);
            unlink (local_file_tmp);
            return -2;
        }
    }

    /* completed write to tmp directory, now move it into the new directory */

    /* sync the data to disk and close the file */
    errno = 0;
    if ( 
#ifdef FILE_SYNC
#ifdef HAVE_FDATASYNC
        fdatasync (write_fd) == 0 &&
#else
        fsync (write_fd) == 0 &&
#endif
#endif
        close (write_fd) == 0 ) {

	if(DeleteMail == 1) {
	    if (unlink(local_file_tmp) != 0) {
                printf("unlink failed %s errno = %d\n", local_file_tmp, errno);
                return(errno);
            }
            return(0);
	}

        /* if this succeeds link the file to the new directory */
        if ( link( local_file_tmp, local_file_new ) == 0 ) {
            /* file was successfully delivered, remove temp file */
            if ( unlink(local_file_tmp) != 0 ) {
                /* not a critical error */
                printf("unlink failed %s errno = %d\n", local_file_tmp, errno);
            }
            return 0;
        } else if (rename(local_file_tmp, local_file_new) == 0) {
            /* file was successfully delivered */
            return 0;
        } else {
            /* even rename failed, time to give up */
            printf("rename failed %s %s errno = %d\n", 
                local_file_tmp, local_file_new, errno);
            unlink(local_file_tmp);  /* remove temp file */
            return -2;
        }
    }

    /* return failure (sync/close failed, message NOT delivered) */
    return -2;
}

/* 
 * Deliver an email to an address
 * calls vexit on failure
 */
void deliver_mail(char *address, char *quota)
{
 pid_t inject_pid = 0;
 int child, qcheck = 0, ret = 0;
 unsigned int xcode;
 FILE *fs;
 char tmp_file[256];
 char maildirquota[80];
 char *email;
#ifdef MAILDROP
 struct vlimits limits;
#endif

   qcheck = -1;

    /* This is a comment, ignore it */
    if ( *address == '#' ) return;

    /* check if the email is looping to this user */
    if ( is_looping( address ) == 1 ) {
        vexiterr (EXIT_BOUNCE, "mail is looping");
    }
    
#ifdef MAKE_SEEKABLE
    /* at this point we know the message size - if its 0 drop out silently */
    if (message_size==0)
        return;
#endif

    /* rewind the message */
    lseek(0,0L,SEEK_SET);

    /* This is an command */
    if ( *address == '|' ) { 

        /* run the command */ 
        run_command(address);
        return;
      }

    /* Starts with '.' or '/', then it's an mbox or maildir delivery */
    else if ((*address == '.') || (*address == '/')) {
        /* check for mbox delivery and exit accordingly */
        if (address[strlen(address)-1] != '/') {
            printf ("can't handle mbox delivery for %s", address);
            vexit(EXIT_DEFER);
        }

        if ((quota == NULL) || (*quota == '\0')) {
          read_quota_from_maildir (address, maildirquota, sizeof(maildirquota));
          quota = maildirquota;
        }

		email = maildir_to_email(address);
        
        /* if the user has a quota set */
#ifdef MAILDROP
       vget_limits(TheDomain, &limits);
       if ( vpw==NULL ) {
         parse_email(email, TheUser, TheDomain, AUTH_SIZE);
         vpw=vauth_getpw(TheUser, TheDomain);
       }
       if ( vpw!=NULL && (limits.disable_spamassassin==1 ||
           (vpw->pw_gid & NO_MAILDROP)) ) {
#endif
        if ( strncmp(quota, "NOQUOTA", 2) != 0 ) {

            /* If the user is over their quota, return it back
             * to the sender.
             */

		    email = maildir_to_email(address);
			qcheck = quota_check(email);
			if (qcheck == 1) {
			   //if (user_over_maildirquota(address,format_maildirquota(quota))==1) {

                /* check for over quota message in domain */
                snprintf(tmp_file, sizeof(tmp_file), "%s/.over-quota.msg",TheDomainDir);
                if ( (fs=fopen(tmp_file, "r")) == NULL ) {
                    /* if no domain over quota then check in vpopmail dir */
                    snprintf(tmp_file, sizeof(tmp_file), "%s/.over-quota.msg",VPOPMAIL_DIR_DOMAINS);
                    fs=fopen(tmp_file, "r");
                }

                if ( fs == NULL ) {
                    printf("user is over quota\n");
                } else {
                    while( fgets( tmp_file, sizeof(tmp_file), fs ) != NULL ) {
                        fputs(tmp_file, stdout);
                    }
                    fclose(fs);
                }
                deliver_quota_warning(address);
                vexiterr (EXIT_OVERQUOTA, "");
            }
            if (QUOTA_WARN_PERCENT >= 0 &&
			     quota_usage(email, quota)
                    >= QUOTA_WARN_PERCENT) {
                deliver_quota_warning(address);
            }
        }
#ifdef MAILDROP
      }
#endif

#ifdef DOMAIN_QUOTAS
    /* bk: check domain quota */
		if (qcheck == -1) {
		   ret = quota_check_domain(TheDomain);
		   if (ret)
			  qcheck = 2;
	    }

//        if (domain_over_maildirquota(address)==1)
	    if (qcheck == 2)
        {
            /* check for over quota message in domain */
            snprintf(tmp_file, sizeof(tmp_file), "%s/.over-quota.msg",TheDomainDir);
            if ( (fs=fopen(tmp_file, "r")) == NULL ) {
                /* if no domain over quota then check in vpopmail dir */
                snprintf(tmp_file, sizeof(tmp_file), "%s/.over-quota.msg",VPOPMAIL_DIR_DOMAINS);
                fs=fopen(tmp_file, "r");
            }

            if ( fs == NULL ) {
                printf("domain is over quota\n");
            } else {
                while( fgets( tmp_file, sizeof(tmp_file), fs ) != NULL ) {
                    fputs(tmp_file, stdout);
                }
                fclose(fs);
            }
            deliver_quota_warning(address);
            vexiterr (EXIT_OVERQUOTA, "");
        }
#endif

        /* Get the email address from the maildir */
		// done above

        email = maildir_to_email(address);

        /* Set the Delivered-To: header */
        if ( strcmp( address, bounce) == 0 ||
             strcmp( email, "") == 0 ) {
            snprintf(DeliveredTo, sizeof(DeliveredTo), 
                "%sDelivered-To: %s@%s\n", getenv("RPLINE"), 
                 TheUser, TheDomain);
        } else {
            snprintf(DeliveredTo, sizeof(DeliveredTo), 
                "%sDelivered-To: %s\n", getenv("RPLINE"), 
                email);
        }
    
        switch (deliver_to_maildir (address, DeliveredTo, 0, message_size)) {
            case -1:
                vexiterr (EXIT_OVERQUOTA, "user is over quota");
                break;
            case -2:
                vexiterr (EXIT_DEFER, "system error");
                break;
            case -3:
                vexiterr (EXIT_BOUNCE, "mail is looping");
                break;
            default:
                return;
        }

    /* must be an email address */
    } else {
      char *dtline;
      char *atpos;
      int dtlen;

      if (*address=='&') ++address;  /* will this case ever happen? */
      inject_pid = qmail_inject_open(address);
      if (inject_pid == 0) vexiterr (EXIT_DEFER, "system error, can't open qmail-inject");
      
      /* use the DTLINE variable, but skip past the dash in 
       * domain-user@domain 
       */

      if ( (dtline = getenv("DTLINE")) != NULL ) {
        dtlen = strlen(dtline);
        /* make sure dtline is at least as long as "Delivered-To: " */
        if (dtlen < 15) {
          dtline = NULL;
        } else {
          dtline += 14;  /* skip "Delivered-To: " */
          dtlen -= 14;
          atpos = strchr (dtline, '@');
          /* ex: dtline = "x-y-z.com-fred@x-y-z.com\n"
           * dtlen = 25, atpos = dtline+14
           * add 25 - 14 - 1 = 10 bytes to dtline,
           * now points to "fred@x-y-z.com\n".
           */
          if (atpos != NULL) {
            dtline += (dtlen - (atpos - dtline) - 1);
          }
        }
      }
      if (dtline == NULL) {
        snprintf(DeliveredTo, sizeof(DeliveredTo), 
          "%sDelivered-To: %s\n", getenv("RPLINE"), address);
      } else {
        snprintf(DeliveredTo, sizeof(DeliveredTo), 
          "%sDelivered-To: %s", getenv("RPLINE"), dtline);
      }
      
      local = 0;
      if (fdcopy (fdm, 0, DeliveredTo, strlen(DeliveredTo), address) != 0) {
          printf ("write to qmail-inject failed: %d\n", errno);
          close(fdm);
          waitpid(inject_pid,&child,0);
          vexiterr (EXIT_DEFER, "system error calling qmail-inject");
      }

      close(fdm);
      if (waitpid(inject_pid,&child,0) <= 0 || !WIFEXITED(child)) {
	      xcode = EXIT_DEFER;
      } else {
	      xcode = WEXITSTATUS(child);
	      if (xcode == 0) return;
      }
      vexiterr (xcode, "system error calling qmail-inject");
    }
}


/* Check if the vpopmail user has a .qmail file in their directory
 * and foward to each email address, Maildir or program 
 *  that is found there in that file
 *
 * Return: 1 if we found and delivered email
 *       : 0 if not found
 *       : -1 if no user .qmail file 
 *
 */
int check_forward_deliver(char *dir)
{
 static char qmail_line[500];
 FILE *fs;
 int i;
 int return_value = 0;

#ifdef QMAIL_EXT
 char tmpbuf[500];
#endif
   
    chdir(dir);

#ifdef QMAIL_EXT
	fs = NULL;

    /* format the file name */
    if (strlen(TheExt)) {
        strcpy(tmpbuf,".qmail-");
        strcat(tmpbuf,TheExt);
        if ( (fs = fopen(tmpbuf,"r")) == NULL ) {
            for (i=strlen(TheExt);i>=0;--i) {
                if (!i || TheExt[i-1]=='-') {
                    strcpy(tmpbuf,".qmail-");
                    strncat(tmpbuf,TheExt,i);
                    strcat(tmpbuf,"default");
                    if ( (fs = fopen(tmpbuf,"r")) != NULL) {
                        break;
                    }
                }
            }
        }
    } 

	if (fs == NULL)
       fs = fopen(".qmail","r");
#else
    fs = fopen(".qmail","r");
#endif

    /* no .qmail file at all */
    if (fs == NULL ) {
        /* no file, so just return */
        return(-1);
    }

    /* read the file, line by line */
    while ( fgets(qmail_line, sizeof(qmail_line), fs ) != NULL ) {
        if (*qmail_line == '#') {
            return_value = 1;
            continue;
        }

        /* remove the trailing new line */
        for(i=0;qmail_line[i]!=0;++i) {
            if (qmail_line[i] == '\n') qmail_line[i] = 0;
        }

        deliver_mail(qmail_line, "");

        return_value = 1;
    }

    /* close the file */
    fclose(fs);

    /* return if we found one or not */
    return(return_value);
}

void sig_catch(sig,f)
int sig;
void (*f)();
{
#ifdef HAVE_SIGACTION
  struct sigaction sa;
  sa.sa_handler = f;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(sig,&sa,(struct sigaction *) 0);
#else
  signal(sig,f); /* won't work under System V, even nowadays---dorks */
#endif
}


/* open a pipe to a command 
 * return the pid or -1 if error
 */
void run_command(char *prog)
{

#define MAX_ENV_BUFF 100

 int child;
 char *(args[4]);
 int wstat;

 while ((*prog == ' ') || (*prog == '|')) ++prog;

 switch(child = fork())
  {
   case -1:
     printf("Unable to fork: %d.", errno); 
     vexit(EXIT_DEFER);
   case 0:
     
     putenv("SHELL=/bin/sh");
     args[0] = "/bin/sh"; args[1] = "-c"; args[2] = prog; args[3] = 0;
     sig_catch(SIGPIPE,SIG_DFL);
     execv(*args,args);
     printf("Unable to run /bin/sh: %d.", errno);
     exit(EXIT_DEFER);    /* the child's exit code will get caught below */
  }

  if (waitpid(child,&wstat,0) < 0 || !WIFEXITED(wstat))
    vexit(EXIT_DEFER);
  switch(WEXITSTATUS(wstat))
   {
    case 64: case 65: case 70: case 76: case 77: case 78: case 100: case 112: vexit(EXIT_BOUNCE);
    case 99: vexit(99);	/* not sure about this, when does it exit 99? */
    case 0: break;
    default: vexit(EXIT_DEFER);
   }

}

/* Check for a looping message
 * This is done by checking for a matching line
 * in the email headers for Delivered-To: which
 * we put in each email
 * 
 * Return 1 if looping
 * Return 0 if not looping
 * Return -1 on error 
 */
int is_looping( char *address ) 
{
 char *dtline;

    /* if we don't know the message size then get it */
    if ( message_size == 0 ) {
        /* read the message to get the size */
        message_size = get_message_size();
    }
    if (*address=='&') ++address;

    /* check the DTLINE */
    dtline = getenv("DTLINE");
    if ( dtline != NULL && is_loop_match(dtline, address) == 1 ) {
      return(1);
    }

#ifdef MAKE_SEEKABLE
    if (!Seekable(0))
        MakeSeekable(stdin);
#endif

    lseek(0,0L,SEEK_SET);
    while(fgets(loop_buf,sizeof(loop_buf),stdin)!=NULL){

        /* if we find the line, return error (looping) */
        if (is_loop_match(loop_buf, address)==1 ) {
            /* seek to the end of stdin */
            fseek(stdin, 0L, SEEK_END);
            /* return the loop found */
            return(1);
        } else if (*loop_buf == '\r' || *loop_buf == '\n') {
            /* seek to the end of stdin */
            fseek(stdin, 0L, SEEK_END);
            /* end of headers return not found looping message value */
            return(0);
        }
    }

    /* if we get here then the there is either no body 
     * or the logic above failed and we scanned
     * the whole email, headers and body. 
     */
    return(0);
}

/* 
 * Get the size of the email message 
 * return the size 
 */
ssize_t get_message_size()
{
 ssize_t message_size;
 ssize_t bytes;

#ifdef MAKE_SEEKABLE
    if (!Seekable(0))
        MakeSeekable(stdin);
#endif

    if ( lseek(0, 0L,SEEK_SET) < 0 ) {
        printf("lseek error %d\n", errno);
        return(-1);
    }

    /* couldn't we lseek to SEEK_END and use return value of lseek? */
    
    message_size = 0;
    while((bytes=read(0,msgbuf,sizeof(msgbuf)))>0) {
        message_size += bytes;
    }
    
    /* rewind file */
    lseek (0, 0L, SEEK_SET);
    
    return(message_size);
}

/*
 * check for locked account
 * deliver to .qmail file if any
 * deliver to user if no .qmail file
 */
void checkuser() 
{
 struct stat mystatbuf;

    if (vpw->pw_gid & BOUNCE_MAIL ) {
        printf("vdelivermail: account is locked email bounced %s@%s\n",
            TheUser, TheDomain);
        vexit(EXIT_BOUNCE);
    }

    /* If their directory path is empty make them a new one */
    if ( vpw->pw_dir == NULL || vpw->pw_dir[0]==0 ) {
        if ( make_user_dir(vpw->pw_name, TheDomain, 
               TheDomainUid, TheDomainGid)==NULL){
            vexiterr (EXIT_BOUNCE, "Auto creation of maildir failed. (#5.9.8)");
        }
        /* Re-read the vpw entry, because we need to lookup the newly created
         * pw_dir entry
         */
        if ((vpw=vauth_getpw(TheUser, TheDomain)) == NULL ) {
           vexiterr (EXIT_BOUNCE, "Failed to vauth_getpw(). (#5.9.8.1)");
        }
    }

    /* check if the directory exists and create if needed */
    if ( stat(vpw->pw_dir, &mystatbuf ) == -1 ) {
        if ( vmake_maildir(TheDomain, vpw->pw_dir )!= VA_SUCCESS ) {
            vexiterr (EXIT_BOUNCE, "Auto re-creation of maildir failed. (#5.9.9)");
        }
    }

    /* check for a .qmail file in their Maildir
     * If it exists, then deliver to the contents and exit
     */
    if ( check_forward_deliver(vpw->pw_dir) == 1 ) {
        vexit(EXIT_OK);
    }

    snprintf(TheDir, sizeof(TheDir), "%s/Maildir/", vpw->pw_dir);

    deliver_mail(TheDir, vpw->pw_shell);
}


/*
 * the vpopmail user does not exist. Follow the rest of
 * the directions in the .qmail-default file
 */
void usernotfound() 
{
    /* read the full message to avoid SIGPIPE if maildrop is calling us */
    if (message_size == 0) message_size = get_message_size();

    if ( strcmp(bounce, DELETE_ALL) == 0 ) {
      /* If they want to delete email for non existant users
       * then just exit 0. Qmail will delete the email for us
       */

        vexit(EXIT_OK);

    } else if ( strcmp(bounce, BOUNCE_ALL) == 0 ) {
      /* If they want to bounce the email back then
       * print a message and exit 100
       */
      FILE *fs;
      char tmp_file[256];

        snprintf(tmp_file, sizeof(tmp_file), "%s/.no-user.msg",TheDomainDir);
        if ( (fs=fopen(tmp_file, "r")) == NULL ) {
            /* if no domain no user then check in vpopmail dir */
            snprintf(tmp_file, sizeof(tmp_file), "%s/.no-user.msg",VPOPMAIL_DIR_DOMAINS);
            fs=fopen(tmp_file, "r");
        }
        if ( fs == NULL ) {
             printf("Sorry, no mailbox here by that name. (#5.1.1)\n");
        } else {
            while( fgets( tmp_file, sizeof(tmp_file), fs ) != NULL ) {
               fputs(tmp_file, stdout);
            }
            fclose(fs);
        }

        vexit(EXIT_BOUNCE);

    }

    /* check if it is a path add the /Maildir/ for delivery */
    if ( bounce[0] == '/' ) {
        if (bounce[strlen(bounce)-1] != '/') strcat( bounce, "/");
        printf ("user does not exist, but will deliver to %s\n", bounce);
        if( check_forward_deliver(bounce) == 1 )
            vexit(EXIT_OK);
        else
            strcat( bounce, "Maildir/");
    }

    deliver_mail(bounce, "");
}

/* 
 * Deliver an quota warning message
 * Return 0 on success
 * Return less than zero on failure
 * 
 * -1 = user is over quota
 * -2 = system failure
 * -3 = mail is looping 
 */
int deliver_quota_warning(const char *dir)
{
 time_t tm;
 int fd, read_fd;
 int err;
 char newdir[400];
 struct stat     sb;
 char quotawarnmsg[BUFF_SIZE];

    snprintf (quotawarnmsg, sizeof(quotawarnmsg), "%s%s", dir, "/quotawarn");
    time(&tm);

    /* Send only one warning every 24 hours */
    if (stat(quotawarnmsg, &sb) == 0 && ((sb.st_mtime + 86400) > tm)) {
        return 0;
    }

    fd = open(quotawarnmsg, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (!fd) {
        printf ("couldn't update quotawarn timestamp\n");
        return -2;
    }
    /* write zero bytes, but update file's timestamp */
    write(fd, quotawarnmsg, 0);
    close(fd);

    /* Look in the domain for a .quotawarn.msg */
    snprintf(quotawarnmsg, sizeof(quotawarnmsg), "%s/.quotawarn.msg", TheDomainDir);
    if ( ((read_fd = open(quotawarnmsg, O_RDONLY)) < 0) || 
           (stat(quotawarnmsg, &sb) != 0)) {

        /* if that fails look in vpopmail dir */
        snprintf(quotawarnmsg, sizeof(quotawarnmsg), "%s/.quotawarn.msg", VPOPMAIL_DIR_DOMAINS);
        if ( ((read_fd = open(quotawarnmsg, O_RDONLY)) < 0) || 
              (stat(quotawarnmsg, &sb) != 0)) {
            return 0;
        }
    }

    /* Don't include Return-Path in quota warnings (since they're computer
     * generated) but add a Date header.
     */
    if ( strcmp( dir, bounce) == 0 ) {
        snprintf(DeliveredTo, sizeof(DeliveredTo), 
            "%s%s", getenv("DTLINE"), date_header());
    } else {
        strcpy(newdir, dir);
        snprintf(DeliveredTo, sizeof(DeliveredTo), 
            "Delivered-To: %s\n%s", 
            maildir_to_email(newdir), date_header());
    }

    err = deliver_to_maildir (dir, DeliveredTo, read_fd, sb.st_size);

    close (read_fd);
    
    if (err != 0) return err;
    
    /* return success */
    return(0);
}

int is_loop_match( const char *dt, const char *address)
{
    char compare[255];
 
    /* make sure dt is really a delivered to header */
    if (strncasecmp ("Delivered-To: ", dt, 14) != 0) return 0;
    
    snprintf (compare, sizeof(compare), "%s\n", address);

    return (strcasecmp (compare, (dt+14)) == 0);
}

#ifdef SPAMASSASSIN
/* Check for a spam message
 *  * This is done by checking for a matching line
 *   * in the email headers for X-Spam-Level: which
 *    * we put in each spam email
 *     *
 *      * Return 1 if spam
 *       * Return 0 if not spam
 *        * Return -1 on error
 *         */
int is_spam(char *spambuf)
{
 int i,j,k;
 int found;

    for(i=0,j=0;spambuf[i]!=0;++i) {

       /* found a line */
       if (spambuf[i]=='\n' || spambuf[i]=='\r' ) {

         /* check for blank line, end of headers */
         for(k=j,found=0;k<i;++k) {
           switch(spambuf[k]) {
             /* skip blank spaces and new lines */
             case ' ':
             case '\n':
             case '\t':
             case '\r':
               break;

             /* found a non blank, so we are still
              * in the headers
              */
             default:
               /* set the found non blank char flag */
               found = 1;
               break;
           }
         }
         if ( found == 0 ) {
           InHeaders=0;
           return(0);
         }

         /* still in the headers check for spam header */
         if ( strncmp(&spambuf[j], "X-Spam-Flag: YES", 16 ) == 0 ) return(1);

         if (spambuf[i+1]!=0) j=i+1;
       }
     }
     return(0);
}
#endif
