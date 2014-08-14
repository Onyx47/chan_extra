/*
 * libgsmat: An implementation of OpenVox G400P GSM/CDMA cards
 *
 * Parts taken from libpri
 * Written by mark.liu <mark.liu@openvox.cn>
 *
 * Copyright (C) 2005-2010 OpenVox Communication Co. Ltd,
 * All rights reserved.
 *
 * $Id: gsm_timers.h 60 2010-09-09 07:59:03Z liuyuan $
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 *
 */

 
#ifndef _GSMAT_TIMERS_H
#define _GSMAT_TIMERS_H

/* -1 means we dont currently support the timer/counter */
#define GSM_TIMERS_DEFAULT { \
				3,		/* N200 */ \
				-1,		/* N201 */ \
				3,		/* N202 */ \
				7,		/* K */ \
				1000,	/* T200 */ \
				-1,		/* T201 */ \
				10000,	/* T202 */ \
				10000,	/* T203 */ \
				-1,		/* T300 */ \
				-1,		/* T301 */ \
				-1,		/* T302 */ \
				-1,		/* T303 */ \
				-1,		/* T304 */ \
				30000,	/* T305 */ \
				-1,		/* T306 */ \
				-1,		/* T307 */ \
				4000,	/* T308 */ \
				-1,		/* T309 */ \
				-1,		/* T310 */ \
				4000,	/* T313 */ \
				-1,		/* T314 */ \
				-1,		/* T316 */ \
				-1,		/* T317 */ \
				-1,		/* T318 */ \
				-1,		/* T319 */ \
				-1,		/* T320 */ \
				-1,		/* T321 */ \
				-1,		/* T322 */ \
				2500,	/* TM20 - Q.921 Appendix IV */ \
				3,		/* NM20 - Q.921 Appendix IV */ \
}

/* XXX Only our default timers are setup now XXX */
#define GSM_TIMERS_UNKNOWN			GSM_TIMERS_DEFAULT
#define GSM_TIMERS_NI2				GSM_TIMERS_DEFAULT
#define GSM_TIMERS_DMS100			GSM_TIMERS_DEFAULT
#define GSM_TIMERS_LUCENT5E			GSM_TIMERS_DEFAULT
#define GSM_TIMERS_ATT4ESS			GSM_TIMERS_DEFAULT
#define GSM_TIMERS_EUROISDN_E1		GSM_TIMERS_DEFAULT
#define GSM_TIMERS_EUROISDN_T1		GSM_TIMERS_DEFAULT
#define GSM_TIMERS_NI1				GSM_TIMERS_DEFAULT
#define GSM_TIMERS_GR303_EOC		GSM_TIMERS_DEFAULT
#define GSM_TIMERS_GR303_TMC		GSM_TIMERS_DEFAULT
#define GSM_TIMERS_QSIG				GSM_TIMERS_DEFAULT
#define __GSM_TIMERS_GR303_EOC_INT	GSM_TIMERS_DEFAULT
#define __GSM_TIMERS_GR303_TMC_INT	GSM_TIMERS_DEFAULT

#define GSM_TIMERS_ALL { \
	GSM_TIMERS_UNKNOWN, \
	GSM_TIMERS_NI2, \
	GSM_TIMERS_DMS100, \
	GSM_TIMERS_LUCENT5E, \
	GSM_TIMERS_ATT4ESS, \
	GSM_TIMERS_EUROISDN_E1, \
	GSM_TIMERS_EUROISDN_T1, \
	GSM_TIMERS_NI1, \
	GSM_TIMERS_QSIG, \
	GSM_TIMERS_GR303_EOC, \
	GSM_TIMERS_GR303_TMC, \
	__GSM_TIMERS_GR303_EOC_INT, \
	__GSM_TIMERS_GR303_TMC_INT, \
}

#endif
