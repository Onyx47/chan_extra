/*
 * base.c - Dahdi driver for the OpenVox GSM PCI cards
 *
 * Copyright (C) 2005-2010 OpenVox Communication Co. Ltd,
 * All rights reserved.
 *
 * Based on previous works and written by J.A. Deniz <odi@odicha.net>
 * Written by Mark.liu <mark.liu@openvox.cn>
 *
 * $Id: opvxg4xx.h 324 2011-04-15 09:31:32Z liuyuan $
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 *
 */

#ifndef _OPVXG4XX_H_
#define _OPVXG4XX_H_

#define G400E_PW_KEY

#define G4_FIFO_SIZE      1024
#define G4_SPANS          4
#define G4_SER_BUF_SIZE   4096
#define G4_CHAN_NUM       2

#ifndef PCI_VENDOR_ID_OPENVOX
#define PCI_VENDOR_ID_OPENVOX       0x1b74
#define PCI_DEVICE_ID_OPENVOX       0x0100
#define PCI_SUBSYSTEM_ID_OPENVOX    0x0104
#endif

#define PIO0_DTR         0
#define PIO0_DCD         4
#define PIO0_RI          8
#define PIO0_STATUS     12
#define PIO0_RST        16
#define PIO0_PWR        20
#define PIO0_PORTSEL    24
#define PIO0_SLOTUSE    28

#define PIO0_DTR_MASK       (0xfu<<PIO0_DTR)
#define PIO0_DCD_MASK       (0xfu<<PIO0_DCD)
#define PIO0_RI_MASK        (0xfu<<PIO0_RI)
#define PIO0_STATUS_MASK    (0xfu<<PIO0_STATUS)
#define PIO0_RST_MASK       (0xfu<<PIO0_RST)
#define PIO0_PWR_MASK       (0xfu<<PIO0_PWR)
#define PIO0_PORTSEL_MASK   (0xfu<<PIO0_PORTSEL)
#define PIO0_SLOTUSE_MASK   (0xfu<<PIO0_SLOTUSE)

/* debug level */
#define DEBUG_G4_NONE     0x0000
#define DEBUG_G4_SYS      0x0001
#define DEBUG_G4_MSG      0x0002
#define DEBUG_G4_STATE    0x0004
#define DEBUG_G4_HDLC     0x0008
#define DEBUG_G4_DCHAN    0x0010
#define DEBUG_G4_CODEC    0x0020
#define DEBUG_G4_I2C      0x0040

/* External device address */
#define SWAP_MEM_BASE   0x00000000
#define DP_MEM_BASE     0x00001000
#define PIO0_BASE       0x000005e0
#define PIO2_BASE       0x000005f0
#define IRQ_BASE        0x0000c000
#define TDM0_BASE       0x00000400
#define BUS_FREQ        66000000
#define I2_PRESCALE     ((BUS_FREQ/5/100000)) /* prescale = busfreq/(5*I2cFreq), now we use 100k I2c Freq; */
#define I2C0_BASE       0x00000540
#define I2C1_BASE       0x00000560
#define I2C2_BASE       0x00000580
#define I2C3_BASE       0x000005a0
#define TDM0_BASE       0x00000400

/* I2C control and flag bits */
#define I2C_FREQ_LOW        0x0
#define I2C_FREQ_HIGH       0x1
#define I2C_CTRL            0x2
#define I2C_TX              0x3
#define I2C_RX              0x3
#define I2C_CMD             0x4
#define I2C_STATUS          0x4

/* I2C control and flag bits */
#define I2C_CTRL_ENABLE     0X80    /* enable I2C core */
#define I2C_CTRL_IRQEN      0X40    /* enable interrupt */
#define I2C_CMD_STA         0x80    /* generate(repeated) start condition */
#define I2C_CMD_STO         0x40    /* generate stop condition */
#define I2C_CMD_RD          0x20    /* read from slave */
#define I2C_CMD_WR          0x10    /* write to slave */
#define I2C_CMD_ACK         0x08    /* ACK, when a receiver, sent ACK(0) or NACK(1) */
#define I2C_CMD_IACK        0x01    /* Interrupt acknowledge, set 1 clear */
#define I2C_STATUS_RXACK    0x80    /* Received acknowledge from slave. 0= Acknowledge received, 1= no */
#define I2C_STATUS_BUSY     0x40    /* I2C bus busy */
#define I2C_STATUS_AL       0x20    /* Arbitration lost */
#define I2C_STATUS_TIP      0x02    /* Trsnasfer in progress */
#define I2C_STATUS_IF       0x01    /* interrupt is pending */
#define I2C_TIMEOUT         0x64    /* wait 10 ms before i2c time out; */

typedef struct g4_gpio
{
	unsigned int base;      /* base address */
	unsigned int dir;       /* io direction register value */
	unsigned int io;        /* previous io register value */
} g4_gpio;

typedef struct g4_span {
	struct g4_card *owner;
	struct dahdi_span span;
	struct dahdi_chan chans[2];
	struct dahdi_chan *_chans[2];
	struct dahdi_chan *sigchan;
	int sigactive;
	atomic_t hdlc_pending;
	int led;
} g4xx_span;

#ifdef VIRTUAL_TTY

typedef struct gsm0710_frame {
	unsigned char channel;
	unsigned char control;
	int data_length;
	unsigned char *data;
} gsm0710_frame_t;


typedef struct channel_status {
	int opened;
	unsigned char v24_signals;
} channel_status_t;


#define GSM0710_BUFFER_SIZE 2048

typedef struct gsm0710_buffer {
	unsigned char data[GSM0710_BUFFER_SIZE];
	unsigned char *readp;
	unsigned char *writep;
	unsigned char *endp;
	int flag_found; // set if last character read was flag
	unsigned long received_count;
	unsigned long dropped_count;
} gsm0710_buffer_t;

// increases buffer pointer by one and wraps around if necessary
#define GSM0710_INC_BUF_POINTER(buf,p) p++; if (p == buf->endp) p = buf->data;
#define gsm0710_buffer_length(buf) ((buf->readp > buf->writep) ? (GSM0710_BUFFER_SIZE - (buf->readp - buf->writep)) : (buf->writep-buf->readp))
#define gsm0710_buffer_free(buf) ((buf->readp > buf->writep) ? (buf->readp - buf->writep) : (GSM0710_BUFFER_SIZE - (buf->writep-buf->readp)))


#define GSM0710_F_FLAG		0xF9
// bits: Poll/final, Command/Response, Extension
#define GSM0710_PF			16
#define GSM0710_CR			2
#define GSM0710_EA			1
// the types of the frames
#define GSM0710_SABM		47
#define GSM0710_UA			99
#define GSM0710_DM			15
#define GSM0710_DISC		67
#define GSM0710_UIH			239
#define GSM0710_UI			3
// the types of the control channel commands
#define GSM0710_C_CLD		193
#define GSM0710_C_TEST		33
#define GSM0710_C_MSC		225
#define GSM0710_C_NSC		17
// V.24 signals: flow control, ready to communicate, ring indicator, data valid

#define GSM0710_S_FC 2
#define GSM0710_S_RTC 4
#define GSM0710_S_RTR 8
#define GSM0710_S_IC 64
#define GSM0710_S_DV 128

#define DEFAULT_GSM0710_FRAME_SIZE 512

#define GSM0710_COMMAND_IS(command, type) ((type & ~GSM0710_CR) == command)
#define GSM0710_PF_ISSET(frame) ((frame->control & GSM0710_PF) == GSM0710_PF)
#define GSM0710_FRAME_IS(type, frame) ((frame->control & ~GSM0710_PF) == type)

struct gsm_serial {
	struct tty_struct *tty;		/* pointer to the tty for this device */
	int	open_count;				/* number of times this port has been opened */
	spinlock_t lock;			/* locks this structure */
	int channel;
	int cardno;
	int span;
};
#endif

typedef struct g4_card {
	spinlock_t lock;
	unsigned char power[G4_SPANS];
	unsigned int fwversion;
	char *card_name;
	int rst_flag;
	int dead;
	unsigned char cardno;
	unsigned int irq;
	void *pci_io;
	unsigned long pci_io_phys;
	unsigned long iomem_size;
	struct g4_span spans[G4_SPANS];
	unsigned int g4spans;
	struct pci_dev *pcidev;
	unsigned int type;
	unsigned int flags;

	int check_start[G4_SPANS];
	int power_on_flag[G4_SPANS];
	
	unsigned int intcount;
	int power_on_check_number;
	
	unsigned char b_rx_buf[G4_SPANS][DAHDI_CHUNKSIZE];
	unsigned char b_tx_buf[G4_SPANS][DAHDI_CHUNKSIZE];
#ifdef VIRTUAL_TTY
	struct gsm_serial *gsm_tty_table[G4_SPANS];
	channel_status_t cstatus[G4_SPANS];
	gsm0710_buffer_t *gsm0710_buffer[G4_SPANS];
	int gsm0710_mode[G4_SPANS];
	int enable_gsm0710[G4_SPANS];
	spinlock_t pci_buffer_lock[G4_SPANS];
#endif
	struct tasklet_struct irq_tasklet;
	unsigned char d_rx_buf[G4_SPANS][G4_SER_BUF_SIZE];
	unsigned char d_tx_buf[G4_SPANS][G4_SER_BUF_SIZE];
	unsigned short d_rx_idx[G4_SPANS];
	unsigned short d_tx_idx[G4_SPANS];
	
	int d_rx_recv[G4_SPANS];

	struct g4_card *next;
	struct g4_card *prev;

	g4_gpio gpio0;
	g4_gpio gpio2;
	
#if (DAHDI_VER_NUM >= 2600)
	char* variety;
	struct dahdi_device *ddev;
#endif //(DAHDI_VER_NUM >= 2600)
	
} g4_card;


#ifndef G4_SLEEP_MILLI_SEC
#define G4_SLEEP_MILLI_SEC(nMilliSec, ms)\
do { \
	set_current_state(TASK_UNINTERRUPTIBLE); \
	ms = (nMilliSec) * HZ / 1000; \
	while(ms > 0) \
	{ \
		ms = schedule_timeout(ms); \
	} \
}while(0);
#endif

#ifndef G4_VERSION_CHECK
#define G4_VERSION_CHECK(g4, major, minor) \
	(((g4->fwversion>>16) == major) && ((g4->fwversion&0xffff) >= minor))
#endif


#define G4_POWERON_CHECK_NUMBER 50

#endif /*_OPVXG4XX_H_*/

