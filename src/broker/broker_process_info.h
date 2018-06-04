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
 * broker_process_info.h - 
 */

#ifndef	_BROKER_PROCESS_INFO_H_
#define	_BROKER_PROCESS_INFO_H_

#ident "$Id$"

#ifdef GET_PSINFO
#if !defined(SOLARIS) && !defined(HPUX)
#error NOT IMPLEMENTED
#endif
#endif

typedef struct t_psinfo T_PSINFO;
struct t_psinfo
{
  int num_thr;
  int cpu_time;
  float pcpu;
};

int get_psinfo (int pid, T_PSINFO * ps_info);

#endif /* _BROKER_PROCESS_INFO_H_ */
