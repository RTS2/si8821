/* 

Linux 2.6 Driver for
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


#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#define wait_event_interruptible_timeout( a, b, c )\
            (c = wait_event_interruptible( a, b ))

#else

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
#include <linux/sched.h> //Perhaps this should be included by linux/wait.h but it is not
#endif

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#endif

#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <asm/atomic.h>

#include "si8821.h"
#include "si8821_module.h"


MODULE_AUTHOR("Jeff Hagen, jhagen@as.arizona.edu Univ of Arizona, H-J. Meyer, Spectral Instruments");
MODULE_DESCRIPTION("Driver for Spectral Instruments Camera Interface cards");
MODULE_LICENSE("GPL");


/* module parameters */

/*
 if maxever is not zero on module load, 
 configure memory based on buflen and maxever
*/

int buflen = 1048576;
module_param( buflen, int,  0 ); 

//int maxever = 33554432; /* this is for lotis */
int maxever = 134709248; /* this is for lotis */
module_param( maxever, int,  0 );

int timeout = 5000;  /* default jiffies */
module_param( timeout, int,  0 );

int verbose = 1;
module_param( verbose, int,  0 ); 


static struct SIDEVICE *si_devices = NULL; /* list of cards */


static struct pci_device_id si_pci_tbl[] __initdata = {
  { 0x10b5, 0x2679, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
  { 0x10b5, 0x3079, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
  { 0, }
};

static spinlock_t spin_multi_devs; /* use this for configure_device */
static struct pci_driver si_driver;
static int si_major = 0;


MODULE_DEVICE_TABLE( pci, si_pci_tbl);

/* The proc filesystem: function to read and entry */

static struct proc_dir_entry *si_proc;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 0)
ssize_t si_read_proc(struct file *filp, char *buffer, size_t length, loff_t *offset)
{
  ssize_t bytes_read = 0;
  struct SIDEVICE *d;
  struct pci_dev *pci;
  char tb[length];

  d = si_devices;

  while (d && bytes_read < length) {
    pci = d->pci;
    if (pci)
    {
      bytes_read +=snprintf (tb + bytes_read, length - bytes_read, "SI %s, major %d minor %d devfn %d irq %d isopen %d\n", pci_name(pci), si_major, d->minor, pci->devfn, pci->irq, atomic_read(&d->isopen)  );
    } else {
      bytes_read += snprintf (tb + bytes_read, length - bytes_read, "SI TEST major %d minor %d\n", si_major, d->minor);
    }
    d = d->next;
  }

  if (copy_to_user (buffer, tb, bytes_read))
    return -EFAULT;

  return bytes_read;
}

#else
int si_read_proc(char *buf, char **start, off_t offset,  int len, int *eof, void *private)
{
  struct SIDEVICE *d;
  struct pci_dev *pci;

  len=0;
  d = si_devices;
  while( d ) {
    pci = d->pci;
    if( pci ) {
      len +=sprintf( buf+len,
        "SI %s, major %d minor %d devfn %d irq %d isopen %d\n", 
                   pci_name(pci), si_major, d->minor, pci->devfn, pci->irq,
                   atomic_read(&d->isopen)  );

    } else {
      len +=sprintf( buf+len, "SI TEST major %d minor %d\n", si_major, d->minor);
    }
      if( len > PAGE_SIZE-100 )
        break;

    d = d->next;
  }
  *start = buf + offset;

  return len > offset ? len - offset : 0;
}
#endif

/* The different file operations */

struct file_operations si_fops = {
    .owner   = THIS_MODULE,   /* owner */
    .read    = si_read,       /* read  */
    .write   = si_write,      /* write */
    .poll    = si_poll,       /* poll */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))
    .ioctl   = si_ioctl,      /* ioctl */
#else
    .unlocked_ioctl   = si_ioctl,      /* ioctl */
#endif
    .mmap    = si_mmap,       /* mmap */
    .open    = si_open,       /* open */
    .release = si_close,      /* release */
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 0)
struct file_operations si_procops = {
    .read = si_read_proc
};
#endif


int PlxPciBarResourceMap( struct SIDEVICE *dev, __u8 BarIndex)
{
  // Default resource to not claimed
  dev->PciBar[BarIndex].bResourceClaimed = FALSE;
  dev->PciBar[BarIndex].pVa = NULL;

  // Request and Map space
  if (dev->PciBar[BarIndex].Properties.Flags & PLX_BAR_FLAG_IO)
  {
      // Request I/O port region
      if (request_region(
              dev->PciBar[BarIndex].Properties.Physical,
              dev->PciBar[BarIndex].Properties.Size,
              "si8821"
              ) != NULL)
      {
          // Note that resource was claimed
          dev->PciBar[BarIndex].bResourceClaimed = TRUE;
      }
  }
  else
  {
    // Request memory region
    if (request_mem_region(
            dev->PciBar[BarIndex].Properties.Physical,
            dev->PciBar[BarIndex].Properties.Size,
            "si8821"
            ) == NULL)
    {
      printk("SI request_mem_region failed\n");
      return (-ENOMEM);
    }
    else
    {
      // Note that resource was claimed
      dev->PciBar[BarIndex].bResourceClaimed = TRUE;
     // Get a kernel-mapped virtual address
      dev->PciBar[BarIndex].pVa =
        ioremap(
            dev->PciBar[BarIndex].Properties.Physical,
            dev->PciBar[BarIndex].Properties.Size
            );
       if (dev->PciBar[BarIndex].pVa == NULL)
      {
        printk("SI ioremap failed\n");
        return (-ENOMEM);
      }
    }
  }
  return 0;
}


static int si_configure_device( struct pci_dev *, const struct pci_device_id *);

static int si_configure_device(pci, id)
struct pci_dev *pci;
const struct pci_device_id *id;
{
  struct SIDEVICE *dev;
  unsigned char irup;
  unsigned int error;
  int wh, len, i;
  int rc;
  __u32 reg;
  __u8  resource_count;
  __u32 RegValue;
  printk("SI configure device\n");
  wh = 0;
  dev = si_devices;
  while( dev ) {
    dev= dev->next;
    wh++;
  }

  if( !pci_set_dma_mask( pci, 0xffffffff ) ) {
    printk("SI pci_dma_supported failed\n");
    return(-EIO);
  }

  if(!(dev = kmalloc( sizeof(struct SIDEVICE), GFP_KERNEL))) {
    printk( "SI si_configure_device no memory\n" );
    return -ENOMEM;
  }

  memset(dev, 0, sizeof(struct SIDEVICE));
  dev->verbose = 1;
  //dev->verbose = SI_VERBOSE_SERIAL;


/* in case of multiple devices on a SMP machine */

  spin_lock( &spin_multi_devs );

  if( si_devices )
    dev->next = si_devices;

  si_devices = dev;
  spin_unlock( &spin_multi_devs );

  dev->minor = wh;
 
  pci_enable_device( pci );

  dev->pci = pci;
  pci_read_config_byte(dev->pci, PCI_INTERRUPT_LINE, &irup);
  //###pci_request_regions( dev->pci, "SI8821");

  /* implement new resource scanning - modeled after PLX SDK*/
  resource_count = 0;
  for (i = 0; i < PCI_NUM_BARS_TYPE_00; ++i) {
    if (0 == pci_resource_start(pci,i)) { /* verify that address is valid */
      continue;
    }
    printk("SI Resource %02d\n", resource_count);
    resource_count++;
    dev->PciBar[i].Properties.Physical = pci_resource_start(pci,i);
    if(pci_resource_flags( pci, i ) & IORESOURCE_IO) { /* I/O Port */
      dev->PciBar[i].Properties.Physical &= ~(0x3);
      dev->PciBar[i].Properties.Flags = PLX_BAR_FLAG_IO;
    }
    else { /* Memory Space */
      dev->PciBar[i].Properties.Physical &= ~(0xf);
      dev->PciBar[i].Properties.Flags = PLX_BAR_FLAG_MEM;
    }
    dev->PciBar[i].Properties.Flags |= PLX_BAR_FLAG_PROBED;  /* Set BAR as already probed */

    rc = pci_read_config_dword(pci,0x10 + (i * sizeof(__u32)),&RegValue);
    dev->PciBar[i].Properties.BarValue = RegValue;
    if(pci_resource_flags( pci, i ) & IORESOURCE_MEM_64) {
      rc = pci_read_config_dword(pci,0x10 + ((i+1) * sizeof(__u32)),&RegValue);
      dev->PciBar[i].Properties.BarValue |= (__u64)RegValue << 32;
    }
    printk("SI PCI BAR %d: %016llX\n",i,dev->PciBar[i].Properties.BarValue);
    printk("SI Phys Addr: %016llX\n",dev->PciBar[i].Properties.Physical);
    dev->PciBar[i].Properties.Size = pci_resource_len(pci,i);
    if (dev->PciBar[i].Properties.Size >= (1 << 10)) {
      printk("SI size: %016llx (%lld Kb)\n",dev->PciBar[i].Properties.Size,dev->PciBar[i].Properties.Size >> 10);
    }
    else {
      printk("SI size: %016llx (%lld bytes)\n",dev->PciBar[i].Properties.Size,dev->PciBar[i].Properties.Size);
    }
    if (dev->PciBar[i].Properties.Flags & PLX_BAR_FLAG_MEM) {  /* Set flags */
      if (pci_resource_flags( pci, i ) & IORESOURCE_MEM_64)
        dev->PciBar[i].Properties.Flags |= PLX_BAR_FLAG_64_BIT;
      else
        dev->PciBar[i].Properties.Flags |= PLX_BAR_FLAG_32_BIT;
      if (pci_resource_flags( pci, i ) & IORESOURCE_PREFETCH)
        dev->PciBar[i].Properties.Flags |= PLX_BAR_FLAG_PREFETCHABLE;
      printk("SI  Property : %sPrefetchable %d-bit\n",
           (dev->PciBar[i].Properties.Flags & PLX_BAR_FLAG_PREFETCHABLE) ? "" : "Non-",
           (dev->PciBar[i].Properties.Flags & PLX_BAR_FLAG_64_BIT) ? 64 : 32);
      }
    /* Claim and map the resource */
    rc = PlxPciBarResourceMap(dev,i);
    if (0 == rc) {
      if (dev->PciBar[i].Properties.Flags & PLX_BAR_FLAG_MEM) {
        printk("SI Kernel VA: %p\n", dev->PciBar[i].pVa);
      }
    }
    else {
       if (dev->PciBar[i].Properties.Flags & PLX_BAR_FLAG_MEM) {
        printk("SI Kernel VA: ERROR - Unable to map space to Kernel\n");
      }
    }
  }
  if (dev->PciBar[0].pVa == NULL) {
    printk("SI ERROR - BAR 0 mapping is required for register access\n");
    return -ENOSYS;
  }
  dev->pRegVa = (__u8*)dev->PciBar[0].pVa;
  printk("SI Register VA: %p\n",dev->pRegVa);

  /* end new resource scanning */
        // perform a little test
  printk("READ_SERIAL_RX  = %x.\n", UART_REG_READ(dev,SERIAL_RX));
  printk("READ_SERIAL_IER = %x.\n", UART_REG_READ(dev,SERIAL_IER));
  printk("READ_SERIAL_IIR = %x.\n", UART_REG_READ(dev,SERIAL_IIR));
  printk("READ_SERIAL_LCR = %x.\n", UART_REG_READ(dev,SERIAL_LCR));
  printk("READ_SERIAL_MCR = %x.\n", UART_REG_READ(dev,SERIAL_MCR));
  printk("READ_SERIAL_LSR = %x.\n", UART_REG_READ(dev,SERIAL_LSR));
  printk("READ_SERIAL_MSR = %x.\n", UART_REG_READ(dev,SERIAL_MSR));
  printk("READ_SERIAL_RAM = %x.\n", UART_REG_READ(dev,SERIAL_RAM));
  UART_REG_WRITE(dev,SERIAL_RAM,0xaa);
  printk("READ_SERIAL_LSR = %x.\n", UART_REG_READ(dev,SERIAL_LSR));
  printk("READ_SERIAL_RAM = %x.\n", UART_REG_READ(dev,SERIAL_RAM));
  UART_REG_WRITE(dev,SERIAL_RAM,0x55);
  printk("READ_SERIAL_LSR = %x.\n", UART_REG_READ(dev,SERIAL_LSR));
  printk("READ_SERIAL_RAM = %x.\n", UART_REG_READ(dev,SERIAL_RAM));

  reg = readl((__u32 *)(dev->pRegVa + PCI9054_INT_CTRL_STAT));
  printk("SI PCI9054_INT_CTRL_STAT: %x\n",reg);
  dev->IDNumber = LOCAL_REG_LREAD(dev, LOCAL_ID_NUMBER);
  if ((dev->IDNumber >>8) >= 0xFF) dev->IDNumber &= 0xFF;
  printk("SI LOCAL_ID_NUMBER = 0x%x\n", dev->IDNumber);
  
  for ( i=0; i<4; i++ ) {
    len = pci_resource_len(pci,i);

    if( !len )
      continue;

    dev->bar_len[i] = len;
    if( IORESOURCE_IO & pci_resource_flags( pci, i ) ) { /* ports */
      dev->bar[i] = pci_resource_start( pci, i );
    } else {
      dev->bar[i] = (__u32) ioremap_nocache(
        pci_resource_start(pci,i), len );
	    printk("SI address ptr %p\n",ioremap_nocache(pci_resource_start(pci,i), len));
	    printk("SI address ptr %p %d\n",pci_resource_start(pci,i), len);
    }

    if( dev->verbose)
      printk("SI address of bar %d: 0x%x\n", i, (unsigned int)dev->bar[i] );
  }

  if( pci->irq ) {
    if((error = request_irq( 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
       pci->irq, (void *)si_interrupt, SA_INTERRUPT|SA_SHIRQ, "SI", dev))){
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
       pci->irq, (void *)si_interrupt, IRQF_DISABLED|IRQF_SHARED, "SI", dev))){
#else
       pci->irq, (void *)si_interrupt, IRQF_SHARED, "SI", dev))){
#endif
       printk( "SI %s failed to get irq %d error %d\n", pci_name(pci),
            pci->irq, error);
       printk( "SI skipping device\n");
       return(-ENODEV);
     } 
  } else
    printk( "SI device %s no pci interupt\n", pci_name(pci) );

  printk( "SI device %s irup 0x%x irq 0x%x id 0x%x\n",
     pci_name(pci), irup, pci->irq, id->device );

  printk( "SI calling pci_set_master\n");
  pci_set_master( dev->pci );
  printk( "SI done with pci_set_master\n");


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

  dev->task.sync = 0;
  dev->task.routine = 0;
  dev->task.data = dev;
#else

  printk( "SI create_workqueue\n");
  dev->bottom_half_wq = create_workqueue("SI8821");
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
  INIT_WORK( &dev->task, si_bottom_half, dev );
#else
  printk( "SI INIT_WORK\n");
  INIT_WORK( &dev->task, si_bottom_half );
#endif

#endif

  printk( "SI spin_lock_init\n");
  spin_lock_init( &dev->uart_lock );
  spin_lock_init( &dev->dma_lock );
  spin_lock_init( &dev->nopage_lock );

  printk( "SI init_waitqueue_head\n");
  init_waitqueue_head( &dev->dma_block );
  init_waitqueue_head( &dev->uart_wblock );
  init_waitqueue_head( &dev->uart_rblock );
  init_waitqueue_head( &dev->mmap_block );

  printk( "SI done init_waitqueue_head\n");
  /* do the master reset local bus */
//  LOCAL_REG_WRITE(dev, LOCAL_COMMAND, 0 );  
//  UART_REG_WRITE(dev, SERIAL_IER, 0);   /* disable all serial ints */
//  mdelay(1);
//  LOCAL_REG_WRITE(dev, LOCAL_COMMAND, (LC_FIFO_MRS_L) );  

//  reg = PLX_REG8_READ(dev, PCI9054_DMA_COMMAND_STAT);
//  printk("SI dma cmd stat 0x%x \n", reg );
  //if( reg & 1 ) {
  // si_stop_dma( dev, NULL );
  // }

  printk("SI register base addr: %x\n",(__u32)((dev)->bar[0]));
  
  printk( "SI turn on interrupts\n");
/* turn on interrupts */
  reg = PLX_REG_READ( dev, PCI9054_INT_CTRL_STAT );
  printk("SI intr stat 0x%x \n", reg );
  PLX_REG_WRITE( dev, PCI9054_INT_CTRL_STAT, reg | (1 << 8) | (1<<11));
  reg = PLX_REG_READ( dev, PCI9054_INT_CTRL_STAT);
  printk( "SI done turn on interrupts\n");

//  printk("SI LOCAL_ID_NUMBER = 0x%x 0x%x\n", 
//    LOCAL_REG_READ(dev, LOCAL_ID_NUMBER), reg );

//  reg = PLX_REG_READ( dev, PCI9054_INT_CTRL_STAT);

//  if( reg & ( 1<<7 ) ) {
//    printk("SI local bus parity error 0x%x\n", reg );
//    PLX_REG_WRITE( dev, PCI9054_INT_CTRL_STAT, reg | (1<<7) );
//  }
//  reg = PLX_REG_READ( dev, PCI9054_INT_CTRL_STAT );
//  printk("SI LOCAL_REV_NUMBER = 0x%x 0x%x\n", 
//    LOCAL_REG_READ(dev, LOCAL_REV_NUMBER), reg );

//  reg = PLX_REG_READ( dev, PCI9054_INT_CTRL_STAT);

  printk("SI device %d driver loaded, intr stat 0x%x verbose: %d\n", wh, reg, verbose );
//  reg = PLX_REG_READ(dev, PCI9054_DMA0_MODE );
//  printk("SI mode 0x%x\n", reg );


/* assign verbose flag as module parameter */

  dev->verbose = verbose;

/* on init, configure memory if module parameter
   maxever is non zero */


  if( maxever > 0 )  {
    if( buflen > maxever )
      buflen = maxever;

    printk( "SI initial load configuring memory to maxever: %d buflen: %d timeout: %d\n", maxever,buflen,timeout );
    dev->dma_cfg.total = maxever;
    dev->dma_cfg.buflen = buflen;
    dev->dma_cfg.timeout = timeout;
    dev->dma_cfg.maxever = maxever;
    dev->dma_cfg.config = SI_DMA_CONFIG_WAKEUP_ONEND;
    si_config_dma( dev );
  }

  return(0);
}



/* the module stuff */


static int __init si_init_module(void)
{
  int result, cardcount, wh;
  struct SIDEVICE *dev;


  si_major = 0; /* let OS assign */



  spin_lock_init( &spin_multi_devs );

  printk("SI init module\n");

  memset( &si_driver, 0, sizeof(struct pci_driver));
  si_driver.name = "si8821";
  si_driver.id_table = si_pci_tbl;
  si_driver.probe = si_configure_device;

//#define NO_HW_TEST 1

#ifdef NO_HW_TEST

    cardcount = 1;
    if(!(si_devices = kmalloc( sizeof(struct SIDEVICE), GFP_KERNEL))) {
      printk( "SI si_configure_device no memory\n" );
      return -ENOMEM;
    }
    printk("SI TEST device configured\n");
    memset(si_devices, 0, sizeof(struct SIDEVICE));
    si_devices->test = 1;
    spin_lock_init( &si_devices->uart_lock );
    spin_lock_init( &si_devices->dma_lock );
#else

  printk("SI looking for card\n");
  result = pci_register_driver( &si_driver );
  printk("pci_register_driver - result: %d\n",result);

  wh = 0;
  dev = si_devices;
  while( dev ) {
    dev= dev->next;
    wh++;
  }

  if( wh == 0 ) {
    pci_unregister_driver( &si_driver );
    cardcount = 0;
    printk("SI no cards found\n");
    return(-ENODEV);
  }

  cardcount = wh;
  printk("SI cardcount: %d\n",cardcount);
#endif
  si_major = 0;
  if((result = register_chrdev(0, "si8821", &si_fops) )<0 ) {
    printk(KERN_WARNING "SI: can't get major %d\n",si_major);
    return result;
  } else 
    printk("SI configuring %d cards, major number %d\n", 
      cardcount, result );

  si_major = result; /* dynamic */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
  si_proc = proc_create("si8821", 0, NULL, &si_procops);
#else
  if((si_proc = proc_entry("si8821", 0, 0 )))
     si_proc->read_proc = si_read_proc;
#endif
  

  if( cardcount == 0 )
    return -ENODEV;
  else
    return 0; /* succeed */
}

static void __exit si_cleanup_module(void)
{
  struct SIDEVICE *dev, *old;

  printk( "SI cleanup\n" );

  dev = si_devices;
  while( dev ) {
    si_stop_dma(dev, NULL);
    si_free_sgl(dev);   
    si_cleanup_serial(dev);
    if( dev->pci ) {
      if( dev->pci->irq )
        free_irq( dev->pci->irq, dev );

      pci_release_regions( dev->pci );
      pci_disable_device( dev->pci );
    }
    old = dev;
    dev = dev->next;
    kfree(old);
  }

#ifndef NO_HW_TEST
  pci_unregister_driver( &si_driver );
#endif
  unregister_chrdev(si_major, "si8821");

  if( si_proc )
    remove_proc_entry( "si8821", 0 );
  si_proc = NULL;
  si_devices = NULL;
}

module_init(si_init_module);
module_exit(si_cleanup_module);


int si_open (struct inode *inode, struct file *filp)
{
  int minor = MINOR(inode->i_rdev);
  int op;
  struct SIDEVICE *dev; /* device information */
  __u32 int_stat;
  printk("SI --> si_open minor: %d\n",minor);
  dev = si_devices;
  while( dev ) {
    if( minor == dev->minor )
      break;
    dev = dev->next;
  }

  if( !dev ) {
    printk("SI bad minor number %d in open\n", minor );
    return(-EBADF);
  }

  try_module_get(THIS_MODULE);

  if( (op = atomic_read(&dev->isopen)) ) {
    printk("SI minor %d already open %d, thats ok\n", op, minor );
  }
  
  filp->private_data = dev;
  atomic_inc(&dev->isopen);

  int_stat = PLX_REG_READ( dev, PCI9054_INT_CTRL_STAT );

  printk( "SI open verbose %d isopen %d minor %d int_stat 0x%x\n", 
    dev->verbose, atomic_read(&dev->isopen), minor, int_stat );

  printk("SI <-- si_open\n");
  return 0;          /* success */
}

int si_close(struct inode *inode, struct file *filp) /* close */
{
  int minor = MINOR(inode->i_rdev);
  struct SIDEVICE *dev;

  dev = si_devices;
  while( dev ) {
    if( minor == dev->minor )
      break;
    dev = dev->next;
  }

  if( !dev ) {
    printk("SI bad minor number %d in close\n", minor );
    return(-EBADF);
  }

  atomic_dec(&dev->isopen);

  if( atomic_read(&dev->isopen) <= 0 ) {
//    if( si_wait_vmaclose( dev )) {
//      printk("SI last close, but vma is still open %d\n", minor );
//    }
    si_stop_dma(dev, NULL );
  }
  
  if( atomic_read(&dev->isopen) <= 0 && atomic_read(&dev->vmact) != 0 ) {
    printk("SI close without vma_close, %d\n", atomic_read(&dev->vmact));
    atomic_set(&dev->vmact, 0);
  }

  filp->private_data = NULL;
  printk( "SI close %d\n", atomic_read(&dev->isopen) );
  module_put(THIS_MODULE);
    
  return 0;
}

/* si_read polls data from uart */
/* si_read does not block */

ssize_t si_read(struct file *filp, char __user *buf, size_t count, loff_t *off )
{
  struct SIDEVICE *dev;
  int i, blocking, ret;
  __u8 ch;

  dev = filp->private_data;
  blocking = (dev->Uart.block & SI_SERIAL_FLAGS_BLOCK)!=0;

  if( dev->test ) {
    for (i=0; i < count; i++) {     // for all characters
      if( put_user( 0, (char __user *)&buf[i] ) )
        return -EFAULT;
    }
    return count;
  }

  for (i=0; i < count; i++) {     // for all characters
    
    while( si_receive_serial( dev, &ch ) == FALSE ) {
      if( !blocking ) {
        if( dev->verbose & SI_VERBOSE_SERIAL)
          printk("SI read, count %d rxcnt %d\n", i, dev->Uart.rxcnt );
        return i;
      }

      ret = dev->Uart.timeout;
      wait_event_interruptible_timeout( dev->uart_rblock,
        si_uart_read_ready( dev ), ret );
      if( si_uart_read_ready( dev )) {
        continue;
      } else {
        if( dev->verbose & SI_VERBOSE_SERIAL )
          printk("SI read, count %d rxcnt %d\n", i, dev->Uart.rxcnt );
        return i;
      }
    }

    if( put_user( ch, (char __user *)&buf[i] ) )
      return -EFAULT;
  }

  if( dev->verbose & SI_VERBOSE_SERIAL)
    printk("SI read, count %d rxcnt %d\n", i, dev->Uart.rxcnt );

  return i;
}

/* si_write operates on the SI uart interface                       */
/* if blocking then it blocks till done writing */

ssize_t si_write(filp, buf, count, off )
struct file *filp;
const char __user *buf;
size_t count;
loff_t *off;
{
  int i, ret, blocking;
  struct SIDEVICE *dev;
  __u8 ch;
 
  dev = filp->private_data;
  blocking = (dev->Uart.block & SI_SERIAL_FLAGS_BLOCK)!=0;

  if( dev->verbose & SI_VERBOSE_SERIAL)
    printk("SI write, count %d\n", count );

  if( dev->test ) {
    for (i=0; i < count; i++) {     // for all characters
      if( get_user( ch, (char __user *)&buf[i] ) )
        return -EFAULT;
    }
    return count;
  }

  for (i=0; i < count; i++) {     // for all characters
    if( get_user( ch, (char __user *)&buf[i] ) )
      return -EFAULT;
    
    if( si_transmit_serial( dev, ch ) == FALSE ) {
      if( blocking ) {
 
        ret = dev->Uart.timeout;
        wait_event_interruptible_timeout( dev->uart_wblock,
          si_uart_tx_empty( dev ), ret );
     /* care with wait_event_.., dual use third parameter */
        if( ret < 0 )
          return ret;
        else
          si_transmit_serial( dev, ch );
      } else {
        return -EWOULDBLOCK;
      }
    }
  }

  if( blocking ) {

    ret = dev->Uart.timeout;
    wait_event_interruptible_timeout( dev->uart_wblock,
      si_uart_tx_empty( dev ), ret );
     /* care with wait_event_.., dual use third parameter */
    if( ret >= 0 )
      ret = count;
  } else {
    ret = i;
  }

  return ret;
}

/* true when UART transmit buffer is empty */

int si_uart_tx_empty( dev )
struct SIDEVICE *dev;
{
  unsigned long flags;
  int ret;

  spin_lock_irqsave( &dev->uart_lock, flags );
  ret = (dev->Uart.txcnt == dev->Uart.serialbufsize);
  spin_unlock_irqrestore( &dev->uart_lock, flags );
  return ret;
}

/* true when UART has data */

int si_uart_read_ready( dev )
struct SIDEVICE *dev;
{
  unsigned long flags;
  int ret;

  spin_lock_irqsave( &dev->uart_lock, flags );
  ret = dev->Uart.rxcnt;
  spin_unlock_irqrestore( &dev->uart_lock, flags );
  return ret;
}

/* when app calls select or poll, block until dma_wake */

unsigned int si_poll(struct file *filp, poll_table *table)
{
  struct SIDEVICE *dev;
  int done, rr;
  unsigned int mask;

  dev = filp->private_data;
  mask = 0;
  done = 0;
  rr = 0;

/* either function for dma or UART but not both */

  if( dev->setpoll == SI_SETPOLL_UART ) { /* for UART */
    rr = si_uart_read_ready( dev );
    if( !rr ) {
      poll_wait( filp, &dev->uart_rblock, table);  /* queue for read */
      rr = si_uart_read_ready( dev );
    }
    if( rr )
      mask |= POLLIN|POLLRDNORM;
  } else {
    done = si_dma_wakeup( dev );

    if( !done ) {
      poll_wait( filp, &dev->dma_block, table);  /* queue for read */
      done = si_dma_wakeup( dev );
    }

    if( done )
      mask |= POLLIN|POLLRDNORM;
  }

  if( dev->verbose & SI_VERBOSE_SERIAL ) {
    char buf[256];
    if( dev->setpoll == SI_SETPOLL_UART )  {
      strcpy( buf, "SI poll uart");
      if( rr )
        strcat(buf, ", rx not empty" );
      else
        strcat(buf, ", rx empty" );
    } else {
      strcpy( buf, "SI poll dma");
      if (done)
        strcat(buf, ", dma ready" );
      else
        strcat(buf, ", dma not ready" );
    }
    printk("%s\n", buf );
  }
  return mask;
}


