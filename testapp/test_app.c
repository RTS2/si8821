/* 

Test application for the
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

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>

#include "si8821.h"
#include "si_app.h"

/* 
  low level testout of the si8821 driver 
*/


int sendfile( int fd, int, char *);
void  init_com( int, int, int, int, int, int  );
void  send_break( int, int );
int print_status(struct SI_CAMERA *);
int print_readout(struct SI_CAMERA *);
int receive_n_ints( int, int, int * );
int print_config(struct SI_CAMERA *);
int print_mem_changes( short *, int );
void print_cfg( struct CFG_ENTRY *, int );

main(argc, argv )
int argc;
char *argv[];
{
  struct SI_CAMERA *c;
  char buf[256], file[256];
  int verbose, i;
  unsigned char rbyte;

  c = ( struct SI_CAMERA *)malloc(sizeof(struct SI_CAMERA ));
  bzero(c, sizeof(struct SI_CAMERA));

  strcpy( file, "/dev/SIPCI_0" );

  for( i=1; i<argc; i++ ) { 
    if( strcmp( argv[i], "-dev" ) == 0 ) {
      i++;
      if( argv[i] )
        strcpy( file, argv[i] );
    }
  }

  if((c->fd = open( file, O_RDWR, 0 ))<0 ){
    perror("si open" );
    fprintf(stderr, "failed to open %s\n", file);
    exit(1);
  }

  verbose = 1;
  if( ioctl(c->fd, SI_IOCTL_VERBOSE, &verbose) <0 ) {
    perror("verbose error");
  }

  init_com( c->fd, 250000, 0, 8, 1, 9000 ); /* setup uart for camera */
  load_camera_cfg( c, "8321A.cfg" );
  setfile_readout( c, "800-1156.set" );
  send_readout( c );
  send_command(c->fd, 'H');     // H    - load readout
  receive_n_ints( c->fd, 32, (int *)&c->readout );
  expect_y( c->fd );

  print_help();
  //printf("WARNING initialization commented out\n");

  while(1) {
    printf("si8821> ");
    fflush(stdout);
    fgets( buf, 256, stdin);
    if( buf[0] == '?' ) {
      print_help();
      continue;
    }
    parse_commands( c, buf );
  }
}

parse_commands( c, buf )
struct SI_CAMERA *c;
char *buf;
{
  char *delim = " \t\n";
  char *s;
  static int repeat = 1;
  static int verbose = 1;

  if( strncasecmp( buf, "sendfile", 8 ) == 0 ) {
    sendfile( c->fd, 250, "2769d.bin" );
  } else if( buf[0] == 'A' ) {
    send_command_yn(c->fd, 'A');  // A  - Open Shutter  returns Y/N  
  } else if( buf[0] == 'B' ) {
    send_command_yn(c->fd, 'B');  // B - Close Shutter returns Y/N
  } else if( buf[0] == 'I' ) {
    send_command(c->fd, 'I');     // I    - Get camera status 
    receive_n_ints( c->fd, 16, c->status );
    print_status( c );
    expect_y( c->fd );
  } else if( buf[0] == 'J' ) {
    set_config(c);

  } else if( buf[0] == 'H' ) {
    send_command(c->fd, 'H');     // H    - load readout
    receive_n_ints( c->fd, 32, (int *)&c->readout );
    print_readout( c );
    expect_y( c->fd );

  } else if( buf[0] == 'L' ) {
    send_command(c->fd, 'L');     // L    - load config
    receive_n_ints( c->fd, 32, (int *)&c->config );
    print_config( c );
    expect_y( c->fd );

  } else if( buf[0] == 'S' ) {
    send_command(c->fd, 'S');     // S    - Cooler On, no response

  } else if( buf[0] == 'T' ) {
    send_command(c->fd, 'T');     // T    - Cooler Off, no response

  } else if( buf[0] == '0' ) {
    send_command(c->fd, '0');
    // Abort Readout, aborts an ongoing exposure (camera cannot 
    // be stopped during actual readout)
  } else if( strncmp( buf, "stopdma", 7 )== 0 ) {
    stop_dma( c );
  } else if( strncmp( buf, "dma", 3 )== 0 ) {
    strtok( buf, delim );
    if( (s = strtok( NULL, delim )))
      dma_test( c, *s, repeat );
    else
      dma_test( c, 'C', repeat );

  } else if( strncmp( buf, "status", 6 )== 0 ) {
    bzero(&c->dma_status, sizeof(struct SI_DMA_STATUS));
    ioctl( c->fd, SI_IOCTL_DMA_STATUS, &c->dma_status );
    print_dma_status(c);
  } else if( strncmp( buf, "repeat", 6 )== 0 ) {
    strtok( buf, delim );
    if( (s = strtok( NULL, delim )))
      repeat = atoi(s);
    else
      repeat = 1;
  } else if( strncmp( buf, "verb", 4 )== 0 ) {
    verbose = (verbose+1)%2;
    if( ioctl(c->fd, SI_IOCTL_VERBOSE, &verbose) <0 ) {
      perror("verbose error");
    }
    printf("Verbose set to %s\n", verbose?"ON":"OFF");
  } else if( strncmp( buf, "crash", 5 )== 0 ) {
    crash(c);
  } else if( strncmp( buf, "image", 5 )== 0 ) {
    int cmd;
    strtok( buf, delim );
    if( (s = strtok( NULL, delim ) ))
       cmd = *s;
     else
       cmd = 'D';
    camera_image( c, cmd );
  } else if( strncmp( buf, "change_readout", 14 )== 0 ) {
    io_readout(c);
  } else if( strncmp( buf, "change_config", 13 )== 0 ) {
    io_config(c);
  } else if( strncmp( buf, "setfile_readout", 5 )== 0 ) {
    strtok( buf, delim );
    if( !(s = strtok( NULL, delim ) ))
      s = "800-299x1.set";
    setfile_readout( c,  s );
    send_readout( c );
    send_command(c->fd, 'H');     // H    - load readout
    receive_n_ints( c->fd, 32, (int *)&c->readout );
    print_readout( c );
    expect_y( c->fd );
  } else if( strncmp( buf, "send_readout", 5 )== 0 ) {
    send_readout( c );
  } else if( strncmp( buf, "quit", 4 )== 0 ) {
    exit(0);
  } else if( strncmp( buf, "vmatest", 7 )== 0 ) {
    vmatest( c );
  } else if( strncmp( buf, "timeout_test", 12 )== 0 ) {
    timeout_test( c );
  } else {
    print_help();
  }
}

/*
  F    - Send Readout Parameters
           send 32 unsigned 32 bit values following
           return Y/N byte

  G    - Send One Readout Parameters
           send one 32 bit offset
           send one 32 bit value
           returns Y/N byte


  J    - Send Configuration Parameters
           send 32 unsigned 32 bit values following
           returns Y/N byte

  K    - Send One Configuration Parameters
           send one 32 bit offset
           send one 32 bit value
           returns Y/N byte


  D    - Image Expose request regular image with shuttered exposure
           wants DMA active

  E    - Image Read, requests a dark image with closed shutter
           wants DMA active

  Z    - Image Tdi, requests a tdi image
           wants DMA active


  P    - Eeprom read, causes the EEPROM in the camera to be 
         read into the camera's configuration buffer.

  M    - Eeprom write, causes the camera's configuration buffer 
         to be copied to the camera's EEPROM.


*/

/*
  C    - Image Test, requests a test image
           wants DMA active

   first test of dma for new driver 
*/

dma_test( c, cmd, repeat )
struct SI_CAMERA *c;
int cmd;
int repeat;
{
  int i, last, nbufs, loop, ret, rnd;
  unsigned char *data1, *data2;
  fd_set wait;
  int sel;
  unsigned short test_buffer[1000];
  data1 = ( unsigned char *)malloc(8000000);
  data2 = ( unsigned char *)malloc(8000000);

  if( cmd != 'C' && cmd != 'D' && cmd != 'E' && cmd != 'Z' ) {
    printf("dma test needs C, D, E, Z type ? for help\n");
    return;
  }
  printf("starting DMA test with %c loops %d\n", cmd, repeat );

  for( loop = 0; loop < repeat; loop++ ) {
    if( !c->dma_active ) {
      c->dma_config.maxever = 16*2048*4111;
      c->dma_config.total = 4000000*2;
      c->dma_config.buflen = 1024*1024; /* power of 2 makes it easy to mmap */
      c->dma_config.timeout = 5000;
//      c->dma_config.config = SI_DMA_CONFIG_WAKEUP_EACH;
      c->dma_config.config = SI_DMA_CONFIG_WAKEUP_ONEND;

      if( ioctl( c->fd, SI_IOCTL_DMA_INIT, &c->dma_config )<0 ){
        perror("dma init");
        return;
    }
    nbufs = c->dma_config.total / c->dma_config.buflen ;
    if( c->dma_config.total % c->dma_config.buflen )
      nbufs += 1;
      c->ptr = (unsigned short *)mmap( 0, c->dma_config.maxever, PROT_READ, MAP_SHARED, c->fd, 0);
      printf("c->ptr: %p\n",c->ptr);
      if(!c->ptr) {
        perror("mmap");
        return;
      }
      for (i=0; i<1000; i++) test_buffer[i] = c->ptr[i];
	  
      //print_mem_changes( c->ptr, 256 );
      //print_mem_changes( c->ptr, c->dma_config.total/sizeof(short) );
      print_mem_changes( c->ptr, c->dma_config.maxever/sizeof(short) );

      c->dma_active = 1;
    }


  if( ioctl( c->fd, SI_IOCTL_DMA_START, &c->dma_status )<0 ){
    perror("dma start");
    return;
  }

  bzero(&c->dma_status, sizeof(struct SI_DMA_STATUS));
  send_command_yn(c->fd, cmd );

  /* random sleep to test driver */ 
  
  rnd = (int)(500000.0*(double)rand()/(double)RAND_MAX);
  usleep( rnd );

/* wait for DMA done */

/* test out driver poll function */

  FD_ZERO( &wait );
  FD_SET( c->fd, &wait );
  sel = select( c->fd+1, &wait, NULL, NULL, NULL );
  printf("wake up from select %d\n", sel );

  ret = ioctl( c->fd, SI_IOCTL_DMA_NEXT, &c->dma_status );
  printf("ioctl SI_IOCTL_DMA_NEXT ret: %d\n",ret);
  
  if( ret < 0 ) {
    perror("ioctl dma_next");
  }
  printf("dma_status.transferred: %d dma_config.total: %d\n");
  if( c->dma_status.transferred != c->dma_config.total )
   printf("NEXT wakeup xfer %d bytes\n", c->dma_status.transferred );

  print_mem_changes( c->ptr, 256 );

  memcpy( data1, c->ptr, 8000000 );

  if( loop == 0 ) {
    memcpy( data2, c->ptr, 8000000 );
  } else if( memcmp( data1, data2, 8000000 ) != 0 ) {
      printf("memory mismatch %d\n", loop );
  }
  bzero(data1, 8000000);
    

  if( c->dma_status.status & SI_DMA_STATUS_DONE )
    printf("done %d\n", loop );
  else
    printf("timeout %d\n", loop );

  //print_dma_status(c);
  printf("TOTAL %i\n",c->dma_config.total);
  //print_data_len( c->ptr, c->dma_config.buflen, c->dma_config.total );
  write_dma_data( c->ptr, c->dma_config.total );
  //print_mem_changes( c->ptr, c->dma_config.total/sizeof(short) );
  //  dma_unmap(c);
  }
  free(data1);
  free(data2);
}

/* look at data and guess how much data was transferred */

write_dma_data( ptr, total )
unsigned char *ptr;
int total;
{
  FILE *fd;
  int tmm;
  char buf[256];

  time(&tmm);
  sprintf( buf, "dma_%d.cam", tmm );

  printf("WRITING DATA %s\n",buf);
  if( !(fd = fopen( buf, "w+" ))) {
    printf("cant write dma.cam\n");
    return;
  }
  fwrite(ptr, total, 1, fd ); 
  fclose(fd);
  printf("wrote %d bytes to %s\n", total, buf );
}

print_data_len( ptr, buflen, total )
unsigned char *ptr;
int total;
{
  int nbufs, i, j, ix, ct;
  unsigned char targ;

  nbufs = total/ buflen;
  if( total & buflen ) 
   nbufs += 1;

  for( i=nbufs-1; i>=0; i-- ) {
    targ = (unsigned char)(i+1);
    for( j=buflen-1; j>=0 ; j-- ) {
      ix = i*buflen+j;
      if( ptr[ ix] != targ ) {
        printf( "total data guess %d, or 0x%x\n",  ix, ix );
        for( ct=ix-8; ct<ix+8; ct++ )
          printf( "0x%x 0x%x\n",  ct, ptr[ct] );
        return;
      }
    }
  }
  printf( "total data full %d\n",  total );
}

/*
   each buffer of the mmaped memory is written with the buffer number
   after it is allocated. This helps to tell what got written by the DMA.
   This printout shows that.
*/

print_mem_changes( ptr, nwords )
short *ptr;
int nwords;
{
  int i, cr;
  short last;

  last = 0xcafe;
  cr = 0;
  for( i=0; i<nwords; i++ ) {
    if( ptr[i] != last ) {
      printf("0x%08x:%04x ", i*2, ptr[i]);
      cr++;
      if( cr>0 && (cr % 8) == 0 )
         printf("\n");
      last = ptr[i];
    }
  }
  if( (cr % 8) != 0 )
    printf("\n");

  printf("mem_changes %d nwords\n", nwords );
}


expect_y( fd )
int fd;
{
  if( expect_yn(fd) != 1 )
    printf("ERROR expected yes got no from uart\n");
}


print_readout(c ) 
struct SI_CAMERA *c;
{
  int i;
  struct CFG_ENTRY *cfg;

  printf("Readout Parameters:\n");
  for( i=0; i< SI_READOUT_MAX; i++ ) {
    cfg = c->e_readout[i];
    if( cfg && cfg->name )
       print_cfg( cfg, c->readout[i] );
  }
}


print_config(c)
struct SI_CAMERA *c;
{
  int i;
  struct CFG_ENTRY *cfg;

  printf("Configuration Parameters:\n");
  for( i=0; i< SI_CONFIG_MAX; i++ ) {
     cfg = c->e_config[i];
     if( cfg && cfg->name )
       print_cfg( cfg, c->config[i] );
  }
}

print_status(c)
struct SI_CAMERA *c;
{
  struct CFG_ENTRY *cfg;
  int i;

  printf("Camera Status Words:\n" );
  for( i=0; i< SI_STATUS_MAX ; i++ ) {
    if( !(cfg = c->e_status[i]))
      break;
    print_cfg( cfg, c->status[i] ); 
  }
}


void print_cfg( cfg, val )
struct CFG_ENTRY *cfg;
int val;
{
  int i, ix;
  unsigned int mask;
  double value;
  char *units;

    switch( cfg->type ) {
      case CFG_TYPE_NOTUSED:
        break;
      case CFG_TYPE_INPUTD:
        if( cfg->u.iobox.units )
          units = cfg->u.iobox.units;
        else
          units = "";
        value = cfg->u.iobox.mult * val + cfg->u.iobox.offset;
        printf("%s %f %s val %d\n", cfg->name, value, units, val );
        break;
      case CFG_TYPE_DROPDI:
        ix = val-cfg->u.dropi.min;
        if( ix < cfg->u.dropi.min || ix > cfg->u.dropi.max )
          printf("%s val %d\n", cfg->name, val );
        else
          printf("%s %s val %d\n", cfg->name, cfg->u.dropi.list[ix], val );
        break;
      case CFG_TYPE_DROPDP:
        ix = val-cfg->u.dropp.val;
        if( ix < cfg->u.dropp.val || ix > cfg->u.dropp.val )
          printf("%s val %d\n", cfg->name, val );
        else
          printf("%s %s val %d\n", cfg->name, cfg->u.dropp.list[ix], val );
        break;
      case CFG_TYPE_BITF:
        mask = cfg->u.bitf.mask&val;
        ix = 0;
        while( !(mask & 1)) {
          ix++;
          mask = (mask>>1);
        }
        printf("%s %s val %d\n", cfg->name, cfg->u.bitf.list[ix], val );
        break;
    }
}

print_dma_status(c)
struct SI_CAMERA *c;
{
  printf("dma status\n");
  printf("transferred %d\n", c->dma_status.transferred );
  printf("status 0x%x\n",    c->dma_status.status ); 
  printf("cur %d\n",         c->dma_status.cur );
  printf("next %d\n",        c->dma_status.next );
}

stop_dma( c )
struct SI_CAMERA *c;
{
  
  if( ioctl( c->fd, SI_IOCTL_DMA_ABORT, &c->dma_status )<0 ) {
    perror("dma abort");
  }
}

dma_unmap( c )
struct SI_CAMERA *c;
{
  int nbufs;

  if( !c->dma_active )
    return;

  nbufs = c->dma_config.total / c->dma_config.buflen ;
  if( c->dma_config.total % c->dma_config.buflen )
    nbufs += 1;

  if(munmap( c->ptr, c->dma_config.buflen*nbufs ) )
    perror("failed to unmap");

  c->dma_active = 0;
}

dma_mmap( c ) 
struct SI_CAMERA *c;
{
  int nbufs;

  nbufs = c->dma_config.total / c->dma_config.buflen ;
  if( c->dma_config.total % c->dma_config.buflen )
    nbufs += 1;
  if(!(c->ptr = (unsigned short *)mmap( 0, 
       c->dma_config.maxever, 
       PROT_READ, MAP_SHARED, c->fd, 0))) {
      perror("mmap");
    } else
    c->dma_active = 1;
}

/* try to crash driver by failing to unmap */

crash(c)
struct SI_CAMERA *c;
{
  int repeat, ret;
  printf("starting crash\n");

  stop_dma( c );
  dma_unmap( c );
  repeat = 50000;
  while( repeat-- >0 ) {
    c->dma_config.maxever = 16*2048*4111;
    c->dma_config.total = 4096*4096*2;
    c->dma_config.buflen = 1024*1024; /* power of 2 makes it easy to mmap */
    c->dma_config.timeout = 5000;
//    c->dma_config.config = SI_DMA_CONFIG_WAKEUP_EACH;
    c->dma_config.config = SI_DMA_CONFIG_WAKEUP_ONEND;

    if( ioctl( c->fd, SI_IOCTL_DMA_INIT, &c->dma_config )<0 ){
      perror("dma init");
      continue;
    }
    dma_mmap(c);
    if( ioctl( c->fd, SI_IOCTL_DMA_START, &c->dma_status )<0 ){
      perror("dma start");
      continue;
    }
    send_command_yn(c->fd, 'C' );

    if( (ret = ioctl( c->fd, SI_IOCTL_DMA_NEXT, &c->dma_status ) )<0 )
      perror("dma next");

    print_mem_changes( c->ptr, c->dma_config.total/sizeof(short) );
    printf("trasferred %d total %d\n", 
      c->dma_status.transferred, c->dma_config.total );
    stop_dma( c );
    dma_unmap( c );

    c->dma_config.maxever = 16*2048*4111;
    c->dma_config.total = 4096*2048*2;
    c->dma_config.buflen = 1024*1024; /* power of 2 makes it easy to mmap */
    c->dma_config.timeout = 1000;
//    c->dma_config.config = SI_DMA_CONFIG_WAKEUP_EACH;
    c->dma_config.config = SI_DMA_CONFIG_WAKEUP_ONEND;

    if( ioctl( c->fd, SI_IOCTL_DMA_INIT, &c->dma_config )<0 ){
      perror("dma init");
      continue;
    }
    dma_mmap(c);

    if( ioctl( c->fd, SI_IOCTL_DMA_START, &c->dma_status )<0 ){
      perror("dma start");
      continue;
    }
    send_command_yn(c->fd, 'C' );
    if( (ret = ioctl( c->fd, SI_IOCTL_DMA_NEXT, &c->dma_status ) )<0 )
      perror("dma next");

    print_mem_changes( c->ptr, c->dma_config.total/sizeof(short) );
    printf("trasferred %d total %d\n", 
      c->dma_status.transferred, c->dma_config.total );
    stop_dma( c );
    dma_unmap( c );
  }
  printf("didnt crash\n");
}

int config_data[] = {
  850, //InstrumentModel
  100, //InstrumentSerialNumber
  0,   //HardwareRevision 
  0,   //SerialRegisterPhasing 
  1,   //SerialRegisterSplit 
  2056,//SerialRegisterSize 
  0,   //ParallelRegisterPhasing 
  1,   //ParallelRegisterSplit 
  2056,//ParallelRegisterSize 
  20,  //ParallelShiftDelay 
  3,   //NumberOfPorts 
  20,  //ShutterCloseDelay 
  0,   //CCDTemperatureOffset 
  0,   //BackplateTemperatureOffset 
  1732,//CCDTemperatureSetpoint 
  0,   //DataU16Size 
  0,   //MPPModeDisable 
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
};

set_config( c )
struct SI_CAMERA *c;
{
  printf("sending default configuration parameters\n");
  memcpy( (int *)&c->config,  config_data, sizeof(int)*32 );
  send_command(c->fd, 'J'); // J    - Send Configuration Parameters
  send_n_ints( c->fd, 32, (int *)&c->config );
  expect_y( c->fd );
}

print_help() 
{
  printf("Comands\n" );
  printf(" A        -  Open Shutter returns Y/N\n");
  printf(" B        -  Close Shutter returns Y/N\n");
  printf(" I        -  Print camera status\n");
  printf(" H        -  Print readout parameters\n");
  printf(" L        -  Print camera config parameters\n");
  printf(" J        -  Send default camera config parameters\n");
  printf(" S        -  Cooler On\n");
  printf(" T        -  Cooler Off\n");
  printf(" O        -  Abort Readout\n");
  printf(" status   -  Print DMA status\n");
  printf(" sendfile - initialization function. This loads the file \n");
  printf("            2769d.bin into camera eeprom\n");
  printf("dma  'cmd'- configure dma for 8 million bytes, start the dma\n");
  printf("            send the 'cmd' command to start a test download,\n");
  printf("            wait for completion\n");
  printf("            cmd can be C test image\n");
  printf("                       D regular image with shuttered exposure\n");
  printf("                       E dark image with closed shutter\n");
  printf("                       Z tdi image\n");
  printf("stopdma  - issue stopdma for testing\n");
  printf("repeat   - number of times to do dma\n");
  printf("verbose  - toggle verbose\n");
  printf("change_readout  - changed readout params\n");
  printf("change_config  - changed config params\n");
  printf("image [cmd]  - take image based on readout params\n");
  printf("setfile_readout 'file'\n" );
  printf("send_readout\n");
}

camera_image( c, cmd )
struct SI_CAMERA *c;
int cmd;
{
  int i, last, nbufs, ret;
  unsigned short *flip;
  fd_set wait;
  int sel, tot, serlen, parlen;

  if( cmd != 'C' && cmd != 'D' && cmd != 'E' && cmd != 'Z' ) {
    printf("camera_image needs C, D, E, Z type ? for help\n");
    return;
  }
  printf("starting camera_image %c\n", cmd );

  if( c->ptr )
    dma_unmap(c);

  serlen = c->readout[READOUT_SERLEN_IX];
  parlen = c->readout[READOUT_PARLEN_IX];
  printf("SERLEN %i\n",serlen); 
  printf("PARLEN %i\n",parlen); 

  //tot = serlen*parlen*2*4; /* 2 bytes per short, 4 quadrants */
  tot = serlen*parlen*2*2; /* 2 bytes per short, 2 readout?? */
  printf("TOT %i\n",tot);

  //c->dma_config.maxever = 16*2048*4111;
  c->dma_config.maxever = 16*2048*4111;
  c->dma_config.total = tot;
  c->dma_config.buflen = 1024*1024; /* power of 2 makes it easy to mmap */
  c->dma_config.timeout = 50000;
//  c->dma_config.config = SI_DMA_CONFIG_WAKEUP_EACH;
  c->dma_config.config = SI_DMA_CONFIG_WAKEUP_ONEND;

  if( ioctl( c->fd, SI_IOCTL_DMA_INIT, &c->dma_config )<0 ){
    perror("dma init");
    return;
  }
  nbufs = c->dma_config.total / c->dma_config.buflen ;
  if( c->dma_config.total % c->dma_config.buflen )
    nbufs += 1;
  c->ptr = (unsigned short *)mmap( 0, c->dma_config.maxever, PROT_READ, MAP_SHARED, c->fd, 0);
  if(!c->ptr) {
        perror("mmap");
        return;
  }
//      print_mem_changes( c->ptr, c->dma_config.total/sizeof(short) );
  printf("c->ptr: %p\n",c->ptr);
  c->dma_active = 1;

  if( ioctl( c->fd, SI_IOCTL_DMA_START, &c->dma_status )<0 ){
    perror("dma start");
    return;
  }

  bzero(&c->dma_status, sizeof(struct SI_DMA_STATUS));
  send_command_yn(c->fd, cmd );

/* wait for DMA done */

  printf("sent command %c, waiting\n", cmd );
  ret = ioctl( c->fd, SI_IOCTL_DMA_NEXT, &c->dma_status );

  if( ret < 0 ) {
    perror("ioctl dma_next");
  }
  if( c->dma_status.transferred != c->dma_config.total )
   printf("NEXT wakeup xfer %d bytes\n", c->dma_status.transferred );

  if( c->dma_status.status & SI_DMA_STATUS_DONE )
    printf("done\n" );
  else
    printf("timeout\n" );

  write_dma_data(c->ptr, tot);
#ifdef NOT
  flip = (unsigned short *)malloc(4096*4096*2);
  bzero( flip, 4096*4096*2 );

  if( serlen < 2047 ) {
    //camera_demux_gen( flip, c->ptr, 2048, serlen, parlen );
    write_dma_data( flip, 2048*2048*2 );
  } else {
    //camera_demux_gen( flip, c->ptr, 4096, serlen, parlen );
    write_dma_data( flip, 4096*4096*2 );
  }
  free(flip);
#endif
}

io_readout(c ) 
struct SI_CAMERA *c;
{
  int i;
  struct CFG_ENTRY *cfg;

  printf("Changing Readout Parameters:\n");
  for( i=0; i< SI_READOUT_MAX; i++ ) {
    cfg = c->e_readout[i];
    if( cfg && cfg->name ) {
       change_cfg(c, cfg, c->readout[i], 'G' );
     }
  }
}

io_config(c ) 
struct SI_CAMERA *c;
{
  int i;
  struct CFG_ENTRY *cfg;

  printf("Changing Configuration Parameters:\n");
  for( i=0; i< SI_READOUT_MAX; i++ ) {
    cfg = c->e_config[i];
    if( cfg && cfg->name ) {
       change_cfg(c, cfg, c->config[i], 'K' );
     }
  }
}


int change_cfg( c, cfg, value, cmd )
struct SI_CAMERA *c;
struct CFG_ENTRY *cfg;
int value;
int cmd;
{
  char buf[256];
  int val, ex;
  int data[2];

  print_cfg( cfg, value );

  fprintf(stdout, "enter new val (ret to ignore): ");
  fflush(stdout);
  fgets( buf, 256, stdin);

  if( buf[0] == '\n' || buf[0] == '\r' )
    return value;

  val = atoi( buf );
  if( val == value ) {
    printf("no change, skipping\n");
    return value;
  }

  data[0] = cfg->index;
  data[1] = val;
  send_command(c->fd, cmd );
  send_n_ints( c->fd, 2, data );
  ex = expect_yn( c->fd );
}



send_readout( c )
struct SI_CAMERA *c;
{
  send_command(c->fd, 'F'); // F    - Send Readout Parameters
  send_n_ints( c->fd, 32, (int *)&c->readout );
  expect_yn( c->fd );
}

vmatest(c)
struct SI_CAMERA *c;
{
  int nbufs, count;
  unsigned short *ptr;

  count = 1000;

  while(count-- > 0) {
    //c->dma_config.maxever = 16*2048*4111;
    c->dma_config.maxever = 16*2048*4111;
    c->dma_config.total = ((count%2)?2:1)* 4000000*2;
    c->dma_config.buflen = 1024*1024; /* power of 2 makes it easy to mmap */
    c->dma_config.config = SI_DMA_CONFIG_WAKEUP_ONEND;
    if( ioctl( c->fd, SI_IOCTL_DMA_INIT, &c->dma_config )<0 ){
      perror("dma init");
      return;
    }
    nbufs = c->dma_config.total / c->dma_config.buflen ;
    if( c->dma_config.total % c->dma_config.buflen )
      nbufs += 1;


    if( !c->ptr ) {
      if(!(c->ptr = (unsigned short *)mmap( 0, c->dma_config.maxever, 
        PROT_READ, MAP_PRIVATE, c->fd, 0))) {
          perror("mmap");
            return;
          }
    }
    print_mem_changes( c->ptr, c->dma_config.total/sizeof(short) );

    if(fork()==0) { /* child */
      sleep(2);
      exit(1);
    }
    sleep(1);
  }
}

timeout_test(c)
struct SI_CAMERA *c;
{
  int nbufs, count, serlen, parlen, tot, ret, repeat;
  unsigned short *ptr;

  repeat = 100;

  serlen = c->readout[READOUT_SERLEN_IX];
  parlen = c->readout[READOUT_PARLEN_IX];

  tot = serlen*parlen*2*2; /* 2 bytes per short, 4 quadrants */
  c->dma_config.maxever = 4096*4096*2;
  c->dma_config.total = tot;

  c->dma_config.buflen = 1024*1024; /* power of 2 makes it easy to mmap */
  c->dma_config.config = SI_DMA_CONFIG_WAKEUP_ONEND;
  c->dma_config.timeout = 10000;

  if( ioctl( c->fd, SI_IOCTL_DMA_INIT, &c->dma_config )<0 ){
    perror("dma init");
    return;
  }
  nbufs = c->dma_config.total / c->dma_config.buflen ;
  if( c->dma_config.total % c->dma_config.buflen )
    nbufs += 1;


  if( !c->ptr ) {
    if(!(c->ptr = (unsigned short *)mmap( 0, c->dma_config.maxever, 
      PROT_READ, MAP_PRIVATE, c->fd, 0))) {
        perror("mmap");
          return;
        }
  }
  print_mem_changes( c->ptr, c->dma_config.total/sizeof(short) );

  while(repeat-- > 0) {
    bzero(&c->dma_status, sizeof(struct SI_DMA_STATUS));
    if( ioctl( c->fd, SI_IOCTL_DMA_START, &c->dma_status )<0 ){
      perror("dma start");
      continue;
    }

    send_command_yn(c->fd, 'D' );


    count = 0;
    do {
      ret = ioctl( c->fd, SI_IOCTL_DMA_NEXT, &c->dma_status );
      printf(" wake ret:%d stat 0x%x xfer %d tot %d\n", 
        ret, c->dma_status.status, 
        c->dma_status.transferred, c->dma_config.total);


      if( !c->dma_status.status &&
           (c->dma_status.transferred ==  c->dma_config.total))
      if( count++ > 7 )
        printf("yup\n");
    } while( ret != 0);
    printf(" done %d\n", count );
    sleep(1);
  }

}




