/*
 * libgsmat: An implementation of OpenVox G400P GSM/CDMA cards
 *
 * Parts taken from libpri
 * Written by mark.liu <mark.liu@openvox.cn>
 *
 * Copyright (C) 2005-2010 OpenVox Communication Co. Ltd,
 * All rights reserved.
 *
 * $Id: gsmsched.c 284 2011-02-28 10:30:09Z liuyuan $
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 *
 */


#include <stdlib.h>
#include <stdio.h>

#include "libgsmat.h"
#include "gsm_internal.h"

static int maxsched = 0;

/* Scheduler routines */

/******************************************************************************
 * set schedule
 * param:
 *		gsm: struct gsm_modul
 *		ms: delay (ms)
 *		function: set callback function 
 *		data: user data
 * return:
 *		int: schedule id (0 < id < MAX_SCHED)
 * e.g.
 *		gsm->restart_timer = gsm_schedule_event(gsm, 10000, em200_error_hard, gsm);
 ******************************************************************************/

int gsm_schedule_check(struct gsm_modul *gsm)
{
	int res = 0;
	int x;

	for (x=1;x<MAX_SCHED;x++) { /* [1, 127] */
		if (!gsm->gsm_sched[x].callback) {
			break;
		}
	}
	
	if (x == MAX_SCHED) {
		gsm_error(gsm, "No more room in scheduler\n");
		res = -1;
	}

	return res;
}

int gsm_schedule_event(struct gsm_modul *gsm, int ms, void (*function)(void *data), void *data)
{
	int x;
	struct timeval tv;
	
	for (x=1;x<MAX_SCHED;x++) { /* [1, 127] */
		if (!gsm->gsm_sched[x].callback) {
			break;
		}
	}
	
	if (x == MAX_SCHED) {
		gsm_error(gsm, "No more room in scheduler\n");
		return -1;
	}

	if (x > maxsched) {
		maxsched = x; /* [1, 127] */
	}

	/* Get current time */
	gettimeofday(&tv, NULL);

	/* Get the schedule end time */
	tv.tv_sec += ms / 1000;
	tv.tv_usec += (ms % 1000) * 1000;
	if (tv.tv_usec > 1000000) {
		tv.tv_usec -= 1000000;
		tv.tv_sec += 1;
	}

	/* Set schedule */
	gsm->gsm_sched[x].when = tv;			/* end time */
	gsm->gsm_sched[x].callback = function;	/* callback function */
	gsm->gsm_sched[x].data = data;			/* data */

	/* return schedule id */
	return x;
}


/******************************************************************************
 * get next schedule time
 * param:
 *		gsm: struct gsm_modul
 *		ms: delay (ms)
 *		function: set callback function 
 *		data: user data
 * return:
 *		int: schedule id (0 < id < MAX_SCHED)
 *		NULL: no schedule found
 * e.g.
 *		gsm->restart_timer = gsm_schedule_event(gsm, 10000, em200_error_hard, gsm);
 ******************************************************************************/
struct timeval *gsm_schedule_next(struct gsm_modul *gsm)
{
	struct timeval *closest = NULL;
	int x;
	
	for (x=1;x<MAX_SCHED;x++) {
		if ((gsm->gsm_sched[x].callback) && 
			(!closest || (closest->tv_sec > gsm->gsm_sched[x].when.tv_sec) || 
						 ((closest->tv_sec == gsm->gsm_sched[x].when.tv_sec) && 
							(closest->tv_usec > gsm->gsm_sched[x].when.tv_usec)))
		)
		closest = &gsm->gsm_sched[x].when;
	}
	
	return closest;
}


/******************************************************************************
 * run schedule
 * param:
 *		gsm: struct gsm_modul
 *		tv: current time
 * return:
 *		union gsm_event
 ******************************************************************************/
static gsm_event *__gsm_schedule_run(struct gsm_modul *gsm, struct timeval *tv)
{
	int x;
	void (*callback)(void *);
	void *data;
	for (x=1;x<MAX_SCHED;x++) {
		if (gsm->gsm_sched[x].callback &&
			((gsm->gsm_sched[x].when.tv_sec < tv->tv_sec) ||
			 ((gsm->gsm_sched[x].when.tv_sec == tv->tv_sec) &&
			  (gsm->gsm_sched[x].when.tv_usec <= tv->tv_usec)))) {
			/* get callback and data */  
			gsm->schedev = 0;
			callback = gsm->gsm_sched[x].callback;
			data = gsm->gsm_sched[x].data;

			/* clear gsm_sched info */
			gsm->gsm_sched[x].callback = NULL;
			gsm->gsm_sched[x].data = NULL;

			/* call schedule routin */
			callback(data);

			/* get gsm_event */
			if (gsm->schedev) {
				return &gsm->ev;
			}
		}
	}
	return NULL;
}


/******************************************************************************
 * run schedule
 * param:
 *		gsm: struct gsm_modul
 * return:
 *		union gsm_event
 ******************************************************************************/
gsm_event *gsm_schedule_run(struct gsm_modul *gsm)
{
	struct timeval tv;

	/* Get current time without tz */
	gettimeofday(&tv, NULL);

	/* run schedule */
	return __gsm_schedule_run(gsm, &tv);
}


/******************************************************************************
 * delete schedule
 * param:
 *		gsm: struct gsm_modul
 * return:
 *		void
 * e.g.
 *		gsm_schedule_del(gsm, gsm->retranstimer);
 ******************************************************************************/
void gsm_schedule_del(struct gsm_modul *gsm,int id)
{
	if ((id >= MAX_SCHED) || (id < 0)) { /* [0, 127] */
		gsm_error(gsm, "Asked to delete sched id %d???\n", id);
	}
	
	gsm->gsm_sched[id].callback = NULL;
}

