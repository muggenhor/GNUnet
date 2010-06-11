/*
     This file is part of GNUnet.
     (C) 2001, 2002, 2003, 2005, 2006 Christian Grothoff (and other contributing authors)

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
 * @file util/os_load.c
 * @brief calls to determine current CPU load
 * @author Tzvetan Horozov
 * @author Christian Grothoff
 * @author Igor Wronsky
 * @author Alex Harper (OS X portion)
 */

#include "platform.h"
#include "gnunet_common.h"
#include "gnunet_os_lib.h"
#include "gnunet_strings_lib.h"

#if SOLARIS
#if HAVE_KSTAT_H
#include <kstat.h>
#endif
#if HAVE_SYS_SYSINFO_H
#include <sys/sysinfo.h>
#endif
#if HAVE_KVM_H
#include <kvm.h>
#endif
#endif
#if SOMEBSD
#if HAVE_KVM_H
#include <kvm.h>
#endif
#endif

#ifdef DARWIN
#include <mach/mach.h>

static processor_cpu_load_info_t prev_cpu_load;
#endif

#define DEBUG_STATUSCALLS GNUNET_NO

#ifdef LINUX
/**
 * File descriptor for reading /proc/stat (or NULL)
 */
static FILE *proc_stat;

/**
 * Is this the first time we're trying to open /proc/stat?  If
 * not, we don't try again if we failed...
 */
static int first_time;
#endif

/**
 * Current CPU load, as percentage of CPU cycles not idle or
 * blocked on IO.
 */
static int currentCPULoad;

static double agedCPULoad = -1;

/**
 * Current IO load, as percentage of CPU cycles blocked on IO.
 */
static int currentIOLoad;

static double agedIOLoad = -1;

#ifdef DARWIN
static int
initMachCpuStats ()
{
  unsigned int cpu_count;
  processor_cpu_load_info_t cpu_load;
  mach_msg_type_number_t cpu_msg_count;
  kern_return_t kret;
  int i, j;

  kret = host_processor_info (mach_host_self (),
                              PROCESSOR_CPU_LOAD_INFO,
                              &cpu_count,
                              (processor_info_array_t *) & cpu_load,
                              &cpu_msg_count);
  if (kret != KERN_SUCCESS)
    {
      GNUNET_log (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK,
                  "host_processor_info failed.");
      return GNUNET_SYSERR;
    }
  prev_cpu_load = GNUNET_malloc (cpu_count * sizeof (*prev_cpu_load));
  for (i = 0; i < cpu_count; i++)
    {
      for (j = 0; j < CPU_STATE_MAX; j++)
        {
          prev_cpu_load[i].cpu_ticks[j] = cpu_load[i].cpu_ticks[j];
        }
    }
  vm_deallocate (mach_task_self (),
                 (vm_address_t) cpu_load,
                 (vm_size_t) (cpu_msg_count * sizeof (*cpu_load)));
  return GNUNET_OK;
}
#endif


/**
 * Update the currentCPU and currentIO load values.
 *
 * Before its first invocation the method initStatusCalls() must be called.
 * If there is an error the method returns -1.
 */
static int
updateUsage ()
{
  currentIOLoad = -1;
  currentCPULoad = -1;
#ifdef LINUX
  /* under linux, first try %idle/usage using /proc/stat;
     if that does not work, disable /proc/stat for the future
     by closing the file and use the next-best method. */
  if (0 == first_time)
    {
      first_time = 1;
      proc_stat = fopen ("/proc/stat", "r");
      if (NULL == proc_stat)
	GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_ERROR, "fopen", "/proc/stat");
    }
  if (proc_stat != NULL)
    {
      static unsigned long long last_cpu_results[5] = { 0, 0, 0, 0, 0 };
      static int have_last_cpu = GNUNET_NO;
      int ret;
      char line[256];
      unsigned long long user_read, system_read, nice_read, idle_read,
        iowait_read;
      unsigned long long user, system, nice, idle, iowait;
      unsigned long long usage_time = 0, total_time = 1;

      /* Get the first line with the data */
      rewind (proc_stat);
      fflush (proc_stat);
      if (NULL == fgets (line, 256, proc_stat))
        {
          GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_ERROR |
                                    GNUNET_ERROR_TYPE_BULK,
                                    "fgets", "/proc/stat");
          fclose (proc_stat);
          proc_stat = NULL;     /* don't try again */
        }
      else
        {
          iowait_read = 0;
          ret = sscanf (line, "%*s %llu %llu %llu %llu %llu",
                        &user_read,
                        &system_read, &nice_read, &idle_read, &iowait_read);
          if (ret < 4)
            {
              GNUNET_log_strerror_file (GNUNET_ERROR_TYPE_ERROR |
                                        GNUNET_ERROR_TYPE_BULK,
                                        "fgets-sscanf", "/proc/stat");
              fclose (proc_stat);
              proc_stat = NULL; /* don't try again */
              have_last_cpu = GNUNET_NO;
            }
          else
            {
              /* Store the current usage */
              user = user_read - last_cpu_results[0];
              system = system_read - last_cpu_results[1];
              nice = nice_read - last_cpu_results[2];
              idle = idle_read - last_cpu_results[3];
              iowait = iowait_read - last_cpu_results[4];
              /* Calculate the % usage */
              usage_time = user + system + nice;
              total_time = usage_time + idle + iowait;
              if ((total_time > 0) && (have_last_cpu == GNUNET_YES))
                {
                  currentCPULoad = (int) (100L * usage_time / total_time);
                  if (ret > 4)
                    currentIOLoad = (int) (100L * iowait / total_time);
                  else
                    currentIOLoad = -1; /* 2.4 kernel */
                }
              /* Store the values for the next calculation */
              last_cpu_results[0] = user_read;
              last_cpu_results[1] = system_read;
              last_cpu_results[2] = nice_read;
              last_cpu_results[3] = idle_read;
              last_cpu_results[4] = iowait_read;
              have_last_cpu = GNUNET_YES;
              return GNUNET_OK;
            }
        }
    }
#endif

#ifdef DARWIN
  {
    unsigned int cpu_count;
    processor_cpu_load_info_t cpu_load;
    mach_msg_type_number_t cpu_msg_count;
    unsigned long long t_sys, t_user, t_nice, t_idle, t_total;
    unsigned long long t_idle_all, t_total_all;
    kern_return_t kret;
    int i, j;

    t_idle_all = t_total_all = 0;
    kret = host_processor_info (mach_host_self (), PROCESSOR_CPU_LOAD_INFO,
                                &cpu_count,
                                (processor_info_array_t *) & cpu_load,
                                &cpu_msg_count);
    if (kret == KERN_SUCCESS)
      {
        for (i = 0; i < cpu_count; i++)
          {
            if (cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM] >=
                prev_cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM])
              {
                t_sys = cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM] -
                  prev_cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM];
              }
            else
              {
                t_sys = cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM] +
                  (ULONG_MAX - prev_cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM] +
                   1);
              }

            if (cpu_load[i].cpu_ticks[CPU_STATE_USER] >=
                prev_cpu_load[i].cpu_ticks[CPU_STATE_USER])
              {
                t_user = cpu_load[i].cpu_ticks[CPU_STATE_USER] -
                  prev_cpu_load[i].cpu_ticks[CPU_STATE_USER];
              }
            else
              {
                t_user = cpu_load[i].cpu_ticks[CPU_STATE_USER] +
                  (ULONG_MAX - prev_cpu_load[i].cpu_ticks[CPU_STATE_USER] +
                   1);
              }

            if (cpu_load[i].cpu_ticks[CPU_STATE_NICE] >=
                prev_cpu_load[i].cpu_ticks[CPU_STATE_NICE])
              {
                t_nice = cpu_load[i].cpu_ticks[CPU_STATE_NICE] -
                  prev_cpu_load[i].cpu_ticks[CPU_STATE_NICE];
              }
            else
              {
                t_nice = cpu_load[i].cpu_ticks[CPU_STATE_NICE] +
                  (ULONG_MAX - prev_cpu_load[i].cpu_ticks[CPU_STATE_NICE] +
                   1);
              }

            if (cpu_load[i].cpu_ticks[CPU_STATE_IDLE] >=
                prev_cpu_load[i].cpu_ticks[CPU_STATE_IDLE])
              {
                t_idle = cpu_load[i].cpu_ticks[CPU_STATE_IDLE] -
                  prev_cpu_load[i].cpu_ticks[CPU_STATE_IDLE];
              }
            else
              {
                t_idle = cpu_load[i].cpu_ticks[CPU_STATE_IDLE] +
                  (ULONG_MAX - prev_cpu_load[i].cpu_ticks[CPU_STATE_IDLE] +
                   1);
              }
            t_total = t_sys + t_user + t_nice + t_idle;
            t_idle_all += t_idle;
            t_total_all += t_total;
          }
        for (i = 0; i < cpu_count; i++)
          {
            for (j = 0; j < CPU_STATE_MAX; j++)
              {
                prev_cpu_load[i].cpu_ticks[j] = cpu_load[i].cpu_ticks[j];
              }
          }
        if (t_total_all > 0)
          currentCPULoad = 100 - (100 * t_idle_all) / t_total_all;
        else
          currentCPULoad = -1;
        vm_deallocate (mach_task_self (),
                       (vm_address_t) cpu_load,
                       (vm_size_t) (cpu_msg_count * sizeof (*cpu_load)));
        currentIOLoad = -1;     /* There's no IO load meter on darwin */
        return GNUNET_OK;
      }
    else
      {
        GNUNET_log (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK,
                    "host_processor_info failed.");
        return GNUNET_SYSERR;
      }
  }
#endif
  /* try kstat (Solaris only) */
#if SOLARIS && HAVE_KSTAT_H && HAVE_SYS_SYSINFO_H
  {
    static long long last_idlecount;
    static long long last_totalcount;
    static int kstat_once;      /* if open fails, don't keep
                                   trying */
    kstat_ctl_t *kc;
    kstat_t *khelper;
    long long idlecount;
    long long totalcount;
    long long deltaidle;
    long long deltatotal;

    if (kstat_once == 1)
      goto ABORT_KSTAT;
    kc = kstat_open ();
    if (kc == NULL)
      {
        GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK,
                             "kstat_open");
        goto ABORT_KSTAT;
      }

    idlecount = 0;
    totalcount = 0;
    for (khelper = kc->kc_chain; khelper != NULL; khelper = khelper->ks_next)
      {
        cpu_stat_t stats;

        if (0 != strncmp (khelper->ks_name, "cpu_stat", strlen ("cpu_stat")))
          continue;
        if (khelper->ks_data_size > sizeof (cpu_stat_t))
          continue;             /* better save then sorry! */
        if (-1 != kstat_read (kc, khelper, &stats))
          {
            idlecount += stats.cpu_sysinfo.cpu[CPU_IDLE];
            totalcount
              += stats.cpu_sysinfo.cpu[CPU_IDLE] +
              stats.cpu_sysinfo.cpu[CPU_USER] +
              stats.cpu_sysinfo.cpu[CPU_KERNEL] +
              stats.cpu_sysinfo.cpu[CPU_WAIT];
          }
      }
    if (0 != kstat_close (kc))
      GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK,
                           "kstat_close");
    if ((idlecount == 0) && (totalcount == 0))
      goto ABORT_KSTAT;         /* no stats found => abort */
    deltaidle = idlecount - last_idlecount;
    deltatotal = totalcount - last_totalcount;
    if ((deltatotal > 0) && (last_totalcount > 0))
      {
        currentCPULoad = (unsigned int) (100.0 * deltaidle / deltatotal);
        if (currentCPULoad > 100)
          currentCPULoad = 100; /* odd */
        if (currentCPULoad < 0)
          currentCPULoad = 0;   /* odd */
        currentCPULoad = 100 - currentCPULoad;  /* computed idle-load before! */
      }
    else
      currentCPULoad = -1;
    currentIOLoad = -1;         /* FIXME-SOLARIS! */
    last_idlecount = idlecount;
    last_totalcount = totalcount;
    return GNUNET_OK;
  ABORT_KSTAT:
    kstat_once = 1;             /* failed, don't try again */
    return GNUNET_SYSERR;
  }
#endif

  /* insert methods better than getloadavg for
     other platforms HERE! */

  /* ok, maybe we have getloadavg on this platform */
#if HAVE_GETLOADAVG
  {
    static int warnOnce = 0;
    double loadavg;
    if (1 != getloadavg (&loadavg, 1))
      {
        /* only warn once, if there is a problem with
           getloadavg, we're going to hit it frequently... */
        if (warnOnce == 0)
          {
            warnOnce = 1;
            GNUNET_log_strerror (GNUNET_ERROR_TYPE_ERROR, "getloadavg");
          }
        return GNUNET_SYSERR;
      }
    else
      {
        /* success with getloadavg */
        currentCPULoad = (int) (100 * loadavg);
        currentIOLoad = -1;     /* FIXME */
        return GNUNET_OK;
      }
  }
#endif

#if MINGW
  /* Win NT? */
  if (GNNtQuerySystemInformation)
    {
      static double dLastKernel;
      static double dLastIdle;
      static double dLastUser;
      double dKernel;
      double dIdle;
      double dUser;
      double dDiffKernel;
      double dDiffIdle;
      double dDiffUser;
      SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION theInfo;

      if (GNNtQuerySystemInformation (SystemProcessorPerformanceInformation,
                                      &theInfo,
                                      sizeof (theInfo), NULL) == NO_ERROR)
        {
          /* PORT-ME MINGW: Multi-processor? */
          dKernel = Li2Double (theInfo.KernelTime);
          dIdle = Li2Double (theInfo.IdleTime);
          dUser = Li2Double (theInfo.UserTime);
          dDiffKernel = dKernel - dLastKernel;
          dDiffIdle = dIdle - dLastIdle;
          dDiffUser = dUser - dLastUser;

          if (((dDiffKernel + dDiffUser) > 0) &&
              (dLastIdle + dLastKernel + dLastUser > 0))
            currentCPULoad =
              100.0 - (dDiffIdle / (dDiffKernel + dDiffUser)) * 100.0;
          else
            currentCPULoad = -1;        /* don't know (yet) */

          dLastKernel = dKernel;
          dLastIdle = dIdle;
          dLastUser = dUser;

          currentIOLoad = -1;   /* FIXME-MINGW */
          return GNUNET_OK;
        }
      else
        {
          /* only warn once, if there is a problem with
             NtQuery..., we're going to hit it frequently... */
          static int once;
          if (once == 0)
            {
              once = 1;
              GNUNET_log (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK,
                          _("Cannot query the CPU usage (Windows NT).\n"));
            }
          return GNUNET_SYSERR;
        }
    }
  else
    {                           /* Win 9x */
      HKEY hKey;
      DWORD dwDataSize, dwType, dwDummy;

      /* Start query */
      if (RegOpenKeyEx (HKEY_DYN_DATA,
                        "PerfStats\\StartSrv",
                        0, KEY_ALL_ACCESS, &hKey) != ERROR_SUCCESS)
        {
          /* only warn once */
          static int once = 0;
          if (once == 0)
            {
              once = 1;
              GNUNET_log (GNUNET_ERROR_TYPE_ERROR | GNUNET_ERROR_TYPE_BULK,
                          _("Cannot query the CPU usage (Win 9x)\n"));
            }
        }

      RegOpenKeyEx (HKEY_DYN_DATA,
                    "PerfStats\\StartStat", 0, KEY_ALL_ACCESS, &hKey);
      dwDataSize = sizeof (dwDummy);
      RegQueryValueEx (hKey,
                       "KERNEL\\CPUUsage",
                       NULL, &dwType, (LPBYTE) & dwDummy, &dwDataSize);
      RegCloseKey (hKey);

      /* Get CPU usage */
      RegOpenKeyEx (HKEY_DYN_DATA,
                    "PerfStats\\StatData", 0, KEY_ALL_ACCESS, &hKey);
      dwDataSize = sizeof (currentCPULoad);
      RegQueryValueEx (hKey,
                       "KERNEL\\CPUUsage",
                       NULL, &dwType, (LPBYTE) & currentCPULoad, &dwDataSize);
      RegCloseKey (hKey);
      currentIOLoad = -1;       /* FIXME-MINGW! */

      /* Stop query */
      RegOpenKeyEx (HKEY_DYN_DATA,
                    "PerfStats\\StopStat", 0, KEY_ALL_ACCESS, &hKey);
      RegOpenKeyEx (HKEY_DYN_DATA,
                    "PerfStats\\StopSrv", 0, KEY_ALL_ACCESS, &hKey);
      dwDataSize = sizeof (dwDummy);
      RegQueryValueEx (hKey,
                       "KERNEL\\CPUUsage",
                       NULL, &dwType, (LPBYTE) & dwDummy, &dwDataSize);
      RegCloseKey (hKey);

      return GNUNET_OK;
    }
#endif

  /* loadaverage not defined and no platform
     specific alternative defined
     => default: error
   */
  return GNUNET_SYSERR;
}

/**
 * Update load values (if enough time has expired),
 * including computation of averages.  Code assumes
 * that lock has already been obtained.
 */
static void
updateAgedLoad (const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  static struct GNUNET_TIME_Absolute lastCall;

  if ((agedCPULoad == -1)
      || (GNUNET_TIME_absolute_get_duration (lastCall).value > 500))
    {
      /* use smoothing, but do NOT update lastRet at frequencies higher
         than 500ms; this makes the smoothing (mostly) independent from
         the frequency at which getCPULoad is called (and we don't spend
         more time measuring CPU than actually computing something). */
      lastCall = GNUNET_TIME_absolute_get ();
      updateUsage ();
      if (currentCPULoad == -1)
        {
          agedCPULoad = -1;
        }
      else
        {
          if (agedCPULoad == -1)
            {
              agedCPULoad = currentCPULoad;
            }
          else
            {
              /* for CPU, we don't do the 'fast increase' since CPU is much
                 more jitterish to begin with */
              agedCPULoad = (agedCPULoad * 31 + currentCPULoad) / 32;
            }
        }
      if (currentIOLoad == -1)
        {
          agedIOLoad = -1;
        }
      else
        {
          if (agedIOLoad == -1)
            {
              agedIOLoad = currentIOLoad;
            }
          else
            {
              /* for IO, we don't do the 'fast increase' since IO is much
                 more jitterish to begin with */
              agedIOLoad = (agedIOLoad * 31 + currentIOLoad) / 32;
            }
        }
    }
}

/**
 * Get the load of the CPU relative to what is allowed.
 * @return the CPU load as a percentage of allowed
 *        (100 is equivalent to full load)
 */
int
GNUNET_OS_load_cpu_get (const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  unsigned long long maxCPULoad;
  int ret;

  updateAgedLoad (cfg);
  ret = agedCPULoad;
  if (ret == -1)
    return -1;
  if (GNUNET_OK !=
      GNUNET_CONFIGURATION_get_value_number (cfg, "LOAD", "MAXCPULOAD",
                                             &maxCPULoad))
    return GNUNET_SYSERR;
  if (maxCPULoad == 0)
    return 100;
  return (100 * ret) / maxCPULoad;
}


/**
 * Get the load of the CPU relative to what is allowed.
 * @return the CPU load as a percentage of allowed
 *        (100 is equivalent to full load)
 */
int
GNUNET_OS_load_disk_get (const struct GNUNET_CONFIGURATION_Handle *cfg)
{
  unsigned long long maxIOLoad;
  int ret;

  updateAgedLoad (cfg);
  ret = agedIOLoad;
  if (ret == -1)
    return -1;
  if (-1 ==
      GNUNET_CONFIGURATION_get_value_number (cfg, "LOAD", "MAXIOLOAD",
                                             &maxIOLoad))
    return GNUNET_SYSERR;
  return (100 * ret) / maxIOLoad;
}

/**
 * The following method is called in order to initialize the status calls
 * routines.  After that it is safe to call each of the status calls separately
 * @return GNUNET_OK on success and GNUNET_SYSERR on error (or calls errexit).
 */
void __attribute__ ((constructor)) GNUNET_cpustats_ltdl_init ()
{
#ifdef LINUX
  updateUsage ();               /* initialize */
  /* Most GNUnet processes don't really need this, so close the FD, but allow 
     re-opening it (unless we failed) */
  if (proc_stat != NULL)
    {
      GNUNET_break (0 == fclose (proc_stat));
      proc_stat = NULL;
      first_time = 0;
    }
#elif defined(DARWIN)
  initMachCpuStats ();
  updateUsage ();               /* initialize */
#elif MINGW
  InitWinEnv (NULL);
  updateUsage ();               /* initialize */
#else
  updateUsage ();               /* initialize */
#endif
}

/**
 * Shutdown the status calls module.
 */
void __attribute__ ((destructor)) GNUNET_cpustats_ltdl_fini ()
{
#ifdef LINUX
  if (proc_stat != NULL)
    {
      fclose (proc_stat);
      proc_stat = NULL;
    }
#elif defined(DARWIN)
  GNUNET_free_non_null (prev_cpu_load);
#elif MINGW
  ShutdownWinEnv ();
#endif
}


/* end of os_load.c */
