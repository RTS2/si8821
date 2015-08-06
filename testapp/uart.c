/* 

UART control code for
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

/* gtk UART control app */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <gtk/gtk.h>
#include <time.h>
#include <sys/mman.h>

#include "si8821.h"
#include "si_app.h"

#define BOX_PACK 0
#define FRAME_SPACE 3


struct UART_CMD {
  struct SI_CAMERA *head;
  int cmd;
  char *label;
  int resp;
  GtkWidget *resp_w;
};


struct COMMAND_DAT {
  struct SI_CAMERA *head;
  int load;
  int send;
  int len;
  struct CFG_ENTRY **dat;
  int *data;
} cmd_dat[3] = {
 {  NULL, 'H', 'F', SI_READOUT_MAX, NULL, NULL },
 {  NULL, 'L', 'J', SI_CONFIG_MAX,  NULL, NULL },
 {  NULL, 'I', -1,  SI_STATUS_MAX,  NULL, NULL },
};


int build_param_list( struct COMMAND_DAT *, struct CFG_ENTRY **, GtkWidget *, int);
int do_contin( struct SI_CAMERA *, GtkWidget *);
int do_verbose( struct SI_CAMERA *, GtkWidget *);
int do_setcmd( struct SI_CAMERA *, GtkWidget *);

static void pwindow_dest( GtkWidget *widget, gpointer   data )
{
  struct SI_CAMERA *head;

  head = (struct SI_CAMERA *)data;
  head->param_window = NULL;
}

static void cwindow_dest( GtkWidget *widget, gpointer   data )
{
  struct SI_CAMERA *head;

  head = (struct SI_CAMERA *)data;
  head->control_window = NULL;
}

void config_dma( GtkWidget *widget, gpointer   data )
{
  struct SI_CAMERA *head;
  const char *s;
  int combo, nbufs;

  head = (struct SI_CAMERA *)data;
  printf("config dma\n");

  if( head->ptr ) {
    nbufs = head->dma_config.total / head->dma_config.buflen ;
    if( head->dma_config.total % head->dma_config.buflen )
      nbufs += 1;
    if(munmap( head->ptr, head->dma_config.buflen*nbufs ) )
      perror("failed to unmap");
  }

  if(  !head->total_e ) {
    int tot, serlen, parlen;

    serlen = head->readout[READOUT_SERLEN_IX];
    parlen = head->readout[READOUT_PARLEN_IX];
    tot = serlen*parlen*2*4; /* 2 bytes per short, 4 quadrants */
    head->dma_config.maxever = 4096*4096*2;
    head->dma_config.total = tot;
    head->dma_config.config = SI_DMA_CONFIG_WAKEUP_EACH;
    head->dma_config.buflen = 1024*1024;
    head->dma_config.timeout = 10000;
    head->contin = 1;
    head->command = 'D';
    
  } else {
    bzero( &head->dma_config, sizeof(struct SI_DMA_CONFIG ));

    if( (s = gtk_entry_get_text(GTK_ENTRY(head->total_e))))
      head->dma_config.total = atoi(s);
    printf("total %d\n", head->dma_config.total );

    if( (s = gtk_entry_get_text(GTK_ENTRY(head->buflen_e))))
      head->dma_config.buflen = atoi(s);
    printf("buflen %d\n", head->dma_config.buflen );

    if( (s = gtk_entry_get_text(GTK_ENTRY(head->timeout_e))))
      head->dma_config.timeout = atoi(s);
    printf("timeout %d\n", head->dma_config.timeout );

    combo = gtk_combo_box_get_active((GtkComboBox *)head->config_c);
    if( combo )
      head->dma_config.config = SI_DMA_CONFIG_WAKEUP_EACH;
    else
      head->dma_config.config = SI_DMA_CONFIG_WAKEUP_ONEND;

    printf("config %d 0x%x\n", combo, head->dma_config.config );
  }

  if( ioctl( head->fd, SI_IOCTL_DMA_INIT, &head->dma_config )<0 ){
    perror("dma init");
    return;
  }

  nbufs = head->dma_config.total / head->dma_config.buflen ;
  if( head->dma_config.total % head->dma_config.buflen )
    nbufs += 1;

  if(!(head->ptr = (unsigned short *)mmap( 0, 
    head->dma_config.buflen*nbufs, 
      PROT_READ, MAP_SHARED, head->fd, 0))) {
    perror("mmap");
    return;
  }
  head->dma_configed = 1;
}

static void config_image( GtkWidget *widget, gpointer data )
{
  struct SI_CAMERA *head;
  const char *s;
  int combo;
  GdkPixbuf *pix;

  head = (struct SI_CAMERA *)data;

  printf("config image\n");
  bzero( &head->dma_config, sizeof(struct SI_DMA_CONFIG ));

  if( (s = gtk_entry_get_text(GTK_ENTRY(head->itype_e))))
    head->dinter.interlace_type = atoi(s);
  printf("itype %d\n", head->dinter.interlace_type );

  if( (s = gtk_entry_get_text(GTK_ENTRY(head->irows_e))))
    head->dinter.n_rows = atoi(s);
  printf("rows %d\n", head->dinter.n_rows );

  if( (s = gtk_entry_get_text(GTK_ENTRY(head->icols_e))))
    head->dinter.n_cols = atoi(s);
  printf("cols %d\n", head->dinter.n_cols );

  pix = gdk_pixbuf_new( GDK_COLORSPACE_RGB, 0, 8, 
    head->dinter.n_rows, head->dinter.n_cols);
  if( head->pix )
     g_object_unref( head->pix );
  head->pix = pix;
}

static void param_load( widget, cmd )
GtkWidget *widget;
struct COMMAND_DAT *cmd;
{
  unsigned char reply;
  int *ret;
  struct CFG_ENTRY **ent;
  struct CFG_ENTRY *cfg;
  char text[80];
  struct SI_CAMERA *h;
  int len, i;

  printf("param_load cmd %c\n",  cmd->load );
  h = cmd->head;

  if( send_command( h->fd, cmd->load ) < 0 ) {
    perror("send_command");
    return;
  }

  len = cmd->len;

  receive_n_ints( h->fd, len, cmd->data );
  if( !expect_yn( h->fd ) )
    printf("didnt get a y from uart\n");

  ent = cmd->dat;
  if( !ent ) 
    return;

  ret = cmd->data;
  for( i=0; i< len ; i++ ) {
     cfg = ent[i];
     if( cfg && cfg->name && cfg->widget ) {
      sprint_cfg_val_only( text, cfg, *ret );
      gtk_entry_set_text(GTK_ENTRY(cfg->widget),text);
    }
    ret++;
  }
}

static void param_send( widget, cmd )
GtkWidget *widget;
struct COMMAND_DAT *cmd;
{
  unsigned char reply;
  int *ret, i, len;
  char text[80];
  const char *s;
  struct SI_CAMERA *h;
  struct CFG_ENTRY **ent;
  struct CFG_ENTRY *cfg;

  printf("param_send cmd %c\n",  cmd->send );
  h = cmd->head;

  if( send_command( h->fd, cmd->load ) < 0 ) {
    perror("send_command");
    return;
  }

  len = cmd->len;
  ent = cmd->dat;
  if( !ent ) 
    return;

  ret = cmd->data;
  for( i=0; i< len ; i++ ) {
    cfg = ent[i];
    if( cfg && cfg->name && cfg->widget ) {
      if( s = gtk_entry_get_text(GTK_ENTRY(cfg->widget)))
        *ret = atoi(s);
    }
    ret++;
  }

  if( send_n_ints( h->fd, cmd->len, cmd->data ) < 0 ) {
    perror("send_command");
    return;
  }
}

static void send_control( widget, cmd )
GtkWidget *widget;
struct UART_CMD *cmd;
{
  int fd, yn;
  char text[256];
 
  fd = cmd->head->fd;
  if( send_command( fd, cmd->cmd ) < 0 ) {
    perror("send_command");
    return;
  }
  if( cmd->resp ) {
    yn = expect_yn( fd );
   
    if( yn == 1 ) 
      strcpy(text, "Y" );
    else if ( yn == 0 )
      strcpy(text, "N" );
    else
      sprintf(text, "error %d", yn );

    gtk_entry_set_text(GTK_ENTRY(cmd->resp_w),text);
  }
}

int show_param( head )
struct SI_CAMERA *head;
{
  int i;
  GtkWidget *window, *vbox, *frame, *hbox;

  if( head->param_window ) {
    gtk_window_activate_focus( GTK_WINDOW(head->param_window) );
    return 0;
  }
  
  for( i=0;i<3; i++ )
    cmd_dat[i].head = head;

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  head->param_window = window;

  gtk_widget_set_name (window, "Spectral Instruments si8821 UART Control");
  gtk_container_set_border_width (GTK_CONTAINER (window),FRAME_SPACE);
  gtk_widget_show (window);
  g_signal_connect (G_OBJECT (window), "destroy",
		    G_CALLBACK (pwindow_dest), head);

  hbox = gtk_hbox_new(FALSE,0);
  gtk_container_add (GTK_CONTAINER (window), hbox);
  gtk_widget_show (hbox);

  frame = gtk_frame_new("Readout Parameters");  
  gtk_container_set_border_width (GTK_CONTAINER (frame),FRAME_SPACE);
  gtk_box_pack_start (GTK_BOX (hbox), frame , FALSE, FALSE,BOX_PACK);
  gtk_widget_show (frame);


  vbox = gtk_vbox_new(FALSE,0);
  gtk_container_set_border_width(GTK_CONTAINER(vbox),FRAME_SPACE);
  gtk_container_add (GTK_CONTAINER (frame), vbox);

  build_param_list( &cmd_dat[0], head->e_readout, vbox, 1  );
  gtk_widget_show (vbox);

  frame = gtk_frame_new("Configuration Parameters");  
  gtk_container_set_border_width (GTK_CONTAINER (frame),FRAME_SPACE);
  gtk_box_pack_start (GTK_BOX (hbox), frame , FALSE, FALSE,BOX_PACK);
  gtk_widget_show (frame);


  vbox = gtk_vbox_new(FALSE,0);
  gtk_container_set_border_width(GTK_CONTAINER(vbox),FRAME_SPACE);
  gtk_container_add (GTK_CONTAINER (frame), vbox);

  build_param_list( &cmd_dat[1], head->e_config, vbox, 0  );
  gtk_widget_show (vbox);

  frame = gtk_frame_new("Status Parameters");  
  gtk_container_set_border_width (GTK_CONTAINER (frame),FRAME_SPACE);
  gtk_box_pack_start (GTK_BOX (hbox), frame , FALSE, FALSE,BOX_PACK);
  gtk_widget_show (frame);

// vbox, table on top, buttons on bottom

  vbox = gtk_vbox_new(FALSE,0);
  gtk_container_set_border_width(GTK_CONTAINER(vbox),FRAME_SPACE);
  gtk_container_add (GTK_CONTAINER (frame), vbox);

  build_param_list( &cmd_dat[2], head->e_status, vbox,0  );
  gtk_widget_show (vbox);

}

build_param_list( cmd, e, vbox, set  )
struct COMMAND_DAT *cmd;
struct CFG_ENTRY **e;
GtkWidget *vbox;
{
  GtkWidget *label, *frame, *hbox, *but;
  int row, col, i;
  struct CFG_ENTRY *cfg;
  char text[80], *units;

  cmd->dat = e;
  hbox = gtk_hbox_new(FALSE,0);

  but = gtk_button_new_with_label( "Load" );
  g_signal_connect (G_OBJECT (but), "clicked", G_CALLBACK (param_load), cmd );
  gtk_box_pack_start (GTK_BOX (hbox), but , FALSE, FALSE,BOX_PACK);
  gtk_widget_show (but);

  if( set )
    readout_set_files(cmd->head, hbox );

  if( cmd->send != -1 )  {
    but = gtk_button_new_with_label( "Send" );
    g_signal_connect (G_OBJECT (but), "clicked", G_CALLBACK (param_send), cmd );
    gtk_box_pack_end (GTK_BOX (hbox), but , FALSE, FALSE,BOX_PACK);
    gtk_widget_show (but);
  }

  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE,BOX_PACK);
  gtk_widget_show (hbox);

  for( i=0; i< cmd->len ; i++ ) {
     cfg = e[i];
     if( cfg && cfg->name ) {
       hbox = gtk_hbox_new(FALSE,0);

       if( cfg->type == CFG_TYPE_INPUTD && cfg->u.iobox.units )
         sprintf( text, "%s (%s)", cfg->name, cfg->u.iobox.units );
       else
         sprintf( text, "%s", cfg->name );

       label = gtk_label_new(text);
       gtk_box_pack_start (GTK_BOX (hbox), label , FALSE, FALSE,BOX_PACK);
       gtk_widget_show (label);

       cfg->widget =gtk_entry_new();
       gtk_box_pack_end (GTK_BOX (hbox), cfg->widget , FALSE, FALSE,BOX_PACK);
       gtk_widget_set_size_request(cfg->widget,60,-1);

       gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE,BOX_PACK);
       if( cmd->data ) {
         sprint_cfg_val_only( text, cfg, cmd->data[i] );
         gtk_entry_set_text(GTK_ENTRY(cfg->widget),text);
       }
       gtk_widget_show (cfg->widget);
       gtk_widget_show (hbox);
    }
  }
}


struct UART_CMD commands[] = {
  { NULL, 'A',   "Open Shutter ",  1, NULL },
  { NULL, 'B',   "Close Shutter",  1, NULL },
  { NULL, 'S',   "Cooler On",      0, NULL },
  { NULL, 'T',   "Cooler Off",     0, NULL },
  { NULL, 'P',   "Eeprom read",    0, NULL },
  { NULL, 'M',   "Eeprom write",   0, NULL },
  { NULL, 0,     NULL, 0 }
};

show_control( head )
struct SI_CAMERA *head;
{
  int i;
  GtkWidget *window, *vbox, *frame, *hbox, *but, *label, *bvbox;
  struct UART_CMD *c;
  char buf[80];

  if( head->control_window ) {
    gtk_window_activate_focus( GTK_WINDOW(head->control_window) );
    return 0;
  }
  
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  head->control_window = window;

  gtk_widget_set_name (window, "Spectral Instruments si8821 UART Controls");
  gtk_container_set_border_width (GTK_CONTAINER (window),FRAME_SPACE);
  gtk_widget_show (window);
  g_signal_connect (G_OBJECT (window), "destroy",
		    G_CALLBACK (cwindow_dest), head);

  bvbox = gtk_vbox_new(FALSE,0);
  gtk_container_add (GTK_CONTAINER (window), bvbox);
  gtk_widget_show (bvbox);

  frame = gtk_frame_new("Controls");  
  gtk_container_set_border_width (GTK_CONTAINER (frame),FRAME_SPACE);
  gtk_box_pack_start (GTK_BOX (bvbox), frame , TRUE, TRUE, 0);
  gtk_widget_show (frame);

  vbox = gtk_vbox_new(FALSE,0);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);


  for( c = &commands[0];c->label; c++ ) {
    c->head = head;
    hbox = gtk_hbox_new(FALSE,0);
    but = gtk_button_new_with_label( c->label );
    g_signal_connect (G_OBJECT (but), "clicked", G_CALLBACK (send_control), c );
    gtk_box_pack_start (GTK_BOX (hbox), but , FALSE, FALSE,BOX_PACK);
    gtk_widget_show (but);

    if( c->resp ) {

      c->resp_w =gtk_entry_new();
      gtk_box_pack_end (GTK_BOX (hbox), c->resp_w , FALSE, FALSE,BOX_PACK);
      gtk_widget_set_size_request(c->resp_w,60,-1);
      gtk_widget_show (c->resp_w);
    }

    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE,BOX_PACK);
    gtk_widget_show (hbox);
  }

  frame = gtk_frame_new("DMA Parameters");  
  gtk_container_set_border_width (GTK_CONTAINER (frame),FRAME_SPACE);
  gtk_box_pack_start (GTK_BOX (bvbox), frame , FALSE, FALSE, 0);
  gtk_widget_show (frame);

  vbox = gtk_vbox_new(FALSE,0);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  but = gtk_combo_box_new_text();
  head->setcmd_c = but;
  gtk_box_pack_end (GTK_BOX (vbox), but, FALSE, FALSE, 0);
  gtk_combo_box_append_text( (GtkComboBox *)but, "Image Command 'D'" );
  gtk_combo_box_append_text( (GtkComboBox *)but, "Dark Command 'E'" );
  gtk_combo_box_append_text( (GtkComboBox *)but, "Test Command 'C'" );
  gtk_combo_box_append_text( (GtkComboBox *)but, "TDI Command 'Z'" );
  gtk_combo_box_set_active ((GtkComboBox *)but, do_getcmd(head->command) );
  g_signal_connect_swapped (G_OBJECT (but), "changed",
                            G_CALLBACK (do_setcmd), head);
  gtk_box_pack_end (GTK_BOX (vbox), but, FALSE, FALSE, 0);
  gtk_widget_show (but);

  hbox = gtk_hbox_new(FALSE,0);
  gtk_widget_show (hbox);
  gtk_box_pack_start(GTK_BOX (vbox), hbox , FALSE, FALSE,BOX_PACK);

  label = gtk_label_new(" total ");
  gtk_box_pack_start(GTK_BOX (hbox), label , FALSE, FALSE,BOX_PACK);
  gtk_widget_show (label);
  head->total_e =gtk_entry_new();
  gtk_box_pack_end (GTK_BOX (hbox), head->total_e , FALSE, FALSE,BOX_PACK);
  sprintf( buf, "%d", head->dma_config.total );
  gtk_entry_set_text(GTK_ENTRY(head->total_e),buf);
  gtk_widget_set_size_request(head->total_e,70,-1);
  gtk_widget_show (head->total_e);

  hbox = gtk_hbox_new(FALSE,0);
  gtk_widget_show (hbox);
  gtk_box_pack_start(GTK_BOX (vbox), hbox , FALSE, FALSE,BOX_PACK);

  label = gtk_label_new(" buflen (bytes)");
  gtk_box_pack_start(GTK_BOX (hbox), label , FALSE, FALSE,BOX_PACK);
  gtk_widget_show (label);
  head->buflen_e =gtk_entry_new();
  sprintf( buf, "%d", head->dma_config.buflen );
  gtk_entry_set_text(GTK_ENTRY(head->buflen_e),buf);
  gtk_box_pack_end (GTK_BOX (hbox), head->buflen_e , FALSE, FALSE,BOX_PACK);
  gtk_widget_set_size_request(head->buflen_e,60,-1);
  gtk_widget_show (head->buflen_e);
  
  hbox = gtk_hbox_new(FALSE,0);
  gtk_widget_show (hbox);
  gtk_box_pack_start(GTK_BOX (vbox), hbox , FALSE, FALSE,BOX_PACK);

  label = gtk_label_new(" timeout (jiffies) ");
  gtk_box_pack_start(GTK_BOX (hbox), label , FALSE, FALSE,BOX_PACK);
  gtk_widget_show (label);
  head->timeout_e =gtk_entry_new();
  sprintf( buf, "%d", head->dma_config.timeout );
  gtk_entry_set_text(GTK_ENTRY(head->timeout_e),buf);
  gtk_box_pack_end (GTK_BOX (hbox), head->timeout_e , FALSE, FALSE,BOX_PACK);
  gtk_widget_set_size_request(head->timeout_e,60,-1);
  gtk_widget_show (head->timeout_e);

  
  hbox = gtk_hbox_new(FALSE,0);
  gtk_widget_show (hbox);
  gtk_box_pack_start(GTK_BOX (vbox), hbox , FALSE, FALSE,BOX_PACK);

  label = gtk_label_new(" Wakeup  ");
  gtk_box_pack_start(GTK_BOX (hbox), label , FALSE, FALSE,BOX_PACK);
  gtk_widget_show (label);

  but = gtk_combo_box_new_text();
  head->config_c = but;
  gtk_box_pack_end (GTK_BOX (hbox), but, FALSE, FALSE, 0);

  gtk_combo_box_append_text( (GtkComboBox *)but, "End of DMA" );
  gtk_combo_box_append_text( (GtkComboBox *)but, "Each nbuf" );
  gtk_combo_box_set_active ((GtkComboBox *)but, 
    (head->dma_config.config & SI_DMA_CONFIG_WAKEUP_EACH)!=0 );
  gtk_widget_show (but);


  but = gtk_button_new_with_label( "Configure DMA" );
  g_signal_connect (G_OBJECT (but), "clicked", G_CALLBACK (config_dma), head );
  gtk_box_pack_start (GTK_BOX (vbox), but , FALSE, FALSE,BOX_PACK);
  gtk_widget_show (but);

  but = gtk_combo_box_new_text();
  head->contin_c = but;
  gtk_box_pack_end (GTK_BOX (vbox), but, FALSE, FALSE, 0);
  gtk_combo_box_append_text( (GtkComboBox *)but, "continuous readout" );
  gtk_combo_box_append_text( (GtkComboBox *)but, "read one image" );
  gtk_combo_box_set_active ((GtkComboBox *)but, 0 );
  g_signal_connect_swapped (G_OBJECT (but), "changed",
                            G_CALLBACK (do_contin), head);

  gtk_widget_show (but);

  but = gtk_combo_box_new_text();
  head->verbose_c = but;
  gtk_box_pack_end (GTK_BOX (vbox), but, FALSE, FALSE, 0);
  gtk_combo_box_append_text( (GtkComboBox *)but, "Verbose dma" );
  gtk_combo_box_append_text( (GtkComboBox *)but, "Verbose uart" );
  gtk_combo_box_append_text( (GtkComboBox *)but, "Verbose off" );
  gtk_combo_box_set_active ((GtkComboBox *)but, 0 );
  g_signal_connect_swapped (G_OBJECT (but), "changed",
                            G_CALLBACK (do_verbose), head);
  gtk_widget_show (but);

//  frame = gtk_frame_new("Image Parameters");  
//  gtk_container_set_border_width (GTK_CONTAINER (frame),FRAME_SPACE);
//  gtk_box_pack_start (GTK_BOX (bvbox), frame , FALSE, FALSE, 0);
//  gtk_widget_show (frame);

//  int interlace_type;  /* kind of image */
//  int n_cols;          /* input cols */
//  int n_rows;          /* input rows */
//  int n_ptr_pos;       /* output words transferred */
//  vbox = gtk_vbox_new(FALSE,0);
//  gtk_container_add (GTK_CONTAINER (frame), vbox);
//  gtk_widget_show (vbox);

//  hbox = gtk_hbox_new(FALSE,0);
//  gtk_widget_show (hbox);
//  gtk_box_pack_start(GTK_BOX (vbox), hbox , FALSE, FALSE,BOX_PACK);
//  
//
//  label = gtk_label_new("Interlace Type");
//  gtk_box_pack_start(GTK_BOX (hbox), label , FALSE, FALSE,BOX_PACK);
//  gtk_widget_show (label);
//  head->itype_e =gtk_entry_new();
//  gtk_box_pack_end (GTK_BOX (hbox), head->itype_e , FALSE, FALSE,BOX_PACK);
//  gtk_widget_set_size_request(head->itype_e,60,-1);
//  gtk_widget_show (head->itype_e);

//  hbox = gtk_hbox_new(FALSE,0);
//  gtk_widget_show (hbox);
//  gtk_box_pack_start(GTK_BOX (vbox), hbox , FALSE, FALSE,BOX_PACK);

//  label = gtk_label_new(" Image Rows ");
//  gtk_box_pack_start(GTK_BOX (hbox), label , FALSE, FALSE,BOX_PACK);
//  gtk_widget_show (label);
//  head->irows_e =gtk_entry_new();
//  gtk_box_pack_end (GTK_BOX (hbox), head->irows_e , FALSE, FALSE,BOX_PACK);
//  gtk_widget_set_size_request(head->irows_e,60,-1);
//  gtk_widget_show (head->irows_e);
  
//  hbox = gtk_hbox_new(FALSE,0);
//  gtk_widget_show (hbox);
//  gtk_box_pack_start(GTK_BOX (vbox), hbox , FALSE, FALSE,BOX_PACK);

//  label = gtk_label_new(" Image Columns ");
//  gtk_box_pack_start(GTK_BOX (hbox), label , FALSE, FALSE,BOX_PACK);
//  gtk_widget_show (label);
//  head->icols_e =gtk_entry_new();
//  gtk_box_pack_end (GTK_BOX (hbox), head->icols_e, FALSE, FALSE,BOX_PACK);
//  gtk_widget_set_size_request(head->icols_e,60,-1);
//  gtk_widget_show (head->icols_e);
//
  
////  but = gtk_button_new_with_label( "Apply to Image" );
//  g_signal_connect (G_OBJECT (but), "clicked", G_CALLBACK (config_image), head );
//  gtk_box_pack_start (GTK_BOX (vbox), but , FALSE, FALSE,BOX_PACK);
//  gtk_widget_show (but);

}


void param_load_all( head )
struct SI_CAMERA *head;
{
  int i;

  for( i=0; i<3; i++ ) {
    param_load( NULL, &cmd_dat[i] );
  }
}

void setup_cmd_dat( head )
struct SI_CAMERA *head;
{
  int i;
  struct COMMAND_DAT *cmd;

  for( i=0; i<3; i++ ) {
    cmd = &cmd_dat[i];
    cmd->head = head;
  }

  cmd_dat[0].data = &head->readout[0];
  cmd_dat[1].data = &head->config[0];
  cmd_dat[2].data = &head->status[0];

}


int do_contin( head, widget )
struct SI_CAMERA *head;
GtkWidget *widget;
{
  head->contin = gtk_combo_box_get_active((GtkComboBox *)head->contin_c)==0;
  return 0;
}

int do_verbose( head, widget )
struct SI_CAMERA *head;
GtkWidget *widget;
{
  int verb;

  switch( gtk_combo_box_get_active((GtkComboBox *)head->verbose_c) ) {
    case 0:
    default:
      verb = SI_VERBOSE_DMA;
      break;
    case 1:
      verb = SI_VERBOSE_SERIAL;
      break;
    case 2:
      verb = 0;
      break;
  }
  if( head->fd >= 0 )
    if( ioctl( head->fd, SI_IOCTL_VERBOSE, &verb ) <0 )
      perror("verbose");

  return 0;
}

int do_setcmd( head, widget )
struct SI_CAMERA *head;
GtkWidget *widget;
{

  switch( gtk_combo_box_get_active((GtkComboBox *)head->setcmd_c) ) {
    case 0:
    default:
      head->command = 'D';
      break;
    case 1:
      head->command = 'E';
      break;
    case 2:
      head->command = 'C';
      break;
    case 3:
      head->command = 'Z';
      break;
  }
  return 0;
}

do_getcmd( command )
int command;
{
  switch( command ) {
    case 'D':
      return 0;
    case 'E':
      return 1;
    case 'C':
      return 2;
    case 'Z':
      return 3;
  }
}



readout_set_files( head, hbox )
struct SI_CAMERA *head;
GtkWidget *hbox;
{
  GtkWidget *but;
  int readout_file( GtkWidget *widget, void *dp);

  but = gtk_button_new_with_label( "Load File" );
  g_signal_connect (G_OBJECT (but), "clicked", G_CALLBACK (readout_file), head );
  gtk_box_pack_start (GTK_BOX (hbox), but , FALSE, FALSE,BOX_PACK);
  gtk_widget_show (but);
}

int readout_file( widget, dp )
GtkWidget *widget;
void *dp;
{
  struct SI_CAMERA *head;
  GtkWidget *dialog;

  head = (struct SI_CAMERA *)dp;

  dialog = gtk_file_chooser_dialog_new ("Load Readout File", NULL,
    GTK_FILE_CHOOSER_ACTION_OPEN,
    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
    GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
    char *filename;
    unsigned short *data;
    int fd, n;

    filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
    gtk_widget_destroy (dialog);
    printf("opening %s\n", filename );
    setfile_readout( head,  filename ); /* load them into local array */
    send_readout( head );               /* send to camera */
    param_load( NULL, &cmd_dat[0] );    /* readback from camera */
    config_dma( NULL, head);            /* config dma using readback */
    g_free (filename);
  } else {
    gtk_widget_destroy (dialog);
  }
}

send_readout( c )
struct SI_CAMERA *c;
{
  send_command(c->fd, 'F'); // F    - Send Readout Parameters
  send_n_ints( c->fd, 32, (int *)&c->readout );
  expect_yn( c->fd );
}

load_status()
{
  param_load( NULL, &cmd_dat[2] );    /* readback status from camera */
}

