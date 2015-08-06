/* 

app header  for
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

/*
  application specific support for the si8821
  used by test_app,  and gui
*/

#ifndef GDKCONFIG_H
typedef void * GtkWidget;
typedef void * GdkPixbuf;
#endif



#define SI_STATUS_MAX    16
#define SI_CONFIG_MAX    32
#define SI_READOUT_MAX   32
#define SI_READSPEED_MAX  8

#define CFG_TYPE_NOTUSED 0 
#define CFG_TYPE_INPUTD  1 
#define CFG_TYPE_DROPDI   8 
#define CFG_TYPE_DROPDP  9 
#define CFG_TYPE_BITF    3 

#define READOUT_SERLEN_IX  1
#define READOUT_PARLEN_IX  5

struct CFG_ENTRY {
  GtkWidget *widget; // gtk widget, if any
  char *name;
  int index;
  char *cfg_string;
  int type;
  int security;

  union { 

    struct IOBOX {
      int min;
      int max;
      char *units;
      double mult;
      double offset;
      int status;
    }iobox;

    struct DROPDOWN {
      int min;
      int max;
      char **list;
    }dropi;

    struct DROPDOWNP {
      int val;
      char **list;
    }dropp;

    struct BITFIELD {
      unsigned int mask;
      char **list;
    }bitf;
  }u;
};


struct SI_DINTERLACE {
  int interlace_type;  /* kind of image */
  int n_cols;          /* input cols */
  int n_rows;          /* input rows */
  int n_ptr_pos;       /* output words transferred */
};


struct SI_CAMERA {
  int fd;               /* open device */
  int dma_active;
  unsigned short *ptr;  /* mmaped data */
  struct SI_DMA_STATUS dma_status;
  struct SI_DMA_CONFIG dma_config;

  int status[SI_STATUS_MAX]; /* values returned from camera via uart */
  int config[SI_CONFIG_MAX];
  int readout[SI_READOUT_MAX];
  int read_speed[SI_READSPEED_MAX];

  struct CFG_ENTRY *e_status[SI_STATUS_MAX]; /* parsed out cfg file */
  struct CFG_ENTRY *e_config[SI_CONFIG_MAX];
  struct CFG_ENTRY *e_readout[SI_READOUT_MAX];
  struct CFG_ENTRY *e_readspeed[SI_READSPEED_MAX];

  GtkWidget *param_window;
  GtkWidget *control_window;
  GtkWidget *image;
  GdkPixbuf *pix;
  int fill_done; /* initial fill complete */
  GtkWidget *bar; /* dma progress bar */
  double fraction;

  GtkWidget *total_e; /* dma config widgets */
  GtkWidget *buflen_e;
  GtkWidget *timeout_e;
  GtkWidget *config_c;
  struct SI_DINTERLACE dinter;

  GtkWidget *itype_e; /* deinterlace widgets */
  GtkWidget *icols_e;
  GtkWidget *irows_e; 
  int dma_done;
  int dma_done_handle;
  int dma_configed;
  int dma_aborted;

  int command;
  int contin;
  GtkWidget *contin_c; 
  GtkWidget *verbose_c;
  GtkWidget *setcmd_c;
  GtkWidget *file_widget;
  char *fname;
  unsigned short *flip_data;
  pthread_t fill;
  int side;
};

int load_camera_cfg( struct SI_CAMERA *, char *);
int load_cfg( struct CFG_ENTRY **, char *, char *);
char *name_cfg(char *);
int send_command( int, int );
int send_command_yn( int, int );


int sendfile(int fd, int breaktime, char *filename);
void init_com(int fd, int baud, int parity, int bits, int stopbits, int buffersize);
int clear_buffer(int fd);
int send_char(int fd, int data);
int receive_char(int fd);
int receive_n_ints(int fd, int n, int *data);
int send_n_ints(int fd, int n, int *data);
int swapl(int *d);
int expect_yn(int fd);
void send_break(int fd, int ms);
int parse_cfg_string(struct CFG_ENTRY *entry);
int setfile_readout(struct SI_CAMERA *c, char *file);
struct CFG_ENTRY *find_readout(struct SI_CAMERA *c, char *name);

