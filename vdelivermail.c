/*
 * vdelivermail
 * part of the vpopmail package
 * 
 * Copyright (C) 1999,2001 Inter7 Internet Technologies, Inc.
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
off_t message_size = 0;
char bounce[AUTH_SIZE];
int CurrentQuotaSizeFd;

#ifdef QMAIL_EXT
/* the User with '-' and following chars out if any */
char TheUserExt[AUTH_SIZE]; 
#endif

#define FILE_SIZE 156
char hostname[FILE_SIZE];
char loop_buf[FILE_SIZE];

#define MSG_BUF_SIZE 5000
char msgbuf[MSG_BUF_SIZE];

#define BUFF_SIZE 300
int fdm;
static char *binqqargs[4];
char *maildir_to_email(char *maildir);

#define QUOTA_WARN_PERCENT 90

/* from qmail's wait.h for run_command() */
#define wait_exitcode(w) ((w) >> 8)

/* Forward declarations */
int process_valias(void);
int is_delete(char *deliverto);
int is_bounce(char *deliverto);
void get_arguments(int argc, char **argv);
off_t get_message_size();
int deliver_mail(char *address, char *quota);
int check_forward_deliver(char *dir);
off_t count_dir(char *dir_name);
int is_looping( char *address );
void run_command(char *prog);
void checkuser(void);
void usernotfound(void);
int is_loop_match( char *dt, char *address);

/* functions in maildirquota.c */
int deliver_quota_warning(const char *dir, const char *q);
int user_over_maildirquota(char *address, char *quota);
void add_warningsize_to_quota( const char *dir, const char *q);
char *format_maildirquota(const char *q);


static char local_file[156];
static char local_file_new[156];

/* 
 * The email message comes in on file descriptor 0 - stanard in
 * The user to deliver the email to is in the EXT environment variable
 * The domain to deliver the email to is in the HOST environment variable
 */
int main(int argc, char **argv)
{
    /* get the arguments to the program and setup things */
    get_arguments(argc, argv);
 
#ifdef VALIAS
    /* process valiases if configured */
    if ( process_valias() == 1 ) {
        printf("vdelivermail: valiases processed\n");
        vexit(0);

    /* if the database is down, deferr */
    } else if ( verrori == VA_NO_AUTH_CONNECTION ) {
        vexit(111);
    }

#endif

    /* get the user from vpopmail database */
    if ((vpw=vauth_getpw(TheUser, TheDomain)) != NULL ) {
        checkuser();
    } 

#ifdef QMAIL_EXT
    /* try and find user that matches the QmailEXT address if: no user found,
     * and the QmailEXT address is different, meaning there was an extension 
     */
    else if ( strncmp(TheUser, TheUserExt, AUTH_SIZE) != 0 ) {
        /* get the user from vpopmail database */
        if ((vpw=vauth_getpw(TheUserExt, TheDomain)) != NULL ) {
            checkuser();
        } else {
            usernotfound();
        }
    }
#endif
    else {
        if ( verrori != 0 ) {
            vexit(111);
        }
        usernotfound();
    } 

    /* exit successfully and have qmail delete the email */
    return(vexit(0));
            
}

/* 
 * Get the command line arguments and the environment variables.
 * Force addresses to be lower case and set the default domain
 */
void get_arguments(int argc, char **argv)
{
 char *tmpstr; 
 int i;

    if (argc != 3) {
        printf("vdelivermail: invalid syntax in .qmail-default file\n");

        /* exit with temporary error */
        vexit(111);
    }

    /* get the last parameter in the .qmail-default file */
    strncpy(bounce, argv[2], sizeof(bounce)); 

    if ((tmpstr=getenv("EXT")) == NULL ) {
        printf("vdelivermail: no EXT environment varilable\n");

        /* exit and bounce the email back */
        vexit(100);
    } else {
        for(i=0;*tmpstr!=0&&i<AUTH_SIZE;++i,++tmpstr) {
            TheUser[i] = *tmpstr;
        }
        TheUser[i] = 0;
    }

    if ((tmpstr=getenv("HOST")) == NULL ) {
        printf("vdelivermail: no HOST environment varilable\n");

        /* exit and bounce the email back */
        vexit(100);
    } else {
        for(i=0;*tmpstr!=0&&i<AUTH_SIZE;++i,++tmpstr) {
            TheDomain[i] = *tmpstr;
        }
        TheDomain[i] = 0;
    }

    lowerit(TheUser);
    lowerit(TheDomain);

    if ( is_username_valid(TheUser) != 0 ) vexit(100);
    if ( is_domain_valid(TheDomain) != 0 ) vexit(100);
    strncpy(TheUserFull, TheUser, AUTH_SIZE);

#ifdef QMAIL_EXT 
    /* delete the '-' and following chars if any and store in TheUserExt */
    for(i = 0; TheUser[i] != 0; i++) {
        if (TheUser[i] == '-' ) {
            break;
        }
        TheUserExt[i] = TheUser[i];
    }

    TheUserExt[i] = 0;
    if ( is_username_valid(TheUserExt) != 0 ) {
        vexit(100);
    }

#endif

    vget_real_domain(TheDomain,AUTH_SIZE);
    vget_assign(TheDomain,TheDomainDir,156,&TheDomainUid,&TheDomainGid);

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

    /* Get the first alias for this user@domain */
    tmpstr = valias_select( TheUser, TheDomain );

    /* tmpstr will be NULL if there are no more aliases */
    while (tmpstr != NULL ) {

        /* We found one */
        found = 1;

        /* deliver the mail */
        deliver_mail(tmpstr, "NOQUOTA");

        /* Get the next alias for this user@domain */
        tmpstr = valias_select_next();
    }

#ifdef QMAIL_EXT 
    /* try and find alias that matches the QmailEXT address 
     * if: no alias found, 
     * and the QmailEXT address is different, meaning there was an extension 
     */
    if ( (!found) && ( strncmp(TheUser, TheUserExt, AUTH_SIZE) != 0 )  ) {
        /* Get the first alias for this user@domain */
        tmpstr = valias_select( TheUserExt, TheDomain );

        /* tmpstr will be NULL if there are no more aliases */
        while (tmpstr != NULL ) {

            /* We found one */
            found = 1;

            /* deliver the mail */
            deliver_mail(tmpstr, "NOQUOTA");

            /* Get the next alias for this user@domain */
            tmpstr = valias_select_next();
        } 
    }    
#endif

    /* Return whether we found an alias or not */
    return(found);
}
#endif

/* If the .qmail-default file has bounce all in it
 * Then return 1
 * otherwise return 0
 */
int is_bounce(char *deliverto)
{
    if ( strcmp( deliverto, BOUNCE_ALL ) == 0 ) {
        return(1);
    } else {
        return(0);
    }
}

/* If the .qmail-default file has delete all in it
 * Then return 1
 * otherwise return 0
 */
int is_delete(char *deliverto)
{
    if ( strcmp( deliverto, DELETE_ALL ) == 0 ) {
        return(1);
    } else {
        return(0);
    }
}



/* cound the directory */
off_t count_dir(char *dir_name)
{
 DIR *mydir;
 struct dirent *mydirent;
 struct stat statbuf;
 off_t file_size = 0;
 char *tmpstr;

    if ( dir_name == NULL ) return(0);
    if (chdir(dir_name) == -1) return(0);
    if ( (mydir = opendir(".")) == NULL )  return(0);

    while( (mydirent=readdir(mydir)) != NULL ) {
        if ( strcmp( mydirent->d_name, "..") == 0 ) continue;
        if ( strcmp( mydirent->d_name, ".") == 0 ) continue;

        if ( (tmpstr=strstr(mydirent->d_name, ",S="))!=NULL) {
            tmpstr += 3;
            file_size += atoi(tmpstr);
        } else if (stat(mydirent->d_name,&statbuf)==0 && 
                   (statbuf.st_mode & S_IFDIR) ) {
            file_size +=  count_dir(mydirent->d_name);
        }
    }
    closedir(mydir);
    if ( dir_name != NULL && strcmp(dir_name, ".." )!=0 && 
                             strcmp(dir_name, "." )!=0) {
        chdir("..");
    }
    return(file_size);
}

long unsigned qmail_inject_open(char *address)
{
 int pim[2];
 long unsigned pid;
 int i=0;
 static char *in_address;

    in_address = malloc(strlen(address)+1);  
    strcpy(in_address, address);

    /* skip over an & sign if there */
    if (in_address[i] == '&') ++i;

    if ( pipe(pim) == -1) return(-1);

    switch(pid=vfork()){
      case -1:
        close(pim[0]);
        close(pim[1]);
        return(-1);
      case 0:
        close(pim[1]);
        if (vfd_move(0,pim[0]) == -1 ) _exit(-1);
        binqqargs[0] = QMAILINJECT;
        binqqargs[1] = &in_address[i];
        execv(*binqqargs, binqqargs);
    }
    fdm = pim[1];
    close(pim[0]);
    free(in_address);
    return(pid);
}



/* 
 * Deliver an email to an address
 * Return 0 on success
 * Return less than zero on failure
 * 
 * -1 = user is over quota
 * -2 and below are system failures
 * -3 mail is looping 
 */
int deliver_mail(char *address, char *quota)
{
 time_t tm;
 off_t file_count;
 long unsigned pid;
 int write_fd;
 int inject = 0;
 FILE *fs;
 char tmp_file[256];

    /* check if the email is looping to this user */
    if ( is_looping( address ) == 1 ) {
        printf("message is looping %s\n", address );
        return(-3);
    }

    /* Contains /Maildir/ ? Then it must be a full or relative
     * path to a Maildir 
     */ 
    if ( strstr(address, "/Maildir/") != NULL ) {

        /* if the user has a quota set */
        if ( strncmp(quota, "NOQUOTA", 2) != 0 ) {

            /* If the user is over thier quota, return it back
             * to the sender.
             */
            if (user_over_maildirquota(address,format_maildirquota(quota))==1) {

                /* check for over quota message in domain */
                sprintf(tmp_file, "%s/.over-quota.msg",TheDomainDir);
                if ( (fs=fopen(tmp_file, "r")) == NULL ) {
                    /* if no domain over quota then check in vpopmail dir */
                    sprintf(tmp_file, "%s/domains/.over-quota.msg",VPOPMAILDIR);
                    fs=fopen(tmp_file, "r");
                }

                if ( fs == NULL ) {
                    printf("user is over quota\n");
                } else {
                    while( fgets( tmp_file, 256, fs ) != NULL ) {
                        fputs(tmp_file, stdout);
                    }
                    fclose(fs);
                }
                deliver_quota_warning(address, format_maildirquota(quota));
                return(-1);
            }
            if (QUOTA_WARN_PERCENT >= 0 &&
                maildir_readquota(address, format_maildirquota(quota))
                    >= QUOTA_WARN_PERCENT) {
                deliver_quota_warning(address, format_maildirquota(quota));
            }
        }

        /* Format the email file name */
        gethostname(hostname,sizeof(hostname));
        pid=getpid();
        time (&tm);
        snprintf(local_file, 156, "%stmp/%lu.%lu.%s,S=%lu",
            address,(long unsigned)tm,(long unsigned)pid,
            hostname, (long unsigned)message_size);
        snprintf(local_file_new, 156, "%snew/%lu.%lu.%s,S=%lu",
            address,(long unsigned)tm,(long unsigned)pid,hostname, 
        (long unsigned)message_size);

        /* open the new email file */
        if ((write_fd=open(local_file,O_CREAT|O_RDWR,S_IRUSR|S_IWUSR))== -1) {
            printf("can not open new email file errno=%d file=%s\n", 
                errno, local_file);
            return(-2);
        }
 
        /* Set the Delivered-To: header */
        if ( strcmp( address, bounce) == 0 ) {
            snprintf(DeliveredTo, AUTH_SIZE, 
                "%sDelivered-To: %s@%s\n", getenv("RPLINE"), 
                 TheUser, TheDomain);
        } else {
        
            snprintf(DeliveredTo, AUTH_SIZE, 
                "%sDelivered-To: %s\n", getenv("RPLINE"), 
                maildir_to_email(address));
        }

    /* This is an command */
    } else if ( *address == '|' ) { 

        /* run the command */ 
        run_command(address);
        return(0);

    /* must be an email address */
    } else {
      char *dtline;
      char *tstr;

        qmail_inject_open(address);
        write_fd = fdm;
        inject = 1;

        /* use the DTLINE variable, but skip past the dash in 
         * domain-user@domain 
         */

        if ( (dtline = getenv("DTLINE")) != NULL ) {
            while (*dtline!=0 && *dtline!=':' ) ++dtline;
            while (*dtline!=0 && *dtline!='-' ) ++dtline;
            if ( *dtline != 0 ) ++dtline;
                for(tstr=dtline;*tstr!=0;++tstr) if (*tstr=='\n') *tstr=0;
            } else {
                if (*address=='&') ++address;
                dtline = address;
            }
            snprintf(DeliveredTo, AUTH_SIZE, 
                "%sDelivered-To: %s\n", getenv("RPLINE"), dtline);
    }

    if ( lseek(0, 0L, SEEK_SET) < 0 ) {
        printf("lseek errno=%d\n", errno);
        return(errno);
    }

    /* write the Return-Path: and Delivered-To: headers */
    if (write(write_fd,DeliveredTo,strlen(DeliveredTo))!= 
              strlen(DeliveredTo)) {

        close(write_fd);
        /* Check if the user is over quota */
        if ( errno == EDQUOT ) {
            return(-1);
        } else {
            printf("failed to write delivered to line errno=%d\n",errno);
           return(errno);
        }
    }


    /* read it in chunks and write it to the new file */
    while((file_count=read(0,msgbuf,MSG_BUF_SIZE))>0) {
        if ( write(write_fd,msgbuf,file_count) == -1 ) {
            close(write_fd);

            /* if the write fails and we are writing to a Maildir
             * then unlink the file
             */
            if ( unlink(local_file) != 0 ) {
                printf("unlink failed %s errno = %d\n", local_file, errno);
                return(errno);
            }

            /* Check if the user is over quota */
            if ( errno == EDQUOT ) {
                return(-1);
            } else {
                printf("write failed errno = %d\n", errno);
                return(errno);
            }
        }
    }
    if ( inject == 1 ) {
        close(write_fd);
        return(0);
    }

    /* if we are writing to a Maildir, move it
     * into the new directory
     */

    /* sync the data to disk and close the file */
    errno = 0;
    if ( 
#ifdef FILE_SYNC
#ifdef HAVE_FDATASYNC
        fdatasync(write_fd) == 0 &&
#else
        fsync(write_fd) == 0 &&
#endif
#endif
        close(write_fd) == 0 ) {

        /* if this succeeds link the file to the new directory */
        if ( link( local_file, local_file_new ) == 0 ) {
            if ( unlink(local_file) != 0 ) {
                printf("unlink failed %s errno = %d\n", local_file, errno);
            }
        } else {

            /* coda fs has problems with link, check for that error */
            if ( errno==EXDEV ) {

                /* try to rename the file instead */
                if (rename(local_file, local_file_new)!=0) {

                    /* even rename failed, time to give up */
                    printf("rename failed %s %s errno = %d\n", 
                        local_file, local_file_new, errno);
                        return(errno);

                /* rename worked, so we are okay now */
                } else {
                    errno = 0;
                }

            /* link failed and we are not on coda */
            } else {
                printf("link REALLY failed %s %s errno = %d\n", 
                    local_file, local_file_new, errno);
                unlink(local_file);
            }
        }
    }

    /* return success */
    return(0);
}

/* Check if the vpopmail user has a .qmail file in thier directory
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
 char tmpbuf[500];
 FILE *fs;
 int i;
 int return_value = 0;

   
    chdir(dir);

    /* format the file name */
    if ( (fs = fopen(".qmail","r")) == NULL ) {

        /* no file, so just return */
        return(-1);
    }

    /* format a simple loop checker name */
    snprintf(tmpbuf, 500, "%s@%s", TheUser, TheDomain);

    /* read the file, line by line */
    while ( fgets(qmail_line, 500, fs ) != NULL ) {

        /* remove the trailing new line */
        for(i=0;qmail_line[i]!=0;++i) {
            if (qmail_line[i] == '\n') qmail_line[i] = 0;
        }

        /* simple loop check, if they are sending it to themselves
         * then skip this line
         */
        if ( strcmp( qmail_line, tmpbuf) == 0 ) continue;

        deliver_mail(qmail_line, "NOQUOTA");
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
 int child;
 char *(args[4]);
 int wstat;

 while (*prog==' ') ++prog;
 while (*prog=='|') ++prog;

    if ( lseek(0, 0L, SEEK_SET) < 0 ) {
        printf("lseek errno=%d\n", errno);
        return;
    }

 switch(child = fork())
  {
   case -1:
     printf("unable to fork\n"); 
     exit(0);
   case 0:
     args[0] = "/bin/sh"; args[1] = "-c"; args[2] = prog; args[3] = 0;
     sig_catch(SIGPIPE,SIG_DFL);
     execv(*args,args);
     printf("Unable to run /bin/sh: ");
     exit(-1);
  }

  wait(&wstat);
  waitpid(wstat,&child,0);
  switch(wait_exitcode(wstat))
   {
    case 64: case 65: case 70: case 76: case 77: case 78: case 112: _exit(100);
    case 0: break;
    default: _exit(111);
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
 int i;
 int found;
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

    lseek(0,0L,SEEK_SET);
    while(fgets(loop_buf,sizeof(loop_buf),stdin)!=NULL){

        /* if we find the line, return error (looping) */
        if (strstr(loop_buf, "Delivered-To")!= 0 && 
            is_loop_match(loop_buf, address)==1 ) {

            /* return the loop found */
            return(1);

            /* check for the start of the body, we only need
            * to check the headers. 
            */
        } else {

            /* walk through the charaters in the body */
            for(i=0,found=0;loop_buf[i]!=0&&found==0;++i){
                switch(loop_buf[i]){

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

            /* if the line only had blanks, then it is the
             * delimiting line between the headers and the
             * body. We don't need to check the body for
             * the duplicate Delivered-To: line. Hence, we
             * are done with our search and can return the
             * looping not found value
            */
            if ( found == 0 ) {
                /* return not found looping message value */
                return(0);
            }
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
off_t get_message_size()
{
 ssize_t message_size;
 ssize_t bytes;

    if ( lseek(0, 0L,SEEK_SET) < 0 ) {
        printf("lseek error %d\n", errno);
        return(-1);
    }

    message_size = 0;
    while((bytes=read(0,msgbuf,MSG_BUF_SIZE))>0) {
        message_size += bytes;
    }
    return(message_size);
}

char *maildir_to_email(char *maildir)
{
 static char email[256];
 int i, j=0;
 char *pnt, *last;

    memset(email, 0, sizeof(email));
    for(last=NULL, pnt=maildir; (pnt=strstr(pnt,"/Maildir/"))!=NULL; pnt+=9 ){
        last = pnt;
    }
    if(!last) return "";

    /* so now pnt at begin of last Maildir occurence
     * going toward start of maildir we can get username
     */
    pnt = last;

    for( i=(pnt-maildir); (i > 1 && *(pnt-1) != '/'); --pnt, --i);

    for( ; (*pnt && *pnt != '/' && j < 255); ++pnt) {
        email[j++] = *pnt;
    }

    email[j++] = '@';

    for (last=NULL, pnt=maildir; (pnt=strstr(pnt, "/" DOMAINS_DIR "/")); pnt+=strlen("/" DOMAINS_DIR "/")) {
        last = pnt;
    }

    if(!last) return "";

    for( pnt = last + 9; (*pnt && *pnt != '/' && j < 255); ++pnt, ++j ) {
      email[j] = *pnt;
    }

    email[j] = 0;

    return( email );
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
        vexit(100);
    }

    /* If thier directory path is empty make them a new one */
    if ( vpw->pw_dir == NULL || vpw->pw_dir[0]==0 ) {
        if ( make_user_dir(vpw->pw_name, TheDomain, 
               TheDomainUid, TheDomainGid)==NULL){
            printf("Auto creation of maildir failed. vpopmail (#5.9.8)\n");
            vexit(100);
        }
    }

    /* check if the directory exists and create if needed */
    if ( stat(vpw->pw_dir, &mystatbuf ) == -1 ) {
        if ( vmake_maildir(TheDomain, vpw->pw_dir )!= VA_SUCCESS ) {
            printf("Auto re-creation of maildir failed. vpopmail (#5.9.9)\n");
            vexit(100);
        }
    }

    /* check for a .qmail file in thier Maildir
     * If it exists, then deliver to the contents and exit
     */
    if ( check_forward_deliver(vpw->pw_dir) == 1 ) {
        vexit(0);
    }

    snprintf(TheDir, AUTH_SIZE, "%s/Maildir/", vpw->pw_dir);
    if ( deliver_mail(TheDir, vpw->pw_shell) != 0 ) {
        vexit(100);
    }
}


/*
 * the vpopmail user does not exist. Follow the rest of
 * the directions in the .qmail-default file
 */
void usernotfound() 
{
 int ret;
 static char tmpuser[AUTH_SIZE];
 static char tmpdomain[AUTH_SIZE];
 static uid_t uid;
 static gid_t gid;

    /* If they want to delete email for non existant users
     * then just exit 0. Qmail will delete the email for us
     */
    if ( strcmp(bounce, DELETE_ALL) == 0 ) {
        /* just exit 0 and qmail will delete the email from the system */
        vexit(0);

    /* If they want to bounce the email back then
     * print a message and exit 100
     */
    } else if ( strcmp(bounce, BOUNCE_ALL) == 0 ) {
        printf("Sorry, no mailbox here by that name. vpopmail (#5.1.1)\n");

        /* exit 100 causes the email to be bounced back */
        vexit(100);

    }

    /* check if it is a path add the /Maildir/ for delivery */
    if ( bounce[0] == '/' ) {
        strcat( bounce, "/Maildir/");
        printf ("user does not exist, but will deliver to %s\n", bounce);
    } else {
        /* check if the forward is local and just deliver */
        parse_email( bounce, tmpuser, tmpdomain, AUTH_SIZE);
        if ( (vpw = vauth_getpw(tmpuser, tmpdomain)) != NULL ) {
            vget_assign(tmpdomain,NULL,0,&uid,&gid);
            if ( uid == TheDomainUid && gid == TheDomainGid ) {
                checkuser();
                return;
            }
        }
    }

    ret = deliver_mail(bounce, "NOQUOTA" );

    /* Send the email out, if we get a -1 then the user is over quota */
    if ( ret == -1 ) {
        printf("user is over quota, mail bounced\n");
        vexit(100);
    } else if ( ret == -2 ) {
        printf("system error\n");
        vexit(100);
    } else if ( ret != 0 ) {
        printf("mail is looping\n");
        vexit(100);
    }

}

/* 
 * Deliver an quota warning message
 * Return 0 on success
 * Return less than zero on failure
 * 
 * -1 = user is over quota
 * -2 and below are system failures
 * -3 mail is looping 
 */
int deliver_quota_warning(const char *dir, const char *q)
{
 time_t tm;
 long unsigned pid;
 long unsigned wrn_msg_sz;
 int write_fd, fdin, fd;
 size_t l;
 char newdir[400];
 char *qname = 0;
 struct stat     sb;
 char    buf[4096];
 FILE *fs;
 char quotawarnmsg[BUFF_SIZE];

    /* Look in the domain for a .quotawarn.msg */
    sprintf(quotawarnmsg, "%s/.quotawarn.msg", TheDomainDir);
    if ( ((fdin=open(quotawarnmsg, O_RDONLY)) < 0) || 
           (stat(quotawarnmsg, &sb)<0)) {

        /* if that fails look in vpopmail dir */
        sprintf(quotawarnmsg, "%s/domains/.quotawarn.msg", VPOPMAILDIR);
        if ( ((fdin=open(quotawarnmsg, O_RDONLY)) < 0) || 
              (stat(quotawarnmsg, &sb)<0)) {
            return(0);
        }
    }

    wrn_msg_sz = sb.st_size;

    l = strlen(dir)+sizeof("/quotawarn");

    if ((qname = malloc(l)) == 0)
    {
            close(fdin);
            perror("malloc");
            return(-1);
    }

    strcat(strcpy(qname, dir), "/quotawarn");
    time(&tm);


    /* Send only one warning every 24 hours */
    if (stat(qname, &sb) == 0 && ((sb.st_mtime + 86400) > tm))
    {
            free(qname);
            close(fdin);
            return(0);
    }

    fd = open(qname, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (!fd)
    {
            free(qname);
            close(fdin);
            perror("open");
            exit(111);
    }


    write(fd, buf, 0);
    close(fd);


    /* Format the email file name */
    gethostname(hostname,sizeof(hostname));
    pid=getpid();
    time (&tm);
    snprintf(local_file, 156, "%stmp/%lu.%lu.%s,S=%lu",
        dir,(long unsigned)tm,pid,hostname, wrn_msg_sz);
    snprintf(local_file_new, 156, "%snew/%lu.%lu.%s,S=%lu",
        dir,(long unsigned)tm,pid,hostname,wrn_msg_sz);

    /* open the new email file */
    if ((write_fd=open(local_file,O_CREAT|O_RDWR,S_IRUSR|S_IWUSR))== -1) {
        printf("can not open new email file errno=%d file=%s\n", 
            errno, local_file);
        return(-2);
    }
    if ( strcmp( dir, bounce) == 0 ) {
        snprintf(DeliveredTo, AUTH_SIZE, 
            "%s%s", getenv("RPLINE"), getenv("DTLINE"));
    } else {
        strcpy(newdir, dir);
        snprintf(DeliveredTo, AUTH_SIZE, 
            "%sDelivered-To: %s\n", getenv("RPLINE"), 
            maildir_to_email(newdir));
    }


    /* write the Return-Path: and Delivered-To: headers */
    if (write(write_fd,DeliveredTo,strlen(DeliveredTo)) != strlen(DeliveredTo)) {
        close(write_fd);
        /* Check if the user is over quota */
        if ( errno == EDQUOT ) {
            return(-1);
        } else {
            printf("failed to write delivered to line errno=%d\n",errno);
            return(errno);
        }
    }

    /* read the quota message in chunks and write it to the new file */
    if((fs=fopen(quotawarnmsg, "ro")) != NULL) {
        while(fgets(buf, MSG_BUF_SIZE, fs)) {
            if ( write(write_fd,buf,strlen(buf)) == -1 ) {
                close(write_fd);

                /* if the write fails and we are writing to a Maildir
                * then unlink the file
                */
                if ( unlink(local_file) != 0 ) {
                    printf("unlink failed %s errno = %d\n", local_file, errno);
                    return(errno);
                }

                /* Check if the user is over quota */
                if ( errno == EDQUOT ) {
                    return(-1);
                } else {
                    printf("write failed errno = %d\n", errno);
                    return(errno);
                }
            }
        }
    }

    /* since we are writing to a Maildir, move it
    * into the new directory
    */

    /* sync the data to disk and close the file */
    errno = 0;
    if ( 
#ifdef FILE_SYNC
#ifdef HAVE_FDATASYNC
        fdatasync(write_fd) == 0 &&
#else
        fsync(write_fd) == 0 &&
#endif
#endif
        close(write_fd) == 0 ) {

        /* if this succeeds link the file to the new directory */
        if ( link( local_file, local_file_new ) == 0 ) {
            if ( unlink(local_file) != 0 ) {
                printf("unlink failed %s errno = %d\n", local_file, errno);
            }
        } else {

            /* coda fs has problems with link, check for that error */
            if ( errno==EXDEV ) {

                /* try to rename the file instead */
                if (rename(local_file, local_file_new)!=0) {

                    /* even rename failed, time to give up */
                    printf("rename failed %s %s errno = %d\n", 
                        local_file, local_file_new, errno);
                    return(errno);

                /* rename worked, so we are okay now */
                } else {
                    errno = 0;
                }

            /* link failed and we are not on coda */
            } else {
                printf("link fucking failed %s %s errno = %d\n", 
                    local_file, local_file_new, errno);
            }
        }
    }

    /* return success */
    add_warningsize_to_quota(dir,q);
    return(0);
}

int is_loop_match( char *dt, char *address)
{
 char *startdt;

    startdt = dt;

    /* walk forward in dt line for @ character */
    while ( *dt != '@' && *dt != 0 ) ++dt;

    /* now walk back to first space */
    while ( *dt != ' ' && dt != startdt) --dt;

    /* step forward one character */
    ++dt;

   /* strcmp up to end of line or newline */
   while ( *dt != 0 && *dt != '\n' && *dt != '\r' ) {

     /* if it does not match, then no loop */
     if ( *dt != *address ) return(0);
     ++dt;
     ++address;
   }

   /* we have a match */
   return(1);
}
