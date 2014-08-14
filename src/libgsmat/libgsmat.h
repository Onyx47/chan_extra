/*
 * libgsmat: An implementation of OpenVox G400P GSM/CDMA cards
 *
 * Parts taken from libpri
 * Written by mark.liu <mark.liu@openvox.cn>
 *
 * Copyright (C) 2005-2010 OpenVox Communication Co. Ltd,
 * All rights reserved.
 *
 * $Id: libgsmat.h 294 2011-03-08 07:50:07Z liuyuan $
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 *
 */

 
#ifndef _LIBGSMAT_H
#define _LIBGSMAT_H
#include <sys/time.h>

//#define AUTO_SIM_CHECK 1
#define NEED_CHECK_PHONE 1
#if (NEED_CHECK_PHONE>0)
#define CONFIG_CHECK_PHONE
#define DEFAULT_CHECK_TIMEOUT 20
#endif


#define GSM_MAX_PHONE_NUMBER 64
#define GSM_MAX_SMS_LENGTH (80*10)		//codec byte size, this is a assume value
#define GSM_MAX_PDU_LENGTH 176


/* No more than 128 scheduled events */
#define MAX_SCHED	10000//128

#define MAX_TIMERS	32

/* Node types */
#define GSM_NETWORK		1
#define GSM_CPE			2

/* Debugging */
#define GSM_DEBUG_Q921_RAW		(1 << 0)	/* 0x0000 Show raw HDLC frames */
#define GSM_DEBUG_Q921_DUMP		(1 << 1)	/* 0x0002 Show each interpreted Q.921 frame */
#define GSM_DEBUG_Q921_STATE	(1 << 2)	/* 0x0004 ebug state machine changes */
#define GSM_DEBUG_CONFIG		(1 << 3) 	/* 0x0008 Display error events on stdout */
#define GSM_DEBUG_AT_DUMP		(1 << 5)	/* 0x0020 Show AT Command */
#define GSM_DEBUG_AT_STATE		(1 << 6)	/* 0x0040 Debug AT Command state changes */
#define GSM_DEBUG_AT_ANOMALY	(1 << 7)	/* 0x0080 Show unexpected events */
#define GSM_DEBUG_APDU			(1 << 8)	/* 0x0100 Debug of APDU components such as ROSE */
#define GSM_DEBUG_AOC			(1 << 9)	/* 0x0200 Debug of Advice of Charge ROSE Messages */
#define GSM_DEBUG_AT_RECEIVED	(1 << 10)	/* 0x0400 Debug of received AT Command */
#define GSM_DEBUG_SMS			(1 << 11)	/* 0x0800 Debug of received AT Command */

#define GSM_DEBUG_ALL			(0xffff)	/* Everything */

/* Switch types */
#define GSM_SWITCH_UNKNOWN		0
#define GSM_SWITCH_E169			1
#define GSM_SWITCH_SIMCOM		2
#define GSM_SWITCH_SIM340DZ		GSM_SWITCH_SIMCOM
#define GSM_SWITCH_EM200		3
#define GSM_SWITCH_M20			4
#define GSM_SWITCH_SIM900		5

/* EXTEND D-Channel Events */
enum EVENT_DEFINE {
	GSM_EVENT_DCHAN_UP = 1,		/* D-channel is up */
	GSM_EVENT_DETECT_MODULE_OK,
	GSM_EVENT_DCHAN_DOWN, 		/* D-channel is down */
	GSM_EVENT_RESTART,			/* B-channel is restarted */
	GSM_EVENT_CONFIG_ERR,		/* Configuration Error Detected */
	GSM_EVENT_RING,				/* Incoming call (SETUP) */
	GSM_EVENT_HANGUP,			/* Call got hung up (RELEASE/RELEASE_COMPLETE/other) */
	GSM_EVENT_RINGING,			/* Call is ringing (ALERTING) */
	GSM_EVENT_MO_RINGING,		/* Call has been answered (CONNECT) */
	GSM_EVENT_ANSWER,			/* Call has been answered (CONNECT) */
	GSM_EVENT_HANGUP_ACK,		/* Call hangup has been acknowledged */
	GSM_EVENT_RESTART_ACK,		/* Restart complete on a given channel (RESTART_ACKNOWLEDGE) */
	GSM_EVENT_FACNAME,			/* Caller*ID Name received on Facility */
	GSM_EVENT_INFO_RECEIVED,	/* Additional info (digits) received (INFORMATION) */
	GSM_EVENT_PROCEEDING,		/* When we get CALL_PROCEEDING */
	GSM_EVENT_SETUP_ACK,		/* When we get SETUP_ACKNOWLEDGE */
	GSM_EVENT_HANGUP_REQ,		/* Requesting the higher layer to hangup (DISCONNECT) */
	GSM_EVENT_NOTIFY,			/* Notification received (NOTIFY) */
	GSM_EVENT_PROGRESS,			/* When we get PROGRESS */
	GSM_EVENT_KEYPAD_DIGIT,		/* When we receive during ACTIVE state (INFORMATION) */
	GSM_EVENT_SMS_RECEIVED,		/* SMS event */
	GSM_EVENT_SIM_FAILED,		/* SIM  not inserted */
	GSM_EVENT_PIN_REQUIRED,		/* SIM Pin required */
	GSM_EVENT_PIN_ERROR,		/* SIM Pin error */
#ifdef AUTO_SIM_CHECK
	GSM_EVENT_SIM_INSERTED,		/* SIM inserted */
#endif
	GSM_EVENT_SMS_SEND_OK,	
	GSM_EVENT_SMS_SEND_FAILED,
	GSM_EVENT_USSD_RECEIVED,
	GSM_EVENT_USSD_SEND_FAILED,
	GSM_EVENT_NO_SIGNAL,
#ifdef CONFIG_CHECK_PHONE
	GSM_EVENT_CHECK_PHONE,		/*Check phone stat*/
#endif //CONFIG_CHECK_PHONE
#ifdef VIRTUAL_TTY
	GSM_EVENT_INIT_MUX,
#endif //VIRTUAL_TTY
};


/* Simple states */
enum STATE_DEFINE {
	GSM_STATE_DOWN = 0,
	GSM_STATE_INIT,
	GSM_STATE_UP,

//Freedom Add 2011-10-14 09:20 Fix up sim900d bug,must send "ATH" after "ATZ"
//////////////////////////////////////////////////////////////////////////////////
	GSM_STATE_SEND_HANGUP,
//////////////////////////////////////////////////////////////////////////////////
	GSM_STATE_SET_ECHO,
	GSM_STATE_SET_REPORT_ERROR,
	GSM_STATE_MODEL_NAME_REQ,
	GSM_STATE_MANUFACTURER_REQ,
	GSM_STATE_VERSION_REQ,
	GSM_STATE_GSN_REQ,
	GSM_STATE_IMEI_REQ,
	GSM_ENABLE_SIM_DETECT,
	GSM_STATE_IMSI_REQ,
	GSM_STATE_INIT_0,
	GSM_STATE_INIT_1,
	GSM_STATE_INIT_2,
	GSM_STATE_INIT_3,
	GSM_STATE_INIT_4,
	GSM_STATE_INIT_5,
	GSM_STATE_SIM_READY_REQ,/* for sim card */
	GSM_STATE_SIM_PIN_REQ,/* for sim card */
	GSM_STATE_SIM_PUK_REQ,/* for sim card */
	GSM_STATE_SIM_READY,/* for sim card */
	GSM_STATE_UIM_READY_REQ,/* for uim card */
	GSM_STATE_UIM_PIN_REQ,/* for uim card */
	GSM_STATE_UIM_PUK_REQ,/* for uim card */
	GSM_STATE_UIM_READY,/* for uim card */

#ifdef VIRTUAL_TTY
	GSM_INIT_MUX,
#endif //VIRTUAL_TTY

	GSM_STATE_MOC_STATE_ENABLED,
	GSM_STATE_SET_SIDE_TONE,
	GSM_STATE_CLIP_ENABLED,
	GSM_STATE_DTR_WAKEUP_DISABLED,
	GSM_STATE_RSSI_ENABLED,
	GSM_STATE_SET_NET_URC,
	GSM_STATE_NET_REQ,
	GSM_STATE_NET_OK,
	GSM_AT_MODE,
	GSM_STATE_NET_NAME_REQ,
	GSM_STATE_READY,
	GSM_STATE_CALL_INIT,
	GSM_STATE_CALL_MADE,
	GSM_STATE_CALL_PRESENT,
	GSM_STATE_CALL_PROCEEDING,
	GSM_STATE_CALL_PROGRESS,
	GSM_STATE_PRE_ANSWER,
	GSM_STATE_CALL_ACTIVE_REQ,
	GSM_STATE_CALL_ACTIVE,
	GSM_STATE_CLIP,
	GSM_STATE_RING,
	GSM_STATE_RINGING,
	GSM_STATE_HANGUP_REQ,
	GSM_STATE_HANGUP_ACQ,
	GSM_STATE_HANGUP,
	GSM_STATE_GET_SMSC_REQ,
	GSM_STATE_SMS_SET_CHARSET,
	GSM_STATE_SMS_SET_INDICATION,
	GSM_STATE_SMS_SET_SMSC,
	GSM_STATE_SET_SPEAK_VOL,
	GSM_STATE_SET_MIC_VOL,
	GSM_STATE_SMS_SET_UNICODE,
	GSM_STATE_SMS_SENDING,
	GSM_STATE_SMS_SENT,
	GSM_STATE_SMS_SENT_END,
	GSM_STATE_SMS_RECEIVED,
	GSM_STATE_USSD_SENDING,

#ifdef CONFIG_CHECK_PHONE
	GSM_STATE_PHONE_CHECK,
#endif
};

#define GSM_PROGRESS_MASK		0

/* Progress indicator values */
#define GSM_PROG_CALL_NOT_E2E_ISDN						(1 << 0)
#define GSM_PROG_CALLED_NOT_ISDN						(1 << 1)
#define GSM_PROG_CALLER_NOT_ISDN						(1 << 2)
#define GSM_PROG_INBAND_AVAILABLE						(1 << 3)
#define GSM_PROG_DELAY_AT_INTERF						(1 << 4)
#define GSM_PROG_INTERWORKING_WITH_PUBLIC				(1 << 5)
#define GSM_PROG_INTERWORKING_NO_RELEASE				(1 << 6)
#define GSM_PROG_INTERWORKING_NO_RELEASE_PRE_ANSWER		(1 << 7)
#define GSM_PROG_INTERWORKING_NO_RELEASE_POST_ANSWER	(1 << 8)
#define GSM_PROG_CALLER_RETURNED_TO_ISDN				(1 << 9)


/* Causes for disconnection */
#define GSM_CAUSE_UNALLOCATED					1
#define GSM_CAUSE_NO_ROUTE_TRANSIT_NET			2	/* !Q.SIG */
#define GSM_CAUSE_NO_ROUTE_DESTINATION			3
#define GSM_CAUSE_CHANNEL_UNACCEPTABLE			6
#define GSM_CAUSE_CALL_AWARDED_DELIVERED		7	/* !Q.SIG */
#define GSM_CAUSE_NORMAL_CLEARING				16
#define GSM_CAUSE_USER_BUSY						17
#define GSM_CAUSE_NO_USER_RESPONSE				18
#define GSM_CAUSE_NO_ANSWER						19
#define GSM_CAUSE_CALL_REJECTED					21
#define GSM_CAUSE_NUMBER_CHANGED				22
#define GSM_CAUSE_DESTINATION_OUT_OF_ORDER		27
#define GSM_CAUSE_INVALID_NUMBER_FORMAT			28
#define GSM_CAUSE_FACILITY_REJECTED				29	/* !Q.SIG */
#define GSM_CAUSE_RESPONSE_TO_STATUS_ENQUIRY	30
#define GSM_CAUSE_NORMAL_UNSPECIFIED			31
#define GSM_CAUSE_NORMAL_CIRCUIT_CONGESTION		34
#define GSM_CAUSE_NETWORK_OUT_OF_ORDER			38	/* !Q.SIG */
#define GSM_CAUSE_NORMAL_TEMPORARY_FAILURE		41
#define GSM_CAUSE_SWITCH_CONGESTION				42	/* !Q.SIG */
#define GSM_CAUSE_ACCESS_INFO_DISCARDED			43	/* !Q.SIG */
#define GSM_CAUSE_REQUESTED_CHAN_UNAVAIL		44
#define GSM_CAUSE_PRE_EMPTED					45	/* !Q.SIG */
#define GSM_CAUSE_FACILITY_NOT_SUBSCRIBED		50	/* !Q.SIG */
#define GSM_CAUSE_OUTGOING_CALL_BARRED			52	/* !Q.SIG */
#define GSM_CAUSE_INCOMING_CALL_BARRED			54	/* !Q.SIG */
#define GSM_CAUSE_BEARERCAPABILITY_NOTAUTH		57
#define GSM_CAUSE_BEARERCAPABILITY_NOTAVAIL		58
#define GSM_CAUSE_SERVICEOROPTION_NOTAVAIL		63	/* Q.SIG */
#define GSM_CAUSE_BEARERCAPABILITY_NOTIMPL		65
#define GSM_CAUSE_CHAN_NOT_IMPLEMENTED			66	/* !Q.SIG */
#define GSM_CAUSE_FACILITY_NOT_IMPLEMENTED		69	/* !Q.SIG */
#define GSM_CAUSE_INVALID_CALL_REFERENCE		81
#define GSM_CAUSE_IDENTIFIED_CHANNEL_NOTEXIST	82	/* Q.SIG */
#define GSM_CAUSE_INCOMPATIBLE_DESTINATION		88
#define GSM_CAUSE_INVALID_MSG_UNSPECIFIED		95	/* !Q.SIG */
#define GSM_CAUSE_MANDATORY_IE_MISSING			96
#define GSM_CAUSE_MESSAGE_TYPE_NONEXIST			97
#define GSM_CAUSE_WRONG_MESSAGE					98
#define GSM_CAUSE_IE_NONEXIST					99
#define GSM_CAUSE_INVALID_IE_CONTENTS			100
#define GSM_CAUSE_WRONG_CALL_STATE				101
#define GSM_CAUSE_RECOVERY_ON_TIMER_EXPIRE		102
#define GSM_CAUSE_MANDATORY_IE_LENGTH_ERROR		103	/* !Q.SIG */
#define GSM_CAUSE_PROTOCOL_ERROR				111
#define GSM_CAUSE_INTERWORKING					127	/* !Q.SIG */

/* Transmit capabilities */
#define GSM_TRANS_CAP_SPEECH					0x0

#define GSM_LAYER_1_ULAW			0x22
#define GSM_LAYER_1_ALAW			0x23

enum sms_mode {
	SMS_UNKNOWN,
	SMS_PDU,
	SMS_TEXT
};

#ifdef CONFIG_CHECK_PHONE
enum phone_stat {
	SPAN_USING,
	PHONE_CONNECT,
	PHONE_RING,
	PHONE_BUSY,
	PHONE_POWEROFF,
	PHONE_NOT_CARRIER,
	PHONE_NOT_ANSWER,
	PHONE_NOT_DIALTONE,
	PHONE_TIMEOUT
};
#endif

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

typedef struct gsm_event_generic {
	/* Events with no additional information fall in this category */
	int e;
} gsm_event_generic;

typedef struct gsm_event_error {
	int e;
	char err[256];
} gsm_event_error;

typedef struct gsm_event_restart {
	int e;
	int channel;
} gsm_event_restart;

typedef struct gsm_event_ringing {
	int e;
	int channel;
	int cref;
	int progress;
	int progressmask;
	struct at_call *call;
} gsm_event_ringing;

typedef struct gsm_event_answer {
	int e;
	int channel;
	int cref;
	int progress;
	int progressmask;
	struct at_call *call;
} gsm_event_answer;

typedef struct gsm_event_facname {
	int e;
	char callingname[256];
	char callingnum[256];
	int channel;
	int cref;
	struct at_call *call;
} gsm_event_facname;

#define GSM_CALLINGPLANANI
#define GSM_CALLINGPLANRDNIS
typedef struct gsm_event_ring {
	int e;
	int channel;				/* Channel requested */
	char callingnum[256];		/* Calling number */
	char callingname[256];		/* Calling name (if provided) */
	char callednum[256];		/* Called number */
	int flexible;				/* Are we flexible with our channel selection? */
	int cref;					/* Call Reference Number */
	int layer1;					/* User layer 1 */
	int complete;				/* Have we seen "Complete" i.e. no more number? */
	struct at_call *call;				/* Opaque call pointer */
	int progress;
	int progressmask;
} gsm_event_ring;

typedef struct gsm_event_hangup {
	int e;
	int channel;				/* Channel requested */
	int cause;
	int cref;
	struct at_call *call;				/* Opaque call pointer */
	long aoc_units;				/* Advise of Charge number of charged units */
} gsm_event_hangup;	

typedef struct gsm_event_restart_ack {
	int e;
	int channel;
} gsm_event_restart_ack;

#define GSM_PROGRESS_CAUSE
typedef struct gsm_event_proceeding {
	int e;
	int channel;
	int cref;
	int progress;
	int progressmask;
	int cause;
	struct at_call *call;
} gsm_event_proceeding;
 
typedef struct gsm_event_setup_ack {
	int e;
	int channel;
	struct at_call *call;
} gsm_event_setup_ack;

typedef struct gsm_event_notify {
	int e;
	int channel;
	int info;
} gsm_event_notify;

typedef struct gsm_event_keypad_digit {
	int e;
	int channel;
	struct at_call *call;
	char digits[64];
} gsm_event_keypad_digit;

typedef struct gsm_event_sms_received {
    int e;
	enum sms_mode mode;		/* sms mode */
    char pdu[512];          /* short message in PDU format */
    char sender[255];
    char smsc[255];
	char time[32];			/* receive sms time */
	char tz[16];			/* receive sms time zone*/
    int len;				/* sms body length */
    char text[1024];			/* short message in text format */
} gsm_event_sms_received;  

typedef struct gsm_event_ussd_received {
    int e;
    unsigned char ussd_stat;
    unsigned char ussd_coding;
    int len;
    char text[1024];
} gsm_event_ussd_received; 


typedef union {
	int e;
	gsm_event_generic gen;		/* Generic view */
	gsm_event_restart restart;	/* Restart view */
	gsm_event_error	  err;		/* Error view */
	gsm_event_facname facname;	/* Caller*ID Name on Facility */
	gsm_event_ring	  ring;		/* Ring */
	gsm_event_hangup  hangup;	/* Hang up */
	gsm_event_ringing ringing;	/* Ringing */
	gsm_event_answer  answer;	/* Answer */
	gsm_event_restart_ack restartack;	/* Restart Acknowledge */
	gsm_event_proceeding  proceeding;	/* Call proceeding & Progress */
	gsm_event_setup_ack   setup_ack;	/* SETUP_ACKNOWLEDGE structure */
	gsm_event_notify notify;			/* Notification */
	gsm_event_keypad_digit digit;		/* Digits that come during a call */
    gsm_event_sms_received sms_received;  /* SM RX */
	gsm_event_ussd_received ussd_received; /*USSD RX */
} gsm_event;

//struct gsm_modul;
struct gsm_sr;

#define GSM_SMS_PDU_FO_TP_RP        0x80 // (1000 0000) Reply path. Parameter indicating that reply path exists.
#define GSM_SMS_PDU_FO_TP_UDHI      0x40 // (0100 0000) User data header indicator. This bit is set to 1 if the User Data field starts with a header.
#define GSM_SMS_PDU_FO_TP_SRI       0x20 // (0010 0000) Status report indication. This bit is set to 1 if a status report is going to be returned to the SME
#define GSM_SMS_PDU_FO_TP_MMS       0x04 // (0000 0100) More messages to send. This bit is set to 0 if there are more messages to send
#define GSM_SMS_PDU_FO_TP_MTI       0x03 // (0000 0011) Message type indicator. See GSM 03.40 subsection 9.2.3.1

typedef struct sms_pdu_tpoa{
    unsigned char len;
    unsigned char type;
    char number[32];
} sms_pdu_tpoa;

typedef struct gsm_sms_pdu_info {
    unsigned char smsc_addr_len;    /* SMC address length */
    unsigned char smsc_addr_type;   /* SMS address type */
    char smsc_addr_number[32];      /* SMC addresss */

    unsigned char first_octet;  /* First Octet */

    struct sms_pdu_tpoa tp_oa;  /* TP-OA */

    unsigned char tp_pid;       /* TP-PID */
    unsigned char tp_dcs;       /* TP-DCS */
    char tp_scts[16];           /* TP-SCTS */
    unsigned char tp_udl;       /* TP-UDL */
    char tp_ud[320];            /* TP_UD */
        
    int index;  /* sms index */
} gsm_sms_pdu_info;


#define GSM_IO_FUNCS
/* Type declaration for callbacks to read a HDLC frame as below */
typedef int (*gsm_rio_cb)(struct gsm_modul *gsm, void *buf, int buflen);

/* Type declaration for callbacks to write a HDLC frame as below */
typedef int (*gsm_wio_cb)(struct gsm_modul *gsm, const void *buf, int buflen);


struct gsm_sched {
	struct timeval when;
	void (*callback)(void *data);
	void *data;
};

typedef struct sms_txt_info_s {
	char id[512];
	struct gsm_modul *gsm;
	int resendsmsidx;
	char message[1024];
	char destination[512];
	char text[1024];		//no use
	int len;				//no use
} sms_txt_info_t;

typedef struct sms_pdu_info_s {
	char id[512];
	struct gsm_modul *gsm;
	int resendsmsidx;
	char message[1024];
	char destination[512];	//Freedom Add 2012-01-29 14:30
	char text[1024];		//Freedom Add 2012-02-13 16:44
	int len;
} sms_pdu_info_t;


typedef union {
	sms_txt_info_t txt_info;
	sms_pdu_info_t pdu_info;
} sms_info_u;

typedef struct ussd_info_s {
	struct gsm_modul *gsm;
	int resendsmsidx;
	char message[1024];		//no use
	int len;				//no use
} ussd_info_t;

typedef struct gsm_ussd_received {
	int return_flag;
    unsigned char ussd_stat;
    unsigned char ussd_coding;
    int len;
    char text[1024];
} ussd_recv_t; 

enum
{
	SMS_CODING_ASCII,
	SMS_CODING_UTF8,
	SMS_CODING_GB18030,
};

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
	char at_last_recv[1024];		/* Last Received command from dchan */
	int at_last_recv_idx;	/* at_lastrecv lenght */
	char at_last_sent[1024];		/* Last sent command to dchan */
	int at_last_sent_idx;	/* at_lastsent lenght */
	char *at_lastsent;
	
	char at_pre_recv[1024];
	
	char pin[16];				/* sim pin */
	char manufacturer[256];			/* gsm modem manufacturer */
	char sim_smsc[32];					/* gsm get SMSC AT+CSCA? */
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
	char ussd_sent_buffer[1024];	/* ussd send buffer */
	int ussd_sent_len;
	int use_cleartext;
	//char sms_smsc[32];
	char sms_text_coding[64];
	enum sms_mode sms_mod_flag;
	sms_info_u *sms_info;
	ussd_info_t *ussd_info;
	
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
#ifdef CONFIG_CHECK_PHONE
	/*Makes add 2012-04-09 16:56,check phone start time*/
	time_t check_timeout;
	int check_mode;
	int phone_stat;
	int auto_hangup_flag;
#endif //CONFIG_CHECK_PHONE
	int vol;
	int mic;
	int debug_at_fd;
	int debug_at_flag;

#ifdef VIRTUAL_TTY
	int already_set_mux_mode;
#endif //VIRTUAL_TTY

};


struct at_call *gsm_getcall(struct gsm_modul *gsm, int cr, int outboundnew);

/* Create a D-channel on a given file descriptor.  The file descriptor must be a
   channel operating in HDLC mode with FCS computed by the fd's driver.  Also it
   must be NON-BLOCKING! Frames received on the fd should include FCS.  Nodetype 
   must be one of GSM_NETWORK or GSM_CPE.  switchtype should be GSM_SWITCH_* */

struct gsm_modul *gsm_new(int fd, int nodetype, int switchtype, int span, int at_debug);

/* Set debug parameters on EXTEND -- see above debug definitions */
void gsm_set_debug(struct gsm_modul *gsm, int debug);

/* Get debug parameters on EXTEND -- see above debug definitions */
int gsm_get_debug(struct gsm_modul *gsm);

/* Check for an outstanding event on the EXTEND */
gsm_event *gsm_check_event(struct gsm_modul *gsm);

/* Give a name to a given event ID */
char *gsm_event2str(int id);

/* Give a name to a node type */
char *gsm_node2str(int id);

/* Give a name to a switch type */
char *gsm_switch2str(int id);

/* Print an event */
void gsm_dump_event(struct gsm_modul *gsm, gsm_event *e);

/* Turn presentation into a string */
char *gsm_pres2str(int pres);

/* Turn numbering plan into a string */
char *gsm_plan2str(int plan);

/* Turn cause into a string */
char *gsm_cause2str(int cause);

/* Acknowledge a call and place it on the given channel.  Set info to non-zero if there
   is in-band data available on the channel */
int gsm_acknowledge(struct gsm_modul *gsm, struct at_call *call, int channel, int info);

/* Send a digit in overlap mode */
int gsm_information(struct gsm_modul *gsm, struct at_call *call, char digit);

#define GSM_KEYPAD_FACILITY_TX
/* Send a keypad facility string of digits */
int gsm_keypad_facility(struct gsm_modul *gsm, struct at_call *call, char *digits);

/* Answer the incomplete(call without called number) call on the given channel.
   Set non-isdn to non-zero if you are not connecting to ISDN equipment */
int gsm_need_more_info(struct gsm_modul *gsm, struct at_call *call, int channel);

/* Answer the call on the given channel (ignored if you called acknowledge already).
   Set non-isdn to non-zero if you are not connecting to ISDN equipment */
int gsm_answer(struct gsm_modul *gsm, struct at_call *call, int channel);

int gsm_senddtmf(struct gsm_modul *gsm, char digit);

#undef gsm_release
#undef gsm_disconnect

/* backwards compatibility for those who don't use asterisk with libgsmat */
#define gsm_release(a,b,c) \
	gsm_hangup(a,b,c)

#define gsm_disconnect(a,b,c) \
	gsm_hangup(a,b,c)

/* Hangup a call */
#define GSM_HANGUP
int gsm_hangup(struct gsm_modul *gsm, struct at_call *call, int cause);

#define GSM_DESTROYCALL
void gsm_destroycall(struct gsm_modul *gsm, struct at_call *call);

#define GSM_RESTART
int gsm_restart(struct gsm_modul *gsm);

int gsm_reset(struct gsm_modul *gsm, int channel);

/* Create a new call */
struct at_call *gsm_new_call(struct gsm_modul *gsm);

/* How long until you need to poll for a new event */
struct timeval *gsm_schedule_next(struct gsm_modul *v);

/* Run any pending schedule events */
extern gsm_event *gsm_schedule_run(struct gsm_modul *gsm);
extern gsm_event *gsm_schedule_run_tv(struct gsm_modul *gsm, const struct timeval *now);

int gsm_call(struct gsm_modul *gsm, struct at_call *c, int transmode, int channel,
   int exclusive, int nonisdn, char *caller, int callerplan, char *callername, int callerpres,
	 char *called,int calledplan, int ulayer1);

struct gsm_sr *gsm_sr_new(void);
void gsm_sr_free(struct gsm_sr *sr);

int gsm_sr_set_channel(struct gsm_sr *sr, int channel, int exclusive, int nonisdn);
int gsm_sr_set_called(struct gsm_sr *sr, char *called, int complete);
int gsm_sr_set_caller(struct gsm_sr *sr, char *caller, char *callername, int callerpres);

int gsm_setup(struct gsm_modul *gsm, struct at_call *call, struct gsm_sr *req);

/* Override message and error stuff */
#define GSM_NEW_SET_API
void gsm_set_message(void (*__gsm_error)(struct gsm_modul *gsm, char *));
void gsm_set_error(void (*__gsm_error)(struct gsm_modul *gsm, char *));

#define GSM_DUMP_INFO_STR
char *gsm_dump_info_str(struct gsm_modul *gsm);

/* Get file descriptor */
int gsm_fd(struct gsm_modul *gsm);

#define GSM_PROGRESS
/* Send progress */
int gsm_progress(struct gsm_modul *gsm, struct at_call *c, int channel, int info);


#define GSM_PROCEEDING_FULL
/* Send call proceeding */
int gsm_proceeding(struct gsm_modul *gsm, struct at_call *c, int channel, int info);

/* Get/Set EXTEND Timers  */
#define GSM_GETSET_TIMERS
int gsm_set_timer(struct gsm_modul *gsm, int timer, int value);
int gsm_get_timer(struct gsm_modul *gsm, int timer);

extern int gsm_send_ussd(struct gsm_modul *gsm, const char *message);
extern int gsm_send_text(struct gsm_modul *gsm, const char *destination, const char *message, const char *id);
extern int gsm_send_pdu(struct gsm_modul *gsm,  const char *message, const char *text, const char *id); 
extern int gsm_decode_pdu(struct gsm_modul *gsm, char *pdu, struct gsm_sms_pdu_info *pdu_info);
extern int gsm_send_pin(struct gsm_modul *gsm, const char *pin);

#define GSM_MAX_TIMERS 32

#define GSM_TIMER_N200	0	/* Maximum numer of q921 retransmissions */
#define GSM_TIMER_N201	1	/* Maximum numer of octets in an information field */
#define GSM_TIMER_N202	2	/* Maximum numer of transmissions of the TEI identity request message */
#define GSM_TIMER_K		3	/* Maximum number of outstanding I-frames */

#define GSM_TIMER_T200	4	/* time between SABME's */
#define GSM_TIMER_T201	5	/* minimum time between retransmissions of the TEI Identity check messages */
#define GSM_TIMER_T202	6	/* minimum time between transmission of TEI Identity request messages */
#define GSM_TIMER_T203	7	/* maxiumum time without exchanging packets */

#define GSM_TIMER_T300	8	
#define GSM_TIMER_T301	9	/* maximum time to respond to an ALERT */
#define GSM_TIMER_T302	10
#define GSM_TIMER_T303	11	/* maximum time to wait after sending a SETUP without a response */
#define GSM_TIMER_T304	12
#define GSM_TIMER_T305	13
#define GSM_TIMER_T306	14
#define GSM_TIMER_T307	15
#define GSM_TIMER_T308	16
#define GSM_TIMER_T309	17
#define GSM_TIMER_T310	18	/* maximum time between receiving a CALLPROCEEDING and receiving a ALERT/CONNECT/DISCONNECT/PROGRESS */
#define GSM_TIMER_T313	19
#define GSM_TIMER_T314	20
#define GSM_TIMER_T316	21	/* maximum time between transmitting a RESTART and receiving a RESTART ACK */
#define GSM_TIMER_T317	22
#define GSM_TIMER_T318	23
#define GSM_TIMER_T319	24
#define GSM_TIMER_T320	25
#define GSM_TIMER_T321	26
#define GSM_TIMER_T322	27

#define GSM_TIMER_TM20	28	/* maximum time avaiting XID response */
#define GSM_TIMER_NM20	29	/* number of XID retransmits */

/* Get EXTEND version */
extern const char *gsm_get_version(void);
extern int gsm_test_atcommand(struct gsm_modul *gsm, char *at);

/*Freedom Add 2011-10-10 11:33*/
int init_cfg_file(void);
int destroy_cfg_file(void);
const char* get_at(int module_id, int cmds_id);
int get_at_cmds_id(char* name);
int get_coverage1(int module_id,char* h);
int get_coverage2(int module_id,char* h);
char* get_dial_str(int module_id, const char* number, char* buf, int len);
char* get_pin_str(int module_id, const char* pin, char* buf, int len);
char* get_dtmf_str(int module_id, char digit, char* buf, int len);
char* get_sms_len(int module_id, int sms_len, char* buf, int len);
char* get_sms_des(int module_id, const char* des, char* buf, int len);
char* get_cid(int module_id, const char* h, char* buf, int len);
void gsm_set_module_id(int *const module_id, const char *name);
const char* gsm_get_module_name(int module_id);

int gsm_encode_pdu_ucs2(const char* SCA, const char* TPA, char* TP_UD, const char* code,char* pDst);
int gsm_forward_pdu(const char* src_pdu,const char* TPA,const char* SCA, char* pDst);

#ifdef CONFIG_CHECK_PHONE
void gsm_set_check_phone_mode(struct gsm_modul *gsm,int mode);
void gsm_hangup_phone(struct gsm_modul *gsm);
int gsm_check_phone_stat(struct gsm_modul *gsm, const char *phone_number, int hangup_flag, unsigned int timeout);
#endif //CONFIG_CHECK_PHONE

#ifdef VIRTUAL_TTY
int gsm_get_mux_command(struct gsm_modul *gsm,char *command);
int gsm_mux_end(struct gsm_modul *gsm, int restart_at_flow);
#endif  //VIRTUAL_TTY

void gsm_module_start(struct gsm_modul *gsm);
char *gsm_state2str(int state);

void gsm_set_debugat(struct gsm_modul *gsm,int mode);
#endif //_LIBGSMAT_H

