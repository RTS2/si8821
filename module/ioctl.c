/* 

Linux 2.6 Driver for the 
Spectral Instruments Camera Interface cards

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

/* define the ioctl calls */

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

#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <asm/atomic.h>

#include "si8821.h"
#include "si8821_module.h"


/* si_ioctl:  Processes the IOCTL messages sent to this device */
// ioctl() replaced with unlocked_ioctl() in 2.6.36 & return type changed
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))
int si_ioctl(
	struct inode  *inode,
	struct file   *filp,
	unsigned int   cmd,
	unsigned long  args
)
#else
long si_ioctl(
	struct file   *filp,
	unsigned int   cmd,
	unsigned long  args
)
#endif
{
	int ret;
	struct SI_DMA_STATUS	dma_status;
	struct SI_PCI_STATUS	pci_status;
	struct SI_SERIAL_PARAM	serial_param;
	struct SI_PCI_CAPABILITIES	pci_cap;
	struct SIDEVICE			*dev;
	int x;
	unsigned long mask;
	unsigned long flags;
	//printk("SI --> si_ioctl\n");
	
	dev = (struct SIDEVICE *)filp->private_data;
	if( !dev )
		return -EIO;
	
	
//  if( dev->verbose )
    //printk("SI ioctl %d\n",  _IOC_SIZE(cmd) );

  /*  Si Interface only */

  ret = 0;
  switch (cmd) {
    case SI_IOCTL_RESET:
      ret = si_reset( dev ); /* parameter ignored */
      break;
      
    case SI_IOCTL_SERIAL_IN_STATUS:
      {
      int status;

      spin_lock_irqsave( &dev->uart_lock, flags );
      status = dev->Uart.rxcnt;
      spin_unlock_irqrestore( &dev->uart_lock, flags );

      ret = put_user( status, (int __user *)args);
      if( dev->verbose & SI_VERBOSE_SERIAL )
		  if (status != 0) printk("SI SI_IOCTL_SERIAL_IN_STATUS %d\n", status );
      }
      break;

    case SI_IOCTL_SERIAL_OUT_STATUS:
      {
        int ul;

        spin_lock_irqsave( &dev->uart_lock, flags );
        ul = dev->Uart.serialbufsize - dev->Uart.txcnt;
        spin_unlock_irqrestore( &dev->uart_lock, flags );
        ret = put_user( ul, (int __user *)args);
        if( dev->verbose& SI_VERBOSE_SERIAL )
          if (ul != 0) printk("SI SI_IOCTL_SERIAL_OUT_STATUS %d\n", ul);
      }
      break;

    case SI_IOCTL_GET_SERIAL:
      si_get_serial_params( dev, &serial_param );
      if(copy_to_user( (struct SI_SERIAL_PARAM __user *)args, &serial_param,
        sizeof(struct SI_SERIAL_PARAM)))
          ret = -EFAULT;
      break;
    case SI_IOCTL_SET_SERIAL:
      printk("SI --> SI_IOCTL_SET_SERIAL\n");
      if(copy_from_user( &serial_param, (struct SI_SERIAL_PARAM *)args, 
        sizeof(struct SI_SERIAL_PARAM)))
          return (-EFAULT);

      //if( dev->verbose & SI_VERBOSE_SERIAL)
        printk("SI_IOCTL_SERIAL_PARAMS, baud %d\n", serial_param.baud );
      printk("SI about to set serial params\n");
      si_set_serial_params( dev, &serial_param );
      ret = 0;
      printk("SI <-- SI_IOCTL_SET_SERIAL\n");

      break;
    case SI_IOCTL_SERIAL_CLEAR:
      si_uart_clear( dev );
      ret = 0;
      break;
    
    case SI_IOCTL_SERIAL_BREAK:
      {
        int tim;

        if( (ret = get_user( tim, (int __user *)args ))<0 )
          break;

        if (tim < 0 || tim > 1000) 
          tim = 1000;        // limit to 1 second

        //if( dev->verbose& SI_VERBOSE_SERIAL )
          printk("SI_IOCTL_SERIAL_BREAK %d\n", tim );

        si_uart_break(dev, tim);
      }
      break;

	case SI_IOCTL_GET_PCI_STATUS:
		if( dev->verbose ) printk("SI_IOCTL_GET_PCI_STATUS\n");
		if( (ret = si_dma_status(dev, &dma_status ))<0 ) break;
		switch (dev->IDNumber) {
			case 97:	mask = 0xff;		break;
			case 104:	mask = 0xffff;		break;
			case 5208:	mask = 0xffffffff;	break;
			default:	mask = 0xffffffff;	break;
		}
		pci_status.sys_revision = (int)SI_VERSION;
// 		for (x=0; x<(int)strlen(SI_DATE);x++) {
// 			pci_status.date[x] = SI_DATE[x];
// 		}
//		strcpy(pci_status.date,SI_DATE);
		pci_status.hardware_id = LOCAL_REG_LREAD(dev, LOCAL_ID_NUMBER) & mask;
		pci_status.hardware_rev = LOCAL_REG_LREAD(dev, LOCAL_REV_NUMBER) & mask;
		pci_status.pci_status = LOCAL_REG_LREAD(dev, LOCAL_STATUS) & mask;
		pci_status.pci_cmd = LOCAL_REG_LREAD(dev, LOCAL_COMMAND) & mask;
		pci_status.open_count = 42;
		pci_status.spare2 = 42;
		pci_status.spare1 = 42;
		if(copy_to_user( (struct SI_PCI_STATUS *)args, &pci_status, sizeof(struct SI_PCI_STATUS))) ret = -EFAULT;
		break;
		
	case SI_IOCTL_SI_GET_CAPABILITIES:
		if( dev->verbose ) printk("SI_IOCTL_SI_GET_CAPABILITIES\n");
		switch (dev->IDNumber) {
			case 97:	pci_cap.InterfacePN = 3097;		break;
			case 104:	pci_cap.InterfacePN = 3383;		break;
			case 5208:	pci_cap.InterfacePN = 5208;		break;
			default:	pci_cap.InterfacePN = 0;		break;
		}
		if (pci_cap.InterfacePN == 5208) {
			pci_cap.CanDo32Bit = 1;
			pci_cap.CanDo50MByte = 0;
			pci_cap.CanDoBackToBack = 1;
			pci_cap.DoesBaudRemap = 0;
		}
		else {
			pci_cap.CanDo32Bit = 0;
			pci_cap.CanDo50MByte = 1;
			pci_cap.CanDoBackToBack = 0;
			pci_cap.DoesBaudRemap = 1;
		}
		pci_cap.CanDoDualBuff = 1;
		if(copy_to_user( (struct SI_PCI_CAPABILITIES *)args, &pci_cap, sizeof(struct SI_PCI_CAPABILITIES))) ret = -EFAULT;
		break;
      
    // DMA related entries
    case SI_IOCTL_DMA_INIT:
      if( dev->verbose )
        printk("SI IOCTL_DMA_INIT\n");

      if(copy_from_user( &dev->dma_cfg, (struct SI_DMA_CONFIG *)args, 
        sizeof(struct SI_DMA_CONFIG))) {
          ret = -EFAULT;
          break;
      }
      ret = si_config_dma( dev );

      break;
    
    case SI_IOCTL_DMA_START:
      if( dev->verbose )
        printk("SI_IOCTL_DMA_START\n");

      if((ret = si_start_dma(dev))<0)
        break;

      if( (ret = si_dma_status(dev, &dma_status ))<0 )
        break;

      if(args && copy_to_user( (struct SI_DMA_STATUS *)args, &dma_status,
        sizeof(struct SI_DMA_STATUS)))
          ret = -EFAULT;
      break;

    case SI_IOCTL_DMA_STATUS:
      if( dev->verbose )
        printk("SI_IOCTL_DMA_STATUS\n");
      if( (ret = si_dma_status(dev, &dma_status ))<0 )
        break;

      if(copy_to_user( (struct SI_DMA_STATUS *)args, &dma_status,
        sizeof(struct SI_DMA_STATUS)))
          ret = -EFAULT;
      break;

    case SI_IOCTL_DMA_NEXT:
      if( dev->verbose )
        printk("SI_IOCTL_DMA_NEXT\n");

      ret = si_dma_next(dev, &dma_status );
      if(copy_to_user( (struct SI_DMA_STATUS *)args, &dma_status,
        sizeof(struct SI_DMA_STATUS)))
          ret = -EFAULT;
      break;
      
    case SI_IOCTL_DMA_ABORT:
      if( dev->verbose )
        printk("SI_IOCTL_DMA_ABORT\n");

      if((ret = si_stop_dma(dev, &dma_status)) <0 )
        break;

      if(args && copy_to_user( (struct SI_DMA_STATUS *)args, &dma_status,
        sizeof(struct SI_DMA_STATUS)))
          ret = -EFAULT;
      break;
    case SI_IOCTL_VERBOSE:
      ret = get_user( dev->verbose, (int __user *)args );
      break;

    case SI_IOCTL_SETPOLL:
      ret = get_user( dev->setpoll, (int __user *)args );
      if( dev->verbose ) {
         if( dev->setpoll == SI_SETPOLL_UART )
           printk("SI setpoll set to uart\n");
         else
           printk("SI setpoll set to dma\n");
      }
      break;

    case SI_IOCTL_FREEMEM:
      if( dev->verbose ) {
        printk("SI freemem\n");

      if( (ret = si_wait_vmaclose( dev )) ) { /* make sure munmap before free */
        printk("SI freemem timeout waiting for munmap\n");
        return ret;
      } 

      if( dev->sgl ) {
        si_stop_dma(dev, NULL);
        si_free_sgl(dev);
      } else {
        if( dev->verbose )
          printk("SI freemem no data allocated\n");
        }
      }
      break;

    default:
      printk( "Unsupported SI_IOCTL_Xxx (%02d)\n", _IOC_NR(cmd));
      ret = -EINVAL;
      break;
  }

//  if( dev->verbose )
//    printk("SI completed ioctl %d\n", ret );

  //printk("SI <-- si_ioctl\n");
  printk("SI <-- si_ioctl OK!!\n");
  return ret;
}


int si_reset( dev )
struct SIDEVICE *dev;
{
	if( dev->verbose )
		printk("SI master local reset\n" );
	/* do the master reset local bus */
	LOCAL_REG_WRITE(dev, LOCAL_COMMAND, 0 );  
	return 0;
}

