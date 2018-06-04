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
 * shard_shm.h - shard shm information
 */

#ifndef	_SHARD_SHM_H_
#define	_SHARD_SHM_H_

#ident "$Id$"


#include "config.h"

#include "broker_config.h"
#include "broker_shm.h"
#include "shard_proxy_common.h"

extern T_PROXY_INFO *shard_shm_find_proxy_info (T_SHM_PROXY * proxy_p, int proxy_id);
extern T_SHARD_INFO *shard_shm_find_shard_info (T_PROXY_INFO * proxy_info_p, int shard_id);
extern T_CLIENT_INFO *shard_shm_get_client_info (T_PROXY_INFO * proxy_info_p, int idx);
extern T_APPL_SERVER_INFO *shard_shm_get_as_info (T_PROXY_INFO * proxy_info_p, T_SHM_APPL_SERVER * shm_as_p,
						  int shard_id, int as_id);
extern bool shard_shm_set_as_client_info (T_PROXY_INFO * proxy_info_p, T_SHM_APPL_SERVER * shm_as_p, int shard_id,
					  int as_id, unsigned int ip_addr, char *driver_info, char *driver_version);
extern bool shard_shm_set_as_client_info_with_db_param (T_PROXY_INFO * proxy_info_p, T_SHM_APPL_SERVER * shm_as_p,
							int shard_id, int as_id, T_CLIENT_INFO * client_info_p);

extern T_SHM_SHARD_CONN_STAT *shard_shm_get_shard_stat (T_PROXY_INFO * proxy_info_p, int idx);
extern T_SHM_SHARD_KEY_STAT *shard_shm_get_key_stat (T_PROXY_INFO * proxy_info_p, int idx);

extern T_SHM_PROXY *shard_shm_initialize_shm_proxy (T_BROKER_INFO * br_info);

extern void shard_shm_dump_appl_server_internal (FILE * fp, T_SHM_PROXY * shm_as_p);
extern void shard_shm_dump_appl_server (FILE * fp, int shmid);

/* SHARD TODO : MV OTHER HEADER FILE */

extern void shard_shm_init_client_info (T_CLIENT_INFO * client_info_p);
extern void shard_shm_init_client_info_request (T_CLIENT_INFO * client_info_p);
extern void shard_shm_set_client_info_request (T_CLIENT_INFO * client_info_p, int func_code);
extern void shard_shm_set_client_info_response (T_CLIENT_INFO * client_info_p);

#endif /* _SHARD_SHM_H_ */
