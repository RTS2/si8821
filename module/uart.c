/* 

Linux 2.6 Driver for
Spectral Instruments Camera Interface

Copyright (C) 2006  Jeffrey R Hagen

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>


#define wait_event_interruptible_timeout( a, b, c )\
            (c = wait_event_interruptible( a, b ))

#else

#include <linux/module.h>
#include <linux/interrupt.h>
#endif

#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/pci.h>

#include "si8821.h"
#include "si8821_module.h"


void si_get_serial_params(struct SIDEVICE *dev, struct SI_SERIAL_PARAM *sp)
{
  sp->timeout     = dev->Uart.timeout;
  sp->flags       = dev->Uart.block;
  sp->baud        = dev->Uart.baud;
  sp->bits        = dev->Uart.bits;
  sp->parity      = dev->Uart.parity;
  sp->stopbits    = dev->Uart.stopbits;
  sp->fifotrigger = dev->Uart.fifotrigger;
  sp->buffersize  = dev->Uart.serialbufsize;
}

void si_uart_clear( dev ) 
struct SIDEVICE *dev;
{
  unsigned long flags;
  int clr;

  spin_lock_irqsave( &dev->uart_lock, flags );
  dev->Uart.rxput = 0;
  dev->Uart.rxget = 0;
  clr = dev->Uart.rxcnt;
  dev->Uart.rxcnt = 0;
  dev->Uart.txput = 0;
  dev->Uart.txget = 0;
  dev->Uart.txcnt = dev->Uart.serialbufsize;
  spin_unlock_irqrestore( &dev->uart_lock, flags );

/* warn if clearing data */
  if( clr > 0 )
    printk("SI UART clear rxcnt %d\n", clr ); 
  else if( dev->verbose & SI_VERBOSE_SERIAL) {
    printk("SI UART clear\n" ); 
  }
}

int si_set_baud_rate(struct SIDEVICE *dev, __u32 baud)
{
  __u8 c;
  int x;
  x=0;
  if ((dev->IDNumber == 104) || (dev->IDNumber == 97)) {
    x = 1000000 / (dev->Uart.baud = baud);    /* 16 MHz oscillator */
    if( dev->verbose & SI_VERBOSE_SERIAL)
      printk("SI set baudrate for 16MHz oscillator %d\n", baud ); 
  }
  else {
    if (dev->IDNumber == 5208) {
      x = 3750000 / (dev->Uart.baud = baud);  /* 60 MHz oscillator */
      if( dev->verbose & SI_VERBOSE_SERIAL)
        printk("SI set baudrate for 60MHz oscillator %d\n", baud ); 
    }
  }
  if (x != 0) {
    c = UART_REG_READ(dev, SERIAL_LCR);
    UART_REG_WRITE(dev, SERIAL_LCR, (__u8)(c | 0x80));
    UART_REG_WRITE(dev, SERIAL_DLL, (__u8)(x & 0xff));
    UART_REG_WRITE(dev, SERIAL_DLH, (__u8)((x >> 8) & 0xff));
    UART_REG_WRITE(dev, SERIAL_LCR, c);
    UART_REG_WRITE(dev, SERIAL_MCR, 0);
  }
  return true;
}
/* setup the serial parameters for the UART control of the SI card */

int si_set_serial_params(struct SIDEVICE *dev, struct SI_SERIAL_PARAM *sp)
{
  __u8 c, reg;
  __u8 *cp;
  int  x;
  unsigned long flags;

  if( dev->verbose & SI_VERBOSE_SERIAL) {
    printk("SI serialparamsSCC\n");
    printk("SI baud rate %d\n", sp->baud);
    printk("SI timeout %d\n", sp->timeout);
    printk("SI number of bits %d\n", sp->bits);
    printk("SI parity %d\n", sp->parity);
    printk("SI stop bits %d\n", sp->stopbits);
    printk("SI set fifo trigger %d\n", sp->fifotrigger);
  }
  spin_lock_irqsave( &dev->uart_lock, flags );

  dev->Uart.timeout = sp->timeout;
  dev->Uart.block = sp->flags & SI_SERIAL_FLAGS_BLOCK;
  dev->Uart.baud = sp->baud;

  si_set_baud_rate(dev, dev->Uart.baud);
  
#ifdef NOT
  x = 1000000 / dev->Uart.baud;    // 16 MHz oscillator
  if (sp->baud == 57600)           // make it go really fast
    x = 4;

  c = UART_REG_READ(dev, SERIAL_LCR);
  UART_REG_WRITE(dev, SERIAL_LCR, (__u8)(c | 0x80));
  UART_REG_WRITE(dev, SERIAL_DLL, (__u8)(x & 0xff));
  UART_REG_WRITE(dev, SERIAL_DLH, (__u8)((x >> 8) & 0xff));
  UART_REG_WRITE(dev, SERIAL_LCR, c);

    // make sure the loop register is NOT set
  UART_REG_WRITE(dev, SERIAL_MCR, 0);  
#endif
  
  dev->Uart.bits = sp->bits;
  x = (sp->bits - 1) & 3;
  c = UART_REG_READ(dev, SERIAL_LCR) & ~3;
  UART_REG_WRITE(dev, SERIAL_LCR, (__u8)(c | x));

  dev->Uart.parity = sp->parity;
  if (sp->parity == 'E' || sp->parity == 'e')
    x = 0x18;
  else if (sp->parity == 'O' || sp->parity == 'o')
    x = 0x08;
  else if (sp->parity == 'M' || sp->parity == 'm')
    x = 0x28;
  else if (sp->parity == 'S' || sp->parity == 's')
    x = 0x38;
  else
    x = 0;

  c = UART_REG_READ(dev, SERIAL_LCR) & ~0x38;
  UART_REG_WRITE(dev, SERIAL_LCR, (__u8)(c | x));

  dev->Uart.stopbits = sp->stopbits;
  x = (sp->stopbits == 2)?4:0;
  c = UART_REG_READ(dev, SERIAL_LCR) & ~4;
  UART_REG_WRITE(dev, SERIAL_LCR, (__u8)(c | x));
  
  switch (sp->fifotrigger) {
    case 1:
    case 4:
    case 8:
    case 14:
      x = sp->fifotrigger;
      break;
    default:
      x = 0;
      break;
  }
  dev->Uart.fifotrigger = x;
  x &= 0x0c;
  x <<= 4;
  UART_REG_WRITE(dev, SERIAL_FCR, (__u8)(x | 7));

  reg = UART_REG_READ(dev, SERIAL_FCR);
  if( dev->verbose & SI_VERBOSE_SERIAL)
    printk("UART SERIAL_FCR 0x%x\n", reg ); 

  UART_REG_WRITE(dev, SERIAL_IER, 0);   /* disable all serial ints */
  cp = dev->Uart.rxbuf;  /* save old pointer just in case */

  /* allocate one large buffer and split it in half. 
     Allocatate new before deallocating old one.
   */

  if( dev->verbose & SI_VERBOSE_SERIAL)
    printk("SI sp->buffersize %d\n", (int)sp->buffersize);
  if( sp->buffersize <= 0 )
    sp->buffersize = 8192;

  if( sp->buffersize % 8192 ) {
    sp->buffersize += 8192 - (sp->buffersize % 8192);
  }

  spin_unlock_irqrestore( &dev->uart_lock, flags );
  dev->Uart.rxbuf = (char *) kmalloc(sp->buffersize * 2, GFP_KERNEL);
  spin_lock_irqsave( &dev->uart_lock, flags );

  if (dev->Uart.rxbuf == 0) {
    dev->Uart.rxbuf = cp;
    sp->buffersize = dev->Uart.serialbufsize;
  } else {
    if (cp)   /* txbuf is part of rxbuf */
      kfree(cp);
  }
  dev->Uart.serialbufsize = sp->buffersize;
  dev->Uart.txbuf = dev->Uart.rxbuf + dev->Uart.serialbufsize;

  if ((x = dev->Uart.fifotrigger))
  {
    x &= 0x0c;
    x <<= 4;
    UART_REG_WRITE(dev, SERIAL_FCR, (__u8)(x | 7)); /* enable rx and tx fifos */
  }
  else
    UART_REG_WRITE(dev, SERIAL_FCR, 0);  /* disable rx and tx fifos */

  UART_REG_READ(dev, SERIAL_IIR);
  UART_REG_READ(dev, SERIAL_RX);
  reg = UART_REG_READ(dev, SERIAL_LSR);
  if( dev->verbose & SI_VERBOSE_SERIAL)
    printk("UART SERIAL_LSR 0x%x\n", reg ); 
  UART_REG_READ(dev, SERIAL_MSR);
  UART_REG_WRITE(dev, SERIAL_IER, RX_INT|TX_INT);   /* tx ints enabled */

  spin_unlock_irqrestore( &dev->uart_lock, flags );

  if( dev->verbose & SI_VERBOSE_SERIAL) {
    reg = UART_REG_READ(dev, SERIAL_IER);
    printk("UART SERIAL_IER 0x%x\n", reg ); 
  }

  if( dev->verbose & SI_VERBOSE_SERIAL) {
    reg = UART_REG_READ(dev, SERIAL_LCR);
    printk("UART SERIAL_LCR 0x%x\n", reg ); 
  }

  if( dev->verbose & SI_VERBOSE_SERIAL) {
    reg = UART_REG_READ(dev, SERIAL_FCR);
    printk("UART SERIAL_FCR 0x%x\n", reg ); 
  }

  si_uart_clear( dev );
  return TRUE;
}

/* inititalize UART for comm */

int si_init_uart(struct SIDEVICE *dev)
{
  struct SI_SERIAL_PARAM param;

/* default serial paramters  */
  param.baud = 57600;
  param.bits = 8;
  param.timeout  = 1000;
  param.flags    = SI_SERIAL_FLAGS_BLOCK;
  param.parity = 'N';
  param.stopbits = 1;
  param.fifotrigger = 8;
  param.buffersize = PAGE_SIZE*2; /* 16384 */
  
  si_set_serial_params(dev, &param ); 

  if( dev->verbose & SI_VERBOSE_SERIAL)
    printk("exit initUART\n");

  return TRUE;
}

/*  close out UART comm */

void si_cleanup_serial(struct SIDEVICE *dev)
{
  unsigned long flags;
  __u32 reg;
  printk("SI --> cleanup_serial\n");

  if( dev->test ) {
    printk("SI cleanup_serial TEST\n");
    return;
  }

  spin_lock_irqsave( &dev->uart_lock, flags );
  UART_REG_WRITE(dev, SERIAL_FCR, 0);    // disable rx and tx fifos
  UART_REG_WRITE(dev, SERIAL_IER, 0);    // disable all ints
  spin_unlock_irqrestore( &dev->uart_lock, flags );
                //  txbuf is part of rxbuf
  if (dev->Uart.rxbuf) {
    kfree(dev->Uart.rxbuf);
    dev->Uart.rxbuf = NULL;
  }

/* turn off interrupts */
  printk("SI turn off interrupts\n");

  reg = PLX_REG_READ( dev, PCI9054_INT_CTRL_STAT);
  PLX_REG_WRITE( dev, PCI9054_INT_CTRL_STAT, reg & ~((1<<11)));
  printk("SI <-- cleanup_serial\n");
}

/* send one character over the serial bus 
   if ready to send, send it, if not, queue
   and let the isr handle it
 */

int si_transmit_serial(struct SIDEVICE *dev, __u8 data)
{
  int ret;
  unsigned long flags;
  __u8 reg;

  if( dev->test ) {
    printk("SI transmit_serial TEST\n");
    return 0;
  }

  spin_lock_irqsave( &dev->uart_lock, flags );
//  print_UART_stat(dev);
  reg = 0;
  if (dev->Uart.txcnt == dev->Uart.serialbufsize &&
       ((reg = UART_REG_READ(dev, SERIAL_LSR)) & 0x20)) {
      UART_REG_WRITE(dev, SERIAL_TX, data);
      ret = TRUE;
  } else {

    if (dev->Uart.txcnt > 1) {    // room in queue?
      dev->Uart.txbuf[dev->Uart.txput++] = data;
      if (dev->Uart.txput == dev->Uart.serialbufsize)
        dev->Uart.txput = 0;
      dev->Uart.txcnt--;
      ret = TRUE;
    } else {
      ret = FALSE;
    }
  }
  spin_unlock_irqrestore( &dev->uart_lock, flags );

//  if( dev->verbose )
//    printk( "SI transmit_serial 0x%x txcnt %d size %d reg 0x%x ret %d\n", 
//     data, dev->Uart.txcnt, dev->Uart.serialbufsize, reg, ret );

  return ret;
}

/* read one character from UART 

   Remove 1 character from the receive queue, copy
   it into pdata, and return TRUE. If the queue
   is empty, return FALSE.
*/

int si_receive_serial(struct SIDEVICE *dev, __u8 *pdata)
{
  int ret;
  unsigned long flags;
  __u8 reg;

  if( dev->test ) {
    printk("SI receive_serial TEST\n");
    return 0;
  }

  spin_lock_irqsave( &dev->uart_lock, flags );
  reg = UART_REG_READ(dev, SERIAL_LSR);
 

  if (dev->Uart.rxput != dev->Uart.rxget)
  {
    *pdata = dev->Uart.rxbuf[dev->Uart.rxget++];
    if (dev->Uart.rxget == dev->Uart.serialbufsize)
      dev->Uart.rxget = 0;
    dev->Uart.rxcnt--;
    ret = TRUE;
  }
  else {
    *pdata = 0;
    ret = FALSE;
  }
 
  spin_unlock_irqrestore( &dev->uart_lock, flags );

  if( dev->verbose & SI_VERBOSE_SERIAL) {
    if( ret )
      ; // printk("SI receive_serial char 0x%x ret %d\n", *pdata, ret );
    else
      printk("SI receive_serial no data, reg 0x%x\n", reg );
  }

  return ret;
}

int si_print_uart_stat(struct SIDEVICE *dev)
{
    printk("UART status\n");
    printk("serialbufsize: %d\n", dev->Uart.serialbufsize);
    printk("rxput:         %d\n", dev->Uart.rxput);
    printk("rxget:         %d\n", dev->Uart.rxget);
    printk("rxcnt:         %d\n", dev->Uart.rxcnt);
    printk("txput:         %d\n", dev->Uart.txput);
    printk("txget:         %d\n", dev->Uart.txget);
    printk("txcnt:         %d\n", dev->Uart.txcnt);
    printk("baud:          %d\n", dev->Uart.baud);
    printk("bits:          %d\n", dev->Uart.bits);
    printk("parity:        %d\n", dev->Uart.parity);
    printk("stopbits:      %d\n", dev->Uart.stopbits);
    printk("fifotrigger:   %d\n", dev->Uart.fifotrigger);

  return(0);
}


/* send a break to the UART, busy wait delay */

int si_uart_break(struct SIDEVICE *dev, int break_time)
{
  unsigned char    uc;
  unsigned long flags;

  if( dev->test ) {
    printk("SI uart_break TEST\n");
    return 0;
  }

  spin_lock_irqsave( &dev->uart_lock, flags );
  if( dev->verbose & SI_VERBOSE_SERIAL) {
    printk("SI uart_break %dms\n",break_time);
  }
  uc = UART_REG_READ(dev, SERIAL_LCR);  // assert break (signal low)
  UART_REG_WRITE(dev, SERIAL_LCR, (__u8)(uc | 0x40));
  
  mdelay(break_time);            // wait break time
    
  uc = UART_REG_READ(dev, SERIAL_LCR);  // de-assert break (signal high)
  UART_REG_WRITE(dev, SERIAL_LCR, (__u8)(uc & ~0x40));
  spin_unlock_irqrestore( &dev->uart_lock, flags );
  return(0);
}


