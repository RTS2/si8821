#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* demultiplex 4 quadrant camera data */
/*

  data gets emptied like this:

   1-----<-------   2----->-------
   |            |   |            |
   |     ^      |   |     ^      |
   |     |      |   |     |      |
   |            |   |            |
   |------------|   |------------|

   3-------------   4-------------
   |            |   |            |
   |     |      |   |     |      |
   |     v      |   |     v      |
   |            |   |            |
   |-----<------|   |----->------|


Whole image is size by size or 4096x4096

*/

camera_demux_old( out, in, size )
unsigned short *out;
unsigned short *in;
int size;
{
  int so2, tot, row, col, irow, icol, i;

  so2 = size/2;
  tot = size*size; /* number of elements in data */

  row = 0;
  col = 0;
  for( i=0; i<tot; i+=4 ) {

    irow = row - so2;
    icol = col - so2;
    out[ row*size + col ]             = in[i]; 
//    out[ row*size + icol+so2 ]        = in[i+1];
   
//    out[ (irow+so2)*size + col ]      = in[i+2];
//    out[ (irow+so2)*size + icol+so2 ] = in[i+3];

    if( ++col >= so2 ) {
      col = 0;
      row++;
    }

  }
}

camera_demux( out, in, size )
unsigned short *out;
unsigned short *in;
int size;
{
  int so2, tot, row, col, irow, icol, i;

  so2 = size/2;
  tot = size*size; /* number of elements in data */

  row = 0;
  col = 0;
  tot = 0;
  for( row = 0; row < 2046; row++ ) {
    for( col = 0; col < 2047; col++ ) {
      out[ (row+2)*4096 + (col+1) ]  = in[tot]; 

      icol = (2047 - (col+1)) + 2048 ;
      irow = (2046 - (row+1)) + 2048;
      out[ (row+2)*4096 + icol ]  = in[tot+1]; 
      out[ irow*4096 + col+1 ]  = in[tot+2]; 
      out[ irow*4096 + icol ]  = in[tot+3]; 

      tot+=4;
    }
  }
}

/* try to make a demux that works on both configurations */

/* size 4096 serlen 2047 parlen 2046 */

/* size 2048 serlen 1023 parlen 1023 */

camera_demux_gen( out, in, size, serlen, parlen )
unsigned short *out;
unsigned short *in;
int size;
{
  int so2, tot, row, col, irow, icol, i;

  so2 = size/2;
  tot = size*size; /* number of elements in data */

  row = 0;
  col = 0;
  tot = 0;
  for( row = 0; row < parlen; row++ ) {
    for( col = 0; col < serlen; col++ ) {
      out[ (row+2)*size + (col+1) ]  = in[tot]; 

      icol = (serlen - (col+1)) + so2 ;
      irow = (parlen - (row+1)) + so2;
      out[ (row+2)*size + icol ]  = in[tot+1]; 
      out[ irow*size + col+1 ]  = in[tot+2]; 
      out[ irow*size + icol ]  = in[tot+3]; 

      tot+=4;
    }
  }
}

