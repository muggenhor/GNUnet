/*
 This file is part of GNUnet.
 (C) 2010,2011 Christian Grothoff (and other contributing authors)

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
 * @file ats/perf_ats_solver.c
 * @brief generic performance test for ATS solvers
 * @author Christian Grothoff
 * @author Matthias Wachs
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_statistics_service.h"
#include "gnunet-service-ats_addresses.h"
#include "gnunet-service-ats_normalization.h"
#include "gnunet_ats_service.h"
#include "gnunet_ats_plugin.h"
#include "test_ats_api_common.h"

#define DEFAULT_UPDATE_PERCENTAGE       20
#define DEFAULT_PEERS_START     10
#define DEFAULT_PEERS_END       10
#define DEFAULT_ADDRESSES       10
#define DEFAULT_ATS_COUNT       2

#define GNUPLOT_PROP_TEMPLATE "#!/usr/bin/gnuplot \n" \
"set datafile separator ';' \n" \
"set title \"Execution time Proportional solver  \" \n" \
"set xlabel \"Number of peers\" \n" \
"set ylabel \"Execution time in us\" \n" \
"set grid \n"

#define GNUPLOT_PROP_UPDATE_TEMPLATE "#!/usr/bin/gnuplot \n" \
"set datafile separator ';' \n" \
"set title \"Execution time Proportional solver with updated problem\" \n" \
"set xlabel \"Number of peers\" \n" \
"set ylabel \"Execution time in us\" \n" \
"set grid \n"

#define GNUPLOT_MLP_TEMPLATE "#!/usr/bin/gnuplot \n" \
"set datafile separator ';' \n" \
"set title \"Execution time MLP solver \" \n" \
"set xlabel \"Number of peers\" \n" \
"set ylabel \"Execution time in us\" \n" \
"set grid \n"

#define GNUPLOT_MLP_UPDATE_TEMPLATE "#!/usr/bin/gnuplot \n" \
"set datafile separator ';' \n" \
"set title \"Execution time MLP solver with updated problem\" \n" \
"set xlabel \"Number of peers\" \n" \
"set ylabel \"Execution time in us\" \n" \
"set grid \n"

#define GNUPLOT_RIL_TEMPLATE "#!/usr/bin/gnuplot \n" \
"set datafile separator ';' \n" \
"set title \"Execution time RIL solver \" \n" \
"set xlabel \"Number of peers\" \n" \
"set ylabel \"Execution time in us\" \n" \
"set grid \n"

#define GNUPLOT_RIL_UPDATE_TEMPLATE "#!/usr/bin/gnuplot \n" \
"set datafile separator ';' \n" \
"set title \"Execution time RIL solver with updated problem\" \n" \
"set xlabel \"Number of peers\" \n" \
"set ylabel \"Execution time in us\" \n" \
"set grid \n"

/**
 * Handle for ATS address component
 */
struct PerfHandle
{
  /**
   * Performance peers
   */
  struct PerfPeer *peers;

  /**
   *  Solver handle
   */
  void *solver;

  /**
   * Statistics stat;
   */
  struct GNUNET_STATISTICS_Handle *stat;

  /**
   * A multihashmap to store all addresses
   */
  struct GNUNET_CONTAINER_MultiPeerMap *addresses;

  /**
   * Solver functions
   * */
  struct GNUNET_ATS_PluginEnvironment env;

  /**
   * Array for results for each iteration with length iterations
   */
  struct Iteration *iterations_results;

  /**
   * Array to store averaged full solution result with length #peers
   */
  struct Result *averaged_full_result;

  /**
   * Array to store averaged updated solution result with length #peers
   */
  struct Result *averaged_update_result;

  /**
   * The current result
   */
  struct Result *current_result;

  /**
   * Current number of peers benchmarked
   */
  int current_p;

  /**
   * Current number of addresses benchmarked
   */
  int current_a;

  /**
   * Solver description as string
   */
  char *ats_string;

  /**
   * Configured ATS solver
   */
  int ats_mode;

  /**
   * #peers to start benchmarking with
   */
  int N_peers_start;

  /**
   * #peers to end benchmarking with
   */
  int N_peers_end;

  /**
   * #addresses to benchmarking with
   */
  int N_address;

  /**
   * Percentage of peers to update
   */
  int opt_update_percent;

  /**
   * Create gnuplot file
   */
  int create_plot;

  /**
   * Measure updates
   */
  int measure_updates;

  /**
   * Number of iterations
   */
  int total_iterations;

  /**
   * Current iteration
   */
  int current_iteration;

  /**
   * Is a bulk operation running?
   */
  int bulk_running;

  /**
   * Is a bulk operation running?
   */
  int expecting_solution;

  /**
   * Was the problem just updates?
   */
  int performed_update;
};

/**
 * Data structure to store results for a single iteration
 */
struct Iteration
{
  /**
   * Head of the linked list
   */
  struct Result *result_head;

  /**
   * Tail of the linked list
   */
  struct Result *result_tail;
};


/**
 * Result for a solver calculcation
 */
struct Result
{
  /**
   * Previous element in the linked list
   */
  struct Result *prev;

  /**
   * Next element in the linked list
   */
  struct Result *next;

  /**
   * Number of peers this solution included
   */
  int peers;

  /**
   * Number of addresses per peer this solution included
   */
  int addresses;

  /**
   * Is this an update or a full solution
   */
  int update;

  /**
   * Was the solution valid or did the solver fail
   */
  int valid;

  /**
   * Result of the solver
   */
  enum GAS_Solver_Additional_Information info;

  /**
   * Duration of setting up the problem in the solver
   */
  struct GNUNET_TIME_Relative d_setup;

  /**
   * Duration of solving the LP problem in the solver
   * MLP solver only
   */
  struct GNUNET_TIME_Relative d_lp;

  /**
   * Duration of solving the MLP problem in the solver
   * MLP solver only
   */
  struct GNUNET_TIME_Relative d_mlp;

  /**
   * Duration of solving whole problem in the solver
   */
  struct GNUNET_TIME_Relative d_total;

  /**
   * Start time of setting up the problem in the solver
   */
  struct GNUNET_TIME_Absolute s_setup;

  /**
   * Start time of solving the LP problem in the solver
   * MLP solver only
   */
  struct GNUNET_TIME_Absolute s_lp;

  /**
   * Start time of solving the MLP problem in the solver
   * MLP solver only
   */
  struct GNUNET_TIME_Absolute s_mlp;

  /**
   * Start time of solving whole problem in the solver
   */
  struct GNUNET_TIME_Absolute s_total;

  /**
   * End time of setting up the problem in the solver
   */
  struct GNUNET_TIME_Absolute e_setup;

  /**
   * End time of solving the LP problem in the solver
   * MLP solver only
   */
  struct GNUNET_TIME_Absolute e_lp;

  /**
   * End time of solving the MLP problem in the solver
   * MLP solver only
   */
  struct GNUNET_TIME_Absolute e_mlp;

  /**
   * End time of solving whole problem in the solver
   */
  struct GNUNET_TIME_Absolute e_total;
};

/**
 * Peer used for the benchmarking
 */
struct PerfPeer
{
  /**
   * Peer identitity
   */
  struct GNUNET_PeerIdentity id;

  /**
   * Head of linked list of addresses used with this peer
   */
  struct ATS_Address *head;

  /**
   * Head of linked list of addresses used with this peer
   */
  struct ATS_Address *tail;
};


/**
 * ATS performance handle
 */
static struct PerfHandle ph;

/**
 * Return value
 */
static int ret;


/**
 * Do shutdown
 */
static void
end_now (int res)
{
  if (NULL != ph.stat)
  {
    GNUNET_STATISTICS_destroy (ph.stat, GNUNET_NO);
    ph.stat = NULL;
  }

  GNUNET_free_non_null (ph.peers);
  GNUNET_free_non_null (ph.iterations_results);
  GNUNET_free_non_null (ph.averaged_full_result);
  GNUNET_free_non_null (ph.averaged_update_result);

  GAS_normalization_stop ();
  ret = res;
}


/**
 * Create a peer used for benchmarking
 *
 * @param cp the number of the peer
 */
static void
perf_create_peer (int cp)
{

  GNUNET_CRYPTO_random_block (GNUNET_CRYPTO_QUALITY_WEAK,
      &ph.peers[cp].id, sizeof (struct GNUNET_PeerIdentity));
  GNUNET_log(GNUNET_ERROR_TYPE_DEBUG, "Creating peer #%u: %s \n", cp,
      GNUNET_i2s (&ph.peers[cp].id));
}


/**
 * Perform an update for an address
 *
 * @param cur the address to update
 */
static void
perf_update_address (struct ATS_Address *cur)
{
  int r_type;
  int r_val;

  r_type = GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, 2);
  switch (r_type)
  {
  case 0:
    r_val = GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, 100);
    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
        "Updating peer `%s' address %p type %s val %u\n",
        GNUNET_i2s (&cur->peer), cur, "GNUNET_ATS_QUALITY_NET_DELAY", r_val);
    ph.env.sf.s_address_update_property (ph.solver, cur,
        GNUNET_ATS_QUALITY_NET_DELAY,
        r_val, (double) (100 + r_val / 100));
    break;
  case 1:
    r_val = GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, 10);

    GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
        "Updating peer `%s' address %p type %s val %u\n",
        GNUNET_i2s (&cur->peer), cur, "GNUNET_ATS_QUALITY_NET_DISTANCE", r_val);
    ph.env.sf.s_address_update_property (ph.solver, cur,
        GNUNET_ATS_QUALITY_NET_DISTANCE,
        r_val, (double) (100 + r_val) / 100);
    break;
  default:
    break;
  }
  ph.env.sf.s_address_update_inuse (ph.solver, cur, GNUNET_YES);
}


static void
bandwidth_changed_cb (void *cls,
                      struct ATS_Address *address)
{
  if ( (0 == ntohl (address->assigned_bw_out.value__)) &&
       (0 == ntohl (address->assigned_bw_in.value__)) )
    return;

  GNUNET_log (GNUNET_ERROR_TYPE_DEBUG,
              "Bandwidth changed addresses %s %p to %u Bps out / %u Bps in\n",
              GNUNET_i2s (&address->peer),
              address,
              (unsigned int) ntohl (address->assigned_bw_out.value__),
              (unsigned int) ntohl (address->assigned_bw_in.value__));
  if (GNUNET_YES == ph.bulk_running)
    GNUNET_break (0);
  return;
}


const double *
get_preferences_cb (void *cls, const struct GNUNET_PeerIdentity *id)
{
  return GAS_normalization_get_preferences_by_peer (id);
}


const double *
get_property_cb (void *cls, const struct ATS_Address *address)
{
  return GAS_normalization_get_properties ((struct ATS_Address *) address);
}

static void
normalized_property_changed_cb (void *cls, struct ATS_Address *peer,
    uint32_t type, double prop_rel)
{
  /* TODO */
}

static void
perf_address_initial_update (void *solver,
    struct GNUNET_CONTAINER_MultiPeerMap * addresses,
    struct ATS_Address *address)
{
  ph.env.sf.s_address_update_property (solver, address, GNUNET_ATS_QUALITY_NET_DELAY,
      100,
      (double) (100 + GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, 100))
          / 100);

  ph.env.sf.s_address_update_property (solver, address,
      GNUNET_ATS_QUALITY_NET_DISTANCE, 10,
      (double) (100 + GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, 100))
          / 100);
}

/**
 * Update a certain percentage of peers
 *
 * @param cp the current number of peers
 * @param ca the current number of addresses
 * @param percentage_peers the percentage of peers to update
 */

static void
perf_update_all_addresses (unsigned int cp, unsigned int ca, unsigned int percentage_peers)
{
  struct ATS_Address *cur_address;
  int c_peer;
  int c_select;
  int c_cur_p;
  int c_cur_a;
  int r;
  int count;
  unsigned int m[cp];

  count = cp * ((double) percentage_peers / 100);
  GNUNET_log(GNUNET_ERROR_TYPE_INFO,
      "Updating %u of %u peers \n", count, cp);

  for (c_peer = 0; c_peer < cp; c_peer++)
    m[c_peer] = 0;

  c_select = 0;

  while (c_select < count)
  {
    r = GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, cp);
    if (0 == m[r])
    {
      m[r] = 1;
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
          "Updating peer [%u] \n", r);
      c_select++;
    }
  }
  for (c_cur_p = 0; c_cur_p < cp; c_cur_p++)
  {
    if (1 == m[c_cur_p])
    {
      r = GNUNET_CRYPTO_random_u32 (GNUNET_CRYPTO_QUALITY_WEAK, ca);
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
          "Updating peer [%u] address [%u]\n", c_cur_p, r);

      c_cur_a = 0;
      for (cur_address = ph.peers[c_cur_p].head; NULL != cur_address; cur_address = cur_address->next)
      {
        if (c_cur_a == r)
          perf_update_address (cur_address);
        c_cur_a ++;
      }
    }
  }
}

/**
 * Create an address for a peer
 *
 * @param cp index of the peer
 * @param ca index of the address
 * @return the address
 */
static struct ATS_Address *
perf_create_address (int cp, int ca)
{
  struct ATS_Address *a;
  a = create_address (&ph.peers[cp].id,
      "Test 1", "test 1", strlen ("test 1") + 1, 0);
  GNUNET_CONTAINER_DLL_insert (ph.peers[cp].head, ph.peers[cp].tail, a);
  GNUNET_CONTAINER_multipeermap_put (ph.addresses, &ph.peers[cp].id, a,
      GNUNET_CONTAINER_MULTIHASHMAPOPTION_MULTIPLE);
  return a;
}


/**
 * Information callback for the solver
 *
 * @param op the solver operation
 * @param stat status of the solver operation
 * @param add additional solver information
 */
static void
solver_info_cb (void *cls,
    enum GAS_Solver_Operation op,
    enum GAS_Solver_Status stat,
    enum GAS_Solver_Additional_Information add)
{
  char *add_info;
  switch (add) {
    case GAS_INFO_NONE:
      add_info = "GAS_INFO_NONE";
      break;
    case GAS_INFO_FULL:
      add_info = "GAS_INFO_MLP_FULL";
      break;
    case GAS_INFO_UPDATED:
      add_info = "GAS_INFO_MLP_UPDATED";
      break;
    case GAS_INFO_PROP_ALL:
      add_info = "GAS_INFO_PROP_ALL";
      break;
    case GAS_INFO_PROP_SINGLE:
      add_info = "GAS_INFO_PROP_SINGLE";
      break;
    default:
      add_info = "INVALID";
      break;
  }

  struct Result *tmp;
  switch (op)
  {
    case GAS_OP_SOLVE_START:
      GNUNET_log(GNUNET_ERROR_TYPE_INFO,
          "Solver notifies `%s' with result `%s' `%s'\n", "GAS_OP_SOLVE_START",
          (GAS_STAT_SUCCESS == stat) ? "SUCCESS" : "FAIL", add_info);
      if (GNUNET_NO == ph.expecting_solution)
      {
        /* We do not expect a solution at the moment */
        GNUNET_break (0);
        return;
      }

      if ((GAS_STAT_SUCCESS == stat) && (NULL == ph.current_result))
      {
        /* Create new result */
        tmp = GNUNET_new (struct Result);
        ph.current_result = tmp;
        GNUNET_CONTAINER_DLL_insert_tail(ph.iterations_results[ph.current_iteration-1].result_head,
            ph.iterations_results[ph.current_iteration-1].result_tail, tmp);
        ph.current_result->addresses = ph.current_a;
        ph.current_result->peers = ph.current_p;
        ph.current_result->s_total = GNUNET_TIME_absolute_get();
        ph.current_result->d_total = GNUNET_TIME_UNIT_FOREVER_REL;
        ph.current_result->d_setup = GNUNET_TIME_UNIT_FOREVER_REL;
        ph.current_result->d_lp = GNUNET_TIME_UNIT_FOREVER_REL;
        ph.current_result->d_mlp = GNUNET_TIME_UNIT_FOREVER_REL;
        ph.current_result->info = add;
        if ((add == GAS_INFO_UPDATED) || (GNUNET_YES == ph.performed_update))
        {
          ph.current_result->update = GNUNET_YES;
        }
        else
        {
          ph.current_result->update = GNUNET_NO;
        }

      }
      return;
    case GAS_OP_SOLVE_STOP:
      GNUNET_log(GNUNET_ERROR_TYPE_INFO,
          "Solver notifies `%s' with result `%s'\n", "GAS_OP_SOLVE_STOP",
          (GAS_STAT_SUCCESS == stat) ? "SUCCESS" : "FAIL", add_info);
      if ((GNUNET_NO == ph.expecting_solution) || (NULL == ph.current_result))
      {
        /* We do not expect a solution at the moment */
        GNUNET_break (0);
        return;
      }

      if (GAS_STAT_SUCCESS == stat)
        ph.current_result->valid = GNUNET_YES;
      else
        ph.current_result->valid = GNUNET_NO;

      if (NULL != ph.current_result)
      {
        /* Finalize result */
        ph.current_result->e_total = GNUNET_TIME_absolute_get ();
        ph.current_result->d_total = GNUNET_TIME_absolute_get_difference (
            ph.current_result->s_total, ph.current_result->e_total);
      }
      ph.current_result = NULL;
      return;

    case GAS_OP_SOLVE_SETUP_START:
      GNUNET_log(GNUNET_ERROR_TYPE_INFO,
          "Solver notifies `%s' with result `%s'\n", "GAS_OP_SOLVE_SETUP_START",
          (GAS_STAT_SUCCESS == stat) ? "SUCCESS" : "FAIL");
      if ((GNUNET_NO == ph.expecting_solution) || (NULL == ph.current_result))
      {
        GNUNET_break(0);
        return;
      }

      if (GAS_STAT_SUCCESS == stat)
        ph.current_result->valid = GNUNET_YES;
      else
        ph.current_result->valid = GNUNET_NO;

      ph.current_result->s_setup = GNUNET_TIME_absolute_get ();
      return;

    case GAS_OP_SOLVE_SETUP_STOP:
      GNUNET_log(GNUNET_ERROR_TYPE_INFO,
          "Solver notifies `%s' with result `%s'\n", "GAS_OP_SOLVE_SETUP_STOP",
          (GAS_STAT_SUCCESS == stat) ? "SUCCESS" : "FAIL");
      if ((GNUNET_NO == ph.expecting_solution) || (NULL == ph.current_result))
      {
        GNUNET_break(0);
        return;
      }

      if (GAS_STAT_SUCCESS == stat)
        ph.current_result->valid = GNUNET_YES;
      else
        ph.current_result->valid = GNUNET_NO;

      ph.current_result->e_setup = GNUNET_TIME_absolute_get ();
      ph.current_result->d_setup = GNUNET_TIME_absolute_get_difference (
          ph.current_result->s_setup, ph.current_result->e_setup);
      return;

    case GAS_OP_SOLVE_MLP_LP_START:
      GNUNET_log(GNUNET_ERROR_TYPE_INFO,
          "Solver notifies `%s' with result `%s'\n", "GAS_OP_SOLVE_LP_START",
          (GAS_STAT_SUCCESS == stat) ? "SUCCESS" : "FAIL");
      if ((GNUNET_NO == ph.expecting_solution) || (NULL == ph.current_result))
      {
        GNUNET_break(0);
        return;
      }

      if (GAS_STAT_SUCCESS == stat)
        ph.current_result->valid = GNUNET_YES;
      else
        ph.current_result->valid = GNUNET_NO;

      ph.current_result->s_lp = GNUNET_TIME_absolute_get ();
      return;
    case GAS_OP_SOLVE_MLP_LP_STOP:
      GNUNET_log(GNUNET_ERROR_TYPE_INFO,
          "Solver notifies `%s' with result `%s'\n", "GAS_OP_SOLVE_LP_STOP",
          (GAS_STAT_SUCCESS == stat) ? "SUCCESS" : "FAIL");
      if ((GNUNET_NO == ph.expecting_solution) || (NULL == ph.current_result))
      {
        GNUNET_break(0);
        return;
      }

      if (GAS_STAT_SUCCESS == stat)
        ph.current_result->valid = GNUNET_YES;
      else
        ph.current_result->valid = GNUNET_NO;

      ph.current_result->e_lp = GNUNET_TIME_absolute_get ();
      ph.current_result->d_lp = GNUNET_TIME_absolute_get_difference (
          ph.current_result->s_lp, ph.current_result->e_lp);
      return;

    case GAS_OP_SOLVE_MLP_MLP_START:
      GNUNET_log(GNUNET_ERROR_TYPE_INFO,
          "Solver notifies `%s' with result `%s'\n", "GAS_OP_SOLVE_MLP_START",
          (GAS_STAT_SUCCESS == stat) ? "SUCCESS" : "FAIL");
      if ((GNUNET_NO == ph.expecting_solution) || (NULL == ph.current_result))
      {
        GNUNET_break(0);
        return;
      }

      if (GAS_STAT_SUCCESS == stat)
        ph.current_result->valid = GNUNET_YES;
      else
        ph.current_result->valid = GNUNET_NO;

      ph.current_result->s_mlp = GNUNET_TIME_absolute_get ();
      return;
    case GAS_OP_SOLVE_MLP_MLP_STOP:
      GNUNET_log(GNUNET_ERROR_TYPE_INFO,
          "Solver notifies `%s' with result `%s'\n", "GAS_OP_SOLVE_MLP_STOP",
          (GAS_STAT_SUCCESS == stat) ? "SUCCESS" : "FAIL");
      if ((GNUNET_NO == ph.expecting_solution) || (NULL == ph.current_result))
      {
        GNUNET_break(0);
        return;
      }

      if (GAS_STAT_SUCCESS == stat)
        ph.current_result->valid = GNUNET_YES;
      else
        ph.current_result->valid = GNUNET_NO;

      ph.current_result->e_mlp = GNUNET_TIME_absolute_get ();
      ph.current_result->d_mlp = GNUNET_TIME_absolute_get_difference (
      ph.current_result->s_mlp, ph.current_result->e_mlp);
      return;
    case GAS_OP_SOLVE_UPDATE_NOTIFICATION_START:
      GNUNET_log(GNUNET_ERROR_TYPE_INFO,
          "Solver notifies `%s' with result `%s'\n", "GAS_OP_SOLVE_UPDATE_NOTIFICATION_START",
          (GAS_STAT_SUCCESS == stat) ? "SUCCESS" : "FAIL");
      return;
    case GAS_OP_SOLVE_UPDATE_NOTIFICATION_STOP:
      GNUNET_log(GNUNET_ERROR_TYPE_INFO,
          "Solver notifies `%s' with result `%s'\n", "GAS_OP_SOLVE_UPDATE_NOTIFICATION_STOP",
          (GAS_STAT_SUCCESS == stat) ? "SUCCESS" : "FAIL");
      if (GAS_STAT_SUCCESS != stat)
      {
        GNUNET_log(GNUNET_ERROR_TYPE_ERROR,
            "Solver `%s' failed to update problem with %u peers and %u address!\n",
            ph.ats_string, ph.current_p, ph.current_a);
      }

      return;
    default:
      break;
    }
}

static void
write_gnuplot_script (char * data_fn, int iteration, int full)
{
  struct GNUNET_DISK_FileHandle *f;
  char * gfn;
  char *data;
  char *iter_text;
  char *template;

  /* Write header */
  switch (ph.ats_mode) {
    case MODE_PROPORTIONAL:
      if (GNUNET_YES == full)
        template = GNUPLOT_PROP_TEMPLATE;
      else
        template = GNUPLOT_PROP_UPDATE_TEMPLATE;
      break;
    case MODE_MLP:
      if (GNUNET_YES == full)
        template = GNUPLOT_MLP_TEMPLATE;
      else
        template = GNUPLOT_MLP_UPDATE_TEMPLATE;
      break;
    case MODE_RIL:
      if (GNUNET_YES == full)
        template = GNUPLOT_RIL_TEMPLATE;
      else
        template = GNUPLOT_RIL_UPDATE_TEMPLATE;
      break;
    default:
      GNUNET_break (0);
      return;
  }
  if (-1 == iteration)
    GNUNET_asprintf (&iter_text, "%s_%u", "avg",ph.total_iterations);
  else
    GNUNET_asprintf (&iter_text, "%u", iteration);
  if (GNUNET_YES == full)
  {
    GNUNET_asprintf (&gfn, "perf_%s_full_%s-%u_%u_%u.gnuplot",
        ph.ats_string,
        iter_text,
        ph.N_peers_start,
        ph.N_peers_end,
        ph.N_address);
  }
  else
  {
    GNUNET_asprintf (&gfn, "perf_%s_updat_%s-%u_%u_%u.gnuplot",
        ph.ats_string,
        iter_text,
        ph.N_peers_start,
        ph.N_peers_end,
        ph.N_address);
  }
  GNUNET_free (iter_text);

  f = GNUNET_DISK_file_open (gfn,
      GNUNET_DISK_OPEN_WRITE | GNUNET_DISK_OPEN_CREATE,
      GNUNET_DISK_PERM_USER_EXEC | GNUNET_DISK_PERM_USER_READ | GNUNET_DISK_PERM_USER_WRITE);
  if (NULL == f)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Cannot open gnuplot file `%s'\n", gfn);
    GNUNET_free (gfn);
    return;
  }

  if (GNUNET_SYSERR == GNUNET_DISK_file_write(f, template, strlen(template)))
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR, "Cannot write data to plot file `%s'\n", gfn);

  data = NULL;
  if (MODE_PROPORTIONAL == ph.ats_mode)
  {
    GNUNET_asprintf (&data, "plot '%s' using 1:%u with lines title 'Total time to solve'\n" \
                           "pause -1",
                           data_fn, 3);
  }
  else if (MODE_MLP == ph.ats_mode)
  {
    GNUNET_asprintf (&data, "plot '%s' using 1:%u with lines title 'Total time to solve',\\\n" \
                            "'%s' using 1:%u with lines title 'Time to setup',\\\n"
                            "'%s' using 1:%u with lines title 'Time to solve LP',\\\n"
                            "'%s' using 1:%u with lines title 'Total time to solve MLP'\n" \
                            "pause -1",
                           data_fn, 3,
                           data_fn, 4,
                           data_fn, 5,
                           data_fn, 6);
  }
  else if (MODE_RIL == ph.ats_mode)
  {
    GNUNET_asprintf (&data,
                     "plot '%s' using 1:%u with lines title 'Total time to solve'\n" \
                     "pause -1",
                     data_fn, 3);
  }

  if ((NULL != data) &&
      (GNUNET_SYSERR == GNUNET_DISK_file_write (f, data, strlen(data))))
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Cannot write data to plot file `%s'\n",
                gfn);
  GNUNET_free_non_null (data);

  if (GNUNET_SYSERR == GNUNET_DISK_file_close(f))
    GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                "Cannot close gnuplot file `%s'\n",
                gfn);
  else
    GNUNET_log (GNUNET_ERROR_TYPE_INFO,
                "Data successfully written to plot file `%s'\n",
                gfn);
  GNUNET_free (gfn);

}

/**
 * Evaluate results for a specific iteration
 *
 * @param iteration the iteration to evaluate
 */
static void
evaluate (int iteration)
{
  struct GNUNET_DISK_FileHandle *f_full;
  struct GNUNET_DISK_FileHandle *f_update;
  char * data_fn_full;
  char * data_fn_update;
  char * data;
  struct Result *cur;
  struct Result *next;
  struct Result *cur_res;
  char * str_d_total;
  char * str_d_setup;
  char * str_d_lp;
  char * str_d_mlp;
  char * iter_text;

  f_full = NULL;
  f_update = NULL;

  data_fn_full = NULL;

  if (ph.create_plot)
  {
    if (-1 == iteration)
      GNUNET_asprintf (&iter_text, "%s", "avg");
    else
      GNUNET_asprintf (&iter_text, "%u", iteration);
    GNUNET_asprintf (&data_fn_full,
                     "perf_%s_full_%s_%u_%u_%u.data",
                     ph.ats_string,
                     iter_text,
                     ph.N_peers_start,
                     ph.N_peers_end,
                     ph.N_address);
    GNUNET_free (iter_text);
    f_full = GNUNET_DISK_file_open (data_fn_full,
        GNUNET_DISK_OPEN_WRITE | GNUNET_DISK_OPEN_CREATE,
        GNUNET_DISK_PERM_USER_EXEC | GNUNET_DISK_PERM_USER_READ | GNUNET_DISK_PERM_USER_WRITE);
    if (NULL == f_full)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Cannot open gnuplot file `%s'\n",
                  data_fn_full);
      GNUNET_free (data_fn_full);
      return;
    }
    data = "#peers;addresses;time total in us;#time setup in us;#time lp in us;#time mlp in us;\n";
    if (GNUNET_SYSERR == GNUNET_DISK_file_write(f_full, data, strlen(data)))
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Cannot write data to log file `%s'\n",
                  data_fn_full);
    write_gnuplot_script (data_fn_full, iteration, GNUNET_YES);
  }

  data_fn_update = NULL;
  if ((ph.create_plot) && (GNUNET_YES == ph.measure_updates))
  {
    if (-1 == iteration)
      GNUNET_asprintf (&iter_text, "%s", "avg");
    else
      GNUNET_asprintf (&iter_text, "%u", iteration);
    GNUNET_asprintf (&data_fn_update, "perf_%s_update_i%u_%u_%u_%u.data",
        ph.ats_string,
        iter_text,
        ph.N_peers_start,
        ph.N_peers_end,
        ph.N_address);
    GNUNET_free (iter_text);
    f_update = GNUNET_DISK_file_open (data_fn_update,
        GNUNET_DISK_OPEN_WRITE | GNUNET_DISK_OPEN_CREATE,
        GNUNET_DISK_PERM_USER_EXEC | GNUNET_DISK_PERM_USER_READ | GNUNET_DISK_PERM_USER_WRITE);
    if (NULL == f_update)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Cannot open gnuplot file `%s'\n", data_fn_update);
      GNUNET_free (data_fn_update);
      if (NULL != f_full)
        GNUNET_DISK_file_close (f_full);
      GNUNET_free (data_fn_full);
      return;
    }
    data = "#peers;addresses;time total in us;#time setup in us;#time lp in us;#time mlp in us;\n";
    if (GNUNET_SYSERR == GNUNET_DISK_file_write (f_update, data, strlen(data)))
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Cannot write data to log file `%s'\n",
                  data_fn_update);
    write_gnuplot_script (data_fn_update, iteration, GNUNET_NO);
  }

  next = ph.iterations_results[ph.current_iteration -1].result_head;
  while (NULL != (cur = next))
  {
    next = cur->next;
    str_d_total = NULL;
    str_d_setup = NULL;
    str_d_lp = NULL;
    str_d_mlp = NULL;

    /* Print log */
    if (GNUNET_NO == cur->update)
    {
      cur_res = &ph.averaged_full_result[cur->peers - ph.N_peers_start];
    }
    else
    {
      cur_res = &ph.averaged_update_result[cur->peers - ph.N_peers_start];
    }

    cur_res->peers = cur->peers;
    cur_res->addresses = cur->addresses;
    cur_res->update = cur->update;

    if (GNUNET_NO == cur->valid)
    {
      fprintf (stderr,
               "Total time to solve %s for %u peers %u addresses: %s\n",
               (GNUNET_YES == cur->update) ? "updated" : "full",
               cur->peers, cur->addresses, "Failed to solve!");
      continue;
    }
    else
      cur_res->valid ++;

    if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us != cur->d_total.rel_value_us)
    {
      if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us == cur_res->d_total.rel_value_us)
        cur_res->d_total.rel_value_us = 0;
      if (GNUNET_YES == cur->valid)
        cur_res->d_total.rel_value_us += cur->d_total.rel_value_us;
      fprintf (stderr,
         "Total time to solve %s for %u peers %u addresses: %llu us\n",
         (GNUNET_YES == cur->update) ? "updated" : "full",
         cur->peers, cur->addresses,
         (unsigned long long) cur->d_total.rel_value_us);
      GNUNET_asprintf(&str_d_total,
         "%llu", (unsigned long long) cur->d_total.rel_value_us);
    }
    else
      GNUNET_asprintf(&str_d_total, "-1");
    if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us != cur->d_setup.rel_value_us)
    {
      if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us == cur_res->d_setup.rel_value_us)
        cur_res->d_setup.rel_value_us = 0;
      if (GNUNET_YES == cur->valid)
        cur_res->d_setup.rel_value_us += cur->d_setup.rel_value_us;
      fprintf (stderr, "Total time to setup %s %u peers %u addresses: %llu us\n",
          (GNUNET_YES == cur->update) ? "updated" : "full",
          cur->peers, cur->addresses,
          (unsigned long long) cur->d_setup.rel_value_us);
      GNUNET_asprintf(&str_d_setup, "%llu",
          (unsigned long long )cur->d_setup.rel_value_us);
    }
    else
      GNUNET_asprintf(&str_d_setup, "-1");

    if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us != cur->d_lp.rel_value_us)
    {
      if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us == cur_res->d_lp.rel_value_us)
        cur_res->d_lp.rel_value_us = 0;
      if (GNUNET_YES == cur->valid)
        cur_res->d_lp.rel_value_us += cur->d_lp.rel_value_us;
      fprintf (stderr,
         "Total time to solve %s LP for %u peers %u addresses: %llu us\n",
         (GNUNET_YES == cur->update) ? "updated" : "full",
         cur->peers,
         cur->addresses,
         (unsigned long long )cur->d_lp.rel_value_us);
      GNUNET_asprintf (&str_d_lp,
          "%llu", (unsigned long long )cur->d_lp.rel_value_us);
    }
    else
      GNUNET_asprintf (&str_d_lp, "-1");

    if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us != cur->d_mlp.rel_value_us)
    {
      if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us == cur_res->d_mlp.rel_value_us)
        cur_res->d_mlp.rel_value_us = 0;
      if (GNUNET_YES == cur->valid)
        cur_res->d_mlp.rel_value_us += cur->d_mlp.rel_value_us;

      fprintf (stderr, "Total time to solve %s MLP for %u peers %u addresses: %llu us\n",
          (GNUNET_YES == cur->update) ? "updated" : "full",
          cur->peers, cur->addresses,
          (unsigned long long )cur->d_mlp.rel_value_us);
      GNUNET_asprintf (&str_d_mlp,
          "%llu", (unsigned long long )cur->d_mlp.rel_value_us);
    }
    else
      GNUNET_asprintf (&str_d_mlp, "-1");

    data = NULL;
    if (GNUNET_YES == ph.create_plot)
    {

      GNUNET_asprintf (&data,
                       "%u;%u;%s;%s;%s;%s\n",
                       cur->peers, cur->addresses,
                       str_d_total,
                       str_d_setup,
                       str_d_lp,
                       str_d_mlp);
      if (cur->update == GNUNET_NO)
      {
        if (GNUNET_SYSERR == GNUNET_DISK_file_write (f_full, data, strlen(data)))
          GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                      "Cannot write data to log file `%s'\n",
                      data_fn_full);
      }
      if ((cur->update == GNUNET_YES) && (NULL != f_update))
      {
        if (GNUNET_SYSERR == GNUNET_DISK_file_write (f_update, data, strlen(data)))
          GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                      "Cannot write data to log file `%s'\n",
                      data_fn_update);
      }
      GNUNET_free (data);
    }
    GNUNET_free_non_null (str_d_total);
    GNUNET_free_non_null (str_d_setup);
    GNUNET_free_non_null (str_d_lp);
    GNUNET_free_non_null (str_d_mlp);

    GNUNET_CONTAINER_DLL_remove (ph.iterations_results[ph.current_iteration-1].result_head,
        ph.iterations_results[ph.current_iteration-1].result_tail, cur);
    GNUNET_free (cur);
  }

  if ((NULL != f_full) && (GNUNET_SYSERR == GNUNET_DISK_file_close (f_full)))
    GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Cannot close log file `%s'\n",
        data_fn_full);
  GNUNET_free_non_null (data_fn_full);

  if ((NULL != f_update) && (GNUNET_SYSERR == GNUNET_DISK_file_close (f_update)))
    GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Cannot close log file `%s'\n",
        data_fn_update);
  GNUNET_free_non_null (data_fn_update);
}

/**
 * Evaluate average results for all iterations
 */
static void
evaluate_average (void)
{
  int c_o;
  int c_i;

  struct GNUNET_DISK_FileHandle *f_full;
  struct GNUNET_DISK_FileHandle *f_update;
  struct Result *cur;
  char * data_fn_full;
  char * data_fn_update;
  char * data;
  char * str_d_total;
  char * str_d_setup;
  char * str_d_lp;
  char * str_d_mlp;

  f_full = NULL;
  f_update = NULL;

  data_fn_full = NULL;

  if (ph.create_plot)
  {
    GNUNET_asprintf (&data_fn_full,
                     "perf_%s_full_avg_%u-%u_%u_%u.data",
                     ph.ats_string,
                     ph.total_iterations,
                     ph.N_peers_start,
                     ph.N_peers_end,
                     ph.N_address);
    f_full = GNUNET_DISK_file_open (data_fn_full,
        GNUNET_DISK_OPEN_WRITE | GNUNET_DISK_OPEN_CREATE,
        GNUNET_DISK_PERM_USER_EXEC | GNUNET_DISK_PERM_USER_READ | GNUNET_DISK_PERM_USER_WRITE);
    if (NULL == f_full)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Cannot open gnuplot file `%s'\n",
                  data_fn_full);
      GNUNET_free (data_fn_full);
      return;
    }
    data = "#peers;addresses;time total in us;#time setup in us;#time lp in us;#time mlp in us;\n";
    if (GNUNET_SYSERR == GNUNET_DISK_file_write(f_full, data, strlen(data)))
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Cannot write data to log file `%s'\n",
                  data_fn_full);
    write_gnuplot_script (data_fn_full, -1, GNUNET_YES);
  }

  data_fn_update = NULL;
  if ((ph.create_plot) && (GNUNET_YES == ph.measure_updates))
  {
    GNUNET_asprintf (&data_fn_update, "perf_%s_update_avg_%u-%u_%u_%u.data",
        ph.ats_string,
        ph.total_iterations,
        ph.N_peers_start,
        ph.N_peers_end,
        ph.N_address);
    f_update = GNUNET_DISK_file_open (data_fn_update,
        GNUNET_DISK_OPEN_WRITE | GNUNET_DISK_OPEN_CREATE,
        GNUNET_DISK_PERM_USER_EXEC | GNUNET_DISK_PERM_USER_READ | GNUNET_DISK_PERM_USER_WRITE);
    if (NULL == f_update)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Cannot open gnuplot file `%s'\n", data_fn_update);
      GNUNET_free (data_fn_update);
      if (NULL != f_full)
        GNUNET_DISK_file_close (f_full);
      GNUNET_free (data_fn_full);
      return;
    }
    data = "#peers;addresses;time total in us;#time setup in us;#time lp in us;#time mlp in us;\n";
    if (GNUNET_SYSERR == GNUNET_DISK_file_write (f_update, data, strlen(data)))
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                  "Cannot write data to log file `%s'\n",
                  data_fn_update);
    write_gnuplot_script (data_fn_update, -1, GNUNET_NO);
  }

  for (c_o = 0; c_o < 2; c_o++)
  {
    if (0 == c_o)
      fprintf (stderr,
          "Duration for a full solution averaged over %i iterations\n",
          ph.total_iterations);
    if (1 == c_o)
      fprintf (stderr,
          "Duration for a full solution averaged over %i iterations\n",
          ph.total_iterations);

    for (c_i = 0; c_i <= ph.N_peers_end - ph.N_peers_start; c_i++)
    {
      if (0 == c_o)
      {
        cur = &ph.averaged_full_result[c_i];
      }
      else if ((GNUNET_YES == ph.measure_updates) && (1 == c_o))
      {
        cur = &ph.averaged_update_result[c_i];
      }
      else
         break;

      if (0 == cur->peers)
        continue;

      str_d_total = NULL;
      str_d_setup = NULL;
      str_d_lp = NULL;
      str_d_mlp = NULL;

      if (0 >= cur->valid)
      {
        fprintf (stderr,
           "No valid results for %s for %u peers %u addresses!\n",
           (GNUNET_YES == cur->update) ? "updated" : "full",
               cur->peers, cur->addresses);
        continue;
      }

      if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us != cur->d_total.rel_value_us)
      {
        fprintf (stderr,
           "Average total time from %u iterations to solve %s for %u peers %u addresses: %llu us\n",
           cur->valid,
           (GNUNET_YES == cur->update) ? "updated" : "full",
               cur->peers, cur->addresses,
           (unsigned long long) cur->d_total.rel_value_us / cur->valid);
        GNUNET_asprintf(&str_d_total, "%llu",
           (unsigned long long) cur->d_total.rel_value_us / cur->valid);
      }
      else
        GNUNET_asprintf (&str_d_total, "-1");

      if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us != cur->d_setup.rel_value_us)
      {
        fprintf (stderr,
           "Average total time from %u iterations to setup for %u peers %u addresses: %llu us\n",
           cur->valid, cur->peers, cur->addresses,
           (unsigned long long) cur->d_setup.rel_value_us / cur->valid);
        GNUNET_asprintf(&str_d_setup, "%llu",
           (unsigned long long) cur->d_setup.rel_value_us / cur->valid);

      }
      else
        GNUNET_asprintf (&str_d_setup, "-1");

      if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us != cur->d_lp.rel_value_us)
      {
        fprintf (stderr,
           "Average total time from %u iterations to solve lp %s for %u peers %u addresses: %llu us\n",
           cur->valid,
           (GNUNET_YES == cur->update) ? "updated" : "full",
           cur->peers, cur->addresses,
           (unsigned long long) cur->d_lp.rel_value_us / cur->valid);
        GNUNET_asprintf(&str_d_lp, "%llu",
           (unsigned long long) cur->d_lp.rel_value_us / ph.total_iterations);
      }
      else
        GNUNET_asprintf (&str_d_lp, "-1");

      if (GNUNET_TIME_UNIT_FOREVER_REL.rel_value_us != cur->d_mlp.rel_value_us)
      {
        fprintf (stderr,
           "Average total time from %u iterations to solve mlp %s for %u peers %u addresses: %llu us\n",
           cur->valid,
           (GNUNET_YES == cur->update) ? "updated" : "full",
               cur->peers, cur->addresses,
           (unsigned long long) cur->d_mlp.rel_value_us / cur->valid);
        GNUNET_asprintf(&str_d_mlp, "%llu",
           (unsigned long long) cur->d_mlp.rel_value_us / cur->valid);
      }
      else
        GNUNET_asprintf (&str_d_mlp, "-1");

      data = NULL;
      if (GNUNET_YES == ph.create_plot)
      {
        GNUNET_asprintf (&data,
                         "%u;%u;%s;%s;%s;%s\n",
                         cur->peers, cur->addresses,
                         str_d_total,
                         str_d_setup,
                         str_d_lp,
                         str_d_mlp);
        if (cur->update == GNUNET_NO)
        {
          if (GNUNET_SYSERR == GNUNET_DISK_file_write (f_full, data, strlen(data)))
            GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                        "Cannot write data to log file `%s'\n",
                        data_fn_full);
        }
        if ((cur->update == GNUNET_YES) && (NULL != f_update))
        {
          if (GNUNET_SYSERR == GNUNET_DISK_file_write (f_update, data, strlen(data)))
            GNUNET_log (GNUNET_ERROR_TYPE_ERROR,
                        "Cannot write data to log file `%s'\n",
                        data_fn_update);
        }
        GNUNET_free (data);
      }

      GNUNET_free_non_null (str_d_total);
      GNUNET_free_non_null (str_d_setup);
      GNUNET_free_non_null (str_d_lp);
      GNUNET_free_non_null (str_d_mlp);
    }
  }

  if ((NULL != f_full) && (GNUNET_SYSERR == GNUNET_DISK_file_close (f_full)))
    GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Cannot close log file `%s'\n",
        data_fn_full);
  GNUNET_free_non_null (data_fn_full);

  if ((NULL != f_update) && (GNUNET_SYSERR == GNUNET_DISK_file_close (f_update)))
    GNUNET_log(GNUNET_ERROR_TYPE_ERROR, "Cannot close log file `%s'\n",
        data_fn_update);
  GNUNET_free_non_null (data_fn_update);
}

/**
 * Run a performance iteration
 */

static void
perf_run_iteration (void)
{
  struct ATS_Address *cur;
  struct ATS_Address *next;
  int cp;
  int ca;
  int count_p = ph.N_peers_end;
  int count_a = ph.N_address;
  struct ATS_Address * cur_addr;


  ph.peers = GNUNET_malloc ((count_p) * sizeof (struct PerfPeer));
  for (cp = 0; cp < count_p; cp++)
    perf_create_peer (cp);
  GNUNET_log(GNUNET_ERROR_TYPE_INFO,
      "Iteration %u of %u, added %u peers\n", ph.current_iteration, ph.total_iterations, cp);

  for (cp = 0; cp < count_p; cp++)
  {
    if (GNUNET_NO == ph.bulk_running)
    {
      ph.bulk_running = GNUNET_YES;
      ph.env.sf.s_bulk_start (ph.solver);
    }
    ph.current_p = cp + 1;
    for (ca = 0; ca < count_a; ca++)
    {
      cur_addr = perf_create_address (cp, ca);
      /* Add address */
      ph.env.sf.s_add (ph.solver, cur_addr, GNUNET_CRYPTO_random_u32(GNUNET_CRYPTO_QUALITY_WEAK, GNUNET_ATS_NetworkTypeCount));
      ph.current_a = ca + 1;
      perf_address_initial_update (ph.solver, ph.addresses, cur_addr);
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
          "Adding address for peer %u address %u\n", cp, ca);
    }
    /* Notify solver about request */
    ph.env.sf.s_get (ph.solver, &ph.peers[cp].id);

    if (cp + 1 >= ph.N_peers_start)
    {
      /* Disable bulk to solve the problem */
      if (GNUNET_YES == ph.bulk_running)
      {
        ph.expecting_solution = GNUNET_YES;
        ph.bulk_running = GNUNET_NO;
        ph.env.sf.s_bulk_stop (ph.solver);
      }
      else
        GNUNET_break (0);

      /* Problem is solved by the solver here due to unlocking */
      ph.expecting_solution = GNUNET_NO;

      /* Update the problem */
      if ((0 < ph.opt_update_percent) && (GNUNET_YES == ph.measure_updates))
      {
        /* Update */
        GNUNET_log(GNUNET_ERROR_TYPE_INFO,
            "Updating problem with %u peers and %u addresses\n", cp + 1, ca);

        ph.expecting_solution = GNUNET_YES;
        ph.performed_update = GNUNET_YES;
        if (GNUNET_NO == ph.bulk_running)
        {
          ph.bulk_running = GNUNET_YES;
          ph.env.sf.s_bulk_start (ph.solver);
        }
        perf_update_all_addresses (cp + 1, ca, ph.opt_update_percent);
        ph.bulk_running = GNUNET_NO;
        ph.env.sf.s_bulk_stop (ph.solver);
        /* Problem is solved by the solver here due to unlocking */
        ph.performed_update = GNUNET_NO;
        ph.expecting_solution = GNUNET_NO;
      }
      GNUNET_assert (GNUNET_NO == ph.bulk_running);
    }
  }

  GNUNET_log(GNUNET_ERROR_TYPE_INFO,
      "Done, cleaning up addresses\n");
  if (GNUNET_NO == ph.bulk_running)
  {
    ph.env.sf.s_bulk_start (ph.solver);
    ph.bulk_running = GNUNET_YES;
  }

  for (cp = 0; cp < count_p; cp++)
  {
    for (cur = ph.peers[cp].head; cur != NULL ; cur = next)
    {
      GNUNET_log(GNUNET_ERROR_TYPE_DEBUG,
          "Deleting addresses for peer %u\n", cp);
      GNUNET_assert (GNUNET_OK == GNUNET_CONTAINER_multipeermap_remove (ph.addresses,
          &ph.peers[cp].id, cur));
      ph.env.sf.s_del (ph.solver, cur, GNUNET_NO);
      next = cur->next;
      GNUNET_CONTAINER_DLL_remove(ph.peers[cp].head, ph.peers[cp].tail, cur);
      GNUNET_free(cur);
    }
  }

  GNUNET_log(GNUNET_ERROR_TYPE_INFO,
      "Iteration done\n");
  GNUNET_free(ph.peers);
}


static void
run (void *cls, char * const *args, const char *cfgfile,
    const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  GNUNET_log_setup ("perf-ats-solver", "WARNING", NULL);
  char *sep;
  char *src_filename = GNUNET_strdup (__FILE__);
  char *test_filename = cls;
  char *solver;
  char *plugin;
  struct GNUNET_CONFIGURATION_Handle *solver_cfg;
  unsigned long long quotas_in[GNUNET_ATS_NetworkTypeCount];
  unsigned long long quotas_out[GNUNET_ATS_NetworkTypeCount];
  int c;

  /* Extract test name */
  if (NULL == (sep  = (strstr (src_filename,".c"))))
  {
    GNUNET_free (src_filename);
    GNUNET_break (0);
    ret = 1;
    return ;
  }
  sep[0] = '\0';

  if (NULL != (sep = strstr (test_filename, ".exe")))
    sep[0] = '\0';

  if (NULL == (solver = strstr (test_filename, src_filename)))
  {
    GNUNET_free (src_filename);
    GNUNET_break (0);
    ret = 1;
    return ;
  }
  solver += strlen (src_filename) +1;

  if (0 == strcmp(solver, "proportional"))
  {
    ph.ats_mode = MODE_PROPORTIONAL;
    ph.ats_string = "proportional";
  }
  else if (0 == strcmp(solver, "mlp"))
  {
    ph.ats_mode = MODE_MLP;
    ph.ats_string = "mlp";
  }
  else if ((0 == strcmp(solver, "ril")))
  {
    ph.ats_mode = MODE_RIL;
    ph.ats_string = "ril";
  }
  else
  {
    GNUNET_free (src_filename);
    GNUNET_break (0);
    ret = 1;
    return ;
  }
  GNUNET_free (src_filename);

  /* Calculcate peers */
  if ((0 == ph.N_peers_start) && (0 == ph.N_peers_end))
  {
    ph.N_peers_start = DEFAULT_PEERS_START;
    ph.N_peers_end = DEFAULT_PEERS_END;
  }
  if (0 == ph.N_address)
    ph.N_address = DEFAULT_ADDRESSES;

  if (ph.N_peers_start != ph.N_peers_end)
    fprintf (stderr, "Benchmarking solver `%s' with %u to %u peers and %u addresses in %u iterations\n",
        ph.ats_string, ph.N_peers_start, ph.N_peers_end, ph.N_address, ph.total_iterations);
  else
    fprintf (stderr, "Benchmarking solver `%s' with %u peers and %u addresses in %u iterations\n",
        ph.ats_string, ph.N_peers_end, ph.N_address, ph.total_iterations);

  if (0 == ph.opt_update_percent)
    ph.opt_update_percent = DEFAULT_UPDATE_PERCENTAGE;

  /* Load quotas */
  solver_cfg = GNUNET_CONFIGURATION_create();
  if ((NULL == solver_cfg) || (GNUNET_SYSERR == (GNUNET_CONFIGURATION_load ( solver_cfg, "perf_ats_solver.conf"))))
  {
    GNUNET_break(0);
    end_now (1);
    return;
  }
  if (GNUNET_ATS_NetworkTypeCount != load_quotas (solver_cfg,
      quotas_out, quotas_in, GNUNET_ATS_NetworkTypeCount))
  {
    GNUNET_break(0);
    end_now (1);
    return;
  }

  /* Create array of DLL to store results for iterations */
  ph.iterations_results = GNUNET_malloc (sizeof (struct Iteration) * ph.total_iterations);
  ph.averaged_full_result = GNUNET_malloc (sizeof (struct Result) * ((ph.N_peers_end + 1) - ph.N_peers_start));
  for (c = 0; c <= ph.N_peers_end - ph.N_peers_start; c++)
  {
    ph.averaged_full_result[c].d_setup = GNUNET_TIME_UNIT_FOREVER_REL;
    ph.averaged_full_result[c].d_total = GNUNET_TIME_UNIT_FOREVER_REL;
    ph.averaged_full_result[c].d_lp = GNUNET_TIME_UNIT_FOREVER_REL;
    ph.averaged_full_result[c].d_mlp = GNUNET_TIME_UNIT_FOREVER_REL;
  }
  ph.averaged_update_result = GNUNET_malloc (sizeof (struct Result) * ((ph.N_peers_end + 1) - ph.N_peers_start));
  for (c = 0; c <= ph.N_peers_end - ph.N_peers_start; c++)
  {
    ph.averaged_update_result[c].d_setup = GNUNET_TIME_UNIT_FOREVER_REL;
    ph.averaged_update_result[c].d_total = GNUNET_TIME_UNIT_FOREVER_REL;
    ph.averaged_update_result[c].d_lp = GNUNET_TIME_UNIT_FOREVER_REL;
    ph.averaged_update_result[c].d_mlp = GNUNET_TIME_UNIT_FOREVER_REL;
  }

  /* Load solver */
  ph.env.cfg = solver_cfg;
  ph.stat = GNUNET_STATISTICS_create ("ats", cfg);
  ph.env.stats = ph.stat;
  ph.addresses = GNUNET_CONTAINER_multipeermap_create (128, GNUNET_NO);
  ph.env.addresses = ph.addresses;
  ph.env.bandwidth_changed_cb = bandwidth_changed_cb;
  ph.env.get_preferences = &get_preferences_cb;
  ph.env.get_property = &get_property_cb;
  ph.env.network_count = GNUNET_ATS_NetworkTypeCount;
  ph.env.info_cb = &solver_info_cb;
  ph.env.info_cb_cls = NULL;

  int networks[GNUNET_ATS_NetworkTypeCount] = GNUNET_ATS_NetworkType;
  for (c = 0; c < GNUNET_ATS_NetworkTypeCount; c++)
  {
    ph.env.networks[c] = networks[c];
    ph.env.out_quota[c] = quotas_out[c];
    ph.env.in_quota[c] = quotas_in[c];
    GNUNET_log (GNUNET_ERROR_TYPE_INFO, "Loading network quotas: `%s' %llu %llu \n",
        GNUNET_ATS_print_network_type(ph.env.networks[c]),
        ph.env.out_quota[c],
        ph.env.in_quota[c]);
  }
  GAS_normalization_start (NULL, NULL, &normalized_property_changed_cb, NULL );

  GNUNET_asprintf (&plugin, "libgnunet_plugin_ats_%s", ph.ats_string);
  GNUNET_log(GNUNET_ERROR_TYPE_INFO, _("Initializing solver `%s'\n"), ph.ats_string);
  if  (NULL == (ph.solver = GNUNET_PLUGIN_load (plugin, &ph.env)))
  {
    GNUNET_log(GNUNET_ERROR_TYPE_ERROR, _("Failed to initialize solver `%s'!\n"), plugin);
    ret = 1;
    return;
  }

  /* Do the benchmark */
  for (ph.current_iteration = 1; ph.current_iteration <= ph.total_iterations; ph.current_iteration++)
  {
    fprintf (stderr, "Iteration %u of %u starting\n", ph.current_iteration, ph.total_iterations);
    perf_run_iteration ();
    evaluate (ph.current_iteration);
    fprintf (stderr, "Iteration %u of %u done\n", ph.current_iteration, ph.total_iterations);
  }
  evaluate_average ();

  /* Unload solver*/
  GNUNET_log(GNUNET_ERROR_TYPE_INFO, _("Unloading solver `%s'\n"), ph.ats_string);
  GNUNET_PLUGIN_unload (plugin, ph.solver);
  GNUNET_free (plugin);
  GNUNET_free (ph.iterations_results);
  GNUNET_free (ph.averaged_full_result);
  GNUNET_free (ph.averaged_update_result);
  GNUNET_CONFIGURATION_destroy (solver_cfg);
  GNUNET_STATISTICS_destroy (ph.stat, GNUNET_NO);
  ph.solver = NULL;
}

/**
 * Main function of the benchmark
 *
 * @param argc argument count
 * @param argv argument values
 */
int
main (int argc, char *argv[])
{
  /* extract command line arguments */
  ph.opt_update_percent = 0;
  ph.N_peers_start = 0;
  ph.N_peers_end = 0;
  ph.N_address = 0;
  ph.ats_string = NULL;
  ph.create_plot = GNUNET_NO;
  ph.measure_updates = GNUNET_NO;
  ph.total_iterations = 1;

  static struct GNUNET_GETOPT_CommandLineOption options[] = {
      { 'a', "addresses", NULL,
          gettext_noop ("addresses to use"),
          1, &GNUNET_GETOPT_set_uint, &ph.N_address },
      { 's', "start", NULL,
          gettext_noop ("start with peer"),
          1, &GNUNET_GETOPT_set_uint, &ph.N_peers_start },
      { 'e', "end", NULL,
          gettext_noop ("end with peer"),
          1, &GNUNET_GETOPT_set_uint, &ph.N_peers_end },
      { 'i', "iterations", NULL,
          gettext_noop ("number of iterations used for averaging (default: 1)"),
          1, &GNUNET_GETOPT_set_uint, &ph.total_iterations },
      { 'p', "percentage", NULL,
          gettext_noop ("update a fix percentage of addresses"),
          1, &GNUNET_GETOPT_set_uint, &ph.opt_update_percent },
      { 'g', "gnuplot", NULL,
          gettext_noop ("create GNUplot file"),
          0, &GNUNET_GETOPT_set_one, &ph.create_plot},
      { 'u', "update", NULL,
          gettext_noop ("measure updates"),
          0, &GNUNET_GETOPT_set_one, &ph.measure_updates},
      GNUNET_GETOPT_OPTION_END
  };

  GNUNET_PROGRAM_run (argc, argv, argv[0], NULL, options, &run, argv[0]);
  return ret;
}

/* end of file perf_ats_solver.c */
