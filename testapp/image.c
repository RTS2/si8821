/* 

Linux GUI for the
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

/* image display app */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <fcntl.h>
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
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>


#include "si8821.h"
#include "si_app.h"

#define BOX_PACK 0
#define FRAME_SPACE 3

void *fun_fill( void * );
inline unsigned char false_color_red( unsigned short );
inline unsigned char false_color_green( unsigned short );
inline unsigned char false_color_blue( unsigned short );

int fill_pix_with_data( struct SI_CAMERA*, unsigned short *, int);
static void dma_go( struct SI_CAMERA *, int );
void *image_fill( void *);

/*
gboolean timeout( dp )
gpointer *dp;
{
  struct SI_CAMERA *head;

  head = (struct SI_CAMERA *)dp;
  if( head->fill_done ) {
    gtk_image_set_from_pixbuf( GTK_IMAGE(head->image), head->pix );
    return 0;
  } else 
    return 1;
}
*/

gboolean dma_poll( dp )
gpointer *dp;
{
  struct SI_CAMERA *head;

  head = (struct SI_CAMERA *)dp;
//  head->fraction += 0.02;
//  if( head->fraction > 1.0 )
//    head->dma_active = 0;

  if( head->dma_active ) {
    gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(head->bar),head->fraction);
    return 1;
  } else  {
    gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(head->bar), 0.0 );
    gtk_progress_bar_set_text( GTK_PROGRESS_BAR(head->bar),"DMA not active" );
    gtk_image_set_from_pixbuf( GTK_IMAGE(head->image), head->pix );
    return 0;
  }
}

static void destroy( GtkWidget *widget, gpointer   data )
{
  gtk_main_quit ();
}

static void do_abort( GtkWidget *widget, gpointer   data )
{
  struct SI_CAMERA *head;

  head = (struct SI_CAMERA *)data;
  head->dma_active = 0;
  head->fraction = 0.0;
  head->dma_aborted = 1;

  if( ioctl( head->fd, SI_IOCTL_DMA_ABORT, NULL )) 
    perror("dma_abort");

  
}

static void dma_done(a, b, condition)
gpointer a;
gint b;
GdkInputCondition condition;
{
  struct SI_CAMERA *head;
  int contin;

  if( condition != GDK_INPUT_READ )
    return;

  head = (struct SI_CAMERA *)a;

  if( ioctl( head->fd, SI_IOCTL_DMA_NEXT, &head->dma_status )) {
    perror("dma_next");
  }

  if( head->dma_status.status & SI_DMA_STATUS_DONE)  {
    int serlen, parlen;


    gdk_input_remove( head->dma_done_handle );
    head->dma_done_handle = 0;
    head->dma_active = 0;

    if( head->dma_aborted ) {
      gtk_progress_bar_set_text( GTK_PROGRESS_BAR(head->bar), "DMA Aborted" );
      head->dma_aborted = 0;
      return;
    } else {
      gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(head->bar),1.0);
      gtk_progress_bar_set_text( GTK_PROGRESS_BAR(head->bar), "DMA Done" );
   }

    serlen = head->readout[READOUT_SERLEN_IX];
    parlen = head->readout[READOUT_PARLEN_IX];
    printf("starting demux\n");
    if( serlen < 1025 ) {
      head->side = 2048;
    } else {
      head->side = 4096;
    }

    if( head->fill )
      pthread_join( head->fill, NULL ); /* must be done before flip */

    camera_demux_gen( head->flip_data, head->ptr, head->side, serlen, parlen );

    printf("dma_done, transferred %d\n", head->dma_status.transferred );
    pthread_create(&head->fill, NULL, image_fill, head );

    if( head->contin )
       dma_go( head, head->command);

  } else   {
    double frac;
//    printf("dma_wakeup, so far transferred %d\n", head->dma_status.transferred);
    frac = (double)head->dma_status.transferred / 
           (double)head->dma_config.total;
    gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(head->bar),frac);
    gtk_progress_bar_set_text( GTK_PROGRESS_BAR(head->bar), "DMA Active" );
  }
}


void *image_fill( v )
void *v;
{
  struct SI_CAMERA *head;

  printf("start image fill\n");
  head = (struct SI_CAMERA *)v;
 
  scale_data( head->flip_data, head->side );
  fill_pix_with_data( head, head->flip_data, head->side ); 
  gtk_image_set_from_pixbuf( GTK_IMAGE(head->image), head->pix );
  printf("finished image fill\n");
  pthread_exit(NULL);
}

static void dma_go( head, cmd)
struct SI_CAMERA *head;
int cmd;
{

  head->dma_aborted = 0;

  if( head->dma_active ) /* dont start if running */
    return;

  if( !head->dma_configed ) { /* dont start if dma not configed */
    return;
  }

  head->command = cmd;
  head->dma_active = 1;
  head->fraction = 0.05;
  gtk_progress_bar_set_text( GTK_PROGRESS_BAR(head->bar), "DMA active" );
  gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(head->bar),0.05);

  head->dma_done_handle = gdk_input_add( head->fd, GDK_INPUT_READ, 
   (GdkInputFunction)dma_done, head );

  if( ioctl( head->fd, SI_IOCTL_DMA_START, &head->dma_status )<0 ){
    perror("dma start");
    return;
  }
  send_command_yn( head->fd, cmd );

//  g_timeout_add( 200, dma_poll, head );
}

static void do_start( GtkWidget *widget, gpointer   data )
{
  struct SI_CAMERA *head;

  head = (struct SI_CAMERA *)data;
  dma_go( data, head->command );
}

static void do_params( GtkWidget *widget, gpointer   data )
{
  show_param( data );
}

static void do_controls( GtkWidget *widget, gpointer   data )
{
  show_control( data );
}

static void do_load( widget, dp )
GtkWidget *widget;
void *dp;
{
  GtkWidget *dialog;
  int ix;
  char *s;
  char *delim = " \t\n";
  FILE *fd;
  char buf[256];
  struct SI_CAMERA *head;
  struct stat file_stat;

  head = (struct SI_CAMERA *)dp;

  dialog = gtk_file_chooser_dialog_new ("Load Image", NULL,
    GTK_FILE_CHOOSER_ACTION_OPEN,
    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
    GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
    char *filename;
    unsigned short *data;
    int fd, n, side;

    filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
    gtk_widget_destroy (dialog);
    printf("opening %s\n", filename );
    data = ( unsigned short *)malloc( 4096*4096*2);
    
    stat( filename, &file_stat );
    if( file_stat.st_size == 4096*4096*2 )
      side = 4096;
    else if( file_stat.st_size == 2048*2048*2 )
      side = 2048;
    else
      side = 0;

    if( side == 0 || (fd = open( filename, O_RDONLY, 0 ))<0 ) {
      printf("cant open %s\n", filename );
    } else {
      if( (n = read(fd, data, side*side*2))< 0 ){
        printf("cant read %s\n", filename );
      }
      scale_data( data, side ); /* side */
      fill_pix_with_data( head, data, side );
      gtk_image_set_from_pixbuf( GTK_IMAGE(head->image), head->pix );
      printf("done loading %s\n", filename );
    }
 
    free(data);
    close(fd);
    g_free (filename);
  } else {
    gtk_widget_destroy (dialog);
  }
}

/* get the chose filename */

store_filename (GtkWidget *widget, void *dp)
{
  int fd, n;
  struct SI_CAMERA *head;
 
  head = (struct SI_CAMERA *)dp;

  head->fname = (char *)gtk_file_selection_get_filename(
    GTK_FILE_SELECTION(head->file_widget));

  if((fd = open( head->fname, O_RDWR|O_CREAT, 0666))>=0) 
    if( (n = write(fd, head->flip_data, 4096*4096*2 ))<0) {
        printf("failed to write\n");
  } else 
    printf("cant open filename: %s\n", head->fname);

  return 0;
}

static void do_save( widget, dp )
GtkWidget *widget;
void *dp;
{
  GtkWidget *dialog;
  time_t tm;
  FILE *fd;
  int i;
  char *lab;
  struct SI_CAMERA *head;

   /* Create the selector */
   
   head->file_widget = gtk_file_selection_new 
     ("Please select a file for saving.");
   
   g_signal_connect (GTK_FILE_SELECTION (head->file_widget)->ok_button,
                     "clicked", G_CALLBACK (store_filename), head);
   			   
   /* Ensure that the dialog box is destroyed when the user clicks a button. */
   
   g_signal_connect_swapped (GTK_FILE_SELECTION (head->file_widget)->ok_button,
                             "clicked",
                             G_CALLBACK (gtk_widget_destroy), 
                             head->file_widget);

   g_signal_connect_swapped (GTK_FILE_SELECTION (head->file_widget)->cancel_button,
                             "clicked",
                             G_CALLBACK (gtk_widget_destroy),
                             head->file_widget); 
   
   /* Display that dialog */
   
   gtk_widget_show (head->file_widget);
}

int main( int argc, char *argv[] )
{
  int i, fd;
  GtkWidget *window, *vbox, *frame, *hbox, *image, *but;
  GtkWidget *vbox2, *align;
  
  GdkPixbuf *pix, *logo;
  GtkWidget *scroll, *bar;
  struct SI_CAMERA *head;

  head = (struct SI_CAMERA *)malloc(sizeof(struct SI_CAMERA));
  bzero(head, sizeof(struct SI_CAMERA));
  head->flip_data  = ( unsigned short *)malloc( sizeof(short)*4096*4096);

  gtk_init (&argc, &argv);

 // fd = open("/dev/SIPCI_0", O_RDWR, 0 );
  fd = -1;
  head->fd = fd;
  if( fd >= 0 ) {
    load_camera_cfg( head, "Test.cfg" );
    init_com( fd, 57600, 0, 8, 1, 9000 ); /* setup uart for camera */
  }
  
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  g_signal_connect (G_OBJECT (window), "destroy",
		    G_CALLBACK (destroy), NULL);
  gtk_widget_set_name (window, "Spectral Instruments si8821 Image Display Control");
  gtk_container_set_border_width (GTK_CONTAINER (window),FRAME_SPACE);
  gtk_widget_show (window);

  vbox = gtk_vbox_new(FALSE,0);
  gtk_container_add (GTK_CONTAINER (window), vbox);
  gtk_widget_show (vbox);

  scroll = gtk_scrolled_window_new(NULL,NULL);
  gtk_widget_set_size_request (scroll, 400, 400);
  gtk_box_pack_start(GTK_BOX(vbox),scroll,TRUE,TRUE,0);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  pix = gdk_pixbuf_new( GDK_COLORSPACE_RGB, 0, 8, 4096, 4096);
  head->pix = pix;

  image = gtk_image_new_from_pixbuf(pix);
  head->image = image;

  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll),image);
  gtk_widget_show (image);
  gtk_widget_show (scroll);

  logo = gdk_pixbuf_new_from_file( "speclogo.jpg", NULL );
  gtk_image_set_from_pixbuf( GTK_IMAGE(head->image), logo );


  align = gtk_alignment_new( 0, 0, 1, 0 );
  gtk_box_pack_start (GTK_BOX (vbox), align, FALSE, TRUE, 0);
  gtk_widget_show (align);

  vbox2 = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (align), vbox2);
  gtk_widget_show (vbox2);


  bar = gtk_progress_bar_new();
  head->bar = bar;
  gtk_box_pack_start(GTK_BOX(vbox2),bar,TRUE,TRUE,0);
  gtk_progress_bar_set_text( GTK_PROGRESS_BAR(bar), "DMA Not active");
  gtk_widget_show(bar);
  
  hbox = gtk_hbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(vbox2),hbox,TRUE,TRUE,0);
  gtk_widget_show (hbox);

  but = gtk_button_new_with_label( "Start" );
  g_signal_connect (G_OBJECT (but), "clicked", G_CALLBACK (do_start), head );
  gtk_box_pack_start(GTK_BOX(hbox),but,TRUE,TRUE,0);
  gtk_widget_show (but);

  but = gtk_button_new_with_label( "Abort" );
  g_signal_connect (G_OBJECT (but), "clicked", G_CALLBACK (do_abort), head );
  gtk_box_pack_start(GTK_BOX(hbox),but,TRUE,TRUE,0);
  gtk_widget_show (but);

  hbox = gtk_hbox_new(FALSE,0);
  gtk_box_pack_start(GTK_BOX(vbox2),hbox,TRUE,TRUE,0);
  gtk_widget_show (hbox);
  
  but = gtk_button_new_with_label( "Controls" );
  g_signal_connect (G_OBJECT (but), "clicked", G_CALLBACK (do_controls), head );
  gtk_box_pack_start(GTK_BOX(hbox),but,TRUE,TRUE,0);
  gtk_widget_show (but);

  but = gtk_button_new_with_label( "Parameters" );
  g_signal_connect (G_OBJECT (but), "clicked", G_CALLBACK (do_params), head );
  gtk_box_pack_start(GTK_BOX(hbox),but,TRUE,TRUE,0);
  gtk_widget_show (but);

  but = gtk_button_new_with_label( "Load" );
  g_signal_connect (G_OBJECT (but), "clicked", G_CALLBACK (do_load), head );
  gtk_box_pack_start(GTK_BOX(hbox),but,TRUE,TRUE,0);
  gtk_widget_show (but);

  but = gtk_button_new_with_label( "Save" );
  g_signal_connect (G_OBJECT (but), "clicked", G_CALLBACK (do_save), head );
  gtk_box_pack_start(GTK_BOX(hbox),but,TRUE,TRUE,0);
  gtk_widget_show (but);

  setup_cmd_dat( head ); /* setup the parameter structures */

  if( fd >= 0 ) {
    param_load_all( head );
    config_dma( NULL, head);
  }

  gtk_main();
}

/* just to see something pretty */

void *fun_fill( dp )
void *dp;
{
  struct SI_CAMERA *head;
  int stride;
  int width, height, n_channels, row, col;
  guchar *pixels, *p;
  int r;
  GdkPixbuf *pix;

  head = (struct SI_CAMERA *)dp;
  head->dma_active = 1;
  head->fraction = 0.0;
  //sleep(3); /* so its all black at startup */
  pix = head->pix;

  n_channels = gdk_pixbuf_get_n_channels (pix);

  g_assert (gdk_pixbuf_get_colorspace (pix) == GDK_COLORSPACE_RGB);
  g_assert (gdk_pixbuf_get_bits_per_sample (pix) == 8);
//  g_assert (gdk_pixbuf_get_has_alpha (pix));
//  g_assert (n_channels == 4);

  width = gdk_pixbuf_get_width (pix);
  height = gdk_pixbuf_get_height (pix);

  stride = gdk_pixbuf_get_rowstride (pix);
  pixels = gdk_pixbuf_get_pixels (pix);

  for( row=0; row<height; row++ ) {
    for( col=0; col<width; col++ ) {
//      usleep(100);
      head->fraction = (double)(row  * width + col)/(height*width) ;
      r = rand();
      p = pixels + col * stride + row * n_channels;
      p[0] = (((int)(256* (double)col/(double)width )) & 0xff);  /* red */
      p[1] = (((int)(256* (double)row/(double)width )) & 0xff);  /* green */
      p[2] = (int)(128.0*sin(
        (double)col*row/(double)width/(double)height*M_PI*200.0 )+128);  /* blue */
//      p[3] = ((((int)r)    ) & 0xff);  /* alpha */
    }
  }
  head->fraction = (double)1.0;
  head->dma_active = 0;
  return NULL;
}


fill_pix_with_data( head, data, side )
struct SI_CAMERA *head;
unsigned short *data;
int side;
{
  int stride;
  int width, height, n_channels, row, col;
  guchar *pixels, *p;
  int r;
  GdkPixbuf *pix;
  unsigned short dp, max;

  head->dma_active = 1;
  head->fraction = 0.0;
  pix = head->pix;

  n_channels = gdk_pixbuf_get_n_channels (pix);

  g_assert (gdk_pixbuf_get_colorspace (pix) == GDK_COLORSPACE_RGB);
  g_assert (gdk_pixbuf_get_bits_per_sample (pix) == 8);

  width = side;
  height = side;

  stride = gdk_pixbuf_get_rowstride (pix);
  pixels = gdk_pixbuf_get_pixels (pix);

  for( row=0; row<height; row++ ) {
    for( col=0; col<width; col++ ) {
      p = pixels + col * stride + row * n_channels;
      dp = *(data + col*width + row);
      p[0] = false_color_red(dp)   & 0xff;  /* red */
      p[1] = false_color_green(dp) & 0xff;  /* green */
      p[2] = false_color_blue(dp)  & 0xff;  /* blue */
    }
  }

  head->fraction = (double)1.0;
  head->dma_active = 0;

}


inline unsigned char false_color_red( dp )
unsigned short dp;
{
  return dp>>8;
}

inline unsigned char false_color_green( dp )
unsigned short dp;
{
  return dp>>8;
}

inline unsigned char false_color_blue( dp )
unsigned short dp;
{
  return dp>>8;
}

scale_data( data, n )
unsigned short *data;
int n;
{
  int tot, i;
  unsigned short max;

  tot= n*n;

  max = 0;
  for( i=0; i<tot; i++ ) {
    if( max < data[i] )
      max = data[i];
  }
  printf("max %d\n", max );

//  for( i=0; i<tot; i++ ) {
//     data[i] = (unsigned short)((double)data[i] / (double)max * 65535.0);
//  }
}
