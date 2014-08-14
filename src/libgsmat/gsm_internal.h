/*
 * libgsmat: An implementation of OpenVox G400P GSM/CDMA cards
 *
 * Parts taken from libpri
 * Written by mark.liu <mark.liu@openvox.cn>
 *
 * Copyright (C) 2005-2010 OpenVox Communication Co. Ltd,
 * All rights reserved.
 *
 * $Id: gsm_internal.h 294 2011-03-08 07:50:07Z liuyuan $
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 *
 */

 
#ifndef _GSM_INTERNAL_H
#define _GSM_INTERNAL_H

#include <stddef.h>
#if 0
#include <sys/time.h>
#endif

//Freedom Add 2012-02-07 15:24
//////////////////////////////////////////////
//For simcom900d which can't report "NO ANSWER" bug
#define SIMCOM900D_NO_ANSWER_BUG 0
//////////////////////////////////////////////

#define DBGHEAD __FILE__ ":%d %s: "
#define DBGINFO __LINE__,__PRETTY_FUNCTION__

#define DEBUGFMT  "[%s(%d)-%s]  "
#define DEBUGARGS __FILE__,__LINE__,__FUNCTION__

#if 0
struct gsm_sched {
	struct timeval when;
	void (*callback)(void *data);
	void *data;
};
#endif

/* No more than 128 scheduled events */
#define MAX_SCHED	10000//128
#define MAX_TIMERS	32

#define FLAG_PREFERRED 2
#define FLAG_EXCLUSIVE 4

#define CODE_CCITT					0x0
#define CODE_INTERNATIONAL 			0x1
#define CODE_NATIONAL 				0x2
#define CODE_NETWORK_SPECIFIC		0x3

#define LOC_USER						0x0
#define LOC_PRIV_NET_LOCAL_USER			0x1
#define LOC_PUB_NET_LOCAL_USER			0x2
#define LOC_TRANSIT_NET					0x3
#define LOC_PUB_NET_REMOTE_USER			0x4
#define LOC_PRIV_NET_REMOTE_USER		0x5
#define LOC_INTERNATIONAL_NETWORK		0x7
#define LOC_NETWORK_BEYOND_INTERWORKING	0xa

#define GSM_NET_UNREGISTERED	0
#define GSM_NET_HOME			1
#define GSM_NET_SEARCHING		2
#define GSM_NET_DENIED			3
#define GSM_NET_UNKNOWN			4
#define GSM_NET_ROAMING			5
#define GSM_NET_REGISTERED		99

#define CDMA_NET_UNREGISTERED	0
#define CDMA_NET_INIT			1
#define CDMA_NET_IDLE			2
#define CDMA_NET_UPDATE			3
#define CDMA_NET_CALLINGD1		4
#define CDMA_NET_ANSWER1		5
#define CDMA_NET_ANSWER2		6
#define CDMA_NET_REGISTERING	7
#define CDMA_NET_CALLINGD2		8
#define CDMA_NET_WAITING_AT		9
#define CDMA_NET_WAITING_ANSWER	10
#define CDMA_NET_TALKING		11
#define CDMA_NET_RELEASE		12

#if 0
typedef struct sms_txt_info_s {
	struct gsm_modul *gsm;
	int resendsmsidx;
	char message[1024];
	char destination[512];
} sms_txt_info_t;

typedef struct sms_pdu_info_s {
	struct gsm_modul *gsm;
	int resendsmsidx;
	char message[1024];
	char destination[512];	//Freedom Add 2012-01-29 14:30
	int len;
} sms_pdu_info_t;


typedef union {
	sms_txt_info_t txt_info;
	sms_pdu_info_t pdu_info;	
} sms_info_u;

struct gsm_modul {
	int fd;				/* File descriptor for D-Channel */
	gsm_rio_cb read_func;		/* Read data callback */
	gsm_wio_cb write_func;		/* Write data callback */
	void *userdata;
	struct gsm_sched gsm_sched[MAX_SCHED];	/* Scheduled events */
	int span;			/* Span number */
	int debug;			/* Debug stuff */
	int state;			/* State of D-channel */
	int sim_state;		/* State of Sim Card / UIM Card*/
	int switchtype;		/* Switch type */
	int localtype;		/* Local network type (unknown, network, cpe) */
	int remotetype;		/* Remote network type (unknown, network, cpe) */

	/* AT Stuff */
	char *at_last_recv;		/* Last Received command from dchan */
	int at_last_recv_idx;	/* at_lastrecv lenght */
	char at_last_sent[1024];		/* Last sent command to dchan */
	int at_last_sent_idx;	/* at_lastsent lenght */
	char *at_lastsent;
	
	char at_pre_recv[1024];
	
	char pin[16];				/* sim pin */
	char manufacturer[256];			/* gsm modem manufacturer */
	char model_name[256];			/* gsm modem name */
	char revision[256];			/* gsm modem revision */
	char imei[64];				/* span imei */
	char imsi[64];				/* sim imsi */

	int network;			/* 0 unregistered - 1 home - 2 roaming */
	int coverage;			/* net coverage -1 not signal */
	int resetting;
	int retranstimer;		/* Timer for retransmitting DISC */
	char sms_recv_buffer[1024];	/* sms received buffer */	
	char sms_sent_buffer[1024];	/* sms send buffer */
	int sms_sent_len;
	enum sms_mode sms_mod_flag;
	sms_info_u *sms_info;
	
    char sanbuf[4096];
    int sanidx;
	int sanskip;

	char net_name[64];			/* Network friendly name */
	
	int cref;			/* Next call reference value */
	
	int busy;			/* Peer is busy */

	/* Various timers */
	int sabme_timer;	/* SABME retransmit */
	int sabme_count;	/* SABME retransmit counter for BRI */
	/* All ISDN Timer values */
	int timers[MAX_TIMERS];

	/* Used by scheduler */
	struct timeval tv;
	int schedev;
	gsm_event ev;		/* Static event thingy */
	
	/* Q.931 calls */
	struct at_call **callpool;
	struct at_call *localpool;
	
	/*Freedom add 2011-10-14 10:23, "gsm send at" show message*/
	int send_at;

};
#endif

struct gsm_sr {
	int transmode;
	int channel;
	int exclusive;
	int nonisdn;
	char *caller;
	char *callername;
	char *called;
	int userl1;
	int numcomplete;
};


#if 0
/* at_call datastructure */

struct at_call {
	struct gsm_modul *gsm;	/* GSM */
	int cr;				/* Call Reference */
	struct at_call *next;
	int channelno; /* An explicit channel */
	int chanflags; /* Channel flags (0 means none retrieved) */
	
	int alive;			/* Whether or not the call is alive */
	int acked;			/* Whether setup has been acked or not */
	int sendhangupack;	/* Whether or not to send a hangup ack */
	int proc;			/* Whether we've sent a call proceeding / alerting */
	
	/* Bearer Capability */
	int userl1;
	int userl2;
	int userl3;
	
	int sentchannel;

	int progcode;			/* Progress coding */
	int progloc;			/* Progress Location */	
	int progress;			/* Progress indicator */
	int progressmask;		/* Progress Indicator bitmask */
	
	int causecode;			/* Cause Coding */
	int causeloc;			/* Cause Location */
	int cause;				/* Cause of clearing */
	
	int peercallstate;		/* Call state of peer as reported */
	int ourcallstate;		/* Our call state */
	int sugcallstate;		/* Status call state */
	
	char callernum[256];
	char callername[256];

	char keypad_digits[64];		/* Buffer for digits that come in KEYPAD_FACILITY */

	char callednum[256];	/* Called Number */
	int complete;			/* no more digits coming */
	int newcall;			/* if the received message has a new call reference value */

	int t308_timedout;		/* Whether t308 timed out once */
	
	long aoc_units;				/* Advice of Charge Units */

};
#endif

#define AT_RES_HAVEEVENT (1 << 0)
#define AT_RES_INERRROR  (1 << 1)

#define MAX_MAND_IES 10

/* Message type */
struct msgtype {
	int msgnum;
	char *name;
	int mandies[MAX_MAND_IES];
};

/* Call state stuff */
#define AT_CALL_STATE_NULL						0
#define AT_CALL_STATE_CALL_INITIATED			1
#define AT_CALL_STATE_OVERLAP_SENDING			2
#define AT_CALL_STATE_OUTGOING_CALL_PROCEEDING	3
#define AT_CALL_STATE_CALL_DELIVERED			4
#define AT_CALL_STATE_CALL_PRESENT				6
#define AT_CALL_STATE_CALL_RECEIVED				7
#define AT_CALL_STATE_CONNECT_REQUEST			8
#define AT_CALL_STATE_INCOMING_CALL_PROCEEDING	9
#define AT_CALL_STATE_ACTIVE					10
#define AT_CALL_STATE_DISCONNECT_REQUEST		11
#define AT_CALL_STATE_DISCONNECT_INDICATION		12
#define AT_CALL_STATE_SUSPEND_REQUEST			15
#define AT_CALL_STATE_RESUME_REQUEST			17
#define AT_CALL_STATE_RELEASE_REQUEST			19
#define AT_CALL_STATE_OVERLAP_RECEIVING			25
#define AT_CALL_STATE_RESTART_REQUEST			61
#define AT_CALL_STATE_RESTART					62

/* Update call state with transition trace. */
#define UPDATE_OURCALLSTATE(gsm,c,newstate) do {\
	if (gsm->debug & (GSM_DEBUG_AT_STATE) && c->ourcallstate != newstate) \
		gsm_message(gsm, DBGHEAD "call %d on channel %d enters state %d (%s)\n", DBGINFO, \
		            c->cr, c->channelno, newstate, callstate2str(newstate)); \
	c->ourcallstate = newstate; \
	} while (0)



/*
 * from gsmsched.c
 */
 
extern int gsm_schedule_check(struct gsm_modul *gsm);
 
extern int gsm_schedule_event(struct gsm_modul *gsm, int ms, void (*function)(void *data), void *data);

extern gsm_event *gsm_schedule_run(struct gsm_modul *gsm);

extern void gsm_schedule_del(struct gsm_modul *gsm, int ev);

/*
 * from gsm.c
 */

extern char *callstate2str(int callstate);
extern struct gsm_modul *__gsm_new_tei(int fd, int node, int switchtype, int span, gsm_rio_cb rd, gsm_wio_cb wr, void *userdata, int debug_at);



extern void __gsm_free_tei(struct gsm_modul *p);

extern void gsm_dump(struct gsm_modul *gsm, const char *h, int len, int txrx);

//Freedom Add 2010-10-10 16:03
extern int gsm_send_at(struct gsm_modul *gsm, const char *at);
extern int gsm_transmit(struct gsm_modul *gsm, const char *at);

extern int gsm_transmit_data(struct gsm_modul *gsm, const char *data, int len);

#if 0
extern struct at_call *gsm_getcall(struct gsm_modul *gsm, int cr, int outboundnew);
#endif

extern int gsm_compare(const char *str_at, const char *str_cmp);

extern int gsm_trim(const char *in, int in_len, char *out, int out_len);

extern void gsm_get_manufacturer(struct gsm_modul *gsm, char *h);

extern void gsm_get_smsc(struct gsm_modul *gsm, char *h);

extern void gsm_get_model_name(struct gsm_modul *gsm, char *h);

extern void gsm_get_model_version(struct gsm_modul *gsm, char *h);

extern void gsm_get_imsi(struct gsm_modul *gsm, char *h); 

extern void gsm_get_imei(struct gsm_modul *gsm,char *h);

extern void gsm_get_operator(struct gsm_modul *gsm, char *buf);

extern gsm_event *gsm_mkerror(struct gsm_modul *gsm, char *errstr);

extern void gsm_message(struct gsm_modul *gsm, char *fmt, ...);

extern void gsm_error(struct gsm_modul *gsm, char *fmt, ...);

extern void sms_send_ok(struct gsm_modul *gsm);

extern int gsm_switch_state(struct gsm_modul *gsm, int state, const char *next_command);
	
extern int gsm_switch_sim_state(struct gsm_modul *gsm, int state, char *next_command);

extern unsigned long gsm_hex2int(const char *hex, int len);

extern int gsm_pdu2sm_event(struct gsm_modul *gsm, char *pdu);

extern int gsm_ussd_event(struct gsm_modul *gsm, char *ussd) ;


extern int sms_get_str(struct gsm_modul *gsm,char *in, size_t inlen, char *out, size_t outlen);

extern int sms_set_str(char *in, char *out);

extern int gsm_text2sm_event(struct gsm_modul *gsm, char *sms_info);
extern int gsm_text2sm_event2(struct gsm_modul *gsm, char *sms_info, char *sms_body);

extern int gsm_sms_send_text(struct gsm_modul *gsm, char *destination, char *msg);

extern int gsm_sms_send_pdu(struct gsm_modul *gsm, char *pdu);

extern enum sms_mode gsm_check_sms_mode(struct gsm_modul *gsm, char *sms_info);

extern int gsm_san(struct gsm_modul *gsm, char *in, char *out, int len);


//Freedom Add 2012-01-29 15:48
extern char* pdu_get_send_number(const char* pdu, char* number, int len);

#endif

