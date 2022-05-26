/*
  A synchronization filter for RS41 Radio Sonde receivers.
 
  Auther: mino-hidetoshi
  version 0.91 2022.05.26

  compile: 
      gcc -O3 -o rs41sync rs41sync.c

  usage:
      sox file.wav -b 8 -c 1 -r 48k -t wav - | ./rs41sync | rs41decoder
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BIT_RATE 4800
#define SPB 10     // samples per bit

#define BUFSIZE ( 1 << 9 )
#define HALF ( BUFSIZE >> 1 )
#define INDX_MASK ( BUFSIZE - 1 )
#define THRES 0.8  // correlation threshold in header pattern ditection

int buf[BUFSIZE];

unsigned char header[] = { 0x10, 0xb6, 0xca, 0x11, 0x22, 0x96 };
                      // { 0x10, 0xb6, 0xca, 0x11, 0x22, 0x96, 0x12, 0xf8 };
int bin_head[ ( sizeof header ) << 3 ];

#define SUBHEADS 7
    // MAIN 
    // ID_SEQ		// PTU  	// GPS1   
    // GPS2		// GPS3 	// ZERO

unsigned char subhead[SUBHEADS][2] = {	// field header patterns
    { 0x10, 0xb6 }, 
    { 0x17, 0x13 }, 	{ 0xe3, 0x88 }, { 0x96, 0x67 }, 
    { 0xd7, 0xad }, 	{ 0xb9, 0xff }, { 0x15, 0xe4 } }; 

int offset[SUBHEADS] = {  		// field header positions
    (BIT_RATE-100)*SPB, 
    0x039*8*SPB,    	0x065*8*SPB,    0x093*8*SPB, 
    0x0B5*8*SPB,    	0x112*8*SPB,    0x12B*8*SPB};

int      bin_subhead[SUBHEADS][ ( sizeof subhead[0] ) * 8 ];
double   avr_subhead[SUBHEADS];
double stdev_subhead[SUBHEADS];

int read_int( FILE *fp, int n, int* i_data ) 
{
   *i_data = 0;
   for( int b = 0; b < n; b++ ) {
      int c;
      if( ( c = fgetc( fp ) ) == EOF ) return -1;
      *i_data += ( c << ( b * 8 ) );
   }
   return 0;
}

void write_int( FILE *fp, int n, int i_data ) {
   for( int b = 0; b < n; b++ ) {
      fputc( i_data & 0xff, fp );
      i_data >>= 8;
   }
}

int read_wav_header( FILE* fp, int* rate, int* depth, int* nch ) 
{
   char str[4];
   int dummy;

   if( fread( str, 1, 4, fp ) < 4 ) return -1;
   if( strncmp( str, "RIFF", 4 ) ) return -1;
   if( read_int( fp, 4, &dummy ) ) return -1;
   if( fread( str, 1, 4, fp ) < 4 ) return -1;
   if( strncmp( str, "WAVE", 4 ) ) return -1;

   while( 1 ) {
      if( fread( str, 1, 4, fp ) < 4 ) return -1;
      if( strncmp( str, "fmt ", 4 ) == 0 ) {
         int chunk_size;
         if( read_int( fp, 4, &chunk_size ) ) return -1;
         if( read_int( fp, 2, &dummy ) ) return -1; chunk_size -= 2;
         if( read_int( fp, 2,    nch ) ) return -1; chunk_size -= 2;
         if( read_int( fp, 4,   rate ) ) return -1; chunk_size -= 4;
         if( read_int( fp, 4, &dummy ) ) return -1; chunk_size -= 4;
         if( read_int( fp, 2, &dummy ) ) return -1; chunk_size -= 2;
         if( read_int( fp, 2,  depth ) ) return -1; chunk_size -= 2;
         if( read_int( fp, chunk_size, &dummy ) ) return -1; 
      }
      else if( strncmp( str, "data", 4 ) == 0 ) {
         if( read_int( fp, 4, &dummy ) ) return -1;
         break;
      }
      else {
         int chunk_size;
         if( read_int( fp, 4, &chunk_size ) ) return -1;
         if( read_int( fp, chunk_size, &dummy ) ) return -1;
      }
   }
   return 0;
}

void write_wav_header( FILE* fp, int rate, int depth, int nch )
{
   fwrite( "RIFF", 1, 4, fp );
   write_int( fp, 4, 0x7ffff024 ); // unknown size ( sox convention ? )
   fwrite( "WAVE", 1, 4, fp );

   int bsize = nch * ( depth / 8 );
   fwrite( "fmt ", 1, 4, fp );
   write_int( fp, 4, 16 ); 	// fmt chunk size
   write_int( fp, 2,  1 ); 	// linear PCM
   write_int( fp, 2,  nch ); 	// #channel
   write_int( fp, 4,  rate ); 	// sampling rate
   write_int( fp, 4,  rate * bsize  ); // sampling rate * block size
   write_int( fp, 2,  bsize ); 	// block size
   write_int( fp, 2,  depth ); 	// depth, bits per sample

   fwrite( "data", 1, 4, fp );
   write_int( fp, 4, 0x7ffff000 ); // unknown size ( sox convention ? )
}

void hex2bin( unsigned char* hex, int* bin, int len ) {
    int j = 0;
    for( int i = 0; i < len; ++ i ) {
       unsigned int nibble = hex[i] & 0x0f;
       for( int b = 0; b < 4; ++b ) {
          bin[j] = nibble & 1;
          nibble >>= 1;
          ++ j;
       }
       nibble = ( hex[i] >> 4 ) & 0x0f;
       for( int b = 0; b < 4; ++b ) {
          bin[j] = nibble & 1;
          nibble >>= 1;
          ++ j;
       }
    }
}

double cor( int* buf, int start, int mask, int* pattern, int pat_len )
{
   double c = 0.0;
   int sample_len = pat_len * SPB;
   for( int i = 0; i < sample_len; ++ i ) {
       c += buf[ ( start + i ) & mask ] * pattern[ i / SPB ];
   }
   return c / sample_len;
}

double avr( int* buf, int start, int mask, int len )
{
   double a = 0.0;
   for( int i = 0; i < len; ++ i ) {
       a += buf[ ( start + i ) & mask ];
   }
   return a / len;
}

double stdev( int* buf, int start, int mask, int len, double avr )
{
   double ss = 0.0;
   for( int i = 0; i < len; ++ i ) {
       double x = buf[ ( start + i ) & mask ];
       ss += x*x;
   }
   return sqrt( ss/len - avr*avr ); 
}

int main(int argc, char *argv[]) {

   int out_spb = SPB;

   for( int i = 1; i < argc; i ++ ) {
      if( strncmp( argv[ i ], "-c", 2 ) == 0 ) out_spb = 1;
   }

   FILE *fp = stdin;
   
   int rate, depth, nch;

   if ( read_wav_header( fp, &rate, &depth, &nch) ) {
      fprintf( stderr, "No valid header found.\n");
      fclose(fp);
      return 1;
   }
   // printf( " rate %d depth %d nch %d \n", rate, depth, nch );
   if( rate != BIT_RATE*SPB || nch != 1 || depth != 8 ) {
      fprintf( stderr, "Input audio must be 8 bit 48K mono. ");
      fprintf( stderr, "Use sox with options -b 8 -c 1 -r 48k.\n");
      fclose(fp);
      return 1;
   }

   //setbuf(stdout, NULL);
   write_wav_header( stdout, BIT_RATE * out_spb, 8, 1 );

   hex2bin( header, bin_head, sizeof header );
   int head_len = sizeof header * 8;

   if( ( sizeof header ) * 8 * SPB > BUFSIZE - SPB ) {
      fprintf( stderr, "Header size > buffer size.\n");
      return 1;
   }
   double head_avr = avr( bin_head, 0, -1, head_len );
   double head_stdev = stdev( bin_head, 0, -1, head_len, head_avr );

   for( int i = 0; i < SUBHEADS; ++ i ) {
      hex2bin( subhead[i], bin_subhead[i], sizeof subhead[i] );
      avr_subhead[i] = avr( bin_subhead[i], 0, -1, 8 * sizeof subhead[i] );
      stdev_subhead[i] = 
         stdev( bin_subhead[i], 0, -1, 8 * sizeof subhead[i], avr_subhead[i] );
   }

   for( int i = 0; i < BUFSIZE; i ++ ) buf[ i ] = 0x80;

   unsigned long count = offset[0];
   int polarity = 1;
   int ihead = 0;
   double max_corr = 0.0;
   int p = 0;
   int bit_start = 0;
   int new_start = 0;

   int c;
   while ( ( c = fgetc(fp) ) !=EOF ) {

      buf[ p ] = c;
      p = ( p + 1 ) & INDX_MASK;
      ++ count;

      if( count > offset[ihead] - SPB ) {

         double sig_avr, sig_stdev, corr;
         int pattern_len = ( sizeof subhead[ihead] ) * 8;

         sig_avr = avr( buf, p+SPB, INDX_MASK, pattern_len*SPB );
         sig_stdev = stdev( buf, p+SPB, INDX_MASK, pattern_len*SPB, sig_avr); 
         sig_stdev = fmax( sig_stdev, 0.01 );
         double corr0 = 
              cor( buf, p+SPB, INDX_MASK, bin_subhead[ihead], pattern_len );
         corr0 = ( corr0 - sig_avr*avr_subhead[ihead] )
                                           /sig_stdev/stdev_subhead[ihead];

         if ( ihead == 0 ) {
            if( fabs(corr0) > THRES ) {
                pattern_len = ( sizeof header ) * 8;
                sig_avr = avr( buf, p+SPB, INDX_MASK, pattern_len*SPB );
                sig_stdev = 
                      stdev( buf, p+SPB, INDX_MASK, pattern_len*SPB, sig_avr); 
                sig_stdev = fmax( sig_stdev, 0.01 );
                double corr0 = 
                      cor( buf, p+SPB, INDX_MASK, bin_head, pattern_len );
                corr0 = 
                      ( corr0 - sig_avr * head_avr )/sig_stdev/head_stdev;
            }
            corr = fabs(corr0);
         }
         else {
            corr = corr0 * polarity;
         }
         if ( corr > THRES ) {
            if ( corr > max_corr ) {
                max_corr = corr;
                new_start = ( p + SPB ) & INDX_MASK;
            }
         }
         else {
            if ( max_corr > 0.0 ) {

               if ( ihead == 0 ) {
                   if ( corr0 < 0 ) {
                       polarity = -1;
                   }
                   count = 0L;
                   fflush(stdout);
#ifdef DEBUG
                   fprintf( stderr, "\n%5.3f ", max_corr );
#endif
               }
#ifdef DEBUG
               fprintf( stderr, "%5ld ", count );
#endif
               int shift = ( new_start - bit_start ) & INDX_MASK;
               if( shift > HALF ) shift -= BUFSIZE;
#ifdef DEBUG
               fprintf( stderr, "%2d ", shift );
#endif
               bit_start = ( shift < SPB/2 ) ? 
                             new_start : ( new_start - SPB ) & INDX_MASK;

               ihead = ( ihead + 1 ) % SUBHEADS;
            }
            max_corr = 0.0;
         }
      }
      if( ihead > 0 && count > offset[ihead] + SPB ) {
         ihead = ( ihead + 1 ) % SUBHEADS;
      }

      int to_go = ( bit_start - p ) & INDX_MASK;
      if( to_go > HALF ) to_go -= BUFSIZE;
      if ( to_go <= 0 ) {
         int sum = 0;
         int upper = to_go + SPB - 1;
         int lower = ( to_go + 1 > 0 ) ? to_go + 1 : 0;
         for( int i = lower; i < upper; ++ i ) {
            sum += buf[ ( p + i ) & INDX_MASK ];
         }
         int avr = (int) ( ( ( ( double ) sum ) + 0.5 ) / ( upper - lower ) );
         avr = polarity > 0 ? avr : 255 - avr;
         for( int i = 0; i < out_spb; i ++ ) {
            putchar( avr );
         }
         bit_start = ( bit_start + SPB ) & INDX_MASK;
      }
   }
   fclose(fp);
   fclose(stdout);

   return 0;
}
