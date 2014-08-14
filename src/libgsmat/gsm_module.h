/*
 * libgsmat: An implementation of OpenVox G400P GSM/CDMA cards
 *
 * Parts taken from libpri
 * Written by mark.liu <mark.liu@openvox.cn>
 *
 * Copyright (C) 2005-2010 OpenVox Communication Co. Ltd,
 * All rights reserved.
 *
 * $Id: module.h 60 2010-09-09 07:59:03Z liuyuan $
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 *
 */
 
#ifndef __GSM_MODULE_H__
#define __GSM_MODULE_H__

extern int module_start(struct gsm_modul *gsm);

extern int module_restart(struct gsm_modul *gsm);

extern int module_dial(struct gsm_modul *gsm, struct at_call *call);

extern int module_answer(struct gsm_modul *gsm);

extern int module_senddtmf(struct gsm_modul *gsm, char digit);

extern int module_hangup(struct gsm_modul *gsm);

extern int module_send_ussd(struct gsm_modul *gsm, const char *message);

extern int module_send_text(struct gsm_modul *gsm, const char *destination, const char *msg);

extern int module_send_pdu(struct gsm_modul *gsm, const char *pdu);

extern int module_send_pin(struct gsm_modul *gsm, const char *pin);

extern gsm_event *module_receive(struct gsm_modul *gsm, char *data, int len);

#ifdef CONFIG_CHECK_PHONE
/*Makes Add 2012-4-9 14:08*/
void module_hangup_phone(struct gsm_modul *gsm);

int module_check_phone_stat(struct gsm_modul *gsm, const char *phone_number,int hangup_flag,unsigned int timeout);
#endif //CONFIG_CHECK_PHONE

#ifdef VIRTUAL_TTY
int module_mux_end(struct gsm_modul *gsm, int restart_at_flow);
#endif //VIRTUAL_TTY

#endif

