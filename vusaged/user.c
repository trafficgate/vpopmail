/*
   $Id$

   * Copyright (C) 2009 Inter7 Internet Technologies, Inc.
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
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef ASSERT_DEBUG
   #include <assert.h>
#endif
#include <errno.h>
#include <vauth.h>
#include <vauthmodule.h>
#include <conf.h>
#include <storage.h>
#include "path.h"
#include "cache.h"
#include "userstore.h"
#include "domain.h"
#include "queue.h"
#include "user.h"

/*
   Linked list of all users currently allocated
*/

user_t *userlist = NULL;
storage_t userlist_num = 0;

static user_t *user_load(const char *);
static void user_remove(user_t *);

/*
   Domain data
*/

extern domain_t *domainlist;
extern storage_t domainlist_num;

/*
   Initialize user system
*/

int user_init(config_t *config)
{
   int ret = 0;
   const char *s = NULL;

#ifdef ASSERT_DEBUG
   assert(config != NULL);
#endif

   /*
	  Initialize userlist
   */

   userlist_num = 0;
   userlist = NULL;

   return 1;
}

/*
   Return a user handle from email address
   This function always returns a value if the
   user exists on the system
*/

user_t *user_get(const char *email)
{
   user_t *u = NULL;
   const char *p = NULL;

#ifdef ASSERT_DEBUG
   assert(email != NULL);
   assert(*(email) != '\0');

   for (p = email; *p; p++) {
	  if ((p - email) > 600)
		 assert("extremely long email address in user_get" == NULL);
   }
#endif

   /*
	  Do quick initial format test
   */

   for (p = email; *p; p++) {
	  if (*p == '@')
		 break;
   }

   if (!(*p))
	  return NULL;

   if (!(*(p + 1)))
	  return NULL;

   /*
	  Look for user in the cache
   */

   u = cache_lookup(email);
   if (u == NULL) {
	  /*
		 Load up previously unloaded user
	  */

	  u = user_load(email);
	  if (u == NULL) {
		 fprintf(stderr, "user_get: user_load failed\n");
		 return NULL;
	  }
   }

#ifdef ASSERT_DEBUG
   else {
	  assert(u->user != NULL);
	  assert(*(u->user) != '\0');
	  assert(u->domain != NULL);
	  assert(*(u->domain->domain) != '\0');
	  assert(!(strncasecmp(u->user, email, (p - email))));
	  assert(!(strncasecmp((p + 1), u->domain->domain, strlen(p + 1))));
   }
#endif

   return u;
}

/*
   Return the current approximate usage of a user
*/

storage_t user_usage(user_t *u)
{
   storage_t usage = 0;

#ifdef ASSERT_DEBUG
   assert(u != NULL);
#endif

   /*
	  Waiting for data still
   */

   if (u->userstore == NULL)
	  return 0;

   usage = userstore_usage(u->userstore);
   return usage;
}

/*
   Look up a user in the cache and return usage
   Does not call user_load to be called
*/

storage_t user_get_usage(const char *user)
{
   user_t *u = NULL;

   u = cache_lookup(user);
   if (u == NULL)
	  return -1;

   if (u->userstore == NULL)
	  return 0;

   return userstore_usage(u->userstore);
}

/*
   Look up a user and return size and counts
*/

int user_get_use(const char *user, storage_t *susage, storage_t *cusage)
{
   user_t *u = NULL;

   u = cache_lookup(user);
   if (u == NULL)
	  return -1;

   if ((susage == NULL) || (cusage == NULL))
	  return 0;

   if (u->userstore == NULL)
	  return 0;

   userstore_use(u->userstore, susage, cusage);
   return 1;
}


/*
   Allocate a user structure and fill it
   This should only be called by the controller thread
   since vpopmail is not thread-safe
*/

static user_t *user_load(const char *email)
{
   user_t *u = NULL;
   int ret = 0, len = 0;
   struct vqpasswd *pw = NULL, *npw = NULL;
   char *home = NULL;
   const char *p = NULL;
   char user[USER_MAX_USERNAME] = { 0 }, domain[DOMAIN_MAX_DOMAIN] = { 0 };
   domain_t *dom = NULL;
   userstore_t *userstore = NULL;

#ifdef ASSERT_DEBUG
   assert(email != NULL);
   assert(*(email) != '\0');
#endif

   /*
	  Find user@domain seperator
   */

   for (p = email; *p; p++) {
	  if (*p == '@')
		 break;
   }

   /*
	  Enforce format
   */

   if (!(*p))
	  return NULL;

   /*
	  vpopmail can mangle; We cannot
   */

   len = (p - email);
   if (len >= sizeof(user)) {
	  fprintf(stderr, "user_load: username too long\n");
	  return NULL;
   }

   memcpy(user, email, len);
   *(user + len) = '\0';

   len = strlen(p + 1);
   if (len >= sizeof(domain)) {
	  fprintf(stderr, "user_load: domain too long\n");
	  return NULL;
   }
   
   memcpy(domain, p + 1, len);
   *(domain + len) = '\0';

   /*
	  Look up user in vpopmail
   */

   pw = vauth_getpw(user, domain);
   if (pw == NULL) {
	  fprintf(stderr, "user_get: vauth_getpw(%s, %s) failed\n", user, domain);
	  return NULL;
   }

   /*
	  Create copy of vqpasswd structure
   */

   npw = malloc(sizeof(struct vqpasswd));
   if (npw == NULL) {
	  fprintf(stderr, "user_get: malloc failed\n");
	  return NULL;
   }

   if (1) {
	  npw->pw_name = strdup(pw->pw_name);
	  if (npw->pw_name == NULL) {
		 free(npw);
		 fprintf(stderr, "user_get: strdup failed\n");
		 return NULL;
	  }

	  npw->pw_passwd = strdup(pw->pw_passwd);
	  if (npw->pw_passwd == NULL) {
		 free(npw->pw_name);
		 free(npw);
		 fprintf(stderr, "user_get: strdup failed\n");
		 return NULL;
	  }

	  npw->pw_gecos = strdup(pw->pw_gecos);
	  if (npw->pw_gecos == NULL) {
		 free(npw->pw_passwd);
		 free(npw->pw_name);
		 free(npw);
		 fprintf(stderr, "user_get: strdup failed\n");
		 return NULL;
	  }

	  npw->pw_dir = strdup(pw->pw_dir);
	  if (npw->pw_dir == NULL) {
		 free(npw->pw_gecos);
		 free(npw->pw_passwd);
		 free(npw->pw_name);
		 free(npw);
		 fprintf(stderr, "user_get: strdup failed\n");
		 return NULL;
	  }

	  npw->pw_shell = strdup(pw->pw_shell);
	  if (npw->pw_shell == NULL) {
		 free(npw->pw_dir);
		 free(npw->pw_gecos);
		 free(npw->pw_passwd);
		 free(npw->pw_name);
		 free(npw);
		 fprintf(stderr, "user_get: strdup failed\n");
		 return NULL;
	  }

	  npw->pw_clear_passwd = strdup(pw->pw_clear_passwd);
	  if (npw->pw_clear_passwd == NULL) {
		 free(npw->pw_shell);
		 free(npw->pw_dir);
		 free(npw->pw_gecos);
		 free(npw->pw_passwd);
		 free(npw->pw_name);
		 free(npw);
		 fprintf(stderr, "user_get: strdup failed\n");
		 return NULL;
	  }

	  npw->pw_uid = pw->pw_uid;
	  npw->pw_gid = pw->pw_gid;
	  npw->pw_flags = pw->pw_flags;
   }

   /*
	  Our root directory is the Maildir of the user
   */

   len = (strlen(pw->pw_dir) + strlen("/Maildir"));
   if ((len + 1) >= PATH_MAX) {
	  free(npw->pw_clear_passwd);
	  free(npw->pw_shell);
	  free(npw->pw_dir);
	  free(npw->pw_gecos);
	  free(npw->pw_passwd);
	  free(npw->pw_name);
	  free(npw);
	  fprintf(stderr, "user_get: home directory too long\n");
	  return NULL;
   }

   home = malloc(len + 1);
   if (home == NULL) {
	  free(npw->pw_clear_passwd);
	  free(npw->pw_shell);
	  free(npw->pw_dir);
	  free(npw->pw_gecos);
	  free(npw->pw_passwd);
	  free(npw->pw_name);
	  free(npw);
	  fprintf(stderr, "user_get: malloc failed\n");
	  return NULL;
   }

   snprintf(home, len + 1, "%s/Maildir", pw->pw_dir);
   *(home + len) = '\0';

   /*
	  Load the domain
   */

   dom = domain_load(domain);
   if (dom == NULL) {
	  free(npw->pw_clear_passwd);
	  free(npw->pw_shell);
	  free(npw->pw_dir);
	  free(npw->pw_gecos);
	  free(npw->pw_passwd);
	  free(npw->pw_name);
	  free(npw);
	  fprintf(stderr, "user_get: domain_load failed\n");
	  free(home);
	  return NULL;
   }

   /*
	  Allocate structure
   */

   u = malloc(sizeof(user_t));
   if (u == NULL) {
	  free(npw->pw_clear_passwd);
	  free(npw->pw_shell);
	  free(npw->pw_dir);
	  free(npw->pw_gecos);
	  free(npw->pw_passwd);
	  free(npw->pw_name);
	  free(npw);
	  fprintf(stderr, "user_load: malloc failed\n");
	  return NULL;
   }

   memset(u, 0, sizeof(user_t));

   /*
	  Initialize processing mutex
   */

   ret = pthread_mutex_init(&u->m_processing, NULL);
   if (ret != 0) {
	  free(npw->pw_clear_passwd);
	  free(npw->pw_shell);
	  free(npw->pw_dir);
	  free(npw->pw_gecos);
	  free(npw->pw_passwd);
	  free(npw->pw_name);
	  free(npw);
	  free(u);
	  free(home);
	  userstore_free(userstore);
#if 0
	  domain_free(dom);
#endif
	  fprintf(stderr, "user_load: pthread_mutex_init failed\n");
	  return NULL;
   }

   /*
	  Copy username
   */

   len = (p - email);
   u->user = malloc(len + 1);
   if (u->user == NULL) {
	  free(npw->pw_clear_passwd);
	  free(npw->pw_shell);
	  free(npw->pw_dir);
	  free(npw->pw_gecos);
	  free(npw->pw_passwd);
	  free(npw->pw_name);
	  free(npw);
	  fprintf(stderr, "user_load: malloc failed\n");
	  free(u);
	  free(home);
	  userstore_free(userstore);
#if 0
	  domain_free(dom);
#endif
	  return NULL;
   }

   memset(u->user, 0, len + 1);
   memcpy(u->user, email, len);

   u->home = home;
   u->domain = dom;
   u->pw = npw;
   u->userstore = NULL;

   /*
	  Add to userlist
   */

   ret = user_userlist_add(u);
   if (!ret) {
	  user_free(u);
	  fprintf(stderr, "user_load: user_userlist_add failed\n");
	  return NULL;
   }

   return u;
}

/*
   Remove user structure from user list linked list
*/

static void user_remove(user_t *u)
{
#ifdef ASSERT_DEBUG
   assert(u != NULL);
   assert(userlist != NULL);
   assert(userlist_num > 0);
#endif

   if (u->next)
	  u->next->prev = u->prev;

   if (u->prev)
	  u->prev->next = u->next;

   if (u == userlist)
	  userlist = u->next;

   userlist_num--;
}

/*
   Deallocate a user structure
*/

void user_free(user_t *u)
{
#ifdef ASSERT_DEBUG
   assert(u != NULL);
#endif

   pthread_mutex_destroy(&u->m_processing);

   if (u->home)
	  free(u->home);

   if (u->user)
	  free(u->user);

   if (u->userstore)
	  userstore_free(u->userstore);

   if (u->pw) {
	  if (u->pw->pw_name)
		 free(u->pw->pw_name);

	  if (u->pw->pw_passwd)
		 free(u->pw->pw_passwd);

	  if (u->pw->pw_gecos)
		 free(u->pw->pw_gecos);

	  if (u->pw->pw_dir)
		 free(u->pw->pw_dir);

	  if (u->pw->pw_shell)
		 free(u->pw->pw_shell);

	  if (u->pw->pw_clear_passwd)
		 free(u->pw->pw_clear_passwd);

	  free(u->pw);
   }

   free(u);
}

/*
   Update user structure
*/

int user_poll(user_t *u)
{
   int ret = 0;
   storage_t before = 0, cbefore = 0, after = 0, cafter = 0;

#ifdef ASSERT_DEBUG
   assert(u != NULL);
   assert(u->user != NULL);
   assert(u->domain != NULL);
   assert(u->domain->domain != NULL);
#endif

   /*
	  Load the userstore if it hasn't already been loaded
   */

   if (u->userstore == NULL) {
	  u->userstore = userstore_load(u->home);
	  before = cbefore = 0;
   }

   /*
	  Otherwise poll for changes
   */

   else {
	  userstore_use(u->userstore, &before, &cbefore);
	  ret = userstore_poll(u->userstore);
   }
   
   /*
	  Update domain record
   */

   if (u->userstore) {
	  userstore_use(u->userstore, &after, &cafter);

	  ret = domain_update(u->domain, before, after, cbefore, cafter);
	  if (!ret)
		 fprintf(stderr, "user_poll: domain_update failed\n");
   }

   return 1;
}

/*
   Returns if a user exists within vpopmail
   This function should only be called by the controller
   thread because vpopmail is not thread-safe
*/

int user_verify(user_t *u)
{
   int ret = 0;
   storage_t usage = 0, count = 0;
   char b[USER_MAX_USERNAME + DOMAIN_MAX_DOMAIN] = { 0 };

#ifdef ASSERT_DEBUG
   assert(u != NULL);
   assert(u->user != NULL);
   assert(u->domain != NULL);
   assert(u->domain->domain != NULL);
#endif

   if (vauth_getpw(u->user, u->domain->domain) == NULL) {
#ifdef USER_DEBUG
	  printf("user: lost %s@%s\n", u->user, u->domain->domain);
#endif

	  ret = snprintf(b, sizeof(b), "%s@%s", u->user, u->domain->domain);
	  *(b + ret) = '\0';

	  /*
		 Update domain usage
	  */

	  if (u->userstore) {
		 userstore_use(u->userstore, &usage, &count);
		 domain_update(u->domain, usage, 0, count, 0);
	  }

	  /*
		 Remove user from the cache and free all memory
		 associated with it
	  */

	  cache_remove(b);
	  user_remove(u);
	  user_free(u);

	  /*
		 Return verification failed
	  */

	  return 0;
   }

   /*
	  User is a vpopmail user
   */

   return 1;
}

/*
   Add user to userlist
*/

int user_userlist_add(user_t *u)
{
   int ret = 0;
   char b[384] = { 0 };

#ifdef ASSERT_DEBUG
   assert(u != NULL);
   assert(u->domain != NULL);
   assert(u->domain->domain != NULL);
   assert(u->home != NULL);
   assert(u->user != NULL);
#endif

   if (userlist)
	  userlist->prev = u;

   u->next = userlist;
   userlist = u;

   userlist_num++;

   /*
	  Add to cache
   */

   ret = snprintf(b, sizeof(b), "%s@%s", u->user, u->domain->domain);
   if (ret >= sizeof(b)) {
	  fprintf(stderr, "user_userlist_add: address too long\n");
	  return 0;
   }

   ret = cache_add(b, u);
   if (!ret) {
	  fprintf(stderr, "user_userlist_add: cache_add failed\n");
	  return 0;
   }

   return 1;
}
