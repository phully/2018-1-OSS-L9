/*
 * Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */


/*
 * broker_util.h -
 */

#ifndef	_BROKER_UTIL_H_
#define	_BROKER_UTIL_H_

#ident "$Id$"

#include <time.h>

#include "cas_common.h"
#include "porting.h"

#if !defined(PROXY_INVALID_ID)
#define PROXY_INVALID_ID	(-1)
#endif
#if !defined(PROXY_INVALID_SHARD)
#define PROXY_INVALID_SHARD	(PROXY_INVALID_ID)
#endif
#if !defined(PROXY_INVALID_CAS)
#define PROXY_INVALID_CAS 	(PROXY_INVALID_ID)
#endif

#define SHARD_INVALID_ID	(PROXY_INVALID_SHARD)
#define CAS_INVALID_ID		(PROXY_INVALID_CAS)

#define	SERVICE_READY_WAIT_COUNT	6000

#define MAKE_FILEPATH(dest,src,dest_len) \
  do { \
      char _buf[BROKER_PATH_MAX]; \
      if ((src) == NULL || (src)[0] == 0) { \
	  (dest)[0] = 0; \
      } else if (realpath ((src), _buf) != NULL) { \
	  strncpy ((dest), _buf, (dest_len)); \
	  (dest)[(dest_len) - 1] = 0; \
      } else { \
	  strncpy ((dest), (src), (dest_len)); \
	  (dest)[(dest_len) - 1] = 0; \
      } \
  } while (0)

#if defined (ENABLE_UNUSED_FUNCTION)
extern int ut_file_lock (char *lock_file);
extern void ut_file_unlock (char *);
extern int ut_access_log (int as_index, struct timeval *start_time, char err_flag, int e_offset);
#endif
extern int ut_kill_process (int pid);
extern int ut_kill_broker_process (int pid, char *br_name);
extern int ut_kill_proxy_process (int pid, char *br_name, int proxy_id);
extern int ut_kill_as_process (int pid, char *br_name, int as_id, int shard_flag);

extern void ut_cd_work_dir (void);
extern void ut_cd_root_dir (void);

extern int ut_set_keepalive (int sock);

#if defined(WINDOWS)
extern int run_child (const char *appl_name);
#endif

extern void as_pid_file_create (char *br_name, int as_index);
extern void as_db_err_log_set (char *br_name, int proxy_index, int shard_id, int shard_cas_id, int as_index,
			       int shard_flag);

extern void ut_get_as_pid_name (char *pid_name, char *br_name, int as_index, int len);

extern int ut_time_string (char *buf, struct timeval *log_time);
extern char *ut_get_ipv4_string (char *ip_str, int len, const unsigned char *ip_addr);
extern float ut_get_avg_from_array (int array[], int size);
extern bool ut_is_appl_server_ready (int pid, char *ready_flag);
extern void ut_get_broker_port_name (char *port_name, char *broker_name, int len);
extern void ut_get_proxy_port_name (char *port_name, char *broker_name, int proxy_id, int len);
extern void ut_get_as_port_name (char *port_name, char *broker_name, int as_id, int len);

extern double ut_size_string_to_kbyte (const char *size_str, const char *default_unit);
extern double ut_time_string_to_sec (const char *time_str, const char *default_unit);

#endif /* _BROKER_UTIL_H_ */
