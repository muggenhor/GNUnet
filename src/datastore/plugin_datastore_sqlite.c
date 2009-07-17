 /*
     This file is part of GNUnet
     (C) 2009 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

/**
 * @file datastore/plugin_datastore_sqlite.c
 * @brief sqlite-based datastore backend
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_statistics_service.h"
#include "plugin_datastore.h"
#include <sqlite3.h>

#define DEBUG_SQLITE GNUNET_YES

/**
 * After how many payload-changing operations
 * do we sync our statistics?
 */
#define MAX_STAT_SYNC_LAG 50

#define QUOTA_STAT_NAME gettext_noop ("file-sharing datastore utilization (in bytes)")


/**
 * Die with an error message that indicates
 * a failure of the command 'cmd' with the message given
 * by strerror(errno).
 */
#define DIE_SQLITE(db, cmd) do { GNUNET_log_from(GNUNET_ERROR_TYPE_ERROR, "sqlite", _("`%s' failed at %s:%d with error: %s\n"), cmd, __FILE__, __LINE__, sqlite3_errmsg(db->dbh)); abort(); } while(0)

/**
 * Log an error message at log-level 'level' that indicates
 * a failure of the command 'cmd' on file 'filename'
 * with the message given by strerror(errno).
 */
#define LOG_SQLITE(db, msg, level, cmd) do { GNUNET_log_from (level, "sqlite", _("`%s' failed at %s:%d with error: %s\n"), cmd, __FILE__, __LINE__, sqlite3_errmsg(db->dbh)); if (msg != NULL) GNUNET_asprintf(msg, _("`%s' failed with error: %s\n"), cmd, sqlite3_errmsg(db->dbh)); } while(0)

#define SELECT_IT_LOW_PRIORITY_1 \
  "SELECT size,type,prio,anonLevel,expire,hash,value,_ROWID_ FROM gn080 WHERE (prio = ? AND hash > ?) "\
  "ORDER BY hash ASC LIMIT 1"

#define SELECT_IT_LOW_PRIORITY_2 \
  "SELECT size,type,prio,anonLevel,expire,hash,value,_ROWID_ FROM gn080 WHERE (prio > ?) "\
  "ORDER BY prio ASC, hash ASC LIMIT 1"

#define SELECT_IT_NON_ANONYMOUS_1 \
  "SELECT size,type,prio,anonLevel,expire,hash,value,_ROWID_ FROM gn080 WHERE (prio = ? AND hash < ? AND anonLevel = 0 AND expire > %llu) "\
  " ORDER BY hash DESC LIMIT 1"

#define SELECT_IT_NON_ANONYMOUS_2 \
  "SELECT size,type,prio,anonLevel,expire,hash,value,_ROWID_ FROM gn080 WHERE (prio < ? AND anonLevel = 0 AND expire > %llu)"\
  " ORDER BY prio DESC, hash DESC LIMIT 1"

#define SELECT_IT_EXPIRATION_TIME_1 \
  "SELECT size,type,prio,anonLevel,expire,hash,value,_ROWID_ FROM gn080 WHERE (expire = ? AND hash > ?) "\
  " ORDER BY hash ASC LIMIT 1"

#define SELECT_IT_EXPIRATION_TIME_2 \
  "SELECT size,type,prio,anonLevel,expire,hash,value,_ROWID_ FROM gn080 WHERE (expire > ?) "\
  " ORDER BY expire ASC, hash ASC LIMIT 1"

#define SELECT_IT_MIGRATION_ORDER_1 \
  "SELECT size,type,prio,anonLevel,expire,hash,value,_ROWID_ FROM gn080 WHERE (expire = ? AND hash < ?) "\
  " ORDER BY hash DESC LIMIT 1"

#define SELECT_IT_MIGRATION_ORDER_2 \
  "SELECT size,type,prio,anonLevel,expire,hash,value,_ROWID_ FROM gn080 WHERE (expire < ? AND expire > %llu) "\
  " ORDER BY expire DESC, hash DESC LIMIT 1"

/**
 * After how many ms "busy" should a DB operation fail for good?
 * A low value makes sure that we are more responsive to requests
 * (especially PUTs).  A high value guarantees a higher success
 * rate (SELECTs in iterate can take several seconds despite LIMIT=1).
 *
 * The default value of 250ms should ensure that users do not experience
 * huge latencies while at the same time allowing operations to succeed
 * with reasonable probability.
 */
#define BUSY_TIMEOUT_MS 250


/**
 * Context for all functions in this plugin.
 */
struct Plugin 
{
  /**
   * Our execution environment.
   */
  struct GNUNET_DATASTORE_PluginEnvironment *env;

  /**
   * Database filename.
   */
  char *fn;

  /**
   * Native SQLite database handle.
   */
  sqlite3 *dbh;

  /**
   * Precompiled SQL for update.
   */
  sqlite3_stmt *updPrio;

  /**
   * Precompiled SQL for insertion.
   */
  sqlite3_stmt *insertContent;

  /**
   * Handle to the statistics service.
   */
  struct GNUNET_STATISTICS_Handle *statistics;
  
  /**
   * How much data are we currently storing
   * in the database?
   */
  unsigned long long payload;

  /**
   * Number of updates that were made to the
   * payload value since we last synchronized
   * it with the statistics service.
   */
  unsigned int lastSync;

  /**
   * Should the database be dropped on shutdown?
   */
  int drop_on_shutdown;
};


/**
 * @brief Prepare a SQL statement
 *
 * @param zSql SQL statement, UTF-8 encoded
 */
static int
sq_prepare (sqlite3 * dbh, const char *zSql,
            sqlite3_stmt ** ppStmt)
{
  char *dummy;
  return sqlite3_prepare (dbh,
                          zSql,
                          strlen (zSql), ppStmt, (const char **) &dummy);
}


/**
 * Create our database indices.
 */
static void
create_indices (sqlite3 * dbh)
{
  /* create indices */
  sqlite3_exec (dbh,
                "CREATE INDEX idx_hash ON gn080 (hash)", NULL, NULL, NULL);
  sqlite3_exec (dbh,
                "CREATE INDEX idx_hash_vhash ON gn080 (hash,vhash)", NULL,
                NULL, NULL);
  sqlite3_exec (dbh, "CREATE INDEX idx_prio ON gn080 (prio)", NULL, NULL,
                NULL);
  sqlite3_exec (dbh, "CREATE INDEX idx_expire ON gn080 (expire)", NULL, NULL,
                NULL);
  sqlite3_exec (dbh, "CREATE INDEX idx_comb3 ON gn080 (prio,anonLevel)", NULL,
                NULL, NULL);
  sqlite3_exec (dbh, "CREATE INDEX idx_comb4 ON gn080 (prio,hash,anonLevel)",
                NULL, NULL, NULL);
  sqlite3_exec (dbh, "CREATE INDEX idx_comb7 ON gn080 (expire,hash)", NULL,
                NULL, NULL);
}



#if 1
#define CHECK(a) GNUNET_break(a)
#define ENULL NULL
#else
#define ENULL &e
#define ENULL_DEFINED 1
#define CHECK(a) if (! a) { GNUNET_log(GNUNET_ERROR_TYPE_ERRROR, "%s\n", e); sqlite3_free(e); }
#endif




/**
 * Initialize the database connections and associated
 * data structures (create tables and indices
 * as needed as well).
 *
 * @return GNUNET_OK on success
 */
static int
database_setup (struct GNUNET_CONFIGURATION_Handle *cfg,
		struct Plugin *plugin)
{
  sqlite3_stmt *stmt;
  char *afsdir;
#if ENULL_DEFINED
  char *e;
#endif
  
  if (GNUNET_OK != 
      GNUNET_CONFIGURATION_get_value_filename (cfg,
					       "datastore-sqlite",
					       "FILENAME",
					       &afsdir))
    {
      GNUNET_log_from (GNUNET_ERROR_TYPE_ERROR,
		       "sqlite",
		       _("Option `%s' in section `%s' missing in configuration!\n"),
		       "FILENAME",
		       "datastore-sqlite");
      return GNUNET_SYSERR;
    }
  if (GNUNET_OK != GNUNET_DISK_directory_create_for_file (afsdir))
    {
      GNUNET_break (0);
      GNUNET_free (afsdir);
      return GNUNET_SYSERR;
    }
  plugin->fn = GNUNET_STRINGS_to_utf8 (afsdir, strlen (afsdir),
#ifdef ENABLE_NLS
					      nl_langinfo (CODESET)
#else
					      "UTF-8"   /* good luck */
#endif
					      );
  GNUNET_free (afsdir);
  
  /* Open database and precompile statements */
  if (sqlite3_open (plugin->fn, &plugin->dbh) != SQLITE_OK)
    {
      GNUNET_log_from (GNUNET_ERROR_TYPE_ERROR,
		       "sqlite",
		       _("Unable to initialize SQLite: %s.\n"),
		       sqlite3_errmsg (plugin->dbh));
      return GNUNET_SYSERR;
    }
  CHECK (SQLITE_OK ==
         sqlite3_exec (plugin->dbh,
                       "PRAGMA temp_store=MEMORY", NULL, NULL, ENULL));
  CHECK (SQLITE_OK ==
         sqlite3_exec (plugin->dbh,
                       "PRAGMA synchronous=OFF", NULL, NULL, ENULL));
  CHECK (SQLITE_OK ==
         sqlite3_exec (plugin->dbh,
                       "PRAGMA count_changes=OFF", NULL, NULL, ENULL));
  CHECK (SQLITE_OK ==
         sqlite3_exec (plugin->dbh, "PRAGMA page_size=4092", NULL, NULL, ENULL));

  CHECK (SQLITE_OK == sqlite3_busy_timeout (plugin->dbh, BUSY_TIMEOUT_MS));


  /* We have to do it here, because otherwise precompiling SQL might fail */
  CHECK (SQLITE_OK ==
         sq_prepare (plugin->dbh,
                     "SELECT 1 FROM sqlite_master WHERE tbl_name = 'gn080'",
                     &stmt));
  if ( (sqlite3_step (stmt) == SQLITE_DONE) &&
       (sqlite3_exec (plugin->dbh,
		      "CREATE TABLE gn080 ("
		      "  size INTEGER NOT NULL DEFAULT 0,"
		      "  type INTEGER NOT NULL DEFAULT 0,"
		      "  prio INTEGER NOT NULL DEFAULT 0,"
		      "  anonLevel INTEGER NOT NULL DEFAULT 0,"
		      "  expire INTEGER NOT NULL DEFAULT 0,"
		      "  hash TEXT NOT NULL DEFAULT '',"
		      "  vhash TEXT NOT NULL DEFAULT '',"
		      "  value BLOB NOT NULL DEFAULT '')", NULL, NULL,
		      NULL) != SQLITE_OK) )
    {
      LOG_SQLITE (plugin, NULL,
		  GNUNET_ERROR_TYPE_ERROR, 
		  "sqlite3_exec");
      sqlite3_finalize (stmt);
      return GNUNET_SYSERR;
    }
  sqlite3_finalize (stmt);
  create_indices (plugin->dbh);

  CHECK (SQLITE_OK ==
         sq_prepare (plugin->dbh,
                     "SELECT 1 FROM sqlite_master WHERE tbl_name = 'gn071'",
                     &stmt));
  if ( (sqlite3_step (stmt) == SQLITE_DONE) &&
       (sqlite3_exec (plugin->dbh,
		      "CREATE TABLE gn071 ("
		      "  key TEXT NOT NULL DEFAULT '',"
		      "  value INTEGER NOT NULL DEFAULT 0)", NULL, NULL,
		      NULL) != SQLITE_OK) )
    {
      LOG_SQLITE (plugin, NULL,
		  GNUNET_ERROR_TYPE_ERROR, "sqlite3_exec");
      sqlite3_finalize (stmt);
      return GNUNET_SYSERR;
    }
  sqlite3_finalize (stmt);

  if ((sq_prepare (plugin->dbh,
                   "UPDATE gn080 SET prio = prio + ?, expire = MAX(expire,?) WHERE "
                   "_ROWID_ = ?",
                   &plugin->updPrio) != SQLITE_OK) ||
      (sq_prepare (plugin->dbh,
                   "INSERT INTO gn080 (size, type, prio, "
                   "anonLevel, expire, hash, vhash, value) VALUES "
                   "(?, ?, ?, ?, ?, ?, ?, ?)",
                   &plugin->insertContent) != SQLITE_OK))
    {
      LOG_SQLITE (plugin, NULL,
                  GNUNET_ERROR_TYPE_ERROR, "precompiling");
      return GNUNET_SYSERR;
    }
  return GNUNET_OK;
}


/**
 * Synchronize our utilization statistics with the 
 * statistics service.
 */
static void 
sync_stats (struct Plugin *plugin)
{
  GNUNET_STATISTICS_set (plugin->statistics,
			 QUOTA_STAT_NAME,
			 plugin->payload,
			 GNUNET_YES);
  plugin->lastSync = 0;
}


/**
 * Shutdown database connection and associate data
 * structures.
 */
static void
database_shutdown (struct Plugin *plugin)
{
  if (plugin->lastSync > 0)
    sync_stats (plugin);
  if (plugin->updPrio != NULL)
    sqlite3_finalize (plugin->updPrio);
  if (plugin->insertContent != NULL)
    sqlite3_finalize (plugin->insertContent);
  sqlite3_close (plugin->dbh);
  GNUNET_free_non_null (plugin->fn);
}


/**
 * Get an estimate of how much space the database is
 * currently using.
 * @return number of bytes used on disk
 */
static unsigned long long sqlite_plugin_get_size (void *cls)
{
  struct Plugin *plugin = cls;
  return plugin->payload;
}


/**
 * Delete the database entry with the given
 * row identifier.
 */
static int
delete_by_rowid (struct Plugin* plugin, 
		 unsigned long long rid)
{
  sqlite3_stmt *stmt;

  if (sq_prepare (plugin->dbh,
                  "DELETE FROM gn080 WHERE _ROWID_ = ?", &stmt) != SQLITE_OK)
    {
      LOG_SQLITE (plugin, NULL,
                  GNUNET_ERROR_TYPE_ERROR |
                  GNUNET_ERROR_TYPE_BULK, "sq_prepare");
      return GNUNET_SYSERR;
    }
  sqlite3_bind_int64 (stmt, 1, rid);
  if (SQLITE_DONE != sqlite3_step (stmt))
    {
      LOG_SQLITE (plugin, NULL,
                  GNUNET_ERROR_TYPE_ERROR |
                  GNUNET_ERROR_TYPE_BULK, "sqlite3_step");
      sqlite3_finalize (stmt);
      return GNUNET_SYSERR;
    }
  sqlite3_finalize (stmt);
  return GNUNET_OK;
}


/**
 * Context for the universal iterator.
 */
struct NextContext;

/**
 * Type of a function that will prepare
 * the next iteration.
 *
 * @param cls closure
 * @param nc the next context; NULL for the last
 *         call which gives the callback a chance to
 *         clean up the closure
 * @return GNUNET_OK on success, GNUNET_NO if there are
 *        no more values, GNUNET_SYSERR on error
 */
typedef int (*PrepareFunction)(void *cls,
			       struct NextContext *nc);


/**
 * Context we keep for the "next request" callback.
 */
struct NextContext
{
  /**
   * Internal state.
   */ 
  struct Plugin *plugin;

  /**
   * Function to call on the next value.
   */
  PluginIterator iter;

  /**
   * Closure for iter.
   */
  void *iter_cls;

  /**
   * Function to call to prepare the next
   * iteration.
   */
  PrepareFunction prep;

  /**
   * Closure for prep.
   */
  void *prep_cls;

  /**
   * Statement that the iterator will get the data
   * from (updated or set by prep).
   */ 
  sqlite3_stmt *stmt;

  /**
   * Row ID of the last result.
   */
  unsigned long long last_rowid;

  /**
   * Expiration time of the last value visited.
   */
  struct GNUNET_TIME_Absolute lastExpiration;

  /**
   * Priority of the last value visited.
   */ 
  unsigned int lastPriority; 

  /**
   * Number of results processed so far.
   */
  unsigned int count;

  /**
   * Set to GNUNET_YES if we must stop now.
   */
  int end_it;
};


/**
 * Function invoked on behalf of a "PluginIterator"
 * asking the database plugin to call the iterator
 * with the next item.
 *
 * @param next_cls whatever argument was given
 *        to the PluginIterator as "next_cls".
 * @param end_it set to GNUNET_YES if we
 *        should terminate the iteration early
 *        (iterator should be still called once more
 *         to signal the end of the iteration).
 */
static void 
sqlite_next_request (void *next_cls,
		     int end_it)
{
  static struct GNUNET_TIME_Absolute zero;
  struct NextContext * nc= next_cls;
  struct Plugin *plugin = nc->plugin;
  unsigned long long rowid;
  sqlite3_stmt *stmtd;
  int ret;
  unsigned int type;
  unsigned int size;
  unsigned int priority;
  unsigned int anonymity;
  struct GNUNET_TIME_Absolute expiration;
  const GNUNET_HashCode *key;
  const void *data;

  sqlite3_reset (nc->stmt);
  if ( (GNUNET_YES == end_it) ||
       (GNUNET_YES == nc->end_it) ||
       (GNUNET_OK != (nc->prep(nc->prep_cls,
			       nc))) ||
       (SQLITE_ROW != sqlite3_step (nc->stmt)) )
    {
    END:
      nc->iter (nc->iter_cls, 
		NULL, NULL, 0, NULL, 0, 0, 0, 
		zero, 0);
      nc->prep (nc->prep_cls, NULL);
      GNUNET_free (nc);
      return;
    }

  rowid = sqlite3_column_int64 (nc->stmt, 7);
  nc->last_rowid = rowid;
  type = sqlite3_column_int (nc->stmt, 1);
  size = sqlite3_column_bytes (nc->stmt, 6);
  if (sqlite3_column_bytes (nc->stmt, 5) != sizeof (GNUNET_HashCode))
    {
      GNUNET_log_from (GNUNET_ERROR_TYPE_WARNING, 
		       "sqlite",
		       _("Invalid data in database.  Trying to fix (by deletion).\n"));
      if (SQLITE_OK != sqlite3_reset (nc->stmt))
        LOG_SQLITE (nc->plugin, NULL,
                    GNUNET_ERROR_TYPE_ERROR |
                    GNUNET_ERROR_TYPE_BULK, "sqlite3_reset");
      if (sq_prepare
          (nc->plugin->dbh,
           "DELETE FROM gn080 WHERE NOT LENGTH(hash) = ?",
           &stmtd) != SQLITE_OK)
        {
          LOG_SQLITE (nc->plugin, NULL,
                      GNUNET_ERROR_TYPE_ERROR |
                      GNUNET_ERROR_TYPE_BULK, 
		      "sq_prepare");
          goto END;
        }

      if (SQLITE_OK != sqlite3_bind_int (stmtd, 1, sizeof (GNUNET_HashCode)))
        LOG_SQLITE (nc->plugin, NULL,
                    GNUNET_ERROR_TYPE_ERROR |
                    GNUNET_ERROR_TYPE_BULK, "sqlite3_bind_int");
      if (SQLITE_DONE != sqlite3_step (stmtd))
        LOG_SQLITE (nc->plugin, NULL,
                    GNUNET_ERROR_TYPE_ERROR |
                    GNUNET_ERROR_TYPE_BULK, "sqlite3_step");
      if (SQLITE_OK != sqlite3_finalize (stmtd))
        LOG_SQLITE (nc->plugin, NULL,
                    GNUNET_ERROR_TYPE_ERROR |
                    GNUNET_ERROR_TYPE_BULK, "sqlite3_finalize");
      goto END;
    }

  priority = sqlite3_column_int (nc->stmt, 2);
  nc->lastPriority = priority;
  anonymity = sqlite3_column_int (nc->stmt, 3);
  expiration.value = sqlite3_column_int64 (nc->stmt, 4);
  nc->lastExpiration = expiration;
  key = sqlite3_column_blob (nc->stmt, 5);
  data = sqlite3_column_blob (nc->stmt, 6);
  nc->count++;
  ret = nc->iter (nc->iter_cls,
		  nc,
		  key,
		  size,
		  data, 
		  type,
		  priority,
		  anonymity,
		  expiration,
		  rowid);
  if (ret == GNUNET_SYSERR)
    {
      nc->end_it = GNUNET_YES;
      return;
    }
  if ( (ret == GNUNET_NO) &&
       (GNUNET_OK == delete_by_rowid (plugin, rowid)) )
    {
      plugin->payload -= (size + GNUNET_DATASTORE_ENTRY_OVERHEAD);
      plugin->lastSync++; 
      if (plugin->lastSync >= MAX_STAT_SYNC_LAG)
	sync_stats (plugin);
    }
}


/**
 * Store an item in the datastore.
 *
 * @param cls closure
 * @param key key for the item
 * @param size number of bytes in data
 * @param data content stored
 * @param type type of the content
 * @param priority priority of the content
 * @param anonymity anonymity-level for the content
 * @param expiration expiration time for the content
 * @param msg set to an error message
 * @return GNUNET_OK on success
 */
static int
sqlite_plugin_put (void *cls,
		   const GNUNET_HashCode * key,
		   uint32_t size,
		   const void *data,
		   uint32_t type,
		   uint32_t priority,
		   uint32_t anonymity,
		   struct GNUNET_TIME_Absolute expiration,
		   char ** msg)
{
  struct Plugin *plugin = cls;
  int n;
  sqlite3_stmt *stmt;
  GNUNET_HashCode vhash;

#if DEBUG_SQLITE
  GNUNET_log_from (GNUNET_ERROR_TYPE_DEBUG,
		   "sqlite",
		   "Storing in database block with type %u/key `%s'/priority %u/expiration %llu.\n",
		   type, 
		   GNUNET_h2s(key),
		   priority,
		   GNUNET_TIME_absolute_get_remaining (expiration).value);
#endif
  GNUNET_CRYPTO_hash (data, size, &vhash);
  stmt = plugin->insertContent;
  if ((SQLITE_OK != sqlite3_bind_int (stmt, 1, size)) ||
      (SQLITE_OK != sqlite3_bind_int (stmt, 2, type)) ||
      (SQLITE_OK != sqlite3_bind_int (stmt, 3, priority)) ||
      (SQLITE_OK != sqlite3_bind_int (stmt, 4, anonymity)) ||
      (SQLITE_OK != sqlite3_bind_int64 (stmt, 5, expiration.value)) ||
      (SQLITE_OK !=
       sqlite3_bind_blob (stmt, 6, key, sizeof (GNUNET_HashCode),
                          SQLITE_TRANSIENT)) ||
      (SQLITE_OK !=
       sqlite3_bind_blob (stmt, 7, &vhash, sizeof (GNUNET_HashCode),
                          SQLITE_TRANSIENT))
      || (SQLITE_OK !=
          sqlite3_bind_blob (stmt, 8, data, size,
                             SQLITE_TRANSIENT)))
    {
      LOG_SQLITE (plugin,
		  msg,
                  GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK, "sqlite3_bind_XXXX");
      if (SQLITE_OK != sqlite3_reset (stmt))
        LOG_SQLITE (plugin, NULL,
                    GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK, "sqlite3_reset");
      return GNUNET_SYSERR;
    }
  n = sqlite3_step (stmt);
  if (n != SQLITE_DONE)
    {
      if (n == SQLITE_BUSY)
        {
	  LOG_SQLITE (plugin, msg,
		      GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK, "sqlite3_step");
          sqlite3_reset (stmt);
          GNUNET_break (0);
          return GNUNET_NO;
        }
      LOG_SQLITE (plugin, msg,
                  GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK, "sqlite3_step");
      sqlite3_reset (stmt);
      return GNUNET_SYSERR;
    }
  if (SQLITE_OK != sqlite3_reset (stmt))
    LOG_SQLITE (plugin, NULL,
                GNUNET_ERROR_TYPE_ERROR |
                GNUNET_ERROR_TYPE_BULK, "sqlite3_reset");
  plugin->lastSync++;
  plugin->payload += size + GNUNET_DATASTORE_ENTRY_OVERHEAD;
  if (plugin->lastSync >= MAX_STAT_SYNC_LAG)
    sync_stats (plugin);
  return GNUNET_OK;
}


/**
 * Update the priority for a particular key in the datastore.  If
 * the expiration time in value is different than the time found in
 * the datastore, the higher value should be kept.  For the
 * anonymity level, the lower value is to be used.  The specified
 * priority should be added to the existing priority, ignoring the
 * priority in value.
 *
 * Note that it is possible for multiple values to match this put.
 * In that case, all of the respective values are updated.
 *
 * @param uid unique identifier of the datum
 * @param delta by how much should the priority
 *     change?  If priority + delta < 0 the
 *     priority should be set to 0 (never go
 *     negative).
 * @param expire new expiration time should be the
 *     MAX of any existing expiration time and
 *     this value
 * @param msg set to an error message
 * @return GNUNET_OK on success
 */
static int
sqlite_plugin_update (void *cls,
		      uint64_t uid,
		      int delta, struct GNUNET_TIME_Absolute expire,
		      char **msg)
{
  struct Plugin *plugin = cls;
  int n;

  sqlite3_bind_int (plugin->updPrio, 1, delta);
  sqlite3_bind_int64 (plugin->updPrio, 2, expire.value);
  sqlite3_bind_int64 (plugin->updPrio, 3, uid);
  n = sqlite3_step (plugin->updPrio);
  if (n != SQLITE_OK)
    LOG_SQLITE (plugin, msg,
		GNUNET_ERROR_TYPE_WARNING | GNUNET_ERROR_TYPE_BULK,
		"sqlite3_step");
#if DEBUG_SQLITE
  else
    GNUNET_log_from (GNUNET_ERROR_TYPE_DEBUG,
		     "sqlite",
		     "Block updated\n");
#endif
  sqlite3_reset (plugin->updPrio);

  if (n == SQLITE_BUSY)
    return GNUNET_NO;
  return n == SQLITE_OK ? GNUNET_OK : GNUNET_SYSERR;
}


struct IterContext
{
  sqlite3_stmt *stmt_1;
  sqlite3_stmt *stmt_2;
  int is_asc;
  int is_prio;
  int is_migr;
  int limit_nonanonymous;
  uint32_t type;
  GNUNET_HashCode key;  
};


static int
iter_next_prepare (void *cls,
		   struct NextContext *nc)
{
  struct IterContext *ic = cls;
  struct Plugin *plugin = nc->plugin;
  int ret;

  if (nc == NULL)
    {
      sqlite3_finalize (ic->stmt_1);
      sqlite3_finalize (ic->stmt_2);
      return GNUNET_SYSERR;
    }
  if (ic->is_prio)
    {
      sqlite3_bind_int (ic->stmt_1, 1, nc->lastPriority);
      sqlite3_bind_int (ic->stmt_2, 1, nc->lastPriority);
    }
  else
    {
      sqlite3_bind_int64 (ic->stmt_1, 1, nc->lastExpiration.value);
      sqlite3_bind_int64 (ic->stmt_2, 1, nc->lastExpiration.value);
    }
  sqlite3_bind_blob (ic->stmt_1, 2, 
		     &ic->key, 
		     sizeof (GNUNET_HashCode),
		     SQLITE_TRANSIENT);
  if (SQLITE_ROW == (ret = sqlite3_step (ic->stmt_1)))
    {
      nc->stmt = ic->stmt_1;
      return GNUNET_OK;
    }
  if (ret != SQLITE_DONE)
    {
      LOG_SQLITE (plugin, NULL,
		  GNUNET_ERROR_TYPE_ERROR |
		  GNUNET_ERROR_TYPE_BULK,
		  "sqlite3_step");
      return GNUNET_SYSERR;
    }
  if (SQLITE_OK != sqlite3_reset (ic->stmt_1))
    LOG_SQLITE (plugin, NULL,
		GNUNET_ERROR_TYPE_ERROR | 
		GNUNET_ERROR_TYPE_BULK, 
		"sqlite3_reset");
  if (SQLITE_ROW == (ret = sqlite3_step (ic->stmt_2))) 
    {
      nc->stmt = ic->stmt_2;
      return GNUNET_OK;
    }
  if (ret != SQLITE_DONE)
    {
      LOG_SQLITE (plugin, NULL,
		  GNUNET_ERROR_TYPE_ERROR |
		  GNUNET_ERROR_TYPE_BULK,
		  "sqlite3_step");
      return GNUNET_SYSERR;
    }
  if (SQLITE_OK != sqlite3_reset (ic->stmt_2))
    LOG_SQLITE (plugin, NULL,
		GNUNET_ERROR_TYPE_ERROR |
		GNUNET_ERROR_TYPE_BULK,
		"sqlite3_reset");
  return GNUNET_NO;
}


/**
 * Call a method for each key in the database and
 * call the callback method on it.
 *
 * @param type entries of which type should be considered?
 * @param iter function to call on each matching value;
 *        will be called once with a NULL value at the end
 * @param iter_cls closure for iter
 * @return the number of results processed,
 *         GNUNET_SYSERR on error
 */
static void
basic_iter (struct Plugin *plugin,
	    uint32_t type,
	    int is_asc,
	    int is_prio,
	    int is_migr,
	    int limit_nonanonymous,
	    const char *stmt_str_1,
	    const char *stmt_str_2,
	    PluginIterator iter,
	    void *iter_cls)
{
  static struct GNUNET_TIME_Absolute zero;
  struct NextContext *nc;
  struct IterContext *ic;
  sqlite3_stmt *stmt_1;
  sqlite3_stmt *stmt_2;

  if (sq_prepare (plugin->dbh, stmt_str_1, &stmt_1) != SQLITE_OK)
    {
      LOG_SQLITE (plugin, NULL,
                  GNUNET_ERROR_TYPE_ERROR |
                  GNUNET_ERROR_TYPE_BULK, "sqlite3_prepare");
      iter (iter_cls, NULL, NULL, 0, NULL, 0, 0, 0, zero, 0);
      return;
    }
  if (sq_prepare (plugin->dbh, stmt_str_2, &stmt_2) != SQLITE_OK)
    {
      LOG_SQLITE (plugin, NULL,
                  GNUNET_ERROR_TYPE_ERROR |
                  GNUNET_ERROR_TYPE_BULK, "sqlite3_prepare");
      sqlite3_finalize (stmt_1);
      iter (iter_cls, NULL, NULL, 0, NULL, 0, 0, 0, zero, 0);
      return;
    }
  nc = GNUNET_malloc (sizeof(struct NextContext) + 
		      sizeof(struct IterContext));
  nc->plugin = plugin;
  nc->iter = iter;
  nc->iter_cls = iter_cls;
  nc->stmt = NULL;
  ic = (struct IterContext*) &nc[1];
  ic->stmt_1 = stmt_1;
  ic->stmt_2 = stmt_2;
  ic->type = type;
  ic->is_asc = is_asc;
  ic->is_prio = is_prio;
  ic->is_migr = is_migr;
  ic->limit_nonanonymous = limit_nonanonymous;
  nc->prep = &iter_next_prepare;
  nc->prep_cls = ic;
  if (is_asc)
    {
      nc->lastPriority = 0;
      nc->lastExpiration.value = 0;
      memset (&ic->key, 0, sizeof (GNUNET_HashCode));
    }
  else
    {
      nc->lastPriority = 0x7FFFFFFF;
      nc->lastExpiration.value = 0x7FFFFFFFFFFFFFFFLL;
      memset (&ic->key, 255, sizeof (GNUNET_HashCode));
    }
  sqlite_next_request (nc, GNUNET_NO);
}


/**
 * Select a subset of the items in the datastore and call
 * the given iterator for each of them.
 *
 * @param type entries of which type should be considered?
 *        Use 0 for any type.
 * @param iter function to call on each matching value;
 *        will be called once with a NULL value at the end
 * @param iter_cls closure for iter
 */
static void
sqlite_plugin_iter_low_priority (void *cls,
				 uint32_t type,
				 PluginIterator iter,
				 void *iter_cls)
{
  basic_iter (cls,
	      type, 
	      GNUNET_YES, GNUNET_YES, 
	      GNUNET_NO, GNUNET_NO,
	      SELECT_IT_LOW_PRIORITY_1,
	      SELECT_IT_LOW_PRIORITY_2, 
	      iter, iter_cls);
}


/**
 * Select a subset of the items in the datastore and call
 * the given iterator for each of them.
 *
 * @param type entries of which type should be considered?
 *        Use 0 for any type.
 * @param iter function to call on each matching value;
 *        will be called once with a NULL value at the end
 * @param iter_cls closure for iter
 */
static void
sqlite_plugin_iter_zero_anonymity (void *cls,
				   uint32_t type,
				   PluginIterator iter,
				   void *iter_cls)
{
  basic_iter (cls,
	      type, 
	      GNUNET_NO, GNUNET_YES, 
	      GNUNET_NO, GNUNET_YES,
	      SELECT_IT_NON_ANONYMOUS_1,
	      SELECT_IT_NON_ANONYMOUS_2, 
	      iter, iter_cls);
}



/**
 * Select a subset of the items in the datastore and call
 * the given iterator for each of them.
 *
 * @param type entries of which type should be considered?
 *        Use 0 for any type.
 * @param iter function to call on each matching value;
 *        will be called once with a NULL value at the end
 * @param iter_cls closure for iter
 */
static void
sqlite_plugin_iter_ascending_expiration (void *cls,
					 uint32_t type,
					 PluginIterator iter,
					 void *iter_cls)
{
  struct GNUNET_TIME_Absolute now;
  char *q1;
  char *q2;

  now = GNUNET_TIME_absolute_get ();
  GNUNET_asprintf (&q1, SELECT_IT_EXPIRATION_TIME_1,
		   now.value);
  GNUNET_asprintf (&q2, SELECT_IT_EXPIRATION_TIME_2,
		   now.value);
  basic_iter (cls,
	      type, 
	      GNUNET_YES, GNUNET_NO, 
	      GNUNET_NO, GNUNET_NO,
	      q1, q2,
	      iter, iter_cls);
  GNUNET_free (q1);
  GNUNET_free (q2);
}


/**
 * Select a subset of the items in the datastore and call
 * the given iterator for each of them.
 *
 * @param type entries of which type should be considered?
 *        Use 0 for any type.
 * @param iter function to call on each matching value;
 *        will be called once with a NULL value at the end
 * @param iter_cls closure for iter
 */
static void
sqlite_plugin_iter_migration_order (void *cls,
				    uint32_t type,
				    PluginIterator iter,
				    void *iter_cls)
{
  struct GNUNET_TIME_Absolute now;
  char *q;

  now = GNUNET_TIME_absolute_get ();
  GNUNET_asprintf (&q, SELECT_IT_MIGRATION_ORDER_2,
		   now.value);
  basic_iter (cls,
	      type, 
	      GNUNET_NO, GNUNET_NO, 
	      GNUNET_YES, GNUNET_NO,
	      SELECT_IT_MIGRATION_ORDER_1,
	      q,
	      iter, iter_cls);
  GNUNET_free (q);
}



/**
 * Select a subset of the items in the datastore and call
 * the given iterator for each of them.
 *
 * @param type entries of which type should be considered?
 *        Use 0 for any type.
 * @param iter function to call on each matching value;
 *        will be called once with a NULL value at the end
 * @param iter_cls closure for iter
 */
static void
sqlite_plugin_iter_all_now (void *cls,
			    uint32_t type,
			    PluginIterator iter,
			    void *iter_cls)
{
  static struct GNUNET_TIME_Absolute zero;
  iter (iter_cls, NULL, NULL, 0, NULL, 0, 0, 0, zero, 0);
}


struct GetNextContext
{
  int total;
  int off;
  int have_vhash;
  unsigned int type;
  sqlite3_stmt *stmt;
  GNUNET_HashCode key;
  GNUNET_HashCode vhash;
};


static int
get_next_prepare (void *cls,
		  struct NextContext *nc)
{
  struct GetNextContext *gnc = cls;
  int sqoff;
  int ret;
  int limit_off;

  if (nc == NULL)
    {
      sqlite3_finalize (gnc->stmt);
      return GNUNET_SYSERR;
    }
  if (nc->count == gnc->total)
    return GNUNET_NO;
  if (nc->count + gnc->off == gnc->total)
    nc->last_rowid = 0;
  if (nc->count == 0)
    limit_off = gnc->off;
  else
    limit_off = 0;
  sqoff = 1;
  ret = sqlite3_bind_blob (nc->stmt,
			   sqoff++,
			   &gnc->key, 
			   sizeof (GNUNET_HashCode),
			   SQLITE_TRANSIENT);
  if ((gnc->have_vhash) && (ret == SQLITE_OK))
    ret = sqlite3_bind_blob (nc->stmt,
			     sqoff++,
			     &gnc->vhash,
			     sizeof (GNUNET_HashCode), SQLITE_TRANSIENT);
  if ((gnc->type != 0) && (ret == SQLITE_OK))
    ret = sqlite3_bind_int (nc->stmt, sqoff++, gnc->type);
  if (ret == SQLITE_OK)
    ret = sqlite3_bind_int64 (nc->stmt, sqoff++, nc->last_rowid + 1);
  if (ret == SQLITE_OK)
    ret = sqlite3_bind_int (nc->stmt, sqoff++, limit_off);
  if (ret != SQLITE_OK)
    return GNUNET_SYSERR;
  if (SQLITE_ROW != sqlite3_step (nc->stmt))
    return GNUNET_NO;
  return GNUNET_OK;
}


/**
 * Iterate over the results for a particular key
 * in the datastore.
 *
 * @param cls closure
 * @param key maybe NULL (to match all entries)
 * @param vhash hash of the value, maybe NULL (to
 *        match all values that have the right key).
 *        Note that for DBlocks there is no difference
 *        betwen key and vhash, but for other blocks
 *        there may be!
 * @param type entries of which type are relevant?
 *     Use 0 for any type.
 * @param iter function to call on each matching value;
 *        will be called once with a NULL value at the end
 * @param iter_cls closure for iter
 */
static void
sqlite_plugin_get (void *cls,
		   const GNUNET_HashCode * key,
		   const GNUNET_HashCode * vhash,
		   uint32_t type,
		   PluginIterator iter, void *iter_cls)
{
  static struct GNUNET_TIME_Absolute zero;
  struct Plugin *plugin = cls;
  struct GetNextContext *gpc;
  struct NextContext *nc;
  int ret;
  int total;
  sqlite3_stmt *stmt;
  char scratch[256];
  int sqoff;

  GNUNET_assert (iter != NULL);
  if (key == NULL)
    {
      sqlite_plugin_iter_low_priority (cls, type, iter, iter_cls);
      return;
    }
  GNUNET_snprintf (scratch, 256,
                   "SELECT count(*) FROM gn080 WHERE hash=:1%s%s",
                   vhash == NULL ? "" : " AND vhash=:2",
                   type == 0 ? "" : (vhash ==
                                     NULL) ? " AND type=:2" : " AND type=:3");
  if (sq_prepare (plugin->dbh, scratch, &stmt) != SQLITE_OK)
    {
      LOG_SQLITE (plugin, NULL,
                  GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK, "sqlite_prepare");
      iter (iter_cls, NULL, NULL, 0, NULL, 0, 0, 0, zero, 0);
      return;
    }
  sqoff = 1;
  ret = sqlite3_bind_blob (stmt,
                           sqoff++,
                           key, sizeof (GNUNET_HashCode), SQLITE_TRANSIENT);
  if ((vhash != NULL) && (ret == SQLITE_OK))
    ret = sqlite3_bind_blob (stmt,
                             sqoff++,
                             vhash,
                             sizeof (GNUNET_HashCode), SQLITE_TRANSIENT);
  if ((type != 0) && (ret == SQLITE_OK))
    ret = sqlite3_bind_int (stmt, sqoff++, type);
  if (SQLITE_OK != ret)
    {
      LOG_SQLITE (plugin, NULL,
                  GNUNET_ERROR_TYPE_ERROR, "sqlite_bind");
      sqlite3_reset (stmt);
      sqlite3_finalize (stmt);
      iter (iter_cls, NULL, NULL, 0, NULL, 0, 0, 0, zero, 0);
      return;
    }
  ret = sqlite3_step (stmt);
  if (ret != SQLITE_ROW)
    {
      LOG_SQLITE (plugin, NULL,
                  GNUNET_ERROR_TYPE_ERROR| GNUNET_ERROR_TYPE_BULK, 
		  "sqlite_step");
      sqlite3_reset (stmt);
      sqlite3_finalize (stmt);
      iter (iter_cls, NULL, NULL, 0, NULL, 0, 0, 0, zero, 0);
      return;
    }
  total = sqlite3_column_int (stmt, 0);
  sqlite3_reset (stmt);
  sqlite3_finalize (stmt);
  if (0 == total)
    {
      iter (iter_cls, NULL, NULL, 0, NULL, 0, 0, 0, zero, 0);
      return;
    }

  GNUNET_snprintf (scratch, 256,
                   "SELECT size, type, prio, anonLevel, expire, hash, value, _ROWID_ "
                   "FROM gn080 WHERE hash=:1%s%s AND _ROWID_ >= :%d "
                   "ORDER BY _ROWID_ ASC LIMIT 1 OFFSET :d",
                   vhash == NULL ? "" : " AND vhash=:2",
                   type == 0 ? "" : (vhash ==
                                     NULL) ? " AND type=:2" : " AND type=:3",
                   sqoff, sqoff + 1);
  if (sq_prepare (plugin->dbh, scratch, &stmt) != SQLITE_OK)
    {
      LOG_SQLITE (plugin, NULL,
                  GNUNET_ERROR_TYPE_ERROR |
                  GNUNET_ERROR_TYPE_BULK, "sqlite_prepare");
      iter (iter_cls, NULL, NULL, 0, NULL, 0, 0, 0, zero, 0);
      return;
    }
  nc = GNUNET_malloc (sizeof(struct NextContext) + 
		      sizeof(struct GetNextContext));
  nc->plugin = plugin;
  nc->iter = iter;
  nc->iter_cls = iter_cls;
  nc->stmt = stmt;
  gpc = (struct GetNextContext*) &nc[1];
  gpc->total = total;
  gpc->type = type;
  gpc->key = *key;
  gpc->stmt = stmt; /* alias used for freeing at the end! */
  if (NULL != vhash)
    {
      gpc->have_vhash = GNUNET_YES;
      gpc->vhash = *vhash;
    }
  gpc->off = GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, total);
  nc->prep = &get_next_prepare;
  nc->prep_cls = gpc;
  sqlite_next_request (nc, GNUNET_NO);
}


/**
 * Drop database.
 */
static void 
sqlite_plugin_drop (void *cls)
{
  struct Plugin *plugin = cls;
  plugin->drop_on_shutdown = GNUNET_YES;
}


/**
 * Callback function to process statistic values.
 *
 * @param cls closure
 * @param subsystem name of subsystem that created the statistic
 * @param name the name of the datum
 * @param value the current value
 * @param is_persistent GNUNET_YES if the value is persistent, GNUNET_NO if not
 * @return GNUNET_OK to continue, GNUNET_SYSERR to abort iteration
 */
static int
process_stat_in (void *cls,
		 const char *subsystem,
		 const char *name,
		 unsigned long long value,
		 int is_persistent)
{
  struct Plugin *plugin = cls;
  plugin->payload += value;
  return GNUNET_OK;
}
			 		 

/**
 * Entry point for the plugin.
 */
void *
libgnunet_plugin_datastore_sqlite_init (void *cls)
{
  static struct Plugin plugin;
  struct GNUNET_DATASTORE_PluginEnvironment *env = cls;
  struct GNUNET_DATASTORE_PluginFunctions *api;

  if (plugin.env != NULL)
    return NULL; /* can only initialize once! */
  memset (&plugin, 0, sizeof(struct Plugin));
  plugin.env = env;
  plugin.statistics = GNUNET_STATISTICS_create (env->sched,
						"sqlite",
						env->cfg);
  GNUNET_STATISTICS_get (plugin.statistics,
			 "sqlite",
			 QUOTA_STAT_NAME,
			 GNUNET_TIME_UNIT_MINUTES,
			 NULL,
			 &process_stat_in,
			 &plugin);
  if (GNUNET_OK !=
      database_setup (env->cfg, &plugin))
    {
      database_shutdown (&plugin);
      return NULL;
    }
  api = GNUNET_malloc (sizeof (struct GNUNET_DATASTORE_PluginFunctions));
  api->cls = &plugin;
  api->get_size = &sqlite_plugin_get_size;
  api->put = &sqlite_plugin_put;
  api->next_request = &sqlite_next_request;
  api->get = &sqlite_plugin_get;
  api->update = &sqlite_plugin_update;
  api->iter_low_priority = &sqlite_plugin_iter_low_priority;
  api->iter_zero_anonymity = &sqlite_plugin_iter_zero_anonymity;
  api->iter_ascending_expiration = &sqlite_plugin_iter_ascending_expiration;
  api->iter_migration_order = &sqlite_plugin_iter_migration_order;
  api->iter_all_now = &sqlite_plugin_iter_all_now;
  api->drop = &sqlite_plugin_drop;
  GNUNET_log_from (GNUNET_ERROR_TYPE_INFO,
                   "sqlite", _("Sqlite database running\n"));
  return api;
}


/**
 * Exit point from the plugin.
 */
void *
libgnunet_plugin_datastore_sqlite_done (void *cls)
{
  char *fn;
  struct GNUNET_DATASTORE_PluginFunctions *api = cls;
  struct Plugin *plugin = api->cls;

  fn = NULL;
  if (plugin->drop_on_shutdown)
    fn = GNUNET_strdup (plugin->fn);
  database_shutdown (plugin);
  plugin->env = NULL; 
  plugin->payload = 0;
  GNUNET_free (api);
  if (fn != NULL)
    {
      if (0 != UNLINK(fn))
	GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_WARNING,
				  "unlink",
				  fn);
      GNUNET_free (fn);
    }
  return NULL;
}

/* end of plugin_datastore_sqlite.c */
