/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief EXTRA for Pseudo TDM
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * Connects to the EXTRA telephony library as well as
 * libpri. Libpri is optional and needed only if you are
 * going to use ISDN connections.
 *
 * You need to install libraries before you attempt to compile
 * and install the EXTRA channel.
 *
 * \par See also
 * \arg \ref Config_extra
 *
 * \ingroup channel_drivers
 *
 * \todo Deprecate the "musiconhold" configuration option post 1.4
 */

/*** MODULEINFO 
	<use>res_smdi</use>
	<depend>dahdi</depend>
	<depend>tonezone</depend>
	<use>gsmat</use>
 ***/

#include "asterisk.h"

#ifndef ASTERISK_VERSION_NUM
#error "---------------------------------------------------------------------------------"
#error "No define ASTERISK_VERSION_NUM"
#error "Get Asterisk version number:"
#error "awk -F. '{printf \"%01d%02d%02d\", $1, $2, $3}' .version"
#error "---------------------------------------------------------------------------------"
#endif //ASTERISK_VERSION_NUM

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 293807 $")

#if defined(__NetBSD__) || defined(__FreeBSD__)
#include <pthread.h>
#include <signal.h>
#else
#include <sys/signal.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <math.h>
#include <ctype.h>

#include <dahdi/user.h>
#include <dahdi/tonezone.h>
#if (ASTERISK_VERSION_NUM >= 10800)
#include "sig_analog.h"
#endif //(ASTERISK_VERSION_NUM >= 10800)
/* Analog signaling is currently still present in chan_extra for use with
 * radio. Sig_analog does not currently handle any radio operations. If
 * radio only uses analog signaling, then the radio handling logic could
 * be placed in sig_analog and the duplicated code could be removed.
 */

#ifdef HAVE_GSMAT
#include <libgsmat.h>
#endif //HAVE_GSMAT

#ifndef HAVE_GSMAT
#error "---------------------------------------------------------------------------------"
#error "No define HAVE_GSMAT"
#error "---------------------------------------------------------------------------------"
#endif //HAVE_GSMAT

#if !(ASTERISK_VERSION_NUM > 10444)
#include "asterisk/options.h"
#endif //!(ASTERISK_VERSION_NUM > 10444)

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/file.h"
#include "asterisk/ulaw.h"
#include "asterisk/alaw.h"
#include "asterisk/callerid.h"
#include "asterisk/adsi.h"
#include "asterisk/cli.h"
#include "asterisk/cdr.h"
#if (ASTERISK_VERSION_NUM >= 10800)
#include "asterisk/cel.h"
#include "asterisk/ccss.h"
#include "asterisk/data.h"
#endif //(ASTERISK_VERSION_NUM >= 10800)
#include "asterisk/features.h"
#include "asterisk/musiconhold.h"
#include "asterisk/say.h"
#include "asterisk/tdd.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/astdb.h"
#include "asterisk/manager.h"
#include "asterisk/causes.h"
#include "asterisk/term.h"
#include "asterisk/utils.h"
#include "asterisk/transcap.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/smdi.h"
#include "asterisk/astobj.h"
#if (ASTERISK_VERSION_NUM > 10444)
#include "asterisk/event.h"
#endif //(ASTERISK_VERSION_NUM > 10444)
#include "asterisk/devicestate.h"
#include "asterisk/paths.h"

static const char * const lbostr[] = {
"0 db (CSU)/0-133 feet (DSX-1)",
"133-266 feet (DSX-1)",
"266-399 feet (DSX-1)",
"399-533 feet (DSX-1)",
"533-655 feet (DSX-1)",
"-7.5db (CSU)",
"-15db (CSU)",
"-22.5db (CSU)"
};

static const char tdesc[] = "GSM/CDMA Telephony Driver FOR Asterisk"
#ifdef HAVE_GSMAT
	       " w/GSMAT"
#endif
;

static char *app_sendsms = "SendSMS";
static char *sendsms_synopsis = "SendSMS(Span,Dest,Message,[ID])";
static char *sendsms_desc =
"SendSMS(Span,Dest,Message)\n"
"  Span - Id of device from chan-extra.conf\n"
"  Dest - destination\n"
"  Message - text of the message\n"
"  ID - Indentification of this sms\n";

static char *app_sendpdu = "SendPDU";
static char *sendpdu_synopsis = "SendPDU(Span,PDU,[ID])";
static char *sendpdu_desc =
"SendPDU(Span,PDU)\n"
"  Span - Id of device from chan-extra.conf\n"
"  PDU - PDU code\n"
"  ID - Indentification of this sms\n";

static char *app_forwardsms = "ForwardSMS";
static char *forwardsms_synopsis = "ForwardSMS(Device,Dest,[ID])";
static char *forwardsms_desc =
"ForwardSMS(Device,Dest[,SMS center])\n"
"  Device - Id of device from chan-extra.conf\n"
"  Dest - destination\n"
"  ID - Indentification of this sms\n";

#define SEND_SMS_MODE_PDU 0
#define SEND_SMS_MODE_TXT 1
struct send_sms_cfg {
	int mode;
	char smsc[64];
	char coding[64];
};

static const char config[] = "chan_extra.conf";

#if (ASTERISK_VERSION_NUM > 10444)
#define _SHOWUSAGE_ CLI_SHOWUSAGE
#define _SUCCESS_ CLI_SUCCESS
#define _FAILURE_ CLI_FAILURE
#else  //(ASTERISK_VERSION_NUM > 10444)
#define _SHOWUSAGE_ RESULT_SHOWUSAGE
#define _SUCCESS_ RESULT_SUCCESS
#define _FAILURE_ RESULT_FAILURE
#endif //(ASTERISK_VERSION_NUM > 10444)

#if !(ASTERISK_VERSION_NUM > 10444) && !defined(ast_verb)
#define VERBOSITY_ATLEAST(level) (option_verbose >= (level))
#define ast_verb(level, ...) do { \
	if (VERBOSITY_ATLEAST((level)) ) { \
		if (level >= 4) \
			ast_verbose(VERBOSE_PREFIX_4 __VA_ARGS__); \
		else if (level == 3) \
			ast_verbose(VERBOSE_PREFIX_3 __VA_ARGS__); \
		else if (level == 2) \
			ast_verbose(VERBOSE_PREFIX_2 __VA_ARGS__); \
		else if (level == 1) \
			ast_verbose(VERBOSE_PREFIX_1 __VA_ARGS__); \
		else \
			ast_verbose(__VA_ARGS__); \
	} \
} while (0)
#endif //!(ASTERISK_VERSION_NUM > 10444) && !defined(ast_verb)

#if !(ASTERISK_VERSION_NUM > 10444) && !defined(ast_debug)
#define ast_debug(level, ...) do {       \
	if (option_debug >= (level) ) \
		ast_log(LOG_DEBUG, __VA_ARGS__); \
} while (0)
#endif //!(ASTERISK_VERSION_NUM > 10444) && !defined(ast_debug)

#define SIG_GSM_EXTRA		        (0x8000000 | DAHDI_SIG_CLEAR)

#ifdef HAVE_GSMAT
#define DCHAN_PROVISIONED (1 << 0)
#define DCHAN_NOTINALARM  (1 << 1)
#define DCHAN_UP          (1 << 2)
#define DCHAN_NO_SIM      (1 << 3)
#define DCHAN_NO_SIGNAL   (1 << 4)
#define DCHAN_PIN_ERROR   (1 << 5)
#define DCHAN_POWER		  (1 << 6)

#define EXTRA_SMSTXT 1
#define SAVE_SMS_TO_FILE 1
#define DCHAN_AVAILABLE	(DCHAN_PROVISIONED | DCHAN_NOTINALARM | DCHAN_UP)
#endif

static char gsm_modem_pin[20];

#ifdef LOTS_OF_SPANS
#define NUM_SPANS	DAHDI_MAX_SPANS
#else
#define NUM_SPANS 		32
#endif

#define CHAN_PSEUDO	-2

static char progzone[10] = "";

static int numbufs = 4;

#define REPORT_CHANNEL_ALARMS 1
#define REPORT_SPAN_ALARMS    2 
static int report_alarms = REPORT_CHANNEL_ALARMS;

#ifdef HAVE_GSMAT
static int gsmdebugfd = -1;
static char gsmdebugfilename[1024] = "";
#endif

#define OPVXG4XX_CODE 0xC4

#ifdef VIRTUAL_TTY
#define OPVXG4XX_INIT				_IO(OPVXG4XX_CODE, 1)
#define OPVXG4XX_SET_MUX			_IOW(OPVXG4XX_CODE, 2,unsigned long)
#define OPVXG4XX_CREATE_CONCOLE		_IO(OPVXG4XX_CODE, 3)
#define OPVXG4XX_CREATE_DAHDI		_IO(OPVXG4XX_CODE, 4)
#define OPVXG4XX_CREATE_EXT			_IO(OPVXG4XX_CODE, 5)
#define OPVXG4XX_CLEAR_MUX			_IO(OPVXG4XX_CODE, 6)
#define OPVXG4XX_ENABLE_TTY_MODULE	_IO(OPVXG4XX_CODE, 7)
#define OPVXG4XX_DISABLE_TTY_MODULE	_IO(OPVXG4XX_CODE, 8)
#define OPVXG4XX_GET_MUX_STAT		_IOR(OPVXG4XX_CODE, 9,int)
#define OPVXG4XX_GET_TTY_MODULE		_IOR(OPVXG4XX_CODE, 10,int)
#define OPVXG4XX_CONNECT_DAHDI		_IO(OPVXG4XX_CODE, 11)
#endif //VIRTUAL_TTY
#define OPVXG4XX_SPAN_INIT			_IO(OPVXG4XX_CODE, 12)
#define OPVXG4XX_SPAN_REMOVE		_IO(OPVXG4XX_CODE, 13)
#define OPVXG4XX_SPAN_STAT			_IOR(OPVXG4XX_CODE, 14,unsigned char)





/*! \brief How long to wait for following digits (FXO logic) */
static int gendigittimeout = 8000;

/*! \brief How long to wait for an extra digit, if there is an ambiguous match */
static int matchdigittimeout = 3000;

/*! \brief Protect the interface list (of extra_pvt's) */
AST_MUTEX_DEFINE_STATIC(iflock);

static int ifcount = 0;

#ifdef HAVE_GSMAT
AST_MUTEX_DEFINE_STATIC(gsmdebugfdlock);
#endif

static ast_cond_t ss_thread_complete;
AST_MUTEX_DEFINE_STATIC(ss_thread_lock);
AST_MUTEX_DEFINE_STATIC(restart_lock);
static int ss_thread_count = 0;
static int num_restart_pending = 0;

/*! \brief Avoid the silly extra_getevent which ignores a bunch of events */
static inline int extra_get_event(int fd)
{
	int j;
	if (ioctl(fd, DAHDI_GETEVENT, &j) == -1)
		return -1;
	return j;
}

/*! \brief Avoid the silly extra_waitevent which ignores a bunch of events */
static inline int extra_wait_event(int fd)
{
	int i, j = 0;
	i = DAHDI_IOMUX_SIGEVENT;
	if (ioctl(fd, DAHDI_IOMUX, &i) == -1)
		return -1;
	if (ioctl(fd, DAHDI_GETEVENT, &j) == -1)
		return -1;
	return j;
}

/*! Chunk size to read -- we use 20ms chunks to make things happy. */
#define READ_SIZE 160

struct extra_pvt;

#ifdef HAVE_GSMAT

#define GSM_PVT_TO_CHANNEL(p) ((p)->gsmoffset)
#define GSM_CHANNEL(p) ((p) & 0xff)
#define GSM_SPAN(p) (((p) >> 8) & 0xff)
#define GSM_EXPLICIT(p) (((p) >> 16) & 0x01)

struct extra_gsm {
	pthread_t master;						/*!< Thread of master */
	ast_mutex_t lock;						/*!< Mutex */
	int nodetype;							/*!< Node type */
	int switchtype;							/*!< Type of switch to emulate */
	int dchannel;					/*!< What channel is the dchannel on */
	int numchans;							/*!< Num of channels we represent */
	struct gsm_modul *dchan;					/*!< Actual d-channels */
	int dchanavail;					/*!< Whether each channel is available */
	struct gsm_modul *gsm;						/*!< Currently active D-channel */
	char pin[20];
	/*! \brief TRUE if to dump GSM event info (Tested but never set) */
	int debug;
	int fd;						/*!< FD's for d-channels */
	/*! \brief Value set but not used */
	int offset;
	/*! \brief Span number put into user output messages */
	int span;
	/*! \brief TRUE if span is being reset/restarted */
	int resetting;
	/*! \brief Current position during a reset (-1 if not started) */
	int resetpos;
	time_t lastreset;						/*!< time when unused channels were last reset */
	long resetinterval;						/*!< Interval (in seconds) for resetting unused channels */
	/*! \brief signalling type (SIG_GSM_EXTRA, etc...) */
	int sig;
	struct send_sms_cfg send_sms;
	struct extra_pvt *pvt;				/*!< Member channel pvt structs */

#ifdef CONFIG_CHECK_PHONE
	ast_mutex_t phone_lock;
	ast_cond_t check_cond;
	ast_mutex_t check_mutex;
	int phone_stat;
#endif

	ast_mutex_t ussd_mutex;
	ast_cond_t ussd_cond;
	ussd_recv_t ussd_received;
	int gsm_init_flag;
	int gsm_reinit;
	int vol;
	int mic;
#ifdef VIRTUAL_TTY
	int virtual_tty;
#endif //VIRTUAL_TTY

	int debug_at_flag;
};

static struct extra_gsm gsms[NUM_SPANS];

/* FIXME - Change debug defs when they are done... */
#if 0
#define DEFAULT_GSM_DEBUG GSM_DEBUG_AT_DUMP 
#else
#define DEFAULT_GSM_DEBUG 0
#endif

static inline void gsm_rel(struct extra_gsm *gsm)
{
	ast_mutex_unlock(&gsm->lock);
#ifdef CONFIG_CHECK_PHONE
	ast_mutex_unlock(&gsm->phone_lock);
	ast_mutex_unlock(&gsm->check_mutex);
#endif
	ast_mutex_unlock(&gsm->ussd_mutex);
}

#else
/*! Shut up the compiler */
struct extra_gsm;
#endif

#define SUB_REAL		0			/*!< Active call */
#define SUB_CALLWAIT	1			/*!< Call-Waiting call on hold */
#define SUB_THREEWAY	2			/*!< Three-way call */
#define SUB_SMS			3
#define SUB_SMSSEND		4

static const char * const subnames[] = {
	"Real",
	"Callwait",
	"Threeway",
	"SMS Received",
	"SMS Send"
};

struct extra_subchannel {
	int dfd;
	struct ast_channel *owner;
	int chan;
	short buffer[AST_FRIENDLY_OFFSET/2 + READ_SIZE];
	struct ast_frame f;		/*!< One frame for each channel.  How did this ever work before? */
	unsigned int needbusy:1;
	unsigned int needcongestion:1;
	unsigned int needanswer:1;
	unsigned int linear:1;
	unsigned int inthreeway:1;
	struct dahdi_confinfo curconf;
};

#define MAX_SLAVES	4

/*! Specify the lists extra_pvt can be put in. */
enum EXTRA_IFLIST {
	EXTRA_IFLIST_NONE,	/*!< The extra_pvt is not in any list. */
	EXTRA_IFLIST_MAIN,	/*!< The extra_pvt is in the main interface list */
};

#define SUB_SUM 5

struct extra_pvt {
	ast_mutex_t lock;					/*!< Channel private lock. */
	struct ast_channel *owner;			/*!< Our current active owner (if applicable) */
							/*!< Up to three channels can be associated with this call */

	struct extra_subchannel sub_unused;		/*!< Just a safety precaution */
	struct extra_subchannel subs[SUB_SUM];			/*!< Sub-channels */
	struct dahdi_confinfo saveconf;			/*!< Saved conference info */

	struct extra_pvt *slaves[MAX_SLAVES];		/*!< Slave to us (follows our conferencing) */
	struct extra_pvt *master;				/*!< Master to us (we follow their conferencing) */
	int inconference;				/*!< If our real should be in the conference */

	int bufsize;                /*!< Size of the buffers */
	int buf_no;					/*!< Number of buffers */
	int buf_policy;				/*!< Buffer policy */
	int faxbuf_no;              /*!< Number of Fax buffers */
	int faxbuf_policy;          /*!< Fax buffer policy */
	int sig;					/*!< Signalling style */
	/*!
	 * \brief Nonzero if the signaling type is sent over a radio.
	 * \note Set to a couple of nonzero values but it is only tested like a boolean.
	 */
	int radio;
	int outsigmod;					/*!< Outbound Signalling style (modifier) */
	int oprmode;					/*!< "Operator Services" mode */
	struct extra_pvt *oprpeer;				/*!< "Operator Services" peer tech_pvt ptr */
	/*! \brief Amount of gain to increase during caller id */
	float cid_rxgain;
	/*! \brief Rx gain set by chan_extra.conf */
	float rxgain;
	/*! \brief Tx gain set by chan_extra.conf */
	float txgain;

	float txdrc; /*!< Dynamic Range Compression factor. a number between 1 and 6ish */
	float rxdrc;
	
	int tonezone;					/*!< tone zone for this chan, or -1 for default */
	enum EXTRA_IFLIST which_iflist;	/*!< Which interface list is this structure listed? */
	struct extra_pvt *next;				/*!< Next channel in list */
	struct extra_pvt *prev;				/*!< Prev channel in list */

	/*!
	 * \brief TRUE if ADSI (Analog Display Services Interface) available
	 * \note Set from the "adsi" value read in from chan_extra.conf
	 */
	unsigned int adsi:1;
	/*!
	 * \brief TRUE if we can use a polarity reversal to mark when an outgoing
	 * call is answered by the remote party.
	 * \note Set from the "answeronpolarityswitch" value read in from chan_extra.conf
	 */
	unsigned int answeronpolarityswitch:1;
	/*!
	 * \brief TRUE if busy detection is enabled.
	 * (Listens for the beep-beep busy pattern.)
	 * \note Set from the "busydetect" value read in from chan_extra.conf
	 */
	unsigned int busydetect:1;
	/*!
	 * \brief TRUE if call return is enabled.
	 * (*69, if your dialplan doesn't catch this first)
	 * \note Set from the "callreturn" value read in from chan_extra.conf
	 */
	unsigned int callreturn:1;

	/*!
	 * \brief TRUE if support for call forwarding enabled.
	 * Dial *72 to enable call forwarding.
	 * Dial *73 to disable call forwarding.
	 * \note Set from the "cancallforward" value read in from chan_extra.conf
	 */
	unsigned int cancallforward:1;
	/*!
	 * \brief TRUE if support for call parking is enabled.
	 * \note Set from the "canpark" value read in from chan_extra.conf
	 */
	unsigned int canpark:1;
	/*! \brief TRUE if to wait for a DTMF digit to confirm answer */
	unsigned int confirmanswer:1;
	/*!
	 * \brief TRUE if the channel is to be destroyed on hangup.
	 * (Used by pseudo channels.)
	 */
	unsigned int destroy:1;
	unsigned int didtdd:1;				/*!< flag to say its done it once */
	/*! \brief TRUE if analog type line dialed no digits in Dial() */
	unsigned int dialednone:1;
	/*!
	 * \brief TRUE if in the process of dialing digits or sending something.
	 * \note This is used as a receive squelch for ISDN until connected.
	 */
	unsigned int dialing:1;
	/*! \brief TRUE if the transfer capability of the call is digital. */
	unsigned int digital:1;
	/*! \brief TRUE if Do-Not-Disturb is enabled, present only for non sig_analog */
	unsigned int dnd:1;

	/*!
	 * \brief TRUE if echo cancellation enabled when bridged.
	 * \note Initialized with the "echocancelwhenbridged" value read in from chan_extra.conf
	 * \note Disabled if the echo canceller is not setup.
	 */
	unsigned int echocanbridged:1;
	/*! \brief TRUE if echo cancellation is turned on. */
	unsigned int echocanon:1;
	/*! \brief TRUE if a fax tone has already been handled. */
	unsigned int faxhandled:1;
	/*! TRUE if dynamic faxbuffers are configured for use, default is OFF */
	unsigned int usefaxbuffers:1;
	/*! TRUE while buffer configuration override is in use */
	unsigned int bufferoverrideinuse:1;
	/*! \brief TRUE if over a radio and extra_read() has been called. */
	unsigned int firstradio:1;
	/*!
	 * \brief TRUE if the call will be considered "hung up" on a polarity reversal.
	 * \note Set from the "hanguponpolarityswitch" value read in from chan_extra.conf
	 */
	unsigned int hanguponpolarityswitch:1;
	/*! \brief TRUE if DTMF detection needs to be done by hardware. */
	unsigned int hardwaredtmf:1;
	
	/*! \brief TRUE if DTMF detection is disabled. */
	unsigned int ignoredtmf:1;
	/*!
	 * \brief TRUE if the channel should be answered immediately
	 * without attempting to gather any digits.
	 * \note Set from the "immediate" value read in from chan_extra.conf
	 */
	unsigned int immediate:1;
	/*! \brief TRUE if in an alarm condition. */
	unsigned int inalarm:1;
	/*! \brief TRUE if TDD in MATE mode */
	unsigned int mate:1;
	/*! \brief TRUE if we originated the call leg. */
	unsigned int outgoing:1;

	/*!
	 * \brief TRUE if PRI congestion/busy indications are sent out-of-band.
	 * \note Set from the "priindication" value read in from chan_extra.conf
	 */
	unsigned int priindication_oob:1;

	/*!
	 * \brief TRUE if we will pulse dial.
	 * \note Set from the "pulsedial" value read in from chan_extra.conf
	 */
	unsigned int pulse:1;
	/*! \brief TRUE if a pulsed digit was detected. (Pulse dial phone detected) */
	unsigned int pulsedial:1;
	unsigned int restartpending:1;		/*!< flag to ensure counted only once for restart */

	/*!
	 * \brief TRUE if caller ID is used on this channel.
	 * \note PRI and SS7 spans will save caller ID from the networking peer.
	 * \note FXS ports will generate the caller ID spill.
	 * \note FXO ports will listen for the caller ID spill.
	 * \note Set from the "usecallerid" value read in from chan_extra.conf
	 */
	unsigned int use_callerid:1;

	/*!
	 * \brief TRUE if channel is out of reset and ready
	 * \note Set but not used.
	 */
	unsigned int inservice:1;

	/*!
	 * \brief TRUE if the channel alarms will be managed also as Span ones
	 * \note Applies to all channels
	 */
	unsigned int manages_span_alarms:1;

#ifdef HAVE_GSMAT
	/*!
	 * \brief XXX BOOLEAN Purpose???
	 * \note Applies to SS7 channels.
	 */
	unsigned int rlt:1;
	/*! \brief TRUE if channel is alerting/ringing */
	unsigned int alerting:1;
	/*! \brief TRUE if the call has already gone/hungup */
	unsigned int alreadyhungup:1;
	/*!
	 * \brief TRUE if this is an idle call
	 * \note Applies to PRI channels.
	 */
	unsigned int isidlecall:1;
	/*!
	 * \brief TRUE if call is in a proceeding state.
	 * The call has started working its way through the network.
	 */
	unsigned int proceeding:1;
	/*! \brief TRUE if the call has seen progress through the network. */
	unsigned int progress:1;
	/*!
	 * \brief TRUE if this channel is being reset/restarted
	 * \note Applies to PRI channels.
	 */
	unsigned int resetting:1;
	/*!
	 * \brief TRUE if this channel has received a SETUP_ACKNOWLEDGE
	 * \note Applies to PRI channels.
	 */
	unsigned int setup_ack:1;
#endif

	/*!
	 * \brief The configured context for incoming calls.
	 * \note The "context" string read in from chan_extra.conf
	 */
	char context[AST_MAX_CONTEXT];
	
	/*!
	 * \brief Saved context string.
	 */
	char defcontext[AST_MAX_CONTEXT];
	/*! \brief Extension to use in the dialplan. */
	char exten[AST_MAX_EXTENSION];
	/*!
	 * \brief Language configured for calls.
	 * \note The "language" string read in from chan_extra.conf
	 */
	char language[MAX_LANGUAGE];

#ifdef HAVE_GSMAT
	/*! \brief Automatic Number Identification number (Alternate PRI caller ID number) */
	char cid_ani[AST_MAX_EXTENSION];
#endif	

	/*! \brief Caller ID number from an incoming call. */
	char cid_num[AST_MAX_EXTENSION];

	char cid_name[AST_MAX_EXTENSION];
	/*! \brief Last Caller ID number from an incoming call. */

	/*! \brief Redirecting Directory Number Information Service (RDNIS) number */
	char rdnis[AST_MAX_EXTENSION];
	/*! \brief Dialed Number Identifier */
	char dnid[AST_MAX_EXTENSION];
	/*!
	 * \brief Bitmapped groups this belongs to.
	 * \note The "group" bitmapped group string read in from chan_extra.conf
	 */
	ast_group_t group;
	/*! \brief Default call PCM encoding format: DAHDI_LAW_ALAW or DAHDI_LAW_MULAW. */
	int law_default;
	/*! \brief Active PCM encoding format: DAHDI_LAW_ALAW or DAHDI_LAW_MULAW */
	int law;
	int confno;					/*!< Our conference */

	/*!
	 * \brief Channel variable list with associated values to set when a channel is created.
	 * \note The "setvar" strings read in from chan_extra.conf
	 */
	struct ast_variable *vars;
	int channel;					/*!< Channel Number */
	int span;					/*!< Span number */

	/*!
	 * \brief Number of most significant digits/characters to strip from the dialed number.
	 * \note Feature is deprecated.  Use dialplan logic.
	 * \note The characters are stripped before the PRI TON/NPI prefix
	 * characters are processed.
	 */
	int stripmsd;

	/*! \brief Echo cancel parameters. */
	struct {
		struct dahdi_echocanparams head;
		struct dahdi_echocanparam params[DAHDI_MAX_ECHOCANPARAMS];
	} echocancel;

	/*!
	 * \brief Number of times to see "busy" tone before hanging up.
	 * \note Set from the "busycount" value read in from chan_extra.conf
	 */
	int busycount;
#if (ASTERISK_VERSION_NUM >= 100000)
	/*!
	 * \brief Busy cadence pattern description.
	 * \note Set from the "busypattern" value read from chan_dahdi.conf
	 */
	struct ast_dsp_busy_pattern busy_cadence;
#else
	/*!
	 * \brief Length of "busy" tone on time.
	 * \note Set from the "busypattern" value read in from chan_extra.conf
	 */
	int busy_tonelength;
	/*!
	 * \brief Length of "busy" tone off time.
	 * \note Set from the "busypattern" value read in from chan_extra.conf
	 */
	int busy_quietlength;
#endif

	/*! \brief Opaque DSP configuration structure. */
	struct ast_dsp *dsp;
	/*! \brief EXTRA dial operation command struct for ioctl() call. */
	struct dahdi_dialoperation dop;

	char accountcode[AST_MAX_ACCOUNT_CODE];		/*!< Account code */
	int amaflags;					/*!< AMA Flags */
	struct tdd_state *tdd;				/*!< TDD flag */

	/*! \brief Delayed dialing for E911.  Overlap digits for ISDN. */
	char dialdest[256];

	int distinctivering;				/*!< Which distinctivering to use */
	int dtmfrelax;					/*!< whether to run in relaxed DTMF mode */

#ifdef HAVE_GSMAT
	/*! \brief EXTRA GSM control parameters */
	struct extra_gsm *gsm;
	/*! \brief Opaque libpri call control structure */
	struct at_call *gsmcall;
	/*! \brief Channel number in span. */
	int gsmoffset;
#endif	

	/*! \brief DSP feature flags: DSP_FEATURE_xxx */
	int dsp_features;
	/*! \brief DTMF digit in progress.  0 when no digit in progress. */
	char begindigit;
	
#if (ASTERISK_VERSION_NUM >= 10601)
	/*! \brief TRUE if confrence is muted. */
	int muting;
#endif //(ASTERISK_VERSION_NUM >= 10601)

#if (ASTERISK_VERSION_NUM >= 10800)
	struct ast_cc_config_params *cc_params;
#endif //(ASTERISK_VERSION_NUM >= 10800)
	/* EXTRA channel names may differ greatly from the
	 * string that was provided to an app such as Dial. We
	 * need to save the original string passed to extra_request
	 * for call completion purposes. This way, we can replicate
	 * the original dialed string later.
	 */
	char dialstring[AST_CHANNEL_NAME];
	//Freedom Add for music on hold 2012-04-24 15:34
	//////////////////////////////////////////////////////////////////
	/*!
	 * \brief The configured music-on-hold class to use for calls.
	 * \note The "musicclass" or "mohinterpret" or "musiconhold" string read in from chan_dahdi.conf
	 */
	char mohinterpret[MAX_MUSICCLASS];
//////////////////////////////////////////////////////////////////
};

#if (ASTERISK_VERSION_NUM >= 10800)
#define DATA_EXPORT_EXTRA_PVT(MEMBER)					\
	MEMBER(extra_pvt, use_callerid, AST_DATA_BOOLEAN)	\
	MEMBER(extra_pvt, cid_rxgain, AST_DATA_DOUBLE)			\
	MEMBER(extra_pvt, rxgain, AST_DATA_DOUBLE)			\
	MEMBER(extra_pvt, txgain, AST_DATA_DOUBLE)			\
	MEMBER(extra_pvt, txdrc, AST_DATA_DOUBLE)			\
	MEMBER(extra_pvt, rxdrc, AST_DATA_DOUBLE)			\
	MEMBER(extra_pvt, adsi, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, answeronpolarityswitch, AST_DATA_BOOLEAN)	\
	MEMBER(extra_pvt, busydetect, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, callreturn, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, cancallforward, AST_DATA_BOOLEAN)		\
	MEMBER(extra_pvt, canpark, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, confirmanswer, AST_DATA_BOOLEAN)		\
	MEMBER(extra_pvt, destroy, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, didtdd, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, dialednone, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, dialing, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, digital, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, dnd, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, echocanbridged, AST_DATA_BOOLEAN)		\
	MEMBER(extra_pvt, echocanon, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, faxhandled, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, usefaxbuffers, AST_DATA_BOOLEAN)		\
	MEMBER(extra_pvt, bufferoverrideinuse, AST_DATA_BOOLEAN)	\
	MEMBER(extra_pvt, firstradio, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, hanguponpolarityswitch, AST_DATA_BOOLEAN)	\
	MEMBER(extra_pvt, hardwaredtmf, AST_DATA_BOOLEAN)		\
	MEMBER(extra_pvt, ignoredtmf, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, immediate, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, inalarm, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, mate, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, outgoing, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, priindication_oob, AST_DATA_BOOLEAN)		\
	MEMBER(extra_pvt, pulse, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, pulsedial, AST_DATA_BOOLEAN)			\
	MEMBER(extra_pvt, restartpending, AST_DATA_BOOLEAN)		\
	MEMBER(extra_pvt, inservice, AST_DATA_BOOLEAN)				\
	MEMBER(extra_pvt, manages_span_alarms, AST_DATA_BOOLEAN)		\
	MEMBER(extra_pvt, context, AST_DATA_STRING)				\
	MEMBER(extra_pvt, defcontext, AST_DATA_STRING)				\
	MEMBER(extra_pvt, exten, AST_DATA_STRING)				\
	MEMBER(extra_pvt, language, AST_DATA_STRING)
AST_DATA_STRUCTURE(extra_pvt, DATA_EXPORT_EXTRA_PVT);
#endif //(ASTERISK_VERSION_NUM >= 10800)

static struct extra_pvt *iflist = NULL;	/*!< Main interface list start */
static struct extra_pvt *ifend = NULL;	/*!< Main interface list end */

/*! \brief Channel configuration from chan_extra.conf .
 * This struct is used for parsing the [channels] section of chan_extra.conf.
 * Generally there is a field here for every possible configuration item.
 *
 * The state of fields is saved along the parsing and whenever a 'channel'
 * statement is reached, the current extra_chan_conf is used to configure the
 * channel (struct extra_pvt)
 *
 * \see extra_chan_init for the default values.
 */
struct extra_chan_conf {
	struct extra_pvt chan;

#ifdef HAVE_GSMAT
	struct extra_gsm gsm;
#endif

	int is_sig_auto; /*!< Use channel signalling from DAHDI? */
	/*! Continue configuration even if a channel is not there. */
	int ignore_failed_channels;
};

/*! returns a new dahdi_chan_conf with default values (by-value) */
static struct extra_chan_conf extra_chan_conf_default(void)
{
	/* recall that if a field is not included here it is initialized
	 * to 0 or equivalent
	 */
	struct extra_chan_conf conf = {

#ifdef HAVE_GSMAT
		.gsm = {
			/*Freedom Modify 2011-10-10 10:11*/
			//.switchtype = GSM_SWITCH_M20,//GSM_SWITCH_SIM340DZ,
			.switchtype = -1,
			.nodetype = GSM_CPE,
			.resetinterval = -1,
			.vol=-1,
			.mic=-1,
		},
#endif
		.chan = {
			.context = "default",
			.cid_num = "",
			.cid_name = "",
			.use_callerid = 1,
			.sig = -1,
			.outsigmod = -1,

			.cid_rxgain = +5.0,

			.tonezone = -1,

			.echocancel.head.tap_length = 1,

			.busycount = 3,

			.accountcode = "",

			.buf_policy = DAHDI_POLICY_IMMEDIATE,
			.buf_no = numbufs,
			.usefaxbuffers = 0,
#if (ASTERISK_VERSION_NUM >= 10800)
			.cc_params = ast_cc_config_params_init(),
#endif //(ASTERISK_VERSION_NUM >= 10800)
		},

		.is_sig_auto = 1,
	};

	return conf;
}

#if (ASTERISK_VERSION_NUM >= 110000)
static struct ast_channel *extra_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause);
#elif (ASTERISK_VERSION_NUM >= 100000)
static struct ast_channel *extra_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, void *data, int *cause);
#elif (ASTERISK_VERSION_NUM >= 10800)
static struct ast_channel *extra_request(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *cause);
#else  //(ASTERISK_VERSION_NUM >= 10800)
static struct ast_channel *extra_request(const char *type, int format, void *data, int *cause);
#endif //(ASTERISK_VERSION_NUM >= 10800)

static int extra_digit_begin(struct ast_channel *ast, char digit);
static int extra_digit_end(struct ast_channel *ast, char digit, unsigned int duration);

#if (ASTERISK_VERSION_NUM >= 110000)
static int extra_call(struct ast_channel *ast, const char *rdest, int timeout);
#else  //(ASTERISK_VERSION_NUM >= 110000)
static int extra_call(struct ast_channel *ast, char *rdest, int timeout);
#endif //(ASTERISK_VERSION_NUM >= 110000)

static int extra_hangup(struct ast_channel *ast);
static int extra_answer(struct ast_channel *ast);
static struct ast_frame *extra_read(struct ast_channel *ast);
static int extra_write(struct ast_channel *ast, struct ast_frame *frame);
static struct ast_frame *extra_exception(struct ast_channel *ast);
static int extra_send_text(struct ast_channel *ast, const char *text);

static int extra_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);

static int extra_indicate(struct ast_channel *chan, int condition, const void *data, size_t datalen);

static int extra_setoption(struct ast_channel *chan, int option, void *data, int datalen);
#if (ASTERISK_VERSION_NUM >= 10800)
static int extra_queryoption(struct ast_channel *chan, int option, void *data, int *datalen);
#endif //(ASTERISK_VERSION_NUM >= 10800)

#if (ASTERISK_VERSION_NUM >= 100000)
static struct ast_channel_tech extra_tech = {
#else
static const struct ast_channel_tech extra_tech = {
#endif
	.type = "EXTRA",
	.description = tdesc,

#if (ASTERISK_VERSION_NUM >= 100000)
#else
	.capabilities = AST_FORMAT_SLINEAR | AST_FORMAT_ULAW | AST_FORMAT_ALAW,
#endif

	.requester = extra_request,
	.send_digit_begin = extra_digit_begin,
	.send_digit_end = extra_digit_end,
	.call = extra_call,
	.hangup = extra_hangup,
	.answer = extra_answer,
	.read = extra_read,
	.write = extra_write,
	.indicate = extra_indicate,
	.setoption = extra_setoption,
	.fixup = extra_fixup,
	.exception = extra_exception,
	.send_text = extra_send_text,
#if (ASTERISK_VERSION_NUM >= 10800)
	.queryoption = extra_queryoption,
#endif //(ASTERISK_VERSION_NUM >= 10800)
};

#define GET_CHANNEL(p) ((p)->channel)

#ifdef HAVE_GSMAT
static inline int gsm_grab(struct extra_pvt *pvt, struct extra_gsm *gsm)
{	
	int res;

	/* Grab the lock first */
	do {
		res = ast_mutex_trylock(&gsm->lock);
		if (res) {
			DEADLOCK_AVOIDANCE(&pvt->lock);
		}
	} while (res);

	/* Then break the poll */
	if (gsm->master != AST_PTHREADT_NULL)
		pthread_kill(gsm->master, SIGURG);
	return 0;
}
#endif

static int extra_setlinear(int dfd, int linear);

static const char *event2str(int event);
static int restore_gains(struct extra_pvt *p);

static void wakeup_sub(struct extra_pvt *p, int a);

//Freedom del 2012-02-02 13:49
#if 0
static int reset_conf(struct extra_pvt *p);
#endif

static inline int extra_confmute(struct extra_pvt *p, int muted);

static int get_alarms(struct extra_pvt *p);
static void handle_alarms(struct extra_pvt *p, int alms);

//Freedom del 2012-02-02 13:49
#if 0
static int conf_del(struct extra_pvt *p, struct extra_subchannel *c, int index);

static int conf_add(struct extra_pvt *p, struct extra_subchannel *c, int index, int slavechannel);
#endif

//Freedom del 2012-02-02 13:49
#if 0
static int isslavenative(struct extra_pvt *p, struct extra_pvt **out);
#endif

static struct ast_channel *extra_new(struct extra_pvt *i, int state, int startpbx, int idx, int law, const char *linkedid);

static int set_actual_gain(int fd, float rxgain, float txgain, float rxdrc, float txdrc, int law);

static int unalloc_sub(struct extra_pvt *p, int x);

static inline int extra_wait_event(int fd);

static void extra_enable_ec(struct extra_pvt *p);
static void extra_disable_ec(struct extra_pvt *p);

static inline int extra_set_hook(int fd, int hs);

/*! Round robin search locations. */
static struct extra_pvt *round_robin[32];

#define extra_get_index(ast, p, nullok)	_extra_get_index(ast, p, nullok, __PRETTY_FUNCTION__, __LINE__)
static int _extra_get_index(struct ast_channel *ast, struct extra_pvt *p, int nullok, const char *fname, unsigned long line)
{
	int res;
	if (p->subs[SUB_REAL].owner == ast)
		res = 0;
	else if (p->subs[SUB_CALLWAIT].owner == ast)
		res = 1;
	else if (p->subs[SUB_THREEWAY].owner == ast)
		res = 2;
	else if (p->subs[SUB_SMS].owner == ast)
		res = 3;
	else if (p->subs[SUB_SMSSEND].owner == ast)
		res = 4;
	else {
		res = -1;

#if (ASTERISK_VERSION_NUM >= 110000)
	   	if (!nullok)
			ast_log(LOG_WARNING,
				"Unable to get index for '%s' on channel %d (%s(), line %lu)\n",
				ast ? ast_channel_name(ast) : "", p->channel, fname, line);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		if (!nullok)
			ast_log(LOG_WARNING,
				"Unable to get index for '%s' on channel %d (%s(), line %lu)\n",
				ast ? ast->name : "", p->channel, fname, line);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	}
	
	return res;
}

/*!
 * \internal
 * \brief Obtain the specified subchannel owner lock if the owner exists.
 *
 * \param pvt Channel private struct.
 * \param sub_idx Subchannel owner to lock.
 *
 * \note Assumes the pvt->lock is already obtained.
 *
 * \note
 * Because deadlock avoidance may have been necessary, you need to confirm
 * the state of things before continuing.
 *
 * \return Nothing
 */
static void extra_lock_sub_owner(struct extra_pvt *pvt, int sub_idx)
{
	for (;;) {
		if (!pvt->subs[sub_idx].owner) {
			/* No subchannel owner pointer */
			break;
		}
		if (!ast_channel_trylock(pvt->subs[sub_idx].owner)) {
			/* Got subchannel owner lock */
			break;
		}
		/* We must unlock the private to avoid the possibility of a deadlock */
		DEADLOCK_AVOIDANCE(&pvt->lock);
	}
}

static void wakeup_sub(struct extra_pvt *p, int a)
{
	extra_lock_sub_owner(p, a);
	if (p->subs[a].owner) {
		ast_queue_frame(p->subs[a].owner, &ast_null_frame);
		ast_channel_unlock(p->subs[a].owner);
	}
}

static void extra_queue_frame(struct extra_pvt *p, struct ast_frame *f)
{
	for (;;) {
		if (p->owner) {
			if (ast_channel_trylock(p->owner)) {
				DEADLOCK_AVOIDANCE(&p->lock);
			} else {
				ast_queue_frame(p->owner, f);
				ast_channel_unlock(p->owner);
				break;
			}
		} else
			break;
	}
}

static void swap_subs(struct extra_pvt *p, int a, int b)
{
	int tchan;
	int tinthreeway;
	struct ast_channel *towner;

	ast_debug(1, "Swapping %d and %d\n", a, b);

	tchan = p->subs[a].chan;
	towner = p->subs[a].owner;
	tinthreeway = p->subs[a].inthreeway;

	p->subs[a].chan = p->subs[b].chan;
	p->subs[a].owner = p->subs[b].owner;
	p->subs[a].inthreeway = p->subs[b].inthreeway;

	p->subs[b].chan = tchan;
	p->subs[b].owner = towner;
	p->subs[b].inthreeway = tinthreeway;

#if (ASTERISK_VERSION_NUM > 10444)
	if (p->subs[a].owner)
		ast_channel_set_fd(p->subs[a].owner, 0, p->subs[a].dfd);
	if (p->subs[b].owner)
		ast_channel_set_fd(p->subs[b].owner, 0, p->subs[b].dfd);
#else  //(ASTERISK_VERSION_NUM > 10444)
	if (p->subs[a].owner)
		p->subs[a].owner->fds[0] = p->subs[a].dfd;
	if (p->subs[b].owner)
		p->subs[b].owner->fds[0] = p->subs[b].dfd;
#endif //(ASTERISK_VERSION_NUM > 10444)
	wakeup_sub(p, a);
	wakeup_sub(p, b);
}

static int extra_open(char *fn)
{
	int fd;
	int isnum;
	int chan = 0;
	int bs;
	int x;
	isnum = 1;
	for (x = 0; x < strlen(fn); x++) {
		if (!isdigit(fn[x])) {
			isnum = 0;
			break;
		}
	}
	if (isnum) {
		chan = atoi(fn);
		if (chan < 1) {
			ast_log(LOG_WARNING, "Invalid channel number '%s'\n", fn);
			return -1;
		}
		fn = "/dev/dahdi/channel";
	}
	fd = open(fn, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		ast_log(LOG_WARNING, "Unable to open '%s': %s\n", fn, strerror(errno));
		return -1;
	}
	if (chan) {
		if (ioctl(fd, DAHDI_SPECIFY, &chan)) {
			x = errno;
			close(fd);
			errno = x;
			ast_log(LOG_WARNING, "Unable to specify channel %d: %s\n", chan, strerror(errno));
			return -1;
		}
	}
	bs = READ_SIZE;
	if (ioctl(fd, DAHDI_SET_BLOCKSIZE, &bs) == -1) {
		ast_log(LOG_WARNING, "Unable to set blocksize '%d': %s\n", bs,  strerror(errno));
		x = errno;
		close(fd);
		errno = x;
		return -1;
	}
	return fd;
}

static void extra_close(int fd)
{
	if (fd > 0)
		close(fd);
}

static void extra_close_sub(struct extra_pvt *chan_pvt, int sub_num)
{
	extra_close(chan_pvt->subs[sub_num].dfd);
	chan_pvt->subs[sub_num].dfd = -1;
}

#ifdef HAVE_GSMAT
static void extra_close_gsm_fd(struct extra_gsm *gsm)
{
	extra_close(gsm->fd);
	gsm->fd = -1;
}
#endif

static int extra_setlinear(int dfd, int linear)
{
	return ioctl(dfd, DAHDI_SETLINEAR, &linear);
}

static int unalloc_sub(struct extra_pvt *p, int x)
{
	if (!x) {
		ast_log(LOG_WARNING, "Trying to unalloc the real channel %d?!?\n", p->channel);
		return -1;
	}
	ast_debug(1, "Released sub %d of channel %d\n", x, p->channel);
	extra_close_sub(p, x);
	p->subs[x].linear = 0;
	p->subs[x].chan = 0;
	p->subs[x].owner = NULL;
	p->subs[x].inthreeway = 0;
	memset(&p->subs[x].curconf, 0, sizeof(p->subs[x].curconf));
	return 0;
}

//Freedom Modify 2012-09-03 14:23 for send dtmf mode
//Generate DTMF from AT command 
#define USE_AT_DTMF

#ifdef USE_AT_DTMF
static int extra_digit_begin(struct ast_channel *chan, char digit)
{
	struct extra_pvt *pvt;

#if (ASTERISK_VERSION_NUM >= 110000)
	pvt = ast_channel_tech_pvt(chan);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	pvt = chan->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	gsm_senddtmf(pvt->gsm->gsm, digit);
	return 0;
}

static int extra_digit_end(struct ast_channel *chan, char digit, unsigned int duration)
{
	return 0;
}

#else

static int digit_to_dtmfindex(char digit)
{
	if (isdigit(digit))
		return DAHDI_TONE_DTMF_BASE + (digit - '0');
	else if (digit >= 'A' && digit <= 'D')
		return DAHDI_TONE_DTMF_A + (digit - 'A');
	else if (digit >= 'a' && digit <= 'd')
		return DAHDI_TONE_DTMF_A + (digit - 'a');
	else if (digit == '*')
		return DAHDI_TONE_DTMF_s;
	else if (digit == '#')
		return DAHDI_TONE_DTMF_p;
	else
		return -1;
}

static int extra_digit_begin(struct ast_channel *chan, char digit)
{
	struct extra_pvt *pvt;
	int idx;
	int dtmf = -1;
	int res;

#if (ASTERISK_VERSION_NUM >= 110000)
	pvt = ast_channel_tech_pvt(chan);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	pvt = chan->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	ast_mutex_lock(&pvt->lock);

	idx = extra_get_index(chan, pvt, 0);

	if ((idx != SUB_REAL) || !pvt->owner)
		goto out;

	if ((dtmf = digit_to_dtmfindex(digit)) == -1)
		goto out;
	
	if (pvt->pulse || ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_SENDTONE, &dtmf)) {
		struct dahdi_dialoperation zo = {
			.op = DAHDI_DIAL_OP_APPEND,
		};

		zo.dialstr[0] = 'T';
		zo.dialstr[1] = digit;
		zo.dialstr[2] = '\0';
		if ((res = ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_DIAL, &zo)))
			ast_log(LOG_WARNING, "Couldn't dial digit %c: %s\n", digit, strerror(errno));
		else
			pvt->dialing = 1;
	} else {
		ast_debug(1, "Started VLDTMF digit '%c'\n", digit);
		pvt->dialing = 1;
		pvt->begindigit = digit;
	}

out:
	ast_mutex_unlock(&pvt->lock);
	return 0;
}

static int extra_digit_end(struct ast_channel *chan, char digit, unsigned int duration)
{
	struct extra_pvt *pvt;
	int res = 0;
	int idx;
	int x;

#if (ASTERISK_VERSION_NUM >= 110000)
	pvt = ast_channel_tech_pvt(chan);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	pvt = chan->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	ast_mutex_lock(&pvt->lock);

	idx = extra_get_index(chan, pvt, 0);

	if ((idx != SUB_REAL) || !pvt->owner || pvt->pulse)
		goto out;

	if (pvt->begindigit) {
		x = -1;
		ast_debug(1, "Ending VLDTMF digit '%c'\n", digit);
		res = ioctl(pvt->subs[SUB_REAL].dfd, DAHDI_SENDTONE, &x);
		pvt->dialing = 0;
		pvt->begindigit = 0;
	}

out:
	ast_mutex_unlock(&pvt->lock);

	return res;
}
#endif //USE_AT_DTMF

static const char * const events[] = {
	"No event",
	"On hook",
	"Ring/Answered",
	"Wink/Flash",
	"Alarm",
	"No more alarm",
	"HDLC Abort",
	"HDLC Overrun",
	"HDLC Bad FCS",
	"Dial Complete",
	"Ringer On",
	"Ringer Off",
	"Hook Transition Complete",
	"Bits Changed",
	"Pulse Start",
	"Timer Expired",
	"Timer Ping",
	"Polarity Reversal",
	"Ring Begin",
};

static struct {
	int alarm;
	char *name;
} alarms[] = {
	{ DAHDI_ALARM_RED, "Red Alarm" },
	{ DAHDI_ALARM_YELLOW, "Yellow Alarm" },
	{ DAHDI_ALARM_BLUE, "Blue Alarm" },
	{ DAHDI_ALARM_RECOVER, "Recovering" },
	{ DAHDI_ALARM_LOOPBACK, "Loopback" },
	{ DAHDI_ALARM_NOTOPEN, "Not Open" },
	{ DAHDI_ALARM_NONE, "None" },
};

static char *alarm2str(int alm)
{
	int x;
	for (x = 0; x < ARRAY_LEN(alarms); x++) {
		if (alarms[x].alarm & alm)
			return alarms[x].name;
	}
	return alm ? "Unknown Alarm" : "No Alarm";
}

static const char *event2str(int event)
{
	static char buf[256];
	if ((event < (ARRAY_LEN(events))) && (event > -1))
		return events[event];
	sprintf(buf, "Event %d", event); /* safe */
	return buf;
}

static char *extra_sig2str(int sig)
{
	static char buf[256];
	switch (sig) {
	case SIG_GSM_EXTRA:
		return "GSM";
	case 0:
		return "Pseudo";
	default:
		snprintf(buf, sizeof(buf), "Unknown signalling %d", sig);
		return buf;
	}
}

#define sig2str extra_sig2str

//Freedom del 2012-02-02 13:49
#if 0
static int conf_add(struct extra_pvt *p, struct extra_subchannel *c, int idx, int slavechannel)
{
	/* If the conference already exists, and we're already in it
	   don't bother doing anything */
	struct dahdi_confinfo zi;

	memset(&zi, 0, sizeof(zi));
	zi.chan = 0;

	if (slavechannel > 0) {
		/* If we have only one slave, do a digital mon */
		zi.confmode = DAHDI_CONF_DIGITALMON;
		zi.confno = slavechannel;
	} else {
		if (!idx) {
			/* Real-side and pseudo-side both participate in conference */
			zi.confmode = DAHDI_CONF_REALANDPSEUDO | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER |
				DAHDI_CONF_PSEUDO_TALKER | DAHDI_CONF_PSEUDO_LISTENER;
		} else
			zi.confmode = DAHDI_CONF_CONF | DAHDI_CONF_TALKER | DAHDI_CONF_LISTENER;
		zi.confno = p->confno;
	}
	if ((zi.confno == c->curconf.confno) && (zi.confmode == c->curconf.confmode))
		return 0;
	if (c->dfd < 0)
		return 0;
	if (ioctl(c->dfd, DAHDI_SETCONF, &zi)) {
		ast_log(LOG_WARNING, "Failed to add %d to conference %d/%d: %s\n", c->dfd, zi.confmode, zi.confno, strerror(errno));
		return -1;
	}
	if (slavechannel < 1) {
		p->confno = zi.confno;
	}
	c->curconf = zi;
	ast_debug(1, "Added %d to conference %d/%d\n", c->dfd, c->curconf.confmode, c->curconf.confno);
	return 0;
}
#endif

//Freedom del 2012-02-02 13:49
#if 0
static int isourconf(struct extra_pvt *p, struct extra_subchannel *c)
{
	/* If they're listening to our channel, they're ours */
	if ((p->channel == c->curconf.confno) && (c->curconf.confmode == DAHDI_CONF_DIGITALMON))
		return 1;
	/* If they're a talker on our (allocated) conference, they're ours */
	if ((p->confno > 0) && (p->confno == c->curconf.confno) && (c->curconf.confmode & DAHDI_CONF_TALKER))
		return 1;
	return 0;
}
#endif

//Freedom del 2012-02-02 13:49
#if 0
static int conf_del(struct extra_pvt *p, struct extra_subchannel *c, int idx)
{
	struct dahdi_confinfo zi;
	if (/* Can't delete if there's no dfd */
		(c->dfd < 0) ||
		/* Don't delete from the conference if it's not our conference */
		!isourconf(p, c)
		/* Don't delete if we don't think it's conferenced at all (implied) */
		) return 0;
	memset(&zi, 0, sizeof(zi));
	if (ioctl(c->dfd, DAHDI_SETCONF, &zi)) {
		ast_log(LOG_WARNING, "Failed to drop %d from conference %d/%d: %s\n", c->dfd, c->curconf.confmode, c->curconf.confno, strerror(errno));
		return -1;
	}
	ast_debug(1, "Removed %d from conference %d/%d\n", c->dfd, c->curconf.confmode, c->curconf.confno);
	memcpy(&c->curconf, &zi, sizeof(c->curconf));
	return 0;
}
#endif

//Freedom del 2012-02-02 13:49
#if 0
static int isslavenative(struct extra_pvt *p, struct extra_pvt **out)
{
	int x;
	int useslavenative;
	struct extra_pvt *slave = NULL;
	/* Start out optimistic */
	useslavenative = 1;
	/* Update conference state in a stateless fashion */
	for (x = 0; x < SUB_SUM; x++) {
		/* Any three-way calling makes slave native mode *definitely* out
		   of the question */
		if ((p->subs[x].dfd > -1) && p->subs[x].inthreeway)
			useslavenative = 0;
	}
	/* If we don't have any 3-way calls, check to see if we have
	   precisely one slave */
	if (useslavenative) {
		for (x = 0; x < MAX_SLAVES; x++) {
			if (p->slaves[x]) {
				if (slave) {
					/* Whoops already have a slave!  No
					   slave native and stop right away */
					slave = NULL;
					useslavenative = 0;
					break;
				} else {
					/* We have one slave so far */
					slave = p->slaves[x];
				}
			}
		}
	}
	/* If no slave, slave native definitely out */
	if (!slave)
		useslavenative = 0;
	else if (slave->law != p->law) {
		useslavenative = 0;
		slave = NULL;
	}
	if (out)
		*out = slave;
	return useslavenative;
}
#endif

//Freedom del 2012-02-02 13:49
#if 0
static int reset_conf(struct extra_pvt *p)
{
	p->confno = -1;
	memset(&p->subs[SUB_REAL].curconf, 0, sizeof(p->subs[SUB_REAL].curconf));
	if (p->subs[SUB_REAL].dfd > -1) {
		struct dahdi_confinfo zi;

		memset(&zi, 0, sizeof(zi));
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETCONF, &zi))
			ast_log(LOG_WARNING, "Failed to reset conferencing on channel %d: %s\n", p->channel, strerror(errno));
	}
	return 0;
}
#endif

//Freedom del 2012-02-02 13:49
#if 0
static int update_conf(struct extra_pvt *p)
{
	int needconf = 0;
	int x;
	int useslavenative;
	struct extra_pvt *slave = NULL;

	useslavenative = isslavenative(p, &slave);
	/* Start with the obvious, general stuff */
	for (x = 0; x < SUB_SUM; x++) {
		/* Look for three way calls */
		if ((p->subs[x].dfd > -1) && p->subs[x].inthreeway) {
			conf_add(p, &p->subs[x], x, 0);
			needconf++;
		} else {
			conf_del(p, &p->subs[x], x);
		}
	}
	/* If we have a slave, add him to our conference now. or DAX
	   if this is slave native */
	for (x = 0; x < MAX_SLAVES; x++) {
		if (p->slaves[x]) {
			if (useslavenative)
				conf_add(p, &p->slaves[x]->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(p));
			else {
				conf_add(p, &p->slaves[x]->subs[SUB_REAL], SUB_REAL, 0);
				needconf++;
			}
		}
	}
	/* If we're supposed to be in there, do so now */
	if (!p->subs[SUB_REAL].inthreeway) {
		if (useslavenative)
			conf_add(p, &p->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(slave));
		else {
			conf_add(p, &p->subs[SUB_REAL], SUB_REAL, 0);
			needconf++;
		}
	}
	/* If we have a master, add ourselves to his conference */
	if (p->master) {
		if (isslavenative(p->master, NULL)) {
			conf_add(p->master, &p->subs[SUB_REAL], SUB_REAL, GET_CHANNEL(p->master));
		} else {
			conf_add(p->master, &p->subs[SUB_REAL], SUB_REAL, 0);
		}
	}
	if (!needconf) {
		/* Nobody is left (or should be left) in our conference.
		   Kill it. */
		p->confno = -1;
	}
	ast_debug(1, "Updated conferencing on %d, with %d conference users\n", p->channel, needconf);
	return 0;
}
#endif

static void extra_enable_ec(struct extra_pvt *p)
{
	int res;
	if (!p)
		return;
	if (p->echocanon) {
		ast_debug(1, "Echo cancellation already on\n");
		return;
	}
	if (p->digital) {
		ast_debug(1, "Echo cancellation isn't required on digital connection\n");
		return;
	}
	if (p->echocancel.head.tap_length) {
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOCANCEL_PARAMS, &p->echocancel);
		if (res) {
			ast_log(LOG_WARNING, "Unable to enable echo cancellation on channel %d (%s)\n", p->channel, strerror(errno));
		} else {
			p->echocanon = 1;
			ast_debug(1, "Enabled echo cancellation on channel %d\n", p->channel);
		}
	} else
		ast_debug(1, "No echo cancellation requested\n");
}

static void extra_disable_ec(struct extra_pvt *p)
{
	int res;

	if (p->echocanon) {
		struct dahdi_echocanparams ecp = { .tap_length = 0 };

		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_ECHOCANCEL_PARAMS, &ecp);

		if (res)
			ast_log(LOG_WARNING, "Unable to disable echo cancellation on channel %d: %s\n", p->channel, strerror(errno));
		else
			ast_debug(1, "Disabled echo cancellation on channel %d\n", p->channel);
	}

	p->echocanon = 0;
}

/* perform a dynamic range compression transform on the given sample */
static int drc_sample(int sample, float drc)
{
	float neg;
	float shallow, steep;
	float max = SHRT_MAX;
	
	neg = (sample < 0 ? -1 : 1);
	steep = drc*sample;
	shallow = neg*(max-max/drc)+(float)sample/drc;
	if (abs(steep) < abs(shallow)) {
		sample = steep;
	}
	else {
		sample = shallow;
	}

	return sample;
}

static void fill_txgain(struct dahdi_gains *g, float gain, float drc, int law)
{
	int j;
	int k;

	float linear_gain = pow(10.0, gain / 20.0);

	switch (law) {
	case DAHDI_LAW_ALAW:
		for (j = 0; j < ARRAY_LEN(g->txgain); j++) {
			if (gain || drc) {
				k = AST_ALAW(j);
				if (drc) {
					k = drc_sample(k, drc);
				}
				k = (float)k*linear_gain;
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->txgain[j] = AST_LIN2A(k);
			} else {
				g->txgain[j] = j;
			}
		}
		break;
	case DAHDI_LAW_MULAW:
		for (j = 0; j < ARRAY_LEN(g->txgain); j++) {
			if (gain || drc) {
				k = AST_MULAW(j);
				if (drc) {
					k = drc_sample(k, drc);
				}
				k = (float)k*linear_gain;
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->txgain[j] = AST_LIN2MU(k);

			} else {
				g->txgain[j] = j;
			}
		}
		break;
	}
}

static void fill_rxgain(struct dahdi_gains *g, float gain, float drc, int law)
{
	int j;
	int k;
	float linear_gain = pow(10.0, gain / 20.0);

	switch (law) {
	case DAHDI_LAW_ALAW:
		for (j = 0; j < ARRAY_LEN(g->rxgain); j++) {
			if (gain || drc) {
				k = AST_ALAW(j);
				if (drc) {
					k = drc_sample(k, drc);
				}
				k = (float)k*linear_gain;
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->rxgain[j] = AST_LIN2A(k);
			} else {
				g->rxgain[j] = j;
			}
		}
		break;
	case DAHDI_LAW_MULAW:
		for (j = 0; j < ARRAY_LEN(g->rxgain); j++) {
			if (gain || drc) {
				k = AST_MULAW(j);
				if (drc) {
					k = drc_sample(k, drc);
				}
				k = (float)k*linear_gain;
				if (k > 32767) k = 32767;
				if (k < -32767) k = -32767;
				g->rxgain[j] = AST_LIN2MU(k);
			} else {
				g->rxgain[j] = j;
			}
		}
		break;
	}
}

static int set_actual_txgain(int fd, float gain, float drc, int law)
{
	struct dahdi_gains g;
	int res;

	memset(&g, 0, sizeof(g));
	res = ioctl(fd, DAHDI_GETGAINS, &g);
	if (res) {
		ast_debug(1, "Failed to read gains: %s\n", strerror(errno));
		return res;
	}

	fill_txgain(&g, gain, drc, law);

	return ioctl(fd, DAHDI_SETGAINS, &g);
}

static int set_actual_rxgain(int fd, float gain, float drc, int law)
{
	struct dahdi_gains g;
	int res;

	memset(&g, 0, sizeof(g));
	res = ioctl(fd, DAHDI_GETGAINS, &g);
	if (res) {
		ast_debug(1, "Failed to read gains: %s\n", strerror(errno));
		return res;
	}

	fill_rxgain(&g, gain, drc, law);

	return ioctl(fd, DAHDI_SETGAINS, &g);
}

static int set_actual_gain(int fd, float rxgain, float txgain, float rxdrc, float txdrc, int law)
{
	return set_actual_txgain(fd, txgain, txdrc, law) | set_actual_rxgain(fd, rxgain, rxdrc, law);
}

static int restore_gains(struct extra_pvt *p)
{
	int res;

	res = set_actual_gain(p->subs[SUB_REAL].dfd, p->rxgain, p->txgain, p->rxdrc, p->txdrc, p->law);
	if (res) {
		ast_log(LOG_WARNING, "Unable to restore gains: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static inline int extra_set_hook(int fd, int hs)
{
	int x, res;

	x = hs;
	res = ioctl(fd, DAHDI_HOOK, &x);

	if (res < 0) {
		if (errno == EINPROGRESS)
			return 0;
		ast_log(LOG_WARNING, "EXTRA hook failed returned %d (trying %d): %s\n", res, hs, strerror(errno));
		/* will expectedly fail if phone is off hook during operation, such as during a restart */
	}

	return res;
}

static inline int extra_confmute(struct extra_pvt *p, int muted)
{
	int x, y,res;

	x = muted;
	
	if (p->sig == SIG_GSM_EXTRA) {
		y = 1;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &y);
		if (res)
			ast_log(LOG_WARNING, "Unable to set audio mode on %d: %s\n", p->channel, strerror(errno));
	}

	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_CONFMUTE, &x);
	if (res < 0)
		ast_log(LOG_WARNING, "EXTRA confmute(%d) failed on channel %d: %s\n", muted, p->channel, strerror(errno));
	return res;
}

#if (ASTERISK_VERSION_NUM >= 110000)
static int extra_call(struct ast_channel *ast, const char *rdest, int timeout)
#else  //(ASTERISK_VERSION_NUM >= 110000)
static int extra_call(struct ast_channel *ast, char *rdest, int timeout)
#endif //(ASTERISK_VERSION_NUM >= 110000)
{
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(ast);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)
	int x, res, mysig;
	char dest[256]; /* must be same length as p->dialdest */
#ifdef HAVE_GSMAT
    char *s = NULL;
#endif

	ast_mutex_lock(&p->lock);
	ast_copy_string(dest, rdest, sizeof(dest));
	ast_copy_string(p->dialdest, rdest, sizeof(p->dialdest));

#if (ASTERISK_VERSION_NUM >= 110000)
	if ((ast_channel_state(ast) == AST_STATE_BUSY)) {
#else  //(ASTERISK_VERSION_NUM >= 110000)
	if ((ast->_state == AST_STATE_BUSY)) {
#endif //(ASTERISK_VERSION_NUM >= 110000)
		p->subs[SUB_REAL].needbusy = 1;
		ast_mutex_unlock(&p->lock);
		return 0;
	}

#if (ASTERISK_VERSION_NUM >= 110000)
	if ((ast_channel_state(ast) != AST_STATE_DOWN) && (ast_channel_state(ast) != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "extra_call called on %s, neither down nor reserved\n", ast_channel_name(ast));
		ast_mutex_unlock(&p->lock);
		return -1;
	}
#else  //(ASTERISK_VERSION_NUM >= 110000)
	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "extra_call called on %s, neither down nor reserved\n", ast->name);
		ast_mutex_unlock(&p->lock);
		return -1;
	}
#endif //(ASTERISK_VERSION_NUM >= 110000)


	p->dialednone = 0;
	if ((p->radio || (p->oprmode < 0)))  /* if a radio channel, up immediately */
	{
		/* Special pseudo -- automatically up */
		ast_setstate(ast, AST_STATE_UP);
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	x = DAHDI_FLUSH_READ | DAHDI_FLUSH_WRITE;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_FLUSH, &x);
	if (res)
		ast_log(LOG_WARNING, "Unable to flush input on channel %d: %s\n", p->channel, strerror(errno));
	p->outgoing = 1;

#if (ASTERISK_VERSION_NUM >= 110000)
	if (IS_DIGITAL(ast_channel_transfercapability(ast))){
#else  //(ASTERISK_VERSION_NUM >= 110000)
	if (IS_DIGITAL(ast->transfercapability)){
#endif //(ASTERISK_VERSION_NUM >= 110000)
		set_actual_gain(p->subs[SUB_REAL].dfd, 0, 0, p->rxdrc, p->txdrc, p->law);
	} else {
		set_actual_gain(p->subs[SUB_REAL].dfd, p->rxgain, p->txgain, p->rxdrc, p->txdrc, p->law);
	}	

	mysig = p->outsigmod > -1 ? p->outsigmod : p->sig;
	switch (mysig) {
	case 0:
		/* Special pseudo -- automatically up*/
		ast_setstate(ast, AST_STATE_UP);
		break;
	case SIG_GSM_EXTRA:
		/* We'll get it in a moment -- but use dialdest to store pre-setup_ack digits */
		p->dialdest[0] = '\0';
		p->dialing = 1;
		break;		
	default:
		ast_debug(1, "not yet implemented\n");
		ast_mutex_unlock(&p->lock);
		return -1;
	}
#ifdef HAVE_GSMAT
	if (p->gsm) {
		struct gsm_sr *sr;
		char *c;
		int ldp_strip;
		int exclusive;

		c = strchr(dest, '/');
		if (c) {
			c++;
		} else {
			c = "";
		}
	
		if (strlen(c) < p->stripmsd) {
			ast_log(LOG_WARNING, "Number '%s' is shorter than stripmsd (%d)\n", c, p->stripmsd);
			ast_mutex_unlock(&p->lock);
			return -1;
		}
		p->dop.op = DAHDI_DIAL_OP_REPLACE;
		s = strchr(c + p->stripmsd, 'w');
		if (s) {
			if (strlen(s) > 1)
				snprintf(p->dop.dialstr, sizeof(p->dop.dialstr), "T%s", s);
			else
				p->dop.dialstr[0] = '\0';
			*s = '\0';
		} else {
			p->dop.dialstr[0] = '\0';
		}
		if (gsm_grab(p, p->gsm)) {
			ast_log(LOG_WARNING, "Failed to grab GSM!\n");
			ast_mutex_unlock(&p->lock);
			return -1;
		}
		if (!(p->gsmcall = gsm_new_call(p->gsm->gsm))) {
			ast_log(LOG_WARNING, "Unable to create call on channel %d\n", p->channel);
			gsm_rel(p->gsm);
			ast_mutex_unlock(&p->lock);
			return -1;
		}
		if (!(sr = gsm_sr_new())) {
			ast_log(LOG_WARNING, "Failed to allocate setup request channel %d\n", p->channel);
			gsm_rel(p->gsm);
			ast_mutex_unlock(&p->lock);
		}

#if (ASTERISK_VERSION_NUM >= 110000)
		p->digital = IS_DIGITAL(ast_channel_transfercapability(ast));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		p->digital = IS_DIGITAL(ast->transfercapability);
#endif //(ASTERISK_VERSION_NUM >= 110000)
		exclusive = 0;

		gsm_sr_set_channel(sr, GSM_PVT_TO_CHANNEL(p), exclusive, 1);

#if (ASTERISK_VERSION_NUM >= 110000)
		ast_verb(3, "Requested transfer capability: 0x%.2x - %s\n", ast_channel_transfercapability(ast), ast_transfercapability2str(ast_channel_transfercapability(ast)));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_verb(3, "Requested transfer capability: 0x%.2x - %s\n", ast->transfercapability, ast_transfercapability2str(ast->transfercapability));
#endif //(ASTERISK_VERSION_NUM >= 110000)

		gsm_sr_set_called(sr, c , s ? 1 : 0);

		ldp_strip = 0;

		if (gsm_setup(p->gsm->gsm, p->gsmcall, sr)) {
			ast_log(LOG_WARNING, "Unable to setup call to %s \n", c);
			gsm_rel(p->gsm);
			ast_mutex_unlock(&p->lock);
			gsm_sr_free(sr);
			return -1;
		}
		gsm_sr_free(sr);
		ast_setstate(ast, AST_STATE_DIALING);
		gsm_rel(p->gsm);
	}
#endif	

	ast_mutex_unlock(&p->lock);
	return 0;
}

/*!
 * \internal
 * \brief Insert the given chan_extra interface structure into the interface list.
 * \since 1.8
 *
 * \param pvt chan_extra private interface structure to insert.
 *
 * \details
 * The interface list is a doubly linked list sorted by the chan_extra channel number.
 * Any duplicates are inserted after the existing entries.
 *
 * \note The new interface must not already be in the list.
 *
 * \return Nothing
 */
static void extra_iflist_insert(struct extra_pvt *pvt)
{
	struct extra_pvt *cur;

	pvt->which_iflist = EXTRA_IFLIST_MAIN;

	/* Find place in middle of list for the new interface. */
	for (cur = iflist; cur; cur = cur->next) {
		if (pvt->channel < cur->channel) {
			/* New interface goes before the current interface. */
			pvt->prev = cur->prev;
			pvt->next = cur;
			if (cur->prev) {
				/* Insert into the middle of the list. */
				cur->prev->next = pvt;
			} else {
				/* Insert at head of list. */
				iflist = pvt;
			}
			cur->prev = pvt;
			return;
		}
	}

	/* New interface goes onto the end of the list */
	pvt->prev = ifend;
	pvt->next = NULL;
	if (ifend) {
		ifend->next = pvt;
	}
	ifend = pvt;
	if (!iflist) {
		/* List was empty */
		iflist = pvt;
	}
}

/*!
 * \internal
 * \brief Extract the given chan_extra interface structure from the interface list.
 * \since 1.8
 *
 * \param pvt chan_extra private interface structure to extract.
 *
 * \note
 * The given interface structure can be either in the interface list or a stand alone
 * structure that has not been put in the list if the next and prev pointers are NULL.
 *
 * \return Nothing
 */
static void extra_iflist_extract(struct extra_pvt *pvt)
{
	/* Extract from the forward chain. */
	if (pvt->prev) {
		pvt->prev->next = pvt->next;
	} else if (iflist == pvt) {
		/* Node is at the head of the list. */
		iflist = pvt->next;
	}

	/* Extract from the reverse chain. */
	if (pvt->next) {
		pvt->next->prev = pvt->prev;
	} else if (ifend == pvt) {
		/* Node is at the end of the list. */
		ifend = pvt->prev;
	}

	/* Node is no longer in the list. */
	pvt->which_iflist = EXTRA_IFLIST_NONE;
	pvt->prev = NULL;
	pvt->next = NULL;
}

static struct extra_pvt *find_next_iface_in_span(struct extra_pvt *cur)
{
	if (cur->next && cur->next->span == cur->span) {
		return cur->next;
	} else if (cur->prev && cur->prev->span == cur->span) {
		return cur->prev;
	}

	return NULL;
}

static void destroy_extra_pvt(struct extra_pvt *pvt)
{
	struct extra_pvt *p = pvt;

	if (p->manages_span_alarms) {
		struct extra_pvt *next = find_next_iface_in_span(p);
		if (next) {
			next->manages_span_alarms = 1;
		}
	}

	/* Remove channel from the list */
	switch (pvt->which_iflist) {
	case EXTRA_IFLIST_NONE:
		break;
	case EXTRA_IFLIST_MAIN:
		extra_iflist_extract(p);
		break;
	}

	if (p->vars) {
		ast_variables_destroy(p->vars);
	}
#if (ASTERISK_VERSION_NUM >= 10800)
	if (p->cc_params) {
		ast_cc_config_params_destroy(p->cc_params);
	}
#endif //(ASTERISK_VERSION_NUM >= 10800)
	ast_mutex_destroy(&p->lock);
	extra_close_sub(p, SUB_REAL);

#if (ASTERISK_VERSION_NUM >= 110000)
	if (p->owner)
		ast_channel_tech_pvt_set(p->owner, NULL);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	if (p->owner)
		p->owner->tech_pvt = NULL;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	ast_free(p);
}

static void destroy_channel(struct extra_pvt *cur, int now)
{
	int i;

	if (!now) {
		/* Do not destroy the channel now if it is owned by someone. */
		if (cur->owner) {
			return;
		}
		for (i = 0; i < SUB_SUM; i++) {
			if (cur->subs[i].owner) {
				return;
			}
		}
	}
	destroy_extra_pvt(cur);
}

static void destroy_all_channels(void)
{
	int chan;
	
	struct extra_pvt *p;

	while (num_restart_pending) {
		usleep(1);
	}

	ast_mutex_lock(&iflock);
	/* Destroy all the interfaces and free their memory */
	while (iflist) {
		p = iflist;

		chan = p->channel;

		/* Free associated memory */
		destroy_extra_pvt(p);
		ast_verb(3, "Unregistered channel %d\n", chan);
	}
	ifcount = 0;
	ast_mutex_unlock(&iflock);
}

#ifdef HAVE_GSMAT
static int gsm_is_up(struct extra_gsm *gsm)
{
	if (gsm->dchanavail == DCHAN_AVAILABLE)
		return 1;
	return 0;
}
#endif

static int extra_hangup(struct ast_channel *ast)
{
	int res = 0;
	int idx,x;
	int law;
	/*static int restore_gains(struct extra_pvt *p);*/

#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(ast);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)
	
#if (ASTERISK_VERSION_NUM >= 110000)
	ast_debug(1, "extra_hangup(%s)\n", ast_channel_name(ast));
#else  //(ASTERISK_VERSION_NUM >= 110000)
	ast_debug(1, "extra_hangup(%s)\n", ast->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	if (!p) {
		ast_log(LOG_WARNING, "Asked to hangup channel not connected\n");
		return 0;
	}

	ast_mutex_lock(&p->lock);

	idx = extra_get_index(ast, p, 1);

	if(p->sig == SIG_GSM_EXTRA){
		x=1;
		ast_channel_setoption(ast, AST_OPTION_AUDIO_MODE, &x, sizeof(char), 0);
		p->cid_num[0] = '\0';
		p->cid_name[0] = '\0';
	}
	x=0;
	
	extra_confmute(p, 0);
#if (ASTERISK_VERSION_NUM >= 10601)
	p->muting = 0;
#endif //(ASTERISK_VERSION_NUM >= 10601)
	restore_gains(p);

	if (p->dsp)
#if (ASTERISK_VERSION_NUM >= 10601)
		ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);
#else  //(ASTERISK_VERSION_NUM >= 10601)
		ast_dsp_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);
#endif //(ASTERISK_VERSION_NUM >= 10601)

	p->exten[0] = '\0';
	
	ast_debug(1, "Hangup: channel: %d index = %d, normal = %d, callwait = %d, thirdcall = %d\n",
		p->channel, idx, p->subs[SUB_REAL].dfd, p->subs[SUB_CALLWAIT].dfd, p->subs[SUB_THREEWAY].dfd);
	p->ignoredtmf = 0;

	if (idx > -1) {
		/* Real channel, do some fixup */
		p->subs[idx].owner = NULL;
		p->subs[idx].needanswer = 0;
		p->subs[idx].needbusy = 0;
		p->subs[idx].needcongestion = 0;
		p->subs[idx].linear = 0;
		extra_setlinear(p->subs[idx].dfd, 0);
		if (idx == SUB_REAL) {
			if ((p->subs[SUB_CALLWAIT].dfd > -1) && (p->subs[SUB_THREEWAY].dfd > -1)) {
				ast_debug(1, "Normal call hung up with both three way call and a call waiting call in place?\n");
				if (p->subs[SUB_CALLWAIT].inthreeway) {
					/* We had flipped over to answer a callwait and now it's gone */
					ast_debug(1, "We were flipped over to the callwait, moving back and unowning.\n");
					/* Move to the call-wait, but un-own us until they flip back. */
					swap_subs(p, SUB_CALLWAIT, SUB_REAL);
					unalloc_sub(p, SUB_CALLWAIT);
					p->owner = NULL;
				} else {
					/* The three way hung up, but we still have a call wait */
					ast_debug(1, "We were in the threeway and have a callwait still.  Ditching the threeway.\n");
					swap_subs(p, SUB_THREEWAY, SUB_REAL);
					unalloc_sub(p, SUB_THREEWAY);
					if (p->subs[SUB_REAL].inthreeway) {
						/* This was part of a three way call.  Immediately make way for
						   another call */
						ast_debug(1, "Call was complete, setting owner to former third call\n");
						p->owner = p->subs[SUB_REAL].owner;
					} else {
						/* This call hasn't been completed yet...  Set owner to NULL */
						ast_debug(1, "Call was incomplete, setting owner to NULL\n");
						p->owner = NULL;
					}
					p->subs[SUB_REAL].inthreeway = 0;
				}
			} else if (p->subs[SUB_CALLWAIT].dfd > -1) {
				/* Move to the call-wait and switch back to them. */
				swap_subs(p, SUB_CALLWAIT, SUB_REAL);
				unalloc_sub(p, SUB_CALLWAIT);
				p->owner = p->subs[SUB_REAL].owner;

#if (ASTERISK_VERSION_NUM >= 110000)
				if (ast_channel_state(p->owner) != AST_STATE_UP)
					p->subs[SUB_REAL].needanswer = 1;
#else  //(ASTERISK_VERSION_NUM >= 110000)
				if (p->owner->_state != AST_STATE_UP)
					p->subs[SUB_REAL].needanswer = 1;
#endif //(ASTERISK_VERSION_NUM >= 110000)

				if (ast_bridged_channel(p->subs[SUB_REAL].owner))
					ast_queue_control(p->subs[SUB_REAL].owner, AST_CONTROL_UNHOLD);
			} else if (p->subs[SUB_THREEWAY].dfd > -1) {
				swap_subs(p, SUB_THREEWAY, SUB_REAL);
				unalloc_sub(p, SUB_THREEWAY);
				if (p->subs[SUB_REAL].inthreeway) {
					/* This was part of a three way call.  Immediately make way for
					   another call */
					ast_debug(1, "Call was complete, setting owner to former third call\n");
					p->owner = p->subs[SUB_REAL].owner;
				} else {
					/* This call hasn't been completed yet...  Set owner to NULL */
					ast_debug(1, "Call was incomplete, setting owner to NULL\n");
					p->owner = NULL;
				}
				p->subs[SUB_REAL].inthreeway = 0;
			}
		} else if (idx == SUB_CALLWAIT) {
			/* Ditch the holding callwait call, and immediately make it availabe */
			if (p->subs[SUB_CALLWAIT].inthreeway) {
				/* This is actually part of a three way, placed on hold.  Place the third part
				   on music on hold now */
				if (p->subs[SUB_THREEWAY].owner && ast_bridged_channel(p->subs[SUB_THREEWAY].owner)) {
				}
				p->subs[SUB_THREEWAY].inthreeway = 0;
				/* Make it the call wait now */
				swap_subs(p, SUB_CALLWAIT, SUB_THREEWAY);
				unalloc_sub(p, SUB_THREEWAY);
			} else {
				unalloc_sub(p, SUB_CALLWAIT);
			}
		} else if (idx == SUB_THREEWAY) {
			if (p->subs[SUB_CALLWAIT].inthreeway) {
				/* The other party of the three way call is currently in a call-wait state.
				   Start music on hold for them, and take the main guy out of the third call */
				if (p->subs[SUB_CALLWAIT].owner && ast_bridged_channel(p->subs[SUB_CALLWAIT].owner)) {
				}
				p->subs[SUB_CALLWAIT].inthreeway = 0;
			}
			p->subs[SUB_REAL].inthreeway = 0;
			/* If this was part of a three way call index, let us make
			   another three way call */
			unalloc_sub(p, SUB_THREEWAY);
		} else if (idx == SUB_SMS) {
		} else if (idx == SUB_SMSSEND) {
		} else {
			/* This wasn't any sort of call, but how are we an index? */
			ast_log(LOG_WARNING, "Index found but not any type of call?\n");
		}
	}

	if (!p->subs[SUB_REAL].owner && !p->subs[SUB_CALLWAIT].owner && !p->subs[SUB_THREEWAY].owner && !p->subs[SUB_SMS].owner && !p->subs[SUB_SMSSEND].owner) {
		p->owner = NULL;
		p->distinctivering = 0;
		p->confirmanswer = 0;
		p->outgoing = 0;
		p->digital = 0;
		p->faxhandled = 0;
		p->pulsedial = 0;
#ifdef HAVE_GSMAT
		p->proceeding = 0;
		p->dialing = 0;
		p->progress = 0;
		p->alerting = 0;
		p->setup_ack = 0;
		p->rlt = 0;
#endif		
		if (p->dsp) {
			ast_dsp_free(p->dsp);
			p->dsp = NULL;
		}

		law = 0 ;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SETLAW, &law);
		if (res < 0)
			ast_log(LOG_WARNING, "Unable to set law on channel %d to default: %s\n", p->channel, strerror(errno));
		/* Perform low level hangup if no owner left */
#ifdef HAVE_GSMAT
		if (p->gsm) {
			/* Make sure we have a call (or REALLY have a call in the case of a GSM) */
			if (p->gsmcall) {
				if (!gsm_grab(p, p->gsm)) {
					if (p->alreadyhungup) {
						ast_debug(1, "Already hungup...  Calling hangup once, and clearing call\n");
						gsm_hangup(p->gsm->gsm, p->gsmcall, -1);
						p->gsmcall = NULL;
					} else {
						const char *cause = pbx_builtin_getvar_helper(ast,"GSM_CAUSE");

#if (ASTERISK_VERSION_NUM >= 110000)
						int icause = ast_channel_hangupcause(ast) ? ast_channel_hangupcause(ast) : -1;
#else  //(ASTERISK_VERSION_NUM >= 110000)
						int icause = ast->hangupcause ? ast->hangupcause : -1;
#endif //(ASTERISK_VERSION_NUM >= 110000)
						ast_debug(1, "Not yet hungup...  Calling hangup once with icause, and clearing call\n");
						p->alreadyhungup = 1;
						if (cause) {
							if (atoi(cause))
								icause = atoi(cause);
						}
						gsm_hangup(p->gsm->gsm, p->gsmcall, icause);
					}
					if (res < 0) 
						ast_log(LOG_WARNING, "gsm_disconnect failed\n");
					gsm_rel(p->gsm);			
				} else {
					ast_log(LOG_WARNING, "Unable to grab GSM on span %d\n", p->span);
					res = -1;
				}
			}
		}
#endif
		tone_zone_play_tone(p->subs[SUB_REAL].dfd, -1);
	
		if (p->sig)
			extra_disable_ec(p);

		x = 0;
		ast_channel_setoption(ast,AST_OPTION_TONE_VERIFY,&x,sizeof(char),0);
		ast_channel_setoption(ast,AST_OPTION_TDD,&x,sizeof(char),0);
		p->didtdd = 0;
		p->dialing = 0;
		p->rdnis[0] = '\0';
//Freedom del 2012-02-02 13:49
#if 0
		update_conf(p);
		reset_conf(p);
#endif
		/* Restore data mode */
		if ( p->sig == SIG_GSM_EXTRA ) {
			x = 0;
			ast_channel_setoption(ast,AST_OPTION_AUDIO_MODE,&x,sizeof(char),0);
		}
	}

	p->oprmode = 0;

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_channel_tech_pvt_set(ast, NULL);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	ast->tech_pvt = NULL;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	ast_mutex_unlock(&p->lock);
	ast_module_unref(ast_module_info->self);

	if ((idx == SUB_SMS) || (idx == SUB_SMSSEND)) {
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_verb(3, "Finish '%s'\n", ast_channel_name(ast));
#else //(ASTERISK_VERSION_NUM >= 110000)
		ast_verb(3, "Finish '%s'\n", ast->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
	} else {
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_verb(3, "Hungup '%s'\n", ast_channel_name(ast));
#else //(ASTERISK_VERSION_NUM >= 110000)
		ast_verb(3, "Hungup '%s'\n", ast->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
	}

	ast_mutex_lock(&iflock);

	if (p->restartpending) {
		num_restart_pending--;
	}

	if (p->destroy) {
		destroy_channel(p, 0);
	}

	ast_mutex_unlock(&iflock);

	return 0;
}

static int extra_answer(struct ast_channel *ast)
{
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(ast);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)
	int res = 0;
	int idx;
	ast_setstate(ast, AST_STATE_UP);/*! \todo XXX this is redundantly set by the analog and PRI submodules! */
	ast_mutex_lock(&p->lock);
	idx = extra_get_index(ast, p, 0);
	if (idx < 0)
		idx = SUB_REAL;
	/* nothing to do if a radio channel */
	if ((p->radio || (p->oprmode < 0))) {
		ast_mutex_unlock(&p->lock);
		return 0;
	}

	switch (p->sig) {
#ifdef HAVE_GSMAT
	case SIG_GSM_EXTRA:
		/* Send a gsm acknowledge */
		if (!gsm_grab(p, p->gsm)) {
			p->proceeding = 1;
			p->dialing = 0;
			/* Answer the remote call */
			res = gsm_answer(p->gsm->gsm, p->gsmcall, 0);
			gsm_rel(p->gsm);
		} else {
			ast_log(LOG_WARNING, "Unable to grab GSM on span %d\n", p->span);
			res = -1;
		}
		break;
#endif
	case 0:
		ast_mutex_unlock(&p->lock);
		return 0;
	default:
		ast_log(LOG_WARNING, "Don't know how to answer signalling %d (channel %d)\n", p->sig, p->channel);
		res = -1;
		break;
	}
	ast_mutex_unlock(&p->lock);
	return res;
}

#if (ASTERISK_VERSION_NUM >= 10800)
static void disable_dtmf_detect(struct extra_pvt *p)
{
	int val = 0;

	p->ignoredtmf = 1;

	ioctl(p->subs[SUB_REAL].dfd, DAHDI_TONEDETECT, &val);

	if (!p->hardwaredtmf && p->dsp) {
		p->dsp_features &= ~DSP_FEATURE_DIGIT_DETECT;
		ast_dsp_set_features(p->dsp, p->dsp_features);
	}
}

static void enable_dtmf_detect(struct extra_pvt *p)
{
	int val = DAHDI_TONEDETECT_ON | DAHDI_TONEDETECT_MUTE;

	if (p->channel == CHAN_PSEUDO)
		return;

	p->ignoredtmf = 0;

	ioctl(p->subs[SUB_REAL].dfd, DAHDI_TONEDETECT, &val);

	if (!p->hardwaredtmf && p->dsp) {
		p->dsp_features |= DSP_FEATURE_DIGIT_DETECT;
		ast_dsp_set_features(p->dsp, p->dsp_features);
	}
}
#endif //(ASTERISK_VERSION_NUM >= 10800)

#if (ASTERISK_VERSION_NUM >= 10800)
static int extra_queryoption(struct ast_channel *chan, int option, void *data, int *datalen)
{
	char *cp;
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(chan);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = chan->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	/* all supported options require data */
	if (!data || (*datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch (option) {
	case AST_OPTION_DIGIT_DETECT:
		cp = (char *) data;
		*cp = p->ignoredtmf ? 0 : 1;
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Reporting digit detection %sabled on %s\n", *cp ? "en" : "dis", ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Reporting digit detection %sabled on %s\n", *cp ? "en" : "dis", chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
		break;
	case AST_OPTION_FAX_DETECT:
		cp = (char *) data;
		*cp = (p->dsp_features & DSP_FEATURE_FAX_DETECT) ? 0 : 1;
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Reporting fax tone detection %sabled on %s\n", *cp ? "en" : "dis", ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Reporting fax tone detection %sabled on %s\n", *cp ? "en" : "dis", chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
		break;
	case AST_OPTION_CC_AGENT_TYPE:
		return -1;
	default:
		return -1;
	}

	errno = 0;

	return 0;
}
#endif //(ASTERISK_VERSION_NUM >= 10800)

static int extra_setoption(struct ast_channel *chan, int option, void *data, int datalen)
{
	char *cp;
	signed char *scp;
	int x;
	int idx;
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(chan), *pp;
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = chan->tech_pvt, *pp;
#endif //(ASTERISK_VERSION_NUM >= 110000)
	struct oprmode *oprmode;

	/* all supported options require data */
	if (!data || (datalen < 1)) {
		errno = EINVAL;
		return -1;
	}

	switch (option) {
	case AST_OPTION_TXGAIN:
		scp = (signed char *) data;
		idx = extra_get_index(chan, p, 0);
		if (idx < 0) {
			ast_log(LOG_WARNING, "No index in TXGAIN?\n");
			return -1;
		}

#if (ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Setting actual tx gain on %s to %f\n", ast_channel_name(chan), p->txgain + (float) *scp);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Setting actual tx gain on %s to %f\n", chan->name, p->txgain + (float) *scp);
#endif //(ASTERISK_VERSION_NUM >= 110000)

		return set_actual_txgain(p->subs[idx].dfd, p->txgain + (float) *scp, p->txdrc, p->law);
	case AST_OPTION_RXGAIN:
		scp = (signed char *) data;
		idx = extra_get_index(chan, p, 0);
		if (idx < 0) {
			ast_log(LOG_WARNING, "No index in RXGAIN?\n");
			return -1;
		}

#if (ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Setting actual rx gain on %s to %f\n", ast_channel_name(chan), p->rxgain + (float) *scp);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Setting actual rx gain on %s to %f\n", chan->name, p->rxgain + (float) *scp);
#endif //(ASTERISK_VERSION_NUM >= 110000)

		return set_actual_rxgain(p->subs[idx].dfd, p->rxgain + (float) *scp, p->rxdrc, p->law);
	case AST_OPTION_TONE_VERIFY:
		if (!p->dsp)
			break;
		cp = (char *) data;
		switch (*cp) {
		case 1:
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Set option TONE VERIFY, mode: MUTECONF(1) on %s\n",ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Set option TONE VERIFY, mode: MUTECONF(1) on %s\n",chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

#if (ASTERISK_VERSION_NUM >= 10601)
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_MUTECONF | p->dtmfrelax);  /* set mute mode if desired */
#else  //(ASTERISK_VERSION_NUM >= 10601)
			ast_dsp_digitmode(p->dsp, DSP_DIGITMODE_MUTECONF | p->dtmfrelax);  /* set mute mode if desired */
#endif //(ASTERISK_VERSION_NUM >= 10601)
			break;
		case 2:
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n",ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Set option TONE VERIFY, mode: MUTECONF/MAX(2) on %s\n",chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
#if (ASTERISK_VERSION_NUM >= 10601)
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX | p->dtmfrelax);  /* set mute mode if desired */
#else  //(ASTERISK_VERSION_NUM >= 10601)
			ast_dsp_digitmode(p->dsp, DSP_DIGITMODE_MUTECONF | DSP_DIGITMODE_MUTEMAX | p->dtmfrelax);  /* set mute mode if desired */
#endif //(ASTERISK_VERSION_NUM >= 10601)
			break;
		default:
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Set option TONE VERIFY, mode: OFF(0) on %s\n",ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Set option TONE VERIFY, mode: OFF(0) on %s\n",chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

#if (ASTERISK_VERSION_NUM >= 10601)
			ast_dsp_set_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);  /* set mute mode if desired */
#else  //(ASTERISK_VERSION_NUM >= 10601)
			ast_dsp_digitmode(p->dsp, DSP_DIGITMODE_DTMF | p->dtmfrelax);  /* set mute mode if desired */
#endif //(ASTERISK_VERSION_NUM >= 10601)
			break;
		}
		break;
	case AST_OPTION_TDD:
		/* turn on or off TDD */
		cp = (char *) data;
		p->mate = 0;
		if (!*cp) { /* turn it off */
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Set option TDD MODE, value: OFF(0) on %s\n",ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Set option TDD MODE, value: OFF(0) on %s\n",chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
			if (p->tdd)
				tdd_free(p->tdd);
			p->tdd = 0;
			break;
		}

#if (ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Set option TDD MODE, value: %s(%d) on %s\n",
			(*cp == 2) ? "MATE" : "ON", (int) *cp, ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Set option TDD MODE, value: %s(%d) on %s\n",
			(*cp == 2) ? "MATE" : "ON", (int) *cp, chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

		extra_disable_ec(p);
		/* otherwise, turn it on */
		if (!p->didtdd) { /* if havent done it yet */
			unsigned char mybuf[41000];/*! \todo XXX This is an abuse of the stack!! */
			unsigned char *buf;
			int size, res, fd, len;
			struct pollfd fds[1];

			buf = mybuf;
			memset(buf, 0x7f, sizeof(mybuf)); /* set to silence */
			ast_tdd_gen_ecdisa(buf + 16000, 16000);  /* put in tone */
			len = 40000;
			idx = extra_get_index(chan, p, 0);
			if (idx < 0) {
				ast_log(LOG_WARNING, "No index in TDD?\n");
				return -1;
			}
			fd = p->subs[idx].dfd;
			while (len) {
				if (ast_check_hangup(chan))
					return -1;
				size = len;
				if (size > READ_SIZE)
					size = READ_SIZE;
				fds[0].fd = fd;
				fds[0].events = POLLPRI | POLLOUT;
				fds[0].revents = 0;
				res = poll(fds, 1, -1);
				if (!res) {
					ast_debug(1, "poll (for write) ret. 0 on channel %d\n", p->channel);
					continue;
				}
				/* if got exception */
				if (fds[0].revents & POLLPRI)
					return -1;
				if (!(fds[0].revents & POLLOUT)) {
					ast_debug(1, "write fd not ready on channel %d\n", p->channel);
					continue;
				}
				res = write(fd, buf, size);
				if (res != size) {
					if (res == -1) return -1;
					ast_debug(1, "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
					break;
				}
				len -= size;
				buf += size;
			}
			p->didtdd = 1; /* set to have done it now */
		}
		if (*cp == 2) { /* Mate mode */
			if (p->tdd)
				tdd_free(p->tdd);
			p->tdd = 0;
			p->mate = 1;
			break;
		}
		if (!p->tdd) { /* if we don't have one yet */
			p->tdd = tdd_new(); /* allocate one */
		}
		break;
	case AST_OPTION_RELAXDTMF:  /* Relax DTMF decoding (or not) */
		if (!p->dsp)
			break;
		cp = (char *) data;

#if (ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Set option RELAX DTMF, value: %s(%d) on %s\n",
			*cp ? "ON" : "OFF", (int) *cp, ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Set option RELAX DTMF, value: %s(%d) on %s\n",
			*cp ? "ON" : "OFF", (int) *cp, chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

#if (ASTERISK_VERSION_NUM >= 10601)
		ast_dsp_set_digitmode(p->dsp, ((*cp) ? DSP_DIGITMODE_RELAXDTMF : DSP_DIGITMODE_DTMF) | p->dtmfrelax);
#else  //(ASTERISK_VERSION_NUM >= 10601)
		ast_dsp_digitmode(p->dsp, ((*cp) ? DSP_DIGITMODE_RELAXDTMF : DSP_DIGITMODE_DTMF) | p->dtmfrelax);
#endif //(ASTERISK_VERSION_NUM >= 10601)
		break;
	case AST_OPTION_AUDIO_MODE:  /* Set AUDIO mode (or not) */
		cp = (char *) data;
		if (!*cp) {
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Set option AUDIO MODE, value: OFF(0) on %s\n", ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Set option AUDIO MODE, value: OFF(0) on %s\n", chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
			x = 0;
			extra_disable_ec(p);
		} else {
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Set option AUDIO MODE, value: ON(1) on %s\n", ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Set option AUDIO MODE, value: ON(1) on %s\n", chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
			x = 1;
		}
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &x) == -1)
			ast_log(LOG_WARNING, "Unable to set audio mode on channel %d to %d: %s\n", p->channel, x, strerror(errno));
		break;
	case AST_OPTION_OPRMODE:  /* Operator services mode */
		oprmode = (struct oprmode *) data;
		/* We don't support operator mode across technologies */
#if (ASTERISK_VERSION_NUM >= 110000)
		if (strcasecmp(ast_channel_tech(chan)->type, ast_channel_tech(oprmode->peer)->type)) {
			ast_log(LOG_NOTICE, "Operator mode not supported on %s to %s calls.\n",
					ast_channel_tech(chan)->type, ast_channel_tech(oprmode->peer)->type);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		if (strcasecmp(chan->tech->type, oprmode->peer->tech->type)) {
			ast_log(LOG_NOTICE, "Operator mode not supported on %s to %s calls.\n",
					chan->tech->type, oprmode->peer->tech->type);
#endif //(ASTERISK_VERSION_NUM >= 110000)
			errno = EINVAL;
			return -1;
		}
#if (ASTERISK_VERSION_NUM >= 110000)
		pp = ast_channel_tech_pvt(oprmode->peer);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		pp = oprmode->peer->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)

		p->oprmode = pp->oprmode = 0;
		/* setup peers */
		p->oprpeer = pp;
		pp->oprpeer = p;
		/* setup modes, if any */
		if (oprmode->mode)
		{
			pp->oprmode = oprmode->mode;
			p->oprmode = -oprmode->mode;
		}

#if (ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Set Operator Services mode, value: %d on %s/%s\n",
			oprmode->mode, ast_channel_name(chan),ast_channel_name(oprmode->peer));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Set Operator Services mode, value: %d on %s/%s\n",
			oprmode->mode, chan->name,oprmode->peer->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

		break;
	case AST_OPTION_ECHOCAN:
		cp = (char *) data;
		if (*cp) {
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Enabling echo cancellation on %s\n", ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Enabling echo cancellation on %s\n", chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
			extra_enable_ec(p);
		} else {
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Disabling echo cancellation on %s\n", ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Disabling echo cancellation on %s\n", chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
			extra_disable_ec(p);
		}
		break;
#if (ASTERISK_VERSION_NUM >= 10800)
	case AST_OPTION_DIGIT_DETECT:
		cp = (char *) data;
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "%sabling digit detection on %s\n", *cp ? "En" : "Dis", ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "%sabling digit detection on %s\n", *cp ? "En" : "Dis", chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

		if (*cp) {
			enable_dtmf_detect(p);
		} else {
			disable_dtmf_detect(p);
		}
		break;
	case AST_OPTION_FAX_DETECT:
		cp = (char *) data;
		if (p->dsp) {
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "%sabling fax tone detection on %s\n", *cp ? "En" : "Dis", ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "%sabling fax tone detection on %s\n", *cp ? "En" : "Dis", chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

			if (*cp) {
				p->dsp_features |= DSP_FEATURE_FAX_DETECT;
			} else {
				p->dsp_features &= ~DSP_FEATURE_FAX_DETECT;
			}
			ast_dsp_set_features(p->dsp, p->dsp_features);
		}
		break;
#endif //(ASTERISK_VERSION_NUM >= 10800)
	default:
		return -1;
	}
	errno = 0;

	return 0;
}

static int parse_buffers_policy(const char *parse, int *num_buffers, int *policy)
{
	int res;
	char policy_str[21] = "";

	if ((res = sscanf(parse, "%30d,%20s", num_buffers, policy_str)) != 2) {
		ast_log(LOG_WARNING, "Parsing buffer string '%s' failed.\n", parse);
		return 1;
	}
	if (*num_buffers < 0) {
		ast_log(LOG_WARNING, "Invalid buffer count given '%d'.\n", *num_buffers);
		return -1;
	}
	if (!strcasecmp(policy_str, "full")) {
		*policy = DAHDI_POLICY_WHEN_FULL;
	} else if (!strcasecmp(policy_str, "immediate")) {
		*policy = DAHDI_POLICY_IMMEDIATE;
#if defined(HAVE_DAHDI_HALF_FULL)
	} else if (!strcasecmp(policy_str, "half")) {
		*policy = DAHDI_POLICY_HALF_FULL;
#endif
	} else {
		ast_log(LOG_WARNING, "Invalid policy name given '%s'.\n", policy_str);
		return -1;
	}

	return 0;
}

static void extra_unlink(struct extra_pvt *slave, struct extra_pvt *master, int needlock)
{
	/* Unlink a specific slave or all slaves/masters from a given master */
	int x;
	int hasslaves;
	if (!master)
		return;
	if (needlock) {
		ast_mutex_lock(&master->lock);
		if (slave) {
			while (ast_mutex_trylock(&slave->lock)) {
				DEADLOCK_AVOIDANCE(&master->lock);
			}
		}
	}
	hasslaves = 0;
	for (x = 0; x < MAX_SLAVES; x++) {
		if (master->slaves[x]) {
			if (!slave || (master->slaves[x] == slave)) {
				/* Take slave out of the conference */
				ast_debug(1, "Unlinking slave %d from %d\n", master->slaves[x]->channel, master->channel);
//Freedom del 2012-02-02 13:49
#if 0
				conf_del(master, &master->slaves[x]->subs[SUB_REAL], SUB_REAL);
				conf_del(master->slaves[x], &master->subs[SUB_REAL], SUB_REAL);
#endif
				master->slaves[x]->master = NULL;
				master->slaves[x] = NULL;
			} else
				hasslaves = 1;
		}
		if (!hasslaves)
			master->inconference = 0;
	}
	if (!slave) {
		if (master->master) {
			/* Take master out of the conference */
//Freedom del 2012-02-02 13:49
#if 0
			conf_del(master->master, &master->subs[SUB_REAL], SUB_REAL);
			conf_del(master, &master->master->subs[SUB_REAL], SUB_REAL);
#endif
			hasslaves = 0;
			for (x = 0; x < MAX_SLAVES; x++) {
				if (master->master->slaves[x] == master)
					master->master->slaves[x] = NULL;
				else if (master->master->slaves[x])
					hasslaves = 1;
			}
			if (!hasslaves)
				master->master->inconference = 0;
		}
		master->master = NULL;
	}
//Freedom del 2012-02-02 13:49
#if 0
	update_conf(master);
#endif
	if (needlock) {
		if (slave)
			ast_mutex_unlock(&slave->lock);
		ast_mutex_unlock(&master->lock);
	}
}

static int extra_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(newchan);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = newchan->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)
	int x;

	ast_mutex_lock(&p->lock);

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_debug(1, "New owner for channel %d is %s\n", p->channel, ast_channel_name(newchan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
	ast_debug(1, "New owner for channel %d is %s\n", p->channel, newchan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	if (p->owner == oldchan) {
		p->owner = newchan;
	}
	for (x = 0; x < 3; x++) {
		if (p->subs[x].owner == oldchan) {
			if (!x) {
				extra_unlink(NULL, p, 0);
			}
			p->subs[x].owner = newchan;
		}
	}

//Freedom del 2012-02-02 13:49
#if 0
	update_conf(p);
#endif

	ast_mutex_unlock(&p->lock);

#if (ASTERISK_VERSION_NUM >= 110000)
	if (ast_channel_state(newchan) == AST_STATE_RINGING) {
#else  //(ASTERISK_VERSION_NUM >= 110000)
	if (newchan->_state == AST_STATE_RINGING) {
#endif //(ASTERISK_VERSION_NUM >= 110000)
		extra_indicate(newchan, AST_CONTROL_RINGING, NULL, 0);
	}
	return 0;
}

static void *analog_ss_thread(void *data);

/*! Checks channel for alarms
 * \param p a channel to check for alarms.
 * \returns the alarms on the span to which the channel belongs, or alarms on
 *          the channel if no span alarms.
 */
static int get_alarms(struct extra_pvt *p)
{
	int res;
	struct dahdi_spaninfo zi;
	struct dahdi_params params;

	memset(&zi, 0, sizeof(zi));
	zi.spanno = p->span;

	if ((res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SPANSTAT, &zi)) >= 0) {
		if (zi.alarms != DAHDI_ALARM_NONE)
			return zi.alarms;
	} else {
		ast_log(LOG_WARNING, "Unable to determine alarm on channel %d: %s\n", p->channel, strerror(errno));
		return 0;
	}

	/* No alarms on the span. Check for channel alarms. */
	memset(&params, 0, sizeof(params));
	if ((res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &params)) >= 0)
		return params.chan_alarms;

	ast_log(LOG_WARNING, "Unable to determine alarm on channel %d\n", p->channel);

	return DAHDI_ALARM_NONE;
}

static void extra_handle_dtmfup(struct ast_channel *ast, int idx, struct ast_frame **dest)
{
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(ast);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	struct ast_frame *f = *dest;

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_debug(1, "DTMF digit: %c on %s\n", (int) f->subclass.integer, ast_channel_name(ast));
#elif (ASTERISK_VERSION_NUM >= 10800) //(ASTERISK_VERSION_NUM >= 110000)
	ast_debug(1, "DTMF digit: %c on %s\n", (int) f->subclass.integer, ast->name);
#else  //(ASTERISK_VERSION_NUM >= 10800)
	ast_debug(1, "DTMF digit: %c on %s\n", f->subclass, ast->name);
#endif //(ASTERISK_VERSION_NUM >= 10800)

	if (p->confirmanswer) {
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Confirm answer on %s!\n", ast_channel_name(ast));
#else  //(ASTERISK_VERSION_NUM >= 110000)
	   ast_debug(1, "Confirm answer on %s!\n", ast->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

		/* Upon receiving a DTMF digit, consider this an answer confirmation instead
		   of a DTMF digit */
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
#if (ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
#else //(ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass = AST_CONTROL_ANSWER;
#endif //(ASTERISK_VERSION_NUM >= 10800)
		*dest = &p->subs[idx].f;
		/* Reset confirmanswer so DTMF's will behave properly for the duration of the call */
		p->confirmanswer = 0;
#if (ASTERISK_VERSION_NUM >= 10800)
	} else if (f->subclass.integer == 'f') {
#else //(ASTERISK_VERSION_NUM >= 10800)
	} else if (f->subclass == 'f') {
#endif //(ASTERISK_VERSION_NUM >= 10800)
		extra_confmute(p, 0);
		p->subs[idx].f.frametype = AST_FRAME_NULL;
#if (ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass.integer = 0;
#else //(ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass = 0;
#endif //(ASTERISK_VERSION_NUM >= 10800)
		*dest = &p->subs[idx].f;
	}
}

static void handle_alarms(struct extra_pvt *p, int alms)
{
	const char *alarm_str = alarm2str(alms);

	if (report_alarms & REPORT_CHANNEL_ALARMS) {
		ast_log(LOG_WARNING, "Detected alarm on channel %d: %s\n", p->channel, alarm_str);
		manager_event(EVENT_FLAG_SYSTEM, "Alarm",
					  "Alarm: %s\r\n"
					  "Channel: %d\r\n",
					  alarm_str, p->channel);
	}

	if (report_alarms & REPORT_SPAN_ALARMS && p->manages_span_alarms) {
		ast_log(LOG_WARNING, "Detected alarm on span %d: %s\n", p->span, alarm_str);
		manager_event(EVENT_FLAG_SYSTEM, "SpanAlarm",
					  "Alarm: %s\r\n"
					  "Span: %d\r\n",
					  alarm_str, p->span);
	}
}

static int extra_ring_phone(struct extra_pvt *p)
{
	int x;
	int res;
	/* Make sure our transmit state is on hook */
	x = 0;
	x = DAHDI_ONHOOK;
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
	do {
		x = DAHDI_RING;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
		if (res) {
			switch (errno) {
			case EBUSY:
			case EINTR:
				/* Wait just in case */
				usleep(10000);
				continue;
			case EINPROGRESS:
				res = 0;
				break;
			default:
				ast_log(LOG_WARNING, "Couldn't ring the phone: %s\n", strerror(errno));
				res = 0;
			}
		}
	} while (res);
	
	return res;
}

static struct ast_frame *extra_handle_event(struct ast_channel *ast)
{
	int res, x;
	int idx, mysig;
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(ast);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	struct ast_frame *f;
	
	idx = extra_get_index(ast, p, 0);

//Freedom 2012-06-07 11:33. if idex is -1 don't use p->subs[idex]
	if (idx < 0) return NULL;

	mysig = p->sig;
	if (p->outsigmod > -1)
		mysig = p->outsigmod;

	p->subs[idx].f.frametype = AST_FRAME_NULL;
#if (ASTERISK_VERSION_NUM >= 10800)
	p->subs[idx].f.subclass.integer = 0;
#else  //(ASTERISK_VERSION_NUM >= 10800)
	p->subs[idx].f.subclass = 0;
#endif //(ASTERISK_VERSION_NUM >= 10800)
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
	p->subs[idx].f.src = "extra_handle_event";
#if (ASTERISK_VERSION_NUM >= 10601)
	p->subs[idx].f.data.ptr = NULL;
#else  //(ASTERISK_VERSION_NUM >= 10601)
	p->subs[idx].f.data = NULL;
#endif //(ASTERISK_VERSION_NUM >= 10601)
	f = &p->subs[idx].f;

	if (idx < 0)
		return &p->subs[idx].f;

	res = extra_get_event(p->subs[idx].dfd);

	ast_debug(1, "Got event %s(%d) on channel %d (index %d)\n", event2str(res), res, p->channel, idx);

	if (res & (DAHDI_EVENT_PULSEDIGIT | DAHDI_EVENT_DTMFUP)) {
		p->pulsedial = (res & DAHDI_EVENT_PULSEDIGIT) ? 1 : 0;
		ast_debug(1, "Detected %sdigit '%c'\n", p->pulsedial ? "pulse ": "", res & 0xff);
		{
			extra_confmute(p, 0);
			p->subs[idx].f.frametype = AST_FRAME_DTMF_END;
#if (ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.subclass.integer = res & 0xff;
#else  //(ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.subclass = res & 0xff;
#endif //(ASTERISK_VERSION_NUM >= 10800)
		}
		extra_handle_dtmfup(ast, idx, &f);
		return f;
	}

	if (res & DAHDI_EVENT_DTMFDOWN) {
		ast_debug(1, "DTMF Down '%c'\n", res & 0xff);
		/* Mute conference */
		extra_confmute(p, 1);
		p->subs[idx].f.frametype = AST_FRAME_DTMF_BEGIN;
#if (ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass.integer = res & 0xff;
#else  //(ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass = res & 0xff;
#endif //(ASTERISK_VERSION_NUM >= 10800)
		return &p->subs[idx].f;
	}

	switch (res) {
	case DAHDI_EVENT_EC_DISABLED:
		ast_verb(3, "Channel %d echo canceler disabled.\n", p->channel);
		p->echocanon = 0;
		break;
#ifdef HAVE_DAHDI_ECHOCANCEL_FAX_MODE
	case DAHDI_EVENT_TX_CED_DETECTED:
		ast_verb(3, "Channel %d detected a CED tone towards the network.\n", p->channel);
		break;
	case DAHDI_EVENT_RX_CED_DETECTED:
		ast_verb(3, "Channel %d detected a CED tone from the network.\n", p->channel);
		break;
	case DAHDI_EVENT_EC_NLP_DISABLED:
		ast_verb(3, "Channel %d echo canceler disabled its NLP.\n", p->channel);
		break;
	case DAHDI_EVENT_EC_NLP_ENABLED:
		ast_verb(3, "Channel %d echo canceler enabled its NLP.\n", p->channel);
		break;
#endif
	case DAHDI_EVENT_BITSCHANGED:
		ast_log(LOG_WARNING, "Received bits changed on %s signalling?\n", sig2str(p->sig));
	case DAHDI_EVENT_PULSE_START:
		/* Stop tone if there's a pulse start and the PBX isn't started */
#if (ASTERISK_VERSION_NUM >= 110000)
		if (!ast_channel_pbx(ast))
#else  //(ASTERISK_VERSION_NUM >= 110000)
		if (!ast->pbx)
#endif //(ASTERISK_VERSION_NUM >= 110000)
			tone_zone_play_tone(p->subs[idx].dfd, -1);
		break;
	case DAHDI_EVENT_DIALCOMPLETE:
		if (p->inalarm) break;
		if ((p->radio || (p->oprmode < 0))) break;
		if (ioctl(p->subs[idx].dfd,DAHDI_DIALING,&x) == -1) {

#if (ASTERISK_VERSION_NUM >= 110000)
			ast_log(LOG_DEBUG, "DAHDI_DIALING ioctl failed on %s: %s\n",ast_channel_name(ast), strerror(errno));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_log(LOG_DEBUG, "DAHDI_DIALING ioctl failed on %s: %s\n",ast->name, strerror(errno));
#endif //(ASTERISK_VERSION_NUM >= 110000)

			return NULL;
		}
		if (!x) { /* if not still dialing in driver */
			extra_enable_ec(p);
		}
		break;
	case DAHDI_EVENT_ALARM:
		switch (p->sig) {
#ifdef HAVE_GSMAT
		if (p->sig == SIG_GSM_EXTRA) {
			if (!p->gsm || !p->gsm->gsm || (gsm_get_timer(p->gsm->gsm, GSM_TIMER_T309) < 0)) {
				/* T309 is not enabled : hangup calls when alarm occurs */
				if (p->gsmcall) {
					if (p->gsm && p->gsm->gsm) {
						if (!gsm_grab(p, p->gsm)) {
							gsm_hangup(p->gsm->gsm, p->gsmcall, -1);
							gsm_destroycall(p->gsm->gsm, p->gsmcall);
							p->gsmcall = NULL;
							gsm_rel(p->gsm);
						} else
							ast_log(LOG_WARNING, "Failed to grab GSM!\n");
					} else
						ast_log(LOG_WARNING, "The GSM Call has not been destroyed\n");
				}
				if (p->owner)
#if (ASTERISK_VERSION_NUM >= 110000)
					ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
					p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
			}
		}
#endif
		default:
			p->inalarm = 1;
			break;
		}
		res = get_alarms(p);
		handle_alarms(p, res);

#ifdef HAVE_GSMAT
		if (!p->gsm || !p->gsm->gsm || gsm_get_timer(p->gsm->gsm, GSM_TIMER_T309) < 0) {
			/* fall through intentionally FIXMEEEEEEEEEEEEEEEE*/
		} else {
			break;
		}
#endif

	case DAHDI_EVENT_ONHOOK:
		if (p->radio) {
			p->subs[idx].f.frametype = AST_FRAME_CONTROL;
#if (ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.subclass.integer = AST_CONTROL_RADIO_UNKEY;
#else  //(ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.subclass = AST_CONTROL_RADIO_UNKEY;
#endif //(ASTERISK_VERSION_NUM >= 10800)
			break;
		}
		if (p->oprmode < 0)
		{
			if (p->oprmode != -1) break;
			break;
		}
		switch (p->sig) {
		default:
			extra_disable_ec(p);
			return NULL;
		}
		break;
	case DAHDI_EVENT_RINGOFFHOOK:
		if (p->inalarm) break;
		if (p->oprmode < 0)
		{
			break;
		}
		if (p->radio)
		{
			p->subs[idx].f.frametype = AST_FRAME_CONTROL;
#if (ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.subclass.integer = AST_CONTROL_RADIO_KEY;
#else  //(ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.subclass = AST_CONTROL_RADIO_KEY;
#endif //(ASTERISK_VERSION_NUM >= 10800)
			break;
 		}
		break;
	default:
		ast_debug(1, "Dunno what to do with event %d on channel %d\n", res, p->channel);
	}
	return &p->subs[idx].f;
}

static struct ast_frame *__extra_exception(struct ast_channel *ast)
{
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(ast);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)
	int res;
	int idx;
	struct ast_frame *f;
	idx = extra_get_index(ast, p, 1);	
	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
#if (ASTERISK_VERSION_NUM >= 10800)
	p->subs[idx].f.subclass.integer = 0;
#else  //(ASTERISK_VERSION_NUM >= 10800)
	p->subs[idx].f.subclass = 0;
#endif //(ASTERISK_VERSION_NUM >= 10800)
	p->subs[idx].f.delivery = ast_tv(0,0);
	p->subs[idx].f.src = "extra_exception";
#if (ASTERISK_VERSION_NUM >= 10601)
	p->subs[idx].f.data.ptr = NULL;
#else  //(ASTERISK_VERSION_NUM >= 10601)
	p->subs[idx].f.data = NULL;
#endif //(ASTERISK_VERSION_NUM >= 10601)
	
	
	if ((!p->owner) && (!(p->radio || (p->oprmode < 0)))) {
		/* If nobody owns us, absorb the event appropriately, otherwise
		   we loop indefinitely.  This occurs when, during call waiting, the
		   other end hangs up our channel so that it no longer exists, but we
		   have neither FLASH'd nor ONHOOK'd to signify our desire to
		   change to the other channel. */
		res = extra_get_event(p->subs[SUB_REAL].dfd);
		/* Switch to real if there is one and this isn't something really silly... */
		if ((res != DAHDI_EVENT_RINGEROFF) && (res != DAHDI_EVENT_RINGERON) &&
			(res != DAHDI_EVENT_HOOKCOMPLETE)) {
			ast_debug(1, "Restoring owner of channel %d on event %d\n", p->channel, res);
			p->owner = p->subs[SUB_REAL].owner;
			if (p->owner && ast_bridged_channel(p->owner))
				ast_queue_control(p->owner, AST_CONTROL_UNHOLD);
		}
		switch (res) {
		case DAHDI_EVENT_ONHOOK:
			extra_disable_ec(p);
			if (p->owner) {
#if (ASTERISK_VERSION_NUM >= 110000)
				ast_verb(3, "Channel %s still has call, ringing phone\n", ast_channel_name(p->owner));
#else  //(ASTERISK_VERSION_NUM >= 110000)
				ast_verb(3, "Channel %s still has call, ringing phone\n", p->owner->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
				extra_ring_phone(p);
			} else
				ast_log(LOG_WARNING, "Absorbed on hook, but nobody is left!?!?\n");
//Freedom del 2012-02-02 13:49
#if 0
			update_conf(p);
#endif
			break;
		case DAHDI_EVENT_RINGOFFHOOK:
			extra_enable_ec(p);
			extra_set_hook(p->subs[SUB_REAL].dfd, DAHDI_OFFHOOK);
#if (ASTERISK_VERSION_NUM >= 110000)
			if (p->owner && (ast_channel_state(p->owner) == AST_STATE_RINGING)) {
#else  //(ASTERISK_VERSION_NUM >= 110000)
			if (p->owner && (p->owner->_state == AST_STATE_RINGING)) {
#endif //(ASTERISK_VERSION_NUM >= 110000)
				p->subs[SUB_REAL].needanswer = 1;
				p->dialing = 0;
			}
			break;
		case DAHDI_EVENT_HOOKCOMPLETE:
		case DAHDI_EVENT_RINGERON:
		case DAHDI_EVENT_RINGEROFF:
			/* Do nothing */
			break;
		default:
			ast_log(LOG_WARNING, "Don't know how to absorb event %s\n", event2str(res));
		}
		f = &p->subs[idx].f;
		return f;
	}
	if (!(p->radio || (p->oprmode < 0))) 
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Exception on %d, channel %d\n", ast_channel_fd(ast, 0),p->channel);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Exception on %d, channel %d\n", ast->fds[0],p->channel);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	/* If it's not us, return NULL immediately */
	if (ast != p->owner) {
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_log(LOG_WARNING, "We're %s, not %s\n", ast_channel_name(ast), ast_channel_name(p->owner));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_log(LOG_WARNING, "We're %s, not %s\n", ast->name, p->owner->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
		f = &p->subs[idx].f;
		return f;
	}
	f = extra_handle_event(ast);
	return f;
}

static int extra_send_text(struct ast_channel *ast, const char *text)
{	
	return 0;
}

static struct ast_frame *extra_exception(struct ast_channel *ast)
{	
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(ast);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)
	struct ast_frame *f;
	ast_mutex_lock(&p->lock);
	f = __extra_exception(ast);
	ast_mutex_unlock(&p->lock);
	return f;
}

static struct ast_frame *extra_read(struct ast_channel *ast)
{
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(ast);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)
	int res;
	int idx;
	void *readbuf;
	struct ast_frame *f=NULL;

	while (ast_mutex_trylock(&p->lock)) {
#if (ASTERISK_VERSION_NUM > 10444)
		CHANNEL_DEADLOCK_AVOIDANCE(ast);
#else  //(ASTERISK_VERSION_NUM > 10444)
		DEADLOCK_AVOIDANCE(&ast->lock);
#endif //(ASTERISK_VERSION_NUM > 10444)
	}

	idx = extra_get_index(ast, p, 0);

	/* Hang up if we don't really exist */
	if (idx < 0)	{
		ast_log(LOG_WARNING, "We don't exist?\n");
		ast_mutex_unlock(&p->lock);
		return NULL;
	}

	if ((p->radio || (p->oprmode < 0)) && p->inalarm) {
		ast_mutex_unlock(&p->lock);
		return NULL;
	}

	p->subs[idx].f.frametype = AST_FRAME_NULL;
	p->subs[idx].f.datalen = 0;
	p->subs[idx].f.samples = 0;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = 0;
#if (ASTERISK_VERSION_NUM >= 10800)
	p->subs[idx].f.subclass.integer = 0;
#else  //(ASTERISK_VERSION_NUM >= 10800)
	p->subs[idx].f.subclass = 0;
#endif //(ASTERISK_VERSION_NUM >= 10800)
	p->subs[idx].f.delivery = ast_tv(0,0);
	p->subs[idx].f.src = "extra_read";
#if (ASTERISK_VERSION_NUM >= 10601)
	p->subs[idx].f.data.ptr = NULL;
#else  //(ASTERISK_VERSION_NUM >= 10601)
	p->subs[idx].f.data = NULL;
#endif //(ASTERISK_VERSION_NUM >= 10601)

	/* make sure it sends initial key state as first frame */
	if ((p->radio || (p->oprmode < 0)) && (!p->firstradio))
	{
		struct dahdi_params ps;

		memset(&ps, 0, sizeof(ps));
		if (ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &ps) < 0) {
			ast_mutex_unlock(&p->lock);
			return NULL;
		}
		p->firstradio = 1;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
		if (ps.rxisoffhook)
		{
#if (ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.subclass.integer = AST_CONTROL_RADIO_KEY;
#else  //(ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.subclass = AST_CONTROL_RADIO_KEY;
#endif //(ASTERISK_VERSION_NUM >= 10800)
		}
		else
		{
#if (ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.subclass.integer = AST_CONTROL_RADIO_UNKEY;
#else  //(ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.subclass = AST_CONTROL_RADIO_UNKEY;
#endif //(ASTERISK_VERSION_NUM >= 10800)
		}
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}

	if (p->subs[idx].needbusy) {
		/* Send busy frame if requested */
		p->subs[idx].needbusy = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
#if (ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass.integer = AST_CONTROL_BUSY;
#else  //(ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass = AST_CONTROL_BUSY;
#endif //(ASTERISK_VERSION_NUM >= 10800)
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}
	
	if (p->subs[idx].needcongestion) {
		/* Send congestion frame if requested */
		p->subs[idx].needcongestion = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
#if (ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass.integer = AST_CONTROL_CONGESTION;
#else  //(ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass = AST_CONTROL_CONGESTION;
#endif //(ASTERISK_VERSION_NUM >= 10800)
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}

	if (p->subs[idx].needanswer) {
		/* Send answer frame if requested */
		p->subs[idx].needanswer = 0;
		p->subs[idx].f.frametype = AST_FRAME_CONTROL;
#if (ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass.integer = AST_CONTROL_ANSWER;
#else  //(ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass = AST_CONTROL_ANSWER;
#endif //(ASTERISK_VERSION_NUM >= 10800)
		ast_mutex_unlock(&p->lock);
		return &p->subs[idx].f;
	}



#if (ASTERISK_VERSION_NUM >= 110000)
	if (ast_channel_rawreadformat(ast)->id == AST_FORMAT_SLINEAR) {
#elif (ASTERISK_VERSION_NUM >= 100000)
	if (ast->rawreadformat.id == AST_FORMAT_SLINEAR) {
#else
	if (ast->rawreadformat == AST_FORMAT_SLINEAR) {
#endif
		if (!p->subs[idx].linear) {
			p->subs[idx].linear = 1;
			res = extra_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set channel %d (index %d) to linear mode.\n", p->channel, idx);
		}
#if (ASTERISK_VERSION_NUM >= 110000)
		} else if ((ast_channel_rawreadformat(ast)->id == AST_FORMAT_ULAW) ||
			(ast_channel_rawreadformat(ast)->id == AST_FORMAT_ALAW)) {
#elif (ASTERISK_VERSION_NUM >= 100000)
		} else if ((ast->rawreadformat.id == AST_FORMAT_ULAW) ||
			(ast->rawreadformat.id == AST_FORMAT_ALAW)) {
#else
		} else if ((ast->rawreadformat == AST_FORMAT_ULAW) ||
			(ast->rawreadformat == AST_FORMAT_ALAW)) {
#endif
		if (p->subs[idx].linear) {
			p->subs[idx].linear = 0;
			res = extra_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set channel %d (index %d) to companded mode.\n", p->channel, idx);
		}
	} else {
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_log(LOG_WARNING, "Don't know how to read frames in format %s\n", ast_getformatname(ast_channel_rawreadformat(ast)));
#elif (ASTERISK_VERSION_NUM >= 100000)
		ast_log(LOG_WARNING, "Don't know how to read frames in format %s\n", ast_getformatname(&ast->rawreadformat));
#else
		ast_log(LOG_WARNING, "Don't know how to read frames in format %s\n", ast_getformatname(ast->rawreadformat));	
#endif
		ast_mutex_unlock(&p->lock);
		return NULL;
	}
	readbuf = ((unsigned char *)p->subs[idx].buffer) + AST_FRIENDLY_OFFSET;
	CHECK_BLOCKING(ast);
	res = read(p->subs[idx].dfd, readbuf, p->subs[idx].linear ? READ_SIZE * 2 : READ_SIZE);

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_clear_flag(ast_channel_flags(ast), AST_FLAG_BLOCKING);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	ast_clear_flag(ast, AST_FLAG_BLOCKING);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	/* Check for hangup */
	if (res < 0) {
		f = NULL;
		if (res == -1) {
			if (errno == EAGAIN) {
				/* Return "NULL" frame if there is nobody there */
				ast_mutex_unlock(&p->lock);
				return &p->subs[idx].f;
			} else
				ast_log(LOG_WARNING, "extra_rec: %s\n", strerror(errno));
		}
		ast_mutex_unlock(&p->lock);
		return f;
	}
	if (res != (p->subs[idx].linear ? READ_SIZE * 2 : READ_SIZE)) {
		ast_debug(1, "Short read (%d/%d), must be an event...\n", res, p->subs[idx].linear ? READ_SIZE * 2 : READ_SIZE);
		ast_mutex_unlock(&p->lock);
		return f;
	}
	if (p->tdd) { /* if in TDD mode, see if we receive that */
		int c;

		c = tdd_feed(p->tdd,readbuf,READ_SIZE);
		if (c < 0) {
			ast_debug(1,"tdd_feed failed\n");
			ast_mutex_unlock(&p->lock);
			return NULL;
		}
		if (c) { /* if a char to return */
#if (ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.subclass.integer = 0;
#else  //(ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.subclass = 0;
#endif //(ASTERISK_VERSION_NUM >= 10800)
			p->subs[idx].f.frametype = AST_FRAME_TEXT;
			p->subs[idx].f.mallocd = 0;
			p->subs[idx].f.offset = AST_FRIENDLY_OFFSET;
#if (ASTERISK_VERSION_NUM >= 10601)
			p->subs[idx].f.data.ptr = p->subs[idx].buffer + AST_FRIENDLY_OFFSET;
#else  //(ASTERISK_VERSION_NUM >= 10601)
			p->subs[idx].f.data = p->subs[idx].buffer + AST_FRIENDLY_OFFSET;
#endif //(ASTERISK_VERSION_NUM >= 10601)
			p->subs[idx].f.datalen = 1;
#if (ASTERISK_VERSION_NUM >= 10601)
			*((char *) p->subs[idx].f.data.ptr) = c;
#else  //(ASTERISK_VERSION_NUM >= 10601)
			*((char *) p->subs[idx].f.data) = c;
#endif //(ASTERISK_VERSION_NUM >= 10601)
			ast_mutex_unlock(&p->lock);
			return &p->subs[idx].f;
		}
	}

	if (p->subs[idx].linear) {
		p->subs[idx].f.datalen = READ_SIZE * 2;
	} else
		p->subs[idx].f.datalen = READ_SIZE;

	p->subs[idx].f.frametype = AST_FRAME_VOICE;
#if (ASTERISK_VERSION_NUM >= 110000)
	memcpy(&p->subs[idx].f.subclass.format,ast_channel_rawreadformat(ast),sizeof(struct ast_format));
#elif (ASTERISK_VERSION_NUM >= 100000)
	memcpy(&p->subs[idx].f.subclass.format,&ast->rawreadformat,sizeof(struct ast_format));
#elif (ASTERISK_VERSION_NUM >= 10800)
	p->subs[idx].f.subclass.codec = ast->rawreadformat;
#else  //(ASTERISK_VERSION_NUM >= 10800)
	p->subs[idx].f.subclass = ast->rawreadformat;
#endif //(ASTERISK_VERSION_NUM >= 10800)
	p->subs[idx].f.samples = READ_SIZE;
	p->subs[idx].f.mallocd = 0;
	p->subs[idx].f.offset = AST_FRIENDLY_OFFSET;
#if (ASTERISK_VERSION_NUM >= 10601)
	p->subs[idx].f.data.ptr = p->subs[idx].buffer + AST_FRIENDLY_OFFSET / sizeof(p->subs[idx].buffer[0]);
#else  //(ASTERISK_VERSION_NUM >= 10601)
	p->subs[idx].f.data = p->subs[idx].buffer + AST_FRIENDLY_OFFSET / sizeof(p->subs[idx].buffer[0]);
#endif //(ASTERISK_VERSION_NUM >= 10601)

	if (p->dialing ||  p->radio || /* Transmitting something */
#if (ASTERISK_VERSION_NUM >= 110000)
		(idx && (ast_channel_state(ast) != AST_STATE_UP)) || /* Three-way or callwait that isn't up */
#else  //(ASTERISK_VERSION_NUM >= 110000)
		(idx && (ast->_state != AST_STATE_UP)) || /* Three-way or callwait that isn't up */
#endif //(ASTERISK_VERSION_NUM >= 110000)
		((idx == SUB_CALLWAIT) && !p->subs[SUB_CALLWAIT].inthreeway) /* Inactive and non-confed call-wait */
		) {
		/* Whoops, we're still dialing, or in a state where we shouldn't transmit....
		   don't send anything */
		p->subs[idx].f.frametype = AST_FRAME_NULL;
#if (ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass.integer = 0;
#else  //(ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.subclass = 0;
#endif //(ASTERISK_VERSION_NUM >= 10800)
		p->subs[idx].f.samples = 0;
		p->subs[idx].f.mallocd = 0;
		p->subs[idx].f.offset = 0;
#if (ASTERISK_VERSION_NUM >= 10601)
		p->subs[idx].f.data.ptr = NULL;
#else  //(ASTERISK_VERSION_NUM >= 10601)
		p->subs[idx].f.data = NULL;
#endif //(ASTERISK_VERSION_NUM >= 10601)
		p->subs[idx].f.datalen= 0;
	}
	if (p->dsp && (!p->ignoredtmf || p->busydetect) && !idx) {
#if (ASTERISK_VERSION_NUM >= 10601)		
		/* Perform busy detection etc on the extra line */
		int mute;
#endif //(ASTERISK_VERSION_NUM >= 10601)

		f = ast_dsp_process(ast, p->dsp, &p->subs[idx].f);

#if (ASTERISK_VERSION_NUM >= 10601)
		/* Check if DSP code thinks we should be muting this frame and mute the conference if so */
		mute = ast_dsp_was_muted(p->dsp);
		if (p->muting != mute) {
			p->muting = mute;
			extra_confmute(p, mute);
		}
#endif //(ASTERISK_VERSION_NUM >= 10601)

		if (f) {
#if (ASTERISK_VERSION_NUM >= 10800)
			if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass.integer == AST_CONTROL_BUSY)) {
#else  //(ASTERISK_VERSION_NUM >= 10800)
			if ((f->frametype == AST_FRAME_CONTROL) && (f->subclass == AST_CONTROL_BUSY)) {
#endif //(ASTERISK_VERSION_NUM >= 10800)
#if (ASTERISK_VERSION_NUM >= 110000)
				if ((ast_channel_state(ast) == AST_STATE_UP) && !p->outgoing) {
#else  //(ASTERISK_VERSION_NUM >= 110000)
				if ((ast->_state == AST_STATE_UP) && !p->outgoing) {
#endif //(ASTERISK_VERSION_NUM >= 110000)
					/* Treat this as a "hangup" instead of a "busy" on the assumption that
					   a busy */
					f = NULL;
				}
			} else if (f->frametype == AST_FRAME_DTMF) {
				/* DSP clears us of being pulse */
				p->pulsedial = 0;
			}
		}
	} else
		f = &p->subs[idx].f;

	if (f && (f->frametype == AST_FRAME_DTMF)) {
		extra_handle_dtmfup(ast, idx, &f);
	}

	ast_mutex_unlock(&p->lock);
	return f;
}

static int my_extra_write(struct extra_pvt *p, unsigned char *buf, int len, int idx, int linear)
{
	int sent=0;
	int size;
	int res;
	int fd;
	fd = p->subs[idx].dfd;
	while (len) {
		size = len;
		if (size > (linear ? READ_SIZE * 2 : READ_SIZE))
			size = (linear ? READ_SIZE * 2 : READ_SIZE);
		res = write(fd, buf, size);
		if (res != size) {
			ast_debug(1, "Write returned %d (%s) on channel %d\n", res, strerror(errno), p->channel);
			return sent;
		}
		len -= size;
		buf += size;
	}
	return sent;
}

static int extra_write(struct ast_channel *ast, struct ast_frame *frame)
{
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(ast);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)
	int res;
	int idx;
	idx = extra_get_index(ast, p, 0);
	if (idx < 0) {
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_log(LOG_WARNING, "%s doesn't really exist?\n", ast_channel_name(ast));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_log(LOG_WARNING, "%s doesn't really exist?\n", ast->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
		return -1;
	}

	/* Write a frame of (presumably voice) data */
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype != AST_FRAME_IMAGE)
			ast_log(LOG_WARNING, "Don't know what to do with frame type '%d'\n", frame->frametype);
		return 0;
	}
#if (ASTERISK_VERSION_NUM >= 100000)
	if ((frame->subclass.format.id!= AST_FORMAT_SLINEAR) &&
		(frame->subclass.format.id != AST_FORMAT_ULAW) &&
		(frame->subclass.format.id != AST_FORMAT_ALAW)) {
		ast_log(LOG_WARNING, "Cannot handle frames in %s format\n", ast_getformatname(&frame->subclass.format));
		return -1;
	}
#elif (ASTERISK_VERSION_NUM >= 10800)
	if ((frame->subclass.codec != AST_FORMAT_SLINEAR) &&
		(frame->subclass.codec != AST_FORMAT_ULAW) &&
		(frame->subclass.codec != AST_FORMAT_ALAW)) {
		ast_log(LOG_WARNING, "Cannot handle frames in %s format\n", ast_getformatname(frame->subclass.codec));
		return -1;
	}
#else  //(ASTERISK_VERSION_NUM >= 10800)
	if ((frame->subclass != AST_FORMAT_SLINEAR) && 
		(frame->subclass != AST_FORMAT_ULAW) &&
		(frame->subclass != AST_FORMAT_ALAW)) {
		ast_log(LOG_WARNING, "Cannot handle frames in %d format\n", frame->subclass);
		return -1;
	}
#endif //

	if (p->dialing) {
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Dropping frame since I'm still dialing on %s...\n",ast_channel_name(ast));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Dropping frame since I'm still dialing on %s...\n",ast->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
		return 0;
	}
	if (!p->owner) {
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Dropping frame since there is no active owner on %s...\n",ast_channel_name(ast));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_debug(1, "Dropping frame since there is no active owner on %s...\n",ast->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
		return 0;
	}

	/* Return if it's not valid data */
#if (ASTERISK_VERSION_NUM >= 10601)
	if (!frame->data.ptr || !frame->datalen)
#else  //(ASTERISK_VERSION_NUM >= 10601)
	if (!frame->data || !frame->datalen)
#endif //(ASTERISK_VERSION_NUM >= 10601)
		return 0;

#if (ASTERISK_VERSION_NUM >= 100000)
	if (frame->subclass.format.id== AST_FORMAT_SLINEAR) {
#elif (ASTERISK_VERSION_NUM >= 10800)
	if (frame->subclass.codec == AST_FORMAT_SLINEAR) {
#else  //(ASTERISK_VERSION_NUM >= 10800)
	if (frame->subclass == AST_FORMAT_SLINEAR) {
#endif //(ASTERISK_VERSION_NUM >= 10800)
		if (!p->subs[idx].linear) {
			p->subs[idx].linear = 1;
			res = extra_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set linear mode on channel %d\n", p->channel);
		}
#if (ASTERISK_VERSION_NUM >= 10601)
		res = my_extra_write(p, (unsigned char *)frame->data.ptr, frame->datalen, idx, 1);
#else  //(ASTERISK_VERSION_NUM >= 10601)
		res = my_extra_write(p, (unsigned char *)frame->data, frame->datalen, idx, 1);
#endif //(ASTERISK_VERSION_NUM >= 10601)
	} else {
		/* x-law already */
		if (p->subs[idx].linear) {
			p->subs[idx].linear = 0;
			res = extra_setlinear(p->subs[idx].dfd, p->subs[idx].linear);
			if (res)
				ast_log(LOG_WARNING, "Unable to set companded mode on channel %d\n", p->channel);
		}
#if (ASTERISK_VERSION_NUM >= 10601)
		res = my_extra_write(p, (unsigned char *)frame->data.ptr, frame->datalen, idx, 0);
#else  //(ASTERISK_VERSION_NUM >= 10601)
		res = my_extra_write(p, (unsigned char *)frame->data, frame->datalen, idx, 0);
#endif //(ASTERISK_VERSION_NUM >= 10601)
	}
	if (res < 0) {
		ast_log(LOG_WARNING, "write failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int extra_indicate(struct ast_channel *chan, int condition, const void *data, size_t datalen)
{
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(chan);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = chan->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	int res=-1;
	int idx;

	ast_mutex_lock(&p->lock);

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_debug(1, "Requested indication %d on channel %s\n", condition, ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
	ast_debug(1, "Requested indication %d on channel %s\n", condition, chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	switch (p->sig) {
	default:
		break;
	}
	idx = extra_get_index(chan, p, 0);
	if (idx == SUB_REAL) {
		switch (condition) {
		case AST_CONTROL_BUSY:
#ifdef HAVE_GSMAT
			if (p->priindication_oob && (p->sig == SIG_GSM_EXTRA)) {

#if (ASTERISK_VERSION_NUM >= 110000)
				ast_channel_hangupcause_set(chan, AST_CAUSE_USER_BUSY);
				ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
				chan->hangupcause = AST_CAUSE_USER_BUSY;
				chan->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)

				res = 0;
			} else if (!p->progress && (p->sig == SIG_GSM_EXTRA)
					&& p->gsm && !p->outgoing) {
				if (p->gsm->gsm) {		
					if (!gsm_grab(p, p->gsm)) {
						gsm_progress(p->gsm->gsm,p->gsmcall, GSM_PVT_TO_CHANNEL(p), 1);
						gsm_rel(p->gsm);
					}
					else
						ast_log(LOG_WARNING, "Unable to grab GSM on span %d\n", p->span);
				}
				p->progress = 1;
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_BUSY);
			} else
#endif			
			res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_BUSY);
			break;
		case AST_CONTROL_RINGING:
#ifdef HAVE_GSMAT
			if ((!p->alerting) && (p->sig == SIG_GSM_EXTRA) 
#if (ASTERISK_VERSION_NUM >= 110000)
					&& p->gsm && !p->outgoing && (ast_channel_state(chan) != AST_STATE_UP)) {
#else  //(ASTERISK_VERSION_NUM >= 110000)
					&& p->gsm && !p->outgoing && (chan->_state != AST_STATE_UP)) {
#endif //(ASTERISK_VERSION_NUM >= 110000)
				if (p->gsm->gsm) {		
					if (!gsm_grab(p, p->gsm)) {
						gsm_acknowledge(p->gsm->gsm,p->gsmcall, GSM_PVT_TO_CHANNEL(p), !p->digital);
						gsm_rel(p->gsm);
					}
					else
						ast_log(LOG_WARNING, "Unable to grab GSM on span %d\n", p->span);
				}
				p->alerting = 1;
			}

#endif			
			res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_RINGTONE);

#if (ASTERISK_VERSION_NUM >= 110000)
			if (ast_channel_state(chan) != AST_STATE_UP) {
				if ((ast_channel_state(chan) != AST_STATE_RING))
					ast_setstate(chan, AST_STATE_RINGING);
			}
#else  //(ASTERISK_VERSION_NUM >= 110000)
			if (chan->_state != AST_STATE_UP) {
				if ((chan->_state != AST_STATE_RING))
					ast_setstate(chan, AST_STATE_RINGING);
			}
#endif //(ASTERISK_VERSION_NUM >= 110000)


			break;
		case AST_CONTROL_PROCEEDING:
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1,"Received AST_CONTROL_PROCEEDING on %s\n",ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1,"Received AST_CONTROL_PROCEEDING on %s\n",chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

#ifdef HAVE_GSMAT
			if (!p->proceeding && (p->sig == SIG_GSM_EXTRA)
					&& p->gsm && !p->outgoing) {
				if (p->gsm->gsm) {		
					if (!gsm_grab(p, p->gsm)) {
						gsm_proceeding(p->gsm->gsm,p->gsmcall, GSM_PVT_TO_CHANNEL(p), !p->digital);
						gsm_rel(p->gsm);
					}
					else
						ast_log(LOG_WARNING, "Unable to grab GSM on span %d\n", p->span);
				}
				p->proceeding = 1;
				p->dialing = 0;
			}
#endif			
			/* don't continue in ast_indicate */
			res = 0;
			break;
		case AST_CONTROL_PROGRESS:
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1,"Received AST_CONTROL_PROGRESS on %s\n",ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1,"Received AST_CONTROL_PROGRESS on %s\n",chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

#ifdef HAVE_GSMAT
			p->digital = 0;	/* Digital-only calls isn't allows any inband progress messages */
			if (!p->progress && (p->sig == SIG_GSM_EXTRA)
					&& p->gsm && !p->outgoing) {
				if (p->gsm->gsm) {		
					if (!gsm_grab(p, p->gsm)) {
						gsm_progress(p->gsm->gsm,p->gsmcall, GSM_PVT_TO_CHANNEL(p), 1);
						gsm_rel(p->gsm);
					}
					else
						ast_log(LOG_WARNING, "Unable to grab GSM on span %d\n", p->span);
				}
				p->progress = 1;
			}
#endif			
			/* don't continue in ast_indicate */
			res = 0;
			break;
		case AST_CONTROL_CONGESTION:
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_channel_hangupcause_set(chan,AST_CAUSE_CONGESTION);
#else  //(ASTERISK_VERSION_NUM >= 110000)
			chan->hangupcause = AST_CAUSE_CONGESTION;
#endif //(ASTERISK_VERSION_NUM >= 110000)

#ifdef HAVE_GSMAT
			if (p->priindication_oob && (p->sig == SIG_GSM_EXTRA)) {
#if (ASTERISK_VERSION_NUM >= 110000)
				ast_channel_hangupcause_set(chan, AST_CAUSE_SWITCH_CONGESTION);
				ast_channel_softhangup_internal_flag_add(chan, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
				chan->hangupcause = AST_CAUSE_SWITCH_CONGESTION;
				chan->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
				res = 0;
			} else if (!p->progress && (p->sig == SIG_GSM_EXTRA)
					&& p->gsm && !p->outgoing) {
				if (p->gsm) {		
					if (!gsm_grab(p, p->gsm)) {
						gsm_progress(p->gsm->gsm,p->gsmcall, GSM_PVT_TO_CHANNEL(p), 1);
						gsm_rel(p->gsm);
					} else
						ast_log(LOG_WARNING, "Unable to grab GSM on span %d\n", p->span);
				}
				p->progress = 1;
				res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_CONGESTION);
			} else
#endif			
			break;

		case AST_CONTROL_RADIO_KEY:
			if (p->radio){
				res = extra_set_hook(p->subs[idx].dfd, DAHDI_OFFHOOK);
			}
			res = 0;
			break;
		case AST_CONTROL_RADIO_UNKEY:
			if (p->radio){
				res = extra_set_hook(p->subs[idx].dfd, DAHDI_RINGOFF);
			}
			res = 0;
			break;
		case AST_CONTROL_FLASH:
			/* flash hookswitch */
				res = 0;
			break;
		case AST_CONTROL_SRCUPDATE:
			res = 0;
			break;
		
		//Freedom Add for music on hold 2012-04-24 15:34
		//////////////////////////////////////////////////////////////////
		case AST_CONTROL_HOLD:
		  ast_moh_start(chan, data, p->mohinterpret);
		  break;
		case AST_CONTROL_UNHOLD:
		  ast_moh_stop(chan);
		  break;
		//////////////////////////////////////////////////////////////////
		case -1:
			res = tone_zone_play_tone(p->subs[idx].dfd, -1);
			break;
		}
	} else {
		res = 0;
	}
	ast_mutex_unlock(&p->lock);
	return res;
}

#if (ASTERISK_VERSION_NUM > 10444)
static struct ast_str *create_channel_name(struct extra_pvt *i)
#else  //(ASTERISK_VERSION_NUM > 10444)
static char *create_channel_name(struct extra_pvt *i)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
#if (ASTERISK_VERSION_NUM > 10444)
	struct ast_str *chan_name;
#else  //(ASTERISK_VERSION_NUM > 10444)
	char *b2 = NULL;
#endif //(ASTERISK_VERSION_NUM > 10444)
	int x, y;

#if (ASTERISK_VERSION_NUM > 10444)
	/* Create the new channel name tail. */
	if (!(chan_name = ast_str_create(32))) {
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)
	
	if (i->channel == CHAN_PSEUDO) {
#if (ASTERISK_VERSION_NUM > 10444)
		ast_str_set(&chan_name, 0, "pseudo-%ld", ast_random());
#else  //(ASTERISK_VERSION_NUM > 10444)
		b2 = ast_safe_string_alloc("pseudo-%ld", ast_random());
#endif //(ASTERISK_VERSION_NUM > 10444)
	} else {
		y = 1;
		do {
#if (ASTERISK_VERSION_NUM > 10444)
			ast_str_set(&chan_name, 0, "%d-%d", i->channel, y);
#else  //(ASTERISK_VERSION_NUM > 10444)
			if (b2)
				free(b2);
			b2 = ast_safe_string_alloc("%d-%d", i->channel, y);
#endif //(ASTERISK_VERSION_NUM > 10444)
	
			for (x = 0; x < SUB_SUM; ++x) {
#if (ASTERISK_VERSION_NUM >= 110000)
				if (i->subs[x].owner && !strcasecmp(ast_str_buffer(chan_name),ast_channel_name(i->subs[x].owner) + 6)) {
#elif (ASTERISK_VERSION_NUM >= 10601)
				if (i->subs[x].owner && !strcasecmp(ast_str_buffer(chan_name),i->subs[x].owner->name + 6)) {
#elif (ASTERISK_VERSION_NUM > 10444)
				if (i->subs[x].owner && !strcasecmp(chan_name->str,i->subs[x].owner->name + 6)) {
#else  //(ASTERISK_VERSION_NUM > 10444)
				if (i->subs[x].owner && !strcasecmp(b2, i->subs[x].owner->name + (!strncmp(i->subs[x].owner->name, "Zap", 3) ? 4 : 6))) {
#endif //(ASTERISK_VERSION_NUM >= 10601)
					break;
				}
			}
			++y;
		} while (x < SUB_SUM);
	}

#if (ASTERISK_VERSION_NUM > 10444)
	return chan_name;
#else  //(ASTERISK_VERSION_NUM > 10444)
	return b2;
#endif //(ASTERISK_VERSION_NUM > 10444)
}

static struct ast_channel *extra_new(struct extra_pvt *i, int state, int startpbx, int idx, int law, const char *linkedid)
{
	struct ast_channel *tmp;
#if (ASTERISK_VERSION_NUM >= 100000)
	struct ast_format deflaw;
#elif (ASTERISK_VERSION_NUM >= 10800)
	format_t deflaw;
#else  //(ASTERISK_VERSION_NUM >= 10800)
	int deflaw;
#endif //(ASTERISK_VERSION_NUM >= 100000)

	int x;
	int features;
#if (ASTERISK_VERSION_NUM > 10444)
	struct ast_str *chan_name;
#else  //(ASTERISK_VERSION_NUM > 10444)
	char *b2;
#endif //(ASTERISK_VERSION_NUM > 10444)
	struct ast_variable *v;

	if (i->subs[idx].owner) {
		ast_log(LOG_WARNING, "Channel %d already has a %s call\n", i->channel,subnames[idx]);
		return NULL;
	}
#if (ASTERISK_VERSION_NUM >= 100000)
	ast_format_clear(&deflaw);
#endif	
#if (ASTERISK_VERSION_NUM > 10444)
	chan_name = create_channel_name(i);

	if (!chan_name) {
		return NULL;
	}
#else  //(ASTERISK_VERSION_NUM > 10444)
	b2 = create_channel_name(i);

	if (!b2) {
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)
#if (ASTERISK_VERSION_NUM >= 11000)
	tmp = ast_channel_alloc(0, state, i->cid_num, i->cid_name, i->accountcode, i->exten, i->context, linkedid, i->amaflags, "EXTRA/%s", ast_str_buffer(chan_name));
#elif (ASTERISK_VERSION_NUM >= 10800)
	tmp = ast_channel_alloc(0, state, i->cid_num, i->cid_name, i->accountcode, i->exten, i->context, linkedid, i->amaflags, "EXTRA/%s", ast_str_buffer(chan_name));
#else  //(ASTERISK_VERSION_NUM >= 10800)
#if (ASTERISK_VERSION_NUM > 10444)
	tmp = ast_channel_alloc(0, state, i->cid_num, i->cid_name, i->accountcode, i->exten, i->context, i->amaflags, "EXTRA/%s", chan_name->str);
#else  //(ASTERISK_VERSION_NUM > 10444)
	tmp = ast_channel_alloc(0, state, i->cid_num, i->cid_name, i->accountcode, i->exten, i->context, i->amaflags, "EXTRA/%s", b2);
#endif //(ASTERISK_VERSION_NUM > 10444)
#endif //(ASTERISK_VERSION_NUM >= 10800)
#if (ASTERISK_VERSION_NUM > 10444)
	ast_free(chan_name);
#else  //(ASTERISK_VERSION_NUM > 10444)
	free(b2);
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (!tmp)
		return NULL;

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_channel_tech_set(tmp, &extra_tech);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	tmp->tech = &extra_tech;
#endif //(ASTERISK_VERSION_NUM >= 110000)

#if (ASTERISK_VERSION_NUM >= 10800)
	ast_channel_cc_params_init(tmp, i->cc_params);
#endif //(ASTERISK_VERSION_NUM >= 10800)
	if (law) {
		i->law = law;
		if (law == DAHDI_LAW_ALAW) {
#if (ASTERISK_VERSION_NUM >= 100000)
			ast_format_set(&deflaw, AST_FORMAT_ALAW, 0);
#else
			deflaw = AST_FORMAT_ALAW;
#endif
		} else {
#if (ASTERISK_VERSION_NUM >= 100000)
			ast_format_set(&deflaw, AST_FORMAT_ULAW, 0);
#else
			deflaw = AST_FORMAT_ULAW;
#endif
		}
	} else {
		switch (i->sig) {
		default:
			i->law = i->law_default;
			break;
		}
		if (i->law_default == DAHDI_LAW_ALAW) {
#if (ASTERISK_VERSION_NUM >= 100000)
			ast_format_set(&deflaw, AST_FORMAT_ALAW, 0);
#else
			deflaw = AST_FORMAT_ALAW;
#endif
		} else {
#if (ASTERISK_VERSION_NUM >= 100000)
			ast_format_set(&deflaw, AST_FORMAT_ULAW, 0);
#else
			deflaw = AST_FORMAT_ULAW;
#endif
		}
	}
#if (ASTERISK_VERSION_NUM > 10444)
	ast_channel_set_fd(tmp, 0, i->subs[idx].dfd);
#else  //(ASTERISK_VERSION_NUM > 10444)
	tmp->fds[0] = i->subs[idx].dfd;
#endif //(ASTERISK_VERSION_NUM > 10444)

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_format_cap_add(ast_channel_nativeformats(tmp), &deflaw);
	/* Start out assuming ulaw since it's smaller :) */
	ast_format_copy(ast_channel_rawreadformat(tmp), &deflaw);
	ast_format_copy(ast_channel_readformat(tmp), &deflaw);
	ast_format_copy(ast_channel_rawwriteformat(tmp), &deflaw);
	ast_format_copy(ast_channel_writeformat(tmp), &deflaw);
#elif (ASTERISK_VERSION_NUM >= 100000)
	ast_format_cap_add(tmp->nativeformats, &deflaw);
	/* Start out assuming ulaw since it's smaller :) */
	ast_format_copy(&tmp->rawreadformat, &deflaw);
	ast_format_copy(&tmp->readformat, &deflaw);
	ast_format_copy(&tmp->rawwriteformat, &deflaw);
	ast_format_copy(&tmp->writeformat, &deflaw);
#else
	tmp->nativeformats = deflaw;
	/* Start out assuming ulaw since it's smaller :) */
	tmp->rawreadformat = deflaw;
	tmp->readformat = deflaw;
	tmp->rawwriteformat = deflaw;
	tmp->writeformat = deflaw;
#endif
	i->subs[idx].linear = 0;
	extra_setlinear(i->subs[idx].dfd, i->subs[idx].linear);
	features = 0;
	if (idx == SUB_REAL) {
		x = DAHDI_TONEDETECT_ON | DAHDI_TONEDETECT_MUTE;
		if (ioctl(i->subs[idx].dfd, DAHDI_TONEDETECT, &x)) {
			i->hardwaredtmf = 0;
#if (ASTERISK_VERSION_NUM >= 10601)
			features |= DSP_FEATURE_DIGIT_DETECT;
#else  //(ASTERISK_VERSION_NUM >= 10601)
			features |= DSP_FEATURE_DTMF_DETECT;
#endif //(ASTERISK_VERSION_NUM >= 10601)
		}
	}
	if (features) {
		if (i->dsp) {
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Already have a dsp on %s?\n", ast_channel_name(tmp));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "Already have a dsp on %s?\n", tmp->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
		} else {
			if (i->channel != CHAN_PSEUDO)
				i->dsp = ast_dsp_new();
			else
				i->dsp = NULL;
			if (i->dsp) {
				i->dsp_features = features;
#ifdef HAVE_GSMAT
				/* We cannot do progress detection until receive PROGRESS message */
				if (i->outgoing && (i->sig == SIG_GSM_EXTRA)) {
					/* Remember requested DSP features, don't treat
					   talking as ANSWER */
					i->dsp_features = features & ~DSP_PROGRESS_TALK;
					features = 0;
				}
#endif	/*defined(HAVE_GSMAT)*/

				ast_dsp_set_features(i->dsp, features);
#if (ASTERISK_VERSION_NUM >= 10601)
				ast_dsp_set_digitmode(i->dsp, DSP_DIGITMODE_DTMF | i->dtmfrelax);
#else  //(ASTERISK_VERSION_NUM >= 10601)
				ast_dsp_digitmode(i->dsp, DSP_DIGITMODE_DTMF | i->dtmfrelax);
#endif //(ASTERISK_VERSION_NUM >= 10601)
				if (!ast_strlen_zero(progzone))
					ast_dsp_set_call_progress_zone(i->dsp, progzone);
				if (i->busydetect ) {
					ast_dsp_set_busy_count(i->dsp, i->busycount);
#if (ASTERISK_VERSION_NUM >= 100000)
					ast_dsp_set_busy_pattern(i->dsp, &i->busy_cadence);
#else
					ast_dsp_set_busy_pattern(i->dsp, i->busy_tonelength, i->busy_quietlength);
#endif
				}
			}
		}
	}

#if (ASTERISK_VERSION_NUM >= 110000)
	if (state == AST_STATE_RING)
		ast_channel_rings_set(tmp, 1);

	ast_channel_tech_pvt_set(tmp,i);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	if (state == AST_STATE_RING)
		tmp->rings = 1;

	tmp->tech_pvt = i;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	if (!ast_strlen_zero(i->language))
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_language_set(tmp, i->language);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_string_field_set(tmp, language, i->language);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	if (!i->owner)
		i->owner = tmp;
	if (!ast_strlen_zero(i->accountcode))
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_accountcode_set(tmp, i->accountcode);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_string_field_set(tmp, accountcode, i->accountcode);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	if (i->amaflags)
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_amaflags_set(tmp, i->amaflags);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		tmp->amaflags = i->amaflags;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	i->subs[idx].owner = tmp;

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_channel_context_set(tmp, i->context);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	ast_copy_string(tmp->context, i->context, sizeof(tmp->context));
#endif //(ASTERISK_VERSION_NUM >= 110000)

	/* If we've been told "no ADSI" then enforce it */
	if (!i->adsi)
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_adsicpe_set(tmp, AST_ADSI_UNAVAILABLE);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		tmp->adsicpe = AST_ADSI_UNAVAILABLE;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	if (!ast_strlen_zero(i->exten))
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_exten_set(tmp, i->exten);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_copy_string(tmp->exten, i->exten, sizeof(tmp->exten));
#endif //(ASTERISK_VERSION_NUM >= 110000)

	if (!ast_strlen_zero(i->rdnis)) {
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_redirecting(tmp)->from.number.valid = 1;
		ast_channel_redirecting(tmp)->from.number.str = ast_strdup(i->rdnis);
#elif (ASTERISK_VERSION_NUM >= 10800)
		tmp->redirecting.from.number.valid = 1;
		tmp->redirecting.from.number.str = ast_strdup(i->rdnis);
#else  //(ASTERISK_VERSION_NUM >= 10800)
		tmp->cid.cid_rdnis = ast_strdup(i->rdnis);
#endif //(ASTERISK_VERSION_NUM >= 110000)
	}

	if (!ast_strlen_zero(i->dnid)) {
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_dialed(tmp)->number.str = ast_strdup(i->dnid);
#elif (ASTERISK_VERSION_NUM >= 10800)
		tmp->dialed.number.str = ast_strdup(i->dnid);
#else  //(ASTERISK_VERSION_NUM >= 10800)
		tmp->cid.cid_dnid = ast_strdup(i->dnid);
#endif //(ASTERISK_VERSION_NUM >= 110000)
	}

	/* Don't use ast_set_callerid() here because it will
	 * generate a needless NewCallerID event */
#if  defined(HAVE_GSMAT)

#if (ASTERISK_VERSION_NUM >= 110000)
	if (!ast_strlen_zero(i->cid_ani)) {
		ast_channel_caller(tmp)->ani.number.valid = 1;
		ast_channel_caller(tmp)->ani.number.str = ast_strdup(i->cid_ani);
	} else if (!ast_strlen_zero(i->cid_num)) {
		ast_channel_caller(tmp)->ani.number.valid = 1;
		ast_channel_caller(tmp)->ani.number.str = ast_strdup(i->cid_num);
	}
#elif (ASTERISK_VERSION_NUM >= 10800)
	if (!ast_strlen_zero(i->cid_ani)) {
		tmp->caller.ani.number.valid = 1;
		tmp->caller.ani.number.str = ast_strdup(i->cid_ani);
	} else if (!ast_strlen_zero(i->cid_num)) {
		tmp->caller.ani.number.valid = 1;
		tmp->caller.ani.number.str = ast_strdup(i->cid_num);
	}
#else  //(ASTERISK_VERSION_NUM >= 10800)
	if (!ast_strlen_zero(i->cid_ani))
		tmp->cid.cid_ani = ast_strdup(i->cid_ani);
	else	
		tmp->cid.cid_ani = ast_strdup(i->cid_num);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	/* Assume calls are not idle calls unless we're told differently */
	i->isidlecall = 0;
	i->alreadyhungup = 0;

#else  //defined(HAVE_GSMAT)

#if (ASTERISK_VERSION_NUM >= 110000)
	if (!ast_strlen_zero(i->cid_num)) {
		ast_channel_caller(tmp)->ani.number.valid = 1;
		ast_channel_caller(tmp)->ani.number.str = ast_strdup(i->cid_num);
	}
#elif (ASTERISK_VERSION_NUM >= 10800)
	if (!ast_strlen_zero(i->cid_num)) {
		tmp->caller.ani.number.valid = 1;
		tmp->caller.ani.number.str = ast_strdup(i->cid_num);
	}
#else  //(ASTERISK_VERSION_NUM >= 10800)
	if (!ast_strlen_zero(i->cid_num)) {
		tmp->cid.cid_ani = ast_strdup(i->cid_num);
	}
#endif //(ASTERISK_VERSION_NUM >= 10800)
#endif	/*  defined(HAVE_GSMAT) */

	/* Assure there is no confmute on this channel */
	extra_confmute(i, 0);
#if (ASTERISK_VERSION_NUM >= 10601)
	i->muting = 0;
#endif //(ASTERISK_VERSION_NUM >= 10601)

#if (ASTERISK_VERSION_NUM >= 110000)
      	ast_devstate_changed_literal(ast_state_chan2dev(state), AST_DEVSTATE_NOT_CACHABLE, ast_channel_name(tmp));
	//ast_devstate_changed_literal(ast_state_chan2dev(state), ast_channel_name(tmp));
#elif (ASTERISK_VERSION_NUM >= 10820)
      ast_devstate_changed_literal(ast_state_chan2dev(state), AST_DEVSTATE_NOT_CACHABLE, tmp->name);
#elif (ASTERISK_VERSION_NUM >= 10601)
	ast_devstate_changed_literal(ast_state_chan2dev(state), tmp->name);
#else  //(ASTERISK_VERSION_NUM >= 10601)
	ast_device_state_changed_literal(tmp->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	for (v = i->vars ; v ; v = v->next)
		pbx_builtin_setvar_helper(tmp, v->name, v->value);

	ast_module_ref(ast_module_info->self);

	if (startpbx) {
		if (ast_pbx_start(tmp)) {
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ast_channel_name(tmp));
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

			ast_hangup(tmp);
			return NULL;
		}
	}
	return tmp;
}

/*! \brief enable or disable the chan_extra Do-Not-Disturb mode for a DAHDI channel
 * \param extrachan "Physical" DAHDI channel (e.g: DAHDI/5)
 * \param flag on 1 to enable, 0 to disable, -1 return dnd value
 *
 * chan_extra has a DND (Do Not Disturb) mode for each extrachan (physical
 * DAHDI channel). Use this to enable or disable it.
 *
 * \bug the use of the word "channel" for those extrachans is really confusing.
 */
static int extra_dnd(struct extra_pvt *extrachan, int flag)
{
	if (flag == -1) {
		return extrachan->dnd;
	}

	/* Do not disturb */
	extrachan->dnd = flag;
	
	ast_verb(3, "%s DND on channel %d\n",
			flag? "Enabled" : "Disabled",
			extrachan->channel);
	
	manager_event(EVENT_FLAG_SYSTEM, "DNDState",
			"Channel: EXTRA/%d\r\n"
			"Status: %s\r\n", extrachan->channel,
			flag? "enabled" : "disabled");

	return 0;
}

static void *analog_ss_thread(void *data)
{
	struct ast_channel *chan = data;
#if (ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = ast_channel_tech_pvt(chan);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	struct extra_pvt *p = chan->tech_pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	char exten[AST_MAX_EXTENSION] = "";
	int timeout;
	int len = 0;
	int res;
	int idx;

	ast_mutex_lock(&ss_thread_lock);
	ss_thread_count++;
	ast_mutex_unlock(&ss_thread_lock);
	/* in the bizarre case where the channel has become a zombie before we
	   even get started here, abort safely
	*/
	if (!p) {
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_log(LOG_WARNING, "Channel became a zombie before simple switch could be started (%s)\n", ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_log(LOG_WARNING, "Channel became a zombie before simple switch could be started (%s)\n", chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

		ast_hangup(chan);
		goto quit;
	}

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_verb(3, "Starting simple switch on '%s'\n", ast_channel_name(chan));
#else  //(ASTERISK_VERSION_NUM >= 110000)
	ast_verb(3, "Starting simple switch on '%s'\n", chan->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	idx = extra_get_index(chan, p, 1);
	if (idx < 0) {
		ast_log(LOG_WARNING, "Huh?\n");
		ast_hangup(chan);
		goto quit;
	}

	if (p->dsp)
		ast_dsp_digitreset(p->dsp);
	switch (p->sig) {
#ifdef HAVE_GSMAT
	case SIG_GSM_EXTRA:
		/* Now loop looking for an extension */
		ast_copy_string(exten, p->exten, sizeof(exten));
		len = strlen(exten);
		res = 0;

#if (ASTERISK_VERSION_NUM >= 110000)
		while ((len < AST_MAX_EXTENSION-1) && ast_matchmore_extension(chan, ast_channel_context(chan), exten, 1, p->cid_num)) {
			if (len && !ast_ignore_pattern(ast_channel_context(chan), exten))
#else  //(ASTERISK_VERSION_NUM >= 110000)
		while ((len < AST_MAX_EXTENSION-1) && ast_matchmore_extension(chan, chan->context, exten, 1, p->cid_num)) {
			if (len && !ast_ignore_pattern(chan->context, exten))
#endif //(ASTERISK_VERSION_NUM >= 110000)
				tone_zone_play_tone(p->subs[idx].dfd, -1);
			else
				tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_DIALTONE);

#if (ASTERISK_VERSION_NUM >= 110000)
			if (ast_exists_extension(chan, ast_channel_context(chan), exten, 1, p->cid_num))
#else  //(ASTERISK_VERSION_NUM >= 110000)
			if (ast_exists_extension(chan, chan->context, exten, 1, p->cid_num))
#endif //(ASTERISK_VERSION_NUM >= 110000)

				timeout = matchdigittimeout;
			else
				timeout = gendigittimeout;
			
			res = ast_waitfordigit(chan, timeout);
			if (res < 0) {
				ast_debug(1, "waitfordigit returned < 0...\n");
				ast_hangup(chan);
				goto quit;
			} else if (res) {
				exten[len++] = res;
				exten[len] = '\0';
			} else
				break;
		}
		
		/* if no extension was received ('unspecified') on overlap call, use the 's' extension */
		if (ast_strlen_zero(exten)) {
			ast_verb(3, "Going to extension s|1 because of empty extension received on overlap call\n");
			exten[0] = 's';
			exten[1] = '\0';
		}
		tone_zone_play_tone(p->subs[idx].dfd, -1);

#if (ASTERISK_VERSION_NUM >= 110000)
		if (ast_exists_extension(chan, ast_channel_context(chan), exten, 1, p->cid_num)) {
			/* Start the real PBX */
			ast_channel_exten_set(chan, exten);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		if (ast_exists_extension(chan, chan->context, exten, 1, p->cid_num)) {
			/* Start the real PBX */
			ast_copy_string(chan->exten, exten, sizeof(chan->exten));
#endif //(ASTERISK_VERSION_NUM >= 110000)
			if (p->dsp) ast_dsp_digitreset(p->dsp);
			extra_enable_ec(p);
			ast_setstate(chan, AST_STATE_RING);
			res = ast_pbx_run(chan);
			if (res) {
				ast_log(LOG_WARNING, "PBX exited non-zero!\n");
			}
		} else {
#if (ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "No such possible extension '%s' in context '%s'\n", exten, ast_channel_context(chan));
			ast_channel_hangupcause_set(chan, AST_CAUSE_UNALLOCATED);
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_debug(1, "No such possible extension '%s' in context '%s'\n", exten, chan->context);
			chan->hangupcause = AST_CAUSE_UNALLOCATED;
#endif //(ASTERISK_VERSION_NUM >= 110000)

			ast_hangup(chan);
			p->exten[0] = '\0';
			/* Since we send release complete here, we won't get one */
			p->gsmcall = NULL;
		}
		goto quit;
		break;
#endif

	default:
		ast_log(LOG_WARNING, "Don't know how to handle simple switch with signalling %s on channel %d\n", sig2str(p->sig), p->channel);
		res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_CONGESTION);
		if (res < 0)
				ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	}
	res = tone_zone_play_tone(p->subs[idx].dfd, DAHDI_TONE_CONGESTION);
	if (res < 0)
			ast_log(LOG_WARNING, "Unable to play congestion tone on channel %d\n", p->channel);
	ast_hangup(chan);
quit:
	ast_mutex_lock(&ss_thread_lock);
	ss_thread_count--;
	ast_cond_signal(&ss_thread_complete);
	ast_mutex_unlock(&ss_thread_lock);
	return NULL;
}

/* destroy a DAHDI channel, identified by its number */
static int extra_destroy_channel_bynum(int channel)
{
	struct extra_pvt *cur;

	ast_mutex_lock(&iflock);
	for (cur = iflist; cur; cur = cur->next) {
		if (cur->channel == channel) {
			int x = DAHDI_FLASH;

			/* important to create an event for extra_wait_event to register so that all analog_ss_threads terminate */
			ioctl(cur->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);

			destroy_channel(cur, 1);
			ast_mutex_unlock(&iflock);
			ast_module_unref(ast_module_info->self);
			return RESULT_SUCCESS;
		}
	}
	ast_mutex_unlock(&iflock);
	return RESULT_FAILURE;
}

#ifdef HAVE_GSMAT
static int gsm_resolve_span(int *span, int channel, int offset, struct dahdi_spaninfo *si)
{
	if (si->totalchans == 2) {
		/* GSM */
		gsms[*span].dchannel = 2 + offset;
	} else {
		ast_log(LOG_WARNING, "Unable to use span %d, since the D-channel cannot be located (unexpected span size of %d channels)\n", *span, si->totalchans);
		*span = -1;
		return 0;
	}

	gsms[*span].dchanavail |= DCHAN_PROVISIONED;
	gsms[*span].offset = offset;

	gsms[*span].span = *span + 1;
	return 0;
}
#endif

/* converts a DAHDI sigtype to signalling as can be configured from
 * chan_extra.conf.
 * While both have basically the same values, this will later be the
 * place to add filters and sanity checks
 */
static int sigtype_to_signalling(int sigtype)
{
	return sigtype;
}

/*!
 * \internal
 * \brief Get file name and channel number from (subdir,number)
 *
 * \param subdir name of the subdirectory under /dev/dahdi/
 * \param channel name of device file under /dev/dahdi/<subdir>/
 * \param path buffer to put file name in
 * \param pathlen maximal length of path
 *
 * \retval minor number of extra channel.
 * \retval -errno on error.
 */
static int device2chan(const char *subdir, int channel, char *path, int pathlen)
{
	struct stat	stbuf;
	int		num;

	snprintf(path, pathlen, "/dev/dahdi/%s/%d", subdir, channel);
	if (stat(path, &stbuf) < 0) {
		ast_log(LOG_ERROR, "stat(%s) failed: %s\n", path, strerror(errno));
		return -errno;
	}
	if (!S_ISCHR(stbuf.st_mode)) {
		ast_log(LOG_ERROR, "%s: Not a character device file\n", path);
		return -EINVAL;
	}
	num = minor(stbuf.st_rdev);
	ast_log(LOG_DEBUG, "%s -> %d\n", path, num);
	return num;

}

/*!
 * \internal
 * \brief Initialize/create a channel interface.
 *
 * \param channel Channel interface number to initialize/create.
 * \param conf Configuration parameters to initialize interface with.
 * \param reloading What we are doing now:
 * 0 - initial module load,
 * 1 - module reload,
 * 2 - module restart
 *
 * \retval Interface-pointer initialized/created
 * \retval NULL if error
 */
static struct extra_pvt *mkintf(int channel, const struct extra_chan_conf *conf, int reloading)
{
	/* Make a extra_pvt structure for this interface */
	struct extra_pvt *tmp;/*!< Current channel structure initializing */
	char fn[80];
	struct dahdi_bufferinfo bi;

	int res;
	int span = 0;
	int here = 0;/*!< TRUE if the channel interface already exists. */
	int x;

	struct dahdi_params p;

	/* Search channel interface list to see if it already exists. */
	for (tmp = iflist; tmp; tmp = tmp->next) {
		if (!tmp->destroy) {
			if (tmp->channel == channel) {
				/* The channel interface already exists. */
				here = 1;
				break;
			}
			if (tmp->channel > channel) {
				/* No way it can be in the sorted list. */
				tmp = NULL;
				break;
			}
		}
	}

	if (!here && reloading != 1) {
		tmp = ast_calloc(1, sizeof(*tmp));
		if (!tmp) {
			return NULL;
		}
#if (ASTERISK_VERSION_NUM >= 10800)
		tmp->cc_params = ast_cc_config_params_init();
		if (!tmp->cc_params) {
			ast_free(tmp);
			return NULL;
		}
#endif //(ASTERISK_VERSION_NUM >= 10800)
		ast_mutex_init(&tmp->lock);
		ifcount++;
		for (x = 0; x < SUB_SUM; x++)
			tmp->subs[x].dfd = -1;
		tmp->channel = channel;
		tmp->priindication_oob = conf->chan.priindication_oob;
	}

	if (tmp) {
		int chan_sig = conf->chan.sig;

		if (!here) {
			/* Can only get here if this is a new channel interface being created. */
			if ((channel != CHAN_PSEUDO)) {
				int count = 0;

				snprintf(fn, sizeof(fn), "%d", channel);
				/* Open non-blocking */
				tmp->subs[SUB_REAL].dfd = extra_open(fn);
				while (tmp->subs[SUB_REAL].dfd < 0 && reloading == 2 && count < 1000) { /* the kernel may not call extra_release fast enough for the open flagbit to be cleared in time */
					usleep(1);
					tmp->subs[SUB_REAL].dfd = extra_open(fn);
					count++;
				}
				/* Allocate a DAHDI structure */
				if (tmp->subs[SUB_REAL].dfd < 0) {
					ast_log(LOG_ERROR, "Unable to open channel %d: %s\nhere = %d, tmp->channel = %d, channel = %d\n", channel, strerror(errno), here, tmp->channel, channel);
					destroy_extra_pvt(tmp);
					return NULL;
				}
				memset(&p, 0, sizeof(p));
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &p);
				if (res < 0) {
					ast_log(LOG_ERROR, "Unable to get parameters: %s\n", strerror(errno));
					destroy_extra_pvt(tmp);
					return NULL;
				}
				if (conf->is_sig_auto)
					chan_sig = sigtype_to_signalling(p.sigtype);
				if (p.sigtype != (chan_sig & 0x3ffff)) {
					ast_log(LOG_ERROR, "Signalling requested on channel %d is %s but line is in %s signalling\n", channel, sig2str(chan_sig), sig2str(p.sigtype));
					destroy_extra_pvt(tmp);
					return NULL;
				}
				tmp->law_default = p.curlaw;
				tmp->law = p.curlaw;
				tmp->span = p.spanno;
				span = p.spanno - 1;
			} else {
				chan_sig = 0;
			}
			tmp->sig = chan_sig;
			tmp->outsigmod = conf->chan.outsigmod;

#ifdef HAVE_GSMAT
			if (chan_sig == SIG_GSM_EXTRA) {
				int offset;
				int myswitchtype;
				int matchesdchan;
				int x;
				offset = 0;
				if ((chan_sig == SIG_GSM_EXTRA) 
						&& ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &offset)) {
					ast_log(LOG_ERROR, "Unable to set clear mode on clear channel %d of span %d: %s\n", channel, p.spanno, strerror(errno));
					destroy_extra_pvt(tmp);
					return NULL;
				}
				if (span >= NUM_SPANS) {
					ast_log(LOG_ERROR, "Channel %d does not lie on a span I know of (%d)\n", channel, span);
					destroy_extra_pvt(tmp);
					return NULL;
				} else {
					struct dahdi_spaninfo si;
					si.spanno = 0;
					if (ioctl(tmp->subs[SUB_REAL].dfd,DAHDI_SPANSTAT,&si) == -1) {
						ast_log(LOG_ERROR, "Unable to get span status: %s\n", strerror(errno));
						destroy_extra_pvt(tmp);
						return NULL;
					}
					gsm_resolve_span(&span, channel, (channel - p.chanpos), &si);
					if (span < 0) {
						ast_log(LOG_WARNING, "Channel %d: Unable to find locate channel!\n", channel);
						destroy_extra_pvt(tmp);
						return NULL;
					}
					myswitchtype = conf->gsm.switchtype;
					/* Make sure this isn't a d-channel */
					matchesdchan=0;
					for (x = 0; x < NUM_SPANS; x++) {
						if (gsms[x].dchannel == tmp->channel) {
							matchesdchan = 1;
						}
					}
					offset = p.chanpos;
					if (!matchesdchan) {
						if (gsms[span].nodetype && (gsms[span].nodetype != conf->gsm.nodetype)) {
							ast_log(LOG_ERROR, "Span %d is already a %s node\n", span + 1, gsm_node2str(gsms[span].nodetype));
							destroy_extra_pvt(tmp);
							return NULL;
						}
						///*Freedom Modify 2011-10-10 10:11*/
						//if (gsms[span].switchtype && (gsms[span].switchtype != myswitchtype)) {
						if ( gsms[span].switchtype == -1 && (gsms[span].switchtype != myswitchtype)) {
							/*Freedom Modify 2011-10-10 10:11*/
							//ast_log(LOG_ERROR, "Span %d is already a %s switch\n", span + 1, gsm_switch2str(gsms[span].switchtype));
							ast_log(LOG_ERROR, "Span %d is already a %s switch\n", span + 1, gsm_get_module_name(gsms[span].switchtype));
							destroy_extra_pvt(tmp);
							return NULL;
						}

						ast_copy_string(gsms[span].pin, gsm_modem_pin, sizeof(gsms[span].pin));
						gsms[span].sig = chan_sig;
						gsms[span].nodetype = conf->gsm.nodetype;
						gsms[span].switchtype = myswitchtype;
						gsms[span].pvt = tmp;
						gsms[span].resetinterval = conf->gsm.resetinterval;
						gsms[span].numchans++;
#ifdef VIRTUAL_TTY
						gsms[span].virtual_tty = conf->gsm.virtual_tty;
#endif //VIRTUAL_TTY

						gsms[span].debug_at_flag = conf->gsm.debug_at_flag;
						gsms[span].vol = conf->gsm.vol;
						gsms[span].mic = conf->gsm.mic;
						if(strlen(conf->gsm.send_sms.coding)) {
							strncpy(gsms[span].send_sms.coding, conf->gsm.send_sms.coding, sizeof(gsms[span].send_sms.coding));
						}
						tmp->gsm = &gsms[span];
						tmp->gsmoffset = offset;
						tmp->gsmcall = NULL;
					} else {
						ast_log(LOG_ERROR, "Channel %d is reserved for D-channel.\n", offset);
						destroy_extra_pvt(tmp);
						return NULL;
					}
				}
			} else {
				tmp->gsmoffset = 0;
			}
#endif			
		} else {
			/* already exists in interface list */
			ast_log(LOG_WARNING, "Attempt to configure channel %d with signaling %s ignored because it is already configured to be %s.\n", tmp->channel, extra_sig2str(chan_sig), extra_sig2str(tmp->sig));
			chan_sig = tmp->sig;
			if (tmp->subs[SUB_REAL].dfd > -1) {
				memset(&p, 0, sizeof(p));
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &p);
			}
		}

		if (tmp->radio) {
			/* XXX Waiting to hear back from Jim if these should be adjustable XXX */
			p.channo = channel;
			p.rxwinktime = 1;
			p.rxflashtime = 1;
			p.starttime = 1;
			p.debouncetime = 5;
		} else {
			p.channo = channel;
		}

		/* don't set parms on a pseudo-channel */
		if (tmp->subs[SUB_REAL].dfd >= 0)
		{
			res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_SET_PARAMS, &p);
			if (res < 0) {
				ast_log(LOG_ERROR, "Unable to set parameters: %s\n", strerror(errno));
				destroy_extra_pvt(tmp);
				return NULL;
			}
		}
#if 1
		if (!here && (tmp->subs[SUB_REAL].dfd > -1)) {
			memset(&bi, 0, sizeof(bi));
			res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_BUFINFO, &bi);
			if (!res) {
				bi.txbufpolicy = conf->chan.buf_policy;
				bi.rxbufpolicy = conf->chan.buf_policy;
				bi.numbufs = conf->chan.buf_no;
				res = ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi);
				if (res < 0) {
					ast_log(LOG_WARNING, "Unable to set buffer policy on channel %d: %s\n", channel, strerror(errno));
				}
			} else {
				ast_log(LOG_WARNING, "Unable to check buffer policy on channel %d: %s\n", channel, strerror(errno));
			}
			tmp->buf_policy = conf->chan.buf_policy;
			tmp->buf_no = conf->chan.buf_no;
			tmp->usefaxbuffers = conf->chan.usefaxbuffers;
			tmp->faxbuf_policy = conf->chan.faxbuf_policy;
			tmp->faxbuf_no = conf->chan.faxbuf_no;
			/* This is not as gnarly as it may first appear.  If the ioctl above failed, we'd be setting
			 * tmp->bufsize to zero which would cause subsequent faxbuffer-related ioctl calls to fail.
			 * The reason the ioctl call above failed should to be determined before worrying about the
			 * faxbuffer-related ioctl calls */
			tmp->bufsize = bi.bufsize;
		}
#endif
		tmp->immediate = conf->chan.immediate;
		tmp->firstradio = 0;
		/* Flag to destroy the channel must be cleared on new mkif.  Part of changes for reload to work */
		tmp->destroy = 0;

		tmp->adsi = conf->chan.adsi;

		tmp->callreturn = conf->chan.callreturn;
		tmp->echocancel = conf->chan.echocancel;

		tmp->pulse = conf->chan.pulse;
		if (tmp->echocancel.head.tap_length) {
			tmp->echocanbridged = conf->chan.echocanbridged;
		} else {
			if (conf->chan.echocanbridged)
				ast_log(LOG_NOTICE, "echocancelwhenbridged requires echocancel to be enabled; ignoring\n");
			tmp->echocanbridged = 0;
		}
		tmp->busydetect = conf->chan.busydetect;
		tmp->busycount = conf->chan.busycount;
#if (ASTERISK_VERSION_NUM >= 100000)
		tmp->busy_cadence = conf->chan.busy_cadence;
#else
		tmp->busy_tonelength = conf->chan.busy_tonelength;
		tmp->busy_quietlength = conf->chan.busy_quietlength;
#endif
		tmp->cancallforward = conf->chan.cancallforward;
		tmp->dtmfrelax = conf->chan.dtmfrelax;

		tmp->channel = channel;
		tmp->stripmsd = conf->chan.stripmsd;
		tmp->use_callerid = conf->chan.use_callerid;

		ast_copy_string(tmp->accountcode, conf->chan.accountcode, sizeof(tmp->accountcode));
		tmp->amaflags = conf->chan.amaflags;
		if (!here) {
			tmp->confno = -1;
		}
		tmp->canpark = conf->chan.canpark;

		ast_copy_string(tmp->defcontext,conf->chan.context,sizeof(tmp->defcontext));
		ast_copy_string(tmp->language, conf->chan.language, sizeof(tmp->language));

		ast_copy_string(tmp->context, conf->chan.context, sizeof(tmp->context));
		
		//Freedom Add for music on hold 2012-04-24 15:34
		//////////////////////////////////////////////////////////////////
		ast_copy_string(tmp->mohinterpret,conf->chan.mohinterpret,sizeof(tmp->mohinterpret));
		
		tmp->group = conf->chan.group;

#if (ASTERISK_VERSION_NUM > 10444)
		if (conf->chan.vars) {
			struct ast_variable *v, *tmpvar;
			for (v = conf->chan.vars ; v ; v = v->next) {
				if ((tmpvar = ast_variable_new(v->name, v->value, v->file))) {
					tmpvar->next = tmp->vars;
					tmp->vars = tmpvar;
				}
			}
		}
#endif //(ASTERISK_VERSION_NUM > 10444)	

		tmp->cid_rxgain = conf->chan.cid_rxgain;
		tmp->rxgain = conf->chan.rxgain;
		tmp->txgain = conf->chan.txgain;
		tmp->txdrc = conf->chan.txdrc;
		tmp->rxdrc = conf->chan.rxdrc;
		tmp->tonezone = conf->chan.tonezone;
		if (tmp->subs[SUB_REAL].dfd > -1) {
			set_actual_gain(tmp->subs[SUB_REAL].dfd, tmp->rxgain, tmp->txgain, tmp->rxdrc, tmp->txdrc, tmp->law);
			if (tmp->dsp)
#if (ASTERISK_VERSION_NUM >= 10601)
				ast_dsp_set_digitmode(tmp->dsp, DSP_DIGITMODE_DTMF | tmp->dtmfrelax);
#else  //(ASTERISK_VERSION_NUM >= 10601)
				ast_dsp_digitmode(tmp->dsp, DSP_DIGITMODE_DTMF | tmp->dtmfrelax);
#endif //(ASTERISK_VERSION_NUM >= 10601)
//Freedom del 2012-02-02 13:49
#if 0
			update_conf(tmp);
#endif

//Freedom del 2012-02-01 18:00
#if 0
			if (!here) {
				switch (chan_sig) {
				default:
					/* Hang it up to be sure it's good */
					extra_set_hook(tmp->subs[SUB_REAL].dfd, DAHDI_ONHOOK);
					break;
				}
			}
#endif
			ioctl(tmp->subs[SUB_REAL].dfd,DAHDI_SETTONEZONE,&tmp->tonezone);
#ifdef HAVE_GSMAT
			/* the dchannel is down so put the channel in alarm */
			if (tmp->gsm && !gsm_is_up(tmp->gsm))
				tmp->inalarm = 1;
#endif			
			if ((res = get_alarms(tmp)) != DAHDI_ALARM_NONE) {
				/* the dchannel is down so put the channel in alarm */
				switch (tmp->sig) {
				default:
					tmp->inalarm = 1;
					break;
				}
				handle_alarms(tmp, res);
			}
		}

		tmp->answeronpolarityswitch = conf->chan.answeronpolarityswitch;
		tmp->hanguponpolarityswitch = conf->chan.hanguponpolarityswitch;

#if (ASTERISK_VERSION_NUM >= 10800)
		ast_cc_copy_config_params(tmp->cc_params, conf->chan.cc_params);
#endif //(ASTERISK_VERSION_NUM >= 10800)

		if (!here) {
			/* We default to in service on protocols that don't have a reset */
			tmp->inservice = 1;
		}
	}
	if (tmp && !here) {
		/* Add the new channel interface to the sorted channel interface list. */
		extra_iflist_insert(tmp);
	}
	return tmp;
}

static int is_group_or_channel_match(struct extra_pvt *p, int span, ast_group_t groupmatch, int *groupmatched, int channelmatch, int *channelmatched)
{
	/* check group matching */
	if (groupmatch) {
		if ((p->group & groupmatch) != groupmatch)
			/* Doesn't match the specified group, try the next one */
			return 0;
		*groupmatched = 1;
	}
	/* Check to see if we have a channel match */
	if (channelmatch != -1) {
		if (p->channel != channelmatch)
			/* Doesn't match the specified channel, try the next one */
			return 0;
		*channelmatched = 1;
	}

	return 1;
}

static int available(struct extra_pvt **pvt, int is_specific_channel)
{
	struct extra_pvt *p = *pvt;

	if (p->inalarm)
		return 0;

	/*switch (p->sig) {
	default:
		break;
	}*/

	/*if (p->locallyblocked || p->remotelyblocked) {
		return 0;
	}*/

	/* If no owner definitely available */
	if (!p->owner) {
#ifdef HAVE_GSMAT
		/* Trust GSM */
		if (p->gsm) {
			if (p->resetting || p->gsmcall)
				return 0;
			else
				return 1;
		}
#endif

		return 1;
	}

	return 0;
}


/* This function can *ONLY* be used for copying pseudo (CHAN_PSEUDO) private
   structures; it makes no attempt to safely copy regular channel private
   structures that might contain reference-counted object pointers and other
   scary bits
*/
static struct extra_pvt *duplicate_pseudo(struct extra_pvt *src)
{
	struct extra_pvt *p;
	struct dahdi_bufferinfo bi;
	int res;

	p = ast_malloc(sizeof(*p));
	if (!p) {
		return NULL;
	}
	*p = *src;

#if (ASTERISK_VERSION_NUM >= 10800)
	/* Must deep copy the cc_params. */
	p->cc_params = ast_cc_config_params_init();
	if (!p->cc_params) {
		ast_free(p);
		return NULL;
	}
	ast_cc_copy_config_params(p->cc_params, src->cc_params);
#endif //(ASTERISK_VERSION_NUM >= 10800)

	p->which_iflist = EXTRA_IFLIST_NONE;
	p->next = NULL;
	p->prev = NULL;
	ast_mutex_init(&p->lock);
	p->subs[SUB_REAL].dfd = extra_open("/dev/dahdi/pseudo");
	if (p->subs[SUB_REAL].dfd < 0) {
		ast_log(LOG_ERROR, "Unable to dup channel: %s\n", strerror(errno));
		destroy_extra_pvt(p);
		return NULL;
	}
	res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_GET_BUFINFO, &bi);
	if (!res) {
		bi.txbufpolicy = src->buf_policy;
		bi.rxbufpolicy = src->buf_policy;
		bi.numbufs = src->buf_no;
		res = ioctl(p->subs[SUB_REAL].dfd, DAHDI_SET_BUFINFO, &bi);
		if (res < 0) {
			ast_log(LOG_WARNING, "Unable to set buffer policy on dup channel: %s\n", strerror(errno));
		}
	} else
		ast_log(LOG_WARNING, "Unable to check buffer policy on dup channel: %s\n", strerror(errno));
	p->destroy = 1;
	extra_iflist_insert(p);
	return p;
}

#ifdef HAVE_GSMAT
static int gsm_find_empty_chan(struct extra_gsm *gsm, int backwards)
{
//	return 1;
	
	int x;

	if (backwards) {
		x = gsm->numchans;
	} else {
		x = 0;
	}
	
	for (;;) {
		if (backwards && (x < 0)) {
			break;
		}
		if (!backwards && (x >= gsm->numchans))
			break;
		if (gsm->pvt && !gsm->pvt->inalarm && !gsm->pvt->owner) {
			ast_debug(1, "Found empty available channel %d\n", 
				gsm->pvt->gsmoffset);
			return 1; /* FIXME = ESTA FUNCION SOBRA */
		}
		if (backwards)
			x--;
		else
			x++;
	}
	
	return -1;
}
#endif

struct extra_starting_point {
	/*! Group matching mask.  Zero if not specified. */
	ast_group_t groupmatch;
	/*! DAHDI channel to match with.  -1 if not specified. */
	int channelmatch;
	/*! Round robin saved search location index. (Valid if roundrobin TRUE) */
	int rr_starting_point;
	/*! ISDN span where channels can be picked (Zero if not specified) */
	int span;
	/*! Analog channel distinctive ring cadance index. */
	int cadance;
	/*! Dialing option. c/r/d if present and valid. */
	char opt;
	/*! TRUE if to search the channel list backwards. */
	char backwards;
	/*! TRUE if search is done with round robin sequence. */
	char roundrobin;
};

static struct extra_pvt *determine_starting_point(const char *data, struct extra_starting_point *param)
{
	char *dest;
	char *s;
	int x;
	int res = 0;
	struct extra_pvt *p;
	char *subdir = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(group);	/* channel/group token */
		AST_APP_ARG(other);	/* Any remining unused arguments */
	);

	/*
	 * data is ---v
	 * Dial(DAHDI/pseudo[/extension[/options]])
	 * Dial(DAHDI/<channel#>[c|r<cadance#>|d][/extension[/options]])
	 * Dial(DAHDI/[i<span>-](g|G|r|R)<group#(0-63)>[c|r<cadance#>|d][/extension[/options]])
	 * Dial(DAHDI/<subdir>!<channel#>[c|r<cadance#>|d][/extension[/options]])
	 *
	 * i - ISDN span channel restriction.
	 *     Used by CC to ensure that the CC recall goes out the same span.
	 *
	 * g - channel group allocation search forward
	 * G - channel group allocation search backward
	 * r - channel group allocation round robin search forward
	 * R - channel group allocation round robin search backward
	 *
	 * c - Wait for DTMF digit to confirm answer
	 * r<cadance#> - Set distintive ring cadance number
	 * d - Force bearer capability for ISDN/SS7 call to digital.
	 */

	if (data) {
		dest = ast_strdupa(data);
	} else {
		ast_log(LOG_WARNING, "Channel requested with no data\n");
		return NULL;
	}
	AST_NONSTANDARD_APP_ARGS(args, dest, '/');
	if (!args.argc || ast_strlen_zero(args.group)) {
		ast_log(LOG_WARNING, "No channel/group specified\n");
		return NULL;
	}

	/* Initialize the output parameters */
	memset(param, 0, sizeof(*param));
	param->channelmatch = -1;

	if (strchr(args.group, '!') != NULL) {
		char *prev = args.group;
		while ((s = strchr(prev, '!')) != NULL) {
			*s++ = '/';
			prev = s;
		}
		*(prev - 1) = '\0';
		subdir = args.group;
		args.group = prev;
	} else if (args.group[0] == 'i') {
		/* Extract the ISDN span channel restriction specifier. */
		res = sscanf(args.group + 1, "%30d", &x);
		if (res < 1) {
			ast_log(LOG_WARNING, "Unable to determine ISDN span for data %s\n", data);
			return NULL;
		}
		param->span = x;

		/* Remove the ISDN span channel restriction specifier. */
		s = strchr(args.group, '-');
		if (!s) {
			ast_log(LOG_WARNING, "Bad ISDN span format for data %s\n", data);
			return NULL;
		}
		args.group = s + 1;
		res = 0;
	}
	if (toupper(args.group[0]) == 'G' || toupper(args.group[0])=='R') {
		/* Retrieve the group number */
		s = args.group + 1;
		res = sscanf(s, "%30d%1c%30d", &x, &param->opt, &param->cadance);
		if (res < 1) {
			ast_log(LOG_WARNING, "Unable to determine group for data %s\n", data);
			return NULL;
		}
		param->groupmatch = ((ast_group_t) 1 << x);

		if (toupper(args.group[0]) == 'G') {
			if (args.group[0] == 'G') {
				param->backwards = 1;
				p = ifend;
			} else
				p = iflist;
		} else {
			if (ARRAY_LEN(round_robin) <= x) {
				ast_log(LOG_WARNING, "Round robin index %d out of range for data %s\n",
					x, data);
				return NULL;
			}
			if (args.group[0] == 'R') {
				param->backwards = 1;
				p = round_robin[x] ? round_robin[x]->prev : ifend;
				if (!p)
					p = ifend;
			} else {
				p = round_robin[x] ? round_robin[x]->next : iflist;
				if (!p)
					p = iflist;
			}
			param->roundrobin = 1;
			param->rr_starting_point = x;
		}
	} else {
		s = args.group;
		if (!strcasecmp(s, "pseudo")) {
			/* Special case for pseudo */
			x = CHAN_PSEUDO;
			param->channelmatch = x;
		} else {
			res = sscanf(s, "%30d%1c%30d", &x, &param->opt, &param->cadance);
			if (res < 1) {
				ast_log(LOG_WARNING, "Unable to determine channel for data %s\n", data);
				return NULL;
			} else {
				param->channelmatch = x;
			}
		}
		if (subdir) {
			char path[PATH_MAX];
			struct stat stbuf;

			snprintf(path, sizeof(path), "/dev/dahdi/%s/%d",
					subdir, param->channelmatch);
			if (stat(path, &stbuf) < 0) {
				ast_log(LOG_WARNING, "stat(%s) failed: %s\n",
						path, strerror(errno));
				return NULL;
			}
			if (!S_ISCHR(stbuf.st_mode)) {
				ast_log(LOG_ERROR, "%s: Not a character device file\n",
						path);
				return NULL;
			}
			param->channelmatch = minor(stbuf.st_rdev);
		}

		p = iflist;
	}

	if (param->opt == 'r' && res < 3) {
		ast_log(LOG_WARNING, "Distinctive ring missing identifier in '%s'\n", data);
		param->opt = '\0';
	}

	return p;
}

#if (ASTERISK_VERSION_NUM >= 110000)
static struct ast_channel *extra_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause)
#elif (ASTERISK_VERSION_NUM >= 100000)
static struct ast_channel *extra_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, void *data, int *cause)
#elif ASTERISK_VERSION_NUM >= 10800
static struct ast_channel *extra_request(const char *type, format_t format, const struct ast_channel *requestor, void *data, int *cause)
#else  //(ASTERISK_VERSION_NUM >= 10800)
static struct ast_channel *extra_request(const char *type, int format, void *data, int *cause)
#endif //(ASTERISK_VERSION_NUM >= 10800)

{
	int callwait = 0;
	struct extra_pvt *p;
	struct ast_channel *tmp = NULL;
	struct extra_pvt *exitpvt;
	int channelmatched = 0;
	int groupmatched = 0;
	int transcapdigital = 0;
	struct extra_starting_point start;
	
	ast_mutex_lock(&iflock);
	p = determine_starting_point(data, &start);
	if (!p) {
		/* We couldn't determine a starting point, which likely means badly-formatted channel name. Abort! */
		ast_mutex_unlock(&iflock);
		return NULL;
	}
	
	/* Search for an unowned channel */
	exitpvt = p;
	while (p && !tmp) {
		if (start.roundrobin)
			round_robin[start.rr_starting_point] = p;

		if (is_group_or_channel_match(p, start.span, start.groupmatch, &groupmatched, start.channelmatch, &channelmatched)
			&& available(&p, channelmatched)) {
			ast_debug(1, "Using channel %d\n", p->channel);

			callwait = (p->owner != NULL);
			if (p->gsm)
			{
				if((p->gsm->dchanavail&DCHAN_UP)==0)
				{
					break;
				}
			}
			if (p->channel == CHAN_PSEUDO) {
				p = duplicate_pseudo(p);
				if (!p) {
					break;
				}
			}

			/* Make special notes */
			switch (start.opt) {
			case '\0':
				/* No option present. */
				break;
			case 'c':
				/* Confirm answer */
				p->confirmanswer = 1;
				break;
			case 'r':
				/* Distinctive ring */
				p->distinctivering = start.cadance;
				break;
			case 'd':
				/* If this is an ISDN call, make it digital */
				transcapdigital = AST_TRANS_CAP_DIGITAL;
				break;
			default:
				ast_log(LOG_WARNING, "Unknown option '%c' in '%s'\n", start.opt, (char *)data);
				break;
			}

			p->outgoing = 1;

#if (ASTERISK_VERSION_NUM >= 110000)
			tmp = extra_new(p, AST_STATE_RESERVED, 0, p->owner ? SUB_CALLWAIT : SUB_REAL, 0, requestor ? ast_channel_linkedid(requestor) : "");
#elif (ASTERISK_VERSION_NUM >= 10800)
			tmp = extra_new(p, AST_STATE_RESERVED, 0, p->owner ? SUB_CALLWAIT : SUB_REAL, 0, requestor ? requestor->linkedid : "");
#else  //(ASTERISK_VERSION_NUM >= 10800)
			tmp = extra_new(p, AST_STATE_RESERVED, 0, p->owner ? SUB_CALLWAIT : SUB_REAL, 0, 0);
#endif //(ASTERISK_VERSION_NUM >= 110000)
			if (!tmp) {
				p->outgoing = 0;
			} else {
				snprintf(p->dialstring, sizeof(p->dialstring), "EXTRA/%s", (char *) data);
			}
			break;
		}

		if (start.backwards) {
			p = p->prev;
			if (!p)
				p = ifend;
		} else {
			p = p->next;
			if (!p)
				p = iflist;
		}
		/* stop when you roll to the one that we started from */
		if (p == exitpvt)
			break;
	}
	ast_mutex_unlock(&iflock);
				
	if (cause && !tmp) {
		if (callwait || channelmatched) {
			*cause = AST_CAUSE_BUSY;
		} else if (groupmatched) {
			*cause = AST_CAUSE_CONGESTION;
		} else {
			/*
			 * We did not match any channel requested.
			 * Dialplan error requesting non-existant channel?
			 */
		}
	}

	return tmp;
}

#ifdef HAVE_GSMAT
static int extra_setlaw(int dfd, int law)
{
	return ioctl(dfd, DAHDI_SETLAW, &law);
}

static void extra_gsm_message(struct gsm_modul *gsm, char *s)
{
	int x;
	int dchan = -1, span = -1;
	int dchancount = 0;

	if (gsm) {
		for (x = 0; x < NUM_SPANS; x++) {
			if (gsms[x].dchan)
				dchancount++;

			if (gsms[x].dchan == gsm)
				dchan = 0;
			
			if (dchan >= 0) {
				span = x;
			}
			dchancount = 0;
		}
		if (dchancount > 1 && (span > -1))
			ast_verbose("[Span %d D-Channel %d]%s", span, dchan, s);
		else
			ast_verbose("%s", s);
	} else
		ast_verbose("%s", s);

	ast_mutex_lock(&gsmdebugfdlock);

	if (gsmdebugfd >= 0) {
		if (write(gsmdebugfd, s, strlen(s)) < 0) {
			ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
		}
	}

	ast_mutex_unlock(&gsmdebugfdlock);
}

static void extra_gsm_error(struct gsm_modul *gsm, char *s)
{
	int x;
	int dchan = -1, span = -1;
	int dchancount = 0;

	if (gsm) {
		for (x = 0; x < NUM_SPANS; x++) {
			if (gsms[x].dchan)
				dchancount++;
			
			if (gsms[x].dchan == gsm)
				dchan = 0;
			
			if (dchan >= 0) 
				span = x;
		
			dchancount = 0;
		}
		if ((dchancount > 1) && (span > -1))
			ast_log(LOG_ERROR, "[Span %d D-Channel %d] GSM: %s", span, dchan, s);
		else
			ast_log(LOG_ERROR, "%s", s);
	} else
		ast_log(LOG_ERROR, "%s", s);

	ast_mutex_lock(&gsmdebugfdlock);

	if (gsmdebugfd >= 0) {
		if (write(gsmdebugfd, s, strlen(s)) < 0) {
			ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
		}
	}

	ast_mutex_unlock(&gsmdebugfdlock);
}

static int gsm_check_restart(struct extra_gsm *gsm)
{
	do {
		gsm->resetpos++;
	} while ((gsm->resetpos < gsm->numchans) &&
		 (!gsm->pvt ||
		  gsm->pvt->gsmcall ||
		  gsm->pvt->resetting));
	if (gsm->resetpos < gsm->numchans) {
		/* Mark the channel as resetting and restart it */
		gsm->pvt->resetting = 1;
		gsm_reset(gsm->gsm, GSM_PVT_TO_CHANNEL(gsm->pvt));
	} else {
		gsm->resetting = 0;
		time(&gsm->lastreset);
	}
	return 0;
}

static int gsm_hangup_all(struct extra_pvt *p, struct extra_gsm *gsm)
{
	int x;
	int redo;
	ast_mutex_unlock(&gsm->lock);
	ast_mutex_lock(&p->lock);
	do {
		redo = 0;
		for (x = 0; x < SUB_SUM; x++) {
			while (p->subs[x].owner && ast_channel_trylock(p->subs[x].owner)) {
				redo++;
				DEADLOCK_AVOIDANCE(&p->lock);
			}
			if (p->subs[x].owner) {
#if (ASTERISK_VERSION_NUM >= 10601)
				ast_queue_hangup_with_cause(p->subs[x].owner, AST_CAUSE_PRE_EMPTED);
#else  //(ASTERISK_VERSION_NUM >= 10601)
				ast_queue_hangup(p->subs[x].owner);
#endif //(ASTERISK_VERSION_NUM >= 10601)
				ast_channel_unlock(p->subs[x].owner);
			}
		}
	} while (redo);
	ast_mutex_unlock(&p->lock);
	ast_mutex_lock(&gsm->lock);
	return 0;
}

#if 0
static struct ast_channel *sms_new(int state,struct extra_pvt *pvt, char *cid_num,
		const struct ast_channel *requestor)
{
	struct ast_channel *chn;
	
#if (ASTERISK_VERSION_NUM > 10444)
	struct ast_str *chan_name;
#else  //(ASTERISK_VERSION_NUM > 10444)
	char *b2;
#endif //(ASTERISK_VERSION_NUM > 10444)

/*	if (pvt->subs[idx].owner) {
		ast_log(LOG_WARNING, "Channel %d already has a %s call\n", pvt->channel,subnames[idx]);
		return NULL;
	}*/

#if (ASTERISK_VERSION_NUM > 10444)
	chan_name = create_channel_name(pvt);

	if (!chan_name) {
		return NULL;
	}
#else  //(ASTERISK_VERSION_NUM > 10444)
	b2 = create_channel_name(pvt);

	if (!b2) {
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)
	
#if (ASTERISK_VERSION_NUM >= 10800)
	chn = ast_channel_alloc(0, state, pvt->cid_num, pvt->cid_name, pvt->accountcode, pvt->exten, pvt->context, NULL, pvt->amaflags, "EXTRA/%s", ast_str_buffer(chan_name));
#else  //(ASTERISK_VERSION_NUM >= 10800)
#if (ASTERISK_VERSION_NUM > 10444)
	chn = ast_channel_alloc(0, state, pvt->cid_num, pvt->cid_name, pvt->accountcode, pvt->exten, pvt->context, pvt->amaflags, "EXTRA/%s", chan_name->str);
#else  //(ASTERISK_VERSION_NUM > 10444)
	chn = ast_channel_alloc(0, state, pvt->cid_num, pvt->cid_name, pvt->accountcode, pvt->exten, pvt->context, pvt->amaflags, "EXTRA/%s", b2);
#endif //(ASTERISK_VERSION_NUM > 10444)
#endif //(ASTERISK_VERSION_NUM >= 10800)

#if (ASTERISK_VERSION_NUM > 10444)
	ast_free(chan_name);
#else  //(ASTERISK_VERSION_NUM > 10444)
	free(b2);
#endif //(ASTERISK_VERSION_NUM > 10444)

/*	chn = ast_channel_alloc(1, state, cid_num, pvt->id, 0, 0, pvt->context,
			requestor ? requestor->linkedid : "", 0,
			"Mobile/%s-%04lx", pvt->id, ast_random() & 0xffff);*/
	if (!chn) {
		goto e_return;
	}

	chn->tech = &extra_tech;
	chn->tech_pvt = pvt;

	pvt->owner = chn;

	return chn;

e_return:
	return NULL;
}
#else
static struct ast_channel *sms_new(int state,struct extra_pvt *pvt, int idx, char *cid_num,
		const struct ast_channel *requestor)
{
	struct ast_channel *chn;
	
#if (ASTERISK_VERSION_NUM > 10444)
	struct ast_str *chan_name;
#else  //(ASTERISK_VERSION_NUM > 10444)
	char *b2;
#endif //(ASTERISK_VERSION_NUM > 10444)

	struct ast_variable *v;

	if (pvt->subs[idx].owner) {
		ast_log(LOG_WARNING, "Channel %d already has a %s call\n", pvt->channel,subnames[idx]);
		return NULL;
	}

#if (ASTERISK_VERSION_NUM > 10444)
	chan_name = create_channel_name(pvt);

	if (!chan_name) {
		return NULL;
	}
#else  //(ASTERISK_VERSION_NUM > 10444)
	b2 = create_channel_name(pvt);

	if (!b2) {
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)
	
#if (ASTERISK_VERSION_NUM >= 10800)
	chn = ast_channel_alloc(0, state, pvt->cid_num, pvt->cid_name, pvt->accountcode, pvt->exten, pvt->context, NULL, pvt->amaflags, "EXTRA-SMS/%s", ast_str_buffer(chan_name));
#else  //(ASTERISK_VERSION_NUM >= 10800)
#if (ASTERISK_VERSION_NUM > 10444)
	chn = ast_channel_alloc(0, state, pvt->cid_num, pvt->cid_name, pvt->accountcode, pvt->exten, pvt->context, pvt->amaflags, "EXTRA-SMS/%s", chan_name->str);
#else  //(ASTERISK_VERSION_NUM > 10444)
	chn = ast_channel_alloc(0, state, pvt->cid_num, pvt->cid_name, pvt->accountcode, pvt->exten, pvt->context, pvt->amaflags, "EXTRA-SMS/%s", b2);
#endif //(ASTERISK_VERSION_NUM > 10444)
#endif //(ASTERISK_VERSION_NUM >= 10800)

#if (ASTERISK_VERSION_NUM > 10444)
	ast_free(chan_name);
#else  //(ASTERISK_VERSION_NUM > 10444)
	free(b2);
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (!chn) {
		goto e_return;
	}
	
#if 0	
#if (ASTERISK_VERSION_NUM >= 10800)
	format_t deflaw;
#else  //(ASTERISK_VERSION_NUM >= 10800)
	int deflaw;
#endif //(ASTERISK_VERSION_NUM >= 10800)
	pvt->law = pvt->law_default;

	if (pvt->law_default == DAHDI_LAW_ALAW) {
		deflaw = AST_FORMAT_ALAW;
	} else {
		deflaw = AST_FORMAT_ULAW;
	}
	

#if (ASTERISK_VERSION_NUM > 10444)
	ast_channel_set_fd(chn, 0, pvt->subs[idx].dfd);
#else  //(ASTERISK_VERSION_NUM > 10444)
	chn->fds[0] = pvt->subs[idx].dfd;
#endif //(ASTERISK_VERSION_NUM > 10444)
	chn->nativeformats = deflaw;
	/* Start out assuming ulaw since it's smaller :) */
	chn->rawreadformat = deflaw;
	chn->readformat = deflaw;
	chn->rawwriteformat = deflaw;
	chn->writeformat = deflaw;
	pvt->subs[idx].linear = 0;
	extra_setlinear(pvt->subs[idx].dfd, pvt->subs[idx].linear);
#endif

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_channel_tech_set(chn, &extra_tech);
	ast_channel_tech_pvt_set(chn, pvt);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	chn->tech = &extra_tech;
	chn->tech_pvt = pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)

	pvt->owner = chn;
	
#if (ASTERISK_VERSION_NUM > 10444)
	ast_channel_set_fd(chn, 0, pvt->subs[idx].dfd);
#else  //(ASTERISK_VERSION_NUM > 10444)
	chn->fds[0] = pvt->subs[idx].dfd;
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (!ast_strlen_zero(pvt->language))
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_language_set(chn, pvt->language);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_string_field_set(chn, language, pvt->language);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	if (!ast_strlen_zero(pvt->accountcode))
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_accountcode_set(chn, pvt->accountcode);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_string_field_set(chn, accountcode, pvt->accountcode);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	pvt->subs[idx].owner = chn;

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_channel_context_set(chn, pvt->context);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	ast_copy_string(chn->context, pvt->context, sizeof(chn->context));
#endif //(ASTERISK_VERSION_NUM >= 110000)

	if (!ast_strlen_zero(pvt->exten))
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_exten_set(chn, pvt->exten);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_copy_string(chn->exten, pvt->exten, sizeof(chn->exten));
#endif //(ASTERISK_VERSION_NUM >= 110000)


#if 0
#if (ASTERISK_VERSION_NUM >= 10601)
	ast_devstate_changed_literal(ast_state_chan2dev(state), chn->name);
#else  //(ASTERISK_VERSION_NUM >= 10601)
	ast_device_state_changed_literal(chn->name);
#endif //(ASTERISK_VERSION_NUM >= 10601)
#endif

	for (v = pvt->vars ; v ; v = v->next)
		pbx_builtin_setvar_helper(chn, v->name, v->value);

	//ast_module_ref(ast_module_info->self);	

	return chn;

e_return:
	return NULL;
}
#endif 

//Freedom add 2012-02-10 14:30
////////////////////////////////////////////////////////////////////////////////
static struct ast_channel *sms_send_new(int state,struct extra_pvt *pvt, int idx, char *cid_num,
		const struct ast_channel *requestor)
{
	struct ast_channel *chn;
	
#if (ASTERISK_VERSION_NUM > 10444)
	struct ast_str *chan_name;
#else  //(ASTERISK_VERSION_NUM > 10444)
	char *b2;
#endif //(ASTERISK_VERSION_NUM > 10444)

	struct ast_variable *v;

	if (pvt->subs[idx].owner) {
		ast_log(LOG_WARNING, "Channel %d already has a %s call\n", pvt->channel,subnames[idx]);
		return NULL;
	}

#if (ASTERISK_VERSION_NUM > 10444)
	chan_name = create_channel_name(pvt);

	if (!chan_name) {
		return NULL;
	}
#else  //(ASTERISK_VERSION_NUM > 10444)
	b2 = create_channel_name(pvt);

	if (!b2) {
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)
	
#if (ASTERISK_VERSION_NUM >= 10800)
	chn = ast_channel_alloc(0, state, NULL, NULL, pvt->accountcode, pvt->exten, pvt->context, NULL, pvt->amaflags, "EXTRA-SMSSEND/%s", ast_str_buffer(chan_name));
#else  //(ASTERISK_VERSION_NUM >= 10800)
#if (ASTERISK_VERSION_NUM > 10444)
	chn = ast_channel_alloc(0, state, NULL, NULL, pvt->accountcode, pvt->exten, pvt->context, pvt->amaflags, "EXTRA-SMSSEND/%s", chan_name->str);
#else  //(ASTERISK_VERSION_NUM > 10444)
	chn = ast_channel_alloc(0, state, NULL, NULL, pvt->accountcode, pvt->exten, pvt->context, pvt->amaflags, "EXTRA-SMSSEND/%s", b2);
#endif //(ASTERISK_VERSION_NUM > 10444)
#endif //(ASTERISK_VERSION_NUM >= 10800)

#if (ASTERISK_VERSION_NUM > 10444)
	ast_free(chan_name);
#else  //(ASTERISK_VERSION_NUM > 10444)
	free(b2);
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (!chn) {
		goto e_return;
	}
	
#if (ASTERISK_VERSION_NUM >= 100000)
	struct ast_format deflaw;
#elif (ASTERISK_VERSION_NUM >= 10800)
	format_t deflaw;
#else  //(ASTERISK_VERSION_NUM >= 10800)
	int deflaw;
#endif //(ASTERISK_VERSION_NUM >= 10800)

	pvt->law = pvt->law_default;

	if (pvt->law_default == DAHDI_LAW_ALAW) {
#if (ASTERISK_VERSION_NUM >= 100000)
		ast_format_set(&deflaw, AST_FORMAT_ALAW, 0);
#else
		deflaw = AST_FORMAT_ALAW;
#endif
	} else {
#if (ASTERISK_VERSION_NUM >= 100000)
		ast_format_set(&deflaw, AST_FORMAT_ULAW, 0);
#else
		deflaw = AST_FORMAT_ULAW;
#endif
	}
	
#if (ASTERISK_VERSION_NUM > 10444)
	ast_channel_set_fd(chn, 0, pvt->subs[idx].dfd);
#else  //(ASTERISK_VERSION_NUM > 10444)
	chn->fds[0] = pvt->subs[idx].dfd;
#endif //(ASTERISK_VERSION_NUM > 10444)

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_format_cap_add(ast_channel_nativeformats(chn), &deflaw);
	/* Start out assuming ulaw since it's smaller :) */
	ast_format_copy(ast_channel_rawreadformat(chn), &deflaw);
	ast_format_copy(ast_channel_readformat(chn), &deflaw);
	ast_format_copy(ast_channel_rawwriteformat(chn), &deflaw);
	ast_format_copy(ast_channel_writeformat(chn), &deflaw);
#elif (ASTERISK_VERSION_NUM >= 100000)
	ast_format_cap_add(chn->nativeformats, &deflaw);
	/* Start out assuming ulaw since it's smaller :) */
	ast_format_copy(&chn->rawreadformat, &deflaw);
	ast_format_copy(&chn->readformat, &deflaw);
	ast_format_copy(&chn->rawwriteformat, &deflaw);
	ast_format_copy(&chn->writeformat, &deflaw);
#else
	chn->nativeformats = deflaw;
	/* Start out assuming ulaw since it's smaller :) */
	chn->rawreadformat = deflaw;
	chn->readformat = deflaw;
	chn->rawwriteformat = deflaw;
	chn->writeformat = deflaw;
#endif

	pvt->subs[idx].linear = 0;
	extra_setlinear(pvt->subs[idx].dfd, pvt->subs[idx].linear);

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_channel_tech_set(chn, &extra_tech);
	ast_channel_tech_pvt_set(chn, pvt);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	chn->tech = &extra_tech;
	chn->tech_pvt = pvt;
#endif //(ASTERISK_VERSION_NUM >= 110000)
	pvt->owner = chn;
	
#if (ASTERISK_VERSION_NUM > 10444)
	ast_channel_set_fd(chn, 0, pvt->subs[idx].dfd);
#else  //(ASTERISK_VERSION_NUM > 10444)
	chn->fds[0] = pvt->subs[idx].dfd;
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (!ast_strlen_zero(pvt->language))
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_language_set(chn, pvt->language);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_string_field_set(chn, language, pvt->language);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	if (!ast_strlen_zero(pvt->accountcode))
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_accountcode_set(chn, pvt->accountcode);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_string_field_set(chn, accountcode, pvt->accountcode);
#endif //(ASTERISK_VERSION_NUM >= 110000)

	pvt->subs[idx].owner = chn;

#if (ASTERISK_VERSION_NUM >= 110000)
	ast_channel_context_set(chn, pvt->context);
#else  //(ASTERISK_VERSION_NUM >= 110000)
	ast_copy_string(chn->context, pvt->context, sizeof(chn->context));
#endif //(ASTERISK_VERSION_NUM >= 110000)

	if (!ast_strlen_zero(pvt->exten))
#if (ASTERISK_VERSION_NUM >= 110000)
		ast_channel_exten_set(chn, pvt->exten);
#else  //(ASTERISK_VERSION_NUM >= 110000)
		ast_copy_string(chn->exten, pvt->exten, sizeof(chn->exten));
#endif //(ASTERISK_VERSION_NUM >= 110000)


#if (ASTERISK_VERSION_NUM >= 110000)
       ast_devstate_changed_literal(ast_state_chan2dev(state), AST_DEVSTATE_NOT_CACHABLE, ast_channel_name(chn));
	//ast_devstate_changed_literal(ast_state_chan2dev(state), ast_channel_name(chn));
#elif (ASTERISK_VERSION_NUM >= 10820)
       ast_devstate_changed_literal(ast_state_chan2dev(state), AST_DEVSTATE_NOT_CACHABLE, chn->name);
#elif (ASTERISK_VERSION_NUM >= 10601)
	ast_devstate_changed_literal(ast_state_chan2dev(state), chn->name);
#else  //(ASTERISK_VERSION_NUM >= 10601)
	ast_device_state_changed_literal(chn->name);
#endif //(ASTERISK_VERSION_NUM >= 10601)

	for (v = pvt->vars ; v ; v = v->next)
		pbx_builtin_setvar_helper(chn, v->name, v->value);

//	ast_module_ref(ast_module_info->self);	

	return chn;

e_return:
	return NULL;
}
////////////////////////////////////////////////////////////////////////////////

#ifdef VIRTUAL_TTY
static int init_virtual_tty(struct extra_gsm *gsm,int enable_tty)
{
	int ret = -1;
	int tty_stat=0;
	int mux_stat=0;
	char mux_command[128];

	ioctl(gsm->fd, OPVXG4XX_GET_TTY_MODULE, &tty_stat);
	//ast_log(LOG_WARNING,"==========tty_stat:%d enable_tty:%d ========\n",tty_stat,enable_tty);
	if(enable_tty){
		if(tty_stat == 1) {
			goto init_virtual_end;
		}

		memset(mux_command,0,sizeof(mux_command));
		ret = gsm_get_mux_command(gsm->gsm,mux_command);
		if(ret != 0) {
			ret = -1;
			goto init_virtual_end;
		}

		if (ioctl(gsm->fd, OPVXG4XX_ENABLE_TTY_MODULE, 0)==0) {
			usleep(10000);
		}

		ret = ioctl(gsm->fd, OPVXG4XX_GET_MUX_STAT, &mux_stat);
		//ast_log(LOG_WARNING,"==========mux_stat:%d========\n",mux_stat);
		if((ret!=0)||(mux_stat==1)) {
			ret = -1;
			goto init_virtual_end;
		}

		if (ioctl(gsm->fd, OPVXG4XX_SET_MUX, mux_command)==0) {
			//ast_log(LOG_WARNING,"==========OPVXG4XX_SET_MUX mux_command:%s========\n",mux_command);
			//usleep(50000);
			sleep(3);
		}

		if (ioctl(gsm->fd, OPVXG4XX_CREATE_CONCOLE, 0)==0) {
			usleep(50000);
		}

		if (ioctl(gsm->fd, OPVXG4XX_CREATE_DAHDI, 0)==0) {
			usleep(50000);
		}

		if (ioctl(gsm->fd, OPVXG4XX_CREATE_EXT, 0)==0) {
			usleep(50000);
		}

		if (ioctl(gsm->fd, OPVXG4XX_CONNECT_DAHDI, 0)==0) {
			usleep(50000);
		}
	} else {
		if(tty_stat == 0) {
			goto init_virtual_end;
		}

		sleep(1);

		if (ioctl(gsm->fd, OPVXG4XX_CLEAR_MUX, 0)==0) {
			sleep(2);
		}
		ioctl(gsm->fd, OPVXG4XX_DISABLE_TTY_MODULE, 0);
	}

	ret = 0;
init_virtual_end:
	gsm_mux_end(gsm->gsm, !ret);

	return ret;
}
#endif

static void *gsm_dchannel(void *vgsm)
{
	struct extra_gsm *gsm = vgsm;
	gsm_event *e;
	struct pollfd fds[1];
	int res;
	int chanpos = 0;
	int x;
	struct ast_channel *c;
	struct timeval tv, lowest, *next;
	struct timeval lastidle = ast_tvnow();
	time_t t;
	FILE *sms_r;
	FILE *sms_w;
	char date[64];	
	pthread_t threadid;
	char* context_name;
	
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	gettimeofday(&lastidle, NULL);

	for (;;) {
		if (!gsm->dchannel)
			break;

		fds[0].fd = gsm->fd;
		fds[0].events = POLLIN | POLLPRI;
		fds[0].revents = 0;

		time(&t);
		ast_mutex_lock(&gsm->lock);

		if (gsm->resetinterval > 0) {
			if (gsm->resetting && gsm_is_up(gsm)) {
				if (gsm->resetpos < 0)
					gsm_check_restart(gsm);
			} else {
				if (!gsm->resetting	&& (t - gsm->lastreset) >= gsm->resetinterval) {
					gsm->resetting = 1;
					gsm->resetpos = -1;
				}
			}
		}
		/* Start with reasonable max */
		lowest = ast_tv(1, 500000);
		if ((next = gsm_schedule_next(gsm->dchan))) {
			/* We need relative time here */
			tv = ast_tvsub(*next, ast_tvnow());
			if (tv.tv_sec < 0) {
				tv = ast_tv(0,0);
			}
			if (gsm->resetting) {
				if (tv.tv_sec > 1) {
					tv = ast_tv(1, 500000);
				}
			} else {
				if (tv.tv_sec > 1) {
					tv = ast_tv(1, 500000);
				}
			}
		} else if (gsm->resetting) {
			/* Make sure we stop at least once per second if we're
			   monitoring idle channels */
			tv = ast_tv(1,0);
		} else {
			/* Don't poll for more than 10 seconds */
			tv = ast_tv(1, 500000);
		}
		if (ast_tvcmp(tv, lowest) < 0) {
			lowest = tv;
		}
		ast_mutex_unlock(&gsm->lock);

		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		pthread_testcancel();
		
		e = NULL;

		res = poll(fds, 1, lowest.tv_sec * 1000 + lowest.tv_usec / 1000);

		pthread_testcancel();
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		
		ast_mutex_lock(&gsm->lock);
	
		if (!res) {
			if(gsm->gsm_init_flag == 0) {
				gsm->gsm_reinit++;
				if(gsm->gsm_reinit%5 == 0) {
					if(gsm->gsm_reinit%30 == 0) {
						gsm->gsm_init_flag = 1;
						gsm->gsm_reinit = 0;
					} else if(gsm->gsm_reinit%15 == 0) {
						ioctl(gsm->fd, OPVXG4XX_SPAN_INIT, 1);
						ast_log(LOG_NOTICE, "GSM reset power of module on D-channel of span %d\n", gsm->span);
					} else {
						ast_log(LOG_NOTICE, "GSM detect module on D-channel of span %d\n", gsm->span);
						gsm_module_start(gsm->dchan);
					}
				}
			}
#ifdef VIRTUAL_TTY
			else if(gsm->gsm->already_set_mux_mode)
				gsm_module_start(gsm->dchan);
#endif
			e = gsm_schedule_run(gsm->dchan);
		} else if (res > -1) {
			if (fds[0].revents & POLLIN) {
				e = gsm_check_event(gsm->dchan);
			} else if (fds[0].revents & POLLPRI) {
				/* Check for an event */
				x = 0;
				res = ioctl(gsm->fd, DAHDI_GETEVENT, &x);
				if (x) {
					ast_log(LOG_NOTICE, "GSM got event: %s (%d) on D-channel of span %d\n", event2str(x), x, gsm->span);
					manager_event(EVENT_FLAG_SYSTEM, "GSMEvent",
						"GSMEvent: %s\r\n"
						"GSMEventCode: %d\r\n"
						"Span: %d\r\n",
						event2str(x),
						x,
						gsm->span
						);
				}
				/* Keep track of alarm state */	
				if (x == DAHDI_EVENT_ALARM) {
					gsm->dchanavail &= ~(DCHAN_NOTINALARM | DCHAN_UP);
				} else if (x == DAHDI_EVENT_NOALARM) {
					gsm->dchanavail |= DCHAN_NOTINALARM;
					gsm_restart(gsm->dchan);
				}
			
				ast_debug(1, "Got event %s (%d) on D-channel for span %d\n", event2str(x), x, gsm->span);
			}
		} else if (errno != EINTR) {
			ast_log(LOG_WARNING, "gsm_event returned error %d (%s)\n", errno, strerror(errno));
		}

		if (e) {
			if (gsm->debug)
				gsm_dump_event(gsm->dchan, e);

			if (GSM_EVENT_DCHAN_UP == e->e) {
				if (!(gsm->dchanavail & DCHAN_UP)) {
					ast_verb(2, "D-Channel on span %d up\n", gsm->span);
				}
				gsm->dchanavail |= DCHAN_UP;
			} else if ((GSM_EVENT_DCHAN_DOWN == e->e)||(GSM_EVENT_NO_SIGNAL== e->e) \
				||(GSM_EVENT_SIM_FAILED== e->e)||(GSM_EVENT_PIN_ERROR== e->e)){
				if (gsm->dchanavail & DCHAN_UP) {
					ast_verb(2, "D-Channel on span %d down\n", gsm->span);
				}
				gsm->dchanavail &= ~DCHAN_UP;
			}

			switch (e->e) {
			case GSM_EVENT_DCHAN_UP:

				/* Note presense of D-channel */
				time(&gsm->lastreset);

				/* Restart in 5 seconds */
				if (gsm->resetinterval > -1) {
					gsm->lastreset -= gsm->resetinterval;
					gsm->lastreset += 5;
				}

				gsm->resetting = 0;
				
				/* Take the channel from inalarm condition */
				if (gsm->pvt) {
					gsm->pvt->inalarm = 0;
				}
				break;
			case GSM_EVENT_DETECT_MODULE_OK:
				gsm->dchanavail |= DCHAN_POWER;
				gsm->gsm_init_flag=1;
				break;
			case GSM_EVENT_DCHAN_DOWN:
				if (!gsm_is_up(gsm)) {
					gsm->resetting = 0;
					/* Hangup active channels and put them in alarm mode */
					struct extra_pvt *p = gsm->pvt;
					if (p) {
						if (!p->gsm || !p->gsm->gsm || gsm_get_timer(p->gsm->gsm, GSM_TIMER_T309) < 0) {
							/* T309 is not enabled : hangup calls when alarm occurs */
							if (p->gsmcall) {
								if (p->gsm && p->gsm->gsm) {
									gsm_hangup(p->gsm->gsm, p->gsmcall, -1);
									gsm_destroycall(p->gsm->gsm, p->gsmcall);
									p->gsmcall = NULL;
								} else
									ast_log(LOG_WARNING, "The GSM Call have not been destroyed\n");
							}
							gsm_hangup_all(p, gsm);

							if (p->owner)
#if (ASTERISK_VERSION_NUM >= 110000)
								ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
								p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
						}
						p->inalarm = 1;
					}
				}
				break;
			case GSM_EVENT_RESTART:
				ast_verb(3, "Restart on requested on span %d\n", gsm->span);
				if (gsm->pvt) {
					ast_mutex_lock(&gsm->pvt->lock);
					if (gsm->pvt->gsmcall) {
						gsm_destroycall(gsm->gsm, gsm->pvt->gsmcall);
						gsm->pvt->gsmcall = NULL;
					}
					gsm_hangup_all(gsm->pvt, gsm);
					if (gsm->pvt->owner)
#if (ASTERISK_VERSION_NUM >= 110000)
						ast_channel_softhangup_internal_flag_add(gsm->pvt->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
						gsm->pvt->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)

					ast_mutex_unlock(&gsm->pvt->lock);
				}
				break;
			case GSM_EVENT_KEYPAD_DIGIT:
				chanpos =  e->digit.channel;
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "KEYPAD_DIGITs received on unconfigured channel %d/%d span %d\n", 
						GSM_SPAN(e->digit.channel), GSM_CHANNEL(e->digit.channel), gsm->span);
				} else {
					ast_mutex_lock(&gsm->pvt->lock);
					/* queue DTMF frame if the PBX for this call was already started (we're forwarding KEYPAD_DIGITs further on */
					if (gsm->pvt->gsmcall==e->digit.call && gsm->pvt->owner) {
						/* how to do that */
						int digitlen = strlen(e->digit.digits);
						char digit;
						int i;					
						for (i = 0; i < digitlen; i++) {	
							digit = e->digit.digits[i];
							{
#if (ASTERISK_VERSION_NUM >= 10800)
								struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = digit, };
#else  //(ASTERISK_VERSION_NUM >= 10800)
								struct ast_frame f = { AST_FRAME_DTMF, digit, };
#endif //(ASTERISK_VERSION_NUM >= 10800)
								extra_queue_frame(gsm->pvt, &f);
							}
						}
					}
					ast_mutex_unlock(&gsm->pvt->lock);
				}
				break;
				
			case GSM_EVENT_INFO_RECEIVED:
				chanpos =  e->ring.channel;
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "INFO received on unconfigured channel %d/%d span %d\n", 
						GSM_SPAN(e->ring.channel), GSM_CHANNEL(e->ring.channel), gsm->span);
					ast_mutex_lock(&gsm->pvt->lock);
					/* queue DTMF frame if the PBX for this call was already started (we're forwarding INFORMATION further on */
					if (gsm->pvt->gsmcall == e->ring.call && gsm->pvt->owner) {
						/* how to do that */
						int digitlen = strlen(e->ring.callednum);
						char digit;
						int i;					
						for (i = 0; i < digitlen; i++) {	
							digit = e->ring.callednum[i];
							{
#if (ASTERISK_VERSION_NUM >= 10800)
								struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = digit, };
#else  //(ASTERISK_VERSION_NUM >= 10800)
								struct ast_frame f = { AST_FRAME_DTMF, digit, };
#endif //(ASTERISK_VERSION_NUM >= 10800)
								extra_queue_frame(gsm->pvt, &f);
							}
						}
					}
					ast_mutex_unlock(&gsm->pvt->lock);
				}
				break;
			case GSM_EVENT_RING:
				if (e->ring.channel == -1) {
					chanpos = gsm_find_empty_chan(gsm, 1); 
				}
				else {
					chanpos = e->ring.channel;
				}
				/* if no channel specified find one empty */
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Ring requested on unconfigured channel %d/%d span %d\n", 
						GSM_SPAN(e->ring.channel), GSM_CHANNEL(e->ring.channel), gsm->span);
				} else {
					ast_mutex_lock(&gsm->pvt->lock);
					if (gsm->pvt->owner) {
						if (gsm->pvt->gsmcall == e->ring.call) {
							ast_log(LOG_WARNING, "Duplicate setup requested on channel %d/%d already in use on span %d\n", 
								GSM_SPAN(e->ring.channel), GSM_CHANNEL(e->ring.channel), gsm->span);
							ast_mutex_unlock(&gsm->pvt->lock);
							break;
						} else {
							/* This is where we handle initial glare */
							ast_debug(1, "Ring requested on channel %d/%d already in use or previously requested on span %d.  Attempting to renegotiate channel.\n", 
							GSM_SPAN(e->ring.channel), GSM_CHANNEL(e->ring.channel), gsm->span);
							ast_mutex_unlock(&gsm->pvt->lock);
							chanpos = -1;
						}
					}
					if (chanpos > -1)
						ast_mutex_unlock(&gsm->pvt->lock);
				}
				
				if ((chanpos < 0) && (e->ring.flexible)) {
					chanpos = gsm_find_empty_chan(gsm, 1);
				}
				
				if (chanpos > -1) {
					ast_mutex_lock(&gsm->pvt->lock);
					gsm->pvt->gsmcall = e->ring.call;
					if (gsm->pvt->use_callerid) {
						ast_copy_string(gsm->pvt->cid_num, e->ring.callingnum, sizeof(gsm->pvt->cid_num));
						ast_copy_string(gsm->pvt->cid_name, e->ring.callingname, sizeof(gsm->pvt->cid_name));
					} else {
						gsm->pvt->cid_num[0] = '\0';
						gsm->pvt->cid_name[0] = '\0';
					}
					
					/* If immediate=yes go to s|1 */
					if (gsm->pvt->immediate) {
						ast_verb(3, "Going to extension s|1 because of immediate=yes\n");
						gsm->pvt->exten[0] = 's';
						gsm->pvt->exten[1] = '\0';
					} else if (!ast_strlen_zero(e->ring.callednum)) { /* Get called number */
						ast_copy_string(gsm->pvt->exten, e->ring.callednum, sizeof(gsm->pvt->exten));
						ast_copy_string(gsm->pvt->dnid, e->ring.callednum, sizeof(gsm->pvt->dnid));
					} else {
						/* Some GSM circuits are set up to send _no_ digits.  Handle them as 's'. */
						gsm->pvt->exten[0] = 's';
						gsm->pvt->exten[1] = '\0';
					}
					/* Set DNID on all incoming calls -- even immediate */
					if (!ast_strlen_zero(e->ring.callednum))
						ast_copy_string(gsm->pvt->dnid, e->ring.callednum, sizeof(gsm->pvt->dnid));
					
					/* No number yet, but received "sending complete"? */
					/* no more digits coming */
					if (e->ring.complete && (ast_strlen_zero(e->ring.callednum))) {
						ast_verb(3, "Going to extension s|1 because of Complete received\n");
						gsm->pvt->exten[0] = 's';
						gsm->pvt->exten[1] = '\0';
					}

					/* Make sure extension exists */
					if ((ast_canmatch_extension(NULL, gsm->pvt->context, gsm->pvt->exten, 1, gsm->pvt->cid_num)) ||
						ast_exists_extension(NULL, gsm->pvt->context, gsm->pvt->exten, 1, gsm->pvt->cid_num)) {
						/* Setup law */
						int law = 1;
						if (ioctl(gsm->pvt->subs[SUB_REAL].dfd, DAHDI_AUDIOMODE, &law) == -1) {
							ast_log(LOG_WARNING, "Unable to set audio mode on channel %d to %d: %s\n", gsm->pvt->channel, law, strerror(errno));
						}

						if (e->ring.layer1 == GSM_LAYER_1_ALAW)
							law = DAHDI_LAW_ALAW;
						else
							law = DAHDI_LAW_MULAW;
						res = extra_setlaw(gsm->pvt->subs[SUB_REAL].dfd, law);
						if (res < 0) {
							ast_log(LOG_WARNING, "Unable to set law on channel %d\n", gsm->pvt->channel);
						}

						res = set_actual_gain(gsm->pvt->subs[SUB_REAL].dfd, gsm->pvt->rxgain, gsm->pvt->txgain, gsm->pvt->rxdrc, gsm->pvt->txdrc, law);
						if (res < 0) {
							ast_log(LOG_WARNING, "Unable to set gains on channel %d\n", gsm->pvt->channel);
						}

						if (e->ring.complete) {
							/* Just announce proceeding */
							gsm->pvt->proceeding = 1;
							gsm_proceeding(gsm->gsm, e->ring.call, GSM_PVT_TO_CHANNEL(gsm->pvt), 0);
						} else {
							gsm_need_more_info(gsm->gsm, e->ring.call, GSM_PVT_TO_CHANNEL(gsm->pvt));
						}
					
						/* Start PBX */
						if (!e->ring.complete
							&& ast_matchmore_extension(NULL, gsm->pvt->context, gsm->pvt->exten, 1, gsm->pvt->cid_num)) {
							/*
							 * Release the GSM lock while we create the channel
							 * so other threads can send D channel messages.
							 * FIXME = TAKE A LOOK if this has sense in gsm environment...
							 */
							ast_mutex_unlock(&gsm->lock);
							c = extra_new(gsm->pvt, AST_STATE_RESERVED, 0, SUB_REAL, law, 0);
							ast_mutex_lock(&gsm->lock);

#if (ASTERISK_VERSION_NUM > 10444)
							if (c && !ast_pthread_create_detached(&threadid, NULL, analog_ss_thread, c)) {
#else  //(ASTERISK_VERSION_NUM > 10444)
							if (c && !ast_pthread_create(&threadid, NULL, analog_ss_thread, c)) {
#endif //(ASTERISK_VERSION_NUM > 10444)
								ast_verb(3, "Accepting overlap call from '%s' to '%s' on channel %d, span %d\n",
									e->ring.callingnum, S_OR(gsm->pvt->exten, "<unspecified>"),
									gsm->pvt->gsmoffset, gsm->span);
							} else {
								ast_log(LOG_WARNING, "Unable to start PBX on channel %d, span %d\n",
									gsm->pvt->gsmoffset, gsm->span);
								if (c)
									ast_hangup(c);
								else {
									gsm_hangup(gsm->gsm, e->ring.call, GSM_CAUSE_SWITCH_CONGESTION);
									gsm->pvt->gsmcall = NULL;
								}
							}
						} else {
							/*
							 * Release the GSM lock while we create the channel
							 * so other threads can send D channel messages.
							 */
							ast_mutex_unlock(&gsm->lock);
							c = extra_new(gsm->pvt, AST_STATE_RING, 0, SUB_REAL, law, 0);
							ast_mutex_lock(&gsm->lock);

							if (c && !ast_pbx_start(c)) {
								ast_verb(3, "Accepting call from '%s' to '%s' on channel %d, span %d\n",
									 e->ring.callingnum, gsm->pvt->exten,
									gsm->pvt->gsmoffset, gsm->span);
								
								extra_enable_ec(gsm->pvt);
							} else {
								ast_log(LOG_WARNING, "Unable to start PBX on channel %d, span %d\n",
									gsm->pvt->gsmoffset, gsm->span);
								if (c) {
									ast_hangup(c);
								} else {
									gsm_hangup(gsm->gsm, e->ring.call, GSM_CAUSE_SWITCH_CONGESTION);
									gsm->pvt->gsmcall = NULL;
								}
							}
						}
					} else {
						ast_verb(3, "Extension '%s' in context '%s' from '%s' does not exist.  Rejecting call on channel %d, span %d\n",
							gsm->pvt->exten, gsm->pvt->context, gsm->pvt->cid_num,
							gsm->pvt->gsmoffset, gsm->span);
						gsm_hangup(gsm->gsm, e->ring.call, GSM_CAUSE_UNALLOCATED);
						gsm->pvt->gsmcall = NULL;
						gsm->pvt->exten[0] = '\0';
					}
					ast_mutex_unlock(&gsm->pvt->lock);
				} else {
					if (e->ring.flexible) {
						gsm_hangup(gsm->gsm, e->ring.call, GSM_CAUSE_NORMAL_CIRCUIT_CONGESTION);
					} else {
						gsm_hangup(gsm->gsm, e->ring.call, GSM_CAUSE_REQUESTED_CHAN_UNAVAIL);
					}
				}
				break;
			case GSM_EVENT_RINGING:
				chanpos = e->ringing.channel;
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Ringing requested on unconfigured channel %d/%d span %d\n", 
						GSM_SPAN(e->ringing.channel), GSM_CHANNEL(e->ringing.channel), gsm->span);
				} else {
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Ringing requested on channel %d/%d not in use on span %d\n", 
							GSM_SPAN(e->ringing.channel), GSM_CHANNEL(e->ringing.channel), gsm->span);
					} else {
						ast_mutex_lock(&gsm->pvt->lock);
						if (ast_strlen_zero(gsm->pvt->dop.dialstr)) {
							extra_enable_ec(gsm->pvt);
							gsm->pvt->alerting = 1;
						} else
							ast_debug(1, "Deferring ringing notification because of extra digits to dial...\n");

						if (e->ringing.progress == 8) {
							/* Now we can do call progress detection */
							if (gsm->pvt->dsp && gsm->pvt->dsp_features) {
								/* RINGING detection isn't required because we got ALERTING signal */
								ast_dsp_set_features(gsm->pvt->dsp, gsm->pvt->dsp_features & ~DSP_PROGRESS_RINGING);
								gsm->pvt->dsp_features = 0;
							}
						}

						ast_mutex_unlock(&gsm->pvt->lock);
					}
				}
				break;
			case GSM_EVENT_PROGRESS:
				/* Get chan value if e->e is not GSM_EVNT_RINGING */
				chanpos = e->proceeding.channel;
				if (chanpos > -1) {
					if ((!gsm->pvt->progress) || (e->proceeding.progress == 8)) {
#if (ASTERISK_VERSION_NUM >= 10800)
						struct ast_frame f = { AST_FRAME_CONTROL, .subclass.integer = AST_CONTROL_PROGRESS, };
#else //(ASTERISK_VERSION_NUM >= 10800)
						struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_PROGRESS, };
#endif //(ASTERISK_VERSION_NUM >= 10800)

						if (e->proceeding.cause > -1) {
							ast_verb(3, "PROGRESS with cause code %d received\n", e->proceeding.cause);			

							/* Work around broken, out of spec USER_BUSY cause in a progress message */
							if (e->proceeding.cause == AST_CAUSE_USER_BUSY) {
								if (gsm->pvt->owner) {
									ast_verb(3, "PROGRESS with 'user busy' received, signalling AST_CONTROL_BUSY instead of AST_CONTROL_PROGRESS\n");

#if (ASTERISK_VERSION_NUM >= 110000)
									ast_channel_hangupcause_set(gsm->pvt->owner, e->proceeding.cause);
#else  //(ASTERISK_VERSION_NUM >= 110000)
									gsm->pvt->owner->hangupcause = e->proceeding.cause;
#endif //(ASTERISK_VERSION_NUM >= 110000)

#if (ASTERISK_VERSION_NUM >= 10800)
									f.subclass.integer = AST_CONTROL_BUSY;
#else  //(ASTERISK_VERSION_NUM >= 10800)
									f.subclass = AST_CONTROL_BUSY;
#endif //(ASTERISK_VERSION_NUM >= 10800)
								}
							}
						}
						
						ast_mutex_lock(&gsm->pvt->lock);
						ast_debug(1, "Queuing frame from GSM_EVENT_PROGRESS on channel %d span %d\n",
							gsm->pvt->gsmoffset,gsm->span);
						extra_queue_frame(gsm->pvt, &f);
						if (e->proceeding.progress == 8) {
							/* Now we can do call progress detection */
							if (gsm->pvt->dsp && gsm->pvt->dsp_features) {
								ast_dsp_set_features(gsm->pvt->dsp, gsm->pvt->dsp_features);
								gsm->pvt->dsp_features = 0;
							}
							/* Bring voice path up */
#if (ASTERISK_VERSION_NUM >= 10800)
							f.subclass.integer = AST_CONTROL_PROGRESS;
#else  //(ASTERISK_VERSION_NUM >= 10800)
							f.subclass = AST_CONTROL_PROGRESS;
#endif //(ASTERISK_VERSION_NUM >= 10800)
							extra_queue_frame(gsm->pvt, &f);
						}
						gsm->pvt->progress = 1;
						gsm->pvt->dialing = 0;
						ast_mutex_unlock(&gsm->pvt->lock);
					}
				}
				break;
			case GSM_EVENT_PROCEEDING:
				chanpos = e->proceeding.channel;
				if (chanpos > -1) {
					if (!gsm->pvt->proceeding) {
#if (ASTERISK_VERSION_NUM >= 10800)
						struct ast_frame f = { AST_FRAME_CONTROL, .subclass.integer = AST_CONTROL_PROCEEDING, };
#else  //(ASTERISK_VERSION_NUM >= 10800)
						struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_PROCEEDING, };
#endif //(ASTERISK_VERSION_NUM >= 10800)
						
						ast_mutex_lock(&gsm->pvt->lock);
						ast_debug(1, "Queuing frame from GSM_EVENT_PROCEEDING on channel %d span %d\n",
							gsm->pvt->gsmoffset,gsm->span);
						extra_queue_frame(gsm->pvt, &f);
						if (e->proceeding.progress == 8) {
							/* Now we can do call progress detection */
							if (gsm->pvt->dsp && gsm->pvt->dsp_features) {
								ast_dsp_set_features(gsm->pvt->dsp, gsm->pvt->dsp_features);
								gsm->pvt->dsp_features = 0;
							}
							/* Bring voice path up */
#if (ASTERISK_VERSION_NUM >= 10800)
							f.subclass.integer = AST_CONTROL_PROGRESS;
#else //(ASTERISK_VERSION_NUM >= 10800)
							f.subclass = AST_CONTROL_PROGRESS;
#endif //(ASTERISK_VERSION_NUM >= 10800)
							extra_queue_frame(gsm->pvt, &f);
						}
						gsm->pvt->proceeding = 1;
						gsm->pvt->dialing = 0;
						ast_mutex_unlock(&gsm->pvt->lock);
					}
				}
				break;
			case GSM_EVENT_FACNAME:
				chanpos =  e->facname.channel;
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Facility Name requested on unconfigured channel %d/%d span %d\n", 
						GSM_SPAN(e->facname.channel), GSM_CHANNEL(e->facname.channel), gsm->span);
				} else {
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Facility Name requested on channel %d/%d not in use on span %d\n", 
							GSM_SPAN(e->facname.channel), GSM_CHANNEL(e->facname.channel), gsm->span);
					} else {
						/* Re-use *69 field for GSM */
						ast_mutex_lock(&gsm->pvt->lock);
						extra_enable_ec(gsm->pvt);
						ast_mutex_unlock(&gsm->pvt->lock);
					}
				}
				break;				
			case GSM_EVENT_ANSWER:
				chanpos = e->answer.channel;
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Answer on unconfigured channel %d/%d span %d\n", 
						GSM_SPAN(e->answer.channel), GSM_CHANNEL(e->answer.channel), gsm->span);
				} else {
					if (chanpos < 0) {
						ast_log(LOG_WARNING, "Answer requested on channel %d/%d not in use on span %d\n", 
							GSM_SPAN(e->answer.channel), GSM_CHANNEL(e->answer.channel), gsm->span);
					} else {
						ast_mutex_lock(&gsm->pvt->lock);
						/* Now we can do call progress detection */

						/* We changed this so it turns on the DSP no matter what... progress or no progress.
						 * By this time, we need DTMF detection and other features that were previously disabled
						 * -- Matt F */
						if (gsm->pvt->dsp && gsm->pvt->dsp_features) {
							ast_dsp_set_features(gsm->pvt->dsp, gsm->pvt->dsp_features);
							gsm->pvt->dsp_features = 0;
						}
						if (!ast_strlen_zero(gsm->pvt->dop.dialstr)) {
							gsm->pvt->dialing = 1;
							/* Send any "w" waited stuff */
							res = ioctl(gsm->pvt->subs[SUB_REAL].dfd, DAHDI_DIAL, &gsm->pvt->dop);
							if (res < 0) {
								ast_log(LOG_WARNING, "Unable to initiate dialing on trunk channel %d: %s\n", gsm->pvt->channel, strerror(errno));
								gsm->pvt->dop.dialstr[0] = '\0';
							} else
								ast_debug(1, "Sent deferred digit string: %s\n", gsm->pvt->dop.dialstr);

							gsm->pvt->dop.dialstr[0] = '\0';
						} else if (gsm->pvt->confirmanswer) {
							ast_debug(1, "Waiting on answer confirmation on channel %d!\n", gsm->pvt->channel);
						} else {
							gsm->pvt->dialing = 0;
							gsm->pvt->subs[SUB_REAL].needanswer =1;
							/* Enable echo cancellation if it's not on already */
							extra_enable_ec(gsm->pvt);
						}

						ast_mutex_unlock(&gsm->pvt->lock);
					}
				}
				break;
			case GSM_EVENT_SIM_FAILED:
				gsm->dchanavail &= ~DCHAN_UP;
				gsm->dchanavail |= DCHAN_NO_SIM;
				if (!gsm_is_up(gsm)) {
					gsm->resetting = 0;
					/* Hangup active channels and put them in alarm mode */
					struct extra_pvt *p = gsm->pvt;
					if (p) {
						if (!p->gsm || !p->gsm->gsm || gsm_get_timer(p->gsm->gsm, GSM_TIMER_T309) < 0) {
							/* T309 is not enabled : hangup calls when alarm occurs */
							if (p->gsmcall) {
								if (p->gsm && p->gsm->gsm) {
									gsm_hangup(p->gsm->gsm, p->gsmcall, -1);
									gsm_destroycall(p->gsm->gsm, p->gsmcall);
									p->gsmcall = NULL;
								} else
									ast_log(LOG_WARNING, "The GSM Call have not been destroyed\n");
							}
							gsm_hangup_all(p, gsm);

							if (p->owner)
#if (ASTERISK_VERSION_NUM >= 110000)
								ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
								p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
						}
						p->inalarm = 1;
					}
				}
				break;
#ifdef AUTO_SIM_CHECK
			case GSM_EVENT_SIM_INSERTED:
				gsm_module_start(gsm->dchan);
				break;
#endif
			case GSM_EVENT_PIN_REQUIRED:
				gsm_send_pin(gsm->gsm, gsm->pin);
				break;    
			case GSM_EVENT_PIN_ERROR:
				gsm->dchanavail &= ~DCHAN_UP;
				gsm->dchanavail |= DCHAN_PIN_ERROR;
				if (!gsm_is_up(gsm)) {
					gsm->resetting = 0;
					/* Hangup active channels and put them in alarm mode */
					struct extra_pvt *p = gsm->pvt;
					if (p) {
						if (!p->gsm || !p->gsm->gsm || gsm_get_timer(p->gsm->gsm, GSM_TIMER_T309) < 0) {
							/* T309 is not enabled : hangup calls when alarm occurs */
							if (p->gsmcall) {
								if (p->gsm && p->gsm->gsm) {
									gsm_hangup(p->gsm->gsm, p->gsmcall, -1);
									gsm_destroycall(p->gsm->gsm, p->gsmcall);
									p->gsmcall = NULL;
								} else
									ast_log(LOG_WARNING, "The GSM Call have not been destroyed\n");
							}
							gsm_hangup_all(p, gsm);

							if (p->owner)
#if (ASTERISK_VERSION_NUM >= 110000)
								ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
								p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
						}
						p->inalarm = 1;
					}
				}
				break;
			case GSM_EVENT_NO_SIGNAL:
				gsm->dchanavail &= ~DCHAN_UP;
				gsm->dchanavail |= DCHAN_NO_SIGNAL;
				if (!gsm_is_up(gsm)) {
					gsm->resetting = 0;
					/* Hangup active channels and put them in alarm mode */
					struct extra_pvt *p = gsm->pvt;
					if (p) {
						if (!p->gsm || !p->gsm->gsm || gsm_get_timer(p->gsm->gsm, GSM_TIMER_T309) < 0) {
							/* T309 is not enabled : hangup calls when alarm occurs */
							if (p->gsmcall) {
								if (p->gsm && p->gsm->gsm) {
									gsm_hangup(p->gsm->gsm, p->gsmcall, -1);
									gsm_destroycall(p->gsm->gsm, p->gsmcall);
									p->gsmcall = NULL;
								} else
									ast_log(LOG_WARNING, "The GSM Call have not been destroyed\n");
							}
							gsm_hangup_all(p, gsm);

							if (p->owner)
#if (ASTERISK_VERSION_NUM >= 110000)
								ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
								p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
						}
						p->inalarm = 1;
					}
				}
				break;
			case GSM_EVENT_SMS_RECEIVED:

#ifdef SAVE_SMS_TO_FILE
				/* save sms */
				if ((sms_r = fopen("/var/log/asterisk/sms/receive_sms", "r")) == NULL) {
					int status = mkdir("/var/log/asterisk/sms",0774);
					if ( -1 == status ) {
						ast_log(LOG_ERROR, "Unable to create file : /var/log/asterisk/sms/receive_sms \n");
						break;
					}
				} else {
					fclose(sms_r);
				}

				if(( sms_w = fopen("/var/log/asterisk/sms/receive_sms", "at")) == NULL) {
					ast_log(LOG_ERROR, "Cannot open file : /var/log/asterisk/sms/receive_sms \n");
					break;
				}
                
				strftime( date, sizeof(date), "%F %T", localtime(&t) );
				fprintf( sms_w, "-START-\n"
								"Time: %s\n"
								"Span: %d\n"
								"Mode: %s\n"
								"Sender: %s\n"
								"SMSC: %s\n"
								"Length: %d\n"
								"Text: %s\n"
								"PDU: %s\n"
								"-END-\n\n",
							date,
							gsm->span,
							(e->sms_received.mode == SMS_PDU) ? "PDU" : "TEXT",
							e->sms_received.sender,
							e->sms_received.smsc,
							e->sms_received.len,
							e->sms_received.text,
							e->sms_received.pdu);
				fclose(sms_w);
				/* end saving sms */
#endif //SAVE_SMS_TO_FILE

				////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				context_name = "sms";
				if (ast_exists_extension(NULL, gsm->pvt->context, context_name, 1, NULL)) {
					char sms_text[1024];
					ast_copy_string(gsm->pvt->cid_num,e->sms_received.sender,sizeof(gsm->pvt->cid_num));
					ast_copy_string(gsm->pvt->cid_name,e->sms_received.sender,sizeof(gsm->pvt->cid_name));

					if (!(c = sms_new(AST_STATE_DOWN, gsm->pvt, SUB_SMS,NULL, NULL))) {
						ast_debug(1, "[%s] error creating %s message channel, disconnecting\n", gsm->pvt->accountcode,context_name);
						break;
					}
			
#if (ASTERISK_VERSION_NUM >= 110000)
					ast_channel_exten_set(c, context_name);
#else  //(ASTERISK_VERSION_NUM >= 110000)
					strcpy(c->exten, context_name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

					pbx_builtin_setvar_helper(c, "SMSSRC", e->sms_received.sender);
					memset(sms_text,0,sizeof(sms_text));
					
#ifdef EXTRA_SMSTXT
					{
						char sms_tmp[1024];
						int i,j=0;
						for(i=0; i<e->sms_received.len; i++) {
							if(e->sms_received.text[i] == '"') {
								sms_tmp[j++]='\\';
								sms_tmp[j++]='\\';
								sms_tmp[j++]='\\';
								sms_tmp[j++]='"';
							} else if(e->sms_received.text[i] == '\\') {
							   sms_tmp[j++]='\\';
							   sms_tmp[j++]='\\';
							   sms_tmp[j++]='\\';
							   sms_tmp[j++]='\\';
							} else {
								sms_tmp[j++]=e->sms_received.text[i];
							}
						}
						memcpy(sms_text,sms_tmp,j);
					}
#else
					memcpy(sms_text,e->sms_received.text,e->sms_received.len);
#endif
					//pbx_builtin_setvar_helper(c, "SMSTXT", e->sms_received.text);
					pbx_builtin_setvar_helper(c, "SMSTXT", sms_text);
					pbx_builtin_setvar_helper(c, "SMSPDU", e->sms_received.pdu);
					pbx_builtin_setvar_helper(c, "SMSTIME", e->sms_received.time);
					pbx_builtin_setvar_helper(c, "SMSTZ", e->sms_received.tz);
					pbx_builtin_setvar_helper(c, "DIALSTATUS", "SMS_END");


#if (ASTERISK_VERSION_NUM >= 10601)
					struct ast_pbx_args args;
					memset(&args, 0, sizeof(args));
					args.no_hangup_chan = 1;
					if (ast_pbx_run_args(c, &args) /*ast_pbx_start(c)*/) {
#else  //(ASTERISK_VERSION_NUM >= 10601)
					//This code is need change.
					//Asterisk-1.6.0.5 not define function :ast_pbx_run_args".
					//But Asterisk-1.6.0.5 and Asterisk-1.6.0.6 ....... use same 10600 of "ASTERISK_VERSION_NUM". 
					//Freedom 2012-09-12 09:55
					if (ast_pbx_run(c)) {
#endif //(ASTERISK_VERSION_NUM >= 10601)
						ast_log(LOG_ERROR, "[%s] unable to start pbx on %s\n", gsm->pvt->accountcode,context_name);
						if(gsm->pvt->owner)
							ast_hangup(gsm->pvt->owner);
					} else {
						ast_hangup(c);
					}
				} 
				///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
				break;

			//Freedom add 2012-02-10 14:30
			////////////////////////////////////////////////////////////////////////////////
			case GSM_EVENT_SMS_SEND_OK:
			case GSM_EVENT_SMS_SEND_FAILED:
				if(GSM_EVENT_SMS_SEND_OK == e->e) 
					context_name = "sms_send_ok";
				else 
					context_name = "sms_send_failed";

				if (ast_exists_extension(NULL, gsm->pvt->context, context_name, 1, NULL)) {
					if (!(c = sms_send_new(AST_STATE_DOWN, gsm->pvt, SUB_SMSSEND,NULL, NULL))) {
						ast_debug(1, "[%s] error creating %s message channel, disconnecting\n",gsm->pvt->accountcode,context_name);
						if(gsm->gsm->sms_info) {
							free(gsm->gsm->sms_info);
							gsm->gsm->sms_info = NULL;
						}
						break;
					}
					
#if (ASTERISK_VERSION_NUM >= 110000)
					ast_channel_exten_set(c, context_name);
#else  //(ASTERISK_VERSION_NUM >= 110000)
					strcpy(c->exten, context_name);
#endif //(ASTERISK_VERSION_NUM >= 110000)
					
					if(gsm->dchan->sms_info) {
						if(gsm->gsm->sms_mod_flag == SMS_TEXT) {
							pbx_builtin_setvar_helper(c, "SMS_SEND_TYPE","text");
							pbx_builtin_setvar_helper(c, "SMS_SEND_SENDER",gsm->gsm->sms_info->txt_info.destination);
							pbx_builtin_setvar_helper(c, "SMS_SEND_TXT",gsm->gsm->sms_info->txt_info.message);
							pbx_builtin_setvar_helper(c, "SMS_SEND_PDU","");
							pbx_builtin_setvar_helper(c, "SMS_SEND_ID",gsm->gsm->sms_info->txt_info.id);
						} else {
							pbx_builtin_setvar_helper(c, "SMS_SEND_TYPE","pdu");
							pbx_builtin_setvar_helper(c, "SMS_SEND_SENDER",gsm->gsm->sms_info->pdu_info.destination);
							pbx_builtin_setvar_helper(c, "SMS_SEND_TXT",gsm->gsm->sms_info->pdu_info.text);
							pbx_builtin_setvar_helper(c, "SMS_SEND_PDU",gsm->gsm->sms_info->pdu_info.message);
							pbx_builtin_setvar_helper(c, "SMS_SEND_ID",gsm->gsm->sms_info->pdu_info.id);
						}
					}

					pbx_builtin_setvar_helper(c, "DIALSTATUS", "SMS_SEND_END");
					

#if (ASTERISK_VERSION_NUM >= 10601)
					struct ast_pbx_args args;
					memset(&args, 0, sizeof(args));
					args.no_hangup_chan = 1;
					if (ast_pbx_run_args(c, &args) /*ast_pbx_start(c)*/) {
#else  //(ASTERISK_VERSION_NUM >= 10601)
					//This code is need change.
					//Asterisk-1.6.0.5 not define function :ast_pbx_run_args".
					//But Asterisk-1.6.0.5 and Asterisk-1.6.0.6 ....... use same 10600 of "ASTERISK_VERSION_NUM". 
					//Freedom 2012-09-12 09:55
					if (ast_pbx_run(c)) {
#endif //(ASTERISK_VERSION_NUM >= 10601)
						ast_log(LOG_ERROR, "[%s] unable to start pbx on %s\n", gsm->pvt->accountcode,context_name);
						if(gsm->pvt->owner)
							ast_hangup(gsm->pvt->owner);
					} else {
						ast_hangup(c);
					}
				}

				if(gsm->gsm->sms_info) {
					free(gsm->gsm->sms_info);
					gsm->gsm->sms_info = NULL;
				}
				break;
			///////////////////////////////////////////////////////////////////////////////
			case GSM_EVENT_USSD_RECEIVED:
				/*ast_log(LOG_WARNING, "Revice USSD stat '%d' \n\t\tcode '%d' \n\t\ttext '%s'\n", \
							e->ussd_received.ussd_stat, \
							e->ussd_received.ussd_coding, \
							e->ussd_received.text);*/
				memset(&gsm->ussd_received,0,sizeof(ussd_recv_t));
				gsm->ussd_received.return_flag=1;
				gsm->ussd_received.ussd_stat=e->ussd_received.ussd_stat;
				gsm->ussd_received.ussd_coding=e->ussd_received.ussd_coding;
				memcpy(gsm->ussd_received.text,e->ussd_received.text,sizeof(gsm->ussd_received.text));
				gsm->ussd_received.len=e->ussd_received.len;
				ast_cond_signal(&gsm->ussd_cond);
				break;
			case GSM_EVENT_USSD_SEND_FAILED:
				memset(&gsm->ussd_received,0,sizeof(ussd_recv_t));
				gsm->ussd_received.return_flag=0;
				ast_cond_signal(&gsm->ussd_cond);
				break;
			case GSM_EVENT_HANGUP:
				chanpos =  e->hangup.channel;
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Hangup requested on unconfigured channel %d/%d span %d\n", 
						GSM_SPAN(e->hangup.channel), GSM_CHANNEL(e->hangup.channel), gsm->span);
				} else {
					if (chanpos > -1) {
						//Freedom Add 2011-11-01 09:33
						///////////////////////////////////////////
						ast_mutex_lock(&gsm->pvt->lock);	
						///////////////////////////////////////////
						if (!gsm->pvt->alreadyhungup) {
							/* we're calling here extra_hangup so once we get there we need to clear p->call after calling gsm_hangup */
							gsm_hangup_all(gsm->pvt ,gsm);
							gsm->pvt->alreadyhungup = 1;

							if (gsm->pvt->owner) {
								/* Queue a BUSY instead of a hangup if our cause is appropriate */

#if (ASTERISK_VERSION_NUM >= 110000)
								ast_channel_hangupcause_set(gsm->pvt->owner, e->hangup.cause);
								switch (ast_channel_state(gsm->pvt->owner)) {
#else  //(ASTERISK_VERSION_NUM >= 110000)
								gsm->pvt->owner->hangupcause = e->hangup.cause;
								switch (gsm->pvt->owner->_state) {
#endif //(ASTERISK_VERSION_NUM >= 110000)
								case AST_STATE_BUSY:
								case AST_STATE_UP:
#if (ASTERISK_VERSION_NUM >= 110000)
									ast_channel_softhangup_internal_flag_add(gsm->pvt->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
									gsm->pvt->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
									break;
								default:
									switch (e->hangup.cause) {
									case GSM_CAUSE_USER_BUSY:
										gsm->pvt->subs[SUB_REAL].needbusy =1;
										break;
									case GSM_CAUSE_CALL_REJECTED:
									case GSM_CAUSE_NETWORK_OUT_OF_ORDER:
									case GSM_CAUSE_NORMAL_CIRCUIT_CONGESTION:
									case GSM_CAUSE_SWITCH_CONGESTION:
									case GSM_CAUSE_DESTINATION_OUT_OF_ORDER:
									case GSM_CAUSE_NORMAL_TEMPORARY_FAILURE:
										gsm->pvt->subs[SUB_REAL].needcongestion =1;
										break;
									default:
#if (ASTERISK_VERSION_NUM >= 110000)
										ast_channel_softhangup_internal_flag_add(gsm->pvt->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
										gsm->pvt->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
									}
									break;
								}

							}
							ast_verb(3, "Channel %d, span %d got hangup, cause %d\n",
								gsm->pvt->gsmoffset, gsm->span, e->hangup.cause);
							gsm->pvt->gsmcall = NULL;
							gsm->pvt->resetting = 0;
						} else {
							gsm_hangup(gsm->gsm, gsm->pvt->gsmcall, e->hangup.cause);
							gsm->pvt->gsmcall = NULL;
						}
						if (e->hangup.cause == GSM_CAUSE_REQUESTED_CHAN_UNAVAIL) {
							ast_verb(3, "Forcing restart of channel %d/%d on span %d since channel reported in use\n",
								GSM_SPAN(e->hangup.channel), GSM_CHANNEL(e->hangup.channel), gsm->span);
							gsm_reset(gsm->gsm, GSM_PVT_TO_CHANNEL(gsm->pvt));
							gsm->pvt->resetting = 1;
						}
						if (e->hangup.aoc_units > -1)
							ast_verb(3, "Channel %d, span %d received AOC-E charging %d unit%s\n",
								gsm->pvt->gsmoffset, gsm->span, (int)e->hangup.aoc_units, (e->hangup.aoc_units == 1) ? "" : "s");

						ast_mutex_unlock(&gsm->pvt->lock);
					} else {
						ast_log(LOG_NOTICE, "===Hangup 014\n");
						ast_log(LOG_WARNING, "Hangup on bad channel %d/%d on span %d\n", 
							GSM_SPAN(e->hangup.channel), GSM_CHANNEL(e->hangup.channel), gsm->span);
					}
				} 
				break;
			case GSM_EVENT_HANGUP_REQ:
				chanpos = e->hangup.channel;
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Hangup REQ requested on unconfigured channel %d/%d span %d\n", 
						GSM_SPAN(e->hangup.channel), GSM_CHANNEL(e->hangup.channel), gsm->span);
				} else {
					if (chanpos > -1) {
						ast_mutex_lock(&gsm->pvt->lock);
						gsm_hangup_all(gsm->pvt, gsm);
						if (gsm->pvt->owner) {
#if (ASTERISK_VERSION_NUM >= 110000)
							ast_channel_hangupcause_set(gsm->pvt->owner, e->hangup.cause);
							switch (ast_channel_state(gsm->pvt->owner)) {
#else  //(ASTERISK_VERSION_NUM >= 110000)
							gsm->pvt->owner->hangupcause = e->hangup.cause;
							switch (gsm->pvt->owner->_state) {
#endif //(ASTERISK_VERSION_NUM >= 110000)
							case AST_STATE_BUSY:
							case AST_STATE_UP:
#if (ASTERISK_VERSION_NUM >= 110000)
								ast_channel_softhangup_internal_flag_add(gsm->pvt->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
								gsm->pvt->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
								break;
							default:
								switch (e->hangup.cause) {
									case GSM_CAUSE_USER_BUSY:
										gsm->pvt->subs[SUB_REAL].needbusy =1;
										break;
									case GSM_CAUSE_CALL_REJECTED:
									case GSM_CAUSE_NETWORK_OUT_OF_ORDER:
									case GSM_CAUSE_NORMAL_CIRCUIT_CONGESTION:
									case GSM_CAUSE_SWITCH_CONGESTION:
									case GSM_CAUSE_DESTINATION_OUT_OF_ORDER:
									case GSM_CAUSE_NORMAL_TEMPORARY_FAILURE:
										gsm->pvt->subs[SUB_REAL].needcongestion =1;
										break;
									default:
#if (ASTERISK_VERSION_NUM >= 110000)
										ast_channel_softhangup_internal_flag_add(gsm->pvt->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
										gsm->pvt->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
								}
								break;
							}
							ast_verb(3, "Channel %d/%d, span %d got hangup request, cause %d\n", GSM_SPAN(e->hangup.channel), GSM_CHANNEL(e->hangup.channel), gsm->span, e->hangup.cause);
							if (e->hangup.aoc_units > -1)
								ast_verb(3, "Channel %d, span %d received AOC-E charging %d unit%s\n",
										gsm->pvt->gsmoffset, gsm->span, (int)e->hangup.aoc_units, (e->hangup.aoc_units == 1) ? "" : "s");
						} else {
							gsm_hangup(gsm->gsm, gsm->pvt->gsmcall, e->hangup.cause);
							gsm->pvt->gsmcall = NULL;
						}
						if (e->hangup.cause == GSM_CAUSE_REQUESTED_CHAN_UNAVAIL) {
							ast_verb(3, "Forcing restart of channel %d/%d span %d since channel reported in use\n",
									GSM_SPAN(e->hangup.channel), GSM_CHANNEL(e->hangup.channel), gsm->span);
							gsm_reset(gsm->gsm, GSM_PVT_TO_CHANNEL(gsm->pvt));
							gsm->pvt->resetting = 1;
						}

						ast_mutex_unlock(&gsm->pvt->lock);
					} else {
						ast_log(LOG_WARNING, "Hangup REQ on bad channel %d/%d on span %d\n", GSM_SPAN(e->hangup.channel), GSM_CHANNEL(e->hangup.channel), gsm->span);
					}
				} 
				break;
			case GSM_EVENT_HANGUP_ACK:
				chanpos =  e->hangup.channel;
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Hangup ACK requested on unconfigured channel number %d/%d span %d\n", 
						GSM_SPAN(e->hangup.channel), GSM_CHANNEL(e->hangup.channel), gsm->span);
				} else {
					if (chanpos > -1) {
						ast_mutex_lock(&gsm->pvt->lock);
						gsm->pvt->gsmcall = NULL;
						gsm->pvt->resetting = 0;
						if (gsm->pvt->owner) {
							ast_verb(3, "Channel %d/%d, span %d got hangup ACK\n", GSM_SPAN(e->hangup.channel), GSM_CHANNEL(e->hangup.channel), gsm->span);
						}
						ast_mutex_unlock(&gsm->pvt->lock);
					}
				}
				break;
			case GSM_EVENT_CONFIG_ERR:
				ast_log(LOG_WARNING, "GSM Error on span %s\n", e->err.err);
				break;
			case GSM_EVENT_RESTART_ACK:
				if (gsm->pvt) {
					ast_mutex_lock(&gsm->pvt->lock);
					gsm_hangup_all(gsm->pvt, gsm);
					if (gsm->pvt->owner) {
						ast_log(LOG_WARNING, "Got restart ack on channel %d/%d span %d with owner\n",
							GSM_SPAN(e->restartack.channel), GSM_CHANNEL(e->restartack.channel), gsm->span);
#if (ASTERISK_VERSION_NUM >= 110000)
						ast_channel_softhangup_internal_flag_add(gsm->pvt->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
						gsm->pvt->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
					}
					gsm->pvt->resetting = 0;
					gsm->pvt->inservice = 1;
					ast_verb(3, "B-channel %d successfully restarted on span %d\n",
						gsm->pvt->gsmoffset, gsm->span);
					ast_mutex_unlock(&gsm->pvt->lock);
					if (gsm->resetting)
						gsm_check_restart(gsm);
				}
				break;
			case GSM_EVENT_SETUP_ACK:
				ast_mutex_lock(&gsm->pvt->lock);
				gsm->pvt->setup_ack = 1;
				/* Send any queued digits */
				for (x = 0;x < strlen(gsm->pvt->dialdest); x++) {
					ast_debug(1, "Sending pending digit '%c'\n", gsm->pvt->dialdest[x]);
					gsm_information(gsm->gsm, gsm->pvt->gsmcall, 
						gsm->pvt->dialdest[x]);
				}
				ast_mutex_unlock(&gsm->pvt->lock);
				break;
			case GSM_EVENT_NOTIFY:
				chanpos = e->notify.channel;
				if (chanpos < 0) {
					ast_log(LOG_WARNING, "Received NOTIFY on unconfigured channel %d/%d span %d\n",
						GSM_SPAN(e->notify.channel), GSM_CHANNEL(e->notify.channel), gsm->span);
				} else 
					/* FIXME ... Some utility for this or delete event & stuff */
				break;
#ifdef CONFIG_CHECK_PHONE
			case GSM_EVENT_CHECK_PHONE:/*Add by makes 2012-4-10 10:05 phone check*/
			{
				/*switch(e->notify.info){
					case PHONE_CONNECT:
					case PHONE_RING:
					case PHONE_BUSY:
					case PHONE_POWEROFF:
						//printf("EXIST\n");
						break;
					case PHONE_NOT_EXIST:
					default:
						//printf("NOT EXIST\n");
						break;
				}*/
				gsm->phone_stat=e->notify.info;
				ast_cond_signal(&gsm->check_cond);
				break;
			}
#endif
#ifdef VIRTUAL_TTY
			case GSM_EVENT_INIT_MUX:
			{
				init_virtual_tty(gsm,gsm->virtual_tty);
				break;
			}
#endif
			default:
				ast_debug(1, "Event: %d\n", e->e);
			}
		}	
		
		ast_mutex_unlock(&gsm->lock);
	}
	/* Never reached */
	return NULL;
}

static int start_gsm(struct extra_gsm *gsm)
{
	int res, x;
	struct dahdi_params p;
	struct dahdi_bufferinfo bi;
	struct dahdi_spaninfo si;

	gsm->fd = open("/dev/dahdi/channel", O_RDWR);

	x = gsm->dchannel;

	if ((gsm->fd < 0) || (ioctl(gsm->fd,DAHDI_SPECIFY,&x) == -1)) {
		ast_log(LOG_ERROR, "Unable to open D-channel %d (%s)\n", x, strerror(errno));
		return -1;
	}

	memset(&p, 0, sizeof(p));
	res = ioctl(gsm->fd, DAHDI_GET_PARAMS, &p);
	if (res) {
		extra_close_gsm_fd(gsm);
		ast_log(LOG_ERROR, "Unable to get parameters for D-channel %d (%s)\n", x, strerror(errno));
		return -1;
	}

	if ((p.sigtype != DAHDI_SIG_HDLCFCS) && (p.sigtype != DAHDI_SIG_HARDHDLC)) {
		extra_close_gsm_fd(gsm);
		ast_log(LOG_ERROR, "D-channel %d is not in HDLC/FCS mode.\n", x);
		return -1;
	}

	memset(&si, 0, sizeof(si));
	res = ioctl(gsm->fd, DAHDI_SPANSTAT, &si);
	if (res) {
		extra_close_gsm_fd(gsm);
		ast_log(LOG_ERROR, "Unable to get span state for D-channel %d (%s)\n", x, strerror(errno));
	}


	if (!si.alarms)
		gsm->dchanavail |= DCHAN_NOTINALARM;
	else
		gsm->dchanavail &= ~DCHAN_NOTINALARM;

	memset(&bi, 0, sizeof(bi));
	bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
	bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
	bi.numbufs = 32;
	bi.bufsize = 1024;
	if (ioctl(gsm->fd, DAHDI_SET_BUFINFO, &bi)) {
		ast_log(LOG_ERROR, "Unable to set appropriate buffering on channel %d: %s\n", x, strerror(errno));
		extra_close_gsm_fd(gsm);
		return -1;
	}

	gsm->dchan = gsm_new(gsm->fd, gsm->nodetype, gsm->switchtype, gsm->span, gsm->debug_at_flag);

	if (!gsm->dchan) {
		extra_close_gsm_fd(gsm);
		ast_log(LOG_ERROR, "Unable to create GSM structure\n");
		return -1;
	}
	gsm_set_debug(gsm->dchan, DEFAULT_GSM_DEBUG);

	/* Assume primary is the one we use */
	gsm->gsm = gsm->dchan;
	gsm->dchan->vol=gsm->vol;
	gsm->dchan->mic=gsm->mic;
	strncpy(gsm->dchan->sms_text_coding,gsm->send_sms.coding,strlen(gsm->send_sms.coding));
	gsm->resetpos = -1;
	if (ast_pthread_create_background(&gsm->master, NULL, gsm_dchannel, gsm)) {
		extra_close_gsm_fd(gsm);
		ast_log(LOG_ERROR, "Unable to spawn D-channel: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static char *gsm_complete_span_helper(const char *line, const char *word, int pos, int state, int rpos)
{
	int which, span;
	char *ret = NULL;

	if (pos != rpos)
		return ret;

	for (which = span = 0; span < NUM_SPANS; span++) {
		if (gsms[span].gsm && ++which > state) {
			if (asprintf(&ret, "%d", span + 1) < 0) {	/* user indexes start from 1 */
				ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
			}
			break;
		}
	}
	return ret;
}

static char *gsm_complete_span_4(const char *line, const char *word, int pos, int state)
{
	return gsm_complete_span_helper(line,word,pos,state,3);
}

static char *gsm_complete_span_5(const char *line, const char *word, int pos, int state)
{
	return gsm_complete_span_helper(line,word,pos,state,4);
}

static char *gsm_complete_span_6(const char *line, const char *word, int pos, int state)
{
	return gsm_complete_span_helper(line,word,pos,state,5);
}

static char *gsm_complete_span_7(const char *line, const char *word, int pos, int state)
{
	return gsm_complete_span_helper(line,word,pos,state,6);
}


static int is_dchan_span(int span, int fd)
{
	if ((span < 1) || (span > NUM_SPANS)) {
		ast_cli(fd, "Invalid span '%d'.  Should be a number from %d to %d\n", span, 1, NUM_SPANS);
		return 0;
	}

	if (!gsms[span-1].gsm) {
		ast_cli(fd, "No GSM running on span %d\n", span);
		return 0;
	}

	if (!gsms[span-1].dchan) {
		ast_cli(fd, "No dchannel running on span %d\n", span);
		return 0;
	}

	return 1;	//Valid span of dchannel
}


#if (ASTERISK_VERSION_NUM > 10444)
static char *handle_gsm_unset_debug_file(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_unset_debug_file(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm unset debug file";
		e->usage = "Usage: gsm unset debug file\n"
					"       Stop sending debug output to the previously \n"
					"       specified file\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;	
	}
#endif //(ASTERISK_VERSION_NUM > 10444)	

	/* Assume it is unset */
	ast_mutex_lock(&gsmdebugfdlock);
	close(gsmdebugfd);
	gsmdebugfd = -1;
	ast_cli(fd, "GSM debug output to file disabled\n");
	ast_mutex_unlock(&gsmdebugfdlock);

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char *handle_gsm_set_debug_file(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_set_debug_file(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int myfd;
	
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm set debug file";
		e->usage = 
				"Usage: gsm set debug file [output-file]\n"
				"       Sends GSM debug output to the specified output file\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;	
	}
#endif //(ASTERISK_VERSION_NUM > 10444)	

	if (argc < 5) {
		return _SHOWUSAGE_;
	}

	if (ast_strlen_zero(argv[4]))
		return _SHOWUSAGE_;

#if (ASTERISK_VERSION_NUM > 10444)	
	myfd = open(argv[4], O_CREAT|O_WRONLY, AST_FILE_MODE);
#else  //(ASTERISK_VERSION_NUM > 10444)	
	myfd = open(argv[4], O_CREAT|O_WRONLY, 0600);
#endif //(ASTERISK_VERSION_NUM > 10444)	
	if (myfd < 0) {
		ast_cli(fd, "Unable to open '%s' for writing\n", argv[4]);
		return _FAILURE_;
	}

	ast_mutex_lock(&gsmdebugfdlock);

	if (gsmdebugfd >= 0) {
		close(gsmdebugfd);
	}
	gsmdebugfd = myfd;
	ast_copy_string(gsmdebugfilename,argv[4],sizeof(gsmdebugfilename));
	ast_mutex_unlock(&gsmdebugfdlock);
	
	ast_cli(fd, "GSM debug output will be sent to '%s'\n", argv[4]);
	
	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char *handle_gsm_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_debug(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
	
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:	
		e->command = "gsm debug span";
		e->usage = 
			"Usage: gsm debug span <span>\n"
			"       Enables debugging on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:	
		return gsm_complete_span_4(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)	

	if (argc < 4) {
		return _SHOWUSAGE_;
	}

	span = atoi(argv[3]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	gsm_set_debug(gsms[span-1].dchan, GSM_DEBUG_AT_RECEIVED);
	
	ast_cli(fd, "Enabled debugging on span %d\n", span);
	return _SUCCESS_;
}


#if (ASTERISK_VERSION_NUM > 10444)
static char *handle_gsm_no_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_no_debug(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm no debug span";
		e->usage = 
			"Usage: gsm no debug span <span>\n"
			"       Disables debugging on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_5(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)	

	if (argc < 5)
		return _SHOWUSAGE_;

	span = atoi(argv[4]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	gsm_set_debug(gsms[span-1].dchan, 0);
	ast_cli(fd, "Disabled debugging on span %d\n", span);

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char *handle_gsm_really_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_really_debug(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;	
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm intensive debug span";
		e->usage = 
			"Usage: gsm intensive debug span <span>\n"
			"       Enables debugging down to the all levels\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_5(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)	

	if (argc < 5)
		return _SHOWUSAGE_;

	span = atoi(argv[4]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	gsm_set_debug(gsms[span-1].dchan, GSM_DEBUG_APDU | GSM_DEBUG_AT_DUMP | GSM_DEBUG_AT_STATE);

	ast_cli(fd, "Enabled EXTENSIVE debugging on span %d\n", span);

	return _SUCCESS_;
}

static void gsm_build_status(int span,char *s, size_t len, int status, int active)
{
	if (!s || len < 1) {
		return;
	}

	s[0] = '\0';

	if (status & DCHAN_POWER)
		strncat(s, "Power on, ", len - strlen(s) - 1);
	else
		strncat(s, "Power off, ", len - strlen(s) - 1);

	if (status & DCHAN_PROVISIONED)
		strncat(s, "Provisioned, ", len - strlen(s) - 1);
	
	if (!(status & DCHAN_NOTINALARM))
		strncat(s, "In Alarm, ", len - strlen(s) - 1);

	if (!(status & DCHAN_POWER)) {  //if power off,module same is Down 
		strncat(s, "Down", len - strlen(s) - 1);
	} else if (status & DCHAN_UP) {
		strncat(s, "Up", len - strlen(s) - 1);
	} else if (status & DCHAN_NO_SIM) {
		strncat(s, "Undetected SIM Card", len - strlen(s) - 1);
	} else if (status & DCHAN_NO_SIGNAL) {
		strncat(s, "No Signal", len - strlen(s) - 1);
	} else if (status & DCHAN_PIN_ERROR) {
		char buffer[256];
		snprintf(buffer,256,"Pin (%s) Error",gsms[span].pin[0] == '\0' ? "undefined" : gsms[span].pin);
		strncat(s, buffer, len - strlen(buffer) - 1);
	} else {
		strncat(s, "Down", len - strlen(s) - 1);
	}

	if (active)
		strncat(s, ", Active", len - strlen(s) - 1);
	else
		strncat(s, ", Standby", len - strlen(s) - 1);

#ifdef VIRTUAL_TTY
	int tty_stat = 0;
	int mux_stat = 0;
	ioctl(gsms[span].fd, OPVXG4XX_GET_TTY_MODULE, &tty_stat);
	ioctl(gsms[span].fd, OPVXG4XX_GET_MUX_STAT, &mux_stat);
	if(tty_stat && mux_stat)
		strncat(s, ", Multiplexer", len - strlen(s) - 1);
	else
		strncat(s, ", Standard", len - strlen(s) - 1);
#else //VIRTUAL_TTY
	strncat(s, ", Standard", len - strlen(s) - 1);
#endif //VIRTUAL_TTY

	s[len - 1] = '\0';
}

static char* gsm_build_state(struct extra_gsm* egsm)
{
	static char ret_str[1024];

	if(egsm->pvt->cid_num[0] != '\0') {
		snprintf(ret_str,1024,"%s Called from %s",gsm_state2str(egsm->gsm->state),egsm->pvt->cid_num);
	} else if(egsm->pvt->gsmcall) {
		snprintf(ret_str,1024,"%s Called to %s",gsm_state2str(egsm->gsm->state),egsm->pvt->gsmcall->callednum);
	} else {
		return gsm_state2str(egsm->gsm->state);
	}

	return ret_str;
}
	

#if (ASTERISK_VERSION_NUM > 10444)
static char *handle_gsm_test_atcommand(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_test_atcommand(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
	char at_command[256];
	int res = 0;
	
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)	
	switch (cmd) {
	case CLI_INIT:	
		e->command = "gsm send at";
		e->usage = 
			"Usage: gsm send at <span> <AT Command>\n"
			"       Send AT Command on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_4(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
	if (argc < 5) {
		return _SHOWUSAGE_;
	}
	
	span = atoi(argv[3]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;
		
	ast_copy_string(at_command, argv[4], sizeof(at_command));
	res = gsm_test_atcommand(gsms[span-1].dchan, at_command);

	if (res == -1) {
		ast_cli(fd, "GSM modem is not in ready state on span %d\n", span);
		return _FAILURE_;
	} else if (res == -2) {
		ast_cli(fd, "Not sending AT Command on span %d\n", span);
		return _FAILURE_;
	}
	
	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char *handle_gsm_show_spans(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_show_spans(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
	char status[256];
	
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
#endif //(ASTERISK_VERSION_NUM > 10444)	

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm show spans";
		e->usage = 
			"Usage: gsm show spans\n"
			"       Displays GSM Information\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;	
	}
#endif //(ASTERISK_VERSION_NUM > 10444)	

	if (argc != 3)
		return _SHOWUSAGE_;

	for (span = 0; span < NUM_SPANS; span++) {
		if (gsms[span].gsm) {
			if (gsms[span].dchannel) {
				gsm_build_status(span,status, sizeof(status), gsms[span].dchanavail, gsms[span].dchan == gsms[span].gsm);
				ast_cli(fd, "GSM span %d: %s\n", span + 1, status);
			}
		}
	}

	return _SUCCESS_;
}


static void show_span(int span, int fd)
{
	char status[256];

	char *info_str = NULL;
	ast_cli(fd, "D-channel: %d\n", gsms[span].dchannel);
	gsm_build_status(span,status, sizeof(status), gsms[span].dchanavail, gsms[span].dchan == gsms[span].gsm);
	ast_cli(fd, "Status: %s\n", status);
	info_str = gsm_dump_info_str(gsms[span].gsm);
	if (info_str) {
		ast_cli(fd, "%s", info_str);
		ast_free(info_str);
	}

	//Freedom Add for debug 2012-02-15 13:41
	/////////////////////////////////////////////////
	ast_cli(fd,"Last event: %s\n",gsm_event2str(gsms[span].gsm->ev.e));
	ast_cli(fd,"State: %s\n",gsm_build_state(&gsms[span]));
	//////////////////////////////////////////////////
	
	//Freedom Add for watch AT command 2012-06-01 10:21
	///////////////////////////////////////////////////////
	char *p;
	ast_cli(fd,"Last send AT: ");
	for(p = gsms[span].gsm->at_last_sent; 
		*p != '\0' || p < (gsms[span].gsm->at_last_sent+sizeof(gsms[span].gsm->at_last_sent)); 
		p++) {
		if(*p == '\r') {
			ast_cli(fd,"\\r");
		} else if(*p == '\n') {
			ast_cli(fd,"\\n");
		} else {
			ast_cli(fd,"%c",*p);
		}
	}
	ast_cli(fd,"\n");

	ast_cli(fd,"Last receive AT: ");
	for(p = gsms[span].gsm->at_pre_recv; 
		*p != '\0' || p < (gsms[span].gsm->at_pre_recv+sizeof(gsms[span].gsm->at_pre_recv));
		p++) {
		if(*p == '\r') {
			ast_cli(fd,"\\r");
		} else if(*p == '\n') {
			ast_cli(fd,"\\n");
		} else {
			ast_cli(fd,"%c",*p);
		}
	}
	ast_cli(fd,"\n");
	///////////////////////////////////////////////////////
}


#if (ASTERISK_VERSION_NUM > 10444)
static char *handle_gsm_show_span(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_show_span(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
	
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:	
		e->command = "gsm show span";
		e->usage = 
			"Usage: gsm show span <span>|all\n"
			"       Displays GSM Information on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_4(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc < 4)
		return _SHOWUSAGE_;

	if(strcasecmp(argv[3],"all") == 0 ){
		for (span = 0; span < NUM_SPANS; span++ ) {
			if (gsms[span].dchan) {
				show_span(span,fd);
			}
		}
	} else {
		span = atoi(argv[3]);
		if (! is_dchan_span(span,fd) ) return _FAILURE_;
		show_span(span-1,fd);
	}

	return _SUCCESS_;
}


#if (ASTERISK_VERSION_NUM > 10444)
static char *handle_gsm_show_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_show_debug(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
	int count=0;
	int debug=0;
	
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
#endif //(ASTERISK_VERSION_NUM > 10444)	

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:	
		e->command = "gsm show debug";
		e->usage = 
			"Usage: gsm show debug\n"
			"	Show the debug state of gsm spans\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;	
	}
#endif //(ASTERISK_VERSION_NUM > 10444)	

	for (span = 0; span < NUM_SPANS; span++) {
		if (gsms[span].gsm) {
			debug = 0;
			if (gsms[span].dchan) {
				debug = gsm_get_debug(gsms[span].dchan);
				ast_cli(fd, "Span %d: Debug: %s\tLevel: %x\n", 
					span+1, (debug & GSM_DEBUG_AT_STATE)? "Yes" : "No",(debug & GSM_DEBUG_AT_STATE));
				count++;
			}
		}
	}
	
	ast_mutex_lock(&gsmdebugfdlock);
	if (gsmdebugfd >= 0) 
		ast_cli(fd, "Logging GSM debug to file %s\n", gsmdebugfilename);
	ast_mutex_unlock(&gsmdebugfdlock);
	    
	if (!count) 
		ast_cli(fd, "No debug set or no GSM running\n");

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char *handle_gsm_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_version(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm show version";
		e->usage = 
			"Usage: gsm show version\n"
			"Show libgsmat version information\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	ast_cli(fd, "libgsmat version: %s\n", gsm_get_version());

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char *handle_gsm_send_ussd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_send_ussd(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
	int ret;
	struct timespec ts;
	int timeout=10;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm send ussd";
		e->usage =
			"Usage: gsm send ussd <span> <message> [timeout]\n"
			"       Send USSD on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_4(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)	

	if (argc < 5 || argc > 6)
		return _SHOWUSAGE_;

	if(argc == 6)
		timeout = atoi(argv[5]);

	span = atoi(argv[3]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	ts.tv_sec = time(NULL) + timeout;
	ts.tv_nsec = 0;
		
	if(ast_mutex_trylock(&gsms[span-1].ussd_mutex) != 0 ) {
		ast_cli(fd, "0:Sending USSD now on span %d\n",span);
		return _SUCCESS_;
	}

	gsm_send_ussd(gsms[span-1].gsm, (char*)argv[4]);
	ret = ast_cond_timedwait(&gsms[span-1].ussd_cond, &gsms[span-1].ussd_mutex, &ts);
	if(ret == 0) {
		if(gsms[span-1].ussd_received.return_flag) {
			ast_cli(fd, "1:Recive USSD sucess on span %d\n", span);
			ast_cli(fd, "\tUSSD Responses:%d\n", gsms[span-1].ussd_received.ussd_stat);
			ast_cli(fd, "\tUSSD Code:%d\n", gsms[span-1].ussd_received.ussd_coding);
			ast_cli(fd, "\tUSSD Message:%s\n", gsms[span-1].ussd_received.text);
		} else {
			ast_cli(fd, "0:Send USSD failed on span %d\n", span);
		}
	} else {
		ast_cli(fd, "0:Send USSD timeout on span %d(%d)\n", span,ret);
	}
	ast_mutex_unlock(&gsms[span-1].ussd_mutex);

	return _SUCCESS_;
}


#if (ASTERISK_VERSION_NUM > 10444)
static char *handle_gsm_send_sms(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_send_sms(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
	const char* id;
#else  //(ASTERISK_VERSION_NUM > 10444)	
	char* id;
#endif //(ASTERISK_VERSION_NUM > 10444)	

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm send sms";
		e->usage =
			"Usage: gsm send sms <span> <destination> <message> [id]\n"
			"       Send SMS on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_4(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)	

	if (argc < 6)
		return _SHOWUSAGE_;

	id = argc >= 7 ? argv[6] : NULL;
	
	span = atoi(argv[3]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	int phone_num_len = strlen(argv[4]);
	int sms_len = strlen(argv[5]);

	if( phone_num_len <= 0 ) {
		ast_cli(fd, "Destination number to short.\n");
		return _FAILURE_;
	} else if ( phone_num_len > GSM_MAX_PHONE_NUMBER ) {
		ast_cli(fd, "Destination number to long.\n");
		return _FAILURE_;
	}

	if( sms_len <= 0 ) {
		ast_cli(fd,"SMS message to short.\n");
		return _FAILURE_;
	} else if( sms_len > GSM_MAX_SMS_LENGTH ) {
		ast_cli(fd,"SMS message to long.\n");
		return _FAILURE_;
	}
	
	if(gsms[span-1].send_sms.mode == SEND_SMS_MODE_PDU) {
		char pdu[1024];		
		if(!gsm_encode_pdu_ucs2(gsms[span-1].send_sms.smsc, (char*)argv[4],(char*)argv[5], gsms[span-1].send_sms.coding, pdu)) {
			ast_cli(fd,"Encode pdu error\n");
			return _FAILURE_;
		}
	
		ast_mutex_lock(&gsms[span-1].lock);
		gsm_send_pdu(gsms[span-1].gsm, (char*)pdu, (char*)argv[5], id);
		ast_mutex_unlock(&gsms[span-1].lock);
	} else {
		ast_mutex_lock(&gsms[span-1].lock);
		gsm_send_text(gsms[span-1].gsm, (char*)argv[4], (char*)argv[5], id);
		ast_mutex_unlock(&gsms[span-1].lock);
	}

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_send_pdu(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_send_pdu(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
	const char* id;
#else  //(ASTERISK_VERSION_NUM > 10444)
	char* id;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm send pdu";
		e->usage =
			"Usage: gsm send pdu <span> <message> [id]\n"
			"       Send PDU on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_4(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc < 5)
		return _SHOWUSAGE_;

	id = argc >= 6 ? argv[5] : NULL;

	span = atoi(argv[3]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	int phone_num_len = strlen(argv[4]);
	int pdu_len = strlen(argv[5]);

	if( phone_num_len <= 0 ) {
		ast_cli(fd, "Destination number to short.\n");
		return _FAILURE_;
	} else if (phone_num_len > GSM_MAX_PHONE_NUMBER) {
		ast_cli(fd, "Destination number to long.\n");
		return _FAILURE_;
	}

	if( pdu_len <= 0 ) {
		ast_cli(fd,"PDU message to short.\n");
		return _FAILURE_;
	} else if(pdu_len > GSM_MAX_PDU_LENGTH) {
		ast_cli(fd,"PDU message to long.\n");
		return _FAILURE_;
	}

	ast_mutex_lock(&gsms[span-1].lock);
	gsm_send_pdu(gsms[span-1].gsm, (char*)argv[4], NULL, id);
	ast_mutex_unlock(&gsms[span-1].lock);

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_set_send_sms_mode_pdu(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_set_send_sms_mode_pdu(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm set send sms mode pdu";
		e->usage =
			"Usage: gsm set send sms mode pdu <span>\n"
			"       Setting send sms mode is pdu on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_7(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 7)
		return _SHOWUSAGE_;
	
	span = atoi(argv[6]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	gsms[span-1].send_sms.mode = SEND_SMS_MODE_PDU;

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_set_send_sms_mode_txt(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_set_send_sms_mode_txt(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;	
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm set send sms mode text";
		e->usage =
			"Usage: gsm set send sms mode text <span>\n"
			"       Setting send sms mode is text on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_7(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 7)
		return _SHOWUSAGE_;

	span = atoi(argv[6]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;
	
	gsms[span-1].send_sms.mode = SEND_SMS_MODE_TXT;

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_set_send_sms_smsc(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_set_send_sms_smsc(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm set send sms smsc";
		e->usage =
			"Usage: gsm set send sms smsc <span> <number>\n"
			"       Setting send sms service center number on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_6(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 7)
		return _SHOWUSAGE_;

	span = atoi(argv[5]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	strncpy(gsms[span-1].send_sms.smsc,argv[6],sizeof(gsms[span-1].send_sms.smsc));

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_set_send_sms_coding(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_set_send_sms_coding(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm set send sms coding";
		e->usage =
			"Usage: gsm set send sms coding <span> <coding>\n"
			"       Setting send sms character coding on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_6(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 7)
		return _SHOWUSAGE_;

	span = atoi(argv[5]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	strncpy(gsms[span-1].send_sms.coding,argv[6],sizeof(gsms[span-1].send_sms.coding));
	strncpy(gsms[span-1].gsm->sms_text_coding,gsms[span-1].send_sms.coding,strlen(gsms[span-1].send_sms.coding));

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_show_send_sms_mode(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_show_send_sms_mode(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm show send sms mode";
		e->usage =
			"Usage: gsm show send sms mode <span>\n"
			"       Show send sms mode on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_6(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 6)
		return _SHOWUSAGE_;

	span = atoi(argv[5]);

	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	if(gsms[span-1].send_sms.mode == SEND_SMS_MODE_PDU)
		ast_cli(fd, "pdu\n");
	else
		ast_cli(fd,"text\n");

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_show_send_sms_smsc(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_show_send_sms_smsc(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm show send sms smsc";
		e->usage =
			"Usage: gsm show send sms smsc <span>\n"
			"       Show send sms service center number on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_6(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 6)
		return _SHOWUSAGE_;

	span = atoi(argv[5]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	if(gsms[span-1].send_sms.smsc[0] == '\0')
		ast_cli(fd, "Undefined\n");
	else
		ast_cli(fd, "%s\n",gsms[span-1].send_sms.smsc);

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_show_send_sms_coding(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_show_send_sms_coding(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm show send sms coding";
		e->usage =
			"Usage: gsm show send sms coding <span>\n"
			"       Show send sms character coding on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_6(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 6)
		return _SHOWUSAGE_;

	span = atoi(argv[5]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	if(gsms[span-1].send_sms.coding[0] == '\0')
		ast_cli(fd,"ascii\n");
	else
		ast_cli(fd, "%s\n",gsms[span-1].send_sms.coding);

	return _SUCCESS_;
}


#ifdef CONFIG_CHECK_PHONE
#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_check_phone_stat(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_check_phone_stat(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
	int timeout=DEFAULT_CHECK_TIMEOUT;
	int ret = -1;
	int hangup_mode = 1;
	struct timespec ts;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
	const char *phone_number = NULL;
#else  //(ASTERISK_VERSION_NUM > 10444)
	char *phone_number = NULL;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm check phone stat";
		e->usage =
			"Usage: gsm check phone stat <span> <number> <hangup> [timeout]\n"
			"       Check the stat of the phone on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_5(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc < 7 || argc > 8)
		return _SHOWUSAGE_;
	
	span = atoi(argv[4]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	phone_number = argv[5];
	hangup_mode = atoi(argv[6]);
	if(argc == 8)
		timeout=atoi(argv[7]);
	if(ast_mutex_trylock(&gsms[span-1].phone_lock)==0){
		gsm_set_check_phone_mode(gsms[span-1].gsm,1);
		ts.tv_sec = time(NULL)+timeout;
		ts.tv_nsec = 0;
		ast_mutex_lock(&gsms[span-1].check_mutex);
		ret=gsm_check_phone_stat(gsms[span-1].gsm,phone_number,hangup_mode,timeout);
		if(ret!=0){
			ast_cli(fd,"SPAN:%d USING\n",span);
			ast_mutex_unlock(&gsms[span-1].check_mutex);
			ast_mutex_unlock(&gsms[span-1].phone_lock);
			return _FAILURE_;
		}
		ret=ast_cond_timedwait(&gsms[span-1].check_cond,&gsms[span-1].check_mutex,&ts);
		if(ret==0){
			switch(gsms[span-1].phone_stat){
				case SPAN_USING:
					ast_cli(fd,"SPAN:%d USING\n",span);
					break;
				case PHONE_CONNECT:
					ast_cli(fd,"PHONE:%s CONNECT\n",phone_number);
					gsm_set_check_phone_mode(gsms[span-1].gsm,0);
					break;
				case PHONE_RING:
					ast_cli(fd,"PHONE:%s RING\n",phone_number);
					gsm_set_check_phone_mode(gsms[span-1].gsm,0);
					break;
				case PHONE_POWEROFF:
					ast_cli(fd,"PHONE:%s POWEROFF\n",phone_number);
					gsm_set_check_phone_mode(gsms[span-1].gsm,0);
					break;
				case PHONE_BUSY:
					ast_cli(fd,"PHONE:%s BUSY\n",phone_number);
					gsm_set_check_phone_mode(gsms[span-1].gsm,0);
					break;
				case PHONE_TIMEOUT:
					ast_cli(fd,"PHONE:%s TIMEOUT\n",phone_number);
					gsm_set_check_phone_mode(gsms[span-1].gsm,0);
					break;
				//case PHONE_NOT_EXIST:
				default:
					ast_cli(fd,"PHONE:%s NOEXIST\n",phone_number);
					gsm_set_check_phone_mode(gsms[span-1].gsm,0);
					break;
			}
		} else {
			ast_cli(fd,"PHONE:%s TIMEOUT\n",phone_number);
		}
		ast_mutex_unlock(&gsms[span-1].check_mutex);
		ast_mutex_unlock(&gsms[span-1].phone_lock);
	} else {
		ast_cli(fd,"SPAN:%d USING\n",span);
	}

	return _SUCCESS_;
}
#endif

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_power_on(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_power_on(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm power on";
		e->usage =
			"Usage: gsm power on <span>\n"
			"       Set GSM module power on on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_4(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 4)
		return _SHOWUSAGE_;

	span = atoi(argv[3]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	ast_mutex_lock(&gsms[span-1].lock);
	if (ioctl(gsms[span-1].gsm->fd, OPVXG4XX_SPAN_INIT, 0)==0) {
		gsms[span-1].gsm_init_flag=0;
		gsms[span-1].gsm_reinit=0;
		sleep(2);	//Wait Module start
		gsm_module_start(gsms[span-1].gsm);
		ast_cli(fd, "Power on span %d sucessed\n",span);
	} else {
		ast_cli(fd, "Unable to power on span %d\n",span);
	}
	ast_mutex_unlock(&gsms[span-1].lock);

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_power_off(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_power_off(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm power off";
		e->usage =
			"Usage: gsm power off <span>\n"
			"       Set GSM module power off on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_4(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 4)
		return _SHOWUSAGE_;
	span = atoi(argv[3]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	ast_mutex_lock(&gsms[span-1].lock);
	unsigned char power_stat=0;
	ioctl(gsms[span-1].gsm->fd, OPVXG4XX_SPAN_STAT, &power_stat);
	if(power_stat) {
		if (!gsm_is_up(&gsms[span-1])) {
			gsms[span-1].resetting = 0;
			/* Hangup active channels and put them in alarm mode */
			struct extra_pvt *p = gsms[span-1].pvt;
			if (p) {
				if (p->gsmcall) {
					gsm_hangup(p->gsm->gsm, p->gsmcall, -1);
					gsm_destroycall(p->gsm->gsm, p->gsmcall);
					p->gsmcall = NULL;
					if (p->owner)
#if (ASTERISK_VERSION_NUM >= 110000)
						ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
						p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
				}
			}
		}
		gsms[span-1].dchanavail &= ~DCHAN_POWER;
		gsms[span-1].dchanavail &= ~DCHAN_UP;
		gsms[span-1].dchanavail |= DCHAN_NO_SIGNAL;
		if (ioctl(gsms[span-1].gsm->fd, OPVXG4XX_SPAN_REMOVE, 0)==0) {
			ast_cli(fd, "Power off span %d sucessed\n",span);	
		}
	} else {
		ast_cli(fd, "Unable to power off span %d\n",span);
	}
	ast_mutex_unlock(&gsms[span-1].lock);

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_power_reset(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_power_reset(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm power reset";
		e->usage =
			"Usage: gsm power reset <span>\n"
			"       Reset GSM module power on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_4(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 4)
		return _SHOWUSAGE_;
	span = atoi(argv[3]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	unsigned char power_stat=0;
	ioctl(gsms[span-1].gsm->fd, OPVXG4XX_SPAN_STAT, &power_stat);
	if(power_stat) {
		if (!gsm_is_up(&gsms[span-1])) {
			gsms[span-1].resetting = 0;
			/* Hangup active channels and put them in alarm mode */
			struct extra_pvt *p = gsms[span-1].pvt;
			if (p) {
				if (p->gsmcall) {
					gsm_hangup(p->gsm->gsm, p->gsmcall, -1);
					gsm_destroycall(p->gsm->gsm, p->gsmcall);
					p->gsmcall = NULL;
					if (p->owner)
#if (ASTERISK_VERSION_NUM >= 110000)
						ast_channel_softhangup_internal_flag_add(p->owner, AST_SOFTHANGUP_DEV);
#else  //(ASTERISK_VERSION_NUM >= 110000)
						p->owner->_softhangup |= AST_SOFTHANGUP_DEV;
#endif //(ASTERISK_VERSION_NUM >= 110000)
				}
			}
		}
		gsms[span-1].dchanavail &= ~DCHAN_POWER;
		gsms[span-1].dchanavail &= ~DCHAN_UP;
		gsms[span-1].dchanavail |= DCHAN_NO_SIGNAL;
		if (ioctl(gsms[span-1].gsm->fd, OPVXG4XX_SPAN_REMOVE, 0)!=0) {
			ast_cli(fd, "Power off span %d failed\n",span);	
		} else {
			if (ioctl(gsms[span-1].gsm->fd, OPVXG4XX_SPAN_INIT, 0)!=0) {
				ast_cli(fd, "Power on span %d failed\n",span);
			} else {
				gsms[span-1].gsm_init_flag=0;
				gsms[span-1].gsm_reinit=0;
				sleep(2);	//Wait Module start
				gsm_module_start(gsms[span-1].gsm);
				ast_cli(fd, "Reset power on span %d sucess\n",span);
			}
		}
	} else {
		if (ioctl(gsms[span-1].gsm->fd, OPVXG4XX_SPAN_INIT, 0)!=0) {
			ast_cli(fd, "Power on span %d failed\n",span);
		} else {
			gsms[span-1].gsm_init_flag=0;
			gsms[span-1].gsm_reinit=0;
			sleep(2);	//Wait Module start
			gsm_module_start(gsms[span-1].gsm);
			ast_cli(fd, "Reset power on span %d sucess\n",span);
		}
	}

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_power_stat(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_power_stat(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm power stat";
		e->usage =
			"Usage: gsm power stat <span>\n"
			"       Get GSM module power stat on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_4(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 4)
		return _SHOWUSAGE_;

	span = atoi(argv[3]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	ast_mutex_lock(&gsms[span-1].lock);
	unsigned char power_stat=0;
	ioctl(gsms[span-1].gsm->fd, OPVXG4XX_SPAN_STAT, &power_stat);
	if(power_stat==1)
		ast_cli(fd, "span %d power on\n",span);
	else
		ast_cli(fd, "span %d power off\n",span);

	ast_mutex_lock(&gsms[span-1].lock);

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_reload(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm reload span";
		e->usage =
			"Usage: gsm reload span <span>\n"
			"       Reload GSM module configure on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_4(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 4)
		return _SHOWUSAGE_;

	span = atoi(argv[3]);
	if (! is_dchan_span(span,fd) ) return _FAILURE_;

	ast_mutex_lock(&gsms[span-1].lock);
	unsigned char power_stat = 0;
	ioctl(gsms[span-1].gsm->fd, OPVXG4XX_SPAN_STAT, &power_stat);
	if(power_stat) {
		gsm_module_start(gsms[span-1].gsm);
	}
	ast_mutex_unlock(&gsms[span-1].lock);

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_set_debugat(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_set_debugat(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
	int span_all=0;
	int status;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm set debug at";
		e->usage =
			"Usage: gsm set debug at <span>|all on|off\n"
			"       Set at command debug mode on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_5(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 6)
		return _SHOWUSAGE_;

	if(!strcasecmp(argv[4],"all")){
		span_all=1;
	}else{
		span = atoi(argv[4]);
		if (! is_dchan_span(span,fd) ) return _FAILURE_;
	}

	status=!strcasecmp(argv[5],"on");
	if(span_all){
		if(status>0)
			ast_cli(fd, "all span at debug on\n");
		else
			ast_cli(fd, "all span at debug off\n");
		for (span = 0; span < NUM_SPANS; span++) {
			if (gsms[span].gsm){
				gsm_set_debugat(gsms[span].gsm,status);
			}
		}
	}else{
		gsm_set_debugat(gsms[span-1].gsm,status);
		if(status>0)
			ast_cli(fd, "span %d at debug on\n",span);
		else
			ast_cli(fd, "span %d at debug off\n",span);
	}
	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char * handle_gsm_show_debugat(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int handle_gsm_show_debugat(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int span;
	int span_all=0;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "gsm show debug at";
		e->usage =
			"Usage: gsm show debug at <span>\n"
			"       Show at command debug stat on a given GSM span\n";
		return NULL;
	case CLI_GENERATE:
		return gsm_complete_span_5(a->line, a->word, a->pos, a->n);
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 5)
		return _SHOWUSAGE_;

	if(!strcasecmp(argv[4],"all")){
		span_all=1;
	}else{
		span = atoi(argv[4]);
		if (! is_dchan_span(span,fd) ) return _FAILURE_;
	}

	if(span_all) {
		for (span = 0; span < NUM_SPANS; span++) {
			if (gsms[span].gsm){
				if(gsms[span].gsm->debug_at_flag){
					ast_cli(fd, "span %d at debug on\n",span+1);
				}else{
					ast_cli(fd, "span %d at debug off\n",span+1);
				}
			}
		}
	} else {
		if(gsms[span-1].gsm->debug_at_flag){
			ast_cli(fd, "span %d at debug on\n",span);
		}else{
			ast_cli(fd, "span %d at debug off\n",span);
		}
	}
	return _SUCCESS_;
}


#if (ASTERISK_VERSION_NUM > 10444)
static struct ast_cli_entry extra_gsm_cli[] = {
	AST_CLI_DEFINE(handle_gsm_debug, "Enables GSM debugging on a span"),
	AST_CLI_DEFINE(handle_gsm_no_debug, "Disables GSM debugging on a span"),
	AST_CLI_DEFINE(handle_gsm_really_debug, "Enables REALLY INTENSE GSM debugging"),
	AST_CLI_DEFINE(handle_gsm_test_atcommand, "Send AT Commmand on a given GSM span"),
	AST_CLI_DEFINE(handle_gsm_show_spans, "Displays GSM Information"),
	AST_CLI_DEFINE(handle_gsm_show_span, "Displays GSM Information"),
	AST_CLI_DEFINE(handle_gsm_show_debug, "Displays current GSM debug settings"),
	AST_CLI_DEFINE(handle_gsm_set_debug_file, "Sends GSM debug output to the specified file"),
	AST_CLI_DEFINE(handle_gsm_unset_debug_file, "Ends GSM debug output to file"),
	AST_CLI_DEFINE(handle_gsm_version, "Displays libgsmat version"),
	AST_CLI_DEFINE(handle_gsm_send_sms, "Send SMS on a given GSM span"),
	AST_CLI_DEFINE(handle_gsm_send_ussd, "Send USSD on a given GSM span"),
	AST_CLI_DEFINE(handle_gsm_send_pdu,"Send PDU on a given GSM span"),
	AST_CLI_DEFINE(handle_gsm_set_send_sms_mode_pdu,"Setting send sms mode is pdu"),
	AST_CLI_DEFINE(handle_gsm_set_send_sms_mode_txt,"Setting send sms mode is text"),
	AST_CLI_DEFINE(handle_gsm_set_send_sms_smsc,"Setting send sms Service Message Center number"),
	AST_CLI_DEFINE(handle_gsm_set_send_sms_coding,"Setting send sms character coding"),
	AST_CLI_DEFINE(handle_gsm_show_send_sms_mode,"Show send sms mode"),
	AST_CLI_DEFINE(handle_gsm_show_send_sms_smsc,"Show send sms Service Message Center number"),
	AST_CLI_DEFINE(handle_gsm_show_send_sms_coding,"Show send sms character coding"),
#ifdef CONFIG_CHECK_PHONE
	AST_CLI_DEFINE(handle_gsm_check_phone_stat,"Check the stat of the phone"),
#endif
	AST_CLI_DEFINE(handle_gsm_power_on,"Power on gsm module"),
	AST_CLI_DEFINE(handle_gsm_power_off,"Power off gsm module"),
	AST_CLI_DEFINE(handle_gsm_power_reset,"Power reset gsm module"),
	AST_CLI_DEFINE(handle_gsm_power_stat,"Get gsm module power stat"),
	AST_CLI_DEFINE(handle_gsm_reload,"Reload GSM module configure"),
	AST_CLI_DEFINE(handle_gsm_set_debugat,"Set at command debug mode on a given GSM span"),
	AST_CLI_DEFINE(handle_gsm_show_debugat,"Show at command debug stat on a given GSM span"),
};
#else  //(ASTERISK_VERSION_NUM > 10444)
static struct ast_cli_entry extra_gsm_cli[] = {
	{ { "gsm", "debug", "span", NULL },
	handle_gsm_debug, "Enables GSM debugging on a span",
	"Usage: gsm debug span <span>\n"
	"       Enables debugging on a given GSM span\n", gsm_complete_span_4},
	
	{ { "gsm", "no", "debug", "span", NULL },
	handle_gsm_no_debug, "Disables GSM debugging on a span",
	"Usage: gsm no debug span <span>\n"
	"       Disables debugging on a given GSM span\n", gsm_complete_span_5},
	
	{ { "gsm", "intensive", "debug", "span", NULL },
	handle_gsm_really_debug, "Enables REALLY INTENSE GSM debugging",
	"Usage: gsm intensive debug span <span>\n"
	"       Enables debugging down to the all levels\n", gsm_complete_span_5},
	
	{ { "gsm", "send", "at", NULL },
	handle_gsm_test_atcommand, "Send AT Commmand on a given GSM span",
	"Usage: gsm send at <span> <AT Command>\n"
	"       Send AT Command on a given GSM span\n", gsm_complete_span_4},

	{ { "gsm", "show", "spans", NULL },
	handle_gsm_show_spans, "Displays GSM Information",
	"Usage: gsm show spans\n"
	"       Displays GSM Information\n", NULL},
	
	{ { "gsm", "show", "span", NULL },
	handle_gsm_show_span, "Displays GSM Information",
	"Usage: gsm show span <span>|all\n"
	"       Displays GSM Information on a given GSM span\n", gsm_complete_span_4},
	
	{ { "gsm", "show", "debug", NULL },
	handle_gsm_show_debug, "Displays current GSM debug settings",
	"Usage: gsm show debug\n"
	"	Show the debug state of gsm spans\n", NULL},
	
	{ { "gsm", "set", "debug", "file", NULL },
	handle_gsm_set_debug_file, "Sends GSM debug output to the specified file",
	"Usage: gsm set debug file [output-file]\n"
	"       Sends GSM debug output to the specified output file\n", NULL},	
			
	{ { "gsm", "unset", "debug", "file", NULL },
	handle_gsm_unset_debug_file, "Ends GSM debug output to file",
	"Usage: gsm unset debug file\n"
	"       Stop sending debug output to the previously \n"
	"       specified file\n", NULL},
	
	{ { "gsm", "show", "version", NULL },
	handle_gsm_version, "Displays libgsmat version",
	"Usage: gsm show version\n"
			"Show libgsmat version information\n", NULL},
		
	{ { "gsm", "send", "sms", NULL },
	handle_gsm_send_sms, "Send SMS on a given GSM span",
	"Usage: gsm send sms <span> <destination> <message> [id]\n"
	"       Send SMS on a given GSM span\n", gsm_complete_span_4},

	{ { "gsm", "send", "ussd", NULL },
	handle_gsm_send_ussd, "Send USSD on a given GSM span",
	"Usage: gsm send ussd <span> <message> \n"
	"       Send USSD on a given GSM span\n", gsm_complete_span_4},
	
	{ { "gsm", "send", "pdu", NULL },
	handle_gsm_send_pdu, "Send PDU on a given GSM span",
	"Usage: gsm send pdu <span> <message> [id]\n"
	"       Send PDU on a given GSM span\n", gsm_complete_span_4},
	
	{ { "gsm", "set", "send", "sms", "mode", "pdu", NULL },
	handle_gsm_set_send_sms_mode_pdu, "Setting send sms mode is pdu on a given GSM span",
	"Usage: gsm set send sms mode pdu <span>\n"
	"       Setting send sms mode is pdu on a given GSM span\n", gsm_complete_span_7},

	{ { "gsm", "set", "send", "sms", "mode", "text", NULL },
	handle_gsm_set_send_sms_mode_txt, "Setting send sms mode is text on a given GSM span",
	"Usage: gsm set send sms mode text <span>\n"
	"       Setting send sms mode is text on a given GSM span\n", gsm_complete_span_7},

	{ { "gsm", "set", "send", "sms", "smsc", NULL },
	handle_gsm_set_send_sms_smsc, "Setting send sms service message center number on a given GSM span",
	"Usage: gsm set send sms smsc <span> <number>\n"
	"       Setting send sms service message center number on a given GSM span\n", gsm_complete_span_6},

	{ { "gsm", "set", "send", "sms", "coding",NULL },
	handle_gsm_set_send_sms_coding, "Setting send sms character coding on a given GSM span",
	"Usage: gsm set send sms coding <span> <coding>\n"
	"       Setting send sms character coding on a given GSM span\n", gsm_complete_span_6},	

	{ { "gsm", "show", "send", "sms", "mode", NULL },
	handle_gsm_show_send_sms_mode, "Show send sms mode on a given GSM span",
	"Usage: gsm set show send sms mode <span>\n"
	"       Show send sms mode on a given GSM span\n", gsm_complete_span_6},

	{ { "gsm", "show", "send", "sms", "smsc", NULL },
	handle_gsm_show_send_sms_smsc, "Show send sms service message center number on a given GSM span",
	"Usage: gsm set show sms smsc <span>\n"
	"       Show send sms service message center number on a given GSM span\n", gsm_complete_span_6},

	{ { "gsm", "show", "send", "sms", "coding", NULL },
	handle_gsm_show_send_sms_coding, "Show send sms character coding on a given GSM span",
	"Usage: gsm show send sms coding <span>\n"
	"       Show send sms character coding on a given GSM span\n", gsm_complete_span_6},
#ifdef CONFIG_CHECK_PHONE
	{ { "gsm", "check", "phone", "stat", NULL },
	handle_gsm_check_phone_stat, "Check the stat of the phone on a given GSM span",
	"Usage: gsm check phone stat <span> <number> <hangup> [timeout]\n"
	"       Check the stat of the phone on a given GSM span\n", gsm_complete_span_5},
#endif
	{ { "gsm", "power", "on", NULL },
	handle_gsm_power_on, "Set GSM module power on on a given GSM span",
	"Usage: gsm power on <span>\n"
	"       Set GSM module power on\n", gsm_complete_span_4},
	{ { "gsm", "power", "off", NULL },
	handle_gsm_power_off, "Set GSM module power off on a given GSM span",
	"Usage: gsm power off <span>\n"
	"       Set GSM module power off\n", gsm_complete_span_4},
	{ { "gsm", "power", "reset", NULL },
	handle_gsm_power_reset, "Reset GSM module power on a given GSM span",
	"Usage: gsm power reset <span>\n"
	"       Reset GSM module power \n", gsm_complete_span_4},
	{ { "gsm", "power", "stat", NULL },
	handle_gsm_power_stat, "Get GSM module power stat on a given GSM span",
	"Usage: gsm power stat <span>\n"
	"       Get GSM module power stat\n", gsm_complete_span_4},
	{ { "gsm", "reload", "span", NULL },
	handle_gsm_reload, "Reload GSM module configure on a given GSM span",
	"Usage: gsm reload span <span>\n"
	"       Reload GSM module configure\n", gsm_complete_span_4},
	{ { "gsm", "debug","at","span", NULL },
	handle_gsm_set_debugat, "Set at command debug mode on a given GSM span",
	"Usage: gsm set debug at <span>|all on|off\n"
	"       Set at command debug mode on a given GSM span\n", gsm_complete_span_5},
	{ { "gsm", "debug","at","span", NULL },
	handle_gsm_show_debugat, "Show at command debug stat on a given GSM span",
	"Usage: gsm show debug at <span>\n"
	"       Show at command debug stat on a given GSM span\n", gsm_complete_span_5},
};
#endif //(ASTERISK_VERSION_NUM > 10444)

#endif /* HAVE_GSMAT */

#if (ASTERISK_VERSION_NUM > 10444)
static char *extra_destroy_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int extra_destroy_channel(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int channel;
	int ret;
	
#if (ASTERISK_VERSION_NUM > 10444)
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)	
	
#if (ASTERISK_VERSION_NUM > 10444)	
	switch (cmd) {
	case CLI_INIT:
		e->command = "extra destroy channel";
		e->usage =
			"Usage: extra destroy channel <chan num>\n"
			"	DON'T USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.  Immediately removes a given channel, whether it is in use or not\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)	

	if (argc != 4)
		return _SHOWUSAGE_;

	channel = atoi(argv[3]);
	ret = extra_destroy_channel_bynum(channel);
	return ( RESULT_SUCCESS == ret ) ? _SUCCESS_ : _FAILURE_;
}

static void extra_softhangup_all(void)
{
	struct extra_pvt *p;
retry:
	ast_mutex_lock(&iflock);
	for (p = iflist; p; p = p->next) {
		ast_mutex_lock(&p->lock);
		if (p->owner && !p->restartpending) {
			if (ast_channel_trylock(p->owner)) {
				if (option_debug > 2)
					ast_verbose("Avoiding deadlock\n");
				/* Avoid deadlock since you're not supposed to lock iflock or pvt before a channel */
				ast_mutex_unlock(&p->lock);
				ast_mutex_unlock(&iflock);
				goto retry;
			}
			if (option_debug > 2)
#if (ASTERISK_VERSION_NUM >= 110000)
				ast_verbose("Softhanging up on %s\n", ast_channel_name(p->owner));
#else  //(ASTERISK_VERSION_NUM >= 110000)
				ast_verbose("Softhanging up on %s\n", p->owner->name);
#endif //(ASTERISK_VERSION_NUM >= 110000)

			ast_softhangup_nolock(p->owner, AST_SOFTHANGUP_EXPLICIT);
			p->restartpending = 1;
			num_restart_pending++;
			ast_channel_unlock(p->owner);
		}
		ast_mutex_unlock(&p->lock);
	}
	ast_mutex_unlock(&iflock);
}

static int setup_extra(int reload);
static int extra_restart(void)
{
	int cancel_code;
	struct extra_pvt *p;

	ast_mutex_lock(&restart_lock);
	ast_verb(1, "Destroying channels and reloading EXTRA configuration.\n");
	extra_softhangup_all();
	ast_verb(4, "Initial softhangup of all EXTRA channels complete.\n");

#ifdef HAVE_GSMAT
	int i;
	for (i = 0; i < NUM_SPANS; i++) {
		if (gsms[i].master && (gsms[i].master != AST_PTHREADT_NULL)) {
			cancel_code = pthread_cancel(gsms[i].master);
			pthread_kill(gsms[i].master, SIGURG);
			ast_debug(4, "Waiting to join thread of span %d with pid=%p, cancel_code=%d\n", i, (void *) gsms[i].master, cancel_code);
			pthread_join(gsms[i].master, NULL);
			ast_debug(4, "Joined thread of span %d\n", i);
		}
	}
#endif

	ast_mutex_lock(&ss_thread_lock);
	while (ss_thread_count > 0) { /* let ss_threads finish and run extra_hangup before dahvi_pvts are destroyed */
		int x = DAHDI_FLASH;
		ast_debug(3, "Waiting on %d analog_ss_thread(s) to finish\n", ss_thread_count);

		ast_mutex_lock(&iflock);
		for (p = iflist; p; p = p->next) {
			if (p->owner) {
				/* important to create an event for extra_wait_event to register so that all analog_ss_threads terminate */
				ioctl(p->subs[SUB_REAL].dfd, DAHDI_HOOK, &x);
			}
		}
		ast_mutex_unlock(&iflock);
		ast_cond_wait(&ss_thread_complete, &ss_thread_lock);
	}

	/* ensure any created channels before monitor threads were stopped are hungup */
	extra_softhangup_all();
	ast_verb(4, "Final softhangup of all EXTRA channels complete.\n");
	destroy_all_channels();
	ast_debug(1, "Channels destroyed. Now re-reading config. %d active channels remaining.\n", ast_active_channels());

#ifdef HAVE_GSMAT
	for (i = 0; i < NUM_SPANS; i++) {
		extra_close_gsm_fd(&(gsms[i]));
	}

	memset(gsms, 0, sizeof(gsms));
	for (i = 0; i < NUM_SPANS; i++) {
		ast_mutex_init(&gsms[i].lock);
#ifdef CONFIG_CHECK_PHONE
		ast_mutex_init(&gsms[i].phone_lock);
		ast_mutex_init(&gsms[i].check_mutex);
		ast_cond_init(&gsms[i].check_cond,NULL);
#endif
		ast_mutex_init(&gsms[i].ussd_mutex);
		ast_cond_init(&gsms[i].ussd_cond,NULL);
		gsms[i].gsm_init_flag = 0;
		gsms[i].gsm_reinit = 0;
		gsms[i].offset = -1;
		gsms[i].master = AST_PTHREADT_NULL;
		gsms[i].fd = -1;

#ifdef VIRTUAL_TTY
		gsms[i].virtual_tty = 0;
#endif //VIRTUAL_TTY
		gsms[i].vol=-1;
		gsms[i].mic=-1;
		gsms[i].debug_at_flag= 0;

	}
	gsm_set_error(extra_gsm_error);
	gsm_set_message(extra_gsm_message);
#endif

	if (setup_extra(2) != 0) {
		ast_log(LOG_WARNING, "Reload channels from extra config failed!\n");
		ast_mutex_unlock(&ss_thread_lock);
		return 1;
	}
	ast_mutex_unlock(&ss_thread_lock);
	ast_mutex_unlock(&restart_lock);
	return 0;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char *extra_restart_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int extra_restart_cmd(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
#if (ASTERISK_VERSION_NUM > 10444)
	const int argc = a->argc;
#endif //(ASTERISK_VERSION_NUM > 10444)
	
#if (ASTERISK_VERSION_NUM > 10444)	
	switch (cmd) {
	case CLI_INIT:
		e->command = "extra restart";
		e->usage =
			"Usage: extra restart\n"
			"	Restarts the EXTRA channels: destroys them all and then\n"
			"	re-reads them from chan_extra.conf.\n"
			"	Note that this will STOP any running CALL on EXTRA channels.\n"
			"";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 2)
		return _SHOWUSAGE_;

	if (extra_restart() != 0)
		return _FAILURE_;
	return _SUCCESS_;
}


#if (ASTERISK_VERSION_NUM > 10444)
static char *extra_show_channels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int extra_show_channels(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
#define FORMAT  "%7s %-10.10s %-15.15s %-10.10s %-10.10s\n"
#define FORMAT2 "%7s %-10.10s %-15.15s %-10.10s %-10.10s\n"
	unsigned int targetnum = 0;
	int filtertype = 0;
	struct extra_pvt *tmp = NULL;
	char tmps[20] = "";
	char statestr[20] = "";
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "extra show channels [group|context]";
		e->usage =
			"Usage: extra show channels [ group <group> | context <context> ]\n"
			"	Shows a list of available channels with optional filtering\n"
			"	<group> must be a number between 0 and 63\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	/* syntax: extra show channels [ group <group> | context <context> ] */

	if (!((argc == 3) || (argc == 5)))
		return _SHOWUSAGE_;

	if (argc == 5) {
		if (!strcasecmp(argv[3], "group")) {
			targetnum = atoi(argv[4]);
			if ((targetnum < 0) || (targetnum > 63))
				return _SHOWUSAGE_;
			targetnum = 1 << targetnum;
			filtertype = 1;
		} else if (!strcasecmp(argv[3], "context")) {
			filtertype = 2;
		}
	}

	ast_cli(fd, FORMAT2, "Chan", "Extension", "Context", "Language", "State");
	ast_mutex_lock(&iflock);
	for (tmp = iflist; tmp; tmp = tmp->next) {
		if (filtertype) {
			switch(filtertype) {
			case 1: /* extra show channels group <group> */
				if (!(tmp->group & targetnum)) {
					continue;
				}
				break;
			case 2: /* extra show channels context <context> */
				if (strcasecmp(tmp->context, argv[4])) {
					continue;
				}
				break;
			default:
				;
			}
		}
		if (tmp->channel > 0) {
			snprintf(tmps, sizeof(tmps), "%d", tmp->channel);
		} else
			ast_copy_string(tmps, "pseudo", sizeof(tmps));

		snprintf(statestr, sizeof(statestr), "%s", "In Service");
		ast_cli(fd, FORMAT, tmps, tmp->exten, tmp->context, tmp->language, statestr);
	}
	ast_mutex_unlock(&iflock);
	return _SUCCESS_;
#undef FORMAT
#undef FORMAT2
}

#if (ASTERISK_VERSION_NUM > 10444)
static char *extra_show_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int extra_show_channel(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int channel;
	struct extra_pvt *tmp = NULL;
	struct dahdi_confinfo ci;
	struct dahdi_params ps;
	int x;
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "extra show channel";
		e->usage =
			"Usage: extra show channel <chan num>\n"
			"	Detailed information about a given channel\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 4)
		return _SHOWUSAGE_;

	channel = atoi(argv[3]);

	ast_mutex_lock(&iflock);
	for (tmp = iflist; tmp; tmp = tmp->next) {
		if (tmp->channel == channel) {
			ast_cli(fd, "Channel: %d\n", tmp->channel);
			ast_cli(fd, "File Descriptor: %d\n", tmp->subs[SUB_REAL].dfd);
			ast_cli(fd, "Span: %d\n", tmp->span);
			ast_cli(fd, "Extension: %s\n", tmp->exten);
			ast_cli(fd, "Dialing: %s\n", tmp->dialing ? "yes" : "no");
			ast_cli(fd, "Context: %s\n", tmp->context);
			ast_cli(fd, "Caller ID: %s\n", tmp->cid_num);
			ast_cli(fd, "Caller ID name: %s\n", tmp->cid_name);
			if (tmp->vars) {
				struct ast_variable *v;
				ast_cli(fd, "Variables:\n");
				for (v = tmp->vars ; v ; v = v->next)
					ast_cli(fd, "       %s = %s\n", v->name, v->value);
			}
			ast_cli(fd, "Destroy: %d\n", tmp->destroy);
			ast_cli(fd, "InAlarm: %d\n", tmp->inalarm);
			ast_cli(fd, "Signalling Type: %s\n", sig2str(tmp->sig));
			ast_cli(fd, "Radio: %d\n", tmp->radio);

#if (ASTERISK_VERSION_NUM >= 110000)
			ast_cli(fd, "Owner: %s\n", tmp->owner ? ast_channel_name(tmp->owner) : "<None>");
			ast_cli(fd, "Real: %s%s%s\n", tmp->subs[SUB_REAL].owner ? ast_channel_name(tmp->subs[SUB_REAL].owner) : "<None>", tmp->subs[SUB_REAL].inthreeway ? " (Confed)" : "", tmp->subs[SUB_REAL].linear ? " (Linear)" : "");
			ast_cli(fd, "Callwait: %s%s%s\n", tmp->subs[SUB_CALLWAIT].owner ? ast_channel_name(tmp->subs[SUB_CALLWAIT].owner) : "<None>", tmp->subs[SUB_CALLWAIT].inthreeway ? " (Confed)" : "", tmp->subs[SUB_CALLWAIT].linear ? " (Linear)" : "");
			ast_cli(fd, "Threeway: %s%s%s\n", tmp->subs[SUB_THREEWAY].owner ? ast_channel_name(tmp->subs[SUB_THREEWAY].owner) : "<None>", tmp->subs[SUB_THREEWAY].inthreeway ? " (Confed)" : "", tmp->subs[SUB_THREEWAY].linear ? " (Linear)" : "");
#else  //(ASTERISK_VERSION_NUM >= 110000)
			ast_cli(fd, "Owner: %s\n", tmp->owner ? tmp->owner->name : "<None>");
			ast_cli(fd, "Real: %s%s%s\n", tmp->subs[SUB_REAL].owner ? tmp->subs[SUB_REAL].owner->name : "<None>", tmp->subs[SUB_REAL].inthreeway ? " (Confed)" : "", tmp->subs[SUB_REAL].linear ? " (Linear)" : "");
			ast_cli(fd, "Callwait: %s%s%s\n", tmp->subs[SUB_CALLWAIT].owner ? tmp->subs[SUB_CALLWAIT].owner->name : "<None>", tmp->subs[SUB_CALLWAIT].inthreeway ? " (Confed)" : "", tmp->subs[SUB_CALLWAIT].linear ? " (Linear)" : "");
			ast_cli(fd, "Threeway: %s%s%s\n", tmp->subs[SUB_THREEWAY].owner ? tmp->subs[SUB_THREEWAY].owner->name : "<None>", tmp->subs[SUB_THREEWAY].inthreeway ? " (Confed)" : "", tmp->subs[SUB_THREEWAY].linear ? " (Linear)" : "");
#endif //(ASTERISK_VERSION_NUM >= 110000)

			ast_cli(fd, "Confno: %d\n", tmp->confno);
			ast_cli(fd, "DSP: %s\n", tmp->dsp ? "yes" : "no");
			ast_cli(fd, "Busy Detection: %s\n", tmp->busydetect ? "yes" : "no");
			if (tmp->busydetect) {
#if defined(BUSYDETECT_TONEONLY)
				ast_cli(fd, "    Busy Detector Helper: BUSYDETECT_TONEONLY\n");
#elif defined(BUSYDETECT_COMPARE_TONE_AND_SILENCE)
				ast_cli(fd, "    Busy Detector Helper: BUSYDETECT_COMPARE_TONE_AND_SILENCE\n");
#endif
#ifdef BUSYDETECT_DEBUG
				ast_cli(fd, "    Busy Detector Debug: Enabled\n");
#endif
				ast_cli(fd, "    Busy Count: %d\n", tmp->busycount);
#if (ASTERISK_VERSION_NUM >= 100000)
				ast_cli(fd, "	 Busy Pattern: %d,%d,%d,%d\n", tmp->busy_cadence.pattern[0], tmp->busy_cadence.pattern[1], (tmp->busy_cadence.length == 4) ? tmp->busy_cadence.pattern[2] : 0, (tmp->busy_cadence.length == 4) ? tmp->busy_cadence.pattern[3] : 0);
#else
				ast_cli(fd, "	 Busy Pattern: %d,%d\n", tmp->busy_tonelength, tmp->busy_quietlength);
#endif
			}
			ast_cli(fd, "TDD: %s\n", tmp->tdd ? "yes" : "no");
			ast_cli(fd, "Relax DTMF: %s\n", tmp->dtmfrelax ? "yes" : "no");
			ast_cli(fd, "Default law: %s\n", tmp->law_default == DAHDI_LAW_MULAW ? "ulaw" : tmp->law_default == DAHDI_LAW_ALAW ? "alaw" : "unknown");
			ast_cli(fd, "Fax Handled: %s\n", tmp->faxhandled ? "yes" : "no");
			ast_cli(fd, "Pulse phone: %s\n", tmp->pulsedial ? "yes" : "no");
			ast_cli(fd, "Gains (RX/TX): %.2f/%.2f\n", tmp->rxgain, tmp->txgain);
			ast_cli(fd, "Dynamic Range Compression (RX/TX): %.2f/%.2f\n", tmp->rxdrc, tmp->txdrc);
			ast_cli(fd, "DND: %s\n", extra_dnd(tmp, -1) ? "yes" : "no");
			ast_cli(fd, "Echo Cancellation:\n");

			if (tmp->echocancel.head.tap_length) {
				ast_cli(fd, "\t%d taps\n", tmp->echocancel.head.tap_length);
				for (x = 0; x < tmp->echocancel.head.param_count; x++) {
					ast_cli(fd, "\t\t%s: %ud\n", tmp->echocancel.params[x].name, tmp->echocancel.params[x].value);
				}
				ast_cli(fd, "\t%scurrently %s\n", tmp->echocanbridged ? "" : "(unless TDM bridged) ", tmp->echocanon ? "ON" : "OFF");
			} else {
				ast_cli(fd, "\tnone\n");
			}
			if (tmp->master)
				ast_cli(fd, "Master Channel: %d\n", tmp->master->channel);
			for (x = 0; x < MAX_SLAVES; x++) {
				if (tmp->slaves[x])
					ast_cli(fd, "Slave Channel: %d\n", tmp->slaves[x]->channel);
			}

#ifdef HAVE_GSMAT
			if (tmp->gsm) {
				ast_cli(fd, "GSM Flags: ");
				if (tmp->resetting)
					ast_cli(fd, "Resetting ");
				if (tmp->gsmcall)
					ast_cli(fd, "Call ");
				ast_cli(fd, "\n");
			}
#endif

			memset(&ci, 0, sizeof(ci));
			ps.channo = tmp->channel;
			if (tmp->subs[SUB_REAL].dfd > -1) {
				memset(&ci, 0, sizeof(ci));
				if (!ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GETCONF, &ci)) {
					ast_cli(fd, "Actual Confinfo: Num/%d, Mode/0x%04x\n", ci.confno, ci.confmode);
				}
				if (!ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GETCONFMUTE, &x)) {
					ast_cli(fd, "Actual Confmute: %s\n", x ? "Yes" : "No");
				}
				memset(&ps, 0, sizeof(ps));
				if (ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_GET_PARAMS, &ps) < 0) {
					ast_log(LOG_WARNING, "Failed to get parameters on channel %d: %s\n", tmp->channel, strerror(errno));
				} else {
					ast_cli(fd, "Hookstate (FXS only): %s\n", ps.rxisoffhook ? "Offhook" : "Onhook");
				}
			}
			ast_mutex_unlock(&iflock);
			return _SUCCESS_;
		}
	}
	ast_mutex_unlock(&iflock);

	ast_cli(fd, "Unable to find given channel %d\n", channel);
	return _FAILURE_;
}

/* Based on irqmiss.c */
#if (ASTERISK_VERSION_NUM > 10444)
static char *extra_show_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int extra_show_status(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	#define FORMAT "%-40.40s %-7.7s %-6d %-6d %-6d %-3.3s %-4.4s %-8.8s %s\n"
	#define FORMAT2 "%-40.40s %-7.7s %-6.6s %-6.6s %-6.6s %-3.3s %-4.4s %-8.8s %s\n"
	int span;
	int res;
	char alarmstr[50];

	int ctl;
	struct dahdi_spaninfo s;
	
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
#endif //(ASTERISK_VERSION_NUM > 10444)

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "extra show status";
		e->usage =
			"Usage: extra show status\n"
			"       Shows a list of EXTRA cards with status\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	ctl = open("/dev/dahdi/ctl", O_RDWR);
	if (ctl < 0) {
		ast_cli(fd, "No EXTRA found. Unable to open /dev/dahdi/ctl: %s\n", strerror(errno));
		return _FAILURE_;
	}
	ast_cli(fd, FORMAT2, "Description", "Alarms", "IRQ", "bpviol", "CRC4", "Framing", "Coding", "Options", "LBO");

	//Freedom Modify 2011-08-11
#if 0
	for (span = 1; span < DAHDI_MAX_SPANS; ++span) {
#else
	for (span = 1; span < NUM_SPANS+1; ++span) {	
		if(!gsms[span-1].gsm)
			continue;
#endif
			
		s.spanno = span;
		res = ioctl(ctl, DAHDI_SPANSTAT, &s);
		if (res) {
			continue;
		}
		alarmstr[0] = '\0';
		if (s.alarms > 0) {
			if (s.alarms & DAHDI_ALARM_BLUE)
				strcat(alarmstr, "BLU/");
			if (s.alarms & DAHDI_ALARM_YELLOW)
				strcat(alarmstr, "YEL/");
			if (s.alarms & DAHDI_ALARM_RED)
				strcat(alarmstr, "RED/");
			if (s.alarms & DAHDI_ALARM_LOOPBACK)
				strcat(alarmstr, "LB/");
			if (s.alarms & DAHDI_ALARM_RECOVER)
				strcat(alarmstr, "REC/");
			if (s.alarms & DAHDI_ALARM_NOTOPEN)
				strcat(alarmstr, "NOP/");
			if (!strlen(alarmstr))
				strcat(alarmstr, "UUU/");
			if (strlen(alarmstr)) {
				/* Strip trailing / */
				alarmstr[strlen(alarmstr) - 1] = '\0';
			}
		} else {
			if (s.numchans)
				strcpy(alarmstr, "OK");
			else
				strcpy(alarmstr, "UNCONFIGURED");
		}

		ast_cli(fd, FORMAT, s.desc, alarmstr, s.irqmisses, s.bpvcount, s.crc4count,
			s.lineconfig & DAHDI_CONFIG_D4 ? "D4" :
			s.lineconfig & DAHDI_CONFIG_ESF ? "ESF" :
			s.lineconfig & DAHDI_CONFIG_CCS ? "CCS" :
			"CAS",
			s.lineconfig & DAHDI_CONFIG_B8ZS ? "B8ZS" :
			s.lineconfig & DAHDI_CONFIG_HDB3 ? "HDB3" :
			s.lineconfig & DAHDI_CONFIG_AMI ? "AMI" :
			"Unk",
			s.lineconfig & DAHDI_CONFIG_CRC4 ?
			s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "CRC4/YEL" : "CRC4" :
			s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "YEL" : "",
			lbostr[s.lbo]
			);
	}
	close(ctl);

	return _SUCCESS_;
#undef FORMAT
#undef FORMAT2
}

#if (ASTERISK_VERSION_NUM > 10444)
static char *extra_show_version(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int extra_show_version(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int pseudo_fd = -1;
	struct dahdi_versioninfo vi;
	
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
#endif //(ASTERISK_VERSION_NUM > 10444)

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "extra show version";
		e->usage =
			"Usage: extra show version\n"
			"       Shows the EXTRA version in use\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if ((pseudo_fd = open("/dev/dahdi/ctl", O_RDONLY)) < 0) {
		ast_cli(fd, "Failed to open control file to get version.\n");
		return _SUCCESS_;
	}

	strcpy(vi.version, "Unknown");
	strcpy(vi.echo_canceller, "Unknown");

	if (ioctl(pseudo_fd, DAHDI_GETVERSION, &vi))
		ast_cli(fd, "Failed to get DAHDI version: %s\n", strerror(errno));
	else
		ast_cli(fd, "EXTRA Version: %s Echo Canceller: %s\n", vi.version, vi.echo_canceller);

	close(pseudo_fd);

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char *extra_set_hwgain(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int extra_set_hwgain(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int channel;
	int gain;
	int tx;
	struct dahdi_hwgain hwgain;
	struct extra_pvt *tmp = NULL;
	
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "extra set hwgain";
		e->usage =
			"Usage: extra set hwgain <rx|tx> <chan#> <gain>\n"
			"	Sets the hardware gain on a a given channel, overriding the\n"
			"   value provided at module loadtime, whether the channel is in\n"
			"   use or not.  Changes take effect immediately.\n"
			"   <rx|tx> which direction do you want to change (relative to our module)\n"
			"   <chan num> is the channel number relative to the device\n"
			"   <gain> is the gain in dB (e.g. -3.5 for -3.5dB)\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 6)
		return _SHOWUSAGE_;

	if (!strcasecmp("rx", argv[3]))
		tx = 0; /* rx */
	else if (!strcasecmp("tx", argv[3]))
		tx = 1; /* tx */
	else
		return _SHOWUSAGE_;

	channel = atoi(argv[4]);
	gain = atof(argv[5])*10.0;

	ast_mutex_lock(&iflock);

	for (tmp = iflist; tmp; tmp = tmp->next) {

		if (tmp->channel != channel)
			continue;

		if (tmp->subs[SUB_REAL].dfd == -1)
			break;

		hwgain.newgain = gain;
		hwgain.tx = tx;
		if (ioctl(tmp->subs[SUB_REAL].dfd, DAHDI_SET_HWGAIN, &hwgain) < 0) {
			ast_cli(fd, "Unable to set the hardware gain for channel %d: %s\n", channel, strerror(errno));
			ast_mutex_unlock(&iflock);
			return _FAILURE_;
		}
		ast_cli(fd, "hardware %s gain set to %d (%.1f dB) on channel %d\n",
			tx ? "tx" : "rx", gain, (float)gain/10.0, channel);
		break;
	}

	ast_mutex_unlock(&iflock);

	if (tmp)
		return _SUCCESS_;

	ast_cli(fd, "Unable to find given channel %d\n", channel);
	return _FAILURE_;

}

#if (ASTERISK_VERSION_NUM > 10444)
static char *extra_set_swgain(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int extra_set_swgain(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int channel;
	float gain;
	int tx;
	int res;
	struct extra_pvt *tmp = NULL;
	
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)

#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "extra set swgain";
		e->usage =
			"Usage: extra set swgain <rx|tx> <chan#> <gain>\n"
			"	Sets the software gain on a a given channel, overriding the\n"
			"   value provided at module loadtime, whether the channel is in\n"
			"   use or not.  Changes take effect immediately.\n"
			"   <rx|tx> which direction do you want to change (relative to our module)\n"
			"   <chan num> is the channel number relative to the device\n"
			"   <gain> is the gain in dB (e.g. -3.5 for -3.5dB)\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 6)
		return _SHOWUSAGE_;

	if (!strcasecmp("rx", argv[3]))
		tx = 0; /* rx */
	else if (!strcasecmp("tx", argv[3]))
		tx = 1; /* tx */
	else
		return _SHOWUSAGE_;

	channel = atoi(argv[4]);
	gain = atof(argv[5]);

	ast_mutex_lock(&iflock);
	for (tmp = iflist; tmp; tmp = tmp->next) {

		if (tmp->channel != channel)
			continue;

		if (tmp->subs[SUB_REAL].dfd == -1)
			break;

		if (tx)
			res = set_actual_txgain(tmp->subs[SUB_REAL].dfd, gain, tmp->txdrc, tmp->law);
		else
			res = set_actual_rxgain(tmp->subs[SUB_REAL].dfd, gain, tmp->rxdrc, tmp->law);

		if (res) {
			ast_cli(fd, "Unable to set the software gain for channel %d\n", channel);
			ast_mutex_unlock(&iflock);
			return _FAILURE_;
		}

		ast_cli(fd, "software %s gain set to %.1f on channel %d\n",
			tx ? "tx" : "rx", gain, channel);
		break;
	}
	ast_mutex_unlock(&iflock);

	if (tmp)
		return _SUCCESS_;
	
	ast_cli(fd, "Unable to find given channel %d\n", channel);
	return _FAILURE_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static char *extra_set_dnd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
#else  //(ASTERISK_VERSION_NUM > 10444)
static int extra_set_dnd(int fd,int argc, char **argv)
#endif //(ASTERISK_VERSION_NUM > 10444)
{
	int channel;
	int on;
	struct extra_pvt *extra_chan = NULL;
	
#if (ASTERISK_VERSION_NUM > 10444)
	int fd = a->fd;
	const int argc = a->argc;
	const char * const *argv = (const char * const *)a->argv;
#endif //(ASTERISK_VERSION_NUM > 10444)
	
#if (ASTERISK_VERSION_NUM > 10444)
	switch (cmd) {
	case CLI_INIT:
		e->command = "extra set dnd";
		e->usage =
			"Usage: extra set dnd <chan#> <on|off>\n"
			"	Sets/resets DND (Do Not Disturb) mode on a channel.\n"
			"	Changes take effect immediately.\n"
			"	<chan num> is the channel number\n"
			" 	<on|off> Enable or disable DND mode?\n"
			;
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
#endif //(ASTERISK_VERSION_NUM > 10444)

	if (argc != 5)
		return _SHOWUSAGE_;

	if ((channel = atoi(argv[3])) <= 0) {
		ast_cli(fd, "Expected channel number, got '%s'\n", argv[3]);
		return _SHOWUSAGE_;
	}

	if (ast_true(argv[4]))
		on = 1;
	else if (ast_false(argv[4]))
		on = 0;
	else {
		ast_cli(fd, "Expected 'on' or 'off', got '%s'\n", argv[4]);
		return _SHOWUSAGE_;
	}

	ast_mutex_lock(&iflock);
	for (extra_chan = iflist; extra_chan; extra_chan = extra_chan->next) {
		if (extra_chan->channel != channel)
			continue;

		/* Found the channel. Actually set it */
		extra_dnd(extra_chan, on);
		break;
	}
	ast_mutex_unlock(&iflock);

	if (!extra_chan) {
		ast_cli(fd, "Unable to find given channel %d\n", channel);
		return _FAILURE_;
	}

	return _SUCCESS_;
}

#if (ASTERISK_VERSION_NUM > 10444)
static struct ast_cli_entry extra_cli[] = {
	AST_CLI_DEFINE(extra_show_channels, "Show active EXTRA channels"),
	AST_CLI_DEFINE(extra_show_channel, "Show information on a channel"),
	AST_CLI_DEFINE(extra_destroy_channel, "Destroy a channel"),
	AST_CLI_DEFINE(extra_restart_cmd, "Fully restart EXTRA channels"),
	AST_CLI_DEFINE(extra_show_status, "Show all EXTRA cards status"),
	AST_CLI_DEFINE(extra_show_version, "Show the EXTRA version in use"),
	AST_CLI_DEFINE(extra_set_hwgain, "Set hardware gain on a channel"),
	AST_CLI_DEFINE(extra_set_swgain, "Set software gain on a channel"),
	AST_CLI_DEFINE(extra_set_dnd, "Sets/resets DND (Do Not Disturb) mode on a channel"),
};
#else  //(ASTERISK_VERSION_NUM > 10444)
static struct ast_cli_entry extra_cli[] = {
	{ { "extra", "show", "channels", NULL },
	extra_show_channels, "Show active EXTRA channels",
	"Usage: extra show channels [ group <group> | context <context> ]\n"
	"	Shows a list of available channels with optional filtering\n"
	"	<group> must be a number between 0 and 63\n", NULL},
	
	{ { "extra", "show", "channel", NULL },
	extra_show_channel, "Show information on a channel",
	"Usage: extra show channel <chan num>\n"
	"	Detailed information about a given channel\n", NULL},
	
	{ { "extra", "destory", "channel", NULL },
	extra_destroy_channel, "Destroy a channel",
	"Usage: extra destroy channel <chan num>\n"
	"	DON'T USE THIS UNLESS YOU KNOW WHAT YOU ARE DOING.  Immediately removes a given channel, whether it is in use or not\n", NULL},
	
	{ { "extra", "restart", NULL },
	extra_restart_cmd, "Fully restart EXTRA channels",
	"Usage: extra restart\n"
	"	Restarts the EXTRA channels: destroys them all and then\n"
	"	re-reads them from chan_extra.conf.\n"
	"	Note that this will STOP any running CALL on EXTRA channels.\n"
	"", NULL},

	{ { "extra", "show", "status", NULL },
	extra_show_status, "Show all EXTRA cards status",
	"Usage: extra show status\n"
	"       Shows a list of EXTRA cards with status\n", NULL},
	
	{ { "extra", "show", "version", NULL },
	extra_show_version, "Show the EXTRA version in use",
	"Usage: extra show version\n"
	"       Shows the EXTRA version in use\n", NULL},
	
	{ { "extra", "show", "version", NULL },
	extra_set_hwgain, "Set hardware gain on a channel",
	"Usage: extra set hwgain <rx|tx> <chan#> <gain>\n"
	"	Sets the hardware gain on a a given channel, overriding the\n"
	"   value provided at module loadtime, whether the channel is in\n"
	"   use or not.  Changes take effect immediately.\n"
	"   <rx|tx> which direction do you want to change (relative to our module)\n"
	"   <chan num> is the channel number relative to the device\n"
	"   <gain> is the gain in dB (e.g. -3.5 for -3.5dB)\n", NULL},
	
	{ { "extra", "show", "version", NULL },
	extra_set_swgain, "Set software gain on a channel",
	"Usage: extra set swgain <rx|tx> <chan#> <gain>\n"
	"	Sets the software gain on a a given channel, overriding the\n"
	"   value provided at module loadtime, whether the channel is in\n"
	"   use or not.  Changes take effect immediately.\n"
	"   <rx|tx> which direction do you want to change (relative to our module)\n"
	"   <chan num> is the channel number relative to the device\n"
	"   <gain> is the gain in dB (e.g. -3.5 for -3.5dB)\n", NULL},	
			
	{ { "extra", "set", "dnd", NULL },
	extra_set_dnd, "Sets/resets DND (Do Not Disturb) mode on a channel",
	"Usage: extra set dnd <chan#> <on|off>\n"
	"	Sets/resets DND (Do Not Disturb) mode on a channel.\n"
	"	Changes take effect immediately.\n"
	"	<chan num> is the channel number\n"
	" 	<on|off> Enable or disable DND mode?\n", NULL},
};
#endif //(ASTERISK_VERSION_NUM > 10444)

#define TRANSFER	0
#define HANGUP		1

static int __unload_module(void)
{
	struct extra_pvt *p;
		
#ifdef HAVE_GSMAT
	int i;
	for (i = 0; i < NUM_SPANS; i++) {
#ifdef VIRTUAL_TTY
		//printf("====__unload_module  OPVXG4XX_CLEAR_MUX===\n");
		ioctl(gsms[i].fd, OPVXG4XX_CLEAR_MUX, 0);
#endif
		ast_mutex_destroy(&gsms[i].lock);

		if (gsms[i].master != AST_PTHREADT_NULL) 
			pthread_cancel(gsms[i].master);
	}
	ast_cli_unregister_multiple(extra_gsm_cli, ARRAY_LEN(extra_gsm_cli));
#endif

	ast_cli_unregister_multiple(extra_cli, ARRAY_LEN(extra_cli));
	ast_manager_unregister("EXTRADialOffhook");
	ast_manager_unregister("EXTRAHangup");
	ast_manager_unregister("EXTRATransfer");
	ast_manager_unregister("EXTRADNDoff");
	ast_manager_unregister("EXTRADNDon");
	ast_manager_unregister("EXTRAShowChannels");
	ast_manager_unregister("EXTRARestart");
#if (ASTERISK_VERSION_NUM >= 10800)
	ast_data_unregister(NULL);
#endif //(ASTERISK_VERSION_NUM >= 10800)

	ast_unregister_application(app_sendsms);
	ast_unregister_application(app_forwardsms);
	
	ast_channel_unregister(&extra_tech);

	/* Hangup all interfaces if they have an owner */
	ast_mutex_lock(&iflock);
	for (p = iflist; p; p = p->next) {
		if (p->owner)
			ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
	}
	ast_mutex_unlock(&iflock);

	destroy_all_channels();
#ifdef HAVE_GSMAT
	for (i = 0; i < NUM_SPANS; i++) {
		if (gsms[i].master && (gsms[i].master != AST_PTHREADT_NULL))
			pthread_join(gsms[i].master, NULL);
			extra_close_gsm_fd(&(gsms[i]));
	}
	
	//Freedom Add 2011-10-10 11:33
	destroy_cfg_file();
#endif
	ast_cond_destroy(&ss_thread_complete);
#if (ASTERISK_VERSION_NUM >= 100000)
	extra_tech.capabilities = ast_format_cap_destroy(extra_tech.capabilities);
#endif
	return 0;
}

static int unload_module(void)
{
	return __unload_module();
}

static void string_replace(char *str, int char1, int char2)
{
	for (; *str; str++) {
		if (*str == char1) {
			*str = char2;
		}
	}
}

static char *parse_spanchan(char *chanstr, char **subdir)
{
	char *p;

	if ((p = strrchr(chanstr, '!')) == NULL) {
		*subdir = NULL;
		return chanstr;
	}
	*p++ = '\0';
	string_replace(chanstr, '!', '/');
	*subdir = chanstr;
	return p;
}

static int build_channels(struct extra_chan_conf *conf, const char *value, int reload, int lineno, int *found_pseudo)
{
	char *c, *chan;
	char *subdir;
	int x, start, finish;
	struct extra_pvt *tmp;

	if ((reload == 0) && (conf->chan.sig < 0) && !conf->is_sig_auto) {
		ast_log(LOG_ERROR, "Signalling must be specified before any channels are.\n");
		return -1;
	}

	c = ast_strdupa(value);
	c = parse_spanchan(c, &subdir);

	while ((chan = strsep(&c, ","))) {
		if (sscanf(chan, "%30d-%30d", &start, &finish) == 2) {
			/* Range */
		} else if (sscanf(chan, "%30d", &start)) {
			/* Just one */
			finish = start;
		} else if (!strcasecmp(chan, "pseudo")) {
			finish = start = CHAN_PSEUDO;
			if (found_pseudo)
				*found_pseudo = 1;
		} else {
			ast_log(LOG_ERROR, "Syntax error parsing '%s' at '%s'\n", value, chan);
			return -1;
		}
		if (finish < start) {
			ast_log(LOG_WARNING, "Sillyness: %d < %d\n", start, finish);
			x = finish;
			finish = start;
			start = x;
		}

		for (x = start; x <= finish; x++) {
			char fn[PATH_MAX];
			int real_channel = x;

			if (!ast_strlen_zero(subdir)) {
				real_channel = device2chan(subdir, x, fn, sizeof(fn));
				if (real_channel < 0) {
					if (conf->ignore_failed_channels) {
						ast_log(LOG_WARNING, "Failed configuring %s!%d, (got %d). But moving on to others.\n",
							subdir, x, real_channel);
						continue;
					} else {
						ast_log(LOG_ERROR, "Failed configuring %s!%d, (got %d).\n",
							subdir, x, real_channel);
						return -1;
					}
				}
			}
			tmp = mkintf(real_channel, conf, reload);

			if (tmp) {
				ast_verb(3, "%s channel %d, %s signalling\n", reload ? "Reconfigured" : "Registered", real_channel, sig2str(tmp->sig));
			} else {
				ast_log(LOG_ERROR, "Unable to %s channel '%s'\n",
						(reload == 1) ? "reconfigure" : "register", value);
				return -1;
			}
		}
	}

	return 0;
}

/** The length of the parameters list of 'extrachan'.
 * \todo Move definition of MAX_CHANLIST_LEN to a proper place. */
#define MAX_CHANLIST_LEN 80

static void process_echocancel(struct extra_chan_conf *confp, const char *data, unsigned int line)
{
	char *parse = ast_strdupa(data);
	char *params[DAHDI_MAX_ECHOCANPARAMS + 1];
	unsigned int param_count;
	unsigned int x;

	if (!(param_count = ast_app_separate_args(parse, ',', params, ARRAY_LEN(params))))
		return;

	memset(&confp->chan.echocancel, 0, sizeof(confp->chan.echocancel));

	/* first parameter is tap length, process it here */

	x = ast_strlen_zero(params[0]) ? 0 : atoi(params[0]);

	if ((x == 32) || (x == 64) || (x == 128) || (x == 256) || (x == 512) || (x == 1024))
		confp->chan.echocancel.head.tap_length = x;
	else if ((confp->chan.echocancel.head.tap_length = ast_true(params[0])))
		confp->chan.echocancel.head.tap_length = 128;

	/* now process any remaining parameters */

	for (x = 1; x < param_count; x++) {
		struct {
			char *name;
			char *value;
		} param;

		if (ast_app_separate_args(params[x], '=', (char **) &param, 2) < 1) {
			ast_log(LOG_WARNING, "Invalid echocancel parameter supplied at line %d: '%s'\n", line, params[x]);
			continue;
		}

		if (ast_strlen_zero(param.name) || (strlen(param.name) > sizeof(confp->chan.echocancel.params[0].name)-1)) {
			ast_log(LOG_WARNING, "Invalid echocancel parameter supplied at line %d: '%s'\n", line, param.name);
			continue;
		}

		strcpy(confp->chan.echocancel.params[confp->chan.echocancel.head.param_count].name, param.name);

		if (param.value) {
			if (sscanf(param.value, "%30d", &confp->chan.echocancel.params[confp->chan.echocancel.head.param_count].value) != 1) {
				ast_log(LOG_WARNING, "Invalid echocancel parameter value supplied at line %d: '%s'\n", line, param.value);
				continue;
			}
		}
		confp->chan.echocancel.head.param_count++;
	}
}

/*! process_extra() - ignore keyword 'channel' and similar */
#define PROC_DAHDI_OPT_NOCHAN  (1 << 0)
/*! process_extra() - No warnings on non-existing cofiguration keywords */
#define PROC_DAHDI_OPT_NOWARN  (1 << 1)

#if (ASTERISK_VERSION_NUM >= 100000)
static void parse_busy_pattern(struct ast_variable *v, struct ast_dsp_busy_pattern *busy_cadence)
{
	int count_pattern = 0;
	int norval = 0;
	char *temp = NULL;

	for (; ;) {
		/* Scans the string for the next value in the pattern. If none, it checks to see if any have been entered so far. */
		if(!sscanf(v->value, "%30d", &norval) && count_pattern == 0) { 
			ast_log(LOG_ERROR, "busypattern= expects either busypattern=tonelength,quietlength or busypattern=t1length, q1length, t2length, q2length at line %d.\n", v->lineno);
			break;
		}

		busy_cadence->pattern[count_pattern] = norval; 
		
		count_pattern++;
		if (count_pattern == 4) {
			break;
		}

		temp = strchr(v->value, ',');
		if (temp == NULL) {
			break;
		}
		v->value = temp + 1;
	}
	busy_cadence->length = count_pattern;

	if (count_pattern % 2 != 0) { 
		/* The pattern length must be divisible by two */
		ast_log(LOG_ERROR, "busypattern= expects either busypattern=tonelength,quietlength or busypattern=t1length, q1length, t2length, q2length at line %d.\n", v->lineno);
	}
	
}
#endif

static int process_extra(struct extra_chan_conf *confp, const char *cat, struct ast_variable *v, int reload, int options)
{
	struct extra_pvt *tmp;
	int y;
	int found_pseudo = 0;
	char extrachan[MAX_CHANLIST_LEN] = {};

	for (; v; v = v->next) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "channel") || !strcasecmp(v->name, "channels")) {
 			if (options & PROC_DAHDI_OPT_NOCHAN) {
				ast_log(LOG_WARNING, "Channel '%s' ignored.\n", v->value);
 				continue;
			}
			if (build_channels(confp, v->value, reload, v->lineno, &found_pseudo)) {
				if (confp->ignore_failed_channels) {
					ast_log(LOG_WARNING, "Channel '%s' failure ignored: ignore_failed_channels.\n", v->value);
					continue;
				} else {
 					return -1;
				}
			}
			ast_log(LOG_DEBUG, "Channel '%s' configured.\n", v->value);
		} else if (!strcasecmp(v->name, "ignore_failed_channels")) {
			confp->ignore_failed_channels = ast_true(v->value);
		} else if (!strcasecmp(v->name, "buffers")) {
			if (parse_buffers_policy(v->value, &confp->chan.buf_no, &confp->chan.buf_policy)) {
				ast_log(LOG_WARNING, "Using default buffer policy.\n");
				confp->chan.buf_no = numbufs;
				confp->chan.buf_policy = DAHDI_POLICY_IMMEDIATE;
			}
		} else if (!strcasecmp(v->name, "faxbuffers")) {
			if (!parse_buffers_policy(v->value, &confp->chan.faxbuf_no, &confp->chan.faxbuf_policy)) {
				confp->chan.usefaxbuffers = 1;
			}
 		} else if (!strcasecmp(v->name, "extrachan")) {
 			ast_copy_string(extrachan, v->value, sizeof(extrachan));
		} else if (!strcasecmp(v->name, "usecallerid")) {
			confp->chan.use_callerid = ast_true(v->value);
		} else if (!strcasecmp(v->name, "cancallforward")) {
			confp->chan.cancallforward = ast_true(v->value);
		} else if (!strcasecmp(v->name, "relaxdtmf")) {
			if (ast_true(v->value))
				confp->chan.dtmfrelax = DSP_DIGITMODE_RELAXDTMF;
			else
				confp->chan.dtmfrelax = 0;
		} else if (!strcasecmp(v->name, "adsi")) {
			confp->chan.adsi = ast_true(v->value);
		} else if (!strcasecmp(v->name, "canpark")) {
			confp->chan.canpark = ast_true(v->value);
		} else if (!strcasecmp(v->name, "echocancelwhenbridged")) {
			confp->chan.echocanbridged = ast_true(v->value);
		} else if (!strcasecmp(v->name, "busydetect")) {
			confp->chan.busydetect = ast_true(v->value);
		} else if (!strcasecmp(v->name, "busycount")) {
			confp->chan.busycount = atoi(v->value);
		//Freedom Add for music on hold 2012-04-24 15:34
		//////////////////////////////////////////////////////////////////
		} else if (!strcasecmp(v->name, "mohinterpret")
		  ||!strcasecmp(v->name, "musiconhold") || !strcasecmp(v->name, "musicclass")) {
			ast_copy_string(confp->chan.mohinterpret, v->value, sizeof(confp->chan.mohinterpret));
		////////////////////////////////////////////////////////////////
		} else if (!strcasecmp(v->name, "busypattern")) {
#if (ASTERISK_VERSION_NUM >= 100000)
			parse_busy_pattern(v, &confp->chan.busy_cadence);
#else
			if (sscanf(v->value, "%30d,%30d", &confp->chan.busy_tonelength, &confp->chan.busy_quietlength) != 2) {
				ast_log(LOG_ERROR, "busypattern= expects busypattern=tonelength,quietlength at line %d.\n", v->lineno);
			}
#endif
		} else if (!strcasecmp(v->name, "echocancel")) {
			process_echocancel(confp, v->value, v->lineno);
 		} else if (!strcasecmp(v->name, "pulsedial")) {
 			confp->chan.pulse = ast_true(v->value);
		} else if (!strcasecmp(v->name, "callreturn")) {
			confp->chan.callreturn = ast_true(v->value);
		} else if (!strcasecmp(v->name, "context")) {
			ast_copy_string(confp->chan.context, v->value, sizeof(confp->chan.context));
		} else if (!strcasecmp(v->name, "language")) {
			ast_copy_string(confp->chan.language, v->value, sizeof(confp->chan.language));
		} else if (!strcasecmp(v->name, "progzone")) {
			ast_copy_string(progzone, v->value, sizeof(progzone));
		} else if (!strcasecmp(v->name, "stripmsd")) {
			ast_log(LOG_NOTICE, "Configuration option \"%s\" has been deprecated. Please use dialplan instead\n", v->name);
			confp->chan.stripmsd = atoi(v->value);
		} else if (!strcasecmp(v->name, "jitterbuffers")) {
			numbufs = atoi(v->value);
		} else if (!strcasecmp(v->name, "group")) {
			confp->chan.group = ast_get_group(v->value);
#if (ASTERISK_VERSION_NUM > 10444)
		} else if (!strcasecmp(v->name, "setvar")) {
			char *varname = ast_strdupa(v->value), *varval = NULL;
			struct ast_variable *tmpvar;
			if (varname && (varval = strchr(varname, '='))) {
				*varval++ = '\0';
				if ((tmpvar = ast_variable_new(varname, varval, ""))) {
					tmpvar->next = confp->chan.vars;
					confp->chan.vars = tmpvar;
				}
			}
#endif //(ASTERISK_VERSION_NUM > 10444)
		} else if (!strcasecmp(v->name, "immediate")) {
			confp->chan.immediate = ast_true(v->value);
		} else if (!strcasecmp(v->name, "cid_rxgain")) {
			if (sscanf(v->value, "%30f", &confp->chan.cid_rxgain) != 1) {
				ast_log(LOG_WARNING, "Invalid cid_rxgain: %s at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "rxgain")) {
			if (sscanf(v->value, "%30f", &confp->chan.rxgain) != 1) {
				ast_log(LOG_WARNING, "Invalid rxgain: %s at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "txgain")) {
			if (sscanf(v->value, "%30f", &confp->chan.txgain) != 1) {
				ast_log(LOG_WARNING, "Invalid txgain: %s at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "txdrc")) {
			if (sscanf(v->value, "%f", &confp->chan.txdrc) != 1) {
				ast_log(LOG_WARNING, "Invalid txdrc: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "rxdrc")) {
			if (sscanf(v->value, "%f", &confp->chan.rxdrc) != 1) {
				ast_log(LOG_WARNING, "Invalid rxdrc: %s\n", v->value);
			}
		} else if (!strcasecmp(v->name, "tonezone")) {
			if (sscanf(v->value, "%30d", &confp->chan.tonezone) != 1) {
				ast_log(LOG_WARNING, "Invalid tonezone: %s at line %d.\n", v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "callerid")) {
			if (!strcasecmp(v->value, "asreceived")) {
				confp->chan.cid_num[0] = '\0';
				confp->chan.cid_name[0] = '\0';
			} else {
				ast_callerid_split(v->value, confp->chan.cid_name, sizeof(confp->chan.cid_name), confp->chan.cid_num, sizeof(confp->chan.cid_num));
			}
		} else if (!strcasecmp(v->name, "fullname")) {
			ast_copy_string(confp->chan.cid_name, v->value, sizeof(confp->chan.cid_name));
		} else if (!strcasecmp(v->name, "cid_number")) {
			ast_copy_string(confp->chan.cid_num, v->value, sizeof(confp->chan.cid_num));

		} else if (!strcasecmp(v->name, "accountcode")) {
			ast_copy_string(confp->chan.accountcode, v->value, sizeof(confp->chan.accountcode));
		} else if (!strcasecmp(v->name, "amaflags")) {
			y = ast_cdr_amaflags2int(v->value);
			if (y < 0)
				ast_log(LOG_WARNING, "Invalid AMA flags: %s at line %d.\n", v->value, v->lineno);
			else
				confp->chan.amaflags = y;
		} else if (!strcasecmp(v->name, "answeronpolarityswitch")) {
			confp->chan.answeronpolarityswitch = ast_true(v->value);
		} else if (!strcasecmp(v->name, "hanguponpolarityswitch")) {
			confp->chan.hanguponpolarityswitch = ast_true(v->value);
#if (ASTERISK_VERSION_NUM >= 10800)
		} else if (ast_cc_is_config_param(v->name)) {
			ast_cc_set_param(confp->chan.cc_params, v->name, v->value);
#endif //(ASTERISK_VERSION_NUM >= 10800)
		} else if (reload != 1) {
			 if (!strcasecmp(v->name, "signalling") || !strcasecmp(v->name, "signaling")) {
				int orig_radio = confp->chan.radio;
				int orig_outsigmod = confp->chan.outsigmod;
				int orig_auto = confp->is_sig_auto;

				confp->chan.radio = 0;
				confp->chan.outsigmod = -1;
				confp->is_sig_auto = 0;

#ifdef HAVE_GSMAT
				if (!strcasecmp(v->value, "gsm")) {
					confp->chan.sig = SIG_GSM_EXTRA;
					confp->gsm.nodetype = GSM_CPE;
#endif
				} else if (!strcasecmp(v->value, "auto")) {
					confp->is_sig_auto = 1;
				} else {
					confp->chan.outsigmod = orig_outsigmod;
					confp->chan.radio = orig_radio;
					confp->is_sig_auto = orig_auto;
					#ifdef HAVE_GSMAT
						ast_log(LOG_ERROR, "Unknown signalling: HAVE_GSMAT\n");
						ast_log(LOG_ERROR, "Unknown signalling method '%s' at line %d.   1\n", v->value, v->lineno);
					#else
						ast_log(LOG_ERROR, "Unknown signalling: not HAVE_GSMAT\n");
						ast_log(LOG_ERROR, "Unknown signalling method '%s' at line %d.   2\n", v->value, v->lineno);
					#endif
				}
#ifdef HAVE_GSMAT
			} else if (!strcasecmp(v->name, "pin")) {
				ast_copy_string(gsm_modem_pin, v->value, sizeof(gsm_modem_pin));
#ifdef VIRTUAL_TTY
			} else if (!strcasecmp(v->name, "tty")) {
				confp->gsm.virtual_tty = ast_true(v->value);
#endif //VIRTUAL_TTY
			} else if (!strcasecmp(v->name, "debugat")) {
				confp->gsm.debug_at_flag = ast_true(v->value);
			} else if (!strcasecmp(v->name, "vol")) {
				confp->gsm.vol = atoi(v->value);
			} else if (!strcasecmp(v->name, "mic")) {
				confp->gsm.mic = atoi(v->value);
			}else if (!strcasecmp(v->name, "smscodec")) {
				ast_copy_string(confp->gsm.send_sms.coding,v->value,sizeof(confp->gsm.send_sms.coding));
			} else if (!strcasecmp(v->name, "gsmresetinterval")) {
				if (!strcasecmp(v->value, "never"))
					confp->gsm.resetinterval = -1;
				else if (atoi(v->value) >= 60)
					confp->gsm.resetinterval = atoi(v->value);
				else
					ast_log(LOG_WARNING, "'%s' is not a valid reset interval, should be >= 60 seconds or 'never' at line %d.\n",
						v->value, v->lineno);
			} else if (!strcasecmp(v->name, "switchtype")) {
				/*Freedom Modify 2011-10-10 10:11*/
	/*			if (!strcasecmp(v->value, "E169")) 
					confp->gsm.switchtype = GSM_SWITCH_E169;
				else if (!strcasecmp(v->value, "simcom") || !strcasecmp(v->value, "sim340dz"))
					confp->gsm.switchtype = GSM_SWITCH_SIMCOM;
				else if (!strcasecmp(v->value, "em200"))
					confp->gsm.switchtype = GSM_SWITCH_EM200;
				else if (!strcasecmp(v->value, "m20"))
					confp->gsm.switchtype = GSM_SWITCH_M20;
				else if (!strcasecmp(v->value, "sim900"))
					confp->gsm.switchtype = GSM_SWITCH_SIM900;
				else {
					ast_log(LOG_ERROR, "Unknown switchtype '%s' at line %d.\n", v->value, v->lineno);
					return -1;
				}*/
				gsm_set_module_id(&(confp->gsm.switchtype), v->value);
#endif
			} else if (!strcasecmp(v->name, "toneduration")) {
				int toneduration;
				int ctlfd;
				int res;
				struct dahdi_dialparams dps;

				ctlfd = open("/dev/dahdi/ctl", O_RDWR);
				if (ctlfd == -1) {
					ast_log(LOG_ERROR, "Unable to open /dev/dahdi/ctl to set toneduration at line %d.\n", v->lineno);
					return -1;
				}

				toneduration = atoi(v->value);
				if (toneduration > -1) {
					memset(&dps, 0, sizeof(dps));

					dps.dtmf_tonelen = dps.mfv1_tonelen = toneduration;
					res = ioctl(ctlfd, DAHDI_SET_DIALPARAMS, &dps);
					if (res < 0) {
						ast_log(LOG_ERROR, "Invalid tone duration: %d ms at line %d: %s\n", toneduration, v->lineno, strerror(errno));
						close(ctlfd);
						return -1;
					}
				}
				close(ctlfd);
			} else if (!strcasecmp(v->name, "reportalarms")) {
				if (!strcasecmp(v->value, "all"))
					report_alarms = REPORT_CHANNEL_ALARMS | REPORT_SPAN_ALARMS;
				if (!strcasecmp(v->value, "none"))
					report_alarms = 0;
				else if (!strcasecmp(v->value, "channels"))
					report_alarms = REPORT_CHANNEL_ALARMS;
			   else if (!strcasecmp(v->value, "spans"))
					report_alarms = REPORT_SPAN_ALARMS;
			 }
		} else if (!(options & PROC_DAHDI_OPT_NOWARN) )
			ast_log(LOG_WARNING, "Ignoring any changes to '%s' (on reload) at line %d.\n", v->name, v->lineno);
	}
	if (extrachan[0]) {
		/* The user has set 'extrachan' */
		/*< \todo pass proper line number instead of 0 */
		if (build_channels(confp, extrachan, reload, 0, &found_pseudo)) {
			return -1;
		}
	}

	/* mark the first channels of each DAHDI span to watch for their span alarms */
	for (tmp = iflist, y=-1; tmp; tmp = tmp->next) {
		if (!tmp->destroy && tmp->span != y) {
			tmp->manages_span_alarms = 1;
			y = tmp->span; 
		} else {
			tmp->manages_span_alarms = 0;
		}
	}

	/*< \todo why check for the pseudo in the per-channel section.
	 * Any actual use for manual setup of the pseudo channel? */
	if (!found_pseudo && reload != 1) {
		/* use the default configuration for a channel, so
		   that any settings from real configured channels
		   don't "leak" into the pseudo channel config
		*/
		struct extra_chan_conf conf = extra_chan_conf_default();

#if (ASTERISK_VERSION_NUM >= 10800)
		if (conf.chan.cc_params) {
			tmp = mkintf(CHAN_PSEUDO, &conf, reload);
		} else {
			tmp = NULL;
		}
#else  //(ASTERISK_VERSION_NUM >= 10800)
		tmp = mkintf(CHAN_PSEUDO, &conf, reload);
#endif //(ASTERISK_VERSION_NUM >= 10800)
		if (tmp) {
			ast_verb(3, "Automatically generated pseudo channel\n");
		} else {
			ast_log(LOG_WARNING, "Unable to register pseudo channel!\n");
		}
#if (ASTERISK_VERSION_NUM >= 10800)
		ast_cc_config_params_destroy(conf.chan.cc_params);
#endif //(ASTERISK_VERSION_NUM >= 10800)
	}
	return 0;
}

#if (ASTERISK_VERSION_NUM >= 10800)
/*!
 * \internal
 * \brief Deep copy struct extra_chan_conf.
 * \since 1.8
 *
 * \param dest Destination.
 * \param src Source.
 *
 * \return Nothing
 */
static void deep_copy_extra_chan_conf(struct extra_chan_conf *dest, const struct extra_chan_conf *src)
{
	struct ast_cc_config_params *cc_params;

	cc_params = dest->chan.cc_params;
	*dest = *src;
	dest->chan.cc_params = cc_params;
	ast_cc_copy_config_params(dest->chan.cc_params, src->chan.cc_params);
}
#endif //(ASTERISK_VERSION_NUM >= 10800)

/*!
 * \internal
 * \brief Setup DAHDI channel driver.
 *
 * \param reload enum: load_module(0), reload(1), restart(2).
 * \param base_conf Default config parameters.  So cc_params can be properly destroyed.
 * \param conf Local config parameters.  So cc_params can be properly destroyed.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int setup_extra_int(int reload, struct extra_chan_conf *base_conf, struct extra_chan_conf *conf)
{
	struct ast_config *cfg;
	struct ast_config *ucfg;
	struct ast_variable *v;
#if (ASTERISK_VERSION_NUM > 10444)
	struct ast_flags config_flags = { reload == 1 ? CONFIG_FLAG_FILEUNCHANGED : 0 };
#endif //(ASTERISK_VERSION_NUM > 10444)
	const char *cat;
	int res;

#if (ASTERISK_VERSION_NUM > 10444)
	cfg = ast_config_load(config, config_flags);
#else  //(ASTERISK_VERSION_NUM > 10444)
	cfg = ast_config_load(config);
#endif //(ASTERISK_VERSION_NUM > 10444)

	/* Error if we have no config file */
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return 0;
#if (ASTERISK_VERSION_NUM > 10444)
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		ucfg = ast_config_load("users.conf", config_flags);
		if (ucfg == CONFIG_STATUS_FILEUNCHANGED) {
			return 0;
#if (ASTERISK_VERSION_NUM >= 10602)
		} else if (ucfg == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "File users.conf cannot be parsed.  Aborting.\n");
			return 0;
#endif //(ASTERISK_VERSION_NUM >= 10602)
		}
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
#if (ASTERISK_VERSION_NUM >= 10602)
		if ((cfg = ast_config_load(config, config_flags)) == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "File %s cannot be parsed.  Aborting.\n", config);
			ast_config_destroy(ucfg);
			return 0;
		}
#else  //(ASTERISK_VERSION_NUM >= 10602)
		cfg = ast_config_load(config, config_flags);
#endif //(ASTERISK_VERSION_NUM >= 10602)

#if (ASTERISK_VERSION_NUM >= 10602)
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "File %s cannot be parsed.  Aborting.\n", config);
		return 0;
#endif //(ASTERISK_VERSION_NUM >= 10602)
#endif //(ASTERISK_VERSION_NUM > 10444)
	} else {
#if (ASTERISK_VERSION_NUM > 10444)
		ast_clear_flag(&config_flags, CONFIG_FLAG_FILEUNCHANGED);
#endif //(ASTERISK_VERSION_NUM > 10444)
#if (ASTERISK_VERSION_NUM >= 10602)		
		if ((ucfg = ast_config_load("users.conf", config_flags)) == CONFIG_STATUS_FILEINVALID) {
			ast_log(LOG_ERROR, "File users.conf cannot be parsed.  Aborting.\n");
			ast_config_destroy(cfg);
			return 0;
		}
#elif (ASTERISK_VERSION_NUM > 10444)
		ucfg = ast_config_load("users.conf", config_flags);
#else  //(ASTERISK_VERSION_NUM >= 10444)
		ucfg = ast_config_load("users.conf");
#endif //(ASTERISK_VERSION_NUM >= 10602)
	}

	/* It's a little silly to lock it, but we might as well just to be sure */
	ast_mutex_lock(&iflock);

	v = ast_variable_browse(cfg, "channels");
	if ((res = process_extra(base_conf, "", v, reload, 0))) {
		ast_mutex_unlock(&iflock);
		ast_config_destroy(cfg);
		if (ucfg) {
			ast_config_destroy(ucfg);
		}
		return res;
	}

	/* Now get configuration from all normal sections in chan_extra.conf: */
	for (cat = ast_category_browse(cfg, NULL); cat ; cat = ast_category_browse(cfg, cat)) {
		/* [channels] and [trunkgroups] are used. Let's also reserve
		 * [globals] and [general] for future use
		 */
		if (!strcasecmp(cat, "general") ||
			!strcasecmp(cat, "trunkgroups") ||
			!strcasecmp(cat, "globals") ||
			!strcasecmp(cat, "channels")) {
			continue;
		}

		/* Copy base_conf to conf. */
#if (ASTERISK_VERSION_NUM >= 10800)
		deep_copy_extra_chan_conf(conf, base_conf);
#else  //(ASTERISK_VERSION_NUM >= 10800)
		memcpy(&conf, &base_conf, sizeof(conf));
#endif //(ASTERISK_VERSION_NUM >= 10800)

		if ((res = process_extra(conf, cat, ast_variable_browse(cfg, cat), reload, PROC_DAHDI_OPT_NOCHAN))) {
			ast_mutex_unlock(&iflock);
			ast_config_destroy(cfg);
			if (ucfg) {
				ast_config_destroy(ucfg);
			}
			return res;
		}
	}

	ast_config_destroy(cfg);

	if (ucfg) {
		const char *chans;

		process_extra(base_conf, "", ast_variable_browse(ucfg, "general"), 1, 0);

		for (cat = ast_category_browse(ucfg, NULL); cat ; cat = ast_category_browse(ucfg, cat)) {
			if (!strcasecmp(cat, "general")) {
				continue;
			}

			chans = ast_variable_retrieve(ucfg, cat, "extrachan");

			if (ast_strlen_zero(chans)) {
				continue;
			}

			/* Copy base_conf to conf. */
#if (ASTERISK_VERSION_NUM >= 10800)
			deep_copy_extra_chan_conf(conf, base_conf);
#else  //(ASTERISK_VERSION_NUM >= 10800)
			memcpy(&conf, &base_conf, sizeof(conf));
#endif //(ASTERISK_VERSION_NUM >= 10800)

			if ((res = process_extra(conf, cat, ast_variable_browse(ucfg, cat), reload, PROC_DAHDI_OPT_NOCHAN | PROC_DAHDI_OPT_NOWARN))) {
				ast_config_destroy(ucfg);
				ast_mutex_unlock(&iflock);
				return res;
			}
		}
		ast_config_destroy(ucfg);
	}
	ast_mutex_unlock(&iflock);

#ifdef HAVE_GSMAT
	if (reload != 1) {
		int x;
		for (x = 0; x < NUM_SPANS; x++) {
			if (gsms[x].pvt) {
				if (start_gsm(gsms + x)) {
					ast_log(LOG_ERROR, "Unable to start D-channel on span %d\n", x + 1);
					return -1;
				} else
					ast_verb(2, "Starting D-Channel on span %d\n", x + 1);
			}
		}
	}
#endif

	return 0;
}

/*!
 * \internal
 * \brief Setup DAHDI channel driver.
 *
 * \param reload enum: load_module(0), reload(1), restart(2).
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int setup_extra(int reload)
{
	int res;
	struct extra_chan_conf base_conf = extra_chan_conf_default();
	struct extra_chan_conf conf = extra_chan_conf_default();
	
#if (ASTERISK_VERSION_NUM >= 10800)
	if (base_conf.chan.cc_params && conf.chan.cc_params) {
		res = setup_extra_int(reload, &base_conf, &conf);
	} else {
		res = -1;
	}
	ast_cc_config_params_destroy(base_conf.chan.cc_params);
	ast_cc_config_params_destroy(conf.chan.cc_params);
#else  //(ASTERISK_VERSION_NUM >= 10800)
	res = setup_extra_int(reload, &base_conf, &conf);
#endif //(ASTERISK_VERSION_NUM >= 10800)

	return res;
}

#if (ASTERISK_VERSION_NUM >= 10800)
/*!
 * \internal
 * \brief Callback used to generate the extra status tree.
 * \param[in] search The search pattern tree.
 * \retval NULL on error.
 * \retval non-NULL The generated tree.
 */
static int extra_status_data_provider_get(const struct ast_data_search *search,
		struct ast_data *data_root)
{
	int ctl, res, span;
	struct ast_data *data_span, *data_alarms;
	struct dahdi_spaninfo s;

	ctl = open("/dev/dahdi/ctl", O_RDWR);
	if (ctl < 0) {
		ast_log(LOG_ERROR, "No EXTRA found. Unable to open /dev/dahdi/ctl: %s\n", strerror(errno));
		return -1;
	}
	for (span = 1; span < DAHDI_MAX_SPANS; ++span) {
		s.spanno = span;
		res = ioctl(ctl, DAHDI_SPANSTAT, &s);
		if (res) {
			continue;
		}

		data_span = ast_data_add_node(data_root, "span");
		if (!data_span) {
			continue;
		}
		ast_data_add_str(data_span, "description", s.desc);

		/* insert the alarms status */
		data_alarms = ast_data_add_node(data_span, "alarms");
		if (!data_alarms) {
			continue;
		}

		ast_data_add_bool(data_alarms, "BLUE", s.alarms & DAHDI_ALARM_BLUE);
		ast_data_add_bool(data_alarms, "YELLOW", s.alarms & DAHDI_ALARM_YELLOW);
		ast_data_add_bool(data_alarms, "RED", s.alarms & DAHDI_ALARM_RED);
		ast_data_add_bool(data_alarms, "LOOPBACK", s.alarms & DAHDI_ALARM_LOOPBACK);
		ast_data_add_bool(data_alarms, "RECOVER", s.alarms & DAHDI_ALARM_RECOVER);
		ast_data_add_bool(data_alarms, "NOTOPEN", s.alarms & DAHDI_ALARM_NOTOPEN);

		ast_data_add_int(data_span, "irqmisses", s.irqmisses);
		ast_data_add_int(data_span, "bpviol", s.bpvcount);
		ast_data_add_int(data_span, "crc4", s.crc4count);
		ast_data_add_str(data_span, "framing",	s.lineconfig & DAHDI_CONFIG_D4 ? "D4" :
							s.lineconfig & DAHDI_CONFIG_ESF ? "ESF" :
							s.lineconfig & DAHDI_CONFIG_CCS ? "CCS" :
							"CAS");
		ast_data_add_str(data_span, "coding",	s.lineconfig & DAHDI_CONFIG_B8ZS ? "B8ZS" :
							s.lineconfig & DAHDI_CONFIG_HDB3 ? "HDB3" :
							s.lineconfig & DAHDI_CONFIG_AMI ? "AMI" :
							"Unknown");
		ast_data_add_str(data_span, "options",	s.lineconfig & DAHDI_CONFIG_CRC4 ?
							s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "CRC4/YEL" : "CRC4" :
							s.lineconfig & DAHDI_CONFIG_NOTOPEN ? "YEL" : "");
		ast_data_add_str(data_span, "lbo", lbostr[s.lbo]);

		/* if this span doesn't match remove it. */
		if (!ast_data_search_match(search, data_span)) {
			ast_data_remove_node(data_root, data_span);
		}
	}
	close(ctl);

	return 0;
}

/*!
 * \internal
 * \brief Callback used to generate the extra channels tree.
 * \param[in] search The search pattern tree.
 * \retval NULL on error.
 * \retval non-NULL The generated tree.
 */
static int extra_channels_data_provider_get(const struct ast_data_search *search,
		struct ast_data *data_root)
{
	struct extra_pvt *tmp;
	struct ast_data *data_channel;

	ast_mutex_lock(&iflock);
	for (tmp = iflist; tmp; tmp = tmp->next) {
		data_channel = ast_data_add_node(data_root, "channel");
		if (!data_channel) {
			continue;
		}

		ast_data_add_structure(extra_pvt, data_channel, tmp);

		/* if this channel doesn't match remove it. */
		if (!ast_data_search_match(search, data_channel)) {
			ast_data_remove_node(data_root, data_channel);
		}
	}
	ast_mutex_unlock(&iflock);

	return 0;
}

/*!
 * \internal
 * \brief Callback used to generate the extra channels tree.
 * \param[in] search The search pattern tree.
 * \retval NULL on error.
 * \retval non-NULL The generated tree.
 */
static int extra_version_data_provider_get(const struct ast_data_search *search,
		struct ast_data *data_root)
{
	int pseudo_fd = -1;
	struct dahdi_versioninfo vi = {
		.version = "Unknown",
		.echo_canceller = "Unknown"
	};

	if ((pseudo_fd = open("/dev/dahdi/ctl", O_RDONLY)) < 0) {
		ast_log(LOG_ERROR, "Failed to open control file to get version.\n");
		return -1;
	}

	if (ioctl(pseudo_fd, DAHDI_GETVERSION, &vi)) {
		ast_log(LOG_ERROR, "Failed to get EXTRA version: %s\n", strerror(errno));
	}

	close(pseudo_fd);

	ast_data_add_str(data_root, "value", vi.version);
	ast_data_add_str(data_root, "echocanceller", vi.echo_canceller);

	return 0;
}

static const struct ast_data_handler extra_status_data_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = extra_status_data_provider_get
};

static const struct ast_data_handler extra_channels_data_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = extra_channels_data_provider_get
};

static const struct ast_data_handler extra_version_data_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = extra_version_data_provider_get
};

static const struct ast_data_entry extra_data_providers[] = {
	AST_DATA_ENTRY("asterisk/channel/dahdi/status", &extra_status_data_provider),
	AST_DATA_ENTRY("asterisk/channel/dahdi/channels", &extra_channels_data_provider),
	AST_DATA_ENTRY("asterisk/channel/dahdi/version", &extra_version_data_provider)
};
#endif //(ASTERISK_VERSION_NUM >= 10800)

#if (ASTERISK_VERSION_NUM >= 10800)
static int sendsms_exec(struct ast_channel *ast, const char *data)
#else  //(ASTERISK_VERSION_NUM >= 10800)
static int sendsms_exec(struct ast_channel *ast, void *data)
#endif //(ASTERISK_VERSION_NUM >= 10800)
{
	char *find;
	const char *pdata;
	
	int span_num;
	char span[32];
	char dest[512];
	char mesg[1024];
	char id[512];
	char* cmd = "SendSMS(Span,Destination,Message,[ID])";
	
	if (ast_strlen_zero((char *) data)) {
		ast_log(LOG_WARNING, "%s Requires arguments\n",cmd);
		return -1;
	}
	
	//Get span
	//////////////////////////////////////////////////////////////////////////////////
	pdata = data;
	find = strchr(pdata,',');
	if(NULL == find) {
		ast_log(LOG_WARNING, "%s Requires arguments\n",cmd);
		return -1;
	}
	
	if(find-pdata > sizeof(span)) {
		ast_log(LOG_WARNING, "%s span overflow\n",cmd);
		return -1;
	}

	strncpy(span,pdata,find-pdata);
	span[find-pdata]='\0';
	
	span_num = atoi(span);
	if ((span_num < 1) || (span_num > NUM_SPANS)) {
		ast_log(LOG_WARNING, "%s Invalid span '%s'.  Should be a number from %d to %d\n", cmd,span, 1, NUM_SPANS);
		return -1;
	}
	
	if (!gsms[span_num-1].gsm) {
		ast_log(LOG_WARNING, "%s No GSM running on span %d\n", cmd, span_num);
		return -1;
	}
	////////////////////////////////////////////////////////////////////////////////////

	//Get destination
	////////////////////////////////////////////////////////////////////////////////////	
	pdata = find+1;
	find = strchr(pdata,',');
	if(NULL == find) {
		ast_log(LOG_WARNING, "%s Requires arguments\n",cmd);
		return -1;
	}
	
	if(find-pdata > sizeof(dest)) {
		ast_log(LOG_WARNING, "%s description overflow\n",cmd);
		return -1;
	}

	strncpy(dest,pdata,find-pdata);
	dest[find-pdata]='\0';
	////////////////////////////////////////////////////////////////////////////////////
	
	//Get message
	////////////////////////////////////////////////////////////////////////////////////
	pdata = find+1;
	find = strchr(pdata,',');
	if(NULL == find) {
		if(strlen(pdata) > sizeof(mesg)) {
			ast_log(LOG_WARNING, "%s message overflow\n",cmd);
			return -1;
		}
		strncpy(mesg,pdata,sizeof(mesg));
		strcpy(id,"");
	} else {
		if(find-pdata > sizeof(dest)) {
			ast_log(LOG_WARNING, "%s span overflow\n",cmd);
			return -1;
		}
		strncpy(dest,pdata,find-pdata);
		dest[find-pdata]='\0';
		
		//Get ID
		////////////////////////////////////////////////////////////////////////////////////
		pdata = find+1;
		if(strlen(pdata) > sizeof(dest)) {
			ast_log(LOG_WARNING, "%s message overflow\n",cmd);
			return -1;
		}
		strncpy(id,pdata,sizeof(id));
		////////////////////////////////////////////////////////////////////////////////////
	}
	////////////////////////////////////////////////////////////////////////////////////
		
	if ( gsms[span_num-1].dchan ) {
		char pdu[1024];
		const char* char_coding = gsms[span_num-1].send_sms.coding;
		const char* smsc = gsms[span_num-1].send_sms.smsc;
		
		//char_coding = pbx_builtin_getvar_helper(ast,"CHAR_CODING");
		//smsc = pbx_builtin_getvar_helper(ast,"SMSC");

		if(!gsm_encode_pdu_ucs2(smsc,dest,mesg,char_coding,pdu)) {
			ast_log(LOG_WARNING,"Encode pdu error\n");
		}
				
		ast_mutex_lock(&gsms[span_num-1].lock);
		gsm_send_pdu(gsms[span_num-1].gsm, (char*)pdu,mesg,id);
		ast_mutex_unlock(&gsms[span_num-1].lock);
	}
	
	return 0;
}

#if (ASTERISK_VERSION_NUM >= 10800)
static int sendpdu_exec(struct ast_channel *ast, const char *data)
#else  //(ASTERISK_VERSION_NUM >= 10800)
static int sendpdu_exec(struct ast_channel *ast, void *data)
#endif //(ASTERISK_VERSION_NUM >= 10800)
{
	char *find;
	const char *pdata;
	
	int span_num;
	char span[32];
	char pdu[1024];
	char id[512];
	char* cmd = "SendPDU(Span,PDU,[ID])";
	
	if (ast_strlen_zero((char *) data)) {
		ast_log(LOG_WARNING, "%s Requires arguments\n",cmd);
		return -1;
	}
	
	//Get span
	//////////////////////////////////////////////////////////////////////////////////
	pdata = data;
	find = strchr(pdata,',');
	if(NULL == find) {
		ast_log(LOG_WARNING, "%s Requires arguments\n",cmd);
		return -1;
	}
	
	if(find-pdata > sizeof(span)) {
		ast_log(LOG_WARNING, "%s span overflow\n",cmd);
		return -1;
	}

	strncpy(span,pdata,find-pdata);
	span[find-pdata]='\0';
	
	span_num = atoi(span);
	if ((span_num < 1) || (span_num > NUM_SPANS)) {
		ast_log(LOG_WARNING, "%s Invalid span '%s'.  Should be a number from %d to %d\n", cmd, span, 1, NUM_SPANS);
		return -1;
	}
	
	if (!gsms[span_num-1].gsm) {
		ast_log(LOG_WARNING, "%s No GSM running on span %d\n", cmd, span_num);
		return -1;
	}
	////////////////////////////////////////////////////////////////////////////////////
	
	//Get PDU
	////////////////////////////////////////////////////////////////////////////////////
	pdata = find+1;
	find = strchr(pdata,',');
	if(NULL == find) {
		if(strlen(pdata) > sizeof(pdu)) {
			ast_log(LOG_WARNING, "%s message overflow\n",cmd);
			return -1;
		}
		strncpy(pdu,pdata,sizeof(pdu));
		strcpy(id,"");
	} else {
		if(find-pdata > sizeof(pdu)) {
			ast_log(LOG_WARNING, "%s span overflow\n",cmd);
			return -1;
		}
		strncpy(pdu,pdata,find-pdata);
		pdu[find-pdata]='\0';
		
		//Get ID
		////////////////////////////////////////////////////////////////////////////////////
		pdata = find+1;
		if(strlen(pdata) > sizeof(pdu)) {
			ast_log(LOG_WARNING, "%s message overflow\n",cmd);
			return -1;
		}
		strncpy(id,pdata,sizeof(id));
		////////////////////////////////////////////////////////////////////////////////////
	}
	////////////////////////////////////////////////////////////////////////////////////
	
	if ( gsms[span_num-1].dchan ) {
		ast_mutex_lock(&gsms[span_num-1].lock);
		gsm_send_pdu(gsms[span_num-1].gsm, pdu,NULL,id);
		ast_mutex_unlock(&gsms[span_num-1].lock);
	}
	
	return 0;
}

#if (ASTERISK_VERSION_NUM >= 10800)
static int forwardsms_exec(struct ast_channel *ast, const char *data)
#else  //(ASTERISK_VERSION_NUM >= 10800)
static int forwardsms_exec(struct ast_channel *ast, void *data)
#endif //(ASTERISK_VERSION_NUM >= 10800)
{
	char *find;
	const char *pdata;
	
	int span_num;
	char span[32];
	char dest[512];
	char id[512];
	char* cmd = "ForwardSMS(Span,Destination,[ID])";
	
	if (ast_strlen_zero((char *) data)) {
		ast_log(LOG_WARNING, "%s Requires arguments\n",cmd);
		return -1;
	}
	
	//Get span
	//////////////////////////////////////////////////////////////////////////////////
	pdata = data;
	find = strchr(pdata,',');
	if(NULL == find) {
		ast_log(LOG_WARNING, "%s Requires arguments\n",cmd);
		return -1;
	}
	
	if(find-pdata > sizeof(span)) {
		ast_log(LOG_WARNING, "%s span overflow\n",cmd);
		return -1;
	}

	strncpy(span,pdata,find-pdata);
	span[find-pdata]='\0';
	
	span_num = atoi(span);
	if ((span_num < 1) || (span_num > NUM_SPANS)) {
		ast_log(LOG_WARNING, "%s Invalid span '%s'.  Should be a number from %d to %d\n", cmd, span, 1, NUM_SPANS);
		return -1;
	}
	
	if (!gsms[span_num-1].gsm) {
		ast_log(LOG_WARNING, "%s No GSM running on span %d\n", cmd, span_num);
		return -1;
	}
	////////////////////////////////////////////////////////////////////////////////////

	//Get destination
	////////////////////////////////////////////////////////////////////////////////////
	pdata = find+1;
	find = strchr(pdata,',');
	if(NULL == find) {
		if(strlen(pdata) > sizeof(dest)) {
			ast_log(LOG_WARNING, "%s message overflow\n",cmd);
			return -1;
		}
		strncpy(dest,pdata,sizeof(dest));
		strcpy(id,"");
	} else {
		if(find-pdata > sizeof(dest)) {
			ast_log(LOG_WARNING, "%s span overflow\n",cmd);
			return -1;
		}
		strncpy(dest,pdata,find-pdata);
		dest[find-pdata]='\0';
		
		//Get ID
		////////////////////////////////////////////////////////////////////////////////////
		pdata = find+1;
		if(strlen(pdata) > sizeof(dest)) {
			ast_log(LOG_WARNING, "%s message overflow\n",cmd);
			return -1;
		}
		strncpy(id,pdata,sizeof(id));
		////////////////////////////////////////////////////////////////////////////////////
	}
	////////////////////////////////////////////////////////////////////////////////////
		
	if ( gsms[span_num-1].dchan ) {
		char new_pdu[1024];
		const char* pdu;
		const char* smsc;
		pdu = pbx_builtin_getvar_helper(ast,"SMSPDU");
		//smsc = pbx_builtin_getvar_helper(ast,"SMSC");
		smsc = gsms[span_num-1].send_sms.smsc;
		gsm_forward_pdu(pdu,dest,smsc,new_pdu);
		
		ast_mutex_lock(&gsms[span_num-1].lock);
		gsm_send_pdu(gsms[span_num-1].gsm, (char*)new_pdu, NULL, id);
		ast_mutex_unlock(&gsms[span_num-1].lock);
	}

	return 0;
}


static int load_module(void)
{
	int res;
#if (ASTERISK_VERSION_NUM >= 100000)
	struct ast_format tmpfmt;
	if (!(extra_tech.capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_format_cap_add(extra_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR, 0));
	ast_format_cap_add(extra_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0));
	ast_format_cap_add(extra_tech.capabilities, ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0));
#endif	
#ifdef HAVE_GSMAT
	int z;

	//Freedom Add 2011-10-10 11:33
	init_cfg_file();
			
	memset(gsms, 0, sizeof(gsms));
	for (z = 0; z < NUM_SPANS; z++) {
		ast_mutex_init(&gsms[z].lock);
		gsms[z].offset = -1;
		gsms[z].master = AST_PTHREADT_NULL;
		gsms[z].fd = -1;
		//Freedom Modify 2012-09-05 16:01, Default use pdu mode, It's a good way for send SMS
		//gsms[z].send_sms.mode = SEND_SMS_MODE_TXT;
		gsms[z].send_sms.mode = SEND_SMS_MODE_PDU;
		gsms[z].send_sms.smsc[0] = '\0';
		gsms[z].send_sms.coding[0] = '\0'; 
	}
	gsm_set_error(extra_gsm_error);
	gsm_set_message(extra_gsm_message);
#endif

	res = setup_extra(0);
	/* Make sure we can register our EXTRA channel type */
	if (res)
		return AST_MODULE_LOAD_DECLINE;
	if (ast_channel_register(&extra_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'EXTRA'\n");
		__unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}
#ifdef HAVE_GSMAT
	ast_cli_register_multiple(extra_gsm_cli, ARRAY_LEN(extra_gsm_cli));
#endif

	ast_cli_register_multiple(extra_cli, ARRAY_LEN(extra_cli));
	
#if (ASTERISK_VERSION_NUM >= 10800)
	/* register all the data providers */
	ast_data_register_multiple(extra_data_providers, ARRAY_LEN(extra_data_providers));
#endif //(ASTERISK_VERSION_NUM >= 10800)

	ast_register_application(app_sendsms, sendsms_exec, sendsms_synopsis, sendsms_desc);
	ast_register_application(app_sendpdu, sendpdu_exec, sendpdu_synopsis, sendpdu_desc);
	ast_register_application(app_forwardsms, forwardsms_exec, forwardsms_synopsis, forwardsms_desc);
	
	memset(round_robin, 0, sizeof(round_robin));

	ast_cond_init(&ss_thread_complete, NULL);

	return res;
}

static int reload(void)
{
	int res = 0;

	//Freedom Add 2011-10-10 11:33
	destroy_cfg_file();
	init_cfg_file();

	res = setup_extra(1);
	if (res) {
		ast_log(LOG_WARNING, "Reload of chan_extra.so is unsuccessful!\n");
		return -1;
	}
	return 0;
}

/* This is a workaround so that menuselect displays a proper description
 * AST_MODULE_INFO(, , "EXTRA Telephony"
 */

#ifdef AST_MODFLAG_LOAD_ORDER
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, tdesc,
#else  //AST_MODFLAG_LOAD_ORDER
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, tdesc,
#endif //AST_MODFLAG_LOAD_ORDER
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
);

