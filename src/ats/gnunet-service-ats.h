/*
     This file is part of GNUnet.
     Copyright (C) 2011 Christian Grothoff (and other contributing authors)

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
 * @file ats/gnunet-service-ats.h
 * @brief ats service
 * @author Matthias Wachs
 * @author Christian Grothoff
 */
#ifndef GNUNET_SERVICE_ATS_H
#define GNUNET_SERVICE_ATS_H

#include "gnunet_statistics_service.h"

#define GAS_normalization_queue_length 3

#define BANDWIDTH_ZERO GNUNET_BANDWIDTH_value_init (0)

/**
 * Handle for statistics.
 */
extern struct GNUNET_STATISTICS_Handle *GSA_stats;


#endif
