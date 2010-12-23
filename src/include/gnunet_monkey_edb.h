/*
      This file is part of GNUnet
      (C) 2009, 2010 Christian Grothoff (and other contributing authors)

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
      Free Software Foundation, Inc., 59 Temple Place - Suite 330,
      Boston, MA 02111-1307, USA.
 */

/**
 * @file include/gnunet_monkey_edb.h
 * @brief Monkey API for accessing the Expression Database (edb)
 */

#ifndef GNUNET_MONKEY_EDB_H
#define GNUNET_MONKEY_EDB_H

#ifdef __cplusplus
extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif


struct GNUNET_MONKEY_EDB_Context;

/**
 * Establish a connection to the Expression Database
 *
 * @param db_file_name path the Expression Database file
 * @return context to use for Accessing the Expression Database, NULL on error
 */
struct GNUNET_MONKEY_EDB_Context *GNUNET_MONKEY_EDB_connect (const char
							     *db_file_name);


/**
 * Disconnect from Database, and cleanup resources
 *
 * @param context context
 * @return GNUNET_OK on success, GNUNET_NO on failure
 */
int GNUNET_MONKEY_EDB_disconnect (struct GNUNET_MONKEY_EDB_Context *context);


typedef int (*GNUNET_MONKEY_ExpressionIterator)(void *cls, const char *expression, int begin_line);


/**
 * Update the context with a list of expressions. 
 * The list is the initializations of sub-expressions 
 * of the expression pointed to by start_line_no and end_line_no
 * For example, consider the following code snippet:
 * {
 *   struct Something whole; // line no.1 
 *   struct SomethingElse part; // line no.2
 *   whole.part = &part; // line no.3
 *   whole.part->member = 1; // line no.4
 * }
 * If the expression supplied to the function is that of line no.4 "whole.part->member = 1;"
 * The returned list of expressions will be: whole.part = part (line no.3), 
 * struct SomethingElse part (line no.2), and struct Something whole (line no.1),
 * which are the initialization expressions of the building components of the expression in question
 * 
 * @param context the returned expessions will be available in it. 
 * expression_list_head, and expression_list_tail must be null, 
 * otherwise GNUNET_NO will be returned 
 * @param file_name path to the file in which the expression in question exists
 * @param start_line_no expression beginning line
 * @param end_line_no expression end line
 * @param iter callback function, iterator for expressions returned from the Database
 * @param iter_cls closure for the expression iterator
 * @return GNUNET_OK success, GNUNET_NO failure
 */
int
GNUNET_MONKEY_EDB_get_expressions (struct GNUNET_MONKEY_EDB_Context *context,
				   const char *file_name, int start_line_no,
				   int end_line_no,
				   GNUNET_MONKEY_ExpressionIterator iter, void *iter_cls);



#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif

#endif

