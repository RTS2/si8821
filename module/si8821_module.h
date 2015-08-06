
/* This file holds the defines and internal structures
   for the linux 2.6 driver for
   Spectral Instruments Interface cards
*/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
    #define SI_DPC_PARAM      VOID
#else
    #define SI_DPC_PARAM      struct work_struct
#endif

/* uart control structure */

struct UART {
  int  serialbufsize;
  __u8 *rxbuf;
  __u8 *txbuf;
  volatile int rxput;
  volatile int rxget;
  volatile int rxcnt;
  int txput;
  volatile int txget;
  volatile int txcnt;
  int baud;
  int bits;
  int parity;
  int stopbits;
  int fifotrigger;
  int block;
  int timeout; /* jiffies for write timeout */
};

#define PCI_NUM_BARS_TYPE_00             6              // Total PCI BARs for Type 0 Header
// BAR flags
typedef enum _PLX_BAR_FLAG
{
  PLX_BAR_FLAG_MEM           = (1 << 0),
  PLX_BAR_FLAG_IO            = (1 << 1),
  PLX_BAR_FLAG_BELOW_1MB     = (1 << 2),
  PLX_BAR_FLAG_32_BIT        = (1 << 3),
  PLX_BAR_FLAG_64_BIT        = (1 << 4),
  PLX_BAR_FLAG_PREFETCHABLE  = (1 << 5),
  PLX_BAR_FLAG_UPPER_32      = (1 << 6),
  PLX_BAR_FLAG_PROBED        = (1 << 7)
} PLX_BAR_FLAG;

/* PCI BAR Properties */
typedef struct _PLX_PCI_BAR_PROP
{
  __u64 BarValue;          /* Actual value in BAR */
  __u64 Physical;          /* BAR Physical Address */
  __u64 Size;              /* Size of BAR space */
  __u32 Flags;             /* Additional BAR properties */
} PLX_PCI_BAR_PROP;

/* PCI BAR Space information */
typedef struct _PLX_PCI_BAR_INFO
{
  __u8 *pVa;                /* BAR Kernel Virtual Address */
  PLX_PCI_BAR_PROP Properties;  /* BAR Properties */
  __s8 bResourceClaimed;    /* Was driver able to claim region? */
} PLX_PCI_BAR_INFO;

/* device structure for one SI card */

struct SIDEVICE {
  struct SIDEVICE *next;   /* one for each card */
  struct pci_dev *pci;     /* device found by kernel        */
  spinlock_t uart_lock;    /* protection for uart registers */
  spinlock_t dma_lock;     /* protection for dma registers  */
  spinlock_t nopage_lock;  /* protection for nopage/mmap  */
  atomic_t isopen;         /* true when device is open      */
  int minor;
  __u32 bar[4];  /*  PCI bus address mappings */ 
  unsigned int bar_len[4]; /* length of PCI bus address mappings */
  PLX_PCI_BAR_INFO  PciBar[PCI_NUM_BARS_TYPE_00];
  __u8 *pRegVa;            /* Virtual address to registers */
  atomic_t vmact;          /* number of vma opens */

  __u32 IDNumber;          /* card in use (104:PC104 PN3383, 97:PCI PN3097, 5208:PCIe PN5308) */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
  struct tq_struct task;
#else
  struct work_struct task;
  struct workqueue_struct  *bottom_half_wq; /* Task queue for bottom half */
#endif

  wait_queue_head_t dma_block;    /* for those who block on DMA */
  int source;              /* interrupt source, passed from irup to bhalf */
  struct UART Uart;        /* structure for uart control */
  wait_queue_head_t uart_rblock; /* for those who block on reads  */
  wait_queue_head_t uart_wblock; /* for those who block on writes */
  wait_queue_head_t mmap_block;  /* for re-init of DMA (wait for vmaclose) */
  int verbose;             /* print messages when true   */

  struct SIDMA_SGL *sgl;   /* array of scatter gather tables */
  dma_addr_t sgl_pci;      /* bus side address of sgl */
  struct SI_DMA_CONFIG dma_cfg; /* dma config struct from ioctl */
  int dma_nbuf;            /* number of buffers (length of sgl[]) */
  int dma_total_len;       /* total to transfer */
  __u32 sgl_len;           /* buflen * nbuf */
  int total_allocs;        /* memory usage statistics */
  int total_bytes;
  atomic_t dma_done;       /* true if irup detects dma done */
  int dma_next;            /* next counter */
  int dma_cur;             /* which dma sgl is active */
  int test;                /* true if test mode (no hardware) */
  int abort_active;        /* abort sequence active */
  __u32 irup_reg;          /* hold reg from irup */
  __u32 rb_count;          /* local bus count at dma_done (must be zero) */
  int setpoll;             /* conditional for the function of select (poll)*/
  int alloc_maxever;       /* these must match dma_cfg */
  int alloc_buflen;         
  int alloc_nbuf;         
  int alloc_sm_buflen;     /* dma_buflen padded out to PAGE_SIZE */
};

#define VMACLOSE_TIMEOUT (10*HZ)   /* seconds */

// local address of the fifo read (no increment)
#define SI_LOCAL_BUSADDR 0x30000004 


// Macros for I/O port access
#define IO_PORT_READ_8(port)                        inb( (U16)(port) )
#define IO_PORT_READ_16(port)                       inw( (U16)(port) )
#define IO_PORT_READ_32(port)                       inl( (U16)(port) )
#define IO_PORT_WRITE_8(port, val)                  outb( (val), (U16)(port) )
#define IO_PORT_WRITE_16(port, val)                 outw( (val), (U16)(port) )
#define IO_PORT_WRITE_32(port, val)                 outl( (val), (U16)(port) )

#if defined(ioread8)
    #define PHYS_MEM_READ_8                         ioread8
    #define PHYS_MEM_READ_16                        ioread16
    #define PHYS_MEM_READ_32                        ioread32
    #define PHYS_MEM_WRITE_8(addr, data)            iowrite8 ( (data), (addr) )
    #define PHYS_MEM_WRITE_16(addr, data)           iowrite16( (data), (addr) )
    #define PHYS_MEM_WRITE_32(addr, data)           iowrite32( (data), (addr) )
#else
    #define PHYS_MEM_READ_8                         readb
    #define PHYS_MEM_READ_16                        readw
    #define PHYS_MEM_READ_32                        readl
    #define PHYS_MEM_WRITE_8(addr, data)            writeb( (data), (addr) )
    #define PHYS_MEM_WRITE_16(addr, data)           writew( (data), (addr) )
    #define PHYS_MEM_WRITE_32(addr, data)           writel( (data), (addr) )
#endif

// Macros for PLX chip register access
#define PLX_REG_READ(pdx, offset)\
  PHYS_MEM_READ_32((__u32 *)(dev->pRegVa + (offset)))
#define PLX_REG_WRITE(pdx, offset, value) \
  PHYS_MEM_WRITE_32((__u32 *)(dev->pRegVa + (offset)),(value))

#define PLX_REG8_READ(pdx, offset)\
  PHYS_MEM_READ_8((__u32 *)(dev->pRegVa + (offset)))
#define PLX_REG8_WRITE(pdx, offset, value) \
  PHYS_MEM_WRITE_8((__u32 *)(dev->pRegVa + (offset)),(value))

// Macros for SI UART access
#define UART_REG_READ(pdx, offset) inb((int)(pdx)->PciBar[2].Properties.Physical + (offset))
#define UART_REG_WRITE(pdx, offset, value)\
  outb((value), (int)(pdx)->PciBar[2].Properties.Physical + (offset))

// Macros for SI LOCAL access
#define LOCAL_REG_READ(pdx, offset) \
  (inl((int)(pdx)->PciBar[3].Properties.Physical + (offset * sizeof(__u32))) & 0xff)
#define LOCAL_REG_WRITE(pdx, offset, value)\
  outl((value & 0xff), (int)(pdx)->PciBar[3].Properties.Physical + (offset * sizeof(__u32)))
#define LOCAL_REG_LREAD(pdx, offset) \
  (inl((int)(pdx)->PciBar[3].Properties.Physical + (offset * sizeof(__u32))))
#define LOCAL_REG_LWRITE(pdx, offset, value)\
  outl((value), (int)(pdx)->PciBar[3].Properties.Physical + (offset * sizeof(__u32)))


/*
   This INTER_TYPE_* mask is used for the "source" word that
   is passed to the bottom half of the interrupt service 
   routine to tell what kind of interrupt occured
*/

#define INTR_TYPE_NONE                  0 
#define INTR_TYPE_LOCAL_1               (1 << 0)
#define INTR_TYPE_LOCAL_2               (1 << 1)
#define INTR_TYPE_PCI_ABORT             (1 << 2)
#define INTR_TYPE_DOORBELL              (1 << 3)
#define INTR_TYPE_OUTBOUND_POST         (1 << 4)
#define INTR_TYPE_DMA_0                 (1 << 5)
#define INTR_TYPE_DMA_1                 (1 << 6)
#define INTR_TYPE_DMA_2                 (1 << 7)
#define INTR_TYPE_DMA_3                 (1 << 8)
#define INTR_TYPE_SOFTWARE              (1 << 9)

/* 
  these macros define the lower four bits
  of the SIDMA_SGL dpr register
*/

#define SIDMA_DPR_PCI_SRC  0x01  /* 1 pci 0 local descriptor */
#define SIDMA_DPR_EOC      0x02  /* true if end of chain */
#define SIDMA_DPR_IRUP     0x04  /* true if irup desired after this block */
#define SIDMA_DPR_TOPCI    0x08  /* true if direction is toward PCI bus */

/* 
  This is the DMA Scatter Gather list
  An array of these is allocated and populated 
  for each buffer requested by the DMA_INIT ioctl.

  fill is needed because the dpr register masks the
  lower 4 bits as address.
*/

struct SIDMA_SGL {
  __u32 padr;   /* DMA channel PCI address */
  __u32 ladr;   /* DMA channel local address */
  __u32 siz;    /* transfer size (bytes) */
  __u32 dpr;    /* descriptor pointer */
  __u64 cpu;    /* kernel virual address of padr */
  __u32 fill;   /* pad out to 16-byte clean */
  __u32 fill2;   /* pad out to 16-byte clean */
//  __u32 fill3;   /* pad out to 16-byte clean */
};


/*
   Standard PCI Configuration Registers
*/

#define PCI9054_VENDOR_ID            0x000
#define PCI9054_COMMAND              0x004
#define PCI9054_REV_ID               0x008
#define PCI9054_CACHE_SIZE           0x00C
#define PCI9054_RTR_BASE             0x010 /* BAR0 */
#define PCI9054_RTR_IO_BASE          0x014 /* BAR1 */
#define PCI9054_LOCAL_BASE0          0x018 /* BAR2 */
#define PCI9054_LOCAL_BASE1          0x01C /* BAR3 */
#define PCI9054_UNUSED_BASE1         0x020 /* BAR4 */
#define PCI9054_UNUSED_BASE2         0x024 /* BAR5 */
#define PCI9054_CIS_PTR              0x028
#define PCI9054_SUB_ID               0x02C
#define PCI9054_EXP_ROM_BASE         0x030
#define PCI9054_CAP_PTR              0x034
#define PCI9054_RESERVED2            0x038
#define PCI9054_INT_LINE             0x03C
#define PCI9054_PM_CAP_ID            0x040
#define PCI9054_PM_CSR               0x044
#define PCI9054_HS_CAP_ID            0x048
#define PCI9054_VPD_CAP_ID           0x04c
#define PCI9054_VPD_DATA             0x050


/*
   These registers are mapped via BAR0 and are accessed
   with the PLX_REG_[READ|WRITE] macros.
*/

#define PCI9054_SPACE0_RANGE         0x000
#define PCI9054_SPACE0_REMAP         0x004
#define PCI9054_LOCAL_DMA_ARBIT      0x008
#define PCI9054_ENDIAN_DESC          0x00c
#define PCI9054_EXP_ROM_RANGE        0x010
#define PCI9054_EXP_ROM_REMAP        0x014
#define PCI9054_SPACE0_ROM_DESC      0x018
#define PCI9054_DM_RANGE             0x01c
#define PCI9054_DM_MEM_BASE          0x020
#define PCI9054_DM_IO_BASE           0x024
#define PCI9054_DM_PCI_MEM_REMAP     0x028
#define PCI9054_DM_PCI_IO_CONFIG     0x02c
#define PCI9054_SPACE1_RANGE         0x0f0
#define PCI9054_SPACE1_REMAP         0x0f4
#define PCI9054_SPACE1_DESC          0x0f8
#define PCI9054_DM_DAC               0x0fc


#define PCI9054_MAILBOX0             0x078
#define PCI9054_MAILBOX1             0x07c

#define PCI9054_MAILBOX2             0x048
#define PCI9054_MAILBOX3             0x04c
#define PCI9054_MAILBOX4             0x050
#define PCI9054_MAILBOX5             0x054
#define PCI9054_MAILBOX6             0x058
#define PCI9054_MAILBOX7             0x05c
#define PCI9054_LOCAL_DOORBELL       0x060
#define PCI9054_PCI_DOORBELL         0x064
#define PCI9054_INT_CTRL_STAT        0x068
#define PCI9054_EEPROM_CTRL_STAT     0x06c
#define PCI9054_PERM_VENDOR_ID       0x070
#define PCI9054_REVISION_ID          0x074


// DMA Registers
#define PCI9054_DMA0_MODE            0x080
#define PCI9054_DMA0_PCI_ADDR        0x084
#define PCI9054_DMA0_LOCAL_ADDR      0x088
#define PCI9054_DMA0_COUNT           0x08c
#define PCI9054_DMA0_DESC_PTR        0x090
#define PCI9054_DMA1_MODE            0x094
#define PCI9054_DMA1_PCI_ADDR        0x098
#define PCI9054_DMA1_LOCAL_ADDR      0x09c
#define PCI9054_DMA1_COUNT           0x0a0
#define PCI9054_DMA1_DESC_PTR        0x0a4
#define PCI9054_DMA_COMMAND_STAT     0x0a8
#define PCI9054_DMA_ARBIT            0x0ac
#define PCI9054_DMA_THRESHOLD        0x0b0
#define PCI9054_DMA0_PCI_DAC         0x0b4
#define PCI9054_DMA1_PCI_DAC         0x0b8


// Messaging Unit Registers
#define PCI9054_OUTPOST_INT_STAT     0x030
#define PCI9054_OUTPOST_INT_MASK     0x034
#define PCI9054_MU_CONFIG            0x0c0
#define PCI9054_FIFO_BASE_ADDR       0x0c4
#define PCI9054_INFREE_HEAD_PTR      0x0c8
#define PCI9054_INFREE_TAIL_PTR      0x0cc
#define PCI9054_INPOST_HEAD_PTR      0x0d0
#define PCI9054_INPOST_TAIL_PTR      0x0d4
#define PCI9054_OUTFREE_HEAD_PTR     0x0d8
#define PCI9054_OUTFREE_TAIL_PTR     0x0dc
#define PCI9054_OUTPOST_HEAD_PTR     0x0e0
#define PCI9054_OUTPOST_TAIL_PTR     0x0e4
#define PCI9054_FIFO_CTRL_STAT       0x0e8

/*
  These registers are defined by SI and control
  the DMA fifo.  They are mapped to BAR2 and are acessed 
  with the LOCAL_REG_[READ|WRITE] macros.
*/

// defines for FIFOBaseRegisterAddress registers
#define LOCAL_COMMAND        0 // local command register  (R/W)
#define LOCAL_FIFO_SETUP     1 // access to FIFO setup register
#define LOCAL_STATUS         2 // local status register    
#define LOCAL_UART           3 // local UART register

#define LOCAL_PIX_CNT_LO	 4 // local pixel down-counter low word (only used in PC104 code)
#define LOCAL_PIX_CNT_HI	 5 // local pixel down-counter high word (only used in PC104 code)

#define LOCAL_ID_NUMBER      7 // ID number (104 for PC104 interface, 97 for PCI board PN 3097)

#define LOCAL_PIX_CNT_LL     8 // local pixel down-counter low word
#define LOCAL_PIX_CNT_ML     9 // local pixel down-counter m word
#define LOCAL_PIX_CNT_MH    10 // local pixel down-counter mh word
#define LOCAL_PIX_CNT_HH    11 // local pixel down-counter high word

#define LOCAL_REV_NUMBER    12 // Revision number

// LOCAL_COMMAND bit definitions
#define LC_FIFO_MRS_L        1 // fifo Master Reset (low active)
#define LC_FIFO_PRS_L        2 // fifo Partial Reset (low active)

// LOCAL_STATUS bit definitions
#define LS_FIFO_EF_L         1 // fifo Empty Flag (low active) (read only)
#define LS_FIFO_HF_L         2 // fifo Half Full Flag (low active) (read only)
#define LS_FIFO_FF_L         4 // fifo Full Flag (low active) (read only)
#define LS_HL_SD             8 // HotLink Signal Detect (read only)
#define LS_HL_LFI           16 // HotLink Line Fault Indicator (read only)
#define LS_IS_100_MHZ       32 // Interface runs at 100MByte/sec speed (read only)(3097 only)
#define LS_CAM_RESET        64 // Camera received reset (read only)(3097 only)

#define LS_UART_XMIT_DONE   32 // UART transmit done (read only)(PC104 only)
#define LS_UART_FIFO_EMPTY  64 // UART transmit done (read only)(PC104 only)
#define LS_UART_FIFO_FULL  128 // UART transmit done (read only)(PC104 only)


/*
  These macros refer to registers mapped to 
  BAR3 which control the 16550 UART serial port
  to the camera.
*/

#define SERIAL_TX   0
#define SERIAL_RX   0
#define SERIAL_DLL  0
#define SERIAL_IER  1
#define RX_INT      1
#define TX_INT      2
#define SERIAL_DLH  1
#define SERIAL_IIR  2
#define SERIAL_FCR  2
#define SERIAL_LCR  3
#define SERIAL_MCR  4
#define SERIAL_LSR  5
#define SERIAL_MSR  6
#define SERIAL_RAM  7

#define TRUE  1 
#define FALSE 0 


// Function prototypes

irqreturn_t si_interrupt( int, struct SIDEVICE *, struct pt_regs *);
int si_set_baud_rate(struct SIDEVICE *dev, __u32 baud);
int si_set_serial_params(struct SIDEVICE *, struct SI_SERIAL_PARAM *);
int si_init_uart(struct SIDEVICE *);
void si_cleanup_serial(struct SIDEVICE *);
int si_transmit_serial(struct SIDEVICE *, __u8 );

int si_receive_serial(struct SIDEVICE *, __u8 *);
int si_print_uart_stat(struct SIDEVICE *);
int si_uart_break(struct SIDEVICE *, int );


int si_stop_dma(struct SIDEVICE *, struct SI_DMA_STATUS *);
void si_free_sgl(struct SIDEVICE *dev);
#ifdef NOT
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
struct page *si_vmanopage( struct vm_area_struct *, unsigned long, int );
#else
struct page *si_vmanopage( struct vm_area_struct *, unsigned long, int * );
#endif
#endif
struct page *si_vmanopage( struct vm_area_struct *, struct vm_fault * );

int si_config_dma( struct SIDEVICE *);
void si_free_sgl( struct SIDEVICE *dev );
/* ioctl() replaced with unlocked_ioctl() in 2.6.36 & return type changed */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))
  int si_ioctl( struct inode *, struct file *, unsigned int, unsigned long );
#else
  long si_ioctl( struct file *, unsigned int, unsigned long );
#endif

int si_start_dma(struct SIDEVICE *);
int si_stop_dma( struct SIDEVICE *, struct SI_DMA_STATUS *);
int si_dma_status( struct SIDEVICE *, struct SI_DMA_STATUS *);
int si_dma_next( struct SIDEVICE *, struct SI_DMA_STATUS *);
int si_reset( struct SIDEVICE * );

int si_open (struct inode *, struct file *);
int si_close(struct inode *, struct file *);
ssize_t si_read( struct file *, char __user *, size_t, loff_t *);
ssize_t si_write(struct file *, const char __user *, size_t , loff_t *);
unsigned int si_poll( struct file *, poll_table *);

int si_mmap( struct file *filp, struct vm_area_struct *vma);
int si_uart_more_to_write( struct SIDEVICE * );
int si_dma_wakeup( struct SIDEVICE * );
int si_uart_read_ready( struct SIDEVICE * );
int si_uart_tx_empty( struct SIDEVICE * );
void si_uart_clear( struct SIDEVICE * );
void si_get_serial_params(struct SIDEVICE *, struct SI_SERIAL_PARAM *);
void si_bottom_half( SI_DPC_PARAM *inp );
int si_wait_vmaclose( struct SIDEVICE *);
int si_alloc_memory( struct SIDEVICE *dev );
void si_print_memtable( struct SIDEVICE *dev );
int si_dma_progress( struct SIDEVICE * );

