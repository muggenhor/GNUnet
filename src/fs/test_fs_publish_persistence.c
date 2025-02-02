/*
     This file is part of GNUnet.
     Copyright (C) 2004, 2005, 2006, 2008, 2009, 2010 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 3, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
     Boston, MA 02110-1301, USA.
*/
/**
 * @file fs/test_fs_publish_persistence.c
 * @brief simple testcase for persistence of simple publish operation
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_testing_lib.h"
#include "gnunet_fs_service.h"


/**
 * File-size we use for testing.
 */
#define FILESIZE (1024 * 1024 * 2)

/**
 * How long until we give up on transmitting the message?
 */
#define TIMEOUT GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_SECONDS, 60)

/**
 * How long should our test-content live?
 */
#define LIFETIME GNUNET_TIME_relative_multiply (GNUNET_TIME_UNIT_MINUTES, 15)


static struct GNUNET_TIME_Absolute start;

static struct GNUNET_FS_Handle *fs;

static const struct GNUNET_CONFIGURATION_Handle *cfg;

static struct GNUNET_FS_PublishContext *publish;

static struct GNUNET_FS_PublishContext *publish;

static char *fn1;

static char *fn2;

static int err;

static struct GNUNET_SCHEDULER_Task * rtask;


static void
abort_publish_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_FS_publish_stop (publish);
  publish = NULL;
  GNUNET_DISK_directory_remove (fn1);
  GNUNET_free (fn1);
  fn1 = NULL;
  GNUNET_DISK_directory_remove (fn2);
  GNUNET_free (fn2);
  fn2 = NULL;
  GNUNET_FS_stop (fs);
  fs = NULL;
  if (NULL != rtask)
  {
    GNUNET_SCHEDULER_cancel (rtask);
    rtask = NULL;
  }
}


static void *
progress_cb (void *cls, const struct GNUNET_FS_ProgressInfo *event);


static void
restart_fs_task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  rtask = NULL;
  GNUNET_FS_stop (fs);
  fs = GNUNET_FS_start (cfg, "test-fs-publish-persistence",
			&progress_cb, NULL,
                        GNUNET_FS_FLAGS_PERSISTENCE,
			GNUNET_FS_OPTIONS_END);
}


/**
 * Consider scheduling the restart-task.
 * Only runs the restart task once per event
 * category.
 *
 * @param ev type of the event to consider
 */
static void
consider_restart (int ev)
{
  static int prev[32];
  static int off;
  int i;

  for (i = 0; i < off; i++)
    if (prev[i] == ev)
      return;
  prev[off++] = ev;
  rtask =
      GNUNET_SCHEDULER_add_with_priority (GNUNET_SCHEDULER_PRIORITY_URGENT,
                                          &restart_fs_task, NULL);
}


static void *
progress_cb (void *cls,
	     const struct GNUNET_FS_ProgressInfo *event)
{
  void *ret;

  ret = NULL;
  switch (event->status)
  {
  case GNUNET_FS_STATUS_PUBLISH_COMPLETED:
    consider_restart (event->status);
    ret = event->value.publish.cctx;
    printf ("Publish complete,  %llu kbps.\n",
            (unsigned long long) (FILESIZE * 1000000LL /
                                  (1 +
                                   GNUNET_TIME_absolute_get_duration
                                   (start).rel_value_us) / 1024));
    if ( (NULL != event->value.publish.cctx) &&
	 (0 == strcmp ("publish-context-dir", event->value.publish.cctx)) )
      GNUNET_SCHEDULER_add_now (&abort_publish_task, NULL);
    break;
  case GNUNET_FS_STATUS_PUBLISH_PROGRESS_DIRECTORY:
    ret = event->value.publish.cctx;
    return ret;
  case GNUNET_FS_STATUS_PUBLISH_PROGRESS:
    consider_restart (event->status);
    ret = event->value.publish.cctx;
    GNUNET_assert (publish == event->value.publish.pc);
#if VERBOSE
    printf ("Publish is progressing (%llu/%llu at level %u off %llu)...\n",
            (unsigned long long) event->value.publish.completed,
            (unsigned long long) event->value.publish.size,
            event->value.publish.specifics.progress.depth,
            (unsigned long long) event->value.publish.specifics.
            progress.offset);
#endif
    break;
  case GNUNET_FS_STATUS_PUBLISH_SUSPEND:
    if (event->value.publish.pc == publish)
      publish = NULL;
    break;
  case GNUNET_FS_STATUS_PUBLISH_RESUME:
    if (NULL == publish)
    {
      GNUNET_assert (GNUNET_YES ==
                     GNUNET_FS_file_information_is_directory (event->
                                                              value.publish.
                                                              fi));
      publish = event->value.publish.pc;
      return "publish-context-dir";
    }
    break;
  case GNUNET_FS_STATUS_PUBLISH_ERROR:
    ret = event->value.publish.cctx;
    FPRINTF (stderr, "Error publishing file: %s\n",
             event->value.publish.specifics.error.message);
    err = 1;
    GNUNET_SCHEDULER_add_now (&abort_publish_task, NULL);
    break;
  case GNUNET_FS_STATUS_PUBLISH_START:
    consider_restart (event->status);
    publish = event->value.publish.pc;
    ret = event->value.publish.cctx;
    if (0 == strcmp ("publish-context1", event->value.publish.cctx))
    {
      GNUNET_assert (0 ==
                     strcmp ("publish-context-dir", event->value.publish.pctx));
      GNUNET_assert (FILESIZE == event->value.publish.size);
      GNUNET_assert (0 == event->value.publish.completed);
      GNUNET_assert (1 == event->value.publish.anonymity);
    }
    else if (0 == strcmp ("publish-context2", event->value.publish.cctx))
    {
      GNUNET_assert (0 ==
                     strcmp ("publish-context-dir", event->value.publish.pctx));
      GNUNET_assert (FILESIZE == event->value.publish.size);
      GNUNET_assert (0 == event->value.publish.completed);
      GNUNET_assert (2 == event->value.publish.anonymity);
    }
    else if (0 == strcmp ("publish-context-dir", event->value.publish.cctx))
    {
      GNUNET_assert (0 == event->value.publish.completed);
      GNUNET_assert (3 == event->value.publish.anonymity);
    }
    else
      GNUNET_assert (0);
    break;
  case GNUNET_FS_STATUS_PUBLISH_STOPPED:
    consider_restart (event->status);
    if ( (NULL != event->value.publish.cctx) &&
	 (0 == strcmp ("publish-context-dir", event->value.publish.cctx)) )
      GNUNET_assert (publish == event->value.publish.pc);
    break;
  default:
    printf ("Unexpected event: %d\n", event->status);
    break;
  }
  return ret;
}


static void
run (void *cls,
     const struct GNUNET_CONFIGURATION_Handle *c,
     struct GNUNET_TESTING_Peer *peer)
{
  const char *keywords[] = {
    "down_foo",
    "down_bar",
  };
  char *buf;
  struct GNUNET_CONTAINER_MetaData *meta;
  struct GNUNET_FS_Uri *kuri;
  struct GNUNET_FS_FileInformation *fi1;
  struct GNUNET_FS_FileInformation *fi2;
  struct GNUNET_FS_FileInformation *fidir;
  size_t i;
  struct GNUNET_FS_BlockOptions bo;

  cfg = c;
  fs = GNUNET_FS_start (cfg, "test-fs-publish-persistence", &progress_cb, NULL,
                        GNUNET_FS_FLAGS_PERSISTENCE, GNUNET_FS_OPTIONS_END);
  GNUNET_assert (NULL != fs);
  fn1 = GNUNET_DISK_mktemp ("gnunet-publish-test-dst");
  buf = GNUNET_malloc (FILESIZE);
  for (i = 0; i < FILESIZE; i++)
    buf[i] = GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, 256);
  GNUNET_assert (FILESIZE ==
                 GNUNET_DISK_fn_write (fn1, buf, FILESIZE,
                                       GNUNET_DISK_PERM_USER_READ |
                                       GNUNET_DISK_PERM_USER_WRITE));
  GNUNET_free (buf);

  fn2 = GNUNET_DISK_mktemp ("gnunet-publish-test-dst");
  buf = GNUNET_malloc (FILESIZE);
  for (i = 0; i < FILESIZE; i++)
    buf[i] = GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, 256);
  GNUNET_assert (FILESIZE ==
                 GNUNET_DISK_fn_write (fn2, buf, FILESIZE,
                                       GNUNET_DISK_PERM_USER_READ |
                                       GNUNET_DISK_PERM_USER_WRITE));
  GNUNET_free (buf);

  meta = GNUNET_CONTAINER_meta_data_create ();
  kuri = GNUNET_FS_uri_ksk_create_from_args (2, keywords);
  bo.content_priority = 42;
  bo.anonymity_level = 1;
  bo.replication_level = 0;
  bo.expiration_time = GNUNET_TIME_relative_to_absolute (LIFETIME);
  fi1 =
      GNUNET_FS_file_information_create_from_file (fs, "publish-context1", fn1,
                                                   kuri, meta, GNUNET_YES, &bo);
  GNUNET_assert (NULL != fi1);
  bo.anonymity_level = 2;
  fi2 =
      GNUNET_FS_file_information_create_from_file (fs, "publish-context2", fn2,
                                                   kuri, meta, GNUNET_YES, &bo);
  GNUNET_assert (NULL != fi2);
  bo.anonymity_level = 3;
  fidir =
      GNUNET_FS_file_information_create_empty_directory (fs,
                                                         "publish-context-dir",
                                                         kuri, meta, &bo, NULL);
  GNUNET_assert (GNUNET_OK == GNUNET_FS_file_information_add (fidir, fi1));
  GNUNET_assert (GNUNET_OK == GNUNET_FS_file_information_add (fidir, fi2));
  GNUNET_FS_uri_destroy (kuri);
  GNUNET_CONTAINER_meta_data_destroy (meta);
  GNUNET_assert (NULL != fidir);
  start = GNUNET_TIME_absolute_get ();
  GNUNET_FS_publish_start (fs, fidir, NULL, NULL, NULL,
                           GNUNET_FS_PUBLISH_OPTION_NONE);
  GNUNET_assert (publish != NULL);
}


int
main (int argc, char *argv[])
{
  if (0 != GNUNET_TESTING_peer_run ("test-fs-publish-persistence",
				    "test_fs_publish_data.conf",
				    &run, NULL))
    return 1;
  return err;
}

/* end of test_fs_publish_persistence.c */
