/* 

Library support code for the
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


#define CHUNK 100         //Not sure how big this should be, but 100 seems safe
#define ABUF_SIZE 10000

int sendfile( int fd, int, char *);
void  init_com( int, int, int, int, int, int  );
void  send_break( int, int );
struct CFG_ENTRY *find_readout( struct SI_CAMERA *, char *);

/* send a file to the UART */

int sendfile( fd, breaktime, filename )
int fd;
int breaktime;
char *filename;
{
  FILE  *dbgptr;
  FILE  *inpointer;
  char  *abuffer;
  char  *bbuffer;
  int  i, j, k, chunk, rxcnt, n;
  div_t  d;
  int tries;

  chunk = CHUNK;
  bbuffer = (char *)malloc( chunk );
  abuffer = (char *)malloc( ABUF_SIZE );
  tries = 5;

  dbgptr = fopen("DbgOut.txt","w");
  fprintf(dbgptr, "Test __DATE__\n");
  fflush(dbgptr);
  fprintf(dbgptr, "FileName: %s breaktime: %d\n",filename, breaktime);
  fflush(dbgptr);
  
  if (fd < 0 ) // do nothing if device not open 
    return(-1);
  
  fprintf(dbgptr, "bIsrOk\n");
  fflush(dbgptr);
  
  if(!(inpointer = fopen(filename,"rb"))) {

    fprintf(dbgptr, "inpointer: %x\n", inpointer);
    fflush(dbgptr);
  
    return(-1);
  } 

  fprintf(dbgptr, "File opened \n");
  fflush(dbgptr);
  fprintf(dbgptr, "abuffer: %x  bbuffer: %x \n", abuffer, bbuffer);
  fflush(dbgptr);
    
  i = 0;
  rewind(inpointer);
  while(!feof(inpointer))
  {
    if (i<ABUF_SIZE)
      abuffer[i++] = (unsigned char )fgetc(inpointer);
    
  }
  i--;  //decrement to get rid of eof character
  fclose(inpointer);
  
  fprintf(dbgptr, "File read %d bytes \n", i);
  fflush(dbgptr);

  //Send data in "chunks"
  d = div(i,chunk);

  do {
    k = 0;
    j = 0;

    send_break (fd, breaktime);
    usleep(50000);
    send_break (fd, breaktime);
    usleep(50000);
    clear_buffer(fd);
    usleep(50000);

    for(i=0;i<chunk;i++)
      bbuffer[i] = abuffer[i+(j*chunk)];
    
    if( write( fd, bbuffer, chunk ) != chunk ) { //send and wait 
      perror("UART write\n");
      fclose(inpointer);
      fclose(dbgptr);
      return -1;
    }

    usleep(20000); //This doesn't need to be so big, but what the hell...
  //If NO data was received, probably the serial cable isn't connected

    if( ioctl(fd, SI_IOCTL_SERIAL_IN_STATUS, &rxcnt) <0 ) {
      perror( "serial in status\n");
      fclose(inpointer);
      fclose(dbgptr);
      return -1;
    }

    if(!rxcnt)  {
      fclose(inpointer);
      fclose(dbgptr);
      return(2);
    }

    if(( n = read( fd, bbuffer, chunk )) != chunk ) {
      if( n < 0 )
        perror( "readback error\n");
    }

    for(i=0;i<chunk;i++) {
      if(bbuffer[i] != abuffer[i+(j*chunk)]) {
        k++;
        break;
      }
    }
    usleep (10000);

  // Now that the first buffer went, skip the
  // no-response check for the rest of the data
    for(j=1;j<d.quot;j++) {
      for(i=0;i<chunk;i++) {
        bbuffer[i] = abuffer[i+(j*chunk)];
      }
      if( write( fd, bbuffer, chunk ) != chunk ) { //send and wait 
        perror("UART write\n");
        break;
      }

      usleep(20000); //This doesn't need to be so big, but what the hell...

      if(( n = read( fd, bbuffer, chunk )) < 0 ) {
        if( n < 0 )
          perror( "readback error\n");
        break;
      }

      for(i=0;i<chunk;i++) {
        if(bbuffer[i] != abuffer[i+(j*chunk)]) {
          k++;
          break;
        }
      }
    }

    //Send the remainder of the data
    for(i=0;i<d.rem;i++) 
      bbuffer[i] = abuffer[i+(j*chunk)];

    if( write( fd, bbuffer, d.rem ) != d.rem ) { //send and wait 
      perror("UART write\n");
      break;
    }

    //Get and compare the remainder of the data
    usleep (20000);

    if(( n = read( fd, bbuffer, d.rem )) != d.rem ) {
      perror( "readback error\n");
      break;
    }

    for(i=0;i<d.rem;i++) {
      if(bbuffer[i] != abuffer[i+(j*chunk)]) {
        k++;
        break;
      }
    }
  }  while (tries-- && k);

  free(abuffer);
  free(bbuffer);

  fclose(dbgptr);
  fclose(inpointer);

//  printf ("Returning with %d as retries\n", tries);

  if(!tries) 
    return(3); //If there are data errors, baud rate might be wrong
  else 
    return(0); //It worked!
}   

void  init_com( fd, baud, parity, bits, stopbits, buffersize )
int fd, baud, parity, bits, stopbits, buffersize;
{
  struct SI_SERIAL_PARAM serial;

  bzero( &serial, sizeof(struct SI_SERIAL_PARAM));

  serial.baud = baud;
  serial.buffersize = buffersize;
  serial.fifotrigger = 8;
  serial.bits = bits;
  serial.stopbits = stopbits;
  serial.parity = parity;

  serial.flags = SI_SERIAL_FLAGS_BLOCK;
  serial.timeout = 1000;
  
  if( ioctl(fd, SI_IOCTL_SET_SERIAL, &serial))
    perror("init_comm");
  
  return;
}


int send_command( fd, cmd )
int fd;
int cmd;
{
  int  ret;

  if( fd < 0 )
    return 0;

  clear_buffer(fd);    
  ret = send_char(fd, cmd);

  return (ret);
}

int clear_buffer(fd) 
{
  if( ioctl( fd, SI_IOCTL_SERIAL_CLEAR, 0 ))
    return -1;
  else
    return 0;
}

int send_char(fd, data) 
int fd;
int data;
{
  unsigned char wbyte, rbyte;
  int n;

  if( fd < 0 )
    return 0;

  wbyte = ( unsigned char) data;
  if( write( fd,  &wbyte, 1 ) != 1 ) {
    perror("write");
    return -1;
  }

  if( (n = read( fd,  &rbyte, 1 )) != 1 ) {
    if( n < 0 )
      perror("read");
    return -1;
  }

  if( wbyte != rbyte )
    return -1;
  else
    return 0;
}

int receive_char(fd) 
int fd;
{
  unsigned char rbyte;
  int n;

  if( fd < 0 )
    return 0;

  if( (n = read( fd,  &rbyte, 1 )) != 1 ) {
    if( n<0)
      perror("read");
    return -1;
  } else
    return (int)rbyte;
}

/* read n bigendian integers and return it */

int receive_n_ints( fd, n, data )
int fd;
int n;
int *data;
{
  int len, i;
  int ret;

  len = n * sizeof(int);

  if( fd < 0 ) {
    bzero( data, len );
    return 0;
  }

  if( (ret = read( fd, data, len )) != len )
    return -1;

  for( i=0; i<n; i++ )
    swapl(&data[i]);
  return 0;
}

int send_n_ints( fd, n, data )
int fd;
int n;
int *data;
{
  int len, i;
  int *d;

  len = n * sizeof(int);
  d = (int *)malloc( len );
  memcpy( d, data, len );
  for( i=0; i<n; i++ )
    swapl(&d[i]);

  if( (i = write( fd, d, len )) != len )
    return -1;

//  memset( d, 0, len );
//  if( receive_n_ints( fd, len, d ) < 0 )
//    return -1;

//  if( memcmp( d, data, len ) != 0 )
//    return -1;
  free(d);

  return len;
}

/* swap 4 byte integer */

int swapl( d )
int *d;
{
  union {
    int i;
    unsigned char c[4];
  }u;
  unsigned char tmp;

  u.i = *d;
  tmp = u.c[0];
  u.c[0] = u.c[3];
  u.c[3] = tmp;

  tmp = u.c[1];
  u.c[1] = u.c[2];
  u.c[2] = tmp;

  *d = u.i;
}


int expect_yn( fd )
int fd;
{
  int got;

  if( (got = receive_char(fd)) < 0 )
   return -1;

  if( got != 'Y' && got != 'N')
   return -1;

  return (got == 'Y');
}


void  send_break(fd, ms)
int fd;
int ms;
{
  if( ioctl(fd, SI_IOCTL_SERIAL_BREAK, &ms))
    perror("send break");
  return;
}

/* parse the cfg file into a SI_CAMERA structure */

int load_camera_cfg( c, fname )
struct SI_CAMERA *c;
char *fname;
{

  load_cfg( c->e_status, fname, "SP" );
  load_cfg( c->e_readout, fname, "ESP" );
  load_cfg( c->e_readout, fname, "RFP" );
  load_cfg( c->e_config, fname, "CP" );

  return 0;

}

//AI:20150221 This is the old cfg file. Not longer valid
/*
[Status]
SP0="1,2,CCD Temperature,0,4095,°C,0.1,-273.15,1"
SP1="1,2,Backplate Temperature,0,4095,°C,0.1,-273.15,1"
SP2="Not Used"
*/
//AI:20150221



int load_cfg( e, fname, var )
struct CFG_ENTRY **e;
char *fname;
char *var;
{
  int len, varlen, index, pindex;
  FILE *fd;
  char buf[256];
  struct CFG_ENTRY *entry;

  varlen = strlen(var);

  if( !(fd = fopen(fname,"r")) ) {
    printf("unable to open %s\n", fname );
    return -1;
  }

  pindex = 0;
  while( fgets( buf, 256, fd )) {
    if( strncmp( buf, var, varlen ) == 0 ) {
       index = atoi( &buf[varlen] );
       len = strlen(buf);
       len -=1;
       buf[len] = 0;
       entry = (struct CFG_ENTRY *)malloc( sizeof(struct CFG_ENTRY ));
       bzero( entry, sizeof(struct CFG_ENTRY ));
       entry->index = index;
       entry->cfg_string = (char *)malloc( len );
       strcpy( entry->cfg_string, buf );
       parse_cfg_string( entry );
       if( index!=pindex || index > 32 ) {
         printf("CFG error\n");
         index = pindex;
       }
      
       e[pindex] = entry; /* warning here no bounds check */
       pindex++;
       if( pindex >= 32 )
         break;
    }
  }

  fclose(fd);
}

/* fill in all the parameters from the cfg_string */

parse_cfg_string( entry )
struct CFG_ENTRY *entry;
{
  char *delim = "=\",\n\r";
  char *s;
  char buf[512];
  int i, tot, min, max;
  unsigned int mask;

  strcpy( buf, entry->cfg_string );
  if( !(s = strtok( buf, delim ))) 
    return -1;

  if( !(s = strtok( NULL, delim ))) 
    return -1;



  int oritype = atoi(s);

  if (oritype == 1 || oritype == 2 || oritype == 3 || oritype == 4 || oritype == 5
      || oritype == 6 || oritype == 7 || oritype == 11 || oritype == 12 
      || oritype == 13 || oritype == 14)
    oritype = 1;
  else if (oritype == 8)
    oritype = 8;
  else if (oritype == 9)
    oritype = 9;
  else if (oritype == 10)
    oritype = 3;
  else if (oritype == 0)
    oritype = 0;
  else 
    {
      printf("CFG type error\n");
      exit(1);
    }
  
  entry->type = oritype;

  if( entry->type == CFG_TYPE_NOTUSED )
    return 0;

  /* AI:20152021 Security val does not exist any longer
  if( !(s = strtok( NULL, delim ))) 
    return -1;
  entry->security = atoi(s);
  */

  if( !(s = strtok( NULL, delim ))) 
    return -1;

  entry->name = malloc(strlen(s)+1);
  strcpy( entry->name, s );
  printf("READING entry->NAME %s\n",entry->name);
  
  printf("READING ENTRY %i\n",entry->type);

  switch( entry->type ) {
    case CFG_TYPE_INPUTD:
      if( !(s = strtok( NULL, delim ))) 
        return -1;
      entry->u.iobox.min = atoi(s);
      if( !(s = strtok( NULL, delim ))) 
        return -1;
      entry->u.iobox.max = atoi(s);

      if( !(s = strtok( NULL, delim ))) 
        return -1;
      entry->u.iobox.units = malloc(strlen(s)+1);
      strcpy( entry->u.iobox.units, s );

      if( !(s = strtok( NULL, delim )))  {
        entry->u.iobox.mult = 1.0;
        return -1;
      }
      entry->u.iobox.mult = atof(s);

      if( !(s = strtok( NULL, delim ))) 
        return -1;
      entry->u.iobox.offset = atof(s);

      if( !(s = strtok( NULL, delim ))) 
        return -1;
      entry->u.iobox.status = atoi(s);

      break;
    case CFG_TYPE_DROPDI:
      if( !(s = strtok( NULL, delim ))) 
        return -1;
      tot = atoi(s);
      entry->u.dropi.min = 0;

      entry->u.dropi.max = tot-1;
      if( tot <= 0 )
        return -1;
      entry->u.dropi.list = (char **)malloc( sizeof(char *)*tot);
      bzero( entry->u.dropi.list, sizeof(char *)*tot);
      for( i=0; i<tot; i++ ){
        if( !(s = strtok( NULL, delim ))) 
          return -1;
	printf("DROP %s\n",s);
        entry->u.dropi.list[i] = (char *)malloc( strlen(s)+1);
        strcpy( entry->u.dropi.list[i], s );
      }

      break;
    case CFG_TYPE_DROPDP:
      if( !(s = strtok( NULL, delim ))) 
        return -1;
      tot = atoi(s);

      entry->u.dropp.list = (char **)malloc( sizeof(char *)*tot);
      bzero( entry->u.dropp.list, sizeof(char *)*tot);
      for( i=0; i<tot; i++ ){
        if( !(s = strtok( NULL, delim ))) 
          return -1;
	entry->u.dropp.val = atoi(s);
	printf("DROPP %s\n",s);
        if( !(s = strtok( NULL, delim ))) 
          return -1;
        entry->u.dropp.list[i] = (char *)malloc( strlen(s)+1);
	printf("DROPP STRING %s\n",s);
        strcpy( entry->u.dropp.list[i], s );
      }

      break;
    case CFG_TYPE_BITF:
      if( !(s = strtok( NULL, delim ))) 
        return -1;
      mask = entry->u.bitf.mask = atoi(s);
      tot = 0;
      while( mask &1 ) {
        tot++;
        mask = (mask>>1);
      }
      entry->u.bitf.list = (char **)malloc( sizeof(char *)*tot);
      bzero( entry->u.bitf.list, sizeof(char *)*tot);
      for( i=0; i<tot; i++ ){
        if( !(s = strtok( NULL, delim )))
          return -1;
        entry->u.bitf.list[i] = (char *)malloc( strlen(s)+1);
        strcpy( entry->u.bitf.list[i], s );
      }
      break;
  }
}

/*
  malloc a new name string from the third param 

SP0="1,2,CCD Temperature,0,4095,°C,0.1,-273.15,1"

*/

char *name_cfg(cfg)
char *cfg;
{
  char buf[256];
  char *delim = ",\n";
  char *s, *ret;

  strncpy( buf, cfg, 256 );
  strtok( buf, delim );
  strtok( NULL, delim );
  s = strtok( NULL, delim );

  if( s ) {
    ret = (char *)malloc( strlen(s)+1);
    strcpy( ret, s );
  } else {
   ret = NULL;
  }

  return ret;
}

send_command_yn(fd, data )
int fd;
int data;
{
  int ex;

  send_command(fd, data);

  ex = expect_yn( fd );
  if( ex < 0  )
    printf("error expected Y/N uart\n");
  else if (ex)
    printf("got yes from uart\n");
  else
    printf("got no from uart\n");
}

/* look for readout entries in setfile, load them and set into camera */

setfile_readout( c, file )
struct SI_CAMERA *c;
char *file;
{
  FILE *fd;
  char *delim = "=\n";
  char *s;
  char buf[256];
  struct CFG_ENTRY *cfg;

  if( !(fd = fopen( file, "r" ))) {
    return -1;
  }

  while( fgets( buf, 256, fd )) {
    printf("BUF: %s",buf);
    if( !(cfg = find_readout( c, buf )))
      continue;

    strtok( buf, delim );
    if( s = strtok( NULL, delim ))
      {
	printf("PARAM... %s\n",s);
	c->readout[ cfg->index ] = atoi(s);
      }

  }

  fclose(fd);
}


struct CFG_ENTRY *find_readout( c, name )
struct SI_CAMERA *c;
char *name;
{
  struct CFG_ENTRY *cfg;
  int i;

  for( i=0; i<SI_READOUT_MAX; i++ ) {
    cfg = c->e_readout[i];

    if( cfg && cfg->name ) {
      if( strncasecmp( cfg->name, name, strlen(cfg->name))==0 )
	{
	  printf("PARAMETER FOUND!!!! %s\n",name);
	  return cfg;
	}
    }
  }
  return NULL;
}


sprint_cfg_val_only( buf, cfg, val )
char *buf;
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
          units = NULL;
        if( units == NULL || cfg->u.iobox.mult == 1.0 && cfg->u.iobox.offset == 0.0 ) {
          sprintf(buf, "%d", val);
        } else {
          value = cfg->u.iobox.mult * val + cfg->u.iobox.offset;
          sprintf(buf, "%6.2f", value);
        }
        break;
      case CFG_TYPE_DROPDI:
        ix = val-cfg->u.dropi.min;
        if( ix < cfg->u.dropi.min || ix > cfg->u.dropi.max )
          sprintf(buf, "%d", val );
        else
          sprintf(buf, "%s", cfg->u.dropi.list[ix] );
        break;
      case CFG_TYPE_DROPDP:
        ix = val-cfg->u.dropp.val;
        if( ix < cfg->u.dropp.val || ix > cfg->u.dropp.val )
          sprintf(buf, "%d", val );
        else
          sprintf(buf, "%s", cfg->u.dropp.list[ix] );
        break;
      case CFG_TYPE_BITF:
        mask = cfg->u.bitf.mask&val;
        ix = 0;
        while( !(mask & 1)) {
          ix++;
          mask = (mask>>1);
        }
        sprintf(buf, "%s", cfg->u.bitf.list[ix] );
        break;
    }
}
