/*
     This file is part of GNUnet
     (C) 2012 Christian Grothoff (and other contributing authors)

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
 * @file src/regex/regex_graph.c
 * @brief functions for creating .dot graphs from regexes
 * @author Maximilian Szengel
 */
#include "platform.h"
#include "gnunet_regex_lib.h"
#include "regex_internal.h"

/**
 * Recursive function doing DFS with 'v' as a start, detecting all SCCs inside
 * the subgraph reachable from 'v'. Used with scc_tarjan function to detect all
 * SCCs inside an automaton.
 *
 * @param scc_counter counter for numbering the sccs
 * @param v start vertex
 * @param index current index
 * @param stack stack for saving all SCCs
 * @param stack_size current size of the stack
 */
static void
scc_tarjan_strongconnect (unsigned int *scc_counter,
                          struct GNUNET_REGEX_State *v, unsigned int *index,
                          struct GNUNET_REGEX_State **stack,
                          unsigned int *stack_size)
{
  struct GNUNET_REGEX_State *w;
  struct GNUNET_REGEX_Transition *t;

  v->index = *index;
  v->lowlink = *index;
  (*index)++;
  stack[(*stack_size)++] = v;
  v->contained = 1;

  for (t = v->transitions_head; NULL != t; t = t->next)
  {
    w = t->to_state;
    if (NULL != w && w->index < 0)
    {
      scc_tarjan_strongconnect (scc_counter, w, index, stack, stack_size);
      v->lowlink = (v->lowlink > w->lowlink) ? w->lowlink : v->lowlink;
    }
    else if (0 != w->contained)
      v->lowlink = (v->lowlink > w->index) ? w->index : v->lowlink;
  }

  if (v->lowlink == v->index)
  {
    w = stack[--(*stack_size)];
    w->contained = 0;

    if (v != w)
    {
      (*scc_counter)++;
      while (v != w)
      {
        w->scc_id = *scc_counter;
        w = stack[--(*stack_size)];
        w->contained = 0;
      }
      w->scc_id = *scc_counter;
    }
  }
}


/**
 * Detect all SCCs (Strongly Connected Components) inside the given automaton.
 * SCCs will be marked using the scc_id on each state.
 *
 * @param a the automaton for which SCCs should be computed and assigned.
 */
static void
scc_tarjan (struct GNUNET_REGEX_Automaton *a)
{
  unsigned int index;
  unsigned int scc_counter;
  struct GNUNET_REGEX_State *v;
  struct GNUNET_REGEX_State *stack[a->state_count];
  unsigned int stack_size;

  for (v = a->states_head; NULL != v; v = v->next)
  {
    v->contained = 0;
    v->index = -1;
    v->lowlink = -1;
  }

  stack_size = 0;
  index = 0;
  scc_counter = 0;

  for (v = a->states_head; NULL != v; v = v->next)
  {
    if (v->index < 0)
      scc_tarjan_strongconnect (&scc_counter, v, &index, stack, &stack_size);
  }
}


/**
 * Save a state to an open file pointer. cls is expected to be a file pointer to
 * an open file. Used only in conjunction with
 * GNUNET_REGEX_automaton_save_graph.
 *
 * @param cls file pointer.
 * @param count current count of the state, not used.
 * @param s state.
 */
void
GNUNET_REGEX_automaton_save_graph_step (void *cls, unsigned int count,
                                        struct GNUNET_REGEX_State *s)
{
  FILE *p;
  struct GNUNET_REGEX_Transition *ctran;
  char *s_acc = NULL;
  char *s_tran = NULL;

  p = cls;

  if (s->accepting)
  {
    GNUNET_asprintf (&s_acc,
                     "\"%s(%i)\" [shape=doublecircle, color=\"0.%i 0.8 0.95\"];\n",
                     s->name, s->proof_id, s->scc_id);
  }
  else
  {
    GNUNET_asprintf (&s_acc, "\"%s(%i)\" [color=\"0.%i 0.8 0.95\"];\n", s->name,
                     s->proof_id, s->scc_id);
  }

  if (NULL == s_acc)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Could not print state %s\n", s->name);
    return;
  }
  fwrite (s_acc, strlen (s_acc), 1, p);
  GNUNET_free (s_acc);
  s_acc = NULL;

  for (ctran = s->transitions_head; NULL != ctran; ctran = ctran->next)
  {
    if (NULL == ctran->to_state)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Transition from State %i has no state for transitioning\n",
                  s->id);
      continue;
    }

    if (ctran->label == 0)
    {
      GNUNET_asprintf (&s_tran,
                       "\"%s(%i)\" -> \"%s(%i)\" [label = \"epsilon\", color=\"0.%i 0.8 0.95\"];\n",
                       s->name, s->proof_id, ctran->to_state->name,
                       ctran->to_state->proof_id, s->scc_id);
    }
    else
    {
      GNUNET_asprintf (&s_tran,
                       "\"%s(%i)\" -> \"%s(%i)\" [label = \"%c\", color=\"0.%i 0.8 0.95\"];\n",
                       s->name, s->proof_id, ctran->to_state->name,
                       ctran->to_state->proof_id, ctran->label, s->scc_id);
    }

    if (NULL == s_tran)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Could not print state %s\n",
                  s->name);
      return;
    }

    fwrite (s_tran, strlen (s_tran), 1, p);
    GNUNET_free (s_tran);
    s_tran = NULL;
  }
}


/**
 * Save the given automaton as a GraphViz dot file
 *
 * @param a the automaton to be saved
 * @param filename where to save the file
 */
void
GNUNET_REGEX_automaton_save_graph (struct GNUNET_REGEX_Automaton *a,
                                   const char *filename)
{
  char *start;
  char *end;
  FILE *p;

  if (NULL == a)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Could not print NFA, was NULL!");
    return;
  }

  if (NULL == filename || strlen (filename) < 1)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "No Filename given!");
    return;
  }

  p = fopen (filename, "w");

  if (NULL == p)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Could not open file for writing: %s",
                filename);
    return;
  }

  /* First add the SCCs to the automaton, so we can color them nicely */
  scc_tarjan (a);

  start = "digraph G {\nrankdir=LR\n";
  fwrite (start, strlen (start), 1, p);

  GNUNET_REGEX_automaton_traverse (a, &GNUNET_REGEX_automaton_save_graph_step,
                                   p);

  end = "\n}\n";
  fwrite (end, strlen (end), 1, p);
  fclose (p);
}
