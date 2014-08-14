/*
 * base.c - Dahdi driver for the OpenVox GSM PCI cards
 *
 * Copyright (C) 2005-2010 OpenVox Communication Co. Ltd,
 * All rights reserved.
 *
 * Based on previous works and written by J.A. Deniz <odi@odicha.net>
 * Written by Mark.liu <mark.liu@openvox.cn>
 *
 * $Id: base.c 343 2011-04-27 06:29:14Z liuyuan $
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 *
 */ 

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <dahdi/kernel.h>
#include <linux/sched.h> 
#include <linux/kthread.h> 
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/wait.h>

#include "opvxg4xx.h"

#ifdef VIRTUAL_TTY
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <asm/uaccess.h>
#endif

/*
* Freedom Add 2011-09-07 13:54
*/
#ifndef DAHDI_VER_NUM
#error "=================================================="
#error "Undefined DAHDI_VER_NUM"
#error "=================================================="
#endif  //DAHDI_VER_NUM

#include <linux/moduleparam.h>

#define OPVXG4XX_CODE 0xC4

#ifdef VIRTUAL_TTY
static int ttymode[256] = {0};
static int ttymode_len;
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


static int debug        = 0;
static int sim          = 0;
static int g4_dev_count = 0;
static int g4_spans     = 0;
static struct pci_dev *multi_gsm = NULL;
static long baudrate    = 115200;   /* default 115200bps. */
static int adcgain      = 0;       /* adc gain of codec, in db, from -42 to 20, mute=21; */
static int dacgain      = -15;      /* dac gain of codec, in db, from -42 to 20, mute=21; */
static int sidetone     = 0;        /* digital side tone, 0=mute, others can be -3 to -21db, step by -3; */
static int inputgain    = 0;        /* input buffer gain, can be 0, 6, 12,24db. */

static int burn = 0;
static int checknumber = 3000; //G4_POWERON_CHECK_NUMBER;

static char *modem = "default";

DECLARE_WAIT_QUEUE_HEAD(cmux_init_sucess);


/*Freedom del 2011-09-01 17:26*/
/************************************************************/
#if 0
static unsigned int UART0_BASE =     0x000004c0;
static unsigned int UART1_BASE =     0x000004e0;
static unsigned int UART2_BASE =     0x00000500;
static unsigned int UART3_BASE =     0x00000520;
#endif
/************************************************************/

static int g4_dchan_tx(struct g4_card *g4, int span);
static int g4_dchan_rx(struct g4_card *g4, int span);

static inline unsigned short swaphl(unsigned short i)
{
	return ((i >>8)  & 0x0ff) | (i << 8);
}

static inline void __g4_outl__(g4_card* g4, unsigned int base, unsigned int regno, unsigned int value)
{
	writel(value, ((unsigned char *)g4->pci_io) + base + regno * sizeof(int));
}

static inline unsigned int __g4_inl__(g4_card* g4, unsigned int base, unsigned int regno)
{
	return readl(((unsigned char *)g4->pci_io) + base + regno * sizeof(int));
}

static inline void __g4_pio0_setreg(g4_card* g4, unsigned int regno, unsigned int value)
{
	__g4_outl__(g4, g4->gpio0.base, regno, value);
}

/* set only 1 bit of gpio, value can be 0 or 1 */
static inline void __g4_pio0_setbit(g4_card* g4, unsigned int bit, unsigned int value)
{
	if (value) {
		g4->gpio0.io |= (1u << bit);
	} else {
		g4->gpio0.io &= ~(1u << bit);
	}
	__g4_outl__(g4, g4->gpio0.base, 0, g4->gpio0.io);
}

/* get only 1 bit of gpio, return 0 or 1 */
static inline unsigned int __g4_pio0_getbit(g4_card* g4, unsigned int bit)
{
	unsigned int i = __g4_inl__(g4, g4->gpio0.base, 0);
	return  (i >> bit) & 1u;
}

/* set more 1 bit of gpio, only masked bit can be set */
static inline void __g4_pio0_setbits(g4_card* g4, unsigned int mask, unsigned int value)
{
	g4->gpio0.io &= ~mask;
	g4->gpio0.io |= mask & value;
	__g4_outl__(g4, g4->gpio0.base, 0, g4->gpio0.io);
}

/* get more 1 bit of gpio, only masked bit can be get */
static inline unsigned int __g4_pio0_getbits(g4_card* g4, unsigned int mask)
{
	return __g4_inl__(g4, g4->gpio0.base, 0) & mask;
}

/* !!!!!! this function can not be used when irq closed, for example ....irq_save() !*/
static void _wait(int msec)
{
	unsigned long origjiffies = jiffies;

	while(1) {
		if (jiffies_to_msecs(jiffies - origjiffies) >= msec) {
			break;
		}
	}
}

#ifdef VIRTUAL_TTY

const unsigned char gsm0710_crctable[256] = { //reversed, 8-bit, poly=0x07 
	0x00, 0x91, 0xE3, 0x72, 0x07, 0x96, 0xE4, 0x75, 
	0x0E, 0x9F, 0xED, 0x7C, 0x09, 0x98, 0xEA, 0x7B, 
	0x1C, 0x8D, 0xFF, 0x6E, 0x1B, 0x8A, 0xF8, 0x69, 
	0x12, 0x83, 0xF1, 0x60, 0x15, 0x84, 0xF6, 0x67, 
	0x38, 0xA9, 0xDB, 0x4A, 0x3F, 0xAE, 0xDC, 0x4D, 
	0x36, 0xA7, 0xD5, 0x44, 0x31, 0xA0, 0xD2, 0x43, 
	0x24, 0xB5, 0xC7, 0x56, 0x23, 0xB2, 0xC0, 0x51, 
	0x2A, 0xBB, 0xC9, 0x58, 0x2D, 0xBC, 0xCE, 0x5F, 
	0x70, 0xE1, 0x93, 0x02, 0x77, 0xE6, 0x94, 0x05, 
	0x7E, 0xEF, 0x9D, 0x0C, 0x79, 0xE8, 0x9A, 0x0B, 
	0x6C, 0xFD, 0x8F, 0x1E, 0x6B, 0xFA, 0x88, 0x19, 
	0x62, 0xF3, 0x81, 0x10, 0x65, 0xF4, 0x86, 0x17, 
	0x48, 0xD9, 0xAB, 0x3A, 0x4F, 0xDE, 0xAC, 0x3D, 
	0x46, 0xD7, 0xA5, 0x34, 0x41, 0xD0, 0xA2, 0x33, 
	0x54, 0xC5, 0xB7, 0x26, 0x53, 0xC2, 0xB0, 0x21, 
	0x5A, 0xCB, 0xB9, 0x28, 0x5D, 0xCC, 0xBE, 0x2F, 
	0xE0, 0x71, 0x03, 0x92, 0xE7, 0x76, 0x04, 0x95, 
	0xEE, 0x7F, 0x0D, 0x9C, 0xE9, 0x78, 0x0A, 0x9B, 
	0xFC, 0x6D, 0x1F, 0x8E, 0xFB, 0x6A, 0x18, 0x89, 
	0xF2, 0x63, 0x11, 0x80, 0xF5, 0x64, 0x16, 0x87, 
	0xD8, 0x49, 0x3B, 0xAA, 0xDF, 0x4E, 0x3C, 0xAD, 
	0xD6, 0x47, 0x35, 0xA4, 0xD1, 0x40, 0x32, 0xA3, 
	0xC4, 0x55, 0x27, 0xB6, 0xC3, 0x52, 0x20, 0xB1, 
	0xCA, 0x5B, 0x29, 0xB8, 0xCD, 0x5C, 0x2E, 0xBF, 
	0x90, 0x01, 0x73, 0xE2, 0x97, 0x06, 0x74, 0xE5, 
	0x9E, 0x0F, 0x7D, 0xEC, 0x99, 0x08, 0x7A, 0xEB, 
	0x8C, 0x1D, 0x6F, 0xFE, 0x8B, 0x1A, 0x68, 0xF9, 
	0x82, 0x13, 0x61, 0xF0, 0x85, 0x14, 0x66, 0xF7, 
	0xA8, 0x39, 0x4B, 0xDA, 0xAF, 0x3E, 0x4C, 0xDD, 
	0xA6, 0x37, 0x45, 0xD4, 0xA1, 0x30, 0x42, 0xD3, 
	0xB4, 0x25, 0x57, 0xC6, 0xB3, 0x22, 0x50, 0xC1, 
	0xBA, 0x2B, 0x59, 0xC8, 0xBD, 0x2C, 0x5E, 0xCF 
};

void gsm0710_setup_step(struct g4_card *g4,int span,int step);

unsigned char gsm0710_make_fcs(const unsigned char *input, int count) {
	unsigned char fcs = 0xFF;
	int i;
	for (i = 0; i < count; i++) {
		fcs = gsm0710_crctable[fcs^input[i]];
	}
	return (0xFF-fcs);
}

int gsm0710_buffer_write(gsm0710_buffer_t *buf, const char *input, int count) {
	int c=buf->endp - buf->writep;

	count = min(count, gsm0710_buffer_free(buf));
	if (count > c){
		memcpy(buf->writep, input, c);
		memcpy(buf->data, input+c, count-c);
		buf->writep = buf->data + (count-c);
	} else {
		memcpy(buf->writep, input, count);
		buf->writep += count;
		if (buf->writep == buf->endp)
			buf->writep = buf->data;
	}
    
	return count;
}

void gsm0710_destroy_frame(gsm0710_frame_t *frame) {
	if (frame->data_length > 0)
		kfree(frame->data);
	kfree(frame);
	frame=NULL;
}

gsm0710_frame_t *gsm0710_buffer_get_frame(gsm0710_buffer_t *buf) {
	int end;
	int length_needed = 5; // channel, type, length, fcs, flag
	unsigned char *data;
	unsigned char fcs = 0xFF;
	gsm0710_frame_t *frame = NULL;

	// Find start flag
	while (!buf->flag_found && gsm0710_buffer_length(buf) > 0){
		if (*buf->readp == GSM0710_F_FLAG)
			buf->flag_found = 1;
		GSM0710_INC_BUF_POINTER(buf, buf->readp);
	}
	if (!buf->flag_found) // no frame started
		return NULL;

	// skip empty frames (this causes troubles if we're using DLC 62)
 	while (gsm0710_buffer_length(buf) > 0 && (*buf->readp == GSM0710_F_FLAG)) {
		GSM0710_INC_BUF_POINTER(buf, buf->readp);
	}

	if (gsm0710_buffer_length(buf) >= length_needed){
		data = buf->readp;
		frame = kmalloc(sizeof(gsm0710_frame_t),GFP_ATOMIC);

		frame->channel = ((*data & 252) >> 2);
		fcs = gsm0710_crctable[fcs^*data];
		GSM0710_INC_BUF_POINTER(buf,data);

		frame->control = *data;
    	fcs = gsm0710_crctable[fcs^*data];
		GSM0710_INC_BUF_POINTER(buf,data);

		frame->data_length = (*data & 254) >> 1;
		fcs = gsm0710_crctable[fcs^*data];
		if ((*data & 1) == 0) {
			/* Current spec (version 7.1.0) states these kind of frames to be invalid
			  * Long lost of sync might be caused if we would expect a long
			  * frame because of an error in length field.*/
			GSM0710_INC_BUF_POINTER(buf,data);
			frame->data_length += (*data*128);
			fcs = gsm0710_crctable[fcs^*data];
			length_needed++;
			
			/*kfree(frame);
			buf->readp = data;
			buf->flag_found = 0;
			return gsm0710_buffer_get_frame(buf);*/
		}
		length_needed += frame->data_length;
	    if (!(gsm0710_buffer_length(buf) >= length_needed)) {
			kfree(frame);
			return NULL;
		}
		GSM0710_INC_BUF_POINTER(buf,data);
	    //extract data
	    if (frame->data_length > 0) {
			if ((frame->data = kmalloc(sizeof(char)*frame->data_length,GFP_ATOMIC))) {
				end = buf->endp - data;
				if (frame->data_length > end) {
					memcpy(frame->data, data, end);
					memcpy(frame->data+end, buf->data, frame->data_length-end);
					data = buf->data + (frame->data_length-end);
				} else {
					memcpy(frame->data, data, frame->data_length);
					data += frame->data_length;
					if (data == buf->endp)
						data = buf->data;
				}
				if (GSM0710_FRAME_IS(GSM0710_UI, frame)) {
					for (end = 0; end < frame->data_length; end++)
				    	fcs = gsm0710_crctable[fcs^(frame->data[end])];
				}
			} else {
#ifdef GSM0710_DEBUG
				printk("Out of memory, when allocating space for frame data.\n");
#endif
				frame->data_length = 0;
			}
		}
	    // check FCS
	    if (gsm0710_crctable[fcs^(*data)] != 0xCF) {
#ifdef GSM0710_DEBUG
			printk("Dropping frame: FCS doesn't match\n");
#endif
			gsm0710_destroy_frame(frame);
			buf->flag_found = 0;
			buf->dropped_count++;
			buf->readp = data;
			return gsm0710_buffer_get_frame(buf);
		} else {
			// check end flag
			GSM0710_INC_BUF_POINTER(buf,data);
			if (*data != GSM0710_F_FLAG) {
#ifdef GSM0710_DEBUG
				printk("Dropping frame: End flag not found. Instead: %d\n", *data);
#endif
				gsm0710_destroy_frame(frame);
				buf->flag_found = 0;
				buf->dropped_count++;
				buf->readp = data;
				return gsm0710_buffer_get_frame(buf);
			} else {
				buf->received_count++;
			}
			GSM0710_INC_BUF_POINTER(buf,data);
		}
		buf->readp = data;
	}
	return frame;
}



/** Writes a frame to a logical channel. C/R bit is set to 1. 
  * Doesn't support FCS counting for UI frames. 
  * 
  * PARAMS: 
  * channel - channel number (0 = control) 
  * input   - the data to be written 
  * count   - the length of the data 
  * type    - the type of the frame (with possible P/F-bit) 
  * 
  * RETURNS: 
  * number of characters written 
  */
int gsm0710_write_frame(struct g4_card *g4, int span,int channel , const char *input , int count , unsigned char type)
{
	// flag, GSM0710_EA=1 C channel, frame type, length 1-2
	unsigned char prefix[5] = {GSM0710_F_FLAG, GSM0710_EA | GSM0710_CR, 0, 0, 0};
	unsigned char postfix[2] = {0xFF, GSM0710_F_FLAG};
	int prefix_length = 4;
	unsigned char *send_buffer=NULL;
	int send_length=0;
	unsigned long flags;
	
	// GSM0710_EA=1, Command, let's add address  
	prefix[1] = prefix[1] | ((63 & (unsigned char)channel) << 2);
	// let's set control field  
	prefix[2] = type;
	// let's not use too big frames  
	count = min(DEFAULT_GSM0710_FRAME_SIZE, count);
	// length  
	if (count > 127){
		prefix_length = 5;
		prefix[3] = ((127 & count) << 1);
		prefix[4] = (32640 & count) >> 7;
	} else {
		prefix[3] = 1 | (count << 1);
	}
	/*if(input)
		printk("==write_frame span:%d chanel:%d length:%d type:%x buf:%s \n",span,channel,count,type,input);
	else
		printk("==write_frame span:%d chanel:%d type:%x\n",span,channel,type);*/
	// CRC checksum 
	postfix[0] = gsm0710_make_fcs(prefix+1, prefix_length-1);
	send_length=prefix_length;
	if (input&&(count > 0))
	{
		send_length=send_length+count;
	}
	send_buffer=kmalloc(sizeof(char)*(2+send_length),GFP_ATOMIC);
	if(send_buffer==NULL)
		return 0;
	memset(send_buffer,0,2+send_length);
	memcpy(send_buffer,prefix,prefix_length);
	if (input&&(count > 0))
	{    
		memcpy(&send_buffer[prefix_length],input,count);
	}
	memcpy(&send_buffer[send_length],postfix,2);
	send_length=send_length+2;
	spin_lock_irqsave(&(g4->pci_buffer_lock[span]),flags);
	memcpy(g4->d_tx_buf[span] + g4->d_tx_idx[span], send_buffer, send_length);
	g4->d_tx_idx[span] += send_length;
	spin_unlock_irqrestore(&(g4->pci_buffer_lock[span]),flags);
	kfree(send_buffer);
	return count;
}

void gsm0710_handle_command(struct g4_card *g4, int span,gsm0710_frame_t *frame) {
	unsigned char type, signals;
	int length = 0, i, type_length, channel, supported = 1;
	unsigned char *response;
  
	if(frame->data_length > 0){
		type = frame->data[0]; // only a byte long types are handled now
		// skip extra bytes
    	for(i = 0; (frame->data_length > i && (frame->data[i] & GSM0710_EA) == 0); i++);
    	i++;
    	type_length = i;
    	if ((type & GSM0710_CR) == GSM0710_CR){
			// command not ack
			// extract frame length
			while (frame->data_length > i) {
				length = (length*128) + ((frame->data[i] & 254) >> 1); 
				if ((frame->data[i] & 1) == 1)
					break;
				i++;
			}
			i++;

			// handle commands
			if (GSM0710_COMMAND_IS(GSM0710_C_CLD, type)) {
#ifdef GSM0710_DEBUG
				printk("The mobile station requested mux-mode termination.\n");
#endif
			} else if (GSM0710_COMMAND_IS(GSM0710_C_TEST, type)) {
#ifdef GSM0710_DEBUG
				printk("Test command:%s\n",frame->data+i);
#endif
			} else if (GSM0710_COMMAND_IS(GSM0710_C_MSC, type)) {
				if (i+1 < frame->data_length) {
					channel = ((frame->data[i] & 252) >> 2);
					i++;
					signals = (frame->data[i]);
#ifdef GSM0710_DEBUG
					printk("Modem status command on channel %d.\n", channel);
#endif
					if ((signals & GSM0710_S_FC) == GSM0710_S_FC) {
#ifdef GSM0710_DEBUG
						printk("No frames allowed.\n");
#endif
					} else {
#ifdef GSM0710_DEBUG
						printk("Frames allowed.\n");
#endif
					}
					if ((signals & GSM0710_S_RTC) == GSM0710_S_RTC) {
#ifdef GSM0710_DEBUG
						printk("RTC\n");
#endif
					} 
					if ((signals & GSM0710_S_IC) == GSM0710_S_IC) {
#ifdef GSM0710_DEBUG
						printk("Ring\n");
#endif
					}
					if ((signals & GSM0710_S_DV) == GSM0710_S_DV) {
#ifdef GSM0710_DEBUG
						printk("DV\n");
#endif
					}
				} else {
#ifdef GSM0710_DEBUG
					printk("ERROR: Modem status command, but no info. i: %d, len: %d, data-len: %d\n", i, length, frame->data_length);
#endif
				}
			} else {
#ifdef GSM0710_DEBUG
				printk("Unknown command (%d) from the control channel.\n", type);
#endif
				response = kmalloc(sizeof(char)*(2+type_length),GFP_ATOMIC);
				response[0] = GSM0710_C_NSC;
				// supposes that type length is less than 128
				response[1] = GSM0710_EA & ((127 & type_length) << 1);
				i = 2;
				while (type_length--) {
					response[i] = frame->data[(i-2)];
					i++;
				}
				gsm0710_write_frame(g4,span,0, response, i, GSM0710_UIH);
				kfree(response);
				supported = 0;
			}
			if (supported) {
				// acknowledge the command
				frame->data[0] = frame->data[0] & ~GSM0710_CR;
				gsm0710_write_frame(g4,span,0, frame->data, frame->data_length, GSM0710_UIH);
			}
		} else {
      		// received ack for a command
     		 if (GSM0710_COMMAND_IS(GSM0710_C_NSC, type)) {
#ifdef GSM0710_DEBUG
				printk("The mobile station didn't support the command sent.\n");
#endif
      		} else {
#ifdef GSM0710_DEBUG
				printk("Command acknowledged by the mobile station.\n");
#endif
     		}
    	}
  	}
}

#ifdef GSM0710_DEBUG
void gsm0710_print_frame(gsm0710_frame_t *frame) {
	int i;
	printk("Received:");
	if (GSM0710_FRAME_IS(GSM0710_SABM, frame))
		printk("SABM\n");
	else if (GSM0710_FRAME_IS(GSM0710_UIH, frame))
		printk("UIH ");
	else if (GSM0710_FRAME_IS(GSM0710_UA, frame))
		printk("UA ");
	else if (GSM0710_FRAME_IS(GSM0710_DM, frame))
		printk("DM ");
	else if (GSM0710_FRAME_IS(GSM0710_DISC, frame))
		printk("DISC ");
	else if (GSM0710_FRAME_IS(GSM0710_UI, frame))
		printk("UI ");
	else {
		printk("unkown (control=%d) ", frame->control);
	}
	printk("frame for channel %d.\n", frame->channel);
	if (frame->data_length > 0) {
		printk("Data:\n");
		for(i=0;i<frame->data_length;i++){
			printk("0x%x ",(unsigned char)frame->data[i];
			if((i!=0)&&(i%10==0)
				printk("\n");
		}
		printk("\n");
	}
}
#endif

/* Extracts and handles frames from the receiver buffer.
 *
 * PARAMS:
 * buf - the receiver buffer
 */
void gsm0710_extract_frames(struct g4_card *g4, int span) {
	gsm0710_frame_t *frame;
	int i;
	unsigned char *buf=g4->d_rx_buf[span];
	int length=g4->d_rx_idx[span];
	struct g4_span *myspan = NULL;

	myspan = &g4->spans[span];
	/*if(length>0)
	{
		printk("span %d d_rx_idx:",span);
		for(i=0;i<length;i++)
		{
			printk("0x%x ",buf[i]);
		}
		printk("\n");
	}*/
	gsm0710_buffer_write(g4->gsm0710_buffer[span],buf,length);
	while ((frame = gsm0710_buffer_get_frame(g4->gsm0710_buffer[span]))){
		if ((GSM0710_FRAME_IS(GSM0710_UI, frame) || GSM0710_FRAME_IS(GSM0710_UIH, frame))) {
			if (frame->channel > 0) {
				// data from logical channel
				if (frame->channel==1)
				{
					if(debug & DEBUG_G4_HDLC)
					{
						printk("card %d span %d DAHDI channel length %d:",g4->cardno,span,frame->data_length);
						for (i = 0; i < frame->data_length; i ++) {
							if (frame->data[i] == 0xd) {
								printk("\\r");
							} else if (frame->data[i] == 0xa) {
								printk("\\n");
							} else {
								printk("%c", frame->data[i]);
							}
						}
						printk("\n");
					}
					dahdi_hdlc_putbuf(myspan->sigchan, frame->data,frame->data_length);
					dahdi_hdlc_finish(myspan->sigchan); 	
				}
				else
				{
					if(debug & DEBUG_G4_DCHAN) 
					{
						printk("card %d span %d TTY channel length:%d:",g4->cardno,span,frame->data_length);
						for (i = 0; i < frame->data_length; i ++) {
							if (frame->data[i] == 0xd) {
								printk("\\r");
							} else if (frame->data[i] == 0xa) {
								printk("\\n");
							} else {
								printk("%c", frame->data[i]);
							}
						}
						printk("\n");
					}
					if(g4->gsm_tty_table[span]) {
						if(g4->gsm_tty_table[span]->tty && g4->gsm_tty_table[span]->open_count) {
							tty_insert_flip_string(g4->gsm_tty_table[span]->tty, frame->data, frame->data_length);
							tty_flip_buffer_push(g4->gsm_tty_table[span]->tty);
						}
					}
				}
			}
			else {
				// control channel command
				gsm0710_handle_command(g4,span,frame);
			}
		} else {
			// not an information frame
#ifdef GSM0710_DEBUG
      		gsm0710_print_frame(frame);
#endif
			if (GSM0710_FRAME_IS(GSM0710_UA, frame)) {
				if (g4->cstatus[frame->channel].opened == 1) {
#ifdef GSM0710_DEBUG
					printk("span %d UA logical channel %d closed.\n", span,frame->channel);
#endif
					g4->cstatus[frame->channel].opened = 0;
					gsm0710_write_frame(g4,span,frame->channel, NULL, 0, GSM0710_SABM | GSM0710_PF);
				} else {
					g4->cstatus[frame->channel].opened = 1;
					if (frame->channel == 0) {
#ifdef GSM0710_DEBUG
						printk("Span %d control channel opened.\n",span);
#endif
					} else {
#ifdef GSM0710_DEBUG
						printk("Span %d Logical channel %d opened.\n",span, frame->channel);
#endif
					}
				}
			} else if (GSM0710_FRAME_IS(GSM0710_DM, frame)) {
				if (g4->cstatus[frame->channel].opened) {
#ifdef GSM0710_DEBUG
					printk("Span %d DM received, so the channel %d was already closed.\n",span, frame->channel);
#endif
					g4->cstatus[frame->channel].opened = 0;
				} else {
					if (frame->channel == 0) {
#ifdef GSM0710_DEBUG
						printk("Span %d couldn't open control channel.\n->Terminating.\n",span);
#endif
					} else {
#ifdef GSM0710_DEBUG
						printk("Span %d logical channel %d couldn't be opened.\n",span, frame->channel);
#endif
					}
				}
			} else if (GSM0710_FRAME_IS(GSM0710_DISC, frame)) {
				// channel close request
				if (g4->cstatus[frame->channel].opened) {
					g4->cstatus[frame->channel].opened = 0;
					gsm0710_write_frame(g4,span,frame->channel, NULL, 0, GSM0710_UA | GSM0710_PF);
					if (frame->channel == 0) {
#ifdef GSM0710_DEBUG
						printk("Span %d DISC control channel closed.\n",span);
#endif
						gsm0710_write_frame(g4,span,0, NULL, 0, GSM0710_SABM | GSM0710_PF);//Multiplexer Control
					} else {
#ifdef GSM0710_DEBUG
						printk("Span %d DISC logical channel %d closed.\n",span, frame->channel);
#endif
						gsm0710_write_frame(g4,span,frame->channel, NULL, 0, GSM0710_SABM | GSM0710_PF);
					}
				} else {
					// channel already closed
#ifdef GSM0710_DEBUG
					printk("Span %d received DISC even though channel %d was already closed.\n", span,frame->channel);
#endif
					gsm0710_write_frame(g4,span,frame->channel, NULL, 0, GSM0710_DM | GSM0710_PF);
				}
			} else if (GSM0710_FRAME_IS(GSM0710_SABM, frame)) {
				// channel open request
				if (g4->cstatus[frame->channel].opened == 0) {
					if (frame->channel == 0) {
#ifdef GSM0710_DEBUG
						printk("Span %d control channel opened.\n",span);
#endif
					} else {
#ifdef GSM0710_DEBUG
						printk("Span %d logical channel %d opened.\n",span, frame->channel);
#endif
					}
				} else {
					// channel already opened
#ifdef GSM0710_DEBUG
					printk("Span %d received SABM even though channel %d was already closed.\n", span,frame->channel);
#endif
				}
				g4->cstatus[frame->channel].opened = 1;
				gsm0710_write_frame(g4,span,frame->channel, NULL, 0, GSM0710_UA | GSM0710_PF);
			}
		}
    	gsm0710_destroy_frame(frame);
	}
}

void at_command(struct g4_card *g4, int span,char* command,int length)
{
	memcpy(g4->d_tx_buf[span] + g4->d_tx_idx[span], command, length);
	g4->d_tx_idx[span] += length;
	if(debug & DEBUG_G4_HDLC)
	{
		printk("AT COMMAND TX %s ",command);
	}
}

void gsm0710_setup_step(struct g4_card *g4,int span,int step)
{
	char mux_command[] = "AT+CMUX=0\r\n";
	char init_command[]="AT\r\n";
	char echo_off_command[]="ATE0\r\n";
	
	switch(step)
	{
		case 0:
			at_command(g4,span,init_command,4);
			break;
		case 1:
			at_command(g4,span,mux_command,11);
			break;
		case 2:
			gsm0710_write_frame(g4,span,0, NULL, 0, GSM0710_SABM | GSM0710_PF);//Multiplexer Control
			break;
		case 3:
			gsm0710_write_frame(g4,span,1, NULL, 0, GSM0710_SABM | GSM0710_PF);//dahdi using
			break;
		case 4:
			gsm0710_write_frame(g4,span,2, NULL, 0, GSM0710_SABM | GSM0710_PF);//pppd using
			break;
		case 5:
			gsm0710_write_frame(g4,span,1, echo_off_command, sizeof(echo_off_command), GSM0710_UIH);//echo off
			break;
	}
}

int gsm0710_init(struct g4_card *g4,int span)
{
	struct g4_span *myspan = NULL;

	myspan = &g4->spans[span];
	spin_lock_init(&g4->pci_buffer_lock[span]);
	g4->gsm0710_buffer[span]=kmalloc(sizeof(gsm0710_buffer_t),GFP_KERNEL);
	if(g4->gsm0710_buffer[span]){
		memset(g4->gsm0710_buffer[span], 0, sizeof(gsm0710_buffer_t));
		g4->gsm0710_buffer[span]->readp = g4->gsm0710_buffer[span]->data;
		g4->gsm0710_buffer[span]->writep = g4->gsm0710_buffer[span]->data;
		g4->gsm0710_buffer[span]->endp = g4->gsm0710_buffer[span]->data + GSM0710_BUFFER_SIZE;
	}
	else
		return -1;
	return 0;
}

void gsm0710_exit(struct g4_card *g4,int span)
{
	g4->gsm0710_mode[span]=0;
	g4->enable_gsm0710[span]=0;
	if(g4->gsm0710_buffer[span])
	{
		kfree(g4->gsm0710_buffer[span]);
		g4->gsm0710_buffer[span]=NULL;
	}
}

#endif


#ifdef G400E_PW_KEY

#include <linux/proc_fs.h>

#define PW_KEY_NAME "gsm_module_power_key"

static struct proc_dir_entry *pw_key_entry[255] = {0};

int pw_key_read(char*buffer, char**buffer_localation, off_t offset,int buffer_length,int* eof, void *data )
{
	int len;
	struct g4_card* g4;
	g4 = (struct g4_card*)data;

    len = sprintf(buffer, "%d\n",__g4_inl__(g4, g4->gpio2.base, 0) & 1 ? 0 : 1);

	return len;
}
                  
int pw_key_write(struct file *filp, const char *buffer,unsigned long count,void *data)
{
	struct g4_card* g4;
	g4 = (struct g4_card*)data;

	if(count <= 2 && count >= 1) {
		if(buffer[0] == '1') {	//On
			__g4_outl__(g4, g4->gpio2.base, 0, 0);
		} else if(buffer[0] == '0') { //Off
			__g4_outl__(g4, g4->gpio2.base, 0, 1);
		}

		return count;
	}

	return -1;
}
    
static int pw_key_entry_init(struct g4_card* g4)
{
	char name[256];

	struct proc_dir_entry * entry;

	if(g4->cardno >= 255) {
		return 0;
	}

	snprintf(name,256,"%s-%d",PW_KEY_NAME,g4->cardno);

	#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	entry = pw_key_entry[g4->cardno] = create_proc_entry(name,0644,NULL);
	if(entry == NULL) {
		printk(KERN_INFO "Error:could not initialize /proc/%s\n",name);
		return 0;
	}

	entry->read_proc = pw_key_read;
	entry->write_proc = pw_key_write;
	//entry->owner = THIS_MODULE;
	entry->mode = S_IFREG | S_IRUGO;
	entry->uid = 0;
	entry->gid = 0;
	entry->size=2;
	entry->data = (void*)g4;
	#else
	static const struct file_operations pw_proc_fops = {
		.read = pw_key_read,
		.write = pw_key_write,
	 };

	/*static const struct proc_dir_entry pw_proc_data = {
		.mode = S_IFREG | S_IRUGO,
		.uid = 0,
		.gid = 0,
		.size = 2,
		.data = *(void*)g4,
	};*/

	entry = pw_key_entry[g4->cardno] = proc_create_data(name, 0644, NULL, &pw_proc_fops, (void*)g4);

	if(entry == NULL) {
		printk(KERN_INFO "Error:could not initialize /proc/%s\n",name);
                return 0;
	}
	#endif
	printk(KERN_INFO "/proc/%s is created\n",name);

	return 1;
}

static int pw_key_entry_exit(struct g4_card* g4)
{
	char name[256];
	struct proc_dir_entry * entry;

	if(g4->cardno >= 255) {
		return 0;
	}

	entry = pw_key_entry[g4->cardno];

	snprintf(name, 256, "%s-%d",PW_KEY_NAME,g4->cardno);

	if(entry) {
		remove_proc_entry(name, NULL);
		printk(KERN_INFO "/proc/%s has been removed\n",name);
	}

	return 1;
}

#endif //G400E_PW_KEY


#ifdef VIRTUAL_TTY

static struct g4_card* g_g4[255];
static int g4_card_sum = 0;
static int tty_sum = 0;
static struct tty_driver *gsm_tty_driver;

#define GSM_TTY_MAJOR		240	/* experimental range */

static int ttygsm_get_cardno(int channel)
{
	int i,j;
	for(i=0; i<sizeof(g_g4)/sizeof(g_g4[0]); i++) {
		if(g_g4[i]) {
			for(j=0; j < g_g4[i]->g4spans; j++) {
				if(g_g4[i]->gsm_tty_table[j])
					if(g_g4[i]->gsm_tty_table[j]->channel == channel)
						return i;
			}
		}
	}
	return -1;
}

static int ttygsm_get_span(int channel)
{
	int i,j;
	for(i=0; i<sizeof(g_g4)/sizeof(g_g4[0]); i++) {
		if(g_g4[i]) {
			for(j=0; j<g_g4[i]->g4spans; j++) {
				if(g_g4[i]->gsm_tty_table[j])
					if(g_g4[i]->gsm_tty_table[j]->channel == channel)
						return j;
			}
		}
	}
	return -1;
}

static int ttygsm_open(struct tty_struct *tty, struct file *file)
{
	struct gsm_serial *gsm;
	int channel;
	int cardno;
	int span;
	unsigned long flags;

	/* initialize the pointer in case something fails */
	tty->driver_data = NULL;

	/* get the serial object associated with this tty pointer */
	channel = tty->index;

	if((cardno=ttygsm_get_cardno(channel)) == -1) {
		printk("Get cardno failed(%d),open function\n",channel);
		return -EPERM;
	}

	if((span=ttygsm_get_span(channel)) == -1) {
		printk("Get span failed(%d),open function\n",channel);
		return -EPERM;
	}
	
	if((g_g4[cardno]->gsm0710_mode==0)||(g_g4[cardno]->enable_gsm0710[span]==0)){
		printk("Enable tty model failed(%d),open function\n",channel);
		return -EPERM;
	}
	
	if(g_g4[cardno] == NULL) {
		printk("GSM card not found(%d),open function\n",cardno);
		return -EPERM;
	}

	if( ! (g_g4[cardno]->spans[span].span.flags & DAHDI_FLAG_RUNNING) ) {
		printk("DAHDI not run,open function\n");
		return -EPERM;
	}

	gsm = g_g4[cardno]->gsm_tty_table[span];
	if (gsm == NULL) {
		printk("GSM span not found(%d,%d),open function\n",cardno,span);
		return -EPERM;
	} else {
		if(gsm->open_count) {
			return -EPERM;
		}
	}

	spin_lock_irqsave(&(gsm->lock),flags);

	/* save our structure within the tty structure */
	tty->driver_data = gsm;
	gsm->tty = tty;

	++gsm->open_count;

	spin_unlock_irqrestore(&(gsm->lock),flags);

	return 0;
}

static void do_close(struct gsm_serial *gsm)
{
	unsigned long flags;

	//down(&gsm->sem);
	spin_lock_irqsave(&(gsm->lock),flags);


	if (!gsm->open_count) {
		/* port was never opened */
		goto exit;
	}

	gsm->tty = NULL;
	--gsm->open_count;
exit:
	//up(&gsm->sem);
	spin_unlock_irqrestore(&(gsm->lock),flags);	
    
	return;
}

static void ttygsm_close(struct tty_struct *tty, struct file *file)
{
	struct gsm_serial *gsm = tty->driver_data;
	
	if (gsm)
		do_close(gsm);
}	

static int ttygsm_write(struct tty_struct *tty, const unsigned char *buffer, int count)
{
	struct gsm_serial *gsm = tty->driver_data;
	int retval = -EINVAL;

	unsigned long flags;

	int channel;
	int cardno;
	int span;
	int send_length=0;
	
	if (!gsm)
		return -ENODEV;

	channel = tty->index;
	cardno = gsm->cardno;
	span = gsm->span;

	if(NULL == g_g4[cardno]) {
		printk("GSM card not found(%d),write function\n",cardno);
		return -ENODEV;
	}

	if(NULL == g_g4[cardno]->gsm_tty_table[span]) {
		printk("Channel %d is not tty mode(%d,%d),write function\n",channel,cardno,span);
		return -ENODEV;
	}

	if( ! (g_g4[cardno]->spans[span].span.flags & DAHDI_FLAG_RUNNING) ) {
		printk("DAHDI not run(%d,%d),write function\n",cardno,span);
		return -EPERM;
	}
	
	if(g_g4[cardno]->gsm0710_mode[span]&&g_g4[cardno]->enable_gsm0710[span]){
		if(g_g4[cardno]->d_tx_idx[span]+count+6 >= G4_SER_BUF_SIZE) {
			printk("No has buffer to write(%d,%d)\n",cardno,span);
			return -ENOSPC;
		}
	}
	else
	{
		if(g_g4[cardno]->d_tx_idx[span]+count >= G4_SER_BUF_SIZE) {
			printk("No has buffer to write(%d,%d)\n",cardno,span);
			return -ENOSPC;
		}
	}

	spin_lock_irqsave(&(gsm->lock),flags);

	if (!gsm->open_count)
		goto exit;
	if((g_g4[cardno]->gsm0710_mode[span]==1)||(g_g4[cardno]->enable_gsm0710[span]==1)){
		while(send_length<count)
		{
			if((count-send_length)>127)
			{
				gsm0710_write_frame(g_g4[cardno],span,2,&buffer[send_length],127,GSM0710_UIH);
				send_length+=127;
			}
			else
			{
				gsm0710_write_frame(g_g4[cardno],span,2,&buffer[send_length],count-send_length,GSM0710_UIH);
				send_length=count;
				break;
			}
		}
	}
	else
	{
		spin_unlock_irqrestore(&(gsm->lock),flags);
		return -ENODEV;
	}
	retval = count;

exit:
	spin_unlock_irqrestore(&(gsm->lock),flags);

	return retval;
}

static int ttygsm_write_room(struct tty_struct *tty) 
{
	struct gsm_serial *gsm = tty->driver_data;
	int room = -EINVAL;
	unsigned long flags;
	
	if (!gsm)
		return -ENODEV;

	//down(&gsm->sem);
	spin_lock_irqsave(&(gsm->lock),flags);

	
	if (!gsm->open_count) {
		/* port was not opened */
		goto exit;
	}

	/* calculate how much room is left in the device */
	room = 255;

exit:
	//up(&gsm->sem);
	spin_unlock_irqrestore(&(gsm->lock),flags);

	return room;
}

static int ttygsm_chars_in_buffer(struct tty_struct *tty)
{
	return 0;
}

static struct tty_operations serial_ops = {
	.open = ttygsm_open,
	.close = ttygsm_close,
	.write = ttygsm_write,
	.write_room = ttygsm_write_room,
	.chars_in_buffer = ttygsm_chars_in_buffer,
};

#endif //VIRTUAL_TTY

static inline void g4_init_uart(g4_card* g4)
{
	unsigned int magic;
	
/*Freedom add 2011-09-01 17:26*/
/************************************************************/
	unsigned int UART0_BASE = 0x000004c0;
	unsigned int UART1_BASE = 0x000004e0;
	unsigned int UART2_BASE = 0x00000500;
	unsigned int UART3_BASE = 0x00000520;
	if (G4_VERSION_CHECK(g4,1,1)) {
		UART0_BASE = 0x00000480;
		UART1_BASE = 0x000004c0;
		UART2_BASE = 0x00000500;
		UART3_BASE = 0x00000600;
	}
/************************************************************/

	switch (baudrate) {
		case 9600:          magic = 6875;           break;
		case 19200:         magic = 3438;           break;
		case 38400:         magic = 1719;           break;
		case 57600:         magic = 1146;           break;
		case 115200:        magic = 573;            break;
		default:            magic = 573;            break;  /* ml we default use 115200 */
	}

#if 1
	//__g4_outl__(g4, UART0_BASE, 5, magic);
	__g4_outl__(g4, UART0_BASE, 4, magic);
	__g4_outl__(g4, UART1_BASE, 4, magic);
	__g4_outl__(g4, UART2_BASE, 4, magic);
	__g4_outl__(g4, UART3_BASE, 4, magic);
#else	
	__g4_outl__(g4, UART0_BASE, 5, magic);
	__g4_outl__(g4, UART1_BASE, 5, magic);
	__g4_outl__(g4, UART2_BASE, 5, magic);
	__g4_outl__(g4, UART3_BASE, 5, magic);
#endif

	/* set work mode : no irq, RTS on ( active low) */
	__g4_outl__(g4, UART0_BASE, 3, 1 << 11);
	__g4_outl__(g4, UART1_BASE, 3, 1 << 11);
	__g4_outl__(g4, UART2_BASE, 3, 1 << 11);
	__g4_outl__(g4, UART3_BASE, 3, 1 << 11);
}

/* G400 codec specific functions. */
static inline unsigned char format_adcgain(int gain)
{
	unsigned char ret = 0x2a;   /* default adcgain=0db */
	if((gain > 21) || (gain < -42)) {
		printk(KERN_WARNING "opvxg4xx: Invalid adcgain %d\n", gain);
	} else {
		int i = 0x2a;
		if(gain == 21) {
			ret = 0x3f; /* Mute. */
		} else {
			ret = (unsigned char)(i + gain);    /* change to correct format; */
		}
	}

	if(debug & DEBUG_G4_CODEC) {
		printk(KERN_DEBUG "opvxg4xx: adcgain is 0x%x(%ddb)\n", ret & 0xff, gain);
	}

	return ret;
}

static inline unsigned char format_dacgain(int gain)
{
	unsigned char ret = 0x56;   /* default dacgain=-20db */

	if(gain > 21 || gain < -42) {
		printk(KERN_WARNING "opvxg4xx: Invalid dacgain %d\n", gain);
	} else {
		int i = 0x6a;
		if(gain == 21) {
			ret = 0x7f; /* Mute. */
		} else {
			ret = (unsigned char)(i + gain);    /* change to correct format; */
		}
	}

	if(debug & DEBUG_G4_CODEC) {
		printk(KERN_DEBUG "opvxg4xx: dacgain is 0x%x(%ddb)\n", ret & 0xff, gain);
	}

	return ret;
}

static inline unsigned char format_sidetone(int sidetone)
{
	unsigned char ret = 0x7;    /* default mute. */
	int i;
	int valid_values[] = {0, -3, -6, -9, -12, -15, -18, -21};
	int num_values = sizeof(valid_values) / sizeof(valid_values[0]);

	for (i = 0; i < num_values; i ++) {
		if(sidetone == valid_values[i]) {
			break;
		}
	}
		
	if (i < num_values) {
		if(i == 0){ /* set to mute; */
			if (debug & DEBUG_G4_CODEC) {
				printk(KERN_DEBUG "opvxg4xx: sidetone set to mute\n");
			}
		} else {
			ret = i-1;
			if (debug & DEBUG_G4_CODEC) {
				printk(KERN_DEBUG "opvxg4xx: sidetone set to %ddb\n", valid_values[i]);
			}
		}
	} else {
		printk(KERN_WARNING "opvxg4xx: sidetone value %d not correct, set to default 7(MUTE)\n", sidetone);
	}

	return ret;
}

static inline unsigned char format_inputgain(int gain)
{
	unsigned char ret = 0;  /* default 0db. */
	int i;
	int valid_values[] = {0, 6, 12, 24 };
	int num_values = sizeof(valid_values) / sizeof(valid_values[0]);

	for(i = 0; i < num_values; i ++) {
		if(gain == valid_values[i]) {
			break;
		}
	}

	if(i < num_values) {
		ret = i;
		if(debug & DEBUG_G4_CODEC) {
			printk(KERN_DEBUG "opvxg4xx: input gain set to %ddb\n", valid_values[i]);
		}
	} else {
		printk(KERN_WARNING "opvxg4xx: input gain value %d not correct, set to default 0db\n", gain);
	}

	return ret;
}

static inline void g4_init_pio0(g4_card* g4)
{
	/* all uart signals are low level enable, and power is also reversed */
	g4->gpio0.base  = PIO0_BASE;
	g4->gpio0.dir   = 0x0fff000f;   /* set io direction */
	g4->gpio0.io    = 0x000f0000;   /* set value: dtr 0, power 0, rst 1, portsel 0 */

	__g4_outl__(g4, g4->gpio0.base, 1, g4->gpio0.dir);
	__g4_outl__(g4, g4->gpio0.base, 2, 0);         /* set interrupt mask to 0 */
	__g4_outl__(g4, g4->gpio0.base, 3, 0);         /* no edge capture */
	__g4_outl__(g4, g4->gpio0.base, 0, g4->gpio0.io);   
}

static inline void g4_init_pio2(g4_card* g4)
{
	g4->gpio2.base  = PIO2_BASE;
}

/* G400 i2c specific functions. */
static inline void i2c_Init(unsigned int *_base)
{
	volatile unsigned int *base = _base;

	/* prescale = busfreq/(5*I2cFreq), now we use 100k I2c Freq; */
	unsigned prescale = BUS_FREQ/5/100000;

	base[I2C_FREQ_LOW] = prescale&0xff;
	base[I2C_FREQ_HIGH] = (prescale>>8)&0xff;
	base[I2C_CTRL] = I2C_CTRL_ENABLE;   /* we do not use irq. */
}

static int i2c_SetRead(unsigned int *_base, unsigned char addr)
{
	volatile unsigned int *base = _base;
	unsigned long origjiffies;

	base[I2C_TX] = 0x80; /*I2C address is 0x100, SamrtPCM address is 0, I2C WR; */
	base[I2C_CMD] = I2C_CMD_STA | I2C_CMD_WR;
	/* while(base[I2C_STATUS] & I2C_STATUS_TIP){}; */
	origjiffies = jiffies;
	while(1) {
		if(!(base[I2C_STATUS] & I2C_STATUS_TIP)) {
			break;
		}
		if (jiffies_to_msecs(jiffies - origjiffies) >= I2C_TIMEOUT) {
			return -1;
		}
	}

	/* while(base[I2C_STATUS] & I2C_STATUS_RXACK){}; */
	origjiffies = jiffies;
	while(1) {
		if(!(base[I2C_STATUS] & I2C_STATUS_RXACK)) {
			break;
		}
		if (jiffies_to_msecs(jiffies - origjiffies) >= I2C_TIMEOUT) {
			return -2;
		}
	}

	_wait(10);
	base[I2C_TX] = addr;
	base[I2C_CMD] = I2C_CMD_STO | I2C_CMD_WR;
	/* while(base[I2C_STATUS] & I2C_STATUS_TIP){}; */
	while(1) {
		if(!(base[I2C_STATUS] & I2C_STATUS_TIP)) {
			break;
		}
		if (jiffies_to_msecs(jiffies - origjiffies) >= I2C_TIMEOUT) {
			return -3;
		}
	}
	
	/* while(base[I2C_STATUS] & I2C_STATUS_RXACK){}; */
	while(1) {
		if(!(base[I2C_STATUS] & I2C_STATUS_RXACK)) {
			break;
		}
		if (jiffies_to_msecs(jiffies - origjiffies) >= I2C_TIMEOUT) {
			return -4;
		}
	}

	return 0;
}

static int i2c_Read(unsigned int *_base, unsigned char* pVal)
{
	volatile unsigned int *base = _base;
	unsigned long origjiffies;

	base[I2C_TX] = 0x81; /* I2C address is 0x100, SamrtPCM address is 0, I2c Read; */
	base[I2C_CMD] = I2C_CMD_STA | I2C_CMD_WR;

	/* while(base[I2C_STATUS] & I2C_STATUS_TIP){}; */
	origjiffies = jiffies;
	while(1) {
		if(!(base[I2C_STATUS] & I2C_STATUS_TIP)) {
			break;
		}
		if (jiffies_to_msecs(jiffies - origjiffies) >= I2C_TIMEOUT) {
			return -1;
		}
	}

	base[I2C_CMD] = I2C_CMD_RD | I2C_CMD_ACK | I2C_CMD_STO;
	/* while(base[I2C_STATUS] & I2C_STATUS_TIP){}; */
	origjiffies = jiffies;
	while(1) {
		if(!(base[I2C_STATUS] & I2C_STATUS_TIP)) {
			break;
		}
		if (jiffies_to_msecs(jiffies - origjiffies) >= I2C_TIMEOUT) {
			return -2;
		}
	}
	*pVal = base[I2C_RX];

	return 0;
}

static int i2c_Write(unsigned int *_base, unsigned char reg, unsigned char value)
{
	unsigned long origjiffies;
	volatile unsigned int *base = _base;

	base[I2C_TX] = 0x80;    /*I2C address is 0x100, SamrtPCM address is 0, I2C WR; */
	base[I2C_CMD] = I2C_CMD_STA | I2C_CMD_WR;
	/* while(base[I2C_STATUS] & I2C_STATUS_TIP){}; */
	origjiffies = jiffies;
	while(1) {
		if(!(base[I2C_STATUS] & I2C_STATUS_TIP)) {
			break;
		}
		if (jiffies_to_msecs(jiffies - origjiffies) >= I2C_TIMEOUT) {
			return -1;
		}
	}

	/* while(base[I2C_STATUS] & I2C_STATUS_RXACK){}; */
	origjiffies = jiffies;
	while(1) {
		if(!(base[I2C_STATUS] & I2C_STATUS_RXACK)) {
			break;
		}
		if (jiffies_to_msecs(jiffies - origjiffies) >= I2C_TIMEOUT) {
			return -2;
		}
	}

	base[I2C_TX] = reg; /* I2C address is 0x100, SamrtPCM address is 0, I2C WR; */
	base[I2C_CMD] = I2C_CMD_WR;
	/* while(base[I2C_STATUS] & I2C_STATUS_TIP){}; */
	origjiffies = jiffies;
	while(1) {
		if(!(base[I2C_STATUS] & I2C_STATUS_TIP)) {
			break;
		}
		if (jiffies_to_msecs(jiffies - origjiffies) >= I2C_TIMEOUT) {
			return -3;
		}
	}

	/* while(base[I2C_STATUS] & I2C_STATUS_RXACK){}; */
	origjiffies = jiffies;
	while(1) {
		if(!(base[I2C_STATUS] & I2C_STATUS_RXACK)) {
			break;
		}
		if (jiffies_to_msecs(jiffies - origjiffies) >= I2C_TIMEOUT) {
			return -4;
		}
	}

	base[I2C_TX] = value;   /* I2C address is 0x100, SamrtPCM address is 0, I2C WR; */
	base[I2C_CMD] = I2C_CMD_STO | I2C_CMD_WR;
	/* while(base[I2C_STATUS] & I2C_STATUS_TIP){}; */
	origjiffies = jiffies;
	while(1) {
		if(!(base[I2C_STATUS] & I2C_STATUS_TIP)) {
			break;
		}
		if (jiffies_to_msecs(jiffies - origjiffies) >= I2C_TIMEOUT) {
			return -5;
		}
	}

	/* while(base[I2C_STATUS] & I2C_STATUS_RXACK){}; */
	origjiffies = jiffies;
	while(1) {
		if(!(base[I2C_STATUS] & I2C_STATUS_RXACK)) {
			break;
		}
		if (jiffies_to_msecs(jiffies - origjiffies) >= I2C_TIMEOUT) {
			return -6;
		}
	}

	return 0;
}

static void init_320aic14k(unsigned int *base)
{
	unsigned char c = '\0';
	int i;

	i = i2c_SetRead(base, 2);
	if(debug & DEBUG_G4_I2C) {
		printk(KERN_DEBUG "initaic14k return %x\n", i);
	}
	i = i2c_Read(base, &c);
	if(debug & DEBUG_G4_I2C) {
		printk(KERN_DEBUG "initaic14k return %x\n", i);
	}
	i = i2c_Write(base, 2, c|0x80); /* set turbo mode */
	if(debug & DEBUG_G4_I2C) {
		printk(KERN_DEBUG "initaic14k return %x\n", i);
	}
	i = i2c_Write(base, 1, 0x41);   /* cx=1, dac16=1, continuous data transfer, 16bit DAC input */
	if(debug & DEBUG_G4_I2C) {
		printk(KERN_DEBUG "initaic14k return %x\n", i);
	}

	i = i2c_Write(base, 3, 0x01);   /* FS/Fs=1, OSR for DAC channels is 128 */
	if(debug & DEBUG_G4_I2C) {
		printk(KERN_DEBUG "initaic14k return %x\n", i);
	}

	/* set m=1, n=2, p=8(default); */
	i = i2c_Write(base, 4, 0x88);   /* set m=8, ( FSDIV=1, D6-0=8) */
	if(debug & DEBUG_G4_I2C) {
		printk(KERN_DEBUG "initaic14k return %x\n", i);
	}
	i = i2c_Write(base, 4, 0x01);   /* set n=16, p=1, ( FSDIV=0); */
	if(debug & DEBUG_G4_I2C) {
		printk(KERN_DEBUG "initaic14k return %x\n", i);
	}
	/*  A/D converter gain */
	c = format_adcgain(adcgain);
	i = i2c_Write(base, 5, c);       /* setup adc gain, reg5a; */
	if(debug & DEBUG_G4_I2C) {
		printk(KERN_DEBUG "initaic14k return %x\n", i);
	}

	c = format_dacgain(dacgain);
	i = i2c_Write(base, 5, c);    /* setup dac gain, reg5b; */
	if(debug & DEBUG_G4_I2C) {
		printk(KERN_DEBUG "initaic14k return %x\n", i);
	}

	c = 0x80;
	c |= (format_sidetone(sidetone) << 3) | format_inputgain(inputgain);
	i = i2c_Write(base, 5, c);       /* setup sidetone gain and input buffer gain, reg 5c; */
	if(debug & DEBUG_G4_I2C) {
		printk(KERN_DEBUG "initaic14k return %x\n", i);
	}

	i = i2c_Write(base, 5, 0xff);   /* useless, reg5d. */
	if(debug & DEBUG_G4_I2C) {
		printk(KERN_DEBUG "initaic14k return %x\n", i);
	}

	i = i2c_Write(base, 6, 0xb0);   /* disable out2/3 */
	if(debug & DEBUG_G4_I2C) {
		printk(KERN_DEBUG "initaic14k return %x\n", i);
	}
}

static inline void g4_init_i2c(struct g4_card *g4)
{
	long ms;

	/* reset i2c */
	__g4_pio0_setbits(g4, PIO0_RST_MASK, 0x0);  /* set RST to low for 100 ms */
	G4_SLEEP_MILLI_SEC(100, ms);
	__g4_pio0_setbits(g4, PIO0_RST_MASK, PIO0_RST_MASK);  /* set RST to HIGH*/

	if (debug & DEBUG_G4_SYS) {
		printk(KERN_DEBUG "pci_io addr is 0x%lx \n", (unsigned long)g4->pci_io);
	}

	i2c_Init( (unsigned int*)(((unsigned char *)(g4->pci_io))+I2C0_BASE) );
	init_320aic14k((unsigned int*)(((unsigned char *)(g4->pci_io))+I2C0_BASE)); 

	i2c_Init((unsigned int*)(((unsigned char *)(g4->pci_io))+I2C1_BASE));
	init_320aic14k((unsigned int*)(((unsigned char *)(g4->pci_io))+I2C1_BASE)); 

	i2c_Init((unsigned int*)(((unsigned char *)(g4->pci_io))+I2C2_BASE));
	init_320aic14k((unsigned int*)(((unsigned char *)(g4->pci_io))+I2C2_BASE)); 

	i2c_Init((unsigned int*)(((unsigned char *)(g4->pci_io))+I2C3_BASE));
	init_320aic14k((unsigned int*)(((unsigned char *)(g4->pci_io))+I2C3_BASE)); 
}

static void g4_power_on(struct g4_card *g4, int delay)
{
	long ms;

	__g4_pio0_setbits(g4, PIO0_PWR_MASK, 0x0);  /* set PWR_N to high */
	G4_SLEEP_MILLI_SEC(1000, ms);
	__g4_pio0_setbits(g4, PIO0_PWR_MASK, PIO0_PWR_MASK);  /* set PWR_N to low for 2500 ms */
	G4_SLEEP_MILLI_SEC(delay, ms);
	__g4_pio0_setbits(g4, PIO0_DTR_MASK, 0x0);  /* set DTR to low, Enable DTR pin to wake up SIM300DZ. If DTR Pin is pull down to a low level.*/
	__g4_pio0_setbits(g4, PIO0_PWR_MASK, 0x0);  /* set PWR_N to high */
	G4_SLEEP_MILLI_SEC(5000, ms);

	g4->check_start[0] = 1; 
	g4->check_start[1] = 1; 
	g4->check_start[2] = 1; 
	g4->check_start[3] = 1; 
}

static void g4_power_off(struct g4_card *g4, unsigned long flag,int delay)
{
	long ms;
	printk("opvxg4xx: card %d power off flag 0x%lx\n",g4->cardno,flag);
	__g4_pio0_setbits(g4, PIO0_PWR_MASK, flag);  /* set PWR_N to low */
	G4_SLEEP_MILLI_SEC(delay, ms);
	__g4_pio0_setbits(g4, PIO0_PWR_MASK, 0x0);  /* set PWR_N to high */
	
	g4->power[0] = 0;
	g4->power[1] = 0;
	g4->power[2] = 0;
	g4->power[3] = 0;

	G4_SLEEP_MILLI_SEC(5000, ms);
}

static int g4_gsm_power_on(struct g4_card *g4, int delay,int span,int force)
{
	long ms;
	if((g4->power_on_flag[span]==0)||force)
	{
		__g4_pio0_setbits(g4, (1<<(span+PIO0_PWR)), (1<<(span+PIO0_PWR)));  /* set PWR_N to low */
		G4_SLEEP_MILLI_SEC(delay, ms);
		__g4_pio0_setbits(g4, (1<<(span+PIO0_PWR)), (~(1<<(span+PIO0_PWR))));  /* set PWR_N to high */
		g4->check_start[span]=1;
		g4->power[span]=1;
		g4->power_on_flag[span]=0;
		G4_SLEEP_MILLI_SEC(5000, ms);
		return 0;
	}
	return -1;
}

static int g4_gsm_power_off(struct g4_card *g4, int delay,int span)
{
	long ms;
#ifdef VIRTUAL_TTY
	unsigned char close_mux[2] = {GSM0710_C_CLD | GSM0710_CR, 1};
#endif
	if(g4->power_on_flag[span]==1)
	{
#ifdef VIRTUAL_TTY
	   	if((g4->gsm0710_mode[span])&&(g4->enable_gsm0710[span]))
		{
			gsm0710_write_frame(g4,span,0, close_mux, 2, GSM0710_UIH);
			g4->gsm0710_mode[span]=0;
			g4->enable_gsm0710[span]=0;
		}
#endif
		__g4_pio0_setbits(g4, (1<<(span+PIO0_PWR)), (1<<(span+PIO0_PWR)));  /* set PWR_N to low */
		G4_SLEEP_MILLI_SEC(delay, ms);
		__g4_pio0_setbits(g4, (1<<(span+PIO0_PWR)), (~(1<<(span+PIO0_PWR))));  /* set PWR_N to high */
		g4->power[span] = 0;
		g4->power_on_flag[span]=0;
		G4_SLEEP_MILLI_SEC(5000, ms);
		return 0;
	}
	return -1;
}

static int g4_switch_on_all(struct g4_card *g4)
{
	printk("opvxg4xx: card %d Powering up all spans...\n",g4->cardno);

	__g4_outl__(g4, g4->gpio2.base, 0, 0);          
	__g4_outl__(g4, SWAP_MEM_BASE, 3, 1);
	g4->rst_flag = 1;
	g4_power_on(g4, 2500);

	return 0;
}

static void g4_switch_off_all(struct g4_card *g4) 
{	
	unsigned long flag=0;
	int i=0;
	__g4_outl__(g4, SWAP_MEM_BASE, 3, 0);
	g4->rst_flag = 0;

	for(i=0;i<G4_SPANS;i++)
	{
		if(g4->power_on_flag[i])
		{
			flag |=1<<(PIO0_PWR+i);
		}
	}
	if (!strcmp(modem, "default")) {
		g4_power_off(g4,flag,2500);
	} else if (!strcmp(modem,"sim340dz")) {
		g4_power_off(g4,flag,1500);
	} else if (!strcmp(modem,"em200")) {
		g4_power_off(g4,flag,2500);
	} else if (!strcmp(modem,"m20")) {
		g4_power_off(g4,flag,800);
	} else {
		printk(KERN_ERR "opvxg4xx: modem parameter error!");
	}

	printk(KERN_INFO " done.\n");
	__g4_outl__(g4, g4->gpio2.base, 0, 1);   
}

static inline void g4_hardware_init(struct g4_card *g4)
{
	unsigned long flags;
	spin_lock_irqsave(&(g4->lock),flags); /*change to spin_lock, due to jiffies can not work when disable irq;*/

	g4_init_uart(g4);

	g4_init_pio0(g4);

	g4_init_pio2(g4);

	spin_unlock_irqrestore(&(g4->lock),flags);
}

static inline void g4_startCard(struct g4_card *g4)
{
	unsigned long flags;
	unsigned int err;

	spin_lock_irqsave(&(g4->lock), flags);

	__g4_outl__(g4, SWAP_MEM_BASE, 0, 1);                        /* start interrupts; */
	err = __g4_inl__(g4, SWAP_MEM_BASE, 1);

	if (debug & DEBUG_G4_SYS) {
		printk(KERN_ERR "err = %d\n", err);
	}

	__g4_outl__(g4, IRQ_BASE, 0x0040/sizeof(int), 0xffffffff);   /* clear all pending interrupts; */
	__g4_outl__(g4, IRQ_BASE, 0x0050/sizeof(int), 1<<16);        /* enable interrupt enable reg; */
	spin_unlock_irqrestore(&(g4->lock),flags);

	return;
}

static void opvx_hdlc_hard_xmit(struct dahdi_chan *sigchan)
{
	struct g4_span *myspan = sigchan->pvt;
	atomic_inc(&myspan->hdlc_pending);
}

static int g4_dchan_rx(struct g4_card *g4, int span) 
{
	unsigned int uart_base = 0;
	int rx_count = 0;
	int i = 0;
	unsigned char data = 0;
	unsigned int count_reg = 0;
	
/*Freedom add 2011-09-01 17:26*/
/************************************************************/
	unsigned int UART0_BASE = 0x000004c0;
	unsigned int UART1_BASE = 0x000004e0;
	unsigned int UART2_BASE = 0x00000500;
	unsigned int UART3_BASE = 0x00000520;
	if (G4_VERSION_CHECK(g4,1,1)) {
		UART0_BASE = 0x00000480;
		UART1_BASE = 0x000004c0;
		UART2_BASE = 0x00000500;
		UART3_BASE = 0x00000600;
	}
/************************************************************/

	switch (span) {
		case 0: uart_base = UART0_BASE; break;
		case 1: uart_base = UART1_BASE; break;
		case 2: uart_base = UART2_BASE; break;
		case 3: uart_base = UART3_BASE; break;
	}

	count_reg = __g4_inl__(g4, uart_base, 6);

	rx_count = count_reg;
	if (rx_count) {
		if ((g4->d_rx_idx[span] + rx_count) <= G4_SER_BUF_SIZE) {
			if (debug & DEBUG_G4_DCHAN) {
				printk(KERN_INFO "opvxg4xx: card %d span %d SER_RX [ ", g4->cardno,span);
			}

			for (i = 0; i < rx_count; i ++) {
				data = __g4_inl__(g4, uart_base, 0);
				if(debug & DEBUG_G4_DCHAN) {
					if (data == 0xd) {
						printk("\\r");
					} else if (data == 0xa) {
						printk("\\n");
					} else {
						printk("%c", data);
					}
				}
				if (g4->spans[span].span.flags & DAHDI_FLAG_RUNNING) {
					g4->d_rx_buf[span][g4->d_rx_idx[span]++] = data;
				}
			}
		
			if (debug & DEBUG_G4_DCHAN) {
				printk(" ]\n");
			}
		} else {
				printk(KERN_WARNING "opvxg4xx: RX buffer overflow on span %d, idx %d, cnt %d, size %d\n", 
							span, g4->d_rx_idx[span], rx_count, G4_SER_BUF_SIZE);
		}
	}

	return rx_count;
}

static int g4_hdlc_rx(struct g4_card *g4, int span) 
{
	int i = 0;
	struct g4_span *myspan = NULL;

	myspan = &g4->spans[span];
	if (!myspan) {
		return -1;
	}
	if ((g4->d_rx_idx[span]) && (!g4->d_rx_recv[span])) {//wait for date communication finish
		if (0 == g4->power_on_flag[span]) {
			if ((g4->d_rx_buf[span][0] == 'A' && g4->d_rx_buf[span][1] == 'T') ||
				(g4->d_rx_buf[span][0] == 'O' && g4->d_rx_buf[span][1] == 'K') ) {

				g4->power_on_flag[span] = 1;
				g4->power[span]=1;
			}
		}

		if ((g4->d_rx_idx[span]) && (debug & DEBUG_G4_HDLC)) {
#ifdef VIRTUAL_TTY
			if((g4->enable_gsm0710[span]==0)||(g4->gsm0710_mode[span]==0))
#endif
			{
				printk("opvxg4xx: HDLC card %d span %d --RX [ ", g4->cardno,span);
				for (i=0;i<g4->d_rx_idx[span]; i++) {
					if (g4->d_rx_buf[span][i] == 0xd) {
						printk("\\r");
					} else if (g4->d_rx_buf[span][i] == 0xa) {
						printk("\\n");
					} else if ((g4->d_rx_buf[span][i]) > 127||(g4->d_rx_buf[span][i]<32)) {
						printk("(0x%X)",g4->d_rx_buf[span][i]);
					} else {
						printk("%c", g4->d_rx_buf[span][i]);
					}
				}
				printk(" ]  %d \n", g4->d_rx_idx[span]);
			}
		}
		
#ifdef VIRTUAL_TTY
		if((g4->enable_gsm0710[span]==1)&&(g4->gsm0710_mode[span]==1)){
			gsm0710_extract_frames(g4,span);
		}
		else{
			dahdi_hdlc_putbuf(myspan->sigchan, g4->d_rx_buf[span],g4->d_rx_idx[span]);
			dahdi_hdlc_finish(myspan->sigchan); 
		}
#else
		dahdi_hdlc_putbuf(myspan->sigchan, g4->d_rx_buf[span],g4->d_rx_idx[span]);
		dahdi_hdlc_finish(myspan->sigchan); 
#endif
		g4->d_rx_idx[span] = 0;
	}
	else if ((0 == g4->power_on_flag[span]) && 
			 (g4->power_on_check_number) &&
		     ((g4->intcount - g4->power_on_check_number) > checknumber)) {

		g4->power_on_check_number = 0;
	}
	return 0;
}


static int g4_dchan_tx(struct g4_card *g4, int span) 
{
	unsigned int uart_base = 0;
	int left = 0;
	int i = 0;
	int count = 0;
	unsigned int count_reg = 0;
	int rd_ptr = 0;
	int wr_ptr = 0;
	struct g4_span *myspan = NULL;
	
/*Freedom add 2011-09-01 17:26*/
/************************************************************/
	unsigned int UART0_BASE = 0x000004c0;
	unsigned int UART1_BASE = 0x000004e0;
	unsigned int UART2_BASE = 0x00000500;
	unsigned int UART3_BASE = 0x00000520;
	if (G4_VERSION_CHECK(g4,1,1)) {
		UART0_BASE = 0x00000480;
		UART1_BASE = 0x000004c0;
		UART2_BASE = 0x00000500;
		UART3_BASE = 0x00000600;
	}
/************************************************************/
	
	switch (span) {
		case 0: uart_base = UART0_BASE; break;
		case 1: uart_base = UART1_BASE; break;
		case 2: uart_base = UART2_BASE; break;
		case 3: uart_base = UART3_BASE; break;
	}

	myspan = &g4->spans[span];
	if (myspan) {
		count_reg = __g4_inl__(g4, uart_base, 7);
		left = G4_FIFO_SIZE - (count_reg & 0x1F); /*  this code should be modified future, since we have 1024bytes fifo. ml */
		if (left >= 1 ) {

			if (debug > DEBUG_G4_DCHAN) {
				printk("opvxg4xx: SER_TX_COUNT_%d [wr_ptr %d rd_ptr %d free %d]\n", span, wr_ptr, rd_ptr, left);
			}
			if (g4->d_tx_idx[span] < left) {
				count = g4->d_tx_idx[span];
			} else {
				count = left;
			}
			if (debug & DEBUG_G4_DCHAN) {
				printk("opvxg4xx: card %d span %d SER_TX [ ", g4->cardno,span);
			}

			for (i = 0; i < count; i++) {
				if ((debug & DEBUG_G4_DCHAN))
				{
#ifdef VIRTUAL_TTY
					if(g4->gsm0710_mode[span]&&g4->enable_gsm0710[span])
						printk("0x%x ", g4->d_tx_buf[span][i]);
					else
#endif
						printk("%c", g4->d_tx_buf[span][i]);
				}
				__g4_outl__(g4, uart_base, 1, g4->d_tx_buf[span][i]);
			}
			
			if (debug & DEBUG_G4_DCHAN) {
				printk(" ]\n");
			}
			
			g4->d_tx_idx[span] -= count;

			if (g4->d_tx_idx[span] > 0) {
				memmove(&g4->d_tx_buf[span][0], &g4->d_tx_buf[span][i], g4->d_tx_idx[span]);
			}

		} else {
			if (debug & DEBUG_G4_DCHAN) {
				printk(KERN_DEBUG "opvxg4xx: span %d wanted to TX %d bytes but only %d bytes free fifo space\n", span, g4->d_tx_idx[span], left);
			}
		}
	}
	return i;
}

static int g4_hdlc_tx(struct g4_card *g4, int span) 
{
	struct g4_span *myspan = NULL;
	int res = 0;
	static unsigned char buf[G4_SER_BUF_SIZE];
	unsigned int size = sizeof(buf) / sizeof(buf[0]); 
	int i;
	
	myspan = &g4->spans[span];
	if (!myspan) {
		return -1;
	}
	if ((1 == g4->check_start[span]) &&
		(0 == g4->power_on_flag[span]))
    {
    		g4->check_start[span]=0;
#ifdef VIRTUAL_TTY
			if(g4->enable_gsm0710[span]==1)
			{
				g4->power_on_check_number = g4->intcount;
				gsm0710_write_frame(g4,span,0,"AT\r\n",4,GSM0710_UIH);
			}
			else
#endif
			{
				memcpy(&g4->d_tx_buf[span][g4->d_tx_idx[span]], "AT\r\n", 4);
				g4->d_tx_idx[span] += 4;
				g4->power_on_check_number = g4->intcount;
				if (debug & DEBUG_G4_HDLC) { 
				  printk("opvxg4xx: HDLC card %d span %d TX [ ", g4->cardno,span);
				  	for (i = 0; i < g4->d_tx_idx[span]; i++) {
						{
			                if (g4->d_tx_buf[span][i] == 0xd) {
			                    printk("\\r");
			                } else if (g4->d_tx_buf[span][i] == 0xa) {
			                    printk("\\n");
			                } else if (g4->d_tx_buf[span][i] == 0x0) {
			                    printk("NULL");
			                } else {
			                    printk("%c", g4->d_tx_buf[span][i]);
			                }
						}
		            }
		            printk(" ] %d \n", g4->d_tx_idx[span]);
				}
			}
	//} else if (g4->power_on_flag[span]) {
	} else {
		if (atomic_read(&myspan->hdlc_pending)) {
			int length=0;
			res= dahdi_hdlc_getbuf(myspan->sigchan, buf, &size);
			if (size > 0) {
				myspan->sigactive = 1;
#ifdef VIRTUAL_TTY
				if(g4->enable_gsm0710[span]==1){
					while(length<size)
					{
						if((size-length)<127)
						{
							gsm0710_write_frame(g4,span,1,&buf[length],size-length,GSM0710_UIH);
							length=size;
						}
						else
						{
							gsm0710_write_frame(g4,span,1,&buf[length],127,GSM0710_UIH);
							length+=127;
						}
					}
				}
				else
#endif
				{
					memcpy(&g4->d_tx_buf[span][g4->d_tx_idx[span]], buf, size);
					g4->d_tx_idx[span] += size;
				}
				if (res != 0) {
					if (debug & DEBUG_G4_HDLC) 
					{ 
			            printk("opvxg4xx: HDLC card %d span %d TX [ ", g4->cardno,span);
			            for (i = 0; i < size; i++) {
							if (buf[i] == 0xd) {
				           		printk("\\r");
				            } else if (buf[i] == 0xa) {
				            	printk("\\n");
				            } else if (buf[i] == 0x0) {
				            	printk("NULL");
				            } else {
				               printk("%c", buf[i]);
							}
			            }
			            printk(" ] %d \n", size);
			        }
					g4_dchan_tx(g4, span);
					myspan->sigactive = 0;
					atomic_dec(&myspan->hdlc_pending); 
				}
			}
		}
	}
	return 0;
}

static int g4_bchan_rx(struct g4_card *g4, int span)
{
	struct g4_span *myspan = NULL;

	myspan = &g4->spans[span];
	if (myspan) {
		volatile unsigned short* p;
		unsigned int i, j;
		
		j = __g4_inl__(g4, TDM0_BASE, 8);
		if(j & 0x8) {/* tdm controller is using high helf, pcm to mem is located at 1st half. */
			p = (unsigned short*) ((unsigned char*)(g4->pci_io) + DP_MEM_BASE);
		} else {
			p = (unsigned short*) ((unsigned char*)(g4->pci_io) + DP_MEM_BASE + 8*128);
		}
		
		for(i = 0; i < DAHDI_CHUNKSIZE; i++) {
			unsigned short line = p[i*64+span];
			g4->b_rx_buf[span][i] = DAHDI_LIN2X(swaphl(line), myspan->chans);
		}
	}

	return 0;
}

static int g4_bchan_tx(struct g4_card *g4, int span) 
{
	struct g4_span *myspan = NULL;

	myspan = &g4->spans[span];
	if (myspan) {
		volatile unsigned short* p;
		unsigned int i, j;
		
		j = __g4_inl__(g4, TDM0_BASE, 8);
		if(j & 0x8) { /* tdm controller is using high helf, mem to pcm is located at 2nd half. */
			p = (unsigned short*) ((unsigned char*)(g4->pci_io) + DP_MEM_BASE + 2048);
		} else {
			p = (unsigned short*) ((unsigned char*)(g4->pci_io) + DP_MEM_BASE + 2048 + 8*128);
		}

		for(i = 0; i < DAHDI_CHUNKSIZE; i++) {
			unsigned short line = DAHDI_XLAW(g4->b_tx_buf[span][i], myspan->chans);
			p[i*64+span] = swaphl(line);
		}
	}

	return 0;
}

static inline void g4_run(struct g4_card *g4)
{
	int s=0;
	struct g4_span *myspan = NULL;

	for (s=0; s<g4->g4spans; s++) {
		myspan = &g4->spans[s];
		if (myspan) {
			if (myspan->span.flags & DAHDI_FLAG_RUNNING) {
				dahdi_transmit(&myspan->span);
				g4_bchan_tx(g4, s);
				g4_hdlc_tx(g4, s);
			}
			
			if (myspan->span.flags & DAHDI_FLAG_RUNNING) {
				g4_hdlc_rx(g4, s);
				g4_bchan_rx(g4, s);
				dahdi_receive(&myspan->span);
			}
		} 
	}
}

static void irq_tasklet_handler(unsigned long data)
{
	struct g4_card *g4=(struct g4_card *)data;
	int s = 0;
	
	g4_run(g4); /* realwork in this function;     */

	for (s = 0; s < g4->g4spans; s++) {
		g4->d_rx_recv[s] = g4_dchan_rx(g4, s);
		if (g4->d_tx_idx[s]) {
			g4_dchan_tx(g4, s); /* sumfin left to transmit */
		}
	}
}

DAHDI_IRQ_HANDLER(g4_interrupt) 
{
	struct g4_card *g4 = dev_id;
	unsigned int i;

	if (!g4 || g4->dead) {
		printk(KERN_CRIT "opvxg4xx: ===return from dead\n");
		return IRQ_NONE;
	}

	if (!g4->pci_io ) {
		printk(KERN_CRIT "opvxg4xx: no pci mem/io\n");
		return IRQ_NONE;
	}
	
	/* simply clear the irq and return, for test. ml */
	i = __g4_inl__(g4, IRQ_BASE, 0x0040/sizeof(int));
	if(i & ( 1<<16)) {
		__g4_outl__(g4, IRQ_BASE, 0x0040/sizeof(int), i); /* simply clear it; */
	} else {
		return IRQ_NONE;
	}
	g4->intcount ++;
#if 0
	g4_run(g4); /* realwork in this function;     */

	for (s = 0; s < g4->g4spans; s++) {
		g4->d_rx_recv[s] = g4_dchan_rx(g4, s);
		if (g4->d_tx_idx[s]) {
			g4_dchan_tx(g4, s); /* sumfin left to transmit */
		}
	}
#else
	tasklet_schedule(&g4->irq_tasklet);
#endif
	return IRQ_HANDLED;
}

static inline struct g4_span *g4_from_span(struct dahdi_span *span)
{
        return container_of(span, struct g4_span, span);
}

static int g4_open(struct dahdi_chan *chan) {
	try_module_get(THIS_MODULE);
	return 0;
}

static int g4_close(struct dahdi_chan *chan) {
	module_put(THIS_MODULE);
	return 0;
}

#if (DAHDI_VER_NUM >= 2500)
static int g4_chanconfig(struct file *file, struct dahdi_chan *chan, int sigtype) {
#else  //(DAHDI_VER_NUM >= 2500)
static int g4_chanconfig(struct dahdi_chan *chan,int sigtype) {
#endif //(DAHDI_VER_NUM >= 2500)
//	if(debug)
//		printk(KERN_INFO "chan_config sigtype=%d\n",sigtype);
	return 0;
}

#if (DAHDI_VER_NUM >= 2500)
static int g4_spanconfig(struct file *file, struct dahdi_span *span, struct dahdi_lineconfig *lc) {
#else  //(DAHDI_VER_NUM >= 2500)
static int g4_spanconfig(struct dahdi_span *span,struct dahdi_lineconfig *lc) {
#endif //(DAHDI_VER_NUM >= 2500)
	return 0;
}

#if (DAHDI_VER_NUM >= 2500)
static int g4_startup(struct file *file, struct dahdi_span *span)
#else  //(DAHDI_VER_NUM >= 2500)
static int g4_startup(struct dahdi_span *span) 
#endif //(DAHDI_VER_NUM >= 2500)
{
	struct g4_card *g4 = g4_from_span(span)->owner;
	int running;
	unsigned long flags;
	
	if (g4 == NULL) {
		printk(KERN_INFO "opvxg4xx: no card for span at startup!\n");
	}

	spin_lock_irqsave(&(g4->lock),flags);
	running = span->flags & DAHDI_FLAG_RUNNING;
	spin_unlock_irqrestore(&g4->lock,flags);

	if (!running) {
		if (!g4->power[span->offset]) {
	//			g4xx_switch_on(g4, span->offset);
		}
		spin_lock_irqsave(&(g4->lock),flags);
		span->flags |= DAHDI_FLAG_RUNNING;
		spin_unlock_irqrestore(&g4->lock,flags);
	} else {
		printk(KERN_INFO "opvxg4xx: already running\n");
		return 0;
	}

	return 0;
}

/* this function will be abended. miaolin */
static int g4_shutdown(struct dahdi_span *span) 
{
	int running;
	struct g4_card *g4 = g4_from_span(span)->owner;
	unsigned long flags;

	if (g4 == NULL) {
		printk(KERN_INFO "opvxg4xx: no card for span at shutdown!\n");
	}

	spin_lock_irqsave(&(g4->lock),flags);
	running = span->flags & DAHDI_FLAG_RUNNING;

	if (running) {
		span->flags &= ~DAHDI_FLAG_RUNNING;
		if (g4->power[span->offset]) {
	//			g4xx_switch_off(g4, span->offset);
		}
	}
	spin_unlock_irqrestore(&g4->lock,flags);

	return 0;
}

static int g4_ioctl(struct dahdi_chan *chan, unsigned int cmd, unsigned long data) {
	struct g4_span *myspan = chan->pvt;
	struct g4_card *g4 = myspan->owner;
	int span=myspan->span.offset;
	unsigned long flags=0;
#ifdef VIRTUAL_TTY
	unsigned char close_channel = {GSM0710_DISC | GSM0710_PF};
	unsigned char close_mux[2] = {GSM0710_C_CLD | GSM0710_CR, 1};
	char mux_command[128];
#endif
	switch(cmd) {
#ifdef VIRTUAL_TTY
		case OPVXG4XX_INIT:
			if(g4->enable_gsm0710[span]==1){
				spin_lock_irqsave(&(g4->lock),flags);
				gsm0710_setup_step(g4,span,0);
				spin_unlock_irqrestore(&g4->lock,flags);
				return 0;
			}
			else
				return -1;
			break;
		case OPVXG4XX_SET_MUX:
			if(g4->enable_gsm0710[span]==1){
				int len=0;
				int ret=0;
				memset(mux_command,0,sizeof(mux_command));
				len=strlen((char*)data);
				copy_from_user (mux_command, (char *)data,len);
				mux_command[len]='\r';
				mux_command[len+1]='\n';
				spin_lock_irqsave(&g4->lock,flags);
				at_command(g4,span,mux_command,len+2);
				g4_dchan_tx(g4, span); /* sumfin left to transmit */
				g4->gsm0710_mode[span]=1;
				spin_unlock_irqrestore(&g4->lock,flags);
				return 0;
			}
			else
				return -1;
			break;
		case OPVXG4XX_CREATE_CONCOLE:
			if(g4->enable_gsm0710[span]==1){
				spin_lock_irqsave(&g4->lock,flags);
				gsm0710_setup_step(g4,span,2);
				spin_unlock_irqrestore(&g4->lock,flags);
				return 0;
			}
			else
				return -1;
			break;
		case OPVXG4XX_CREATE_DAHDI:
			if(g4->enable_gsm0710[span]==1){
				spin_lock_irqsave(&g4->lock,flags);
				gsm0710_setup_step(g4,span,3);
				spin_unlock_irqrestore(&g4->lock,flags);
				return 0;
			}
			else
				return -1;
			break;
		case OPVXG4XX_CREATE_EXT:
			if(g4->enable_gsm0710[span]==1){
				spin_lock_irqsave(&g4->lock,flags);
				gsm0710_setup_step(g4,span,4);
				spin_unlock_irqrestore(&g4->lock,flags);
				return 0;
			}
			else
				return -1;
			break;
		case OPVXG4XX_CLEAR_MUX:
			g4->gsm0710_mode[span]=0;
			if(g4->enable_gsm0710[span]==1){
				spin_lock_irqsave(&g4->lock,flags);
				/*gsm0710_write_frame(g4,span,1, &close_channel, 1, GSM0710_UIH);
				g4_dchan_tx(g4, span);
				mdelay(200);
				gsm0710_write_frame(g4,span,2, &close_channel, 1, GSM0710_UIH);
				g4_dchan_tx(g4, span);
				mdelay(200);*/
				gsm0710_write_frame(g4,span,0, close_mux, 2, GSM0710_UIH);
				g4_dchan_tx(g4, span);
				spin_unlock_irqrestore(&g4->lock,flags);
				return 0;
			}
			else
				return -1;
			break;
		case OPVXG4XX_ENABLE_TTY_MODULE:
			spin_lock_irqsave(&g4->lock,flags);
			g4->enable_gsm0710[span]=1;
			spin_unlock_irqrestore(&g4->lock,flags);
			break;
		case OPVXG4XX_DISABLE_TTY_MODULE:
			spin_lock_irqsave(&g4->lock,flags);
			g4->enable_gsm0710[span]=0;
			spin_unlock_irqrestore(&g4->lock,flags);
			break;
		case OPVXG4XX_GET_MUX_STAT:
			if(g4->enable_gsm0710[span]==1){
				int stat=g4->gsm0710_mode[span];
				put_user(stat, (int __user *)data);
				return 0;
			}
			else
				return -1;
			break;
		case OPVXG4XX_GET_TTY_MODULE:
		{
			int stat=g4->enable_gsm0710[span];
			put_user(stat, (int __user *)data);
			return 0;
			break;
		}
		case OPVXG4XX_CONNECT_DAHDI:
			spin_lock_irqsave(&g4->lock,flags);
			memset(g4->d_rx_buf[span],0,G4_SER_BUF_SIZE);
			memset(g4->d_tx_buf[span],0,G4_SER_BUF_SIZE);
			g4->d_rx_idx[span]=0;
			g4->d_tx_idx[span]=0;
			spin_unlock_irqrestore(&g4->lock,flags);
			break;
#endif
		case OPVXG4XX_SPAN_INIT:
		{
			int stat=0;
			stat=(unsigned int)data;
			//copy_from_user (stat, (unsigned int*)(&data),sizeof(int));
			return g4_gsm_power_on(g4,2500,span,stat);
			break;
		}
		case OPVXG4XX_SPAN_REMOVE:
		{
			return g4_gsm_power_off(g4,800,span);
			break;
		}
		case OPVXG4XX_SPAN_STAT:
		{
			unsigned char stat=g4->power_on_flag[span];
			put_user(stat, (unsigned char __user *)data);
			return 0;
		}	
		default:
			return -ENOTTY;
	}
	return 0;
}

#ifdef DAHDI_SPAN_OPS
static const struct dahdi_span_ops g4_span_ops = {
	.owner = THIS_MODULE,
	.spanconfig = g4_spanconfig,
	.chanconfig = g4_chanconfig,
	.startup= g4_startup,
	.shutdown = g4_shutdown,
	.open = g4_open,
	.close = g4_close,
	.ioctl = g4_ioctl,
	.hdlc_hard_xmit = opvx_hdlc_hard_xmit,
};
#endif

static int g4_init_span(struct g4_span *myspan, struct g4_card *g4, int offset) 
{
	int i;
	
	memset(&myspan->span,0,sizeof(struct dahdi_span));
	sprintf(myspan->span.name,"opvxg4xx/%d/%d",g4->cardno, offset + 1);
	myspan->span.spantype = SPANTYPE_DIGITAL_BRI_TE;
	switch (g4->type) {
	case 0x0104:
		sprintf(myspan->span.desc,"OpenVox G400P GSM/CDMA PCI Card %d", g4->cardno);    /* we always be 4 ports card */
#if (DAHDI_VER_NUM < 2600)
		myspan->span.manufacturer = "OpenVox";
#endif //(DAHDI_VER_NUM < 2600)
		break;
	}

#ifdef DAHDI_SPAN_MODULE
	myspan->span.owner = THIS_MODULE;
#endif
#ifdef DAHDI_SPAN_OPS
	myspan->span.ops    = &g4_span_ops;
#else
	myspan->span.spanconfig = g4_spanconfig;
	myspan->span.chanconfig = g4_chanconfig;
	myspan->span.startup = g4_startup;
	myspan->span.shutdown = g4_shutdown;
	myspan->span.maint = NULL;
	myspan->span.rbsbits = NULL;
	myspan->span.open = g4_open;
	myspan->span.close = g4_close;
	myspan->span.ioctl = g4_ioctl;
	myspan->span.hdlc_hard_xmit = opvx_hdlc_hard_xmit;
	myspan->span.pvt = g4;
#endif
	myspan->span.flags = 0;
	myspan->span.chans = myspan->_chans;
	myspan->span.channels = 2;
	for (i = 0; i < myspan->span.channels; i++) {
		myspan->_chans[i] = &myspan->chans[i];
	}

#if (DAHDI_VER_NUM < 2600)
	myspan->span.irq = g4->pcidev->irq; 
#endif //(DAHDI_VER_NUM < 2600)

	myspan->span.deflaw = DAHDI_LAW_ALAW;
	myspan->span.linecompat = DAHDI_CONFIG_CCS | DAHDI_CONFIG_AMI;
#if !(DAHDI_VER_NUM >= 2500)
	init_waitqueue_head(&myspan->span.maintq);
#endif //!(DAHDI_VER_NUM >= 2500)
	myspan->owner       = g4;
	myspan->span.offset = offset;

	memset(&(myspan->chans[0]),0x0,sizeof(struct dahdi_chan));
	sprintf(myspan->chans[0].name,"opvxg4xx/%d/%d/%d", g4->cardno, offset + 1, 0);
	myspan->chans[0].pvt = myspan;
	myspan->chans[0].sigcap =  DAHDI_SIG_CLEAR;
	myspan->chans[0].chanpos = 1; 

	memset(&(myspan->chans[1]),0x0,sizeof(struct dahdi_chan));
	sprintf(myspan->chans[1].name,"opvxg4xx/%d/%d/%d", g4->cardno, offset + 1, 1);
	myspan->chans[1].pvt = myspan;
	myspan->chans[1].sigcap =  DAHDI_SIG_HARDHDLC;
	myspan->sigchan = &myspan->chans[1]; 
	myspan->chans[1].chanpos = 2;
	myspan->sigactive = 0; 
	atomic_set(&myspan->hdlc_pending, 0); 

	/* setup B channel buffer */
	memset(g4->b_rx_buf[offset],0x0,sizeof(g4->b_rx_buf[offset]));
	myspan->chans[0].readchunk = g4->b_rx_buf[offset];
	memset(g4->b_tx_buf[offset],0x0,sizeof(g4->b_tx_buf[offset]));
	myspan->chans[0].writechunk = g4->b_tx_buf[offset];

#if (DAHDI_VER_NUM < 2600)
	if (dahdi_register(&myspan->span,0)) {
		printk(KERN_INFO "opvxg4xx: unable to register dahdi span!\n");
		return -1;
	}
#endif //(DAHDI_VER_NUM < 2600)

	return 0;
}

static int __devinit g4_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct g4_card *g4 = NULL;
	struct g4_span *myspan = NULL;
	int i = 0;
	unsigned long status;
	multi_gsm = pdev;

	if (pci_enable_device(pdev)) {
		return -1;
	}
		
	g4 = kmalloc(sizeof(struct g4_card),GFP_KERNEL);
	if (!g4) {
		printk(KERN_WARNING "opvxg4xx: unable to kmalloc!\n");
		pci_disable_device(pdev);
		return -ENOMEM;
	}
	memset(g4, 0x0, sizeof(struct g4_card));

	spin_lock_init(&g4->lock);
	g4->pcidev = pdev;

	if (!pdev->irq) {
		printk(KERN_WARNING "opvxg4xx: no irq!\n");
	} else {
		g4->irq = pdev->irq;
	}

	if (debug & DEBUG_G4_SYS) {   
		printk(KERN_DEBUG "bus number = %d devfn = %d irq = %d\n", 
			pdev->bus->number, pdev->devfn, g4->irq);
	}

	/* We only have 1 64KB memory region. */
	g4->pci_io_phys = pdev->resource[0].start;
	if (debug & DEBUG_G4_SYS) {   
		printk(KERN_DEBUG "pci-io-phys = %lx\n", g4->pci_io_phys);
	}
	if (!g4->pci_io_phys) {
		printk(KERN_WARNING "opvxg4xx: no iomem!\n");
		pci_disable_device(pdev);
		return -EIO;
	}

	g4->iomem_size = (pdev->resource[0].end - pdev->resource[0].start + 1);   /* our region is at bar0 */
	if (debug & DEBUG_G4_SYS) {
		printk(KERN_DEBUG "opvxg4xx: iomem at %lx size %ld\n", g4->pci_io_phys, g4->iomem_size);
	}

	if (check_mem_region(g4->pci_io_phys, g4->iomem_size)) {
		printk(KERN_WARNING "opvxg4xx: iomem already in use!\n");;
		pci_disable_device(pdev);
		return -EBUSY;
	}

	request_mem_region(g4->pci_io_phys, g4->iomem_size, "opvxg4xx");
	g4->pci_io = ioremap(g4->pci_io_phys, g4->iomem_size); /* 64kb */
	if (debug & DEBUG_G4_SYS) {
		printk(KERN_DEBUG "opvxg4xx: iomem remote to %lx \n", (unsigned long)g4->pci_io);
	}	
	tasklet_init(&(g4->irq_tasklet),irq_tasklet_handler,(unsigned long)g4);
	
	if (request_irq(g4->irq, g4_interrupt, DAHDI_IRQ_SHARED, "opvxg4xx", g4)) {
		printk(KERN_WARNING "opvxg4xx: unable to register irq\n");
		tasklet_kill(&(g4->irq_tasklet));
		release_mem_region(g4->pci_io_phys, g4->iomem_size);
		kfree(g4);
		pci_disable_device(pdev);
		return -EIO;
	}

	g4->fwversion = __g4_inl__(g4, SWAP_MEM_BASE, 2);
	g4->card_name = "G400P";
	g4->rst_flag = __g4_inl__(g4, SWAP_MEM_BASE, 3);

	g4->type = pdev->subsystem_device;
	switch (g4->type) {
		case PCI_SUBSYSTEM_ID_OPENVOX:
			printk(KERN_NOTICE
				"Found an OpenVox %s: Version %x.%x\n", g4->card_name, g4->fwversion>>16, g4->fwversion&0xffff);
			break;
	}

/*Freedom del 2011-09-01 17:26*/
/************************************************************/
#if 0
	if (G4_VERSION_CHECK(g4,1,1)) {
		UART0_BASE = 0x00000480;
		UART1_BASE = 0x000004c0;
		UART2_BASE = 0x00000500;
		UART3_BASE = 0x00000600;
	}
#endif 
/************************************************************/

	pci_set_drvdata(pdev, g4);

	g4_hardware_init(g4);

	status = __g4_pio0_getbits(g4, PIO0_STATUS_MASK | PIO0_SLOTUSE_MASK);
	for (i = 0; i < 4; i ++) {
		if (!(status&(1<<(PIO0_SLOTUSE+i)))) {
			g4->flags |= (1 << i);
		}
		printk(KERN_NOTICE "card %d opvxg4xx: slot %d is %s\n", g4->cardno, i, status&(1<<(PIO0_SLOTUSE+i)) ? "Empty" : "Installed" );
	}
	
#if (DAHDI_VER_NUM >= 2600)
	g4->ddev = dahdi_create_device();
#endif //(DAHDI_VER_NUM >= 2600)

	g4->g4spans = 4;    /* we always have 4 spans */
	g4_spans += g4->g4spans;

	g4->cardno = g4_dev_count ++;
	for (i = 0; i < g4->g4spans; i++) {
		myspan = &g4->spans[i];
		if (g4->flags & (1 << i)) 
		{
			g4_init_span(myspan, g4, i);
#if (DAHDI_VER_NUM >= 2600)
			list_add_tail(&myspan->span.device_node, &g4->ddev->spans);
#endif //(DAHDI_VER_NUM >= 2600)

#ifdef VIRTUAL_TTY
			g4->gsm_tty_table[i] = kmalloc(sizeof(*g4->gsm_tty_table[0]), GFP_KERNEL);
			if (!g4->gsm_tty_table[i]) {
				printk("Create gsm_tty_table failed(%d)\n",i);
			} else {
				spin_lock_init(&(g4->gsm_tty_table[i]->lock));
				g4->gsm_tty_table[i]->open_count = 0;
				g4->gsm_tty_table[i]->channel = tty_sum++;
				g4->gsm_tty_table[i]->cardno = g4->cardno;
				g4->gsm_tty_table[i]->span = i;
				g4->gsm_tty_table[i]->tty = NULL;
				printk(KERN_NOTICE "opvxg4xx: card %d slot %d is ttymode\n", g4->cardno,i);
			}
#endif
		}
	}
	
#if (DAHDI_VER_NUM >= 2600)		
	g4->ddev->manufacturer = "OpenVox";
	g4->ddev->devicetype = g4->variety;
	g4->ddev->location = kasprintf(GFP_KERNEL, "PCI Bus %02d Slot %02d",
				       g4->pcidev->bus->number,
				       PCI_SLOT(g4->pcidev->devfn) + 1);
	if (!g4->ddev->location) {
		release_mem_region(g4->pci_io_phys, g4->iomem_size);
		tasklet_kill(&(g4->irq_tasklet));
		kfree(g4);
		dahdi_free_device(g4->ddev);
		pci_disable_device(pdev);
		return -ENOMEM;
	}
	
	if (dahdi_register_device(g4->ddev, &g4->pcidev->dev)) {
		dev_err(&g4->pcidev->dev, "Unable to register device.\n");	
		tasklet_kill(&(g4->irq_tasklet));
		release_mem_region(g4->pci_io_phys, g4->iomem_size);
		kfree(g4);
		kfree(g4->ddev->location);
		dahdi_free_device(g4->ddev);
		pci_disable_device(pdev);
		return -1;
	}
#endif //(DAHDI_VER_NUM >= 2600)
	
	g4_startCard(g4);
	g4_switch_on_all(g4);
	g4_init_i2c(g4);

#ifdef VIRTUAL_TTY
	if(g4_card_sum < sizeof(g_g4)/sizeof(g_g4[0])) {
		g_g4[g4_card_sum++] = g4;
	} else {
		printk("g4_card over\n");
	}
	for (i = 0; i < g4->g4spans; i++) {
		if (g4->flags & (1 << i)) 
		{
			gsm0710_init(g4,i);
		}
	}
#endif //VIRTUAL_TTY

#ifdef G400E_PW_KEY
	pw_key_entry_init(g4);
#endif //G400E_PW_KEY
	
	return 0;
}

static void __devexit g4_remove(struct pci_dev *pdev)
{
	struct g4_card *g4 = pci_get_drvdata(pdev);
	struct g4_span *myspan = NULL;
	unsigned long flags;
	unsigned long pci_io_phys;
	unsigned long iomem_size;
	void *pci_io;
	int i = 0;
	if (g4) {

#ifdef G400E_PW_KEY
		pw_key_entry_exit(g4);
#endif //G400E_PW_KEY

		for (i = 0; i < g4->g4spans; i ++) {
			if (g4->flags & (1 << i)) {
				myspan = &g4->spans[i];
				myspan->span.flags |= DAHDI_FLAG_RUNNING;
			}
		}
		
		__g4_outl__(g4, SWAP_MEM_BASE, 0, 0);                /* disable generate interrupts; */
		__g4_outl__(g4, IRQ_BASE, 0x0050/sizeof(int), 0);    /* disable interrupt enable reg; */
		g4_switch_off_all(g4);
		spin_lock_irqsave(&g4->lock,flags);
		g4->dead = 1;
		
		pci_io = g4->pci_io;
		pci_io_phys = g4->pci_io_phys;
		iomem_size = g4->iomem_size;
		g4->pci_io = 0;
		spin_unlock_irqrestore(&g4->lock,flags);
#if (DAHDI_VER_NUM < 2600)
		for (i = 0; i < g4->g4spans; i ++) {
			if (g4->flags & (1 << i)) {
				myspan = &g4->spans[i];
				dahdi_unregister(&myspan->span);
			}
#ifdef VIRTUAL_TTY
			gsm0710_exit(g4,i);
#endif
			if (debug & DEBUG_G4_MSG) {
				printk(KERN_DEBUG "opvxg4xx: unregistered card %d span %d.\n",g4->cardno,i);
			}
		}
#else	
		dahdi_unregister_device(g4->ddev);
		dahdi_free_device(g4->ddev);
#endif //(DAHDI_VER_NUM < 2600)
		iounmap((void *) pci_io);

		/* release mem region */
		release_mem_region(pci_io_phys, iomem_size);
		
		spin_lock_irqsave(&g4->lock,flags);

		/* free irq */
		free_irq(g4->irq,g4);
		/* init PCI_COMMAND */
		pci_write_config_word(pdev, PCI_COMMAND, 0);  

		/* disable pci device */
		pci_disable_device(pdev);
		tasklet_kill(&(g4->irq_tasklet));
		spin_unlock_irqrestore(&g4->lock,flags);
#ifdef VIRTUAL_TTY
		/* shut down all of the timers and free the memory */
		for (i = 0; i < g4->g4spans; ++i) {
			if (g4->gsm_tty_table[i]) {
				/* close the port */
				while (g4->gsm_tty_table[i]->open_count)
					do_close(g4->gsm_tty_table[i]);
				/* shut down our timer and free the memory */
				kfree(g4->gsm_tty_table[i]);
				g4->gsm_tty_table[i] = NULL;
			}
		}
#endif //VIRTUAL_TTY
		/* kfree g4 */
		kfree(g4);
		g4 = NULL;
		
		//Freedom add 2011-08-23
		/////////////////////////////////////////////////////////////////
		pci_set_drvdata(pdev, NULL);
		/////////////////////////////////////////////////////////////////
		
		printk(KERN_INFO "opvxg4xx: shutdown OpenVox G4XX GSM/CDMA cards.\n");
	}

	dev_info(&pdev->dev, "Driver unloaded.\n");

	return;
}

static struct pci_device_id g4_ids[] = {
	{ PCI_VENDOR_ID_OPENVOX, PCI_DEVICE_ID_OPENVOX, PCI_VENDOR_ID_OPENVOX, PCI_SUBSYSTEM_ID_OPENVOX, 0, 0, 0 },
	{ 0, }
};

static struct pci_driver g4_driver = {
	.name = "opvxg4xx",
	.probe = g4_probe,
	.remove = __devexit_p(g4_remove),
	.id_table = g4_ids,
};

static int __init g4_init(void)
{
#ifdef VIRTUAL_TTY
	int i;
	int retval;

	for(i=0; i<sizeof(g_g4)/sizeof(g_g4[0]); i++) {
		g_g4[i] = NULL;
	}
#endif //VIRTUAL_TTY

	if (dahdi_pci_module(&g4_driver)) {
		return -ENODEV;
	}

#ifdef VIRTUAL_TTY
	if(tty_sum <= 0) {
		printk("No has gsm tty to install\n");
		return 0; 
	}
	
	/* allocate the tty driver */
	gsm_tty_driver = alloc_tty_driver(tty_sum);
	if (!gsm_tty_driver) {
		printk("alloc_tty_driver failed\n");
		return -ENOMEM;
	}

	/* initialize the tty driver */
	gsm_tty_driver->owner = THIS_MODULE;
	gsm_tty_driver->driver_name = "TTY GSM Card";
	gsm_tty_driver->name = "ttyGSM";

	/* no more devfs subsystem */
	gsm_tty_driver->major = GSM_TTY_MAJOR;
	gsm_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	gsm_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	gsm_tty_driver->flags = TTY_DRIVER_RESET_TERMIOS | TTY_DRIVER_REAL_RAW ;

	/* no more devfs subsystem */
	gsm_tty_driver->init_termios = tty_std_termios;
	gsm_tty_driver->init_termios.c_iflag = 0;
	gsm_tty_driver->init_termios.c_oflag = 0;

	switch (baudrate) {
		case 9600:          
			gsm_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD; 
			break;
		case 19200:
			gsm_tty_driver->init_termios.c_cflag = B19200 | CS8 | CREAD; 
			break;
		case 38400:
			gsm_tty_driver->init_termios.c_cflag = B38400 | CS8 | CREAD; 
			break;
		case 57600:
			gsm_tty_driver->init_termios.c_cflag = B57600 | CS8 | CREAD; 
			break;
		case 115200:
		default:
			gsm_tty_driver->init_termios.c_cflag = B115200 | CS8 | CREAD; 
			break;
	}

	gsm_tty_driver->init_termios.c_lflag = 0;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)
	gsm_tty_driver->init_termios.c_ispeed = baudrate;
	gsm_tty_driver->init_termios.c_ospeed = baudrate;
#endif //LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)
	tty_set_operations(gsm_tty_driver, &serial_ops);

	/* register the tty driver */
	retval = tty_register_driver(gsm_tty_driver);
	if (retval) {
		printk(KERN_ERR "failed to register gsm tty driver\n");
		put_tty_driver(gsm_tty_driver);
		gsm_tty_driver = NULL;
		return retval;
	}

	printk("GSM tty install\n");
	return retval;

#endif //VIRTUAL_TTY

	return 0;
}

static void __exit g4_cleanup(void)
{
#ifdef VIRTUAL_TTY
	int i;
	
	if(gsm_tty_driver) {
		for (i = 0; i < tty_sum; ++i)
			tty_unregister_device(gsm_tty_driver, i);
		tty_unregister_driver(gsm_tty_driver);
	}
#endif //VIRTUAL_TTY

	pci_unregister_driver(&g4_driver);
}

module_param(debug, int, 0600);
module_param(sim, int, 0600);
module_param(baudrate, long, 0600);
module_param(adcgain, int, 0600);           /* adc gain of codec, in db, from -42 to 20, mute=21; */
module_param(dacgain, int, 0600);           /* dac gain of codec, in db, from -42 to 20, mute=21; */
module_param(sidetone, int, 0600);          /* digital side tone, 0=mute, others can be -3 to -21db, step by -3; */
module_param(inputgain, int, 0600);         /* input buffer cain, can be 0, 6, 12,24db. */
module_param(modem, charp, 0600);           /* modem */
module_param(burn, int, 0600);              /* reset flag */
module_param(checknumber, int, 0600);       /* check AT number */

#ifdef VIRTUAL_TTY
module_param_array(ttymode,int,&ttymode_len,0600);
#endif //VIRTUAL_TTY

MODULE_DESCRIPTION("OpenVox GSM/CDMA card Dahdi driver");
MODULE_AUTHOR("Miao Lin <lin.miao@openvox.cn>");
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

MODULE_DEVICE_TABLE(pci, g4_ids);
module_init(g4_init);
module_exit(g4_cleanup);

