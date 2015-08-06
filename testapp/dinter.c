/* 

Dinterlace code for the
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
#include <gtk/gtk.h>

#include "si8821.h"
#include "si_app.h"

//int bSave = 0;
//int dma_mode = 0;
//int dma_abort = 0;
//char *szFileName = NULL;
//char *szFitsHeader = NULL;
//unsigned short *dmabuf = NULL;
//unsigned short *lpImage = NULL;

//  write data to file code ( flip and unflip )
//  unsigned char  nTempPix1;
//  unsigned char  nTempPix2;
//  unsigned char *  lpbImage;
//    if (len_words)                    // and we have pixels
//        {
//
//          lpbImage = (unsigned char *)from_ptr;
//          for (k=0; k<len_words*2; k+=2)
//          {
//            nTempPix1 = *lpbImage;
//            nTempPix2 = *(lpbImage+1);
//            *lpbImage = nTempPix2;
//            *(lpbImage+1) = nTempPix1;
//            lpbImage += 2;
//          }
//          fwrite(from_ptr, sizeof(unsigned short), len_words, fFile);
//          lpbImage = (unsigned char *)from_ptr;
//          for (k=0; k<len_words*2; k+=2)
//          {
//            nTempPix1 = *lpbImage;
//            nTempPix2 = *(lpbImage+1);
//            *lpbImage = nTempPix2;
//            *(lpbImage+1) = nTempPix1;
//            lpbImage += 2;
//          }
//          //to_ptr += len_words;            // advance for next round
//        }
//

/* based on the DINTERLACE configuration 
   reorder the data from data buffer "from" to the 
   data buffer "to"
*/


void deinterlace( cfg, from, to, len_bytes )
struct SI_DINTERLACE *cfg;
unsigned short *from;
unsigned short *to;
int len_bytes;
{
  int i;
  unsigned int k;
  unsigned short *to_ptr;
  unsigned short *from_ptr;
  unsigned int  len_words;
  unsigned int  offset;
  unsigned int  nX, nY;
  unsigned int  index;
  int    ntb, n_rows, n_cols;
  size_t  nStrLen;
  unsigned int  n1cols,n2cols,n3cols,n4cols;
  unsigned int  n1rows,n2rows,n3rows,n4rows;
  int    repeat;


  to_ptr = to;        // pick up the image pointer for write
  from_ptr = from;      // pick up the image pointer for de interlace
  n_rows = cfg->n_rows;
  n_cols = cfg->n_cols;
  offset  = 0;          // offset into (input)image
  nX  = 0;                 // X pointer
  nY  = 0;                 // Y pointer
  ntb = 0;                 // pointer to test buffer
  cfg->n_ptr_pos = 0;      // pointer to filling buffer

  len_words = len_bytes/2;

  // here we de-interlace
  switch (cfg->interlace_type)// only if we de-interlace
  {
    case 0:                // --> no de interlace
      for (k=0; k<len_words; k++) {
        *to_ptr++ = *from_ptr++;
      }
      cfg->n_ptr_pos += len_words;     // just add how many pixels we have written
      break;
    case 1:                            // --> four quadrant de-interlace
      for (k=0; k<len_words; k++)
      {
        switch (offset % 4) {
        case 0:  
          index = nX + n_cols * nY; 
          break;  // serial top incrementing
        case 1:  
          index = (n_cols - nX -1) + (n_cols * nY);  
          break;  // serial top decrementing
        case 2:  
          index = (nX + n_cols * n_rows - n_cols) - (n_cols * nY);  
          break;  // serial bottom incrementing
        case 3:  
          index = (n_cols * n_rows - nX - 1) - (n_cols * nY); 
          // serial bottom decrementing
          nX++;
          if (nX == n_cols/2) {
            nX = 0;
            nY++;
            cfg->n_ptr_pos += (n_cols);
          }
          break;
        }
        *(to + index) = *from_ptr; // copy data
        from_ptr++;                         // advance de-interlace pointer
        offset++;                           // advance image offset
      }
      break;
    case 2:                          // --> serial split de-interlace
      for (k=0; k<len_words; k++) {
        switch (offset % 2) {
        case 0:  
          index = nX + n_cols * nY;  
          break;  // serial top incrementing
        case 1:  
          index = (n_cols - nX -1) + (n_cols * nY);
          // serial top decrementing
          nX++;
          if (nX == n_cols/2) {
            nX = 0;
            nY++;
            cfg->n_ptr_pos += (n_cols);
          }
          break;
        }
        *(to + index) = *from_ptr; // copy data
        from_ptr++;                  // advance de-interlace pointer
        offset++;                    // advance image offset
      }
      break;
    case 3:                          // --> parallel split de-interlace
      for (k=0; k<len_words; k++) {
        switch (offset % 2) {
        case 0:
          index = nX + n_cols * nY;
          break;  // serial top incrementing
        case 1:  
          index = (nX + n_cols * n_rows - n_cols) - (n_cols * nY);
          // serial bottom incrementing
          nX++;
          if (nX == n_cols) {
            nX = 0;
            nY++;
            cfg->n_ptr_pos += n_cols;
          }
          break;
        }
        *(to + index) = *from_ptr; // copy data
        from_ptr++;                  // advance de-interlace pointer
        offset++;                    // advance image offset
      }
      break;
    case 4:                          // --> parallel split de-interlace
      for (k=0; k<len_words; k++) {
        switch (offset % 2) {
        case 0:  
          index = nX + n_cols * nY;
          break;  // serial top incrementing
        case 1:  
          index = (n_cols * n_rows - nX - 1) - (n_cols * nY);
          // serial bottom decrementing
          nX++;
          if (nX == n_cols) {
            nX = 0;
            nY++;
            cfg->n_ptr_pos += n_cols;
          }
          break;
        }
        *(to + index) = *from_ptr;  // copy data
        from_ptr++;                  // advance de-interlace pointer
        offset++;                    // advance image offset
      }
      break;
    case 5:                          // --> 9 CCD single port A-channel
      n1cols = n_cols / 3;
      n2cols = n1cols * 2;
      n3cols = n1cols * 3;
      n1rows = n_rows / 3;
      n2rows = n1rows * 2;
      n3rows = n1rows * 3;
      for (k=0; k<len_words; k++) {
        switch (offset % 9) {
        case 0:  
          index = nX + n3cols * nY;
          break;
        case 1:  
          index = nX + n1cols + n3cols * nY;  
          break;
        case 2:  
          index = nX + n2cols + n3cols * nY;  
          break;
        case 3:  
          index = nX + (n1rows * n3cols) + n3cols * nY;
          break;
        case 4:  
          index = nX + n1cols + (n1rows * n3cols) + n3cols * nY;  
          break;
        case 5:  
          index = nX + n2cols + (n1rows * n3cols) + n3cols * nY;
          break;
        case 6:  
          index = nX + (n2rows * n3cols)      + n3cols * nY;
          break;
        case 7:  
          index = nX + n1cols + (n2rows * n3cols) + n3cols * nY;  
          break;
        case 8:  
          index = nX + n2cols + (n2rows * n3cols) + n3cols * nY;
          nX++;
          if (nX == n1cols) {
            nX = 0;
            nY++;
            cfg->n_ptr_pos += n_cols;
          }
          break;
        }
        *(to + index) = *from_ptr; // copy data
        from_ptr++;                  // advance de-interlace pointer
        offset++;                    // advance image offset
      }
      break;
    case 6:                          // --> 9 CCD single port B-channel
      n1cols = n_cols / 3;
      n2cols = n1cols * 2;
      n3cols = n1cols * 3;
      n1rows = n_rows / 3;
      n2rows = n1rows * 2;
      n3rows = n1rows * 3;
      for (k=0; k<len_words; k++) {
        switch (offset % 9) {
        case 0:  
          index = (n1cols - nX - 1) + (n1rows * n3cols -n3cols) -
                       n3cols * nY;  
          break;
        case 1:  
          index = (n2cols - nX - 1) + (n1rows * n3cols -n3cols) -
            n3cols * nY;  
          break;
        case 2:  
          index = (n3cols - nX - 1) + (n1rows * n3cols -n3cols) -  
            n3cols * nY;  
          break;
        case 3:  
          index = (n1cols - nX - 1) + (n2rows * n3cols -n3cols) -
            n3cols * nY;  
          break;
        case 4:  
          index = (n2cols - nX - 1) + (n2rows * n3cols -n3cols) -  
            n3cols * nY;  
          break;
        case 5:  
          index = (n3cols - nX - 1) + (n2rows * n3cols -n3cols) -  
            n3cols * nY;  
          break;
        case 6:  
          index = (n1cols - nX - 1) + (n3rows * n3cols -n3cols) -
            n3cols * nY;  
          break;
        case 7:  
          index = (n2cols - nX - 1) + (n3rows * n3cols -n3cols) -  
            n3cols * nY;  
          break;
        case 8:  
          index = (n3cols - nX - 1) + (n3rows * n3cols -n3cols) -  
            n3cols * nY;
          nX++;
          if (nX == n1cols) {
            nX = 0;
            nY++;
            cfg->n_ptr_pos += n_cols;
          }
          break;
        }
        *(to + index) = *from_ptr; // copy  data
        from_ptr++;                  // advance de-interlace pointer
        offset++;                    // advance image offset
      }
      break;
    case 7:                          // --> 9 CCD dual port A+B-channel
      n1cols = n_cols / 3;
      n2cols = n1cols * 2;
      n3cols = n1cols * 3;
      n1rows = n_rows / 3;
      n2rows = n1rows * 2;
      n3rows = n1rows * 3;
      for (k=0; k<len_words; k++) {
        switch (offset % 18) {
        case 0:
          index = nX  + n3cols * nY;  
          break;
        case 1:    
          index = nX + n1cols + n3cols * nY;  
          break;
        case 2:    
          index = nX + n2cols + n3cols * nY;
          break;
        case 3:
          index = nX + (n1rows * n3cols) + n3cols * nY;
          break;
        case 4:
          index = nX + n1cols + (n1rows * n3cols)  + n3cols * nY;  
          break;
        case 5:    
          index = nX + n2cols + (n1rows * n3cols)  + n3cols * nY;  
          break;
        case 6:    
          index = nX + (n2rows * n3cols) + n3cols * nY;
          break;
        case 7: 
          index = nX + n1cols + (n2rows * n3cols)  + n3cols * nY;  
          break;
        case 8:
          index = nX + n2cols + (n2rows * n3cols)  + n3cols * nY;  
          break;
        case 9:
          index = (n1cols - nX - 1)  + (n1rows * n3cols -n3cols) -
             n3cols * nY;
          break;
        case 10: 
          index = (n2cols - nX - 1)  + (n1rows * n3cols -n3cols) -
             n3cols * nY;
          break;
        case 11:
          index = (n3cols - nX - 1)  + (n1rows * n3cols -n3cols) - 
            n3cols * nY;  
          break;
        case 12:
          index = (n1cols - nX - 1)  + (n2rows * n3cols -n3cols) - 
            n3cols * nY;  
          break;
        case 13:
          index = (n2cols - nX - 1)  + (n2rows * n3cols -n3cols) -
            n3cols * nY;
          break;
        case 14:
          index = (n3cols - nX - 1)  + (n2rows * n3cols -n3cols) - 
            n3cols * nY;
          break;
        case 15:
          index = (n1cols - nX - 1)  + (n3rows * n3cols -n3cols) -
             n3cols * nY;  
          break;
        case 16:  
          index = (n2cols - nX - 1)  + (n3rows * n3cols -n3cols) - 
            n3cols * nY;
          break;
        case 17:
          index = (n3cols - nX - 1)  + (n3rows * n3cols -n3cols) -
            n3cols * nY;
          nX++;
          if (nX == n1cols) {
            nX = 0;
            nY++;
            cfg->n_ptr_pos += n_cols;
          }
          break;
        }
        *(to + index) = *from_ptr; // copy data
        from_ptr++;                // advance de-interlace pointer
        offset++;                  // advance image offset
      }
      break;
    case 8:                          // --> 16 CCD single port A-channel
      n1cols = n_cols / 4;
      n2cols = n1cols * 2;
      n3cols = n1cols * 3;
      n4cols = n1cols * 4;
      n1rows = n_rows / 4;
      n2rows = n1rows * 2;
      n3rows = n1rows * 3;
      n4rows = n1rows * 4;
      for( k=0 ; k<len_words ; k++ ) {
        switch( offset % 16 ) {
          case 0:
            index = nX + (n4cols * nY);
            break;
          case 1:
            index = nX + (n4cols * nY)        + n1cols;
            break;
          case 2:
            index = nX + (n4cols * nY)        + n2cols;
            break;
          case 3:
            index = nX + (n4cols * nY)        + n3cols;  
            break;
          case 4:
            index = nX + (n4cols * (n1rows + nY));      
            break;
          case 5:
            index = nX + (n4cols * (n1rows + nY)) + n1cols;  
            break;
          case 6:
            index = nX + (n4cols * (n1rows + nY)) + n2cols;  
            break;
          case 7:    
            index = nX + (n4cols * (n1rows + nY)) + n3cols;  
            break;
          case 8:
            index = nX + (n4cols * (n2rows + nY));      
            break;
          case 9:
            index = nX + (n4cols * (n2rows + nY)) + n1cols;  
            break;
          case 10:
            index = nX + (n4cols * (n2rows + nY)) + n2cols;  
            break;
          case 11:
            index = nX + (n4cols * (n2rows + nY)) + n3cols;  
            break;
          case 12:
            index = nX + (n4cols * (n3rows + nY));      
            break;
          case 13:
            index = nX + (n4cols * (n3rows + nY)) + n1cols;  
            break;
          case 14:  
            index = nX + (n4cols * (n3rows + nY)) + n2cols;  
            break;
          case 15:
            index = nX + (n4cols * (n3rows + nY)) + n3cols;
            nX++;
            if( nX >= n1cols ) {
              nX = 0;
              nY++;
              cfg->n_ptr_pos += n_cols;
            }
            break;
        }
        *(to + index) = *from_ptr; // copy data
        from_ptr++;                // advance de-interlace pointer
        offset++;                  // advance image offset
      }
      break;
    case 9:                          // --> 16 CCD single port B-channel
      n1cols = n_cols / 4;
      n2cols = n1cols * 2;
      n3cols = n1cols * 3;
      n4cols = n1cols * 4;
      n1rows = n_rows / 4;
      n2rows = n1rows * 2;
      n3rows = n1rows * 3;
      n4rows = n1rows * 4;
      for( k=0 ; k<len_words ; k++ ) {
        switch( offset % 16 ) {
          case 0:
            index = (n1cols - nX - 1) + ((n1rows - nY - 1) * n4cols);  
            break;
          case 1:
            index = (n2cols - nX - 1) + ((n1rows - nY - 1) * n4cols);  
            break;
          case 2:
            index = (n3cols - nX - 1) + ((n1rows - nY - 1) * n4cols);  
            break;
          case 3:
            index = (n4cols - nX - 1) + ((n1rows - nY - 1) * n4cols);  
            break;
          case 4:
            index = (n1cols - nX - 1) + ((n2rows - nY - 1) * n4cols);  
            break;
          case 5:
            index = (n2cols - nX - 1) + ((n2rows - nY - 1) * n4cols);  
            break;
          case 6:
            index = (n3cols - nX - 1) + ((n2rows - nY - 1) * n4cols);  
            break;
          case 7:
            index = (n4cols - nX - 1) + ((n2rows - nY - 1) * n4cols);  
            break;
          case 8:
            index = (n1cols - nX - 1) + ((n3rows - nY - 1) * n4cols);  
            break;
          case 9:
            index = (n2cols - nX - 1) + ((n3rows - nY - 1) * n4cols);  
            break;
          case 10:  
            index = (n3cols - nX - 1) + ((n3rows - nY - 1) * n4cols);  
            break;
          case 11:
            index = (n4cols - nX - 1) + ((n3rows - nY - 1) * n4cols);  
            break;
          case 12:  
            index = (n1cols - nX - 1) + ((n4rows - nY - 1) * n4cols);  
            break;
          case 13:
            index = (n2cols - nX - 1) + ((n4rows - nY - 1) * n4cols);  
            break;
          case 14:  
            index = (n3cols - nX - 1) + ((n4rows - nY - 1) * n4cols);  
            break;
          case 15:  
            index = (n4cols - nX - 1) + ((n4rows - nY - 1) * n4cols);
            nX++;
            if( nX >= n1cols ) {
              nX = 0;
              nY++;
              cfg->n_ptr_pos += n_cols;
            }
            break;
        }
        *(to + index) = *from_ptr;  // copy data
        from_ptr++;                // advance de-interlace pointer
        offset++;                  // advance image offset
      }
      break;
    case 10:                        // --> 16 CCD dual port A+B-channel
      n1cols = n_cols / 4;
      n2cols = n1cols * 2;
      n3cols = n1cols * 3;
      n4cols = n1cols * 4;
      n1rows = n_rows / 4;
      n2rows = n1rows * 2;
      n3rows = n1rows * 3;
      n4rows = n1rows * 4;
      for( k=0 ; k<len_words ; k++ ) {
        switch( offset % 32 ) {
          case 0:
            index = nX + (n4cols * nY); 
            break;
          case 1:
            index = nX + (n4cols * nY)        + n1cols;  
            break;
          case 2:
            index = nX + (n4cols * nY)        + n2cols;  
            break;
          case 3:
            index = nX + (n4cols * nY)        + n3cols;  
            break;
          case 4:
            index = nX + (n4cols * (n1rows + nY));      
            break;
          case 5:
            index = nX + (n4cols * (n1rows + nY)) + n1cols;  
            break;
          case 6:
            index = nX + (n4cols * (n1rows + nY)) + n2cols;  
            break;
          case 7:    
            index = nX + (n4cols * (n1rows + nY)) + n3cols;  
            break;
          case 8:    
            index = nX + (n4cols * (n2rows + nY));      
            break;
          case 9:    
            index = nX + (n4cols * (n2rows + nY)) + n1cols;  
            break;
          case 10:  
            index = nX + (n4cols * (n2rows + nY)) + n2cols;  
            break;
          case 11:  
            index = nX + (n4cols * (n2rows + nY)) + n3cols;  
            break;
          case 12:  
            index = nX + (n4cols * (n3rows + nY));      
            break;
          case 13:  
            index = nX + (n4cols * (n3rows + nY)) + n1cols;  
            break;
          case 14:  
            index = nX + (n4cols * (n3rows + nY)) + n2cols;  
            break;
          case 15:  
            index = nX + (n4cols * (n3rows + nY)) + n3cols;  
            break;
          case 16:  
            index = (n1cols - nX - 1) + ((n1rows - nY - 1) * n4cols);  
            break;
          case 17:  
            index = (n2cols - nX - 1) + ((n1rows - nY - 1) * n4cols);  
            break;
          case 18:  
            index = (n3cols - nX - 1) + ((n1rows - nY - 1) * n4cols);  
            break;
          case 19:  
            index = (n4cols - nX - 1) + ((n1rows - nY - 1) * n4cols);  
            break;
          case 20:  
            index = (n1cols - nX - 1) + ((n2rows - nY - 1) * n4cols);  
            break;
          case 21:  
            index = (n2cols - nX - 1) + ((n2rows - nY - 1) * n4cols);  
            break;
          case 22:  
            index = (n3cols - nX - 1) + ((n2rows - nY - 1) * n4cols);  
            break;
          case 23:  
            index = (n4cols - nX - 1) + ((n2rows - nY - 1) * n4cols);  
            break;
          case 24:  
            index = (n1cols - nX - 1) + ((n3rows - nY - 1) * n4cols);  
            break;
          case 25:  
            index = (n2cols - nX - 1) + ((n3rows - nY - 1) * n4cols);  
            break;
          case 26:  
            index = (n3cols - nX - 1) + ((n3rows - nY - 1) * n4cols);  
            break;
          case 27:  
            index = (n4cols - nX - 1) + ((n3rows - nY - 1) * n4cols);  
            break;
          case 28:  
            index = (n1cols - nX - 1) + ((n4rows - nY - 1) * n4cols);  
            break;
          case 29:  
            index = (n2cols - nX - 1) + ((n4rows - nY - 1) * n4cols);  
            break;
          case 30:  
            index = (n3cols - nX - 1) + ((n4rows - nY - 1) * n4cols);  
            break;
          case 31:  
            index = (n4cols - nX - 1) + ((n4rows - nY - 1) * n4cols);
            nX++;
            if( nX >= n1cols ) {
              nX = 0;
              nY++;
              cfg->n_ptr_pos += n_cols;
            }
            break;
        }
        *(to + index) = *from_ptr;  // copy data
        from_ptr++;                // advance de-interlace pointer
        offset++;                  // advance image offset
      }
      break;
  }
  
  return; 
}
