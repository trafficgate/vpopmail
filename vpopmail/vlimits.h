/*
 * vlimits.h
 * handle domain limits in both file and mysql tables
 * Brian Kolaci <bk@galaxy.net>
 */

#ifndef VPOPMAIL_VLIMITS_H
#define VPOPMAIL_VLIMITS_H

/* permissions for non-postmaster admins */
#define VLIMIT_DISABLE_CREATE 0x01
#define VLIMIT_DISABLE_MODIFY 0x02
#define VLIMIT_DISABLE_DELETE 0x04

#define VLIMIT_DISABLE_ALL (VLIMIT_DISABLE_CREATE|VLIMIT_DISABLE_MODIFY|VLIMIT_DISABLE_DELETE)
#define VLIMIT_DISABLE_BITS 3

struct vlimits {
      /* max service limits */
      int       maxpopaccounts;
      int       maxaliases;
      int       maxforwards;
      int       maxautoresponders;
      int       maxmailinglists;

      /* quota & message count limits */
      int       diskquota;
      int       maxmsgcount;
      int       defaultquota;
      int       defaultmaxmsgcount;

      /* the following are 0 (false) or 1 (true) */
      short     disable_pop;
      short     disable_imap;
      short     disable_dialup;
      short     disable_passwordchanging;
      short     disable_webmail;
      short     disable_relay;
      short     disable_smtp;

      /* the following permissions are for non-postmaster admins */
      short     perm_account;
      short     perm_alias;
      short     perm_forward;
      short     perm_autoresponder;
      short     perm_maillist;
      short     perm_maillist_users;
      short     perm_maillist_moderators;
      short     perm_quota;
      short     perm_defaultquota;
};

void vdefault_limits(struct vlimits *limits);
int vget_limits(const char * domain, struct vlimits * limits);
int vset_limits(const char * domain, const struct vlimits * limits);
int vdel_limits(const char * domain);
int vlimits_read_limits_file(const char * dir, struct vlimits * limits);
int vlimits_get_gid_mask(struct vlimits *limits);
#endif
