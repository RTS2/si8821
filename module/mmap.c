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

/* mmap 
   allocate the driver buffers and 
   map it to the application 
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
#include <linux/sched.h> //Perhaps this should have been included in linux/wait.h but it is not
#endif

#include <linux/module.h>
#include <linux/interrupt.h>
#endif

#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <asm/atomic.h>

#include "si8821.h"
#include "si8821_module.h"

void *jeff_alloc( int size, dma_addr_t *pphy );

/* use the vma mechanism for mapping data */

void si_vmaopen( struct vm_area_struct *area )
{
  struct SIDEVICE *dev;

  dev = (struct SIDEVICE *)area->vm_file->private_data;
  if( dev->verbose )
    printk("SI vmaopen vmact %d\n", atomic_read(&dev->vmact) );

  atomic_inc(&dev->vmact);
}

void si_vmaclose( struct vm_area_struct *area )
{
  struct SIDEVICE *dev;

  dev = (struct SIDEVICE *)area->vm_file->private_data;
  
  if( dev->verbose )
    printk("SI vmaclose vmact %d\n", atomic_read(&dev->vmact) );
  
  if( atomic_dec_and_test(&dev->vmact) )
    ;//wake_up_interruptible( &dev->mmap_block );

}

/* when the application faults this routine is called to map the data */

#ifdef NOT	
struct page *si_vmanopage( vma, address, type )	/* this seems to be legacy. the real call is as below */
struct vm_area_struct *vma;
unsigned long address;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
int type;
#else
int *type;
#endif
#endif

struct page *si_vmanopage( vma, vmf)
struct vm_area_struct *vma;
struct vm_fault *vmf;
{
  unsigned int loff, off;
  int nbuf;
  void *vaddr;
  struct SIDEVICE *dev;
  struct page *pg;
  unsigned long flags;
  // following for test pg 428 LDD3
  struct page *pageptr;
  unsigned long offset;
  void * page_addr;
  unsigned long page_frame;
  

  dev = (struct SIDEVICE *)vma->vm_file->private_data;
  spin_lock_irqsave( &dev->nopage_lock, flags );

  //printk("SI si_vmanopage %p %p\n",(void *)vma->vm_start,(void *)vma->vm_end);
  //printk("SI fault.flags: %lx fault.pgoff: %p fault.virt_addr: %p\n",vmf->flags, vmf->pgoff, vmf->virtual_address);
  
  //off = address - vma->vm_start; /* vma->vm_offset byte offset, must be fixed */
  off = vma->vm_pgoff;
  if( !dev ) {
    printk("SI nopage failed, off 0x%x\n", (unsigned int)off );
    spin_unlock_irqrestore( &dev->nopage_lock, flags );
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
    return(NOPAGE_SIGBUS);
#else
    return(VM_FAULT_SIGBUS);
#endif
  }

  if( !dev->sgl ) {
    printk("SI nopage, sgl NULL\n");
    spin_unlock_irqrestore( &dev->nopage_lock, flags );
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
    return(NOPAGE_SIGBUS);
#else
    return(VM_FAULT_SIGBUS);
#endif
  }
  
  // another ending...
  offset = vmf->pgoff << PAGE_SHIFT;
  nbuf = offset/dev->alloc_sm_buflen;
  loff = offset % dev->alloc_sm_buflen;
  vaddr = ((unsigned char *)dev->sgl[nbuf].cpu) + loff;
  //printk("vaddr: %p offset: %d nbuf %d loff %d\n",vaddr,offset,nbuf,loff);
  pg = virt_to_page( vaddr );
  get_page( pg ); 
  vmf->page = pg;
  spin_unlock_irqrestore( &dev->nopage_lock, flags );
  return 0;
 
#ifdef NOT  
  // alternate ending follows:
  offset = vma->vm_pgoff << PAGE_SHIFT;
  page_addr = (void *)(((unsigned long)vmf->virtual_address - vma->vm_start) + (vma->vm_pgoff << PAGE_SHIFT));
  page_frame = ((unsigned long)page_addr >> PAGE_SHIFT);
  //pg = virt_to_page(__va (page_addr));
  pg = pfn_to_page(page_frame);
  get_page(pg);
  vmf->page = pg;
  spin_unlock_irqrestore( &dev->nopage_lock, flags );
  return 0;
#endif
  
#ifdef NOT  
  printk("SI dev->alloc_sm_buflen: %x\n",dev->alloc_sm_buflen);
  nbuf = off/dev->alloc_sm_buflen;
  loff = off % dev->alloc_sm_buflen;
  if( nbuf >= dev->dma_nbuf ) {
    printk("SI nopage, requested more mmap than data: nbuf %d max %d\n", 
      nbuf, dev->dma_nbuf );
    spin_unlock_irqrestore( &dev->nopage_lock, flags );
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
    return(NOPAGE_SIGBUS);
#else
    return(VM_FAULT_SIGBUS);
#endif
  }

  /* force return here */
  return(VM_FAULT_SIGBUS);

  vaddr = ((unsigned char *)dev->sgl[nbuf].cpu) + loff;

  pg = virt_to_page( vaddr );
  get_page( pg ); 
  spin_unlock_irqrestore( &dev->nopage_lock, flags );

  //if( type )
    //*type = VM_FAULT_MINOR;

  return( pg );
#endif
}

static struct vm_operations_struct si_vm_ops = {
  .open  = si_vmaopen, 
  .close = si_vmaclose, 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
  .nopage = si_vmanopage
#else
  .fault = si_vmanopage
#endif
};

int si_mmap(filp, vma )
struct file *filp;
struct vm_area_struct *vma;
{
  struct SIDEVICE *dev;

  dev = (struct SIDEVICE *)filp->private_data;

  if( dev->verbose )
    printk("SI mmap vmact %d ptr 0x%x\n", 
         atomic_read(&dev->vmact), (unsigned int)vma->vm_file );

  vma->vm_ops = &si_vm_ops;
  vma->vm_file = filp;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
  vma->vm_flags |= VM_RESERVED;   /* Don't swap */
#else
  vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;   /* Don't swap */
#endif

  si_vmaopen(vma);
  return(0);
}

/* setup dma channel */

/* alloc all the memory for a dma transfer
   dev->sgl holds an array of scatted gather tables
   which holds the pointers to the dma.
   An additional element, cpu is added to hold
   the kernel side virt address of the hardware address 
*/

int si_config_dma( dev )
struct SIDEVICE *dev;
{
  int nchains, nb, nbytes;
  unsigned int end_mask;
  struct SIDMA_SGL *ch;
  dma_addr_t ch_dma, last;   /* pci address */
  __u32 local_addr, cmd_stat;
  int buflen, nbuf, sm_buflen, isalloc;
  unsigned char setb;
  unsigned long flags;

/* stop the dma if its running */

  cmd_stat = PLX_REG8_READ(dev, PCI9054_DMA_COMMAND_STAT);
  if( cmd_stat & 1 )
    si_stop_dma(dev, NULL );

  printk("si_config_dma alloc_maxever %d alloc_buflen %d\n", 
    dev->alloc_maxever, dev->alloc_buflen );

  if( dev->alloc_maxever == 0 ) { /* allocate the memory */
    if( dev->dma_cfg.total <= 0 ) {
      printk("SI config.total %d\n", dev->dma_cfg.total ); 
      return -EIO;
    }
    if( dev->dma_cfg.maxever <= 0 )
      dev->dma_cfg.maxever = dev->dma_cfg.total;
    
    dev->alloc_maxever = dev->dma_cfg.maxever;
    dev->alloc_buflen = dev->dma_cfg.buflen;
    si_alloc_memory(dev);
    isalloc = 1;
  } else {
    isalloc = 0;
  }
  printk("si_config_dma2 alloc_maxever %d alloc_buflen %d\n", 
    dev->alloc_maxever, dev->alloc_buflen );

  if( dev->dma_cfg.maxever > dev->alloc_maxever ) {
    printk("SI config need to freemem, maxever: asked for %d have %d\n",
      dev->dma_cfg.maxever,  dev->alloc_maxever );
    return -EIO;
  }

  if( dev->dma_cfg.buflen != dev->alloc_buflen ) {
    printk("SI config need to freemem, buflen: asked for %d have %d\n",
      dev->dma_cfg.buflen,  dev->alloc_buflen );
    return -EIO;
  }

/* sm_buflen is size of the mmapped buffer */
/* buflen is the size of the dma buffer (likely the same) */
/* nbytes is the number of bytes to xfer in last buffer */

  nbuf = dev->dma_cfg.total / dev->dma_cfg.buflen;
  if( (nbytes = dev->dma_cfg.total  % dev->dma_cfg.buflen) )
    nbuf++;

  dev->dma_nbuf = nbuf;
  buflen = dev->dma_cfg.buflen;

  if( nbytes == 0 )
    nbytes = buflen;

  dev->dma_nbuf = nbuf;

  if( (buflen % PAGE_SIZE) == 0 )
    sm_buflen = buflen;
  else
    sm_buflen = buflen + PAGE_SIZE - (buflen%PAGE_SIZE);

  dev->alloc_sm_buflen = sm_buflen;


/* check limits */
  if( nbuf < 1 || buflen < PAGE_SIZE ) {
    printk( "SI si_dma_init nbuf %d buflen %d\n", nbuf, buflen );
    return(-EIO);
  }

  if( (unsigned int)(nbuf * buflen) > 0x7fffffff ) {
    printk( "SI si_dma_init too big nbuf %d buflen %d\n", nbuf, buflen );
    return(-EIO);
  }

  if( buflen != sm_buflen ) {
    printk("SI WARNING buflen %d sm_buflen %d, not a multiple of page size\n", 
      buflen, sm_buflen );
  }

  spin_lock_irqsave( &dev->dma_lock, flags );

  if( dev->dma_cfg.config & SI_DMA_CONFIG_WAKEUP_EACH ) 
    end_mask  = SIDMA_DPR_PCI_SRC| SIDMA_DPR_IRUP|SIDMA_DPR_TOPCI;
  else
    end_mask  = SIDMA_DPR_PCI_SRC| SIDMA_DPR_TOPCI;

  last = 0;
  if( dev->verbose )
    printk( "SI buflen %d dma_nbuf %d\n", buflen, dev->dma_nbuf );
  
  local_addr = SI_LOCAL_BUSADDR;
  nchains = dev->dma_nbuf;
  for( nb=nchains-1; nb>=0; nb-- ) {
    printk("SI chain %d\n",nb);
    ch = &dev->sgl[nb];    /* kernel virt address of this SGL */
    ch_dma = dev->sgl_pci + sizeof(struct SIDMA_SGL )*nb; /*bus side address*/
    ch->siz = nbytes;
    nbytes = buflen;
    ch->dpr = last | end_mask;

    last = (dma_addr_t)ch_dma & 0xfffffff0;
    setb = ((nb+1)&0x7f); /* set mem to see mmap working */
    memset( (void *)ch->cpu, setb, sm_buflen );

  }
/* always wake up at the end */
  end_mask  = SIDMA_DPR_PCI_SRC| SIDMA_DPR_IRUP|
               SIDMA_DPR_TOPCI | SIDMA_DPR_EOC;
  dev->sgl[dev->dma_nbuf-1].dpr = last | end_mask; /* point last at first */
  spin_unlock_irqrestore( &dev->dma_lock, flags );


  if( dev->verbose )
    printk("SI si_config_dma sgl 0x%p sgl_pci 0x%p buflen %d sm_buflen %d nbuf %d\n", 
     dev->sgl, dev->sgl_pci, 
     buflen, sm_buflen, nbuf  );

  if( isalloc )
    si_print_memtable( dev );

  return 0;
}

/* allocate memory */

int si_alloc_memory( dev )
struct SIDEVICE *dev;
{
  int nbuf, buflen, sm_buflen, nb;
  struct SIDMA_SGL *ch;
  void *cpu;
  unsigned long flags;
  struct page *page, *pend;
  dma_addr_t ch_dma, dma_buf;
  __u32 local_addr;

  unsigned char setb;

  if( dev->sgl ) {
    printk( "SI alloc memory already allocated\n"); 
    return -EIO;
  }
  
  nbuf = dev->alloc_maxever / dev->alloc_buflen;
  if( (dev->alloc_maxever % dev->alloc_buflen) )
    nbuf++;

  dev->alloc_nbuf = nbuf;
  buflen = dev->alloc_buflen;

  if( (dev->alloc_buflen % PAGE_SIZE) == 0 )
    dev->alloc_sm_buflen = dev->alloc_buflen;
  else
    dev->alloc_sm_buflen = dev->alloc_buflen + PAGE_SIZE - 
                          (dev->alloc_buflen%PAGE_SIZE);

  sm_buflen = dev->alloc_sm_buflen;
  local_addr = SI_LOCAL_BUSADDR;
  
  
  /* allocate memory for SGL table */
  dev->sgl_len = nbuf * sizeof(struct SIDMA_SGL);
  dev->sgl = pci_alloc_consistent( dev->pci, dev->sgl_len, &dev->sgl_pci); /* allocate memory for SGL table */
  if (dev->sgl) {
	  printk("SI SGL memory allocated at %p nbuf: %d SGLsize: %d dev->sgl_len: %d\n", dev->sgl, nbuf, sizeof(struct SIDMA_SGL), dev->sgl_len);
  }
//  dev->sgl = jeff_alloc( dev->sgl_len, &dev->sgl_pci); 

//  spin_lock_irqsave( &dev->dma_lock, flags );

  if( !dev->sgl ) {
    printk( "SI no memory allocating table\n"); 
//    spin_unlock_irqrestore( &dev->dma_lock, flags );
    return -EIO;
  }
  memset(dev->sgl, 0, dev->sgl_len); /* clear so cpu will be null if fail */

  
  /* allocate memory for the buffers */
  printk("SI allocate memory for the buffers total_allocs: %d total_bytes %d\n",dev->total_allocs,dev->total_bytes);
  dev->total_allocs++;
  dev->total_bytes += dev->sgl_len;

  for( nb=nbuf-1; nb>=0; nb-- ) {

    cpu = pci_alloc_consistent( dev->pci, sm_buflen, &dma_buf ); 
    //cpu = jeff_alloc( sm_buflen, &dma_buf ); 
    if( !cpu ) {
      printk( "SI no memory allocating buffer %d\n", nbuf - nb); 
//      spin_unlock_irqrestore( &dev->dma_lock, flags );
      si_free_sgl(dev);   
      return -EIO;
    }

//   if I am already doing the pci_alloc_consistent, why do I need this 
    pend = virt_to_page(cpu + buflen-1);
    page = virt_to_page(cpu);
    while( page <= pend ) {
      SetPageReserved(page);		/* SetPageReserved so it does not get swapped out */ 
      get_page(page); /* doit once too many so it sticks */
      page++;
    }

    ch = &dev->sgl[nb];    /* kernel virt address of this SGL */
    ch_dma = dev->sgl_pci + sizeof(struct SIDMA_SGL )*nb; /*bus side address*/
    ch->padr = (__u32)dma_buf;
    //printk("dma_buf map %p %x\n",dma_buf,ch->padr);
    ch->ladr = local_addr;
    ch->cpu = (__u64)cpu;
    //printk("cpu map %p %lx\n",cpu,ch->cpu);
    ch->siz = 0;
    ch->dpr = 0;
    dev->total_allocs++;
    dev->total_bytes += buflen;
    setb = ((nb+1)&0x7f); /* set mem to see mmap working */
    memset( cpu, setb, sm_buflen );

  }
//  spin_unlock_irqrestore( &dev->dma_lock, flags );

  if( dev->verbose )
    printk("SI si_alloc_memory - includes sgl and buffers, %d allocates and %d bytes\n",
      dev->total_allocs, dev->total_bytes );

 return 0;
}

void si_print_memtable( dev )
struct SIDEVICE *dev;
{
  int nb;
  struct SIDMA_SGL *ch;

  if( !dev->sgl )
   return;

  printk("si_print_memtable nbuf %d\n",  dev->dma_nbuf );

  printk("SI           ch          padr       ladr       siz        dpr        cpu\n" );
  for( nb=0; nb< dev->dma_nbuf; nb++ ) {
    ch = &dev->sgl[nb];
    if( dev->verbose )
      printk("SI 0x%p 0x%x 0x%x 0x%x 0x%x 0x%p\n", 
      (void *)dev->sgl + sizeof(struct SIDMA_SGL)* nb, 
        ch->padr, ch->ladr, ch->siz, ch->dpr, (void *)ch->cpu );
    ch++;
  }
}

void si_free_sgl( dev )
struct SIDEVICE *dev;
{
  int n, nchains;
  struct SIDMA_SGL *ch, *dchain;
  int total_frees, total_bytes, sm_buflen;
  struct page *page, *pend;
  unsigned long flags;
  
  if( !dev->sgl )
    return;

  if( atomic_read(&dev->vmact) != 0 ) /* what about multiple opens */
    return;

  dchain = dev->sgl;

  spin_lock_irqsave( &dev->dma_lock, flags );
  dev->sgl = 0;
  spin_unlock_irqrestore( &dev->dma_lock, flags );

  total_frees = 0;
  total_bytes = 0;
  nchains = dev->alloc_nbuf;
  if( dev->verbose )
    printk("SI free_sgl nbuf %d\n", nchains );

  if( (dev->alloc_buflen % PAGE_SIZE) == 0 )
    sm_buflen = dev->alloc_buflen;
  else
    sm_buflen = dev->alloc_buflen + PAGE_SIZE - (dev->alloc_buflen%PAGE_SIZE);


  for( n=0; n<nchains; n++ ) {
    ch = &dchain[n];
    if( !ch->cpu )
      break;

/* does free_consist do this */

    pend = virt_to_page(ch->cpu + sm_buflen-1);
    page = virt_to_page(ch->cpu);
    while( page <= pend ) {
      ClearPageReserved(page);
      put_page_testzero(page);
      page++;
    }

    pci_free_consistent( dev->pci, sm_buflen, 
      (void *)ch->cpu, ch->padr );
    total_frees++;
    total_bytes += dev->dma_cfg.buflen;
  }
  pci_free_consistent( dev->pci, dev->sgl_len, dchain, dev->sgl_pci );
  total_frees++;
  total_bytes += dev->sgl_len;
  dev->dma_cfg.buflen = 0;
  dev->sgl_pci = 0;
  if( dev->verbose )
    printk("SI free_sgl allocates freed %d, bytes %d\n", 
    total_frees, total_bytes );

  dev->total_allocs = 0;
  dev->total_bytes = 0;

  dev->alloc_nbuf = 0;
  dev->alloc_buflen = 0;
  dev->alloc_sm_buflen = 0;
  dev->alloc_maxever = 0;
}

/* start configured dma */

int si_start_dma(dev)
struct SIDEVICE *dev;
{
  int n_pixels;
  int rb_count;
  __u32 reg;
  unsigned long flags;
  int i=0;
  n_pixels = dev->dma_cfg.total/2;

  if( dev->test ) {
    printk("SI start_dma test mode \n");
    return 0;
  }
  
  reg = PLX_REG8_READ(dev, PCI9054_DMA_COMMAND_STAT );
  if( reg & 1 ) { /* already on stop */
    printk("SI start_dma already on stopping first, dma_stat 0x%x\n", reg );
    si_stop_dma( dev, NULL );
  }

  spin_lock_irqsave( &dev->dma_lock, flags );
    // Start DMA

  // load pixel counter with number of pixels
  switch (dev->IDNumber) {
  case 97:
	LOCAL_REG_LWRITE(dev, LOCAL_PIX_CNT_LL, n_pixels        & 0xff);
	LOCAL_REG_LWRITE(dev, LOCAL_PIX_CNT_ML, (n_pixels >> 8) & 0xff);
	LOCAL_REG_LWRITE(dev, LOCAL_PIX_CNT_MH, (n_pixels >>16) & 0xff);
	LOCAL_REG_LWRITE(dev, LOCAL_PIX_CNT_HH, (n_pixels >>24) & 0xff);
	break;
  case 104:
	LOCAL_REG_LWRITE(dev, LOCAL_PIX_CNT_LO, n_pixels        & 0xffff);
	LOCAL_REG_LWRITE(dev, LOCAL_PIX_CNT_HI, (n_pixels >> 16) & 0xffff);
	break;
  case 5208:
	LOCAL_REG_LWRITE(dev, LOCAL_PIX_CNT_LL, n_pixels);
	break;
  default:
	  break;
  }
  switch (dev->IDNumber) {
  case 97:
	rb_count  =  LOCAL_REG_LREAD(dev, LOCAL_PIX_CNT_LL) & 0xff;
	rb_count += (LOCAL_REG_LREAD(dev, LOCAL_PIX_CNT_ML) & 0xff) << 8;
	rb_count += (LOCAL_REG_LREAD(dev, LOCAL_PIX_CNT_MH) & 0xff) << 16;
	rb_count += (LOCAL_REG_LREAD(dev, LOCAL_PIX_CNT_HH) & 0xff) << 24;
	break;
  case 104:
	rb_count  =  LOCAL_REG_LREAD(dev, LOCAL_PIX_CNT_LO) & 0xffff;
	rb_count += (LOCAL_REG_LREAD(dev, LOCAL_PIX_CNT_HI) & 0xffff) << 16;
	break;
  case 5208:
	rb_count  =  LOCAL_REG_LREAD(dev, LOCAL_PIX_CNT_LL);
	break;
  }
  if( rb_count != n_pixels )
    printk("SI start_dma ERROR pixel register mismatch %d %d\n",
      n_pixels, rb_count  );

  // enable FIFOs
  LOCAL_REG_WRITE(dev, LOCAL_COMMAND, (LC_FIFO_MRS_L | LC_FIFO_PRS_L));  
  
  atomic_set(&dev->dma_done, 0x1); // reflection of status bit
  dev->dma_cur = 0;
  dev->dma_next = 0;
    // setup DMA mode, turns on interrrupt DMA0
  PLX_REG_WRITE(dev, PCI9054_DMA0_MODE, (__u32)0x00021f43);

    // Write SGL physical address & set descriptors in PCI space
  PLX_REG_WRITE(dev, PCI9054_DMA0_DESC_PTR, (__u32)dev->sgl_pci | (1 << 0));

    // Enable DMA channel
  PLX_REG8_WRITE(dev, PCI9054_DMA_COMMAND_STAT, ((1 << 0)));

  reg = PLX_REG_READ( dev, PCI9054_INT_CTRL_STAT);
  PLX_REG_WRITE( dev, PCI9054_INT_CTRL_STAT, reg | ((1 << 8) | (1<<18)));
    // Start DMA
  PLX_REG8_WRITE(dev, PCI9054_DMA_COMMAND_STAT, (((1 << 0) | (1 << 1))));

  spin_unlock_irqrestore( &dev->dma_lock, flags );

  if( dev->verbose ) {
    printk("SI Starting DMA transfer, int_stat 0x%x rb_count %d\n",
      reg, rb_count);
  }

  return 0;
}

/* stop running dma */

int si_stop_dma(dev, status )
struct SIDEVICE *dev;
struct SI_DMA_STATUS *status;
{
  unsigned long flags;
  int ret;
  __u32 cmd_stat;

  ret = 0;
  dev->abort_active = 1;
  spin_lock_irqsave( &dev->dma_lock, flags );

  cmd_stat = PLX_REG8_READ(dev, PCI9054_DMA_COMMAND_STAT);
  if( cmd_stat & 1 ) { /* if dma enabled, do abort sequence */
    PLX_REG8_WRITE(dev, PCI9054_DMA_COMMAND_STAT,  0x0 ); /* disable*/
    PLX_REG8_WRITE(dev, PCI9054_DMA_COMMAND_STAT, (1<<2) ); /* abort */
    atomic_set(&dev->dma_done, cmd_stat);
  }
  spin_unlock_irqrestore( &dev->dma_lock, flags );
  if( dev->verbose )
    printk("SI stop_dma stat 0x%x\n", cmd_stat );

  if( cmd_stat & 1 ) {
    ret = 10; /* jiffies timeout */
    wait_event_interruptible_timeout( 
       dev->dma_block, (atomic_read(&dev->dma_done)&SI_DMA_STATUS_DONE), ret );
    if( !(atomic_read(&dev->dma_done)&SI_DMA_STATUS_DONE) ) {
       printk("SI timeout in abort sequence\n");
       ret = -EIO;
    } else 
      ret = 0;
  }
  dev->abort_active = 0;

  si_dma_status(dev, status);
  return ret;
}

/* no block status request */

int si_dma_status(dev, stat)
struct SIDEVICE *dev;
struct SI_DMA_STATUS *stat;
{
  if( !stat )
    return 0;

  stat->status = atomic_read(&dev->dma_done);

  stat->transferred =  si_dma_progress( dev );
  stat->next = dev->dma_next;
  stat->cur  = dev->dma_cur;

  return 0;
}

/* block for next buffer complete */

int si_dma_next(dev, stat)
struct SIDEVICE *dev;
struct SI_DMA_STATUS *stat;
{
  int next, cur, ret, tmout;
  unsigned long flags;

  tmout = dev->dma_cfg.timeout; /* jiffies timeout */
  ret = 0;
  if( dev->dma_cfg.config & SI_DMA_CONFIG_WAKEUP_EACH ) {
    spin_lock_irqsave( &dev->dma_lock, flags );
    next = dev->dma_next;
    cur = dev->dma_cur;
    spin_unlock_irqrestore( &dev->dma_lock, flags );
    if( next >= cur ) {
      if( !si_dma_wakeup(dev) ) {
        wait_event_interruptible_timeout( dev->dma_block, 
          si_dma_wakeup( dev ), tmout );
        if( si_dma_wakeup(dev) )
          ret = 0;
        else 
          ret = -EWOULDBLOCK;
      } else {
        ret = 0;
      }
    } else {
        ret = 0;
      }
  } else {
    if( !si_dma_wakeup(dev) ) {
      wait_event_interruptible_timeout( dev->dma_block, 
        si_dma_wakeup( dev ), tmout );
      if( si_dma_wakeup(dev) )
        ret = 0;
      else 
        ret = -EWOULDBLOCK;
    } else  {
      ret = 0;
    }
  }
  si_dma_status( dev, stat );
//  if( stat->transferred == 0 ) {
//    printk("SI dma_next wakeup with transfer zero\n");
//  }

  if( ret == 0 )
    dev->dma_next++;

  return ret;
}

/* true if its time to wakeup dma_block */

int si_dma_wakeup( dev )
struct SIDEVICE *dev;
{
  int ret;
  int done;

  done = ((atomic_read( &dev->dma_done) & SI_DMA_STATUS_DONE)!=0);

  if( !dev->sgl ) { /* if its not configured or enabled, always wakeup */
    ret = 1;
  } else {
    if( dev->dma_cfg.config & SI_DMA_CONFIG_WAKEUP_EACH )
      ret = ((dev->dma_next < dev->dma_cur)||(done != 0));
    else
      ret = done;
  }

  if( ret && dev->verbose )
    printk("SI wakeup done %d\n", ret );
  return ret;
}

/* wait for vma close */

int si_wait_vmaclose( dev )
struct SIDEVICE *dev;
{
  int tmout;

  tmout = VMACLOSE_TIMEOUT;

  printk("si_wait_vmaclose waiting\n");
  wait_event_interruptible_timeout( dev->mmap_block, 
    (atomic_read(&dev->vmact)==0), tmout );

  printk("si_wait_vmaclose wakeup\n");
  if( atomic_read(&dev->vmact) > 0 ) {
    if( dev->verbose )
      printk("si_wait_vmaclose timeout\n");
    return -EWOULDBLOCK;
  } else {
    if( dev->verbose )
      printk("si_wait_vmaclose ok\n");
    return 0;
  }
}

/* return byte count progress */

int si_dma_progress( dev )
struct SIDEVICE *dev;
{
  __u32 pci;
  int nb, nchains, prog;
  struct SIDMA_SGL *ch;

  pci = PLX_REG_READ( dev, PCI9054_DMA0_PCI_ADDR);

  prog = 0;
  nb = 0;
  nchains = dev->dma_nbuf;

  for( nb=0; nb<nchains; nb++) {
    ch = &dev->sgl[nb];    /* kernel virt address of this SGL */
    prog += ch->siz;
    if( ch->padr == pci ) {
      break;
    }
//    printk( "SI pci 0x%x prog %d %d\n", ch->padr, prog, ch->siz );
  }

/* this can happen if its already done */

  if( prog > dev->dma_cfg.total ) {
    printk("SI prog %d total %d dma_done 0x%x\n", prog, dev->dma_cfg.total,
      atomic_read(&dev->dma_done) );

    prog = dev->dma_cfg.total;
  }

  return prog;
}


void *jeff_alloc( size, pphy )
int size;
dma_addr_t *pphy;
{
  unsigned char *km;
//  unsigned int phy, phy_ix;
//  int count;
  int order;

  order  = get_order( size );
  printk("order %d\n", order );
  if( !( km = (unsigned char *)__get_free_pages( GFP_KERNEL, order )) ) {
    printk("SI TEST get_free_pages no memory\n");
    return NULL;
  } else {
    //int i;
    
   memset( km, 0, size );

  if( pphy )
    *pphy = (dma_addr_t) virt_to_phys( km );

//    count = 0;
    
//    for( i=0; i<TEST_SIZE; i+=8192 ) {
//       phy_ix = (unsigned int)virt_to_phys( km+i );
//       if( phy_ix != (phy + i ))
//         count++;
//    }
//    if( count == 0 )
//      printk("SI TEST worked count %d\n", count );
//    else
//      printk("SI TEST failed count %d\n", count );
//    
//    free_pages((unsigned long)km, order);
  
    return (void *)km;
  }
}
