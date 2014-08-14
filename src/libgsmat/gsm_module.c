/*
 * libgsmat: An implementation of OpenVox G400P GSM/CDMA cards
 *
 * Written by mark.liu <mark.liu@openvox.cn>
 * 
 * Modified by freedom.huang <freedom.huang@openvox.cn>
 * 
 * Copyright (C) 2005-2011 OpenVox Communication Co. Ltd,
 * All rights reserved.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <time.h>

#include "libgsmat.h"
#include "gsm_internal.h"
#include "gsm_module.h"
#include "gsm_config.h"



static char* trim_CRLF( char *String )
{
#define ISCRLF(x) ((x)=='\n'||(x)=='\r'||(x)==' ')

	char *Tail, *Head;
	for ( Tail = String + strlen( String ) - 1; Tail >= String; Tail-- ) {
		if (!ISCRLF( *Tail ))
			break;
	}
	Tail[1] = 0;

	for ( Head = String; Head <= Tail; Head ++ ) {
		if ( !ISCRLF( *Head ) )
			break;
	}

	if ( Head != String )
		memcpy( String, Head, ( Tail - Head + 2 ) * sizeof(char) );

	return String;
}

static void module_get_coverage(struct gsm_modul *gsm, char *h)
{
	int coverage = -1;
	
	if (gsm_compare(h,get_at(gsm->switchtype,AT_CHECK_SIGNAL1))) {
		coverage = get_coverage1(gsm->switchtype,h);
	} else if (gsm_compare(h,get_at(gsm->switchtype,AT_CHECK_SIGNAL2))) {
		coverage = get_coverage2(gsm->switchtype,h);
	}
	if ((coverage) && (coverage != 99)) {
		gsm->coverage = coverage;
	} else {
		gsm->coverage = -1;
	}
}

int module_start(struct gsm_modul *gsm) 
{
	gsm->resetting = 0;
	gsm_switch_state(gsm, GSM_STATE_INIT, get_at(gsm->switchtype,AT_CHECK));
	//gsm_switch_state(gsm, GSM_STATE_UP, get_at(gsm->switchtype,AT_ATZ));
	return 0;
}

int module_restart(struct gsm_modul *gsm) 
{
	gsm->resetting = 1;
	gsm_switch_state(gsm, GSM_STATE_UP, get_at(gsm->switchtype,AT_ATZ));
	return 0;
}

int module_dial(struct gsm_modul *gsm, struct at_call *call) 
{
	char buf[128];

	memset(buf, 0x0, sizeof(buf));
	
	get_dial_str(gsm->switchtype, call->callednum, buf, sizeof(buf));

	gsm_switch_state(gsm, GSM_STATE_CALL_INIT, buf);
	return 0;
}

int module_answer(struct gsm_modul *gsm) 
{
	gsm_switch_state(gsm, GSM_STATE_PRE_ANSWER, get_at(gsm->switchtype,AT_ANSWER));
	
	return 0;
}

int module_senddtmf(struct gsm_modul *gsm, char digit)
{
	char buf[128];
	memset(buf, 0x0, sizeof(buf));
	get_dtmf_str(gsm->switchtype, digit, buf, sizeof(buf));

	return gsm_send_at(gsm, buf);
}

int module_hangup(struct gsm_modul *gsm) 
{
	gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
	
	return 0;
}

static char* get_cur_time(char* buf, int size)
{
	time_t  t;
	struct tm *ptm;
	int len = 0;

	time(&t);

	ptm = localtime(&t);
	//len =  strftime(buf,size, "%Y-%m-%d %H:%M:%S", ptm);
	len =  strftime(buf,size, "%H:%M:%S", ptm);
	buf[len] = '\0';

	return buf;
}

int module_send_ussd(struct gsm_modul *gsm, const char *message) 
{	
	if (gsm->state == GSM_STATE_READY) {
//		char time_buf[20];
		char buf[1024];
/*		get_cur_time(time_buf,20);
		gsm_message(gsm, "Send USSD on span %d at %s\n",gsm->span,time_buf);*/
		snprintf(gsm->ussd_sent_buffer, sizeof(gsm->ussd_sent_buffer) - 1, "%s", message);
		get_ussd_str(gsm->switchtype, message, buf, sizeof(buf));
		gsm_switch_state(gsm, GSM_STATE_USSD_SENDING, buf);
		return 0;
	}
	
	if (gsm->debug & GSM_DEBUG_AT_RECEIVED) {
		gsm_message(gsm, "Cannot send USSD when not ready, waiting...\n");
	}
	
	return -1;
}

int module_send_text(struct gsm_modul *gsm, const char *destination, const char *message) 
{	
	if (gsm->state == GSM_STATE_READY) {
		char time_buf[20];
		get_cur_time(time_buf,20);
		gsm_message(gsm, "Send SMS to %s on span %d at %s\n",gsm->sms_info->txt_info.destination,gsm->span,time_buf);
		snprintf(gsm->sms_sent_buffer, sizeof(gsm->sms_sent_buffer) - 1, "%s", message);
		gsm_switch_state(gsm, GSM_STATE_SMS_SENDING, get_at(gsm->switchtype, AT_SEND_SMS_TEXT_MODE));
		return 0;
	}
	
	if (gsm->debug & GSM_DEBUG_AT_RECEIVED) {
		gsm_message(gsm, "Cannot send SMS when not ready, waiting...\n");
	}
	
	return -1;
}

int module_send_pdu( struct gsm_modul *gsm, const char *pdu)
{	
	if (gsm->state == GSM_STATE_READY) {
		char time_buf[20];
		get_cur_time(time_buf,20);
		gsm_message(gsm, "Send SMS to %s on span %d at %s\n",gsm->sms_info->txt_info.destination,gsm->span,time_buf);
		snprintf(gsm->sms_sent_buffer, sizeof(gsm->sms_sent_buffer) - 1, "%s", pdu);
		gsm_switch_state(gsm, GSM_STATE_SMS_SENDING, get_at(gsm->switchtype, AT_SEND_SMS_PDU_MODE));
		return 0;
	} 
	
	if (gsm->debug & GSM_DEBUG_AT_RECEIVED) {
		gsm_message(gsm, "Cannot send PDU when not ready, waiting...\n");
	}
	
	return -1;
}


int module_send_pin(struct gsm_modul *gsm, const char *pin)
{
	char buf[256];
	/* If the SIM PIN is blocked */
	if (gsm->state == GSM_STATE_SIM_PIN_REQ) {
		
		memset(buf, 0x0, sizeof(buf));
		get_pin_str(gsm->switchtype, pin, buf, sizeof(buf));
		gsm_send_at(gsm, buf);
	}

	return 0;
}


static int parse_ussd_code(struct gsm_modul *gsm,const char* ussd_code)
{
	int response_type;
	char *temp_buffer=NULL;
	temp_buffer=strchr(ussd_code,':');
	if(temp_buffer) {
		if(strchr(temp_buffer,' ')) {
			temp_buffer=strchr(temp_buffer,' ');
			temp_buffer=temp_buffer+1;
		} else {
			temp_buffer=temp_buffer+1;
		}
	} else {
		return -1;
	}

	if(!strstr(temp_buffer,",\""))
		return -1;

	response_type=atoi(temp_buffer);

	char buf[1024];
	char *temp=buf;
	char *end=NULL;
	strncpy(buf, temp_buffer, sizeof(buf));
	memset(&gsm->ev.ussd_received,0,sizeof(gsm_event_ussd_received));
	gsm->ev.ussd_received.ussd_stat=atoi(temp);
	temp=strchr(temp,'"');
	if(!temp)
		return -1;
	else
		temp++;
	end=strrchr(temp,'"');
	if(!end)
		return -1;
	strncpy(gsm->ev.ussd_received.text,temp,end-temp);
	gsm->ev.ussd_received.text[end-temp]='\0';
	//header_len = gsm_hex2int(gsm->ev.ussd_received.text, 2) + 1;
	gsm->ev.ussd_received.len=strlen(gsm->ev.ussd_received.text);
	temp=strchr(end,',');
	if(temp) {
		temp++;
		gsm->ev.ussd_received.ussd_coding=atoi(temp);
	} else {
		gsm->ev.ussd_received.ussd_coding=0;
	}

	return response_type;
}


static gsm_event *module_check_ussd(struct gsm_modul *gsm, char *buf, int i)
{
	int response_type;

	if(gsm->state != GSM_STATE_USSD_SENDING)
		return NULL;

	if(gsm_compare(buf, get_at(gsm->switchtype,AT_CHECK_USSD))) {
		char *error_msg = NULL;
		response_type=parse_ussd_code(gsm,buf);

		//Freedom Modify 2012-10-11 13:42
#if 0
		switch(response_type) {
		case 1:		//Successful;
			break;
		case -1:
			error_msg = "USSD parse failed\n";
		case 0:
			error_msg = "USSD response type: No further action required 0\n";
			break;
		case 2:
			error_msg = "USSD response type: USSD terminated by network 2\n";
			break;
		case 3:
			error_msg = "USSD response type: Other local client has responded 3\n";
			break;
		case 4:
			error_msg = "USSD response type: Operation not supported 4\n";
			break;
		case 5:
			error_msg = "USSD response type: Network timeout 5\n";
			break;
		default:
			error_msg = "CUSD message has unknown response type \n";
			break;
		}
#else
		if( -1 == response_type ) {
			error_msg = "USSD parse failed\n";
		}
#endif

		if(error_msg) {
			gsm->at_last_recv[0] = '\0';
			gsm->at_pre_recv[0] = '\0';
			if (gsm->ussd_info) {
				free(gsm->ussd_info);
				gsm->ussd_info = NULL;
			}
			gsm_switch_state(gsm, GSM_STATE_READY,NULL);
			gsm->ev.e = GSM_EVENT_USSD_SEND_FAILED;
			return &gsm->ev;
        } else { //Successful
			gsm->ev.e = GSM_EVENT_USSD_RECEIVED;
			gsm->at_last_recv[0] = '\0';
			gsm->at_pre_recv[0] = '\0';
			
			if (gsm->ussd_info) {
				free(gsm->ussd_info);
				gsm->ussd_info = NULL;
			}
			gsm_switch_state(gsm, GSM_STATE_READY,NULL);
			return &gsm->ev;
		}		
	} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_CMS_ERROR)) || 
			   gsm_compare(buf, get_at(gsm->switchtype,AT_NO_CARRIER)) ||
				gsm_compare(buf, get_at(gsm->switchtype,AT_CME_ERROR))) {
		gsm_error(gsm, "Error sending USSD (%s) on span %d.\n", buf, gsm->span);
		if (gsm->ussd_info) {
			free(gsm->ussd_info);
			gsm->ussd_info = NULL;
		}
		gsm_switch_state(gsm, GSM_STATE_READY,NULL);
		gsm->ev.e = GSM_EVENT_USSD_SEND_FAILED;
		return &gsm->ev;
	}

	return NULL;
}

static gsm_event *module_check_sms(struct gsm_modul *gsm, char *buf, int i)
{
	int res;
	int compare1;
	int compare2;
	char sms_buf[1024];
		
	compare1 = gsm_compare(gsm->at_last_recv, get_at(gsm->switchtype,AT_CHECK_SMS));
	compare2 = gsm_compare(gsm->at_pre_recv, get_at(gsm->switchtype,AT_CHECK_SMS));

	if (((2 == i) && compare1) || ((1 == i) && compare2)) {
		enum sms_mode mode;
		if (2 == i) {
			mode = gsm_check_sms_mode(gsm, gsm->at_last_recv);
		} else if (1 == i) {
			mode = gsm_check_sms_mode(gsm, gsm->at_pre_recv);		
		}
		
		if (SMS_TEXT == mode) {
		    memcpy(gsm->sms_recv_buffer, gsm->at_last_recv, gsm->at_last_recv_idx);
		
			res = (2 == i) ? gsm_text2sm_event2(gsm, gsm->at_last_recv, gsm->sms_recv_buffer) : \
			                 gsm_text2sm_event2(gsm, gsm->at_pre_recv, gsm->sms_recv_buffer);
			if (!res) {
				gsm->ev.e = GSM_EVENT_SMS_RECEIVED;
				gsm->at_last_recv[0] = '\0';
				return &gsm->ev;
			} else {
				return NULL;
			}
		} else if (SMS_PDU == mode) {
			if (((2 == i) && compare1) || ((1 == i) && compare2)) {
				char *temp_buffer = NULL;
				temp_buffer = strchr(gsm->at_last_recv,',');
				if(temp_buffer) {
					temp_buffer = strstr(temp_buffer,"\r\n");
					if(temp_buffer)
						temp_buffer += 2;
					else
 						temp_buffer = gsm->sms_recv_buffer;
				} else {
					temp_buffer=gsm->sms_recv_buffer;
				}

				int len = strlen(temp_buffer);
				if((temp_buffer[len-2]=='\r')||(temp_buffer[len-2]=='\n')) {
					temp_buffer[len-2]='\0';
				} else if((temp_buffer[len-1]=='\r')||(temp_buffer[len-1]=='\n')) {
					temp_buffer[len-1]='\0';
				}

				strncpy(gsm->sms_recv_buffer, temp_buffer, sizeof(gsm->sms_recv_buffer));
				if (!gsm_pdu2sm_event(gsm, gsm->sms_recv_buffer)) {
					gsm->ev.e = GSM_EVENT_SMS_RECEIVED;
					gsm->at_last_recv[0] = '\0';
					gsm->at_pre_recv[0] = '\0';
					gsm->sms_recv_buffer[0] = '\0';
					return &gsm->ev;
				} else {
					gsm->at_pre_recv[0] = '\0';
					gsm->sms_recv_buffer[0] = '\0';
					return NULL;
				}
			}
		}
	//Freedom del 2011-12-13 17:52
	/////////////////////////////////////////////////////////////////////////////////////
#if 0
	} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_ERROR)) || 
			   gsm_compare(buf, get_at(gsm->switchtype,AT_NO_CARRIER)) ||
			   gsm_compare(buf, get_at(gsm->switchtype,AT_NO_ANSWER))) {
		gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
#endif
	/////////////////////////////////////////////////////////////////////////////////////
	}	
	switch(gsm->state) {
		case GSM_STATE_READY:
			if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
				if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_SEND_SMS_PDU_MODE))) {
	                gsm_send_at(gsm, get_at(gsm->switchtype,AT_UCS2)); /* text to pdu mode */
					gsm->sms_mod_flag = SMS_PDU;
				} else if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_UCS2))) {
					gsm->sms_mod_flag = SMS_PDU;
				}
			}
			break;

		case GSM_STATE_SMS_SENDING:
			if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
				if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_SEND_SMS_PDU_MODE))) {
					gsm_send_at(gsm, get_at(gsm->switchtype,AT_UCS2));
				} else if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_SEND_SMS_TEXT_MODE))) {
					gsm_send_at(gsm, get_at(gsm->switchtype,AT_GSM));
				} else if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_UCS2))) {
					gsm->sms_mod_flag = SMS_PDU;
					memset(sms_buf, 0x0, sizeof(sms_buf));
					if (gsm->sms_info) {
						get_sms_len(gsm->switchtype, gsm->sms_info->pdu_info.len, sms_buf, sizeof(sms_buf));
					}
					gsm_send_at(gsm, sms_buf);
				} else if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_GSM))) {
					gsm->sms_mod_flag = SMS_TEXT;
					memset(sms_buf, 0x0, sizeof(sms_buf));
					if (gsm->sms_info) {
						get_sms_des(gsm->switchtype, gsm->sms_info->txt_info.destination, sms_buf, sizeof(sms_buf));	
					}
					gsm_send_at(gsm, sms_buf);
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);				
				}
			} else if (gsm_compare(buf, "> ") || gsm_compare(gsm->sanbuf, "> ")) {
				gsm_transmit(gsm, gsm->sms_sent_buffer);
				memset(gsm->sms_sent_buffer, 0x0, sizeof(gsm->sms_sent_buffer));
				snprintf(gsm->sms_sent_buffer, sizeof(gsm->sms_sent_buffer) - 1, "%c", 0x1A);
				gsm_switch_state(gsm, GSM_STATE_SMS_SENT, gsm->sms_sent_buffer);
			} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_CMS_ERROR)) || 
					   gsm_compare(buf, get_at(gsm->switchtype,AT_CME_ERROR))) {
				gsm_error(gsm, "Error sending SMS (%s) on span %d.\n", buf, gsm->span);
				if (gsm->sms_info) {
					free(gsm->sms_info);
					gsm->sms_info = NULL;
				}
				gsm->state = GSM_STATE_READY;
            } else {
//                gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
            }
			break;

		case GSM_STATE_SMS_SENT:
			if (gsm_compare(buf, get_at(gsm->switchtype,AT_SEND_SMS_SUCCESS))) {
				//gsm_switch_state(gsm, GSM_STATE_SMS_SENT_END,NULL);
				if(gsm->sms_mod_flag == SMS_PDU) {
					gsm_switch_state(gsm, GSM_STATE_READY,NULL);
					gsm->ev.e = GSM_EVENT_SMS_SEND_OK;
					return &gsm->ev;
				} else {
					gsm_switch_state(gsm, GSM_STATE_SMS_SENT_END,get_at(gsm->switchtype,AT_SEND_SMS_PDU_MODE));
				}
			} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_CMS_ERROR))) {
				gsm_switch_state(gsm, GSM_STATE_READY,NULL);
				gsm->ev.e = GSM_EVENT_SMS_SEND_FAILED;
				return &gsm->ev;
			}
			break;

		case GSM_STATE_SMS_SENT_END:
			if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
				gsm_switch_state(gsm, GSM_STATE_READY,NULL);
				gsm->ev.e = GSM_EVENT_SMS_SEND_OK;
				return &gsm->ev;
			} else {
				gsm_switch_state(gsm, GSM_STATE_READY,NULL);
				gsm->ev.e = GSM_EVENT_SMS_SEND_FAILED;
				return &gsm->ev;
			}
			break;

		default:
			break;
	}
	return NULL;
}

static gsm_event * module_check_network(struct gsm_modul *gsm, struct at_call *call, char *buf, int i)
{
//Freedom Modify 2012-02-16 15:00
//	if (gsm->state < GSM_STATE_READY) {
//Freedom Modify 2012-06-05 15:38
//	if (gsm->state != GSM_STATE_READY) {
	if (gsm->state != GSM_STATE_READY && gsm->state != 	GSM_STATE_NET_REQ) {
		return NULL;
	}

	if (gsm_compare(buf, get_at(gsm->switchtype,AT_CHECK_SIGNAL1))){
		module_get_coverage(gsm, buf);
	} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_CREG))) {
		/*
			0 not registered, ME is not currently searching a new operator to register to
			1 registered, home network
			2 not registered, but ME is currently searching a new operator to register to
			3 registration denied
			4 unknown
			5 registered, roaming
		*/
		trim_CRLF(buf);
		if (!strcmp(buf, get_at(gsm->switchtype,AT_CREG0))) {
			gsm->network = GSM_NET_UNREGISTERED;
			gsm->ev.gen.e = GSM_EVENT_DCHAN_DOWN;
			gsm->coverage		= -1;
			gsm->net_name[0]	= 0x0;
			gsm_switch_state(gsm, GSM_STATE_NET_REQ, get_at(gsm->switchtype,AT_ASK_NET));
			return &gsm->ev;
		} else if (!strcmp(buf, get_at(gsm->switchtype,AT_CREG1))) {
			if(gsm->state == GSM_STATE_NET_REQ) {
				gsm_switch_state(gsm, GSM_STATE_NET_OK, get_at(gsm->switchtype,AT_NET_OK));
			} else {
				gsm->network = GSM_NET_HOME;
				gsm->ev.gen.e = GSM_EVENT_DCHAN_UP;
				return &gsm->ev;
			}
		} else if (!strcmp(buf, get_at(gsm->switchtype,AT_CREG2))) {
			gsm->network = GSM_NET_SEARCHING;
			gsm->ev.gen.e = GSM_EVENT_DCHAN_DOWN;
			gsm->coverage		= -1;
			gsm->net_name[0]	= 0x0;
			gsm_switch_state(gsm, GSM_STATE_NET_REQ, get_at(gsm->switchtype,AT_ASK_NET));
			return &gsm->ev;
		} else if (!strcmp(buf, get_at(gsm->switchtype,AT_CREG3))) {
			gsm->network = GSM_NET_DENIED;
			gsm->ev.gen.e = GSM_EVENT_DCHAN_DOWN;
			gsm->coverage		= -1;
			gsm_switch_state(gsm, GSM_STATE_NET_REQ, get_at(gsm->switchtype,AT_ASK_NET));
			gsm->net_name[0]	= 0x0;
			return &gsm->ev;
		} else if (!strcmp(buf, get_at(gsm->switchtype,AT_CREG4))) {
			gsm->network = GSM_NET_UNKNOWN;
			gsm->ev.gen.e = GSM_EVENT_DCHAN_DOWN;
			gsm->coverage		= -1;
			gsm->net_name[0]	= 0x0;
			gsm_switch_state(gsm, GSM_STATE_NET_REQ, get_at(gsm->switchtype,AT_ASK_NET));
			return &gsm->ev;
		} else if (!strcmp(buf, get_at(gsm->switchtype,AT_CREG5))) {
			if(gsm->state == GSM_STATE_NET_REQ) {
				gsm_switch_state(gsm, GSM_STATE_NET_OK, get_at(gsm->switchtype,AT_NET_OK));
			} else {
				gsm->network = GSM_NET_ROAMING;
				gsm->ev.gen.e = GSM_EVENT_DCHAN_UP;
				return &gsm->ev;
			}
//Freedom del 2012-06-05 17:10
/*		} else {
			gsm->network		= GSM_NET_UNREGISTERED;
			gsm->coverage		= -1;
    		gsm->net_name[0]	= 0x0;
*/
		}
	} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
		if (((2 == i) && (gsm_compare(gsm->at_last_recv, get_at(gsm->switchtype,AT_CHECK_SIGNAL1))))
			  || ((1 == i) && (gsm_compare(gsm->at_pre_recv, get_at(gsm->switchtype,AT_CHECK_SIGNAL1))))) {
			/*	
				i == 2: [ \r\n+CSQ: 25,6\r\n\r\nOK\r\n ]
				i == 1: [ \r\n+CSQ: 25,6\r\n ]
						[ \r\nOK\r\n ]
			*/
//			gsm_transmit(gsm, "AT+CREG?\r\n"); /* Req Net Status */
			if (gsm->coverage < 1) {
				gsm->ev.gen.e = GSM_EVENT_DCHAN_DOWN;
			} else {
				gsm->ev.gen.e = GSM_EVENT_DCHAN_UP;			
			}
			return &gsm->ev;
		} else if (((2 == i) && gsm_compare(gsm->at_last_recv, get_at(gsm->switchtype,AT_CREG)))
		      || ((1 == i) && gsm_compare(gsm->at_pre_recv, get_at(gsm->switchtype,AT_CREG)))) {
			/*if (GSM_NET_UNREGISTERED == gsm->network) {
				gsm->ev.gen.e = GSM_EVENT_DCHAN_DOWN;
				gsm->coverage		= -1;
				gsm->net_name[0]	= 0x0;
				return &gsm->ev;
			} else if (GSM_NET_HOME == gsm->network || GSM_NET_ROAMING == gsm->network) {
				gsm->ev.gen.e = GSM_EVENT_DCHAN_UP;
				return &gsm->ev;	
			}*/
		}
	} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_CHECK_SIGNAL2))) {
		module_get_coverage(gsm, buf);
//		gsm_transmit(gsm, "AT+CREG?\r\n"); /* Req Net Status */
		if (gsm->coverage < 1) {
			gsm->ev.gen.e = GSM_EVENT_NO_SIGNAL;	
		} else {
			gsm->ev.gen.e = GSM_EVENT_DCHAN_UP;		
		}
		return &gsm->ev;
//Freedom Add 2012-06-05 15:21 adjust network state
//////////////////////////////////////////////////////////////////////////////////////////////////////
	} else if (((2 == i) && gsm_compare(gsm->at_last_recv, get_at(gsm->switchtype,AT_CREG)))
		      || ((1 == i) && gsm_compare(gsm->at_pre_recv, get_at(gsm->switchtype,AT_CREG)))) {
			/*if (GSM_NET_UNREGISTERED == gsm->network) {
				gsm->ev.gen.e = GSM_EVENT_DCHAN_DOWN;
				gsm->coverage		= -1;
				gsm->net_name[0]	= 0x0;
				return &gsm->ev;
			} else if (GSM_NET_HOME == gsm->network || GSM_NET_ROAMING == gsm->network) {
				gsm->ev.gen.e = GSM_EVENT_DCHAN_UP;
				return &gsm->ev;	
			}*/
	}
//////////////////////////////////////////////////////////////////////////////////////////////////////

	return NULL;
}

#ifdef VIRTUAL_TTY
int module_mux_end(struct gsm_modul *gsm, int restart_at_flow)
{
	if(restart_at_flow) {
		//Restart AT command flow again, Because after execute "AT+CMUX=0" or clear MUX mode, something need reinitialize.
		gsm->already_set_mux_mode = 1;
		return gsm_switch_state(gsm, GSM_STATE_UP, get_at(gsm->switchtype,AT_ATZ));
	} else {
		return gsm_switch_state(gsm, GSM_STATE_READY, get_at(gsm->switchtype,AT_NET_NAME));
	}
}
#endif //VIRTUAL_TTY

gsm_event *module_receive(struct gsm_modul *gsm, char *data, int len)
{
	struct at_call *call;
	char buf[1024];
	char receivebuf[1024];
	char *p;
	gsm_event* res_event=NULL;
	int i;

	//Freedom Add 2012-02-07 15:24
	/////////////////////////////////////////////////////////
#if SIMCOM900D_NO_ANSWER_BUG
	static int first = 1;
	static struct timeval start_time,end_time;
#endif //SIMCOM900D_NO_ANSWER_BUG
	//////////////////////////////////////////////////////////

	/* get ast_call */
	call = gsm_getcall(gsm, gsm->cref, 0);
	if (!call) {
		gsm_error(gsm, "Unable to locate call %d\n", gsm->cref);
		return NULL;
	}

	strncpy(receivebuf, data, sizeof(receivebuf));
	p = receivebuf;
	i = 0;

	while (1) {	
		len = gsm_san(gsm, p, buf, len);
		if (0 == len || -1 == len) {
			return NULL;
		}

		if (gsm->debug & GSM_DEBUG_AT_RECEIVED) {
			char tmp[1024];
			gsm_trim(gsm->at_last_sent, strlen(gsm->at_last_sent), tmp, sizeof(tmp));
			if (-3 == len) {
				gsm_message(gsm, "\t\t%d:<<< %d %s -- %s , NULL\n", gsm->span, i, tmp, buf);		
			}
			gsm_message(gsm, "\t\t%d:<<< %d %s -- %s , %d\n", gsm->span, i, tmp, buf, len);
		}

		strncpy(p, gsm->sanbuf, sizeof(receivebuf));
		len = (-3 == len) ? 0: gsm->sanidx;
		gsm->sanidx = 0;
#ifdef AUTO_SIM_CHECK
		if (gsm_compare(buf,get_at(gsm->switchtype,AT_CALL_READY))){
			gsm->ev.e = GSM_EVENT_SIM_INSERTED;
			return &gsm->ev;
		}
		else if (gsm_compare(buf,get_at(gsm->switchtype,AT_PIN_NOT_READY))){
			memset(gsm->imsi,0,sizeof(gsm->imsi));
			memset(gsm->sim_smsc,0,sizeof(gsm->sim_smsc));
			memset(gsm->net_name,0,sizeof(gsm->net_name));
			gsm->network=GSM_NET_UNREGISTERED;
			gsm_switch_state(gsm, GSM_STATE_SIM_READY_REQ, get_at(gsm->switchtype,AT_ASK_PIN));
			gsm->ev.e = GSM_EVENT_SIM_FAILED;
			return &gsm->ev;
		}
#endif
		switch (gsm->state) {
			case GSM_STATE_MANUFACTURER_REQ:
				/* Request manufacturer identification */
				if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_GET_MANUFACTURER))) {
					if (gsm_compare(buf,get_at(gsm->switchtype,AT_OK))) {
						gsm_switch_state(gsm, GSM_STATE_VERSION_REQ, get_at(gsm->switchtype,AT_GET_VERSION));
					} else if (!gsm_compare(buf, get_at(gsm->switchtype,AT_CME_ERROR))) {
						gsm_get_manufacturer(gsm, buf);
					} else {
						//Freedom del 2012-06-05 15:50
						//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;
			
			case GSM_STATE_VERSION_REQ:
				if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_GET_VERSION))) {
					if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
						gsm_switch_state(gsm, GSM_STATE_IMEI_REQ, get_at(gsm->switchtype,AT_GET_IMEI));
					} else if (!gsm_compare(buf, get_at(gsm->switchtype,AT_CME_ERROR))) {
						gsm_get_model_version(gsm, buf+9);
					} else {
						//Freedom del 2012-06-05 15:50
						//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

			case GSM_STATE_IMEI_REQ:
				/* IMEI (International Mobile Equipment Identification). */
				if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_GET_IMEI))) {
					if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
#ifdef AUTO_SIM_CHECK
						gsm_switch_state(gsm, GSM_ENABLE_SIM_DETECT, get_at(gsm->switchtype,AT_SIM_DECTECT));
#else
						gsm_switch_state(gsm, GSM_STATE_SIM_READY_REQ, get_at(gsm->switchtype,AT_ASK_PIN));
#endif
					} else if (!gsm_compare(buf, get_at(gsm->switchtype,AT_CME_ERROR))) {
						gsm_get_imei(gsm, buf);
					} else {
						//Freedom del 2012-06-05 15:50
						//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

#ifdef AUTO_SIM_CHECK
			case GSM_ENABLE_SIM_DETECT:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					gsm_switch_state(gsm, GSM_STATE_SIM_READY_REQ, get_at(gsm->switchtype,AT_ASK_PIN));
				} else{
					gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
					gsm_switch_state(gsm, GSM_STATE_SIM_READY_REQ, get_at(gsm->switchtype,AT_ASK_PIN));
				}
				break;	
#endif

			case GSM_STATE_SIM_READY_REQ:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_PIN_READY))) {
					//FreedomDebug 2011-12-13 17:51
					//gsm_switch_sim_state(gsm, GSM_STATE_SIM_PIN_REQ, NULL);
					gsm_switch_sim_state(gsm, GSM_STATE_SIM_READY, NULL);
				} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_PIN_SIM))) { /* waiting for SIM PIN */
					gsm_switch_sim_state(gsm, GSM_STATE_SIM_PIN_REQ, NULL);
				} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					switch(gsm->sim_state) {
						case GSM_STATE_SIM_READY:
							gsm_switch_state(gsm, GSM_STATE_IMSI_REQ, get_at(gsm->switchtype,AT_IMSI));
							break;
						case GSM_STATE_SIM_PIN_REQ:
							gsm_switch_state(gsm, GSM_STATE_SIM_PIN_REQ, NULL);
							gsm->ev.e = GSM_EVENT_PIN_REQUIRED;
							return &gsm->ev;
							break;
					}/*+CME ERROR:incorrect passwo!*/
				} else if(gsm_compare(buf, get_at(gsm->switchtype,AT_CME_ERROR))) {
					/* Todo: other code support */
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
					gsm->ev.e = GSM_EVENT_SIM_FAILED;
					return &gsm->ev;
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;
			case GSM_STATE_SIM_PIN_REQ:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) { /* \r\n+CPIN: SIM PIN\r\n\r\nOK\r\n */
					/* gsm_message(gsm, "LAST>%s<\n",gsm->lastcmd); */
					gsm_switch_state(gsm, GSM_STATE_IMSI_REQ, get_at(gsm->switchtype,AT_IMSI));
				} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_SIM_NO_INSERTED))) {

					//Freedom Del 2011-12-13 18:47
					//////////////////////////////////////////////////////////
#if 0
					/* re-enter SIM if neccessary */
					gsm->state = GSM_STATE_SIM_PIN_REQ;
					gsm->ev.e = GSM_EVENT_PIN_REQUIRED;
					return &gsm->ev;
#endif
					//////////////////////////////////////////////////////////
					gsm->ev.e = GSM_EVENT_SIM_FAILED;
					return &gsm->ev;
				} else if(gsm_compare(buf, get_at(gsm->switchtype,AT_CME_ERROR))){
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
					gsm->ev.e = GSM_EVENT_PIN_ERROR;
					return &gsm->ev;
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

			case GSM_STATE_IMSI_REQ:
				/* IMSI (International Mobile Subscriber Identity number). */
				if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_IMSI))) {
					if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
						if(get_at(gsm->switchtype,AT_MOC_ENABLED))
						{
							gsm_switch_state(gsm, GSM_STATE_MOC_STATE_ENABLED, get_at(gsm->switchtype,AT_MOC_ENABLED));
						}
						else if(get_at(gsm->switchtype,AT_SET_SIDE_TONE))
						{
							gsm_switch_state(gsm, GSM_STATE_SET_SIDE_TONE, get_at(gsm->switchtype,AT_SET_SIDE_TONE));
						}
						else
						{
							gsm_switch_state(gsm, GSM_STATE_CLIP_ENABLED, get_at(gsm->switchtype,AT_CLIP_ENABLED));
						}
					} else if (!gsm_compare(buf, get_at(gsm->switchtype,AT_CME_ERROR))) {
						gsm_get_imsi(gsm, buf);
					} else {
						//Freedom del 2012-06-05 15:50
						//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

			case GSM_STATE_MOC_STATE_ENABLED:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_MOC_ENABLED))) {
						if(get_at(gsm->switchtype,AT_SET_SIDE_TONE))
						{
							gsm_switch_state(gsm, GSM_STATE_SET_SIDE_TONE, get_at(gsm->switchtype,AT_SET_SIDE_TONE));
						}
						else
						{
							gsm_switch_state(gsm, GSM_STATE_CLIP_ENABLED, get_at(gsm->switchtype,AT_CLIP_ENABLED));
						}
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

			case GSM_STATE_SET_SIDE_TONE:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_SET_SIDE_TONE))) {
						char value[1024];
						char send[1024];
						memset(value,0,sizeof(value));
						memset(send,0,sizeof(send));
						if(gsm->vol >= 0) {
							if(str_check_symbol(get_at(gsm->switchtype, AT_CLVL),value,sizeof(value))) {
								if(str_replace(value,value,sizeof(value),"$VOLUME_LEVEL","%d")) {
									sprintf(send, value,gsm->vol);
									gsm_switch_state(gsm, GSM_STATE_SET_SPEAK_VOL, send);
									break;
								}
							}
						} 

						if(gsm->mic >= 0) {
							if(str_check_symbol(get_at(gsm->switchtype, AT_CMIC),value,sizeof(value))) {
								if(str_replace(value,value,sizeof(value),"$MICROPHONE_LEVEL","%d")) {
									sprintf(send, value,gsm->mic);
									gsm_switch_state(gsm, GSM_STATE_SET_MIC_VOL, send);
									break;
								}
							}
						}

						gsm_switch_state(gsm, GSM_STATE_CLIP_ENABLED, get_at(gsm->switchtype,AT_CLIP_ENABLED));
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

			case GSM_STATE_SET_SPEAK_VOL:
				if (gsm_compare(buf,get_at(gsm->switchtype,AT_OK))) {
					if(gsm->mic >= 0) {
						char value[1024];
						char send[1024];
						memset(value,0,sizeof(value));
						memset(send,0,sizeof(send));
						if(str_check_symbol(get_at(gsm->switchtype, AT_CMIC),value,sizeof(value))) {
							if(str_replace(value,value,sizeof(value),"$MICROPHONE_LEVEL","%d")) {
								sprintf(send, value,gsm->mic);
								gsm_switch_state(gsm, GSM_STATE_SET_MIC_VOL, send);
								break;
							}
						}
					}
					 
					gsm_switch_state(gsm, GSM_STATE_CLIP_ENABLED, get_at(gsm->switchtype,AT_CLIP_ENABLED));
					
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

			case GSM_STATE_SET_MIC_VOL:
				if (gsm_compare(buf,get_at(gsm->switchtype,AT_OK))) {
					gsm_switch_state(gsm, GSM_STATE_CLIP_ENABLED, get_at(gsm->switchtype,AT_CLIP_ENABLED));
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

			case GSM_STATE_CLIP_ENABLED:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_CLIP_ENABLED))) {
						gsm_switch_state(gsm, GSM_STATE_DTR_WAKEUP_DISABLED, get_at(gsm->switchtype,AT_DTR_WAKEUP));
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

				//For SIMCOM5215J, Solve module send "DTR interrupt" AT error
			case GSM_STATE_DTR_WAKEUP_DISABLED:
				if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_DTR_WAKEUP))) {
					gsm_switch_state(gsm, GSM_STATE_RSSI_ENABLED, get_at(gsm->switchtype,AT_RSSI_ENABLED));
				}
				break;

			case GSM_STATE_RSSI_ENABLED:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_RSSI_ENABLED))) {
						gsm_switch_state(gsm, GSM_STATE_SET_NET_URC, get_at(gsm->switchtype,AT_SET_NET_URC));
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;
			
			case GSM_STATE_SET_NET_URC:
				if (gsm_compare(buf,  get_at(gsm->switchtype,AT_OK))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_SET_NET_URC))) {
						gsm_switch_state(gsm, GSM_STATE_NET_REQ, get_at(gsm->switchtype,AT_ASK_NET));
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, "!%s!\n", buf);
				} 
				break;
							
			case GSM_STATE_NET_REQ:
				if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_ASK_NET))) {
					trim_CRLF(buf);
					if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {						
						if ((GSM_NET_HOME == gsm->network) || (GSM_NET_ROAMING == gsm->network)) {
							gsm_switch_state(gsm, GSM_STATE_NET_OK, get_at(gsm->switchtype,AT_NET_OK));
						} else {
							//gsm->ev.gen.e = GSM_EVENT_DCHAN_DOWN;
							//usleep (500000);
							//gsm_switch_state(gsm, GSM_STATE_NET_REQ, get_at(gsm->switchtype,AT_ASK_NET)); /* Req Net Status */
							//return NULL;
							//return &gsm->ev;
						}	/* FIXME ---- Do something better here... use a timer... */
					} else if (!strcmp(buf, get_at(gsm->switchtype,AT_CREG10))) {
						gsm->network = GSM_NET_UNREGISTERED;
					} else if (!strcmp(buf, get_at(gsm->switchtype,AT_CREG11))) {
						gsm->network = GSM_NET_HOME;
					} else if (!strcmp(buf, get_at(gsm->switchtype,AT_CREG12))) {
						gsm->network = GSM_NET_SEARCHING;
					} else if (!strcmp(buf, get_at(gsm->switchtype,AT_CREG13))) {
						gsm->network = GSM_NET_DENIED;
					} else if (!strcmp(buf, get_at(gsm->switchtype,AT_CREG14))) {
						gsm->network = GSM_NET_UNKNOWN;
					} else if (!strcmp(buf, get_at(gsm->switchtype,AT_CREG15))) {
						gsm->network = GSM_NET_ROAMING;
					}else {
						gsm->network		= GSM_NET_UNREGISTERED;
						//gsm->coverage		= -1;
						//gsm->net_name[0]	= 0x0;
						gsm_switch_state(gsm, GSM_STATE_NET_REQ, get_at(gsm->switchtype,AT_ASK_NET));
						return NULL;
					}
				} else {
					if(gsm->ev.gen.e == GSM_EVENT_DCHAN_DOWN) {
						if(gsm->send_at) {
							gsm->send_at = 0;
						}
					} else {
						//Freedom del 2012-06-05 15:50
						//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
					}
				}
				break;

			case GSM_STATE_NET_OK:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_NET_OK))) { /* set only <format> (for read command +COPS?) ¨C not shown in Read command response */
						gsm_switch_state(gsm, GSM_STATE_GET_SMSC_REQ, get_at(gsm->switchtype,AT_GET_SMSC));
					}
				}
				break;

			case GSM_STATE_GET_SMSC_REQ:
				/* Request smsc */
				if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_GET_SMSC))) {
					if (gsm_compare(buf,get_at(gsm->switchtype,AT_OK))) {
						gsm_switch_state(gsm, GSM_STATE_SMS_SET_CHARSET, get_at(gsm->switchtype,AT_SMS_SET_CHARSET));
					} else if (!gsm_compare(buf, get_at(gsm->switchtype,AT_CME_ERROR))) {
						gsm_get_smsc(gsm, buf);
					} else {
						//Freedom del 2012-06-05 15:50
						//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

			case GSM_STATE_SMS_SET_CHARSET:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					if(get_at(gsm->switchtype,AT_MODE)){
						gsm_switch_state(gsm, GSM_AT_MODE, get_at(gsm->switchtype,AT_MODE));
					} else if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_SMS_SET_CHARSET))) {
						gsm_switch_state(gsm, GSM_STATE_SMS_SET_INDICATION, get_at(gsm->switchtype,AT_GSM));
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

			case GSM_AT_MODE:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_MODE))) { /*add by makes 2012-4-10 15:54*/
						gsm_switch_state(gsm, GSM_STATE_SMS_SET_INDICATION, get_at(gsm->switchtype,AT_GSM));
					}
				}
				break;

			case GSM_STATE_SMS_SET_INDICATION:
				if (gsm_compare(buf,get_at(gsm->switchtype,AT_OK))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_GSM))) {
						gsm->sms_mod_flag = SMS_TEXT;
						sleep(1);
						gsm_switch_state(gsm, GSM_STATE_NET_NAME_REQ, get_at(gsm->switchtype,AT_ASK_NET_NAME));
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

			case GSM_STATE_NET_NAME_REQ:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_CHECK_NET))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_ASK_NET_NAME))) {
						gsm_get_operator(gsm, buf);
						UPDATE_OURCALLSTATE(gsm, call, AT_CALL_STATE_NULL);
						call->peercallstate = AT_CALL_STATE_NULL;
#ifdef VIRTUAL_TTY
						if(gsm->already_set_mux_mode) {
							gsm_switch_state(gsm, GSM_STATE_READY, get_at(gsm->switchtype,AT_NET_NAME));
						} else {
							gsm_switch_state(gsm, GSM_INIT_MUX, get_at(gsm->switchtype,AT_CHECK));
						}
#else
						gsm_switch_state(gsm, GSM_STATE_READY, get_at(gsm->switchtype,AT_NET_NAME));
#endif
						if (gsm->retranstimer) {
							gsm_schedule_del(gsm, gsm->retranstimer);
							gsm->retranstimer = 0;
						}
						if (gsm->resetting) {
							gsm->resetting = 0;
							gsm->ev.gen.e = GSM_EVENT_RESTART_ACK;
						} else {
							gsm->ev.gen.e = GSM_EVENT_DCHAN_UP;
						}
						return &gsm->ev;
					}
				} else {
					//Freedom del 2012-06-05 15:50
					//gsm_error(gsm, DEBUGFMT "!%s!,last at tx:[%s]\n", DEBUGARGS, buf,gsm->at_last_sent);
				}
				break;

#ifdef VIRTUAL_TTY
			case GSM_INIT_MUX:
				gsm->ev.gen.e = GSM_EVENT_INIT_MUX;
				return &gsm->ev;
				break;
#endif //VIRTUAL_TTY

			case GSM_STATE_READY:
				/* Request operators */
#ifdef VIRTUAL_TTY
				if(gsm->already_set_mux_mode)
					gsm->already_set_mux_mode=0;
#endif
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_CHECK_NET))) { /* AT+COPS? */
					strncpy(gsm->net_name, buf, sizeof(gsm->net_name));
			//	} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					//Freedom notice must to rewrite this code 2011-10-12 15:13
			//		gsm_message(gsm, "%s\n", data);
				} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_INCOMING_CALL))) { /* Incoming call */
					if (!call->newcall) {
						break;
					}
					call->newcall = 0;

					char caller_id[64];
					/* An unsolicited result code is returned after every RING at a mobile terminating call  */
					/* +CLIP: <number>, <type>,¡±¡±,,<alphaId>,<CLI validity> */
					get_cid(gsm->switchtype,buf,caller_id,sizeof(caller_id));

					/* Set caller number and caller name (if provided) */
					if (!strlen(caller_id)) {
						strncpy(gsm->ev.ring.callingnum, "", sizeof(gsm->ev.ring.callingnum));
					} else {
						strncpy(gsm->ev.ring.callingnum, caller_id, sizeof(gsm->ev.ring.callingnum));
						strncpy(gsm->ev.ring.callingname, caller_id, sizeof(gsm->ev.ring.callingname));
					}

					/* Return ring event */
					UPDATE_OURCALLSTATE(gsm, call, AT_CALL_STATE_CALL_PRESENT);
					call->peercallstate = AT_CALL_STATE_CALL_INITIATED;
					call->alive = 1;
					gsm->state = GSM_STATE_RING;
					gsm->ev.e					= GSM_EVENT_RING;
					gsm->ev.ring.channel		= call->channelno; /* -1 : default */
					gsm->ev.ring.cref			= call->cr;
					gsm->ev.ring.call			= call;
					gsm->ev.ring.layer1			= GSM_LAYER_1_ALAW; /* a law */
					gsm->ev.ring.complete		= call->complete; 
					gsm->ev.ring.progress		= call->progress;
					gsm->ev.ring.progressmask	= call->progressmask;
					gsm->ev.ring.callednum[0]	= '\0';				/* called number should not be existed */ 
					return &gsm->ev;
				} else {
					if(gsm->send_at) {
						gsm_message(gsm, "%s", data);
						gsm->send_at = 0;
					}
				}
				break;

			case GSM_STATE_RING:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_RING))) {
					call->alive = 1;
					gsm->state = GSM_STATE_RINGING;
					gsm->ev.e					= GSM_EVENT_RINGING;
					gsm->ev.ring.channel		= call->channelno;
					gsm->ev.ring.cref			= call->cr;
					gsm->ev.ring.progress		= call->progress;
					gsm->ev.ring.progressmask	= call->progressmask;
					return &gsm->ev;
				}
				break;

			//Freedom Add 2011-12-08 15:32 Check reject call
			////////////////////////////////////////////////////
			case GSM_STATE_RINGING:
				if( gsm_compare(buf, get_at(gsm->switchtype,AT_NO_CARRIER)) ||
					gsm_compare(buf, get_at(gsm->switchtype,AT_NO_ANSWER)) ) {
					
					gsm->state = GSM_STATE_READY;
					UPDATE_OURCALLSTATE(gsm, call, AT_CALL_STATE_NULL);
					call->peercallstate = AT_CALL_STATE_NULL;
					gsm->ev.e = GSM_EVENT_HANGUP;
					gsm->ev.hangup.channel = call->channelno;
					gsm->ev.hangup.cause = GSM_CAUSE_NO_ANSWER;
					gsm->ev.hangup.cref = call->cr;
					gsm->ev.hangup.call = call;
					call->alive = 0;
					call->sendhangupack = 0;
					gsm_destroycall(gsm, call);
					return &gsm->ev;
				}
				break;
			////////////////////////////////////////////////////

			case GSM_STATE_PRE_ANSWER:
				/* Answer the remote calling */
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_ANSWER))) {
						UPDATE_OURCALLSTATE(gsm, call, AT_CALL_STATE_ACTIVE);
						call->peercallstate = AT_CALL_STATE_ACTIVE;
						call->alive = 1;
						gsm->state = GSM_STATE_CALL_ACTIVE;
						gsm->ev.e				= GSM_EVENT_ANSWER;
						gsm->ev.answer.progress	= 0;
						gsm->ev.answer.channel	= call->channelno;
						gsm->ev.answer.cref		= call->cr;
						gsm->ev.answer.call		= call;
						return &gsm->ev;
					}
				}
				break;

			case GSM_STATE_CALL_ACTIVE:
				/* Remote end of active all. Waiting ...*/
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_NO_CARRIER)) || 
					gsm_compare(buf, get_at(gsm->switchtype,AT_NO_ANSWER))) {
					gsm_switch_state(gsm, GSM_STATE_READY, get_at(gsm->switchtype,AT_NET_NAME));
					UPDATE_OURCALLSTATE(gsm, call, AT_CALL_STATE_NULL);
					call->peercallstate = AT_CALL_STATE_NULL;
					call->alive = 0;
					call->sendhangupack = 0;
					gsm->ev.e				= GSM_EVENT_HANGUP;
					gsm->ev.hangup.channel	= 1;
					gsm->ev.hangup.cause	= GSM_CAUSE_NORMAL_CLEARING;
					gsm->ev.hangup.cref		= call->cr;
					gsm->ev.hangup.call		= call;
					gsm_hangup(gsm, call, GSM_CAUSE_NORMAL_CLEARING);
					return &gsm->ev;
				}
				break;

			case GSM_STATE_HANGUP_REQ:
				/* Hangup the active call */
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_HANGUP))) {
						UPDATE_OURCALLSTATE(gsm, call, AT_CALL_STATE_NULL);
						call->peercallstate		= AT_CALL_STATE_NULL;
						call->alive = 0;
						call->sendhangupack = 0;
						gsm->state = GSM_STATE_READY;
						gsm->ev.e				= GSM_EVENT_HANGUP;
						gsm->ev.hangup.channel	= 1;
						gsm->ev.hangup.cause	= GSM_CAUSE_NORMAL_CLEARING; // trabajar el c->cause con el ^CEND
						gsm->ev.hangup.cref		= call->cr;
						gsm->ev.hangup.call		= call;
						return &gsm->ev;
					}
				}
				break;

			case GSM_STATE_CALL_INIT:
				/* After dial */
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
						gsm_send_at(gsm, get_at(gsm->switchtype,AT_CALL_INIT));
						gsm->state = GSM_STATE_CALL_MADE;
				}
				break;

			case GSM_STATE_CALL_MADE:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_CALL_INIT))) {
						call->channelno = 1;
						gsm_switch_state(gsm, GSM_STATE_CALL_PROCEEDING, get_at(gsm->switchtype,AT_CALL_PROCEEDING));
						gsm->ev.e 					= GSM_EVENT_PROCEEDING;
						gsm->ev.proceeding.progress	= 8;
						gsm->ev.proceeding.channel	= call->channelno;
						gsm->ev.proceeding.cause	= 0;
						gsm->ev.proceeding.cref		= call->cr;
						gsm->ev.proceeding.call		= call;
						return &gsm->ev;
					}
				}
				break;

			case GSM_STATE_CALL_PROCEEDING:
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_OK))) {
					if (gsm_compare(gsm->at_last_sent, get_at(gsm->switchtype,AT_CALL_PROCEEDING))) {
						
						//Freedom Add 2012-02-07 15:24
						//////////////////////////////////////////////////////////////////////////
#if SIMCOM900D_NO_ANSWER_BUG
						first = 1;
#endif //SIMCOM900D_NO_ANSWER_BUG
						//////////////////////////////////////////////////////////////////////////
						gsm->state = GSM_STATE_CALL_PROGRESS;
						gsm->ev.proceeding.e		= GSM_EVENT_PROGRESS;
						gsm->ev.proceeding.progress	= 8;
						gsm->ev.proceeding.channel	= call->channelno;
						gsm->ev.proceeding.cause	= 0;
						gsm->ev.proceeding.cref		= call->cr;
						gsm->ev.proceeding.call		= call;
						return &gsm->ev;
					}
				}
				break;

			case GSM_STATE_CALL_PROGRESS:
				//Freedom Add 2012-02-07 15:24
				//////////////////////////////////////////////////////////////////////////
#if SIMCOM900D_NO_ANSWER_BUG
				if(first) {
					first = 0;
					gettimeofday(&start_time,NULL);
				} else {
					gettimeofday(&end_time,NULL);
					if((end_time.tv_sec-start_time.tv_sec) >= 30 ) {
						first = 1;
						gsm_message(gsm,"Dial Timeout\n");
						gsm->state = GSM_STATE_READY;
						UPDATE_OURCALLSTATE(gsm, call, AT_CALL_STATE_NULL);
						call->peercallstate = AT_CALL_STATE_NULL;
						gsm->ev.e = GSM_EVENT_HANGUP;
						gsm->ev.hangup.channel = call->channelno;
						gsm->ev.hangup.cause = GSM_CAUSE_NO_ANSWER;
						gsm->ev.hangup.cref = call->cr;
						gsm->ev.hangup.call = call;
						call->alive = 0;
						call->sendhangupack = 0;
						module_hangup(gsm);
						gsm_destroycall(gsm, call);
						return &gsm->ev;
					}
				}
#endif //SIMCOM900D_NO_ANSWER_BUG
				//////////////////////////////////////////////////////////////////////////
				if (gsm_compare(buf, get_at(gsm->switchtype,AT_RING))) {
					call->alive = 1;
					gsm->ev.gen.e = GSM_EVENT_MO_RINGING;
					gsm->ev.answer.progress = 0;
					gsm->ev.answer.channel = call->channelno;
					gsm->ev.answer.cref = call->cr;
					gsm->ev.answer.call = call;
					return &gsm->ev;
				}
				else if (gsm_compare(buf, get_at(gsm->switchtype,AT_MO_CONNECTED))) {
					call->alive = 1;
					gsm->state = GSM_STATE_CALL_ACTIVE;
					gsm->ev.gen.e = GSM_EVENT_ANSWER;
					gsm->ev.answer.progress = 0;
					gsm->ev.answer.channel = call->channelno;
					gsm->ev.answer.cref = call->cr;
					gsm->ev.answer.call = call;
					return &gsm->ev;
				} 
				break;

#ifdef CONFIG_CHECK_PHONE
			case GSM_STATE_PHONE_CHECK: /*add by makes 2012-04-10 11:03 */
			{
				if(time(NULL)<gsm->check_timeout){
					if (gsm_compare(buf, get_at(gsm->switchtype,AT_RING))){
						if(gsm->auto_hangup_flag)
							gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
						gsm->ev.gen.e = GSM_EVENT_CHECK_PHONE;
						gsm->ev.notify.info = PHONE_RING;
						gsm->phone_stat=PHONE_RING;
						return &gsm->ev;
					} else if(gsm_compare(buf, get_at(gsm->switchtype,AT_BUSY))){
						gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
						gsm->ev.gen.e = GSM_EVENT_CHECK_PHONE;
						gsm->ev.notify.info = PHONE_BUSY;
						gsm->phone_stat=PHONE_BUSY;
						return &gsm->ev;
					} else if(gsm_compare(buf, get_at(gsm->switchtype,AT_MO_CONNECTED))){
						gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
						gsm->ev.gen.e = GSM_EVENT_CHECK_PHONE;
						gsm->ev.notify.info = PHONE_CONNECT;
						gsm->phone_stat=PHONE_CONNECT;
						return &gsm->ev;
					} else if(gsm_compare(buf, get_at(gsm->switchtype,AT_NO_CARRIER))){
						gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
						gsm->ev.gen.e = GSM_EVENT_CHECK_PHONE;
						gsm->ev.notify.info = PHONE_NOT_CARRIER;
						gsm->phone_stat=PHONE_NOT_CARRIER;
						return &gsm->ev;
					} else if(gsm_compare(buf, get_at(gsm->switchtype,AT_NO_ANSWER))){
						gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
						gsm->ev.gen.e = GSM_EVENT_CHECK_PHONE;
						gsm->ev.notify.info = PHONE_NOT_ANSWER;
						gsm->phone_stat=PHONE_NOT_ANSWER;
						return &gsm->ev;
					} else if(gsm_compare(buf, get_at(gsm->switchtype,AT_NO_DIALTONE))){
						gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
						gsm->ev.gen.e = GSM_EVENT_CHECK_PHONE;
						gsm->ev.notify.info = PHONE_NOT_DIALTONE;
						gsm->phone_stat=PHONE_NOT_DIALTONE;
						return &gsm->ev;
					}
				} else {
					if(gsm->auto_hangup_flag) {
						gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
						gsm->ev.gen.e = GSM_EVENT_CHECK_PHONE;
						gsm->ev.notify.info = PHONE_TIMEOUT;
						gsm->phone_stat=PHONE_TIMEOUT;
						return &gsm->ev;
					} else {
						if(gsm_compare(buf, get_at(gsm->switchtype,AT_BUSY))){
							gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
							gsm->ev.gen.e = GSM_EVENT_CHECK_PHONE;
							gsm->ev.notify.info = PHONE_BUSY;
							gsm->phone_stat=PHONE_BUSY;
							return &gsm->ev;
						} else if(gsm_compare(buf, get_at(gsm->switchtype,AT_MO_CONNECTED))){
							gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
							gsm->ev.gen.e = GSM_EVENT_CHECK_PHONE;
							gsm->ev.notify.info = PHONE_CONNECT;
							gsm->phone_stat=PHONE_CONNECT;
							return &gsm->ev;
						} else if(gsm_compare(buf, get_at(gsm->switchtype,AT_NO_CARRIER))){
							gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
							gsm->ev.gen.e = GSM_EVENT_CHECK_PHONE;
							gsm->ev.notify.info = PHONE_NOT_CARRIER;
							gsm->phone_stat=PHONE_NOT_CARRIER;
							return &gsm->ev;
						} else if(gsm_compare(buf, get_at(gsm->switchtype,AT_NO_ANSWER))){
							gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
							gsm->ev.gen.e = GSM_EVENT_CHECK_PHONE;
							gsm->ev.notify.info = PHONE_NOT_ANSWER;
							gsm->phone_stat=PHONE_NOT_ANSWER;
							return &gsm->ev;
						} else if(gsm_compare(buf, get_at(gsm->switchtype,AT_NO_DIALTONE))){
							gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
							gsm->ev.gen.e = GSM_EVENT_CHECK_PHONE;
							gsm->ev.notify.info = PHONE_NOT_DIALTONE;
							gsm->phone_stat=PHONE_NOT_DIALTONE;
							return &gsm->ev;
						}
					}
				}
				break;
			}
#endif
			default:
				break;
		}

		i ++;
				
		if(gsm->state >= GSM_STATE_READY) {
			if(gsm_compare(buf, get_at(gsm->switchtype,AT_NO_CARRIER)) ||
				gsm_compare(buf, get_at(gsm->switchtype,AT_NO_ANSWER))) {
				gsm->state = GSM_STATE_READY;
				UPDATE_OURCALLSTATE(gsm, call, AT_CALL_STATE_NULL);
				call->peercallstate = AT_CALL_STATE_NULL;
				gsm->ev.e = GSM_EVENT_HANGUP;
				gsm->ev.hangup.channel = call->channelno;
				gsm->ev.hangup.cause = GSM_CAUSE_NO_ANSWER;
				gsm->ev.hangup.cref = call->cr;
				gsm->ev.hangup.call = call;
				call->alive = 0;
				call->sendhangupack = 0;
				gsm_destroycall(gsm, call);
				return &gsm->ev;
			} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_NO_DIALTONE))) {
				gsm->state = GSM_STATE_READY;
				UPDATE_OURCALLSTATE(gsm, call, AT_CALL_STATE_NULL);
				call->peercallstate = AT_CALL_STATE_NULL;
				gsm->ev.e = GSM_EVENT_HANGUP;
				gsm->ev.hangup.cause = GSM_CAUSE_NETWORK_OUT_OF_ORDER;
				gsm->ev.hangup.cref = call->cr;
				gsm->ev.hangup.call = call;
				call->alive = 0;
				call->sendhangupack = 0;
				gsm_destroycall(gsm, call);
				return &gsm->ev;
			} else if (gsm_compare(buf, get_at(gsm->switchtype,AT_BUSY))) {
				gsm->state = GSM_STATE_READY;
				UPDATE_OURCALLSTATE(gsm, call, AT_CALL_STATE_NULL);
				call->peercallstate = AT_CALL_STATE_NULL;
				gsm->ev.e = GSM_EVENT_HANGUP;
				gsm->ev.hangup.channel = call->channelno;
				gsm->ev.hangup.cause = GSM_CAUSE_USER_BUSY;
				gsm->ev.hangup.cref = call->cr;
				gsm->ev.hangup.call = call;
				call->alive = 0;
				call->sendhangupack = 0;
				gsm_destroycall(gsm, call);
				return &gsm->ev;
			}
		}

		res_event = module_check_sms(gsm, buf, i);
		if (res_event) {
			return res_event;
		}
		
		res_event = module_check_network(gsm, call ,buf, i);
		if (res_event) {
			return res_event;
		}

		res_event = module_check_ussd(gsm, buf, i);
		if (res_event) {
			return res_event;
		}
	}


	return res_event;
}

#ifdef CONFIG_CHECK_PHONE
void module_hangup_phone(struct gsm_modul *gsm)
{
	gsm_switch_state(gsm, GSM_STATE_HANGUP_REQ, get_at(gsm->switchtype,AT_HANGUP));
}

int module_check_phone_stat(struct gsm_modul *gsm, const char *phone_number,int hangup_flag,unsigned int timeout)
{
	char buf[128];
	int time_out=0;
	memset(buf, 0x0, sizeof(buf));
	gsm->phone_stat=-1;
	gsm->auto_hangup_flag=hangup_flag;
	if(timeout<=0)
		time_out=DEFAULT_CHECK_TIMEOUT;
	else
		time_out=timeout;

	if(gsm->state != GSM_STATE_READY) {
		return -1;
	} else {
		gsm->check_timeout=time(NULL)+time_out;
		get_dial_str(gsm->switchtype, phone_number, buf, sizeof(buf));
		gsm_switch_state(gsm, GSM_STATE_PHONE_CHECK, buf);
	}
	return 0;
}
#endif

