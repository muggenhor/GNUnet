/*
     This file is part of GNUnet
     (C) 2006, 2009 Christian Grothoff (and other contributing authors)

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
 * @file datacache/plugin_datacache_sqlite.c
 * @brief sqlite for an implementation of a database backend for the datacache
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "plugin_datacache.h"
#include <sqlite3.h>

#define DEBUG_DATACACHE_SQLITE GNUNET_NO

/**
 * How much overhead do we assume per entry in the
 * datacache?
 */
#define OVERHEAD (sizeof(GNUNET_HashCode) + 32)

/**
 * Context for all functions in this plugin.
 */
struct Plugin 
{
  /**
   * Our execution environment.
   */
  struct GNUNET_DATACACHE_PluginEnvironment *env;

  /**
   * Handle to the sqlite database.
   */
  sqlite3 *dbh;

  /**
   * Filename used for the DB.
   */ 
  char *fn;
};


/**
 * Log an error message at log-level 'level' that indicates
 * a failure of the command 'cmd' on file 'filename'
 * with the message given by strerror(errno).
 */
#define LOG_SQLITE(db, level, cmd) do { GNUNET_log(level, _("`%s' failed at %s:%d with error: %s\n"), cmd, __FILE__, __LINE__, sqlite3_errmsg(db)); } while(0)


#define SQLITE3_EXEC(db, cmd) do { emsg = NULL; if (SQLITE_OK != sqlite3_exec(db, cmd, NULL, NULL, &emsg)) { GNUNET_log(GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK, _("`%s' failed at %s:%d with error: %s\n"), "sqlite3_exec", __FILE__, __LINE__, emsg); sqlite3_free(emsg); } } while(0)


/**
 * @brief Prepare a SQL statement
 */
static int
sq_prepare (sqlite3 * dbh, const char *zSql,    /* SQL statement, UTF-8 encoded */
            sqlite3_stmt ** ppStmt)
{                               /* OUT: Statement handle */
  char *dummy;
  return sqlite3_prepare (dbh,
                          zSql,
                          strlen (zSql), ppStmt, (const char **) &dummy);
}


/**
 * Store an item in the datastore.
 *
 * @param key key to store data under
 * @param size number of bytes in data
 * @param data data to store
 * @param type type of the value
 * @param discard_time when to discard the value in any case
 * @return 0 on error, number of bytes used otherwise
 */
static uint32_t 
sqlite_plugin_put (void *cls,
		   const GNUNET_HashCode * key,
		   uint32_t size,
		   const char *data,
		   uint32_t type,
		   struct GNUNET_TIME_Absolute discard_time)
{
  struct Plugin *plugin = cls;
  sqlite3_stmt *stmt;

#if DEBUG_DATACACHE_SQLITE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Processing `%s' of %u bytes with key `%4s' and expiration %llums\n",
	      "PUT",
	      (unsigned int) size,
	      GNUNET_h2s (key),
	      (unsigned long long) GNUNET_TIME_absolute_get_remaining (discard_time).value);
#endif
  if (sq_prepare (plugin->dbh,
                  "INSERT INTO ds090 "
                  "(type, expire, key, value) "
                  "VALUES (?, ?, ?, ?)", &stmt) != SQLITE_OK)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK,
		  _("`%s' failed at %s:%d with error: %s\n"),
		  "sq_prepare", __FILE__, __LINE__, 
		  sqlite3_errmsg (plugin->dbh));
      return 0;
    }
  if ( (SQLITE_OK != sqlite3_bind_int (stmt, 1, type)) ||
       (SQLITE_OK != sqlite3_bind_int64 (stmt, 2, discard_time.value)) ||
       (SQLITE_OK != sqlite3_bind_blob (stmt, 3, key, sizeof (GNUNET_HashCode),
					SQLITE_TRANSIENT)) ||
       (SQLITE_OK != sqlite3_bind_blob (stmt, 4, data, size, SQLITE_TRANSIENT)))
    {
      LOG_SQLITE (plugin->dbh,
                  GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK, 
		  "sqlite3_bind_xxx");
      sqlite3_finalize (stmt);
      return 0;
    }
  if (SQLITE_DONE != sqlite3_step (stmt))
    {
      LOG_SQLITE (plugin->dbh,
		  GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK, 
		  "sqlite3_step");
      sqlite3_finalize (stmt);
      return 0;
    }
  if (SQLITE_OK != sqlite3_finalize (stmt))
    LOG_SQLITE (plugin->dbh,
		GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK, 
		"sqlite3_finalize");
  return size + OVERHEAD;
}


/**
 * Iterate over the results for a particular key
 * in the datastore.
 *
 * @param key
 * @param type entries of which type are relevant?
 * @param iter maybe NULL (to just count)
 * @return the number of results found
 */
static unsigned int 
sqlite_plugin_get (void *cls,
		   const GNUNET_HashCode * key,
		   uint32_t type,
		   GNUNET_DATACACHE_Iterator iter,
		   void *iter_cls)
{
  struct Plugin *plugin = cls;
  sqlite3_stmt *stmt;
  struct GNUNET_TIME_Absolute now;
  struct GNUNET_TIME_Absolute exp;
  unsigned int size;
  const char *dat;
  unsigned int cnt;
  unsigned int off;
  unsigned int total;
  char scratch[256];

  now = GNUNET_TIME_absolute_get ();
#if DEBUG_DATACACHE_SQLITE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Processing `%s' for key `%4s'\n",
	      "GET",
	      GNUNET_h2s (key));
#endif
  if (sq_prepare (plugin->dbh,
                  "SELECT count(*) FROM ds090 WHERE key=? AND type=? AND expire >= ?",
                  &stmt) != SQLITE_OK)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK,
		  _("`%s' failed at %s:%d with error: %s\n"),
		  "sq_prepare", __FILE__, __LINE__, 
		  sqlite3_errmsg (plugin->dbh));
      return 0;
    }
  sqlite3_bind_blob (stmt, 1, key, sizeof (GNUNET_HashCode),
                     SQLITE_TRANSIENT);
  sqlite3_bind_int (stmt, 2, type);
  sqlite3_bind_int64 (stmt, 3, now.value);
  if (SQLITE_ROW != sqlite3_step (stmt))
    {
      LOG_SQLITE (plugin->dbh,
                  GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK, 
		  "sqlite_step");
      sqlite3_finalize (stmt);
      return 0;
    }
  total = sqlite3_column_int (stmt, 0);
  sqlite3_finalize (stmt);
  if ( (total == 0) || (iter == NULL) )
    return total;    

  cnt = 0;
  off = GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, total);
  while (cnt < total)
    {
      off = (off + 1) % total;
      GNUNET_snprintf (scratch, 
		       sizeof(scratch),
                       "SELECT value,expire FROM ds090 WHERE key=? AND type=? AND expire >= ? LIMIT 1 OFFSET %u",
                       off);
      if (sq_prepare (plugin->dbh, scratch, &stmt) != SQLITE_OK)
        {
          GNUNET_log (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK,
		      _("`%s' failed at %s:%d with error: %s\n"),
		      "sq_prepare", __FILE__, __LINE__,
		      sqlite3_errmsg (plugin->dbh));
          return cnt;
        }
      sqlite3_bind_blob (stmt, 1, key, sizeof (GNUNET_HashCode),
                         SQLITE_TRANSIENT);
      sqlite3_bind_int (stmt, 2, type);
      sqlite3_bind_int64 (stmt, 3, now.value);
      if (sqlite3_step (stmt) != SQLITE_ROW)
        break;
      size = sqlite3_column_bytes (stmt, 0);
      dat = sqlite3_column_blob (stmt, 0);
      exp.value = sqlite3_column_int64 (stmt, 1);
      cnt++;
      if (GNUNET_OK != iter (iter_cls,
			     exp,
			     key, 
			     size,
			     dat,
			     type))
        {
          sqlite3_finalize (stmt);
          break;
        }
      sqlite3_finalize (stmt);
    }
  return cnt;
}


/**
 * Delete the entry with the lowest expiration value
 * from the datacache right now.
 * 
 * @return GNUNET_OK on success, GNUNET_SYSERR on error
 */ 
static int 
sqlite_plugin_del (void *cls)
{
  struct Plugin *plugin = cls;
  unsigned int dsize;
  unsigned int dtype;
  sqlite3_stmt *stmt;
  sqlite3_stmt *dstmt;

#if DEBUG_DATACACHE_SQLITE
  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
	      "Processing `%s'\n",
	      "DEL");
#endif
  stmt = NULL;
  dstmt = NULL;
  if ((sq_prepare (plugin->dbh,
                   "SELECT type, key, value FROM ds090 ORDER BY expire ASC LIMIT 1",
                   &stmt) != SQLITE_OK) ||
      (sq_prepare (plugin->dbh,
                   "DELETE FROM ds090 "
                   "WHERE key=? AND value=? AND type=?",
                   &dstmt) != SQLITE_OK))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK,
		  _("`%s' failed at %s:%d with error: %s\n"),
		  "sq_prepare", __FILE__, __LINE__, sqlite3_errmsg (plugin->dbh));
      if (dstmt != NULL)
        sqlite3_finalize (dstmt);
      if (stmt != NULL)
        sqlite3_finalize (stmt);
      return GNUNET_SYSERR;
    }
  if (SQLITE_ROW != sqlite3_step (stmt))
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK,
		  _("`%s' failed at %s:%d with error: %s\n"),
		  "sqlite3_step", __FILE__, __LINE__,
		  sqlite3_errmsg (plugin->dbh));
      sqlite3_finalize (dstmt);
      sqlite3_finalize (stmt);
      return GNUNET_SYSERR;
    }
  dtype = sqlite3_column_int (stmt, 0);
  GNUNET_break (sqlite3_column_bytes (stmt, 1) == sizeof (GNUNET_HashCode));
  dsize = sqlite3_column_bytes (stmt, 2);
  sqlite3_bind_blob (dstmt,
		     1, sqlite3_column_blob (stmt, 1),
		     sizeof (GNUNET_HashCode),
		     SQLITE_TRANSIENT);
  sqlite3_bind_blob (dstmt,
		     2, sqlite3_column_blob (stmt, 2),
		     dsize,
		     SQLITE_TRANSIENT);
  sqlite3_bind_int (dstmt, 3, dtype);
  if (sqlite3_step (dstmt) != SQLITE_DONE)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK,
		  _("`%s' failed at %s:%d with error: %s\n"),
		  "sqlite3_step", __FILE__, __LINE__,
		  sqlite3_errmsg (plugin->dbh));    
      sqlite3_finalize (dstmt);
      sqlite3_finalize (stmt);
      return GNUNET_SYSERR;
    }
  plugin->env->delete_notify (plugin->env->cls,
			      sqlite3_column_blob (stmt, 1),
			      dsize + OVERHEAD);
  sqlite3_finalize (dstmt);
  sqlite3_finalize (stmt);
  return GNUNET_OK;
}


/**
 * Entry point for the plugin.
 */
void *
libgnunet_plugin_datacache_sqlite_init (void *cls)
{
  struct GNUNET_DATACACHE_PluginEnvironment *env = cls;
  struct GNUNET_DATACACHE_PluginFunctions *api;
  struct Plugin *plugin;
  char *fn;
  char *fn_utf8;
  int fd;
  sqlite3 *dbh;
  char *tmpl;
  const char *tmpdir;
  char *emsg;

  tmpdir = getenv ("TMPDIR");
  tmpdir = tmpdir ? tmpdir : "/tmp";

#define TEMPLATE "/gnunet-dstoreXXXXXX"
  tmpl = GNUNET_malloc (strlen (tmpdir) + sizeof (TEMPLATE) + 1);
  strcpy (tmpl, tmpdir);
  strcat (tmpl, TEMPLATE);
#undef TEMPLATE
#ifdef MINGW
  fn = (char *) GNUNET_malloc (MAX_PATH + 1);
  plibc_conv_to_win_path (tmpl, fn);
  GNUNET_free (tmpl);
#else
  fn = tmpl;
#endif
  fd = mkstemp (fn);
  if (fd == -1)
    {
      GNUNET_break (0);
      GNUNET_free (fn);
      return NULL;
    }
  CLOSE (fd);
  fn_utf8 = GNUNET_STRINGS_to_utf8 (fn, strlen (fn),
#ifdef ENABLE_NLS
				    nl_langinfo (CODESET)
#else
				    "UTF-8"      /* good luck */
#endif
    );
  if (SQLITE_OK != sqlite3_open (fn_utf8, &dbh))
    {
      GNUNET_free (fn);
      GNUNET_free (fn_utf8);
      return NULL;
    }
  GNUNET_free (fn);

  SQLITE3_EXEC (dbh, "PRAGMA temp_store=MEMORY");
  SQLITE3_EXEC (dbh, "PRAGMA synchronous=OFF");
  SQLITE3_EXEC (dbh, "PRAGMA count_changes=OFF");
  SQLITE3_EXEC (dbh, "PRAGMA page_size=4092");
  SQLITE3_EXEC (dbh,
                "CREATE TABLE ds090 ("
                "  type INTEGER NOT NULL DEFAULT 0,"
                "  expire INTEGER NOT NULL DEFAULT 0,"
                "  key BLOB NOT NULL DEFAULT '',"
                "  value BLOB NOT NULL DEFAULT '')");
  SQLITE3_EXEC (dbh, "CREATE INDEX idx_hashidx ON ds090 (key,type,expire)");
  plugin = GNUNET_malloc (sizeof (struct Plugin));
  plugin->env = env;
  plugin->dbh = dbh;
  plugin->fn = fn_utf8;
  api = GNUNET_malloc (sizeof (struct GNUNET_DATACACHE_PluginFunctions));
  api->cls = plugin;
  api->get = &sqlite_plugin_get;
  api->put = &sqlite_plugin_put;
  api->del = &sqlite_plugin_del;
  GNUNET_log_from (GNUNET_ERROR_TYPE_INFO,
                   "sqlite", _("Sqlite datacache running\n"));
  return api;
}


/**
 * Exit point from the plugin.
 */
void *
libgnunet_plugin_datacache_sqlite_done (void *cls)
{
  struct GNUNET_DATACACHE_PluginFunctions *api = cls;
  struct Plugin *plugin = api->cls;

  UNLINK (plugin->fn);
  GNUNET_free (plugin->fn);
  sqlite3_close (plugin->dbh);
  GNUNET_free (plugin);
  GNUNET_free (api);
  return NULL;
}



/* end of plugin_datacache_sqlite.c */

