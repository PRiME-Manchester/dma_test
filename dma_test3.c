/**** dma_test.c/simple_summary ****
*
* SUMMARY
*  SDRAM testing using CRCs (based on simple.c by Luis Plana)
*
* AUTHOR
*  Patrick Camilleri
*
* DETAILS
*  Created on       : 28 September 2015
*  Version          : Revision: 1
*
*************************************/

// SpiNNaker API

#include "spin1_api.h"
// ------------------------------------------------------------------------
// simulation constants
// ------------------------------------------------------------------------

#define TIMER_TICK_PERIOD  10000
#define TOTAL_TICKS        400
#define ITER_TIMES         1

#define BLOCK_SIZE         4000
#define DMA_REPS           10000

#define CORES_USED         3
#define SDRAM_SIZE         112000000000

#define NODEBUG

// ------------------------------------------------------------------------
// variables
// ------------------------------------------------------------------------

uint coreID;
uint chipID;
uint test_DMA;
uint dma_errors=0;
uint read_count=0;
uint err_count=1;

uint transfers = 0;
uint ufailed = 0;

uint *dtcm_buffer;
uint *sdram_buffer;

uint t1,t2;
uint sdram_tmp;


// ---------------------------------------------
// Function prototypes
// ---------------------------------------------

void app_init ();
void app_done ();
void count_ticks (uint ticks, uint null);
void reverse(char *s, int len);
uint itoa(uint num, char s[], uint len);
void ftoa(float n, char *res, int precision);
void configure_crc_tables(void);
void SDRAM_Write(uint none1, uint none2);
void SDRAM_Read(uint tid, uint ttag);

/****** dma_test.c/c_main
*
* SUMMARY
*  This function is called at application start-up.
*  It is used to register event callbacks and begin the simulation.
*
* SYNOPSIS
*  int c_main()
*
* SOURCE
*/
void c_main()
{
  // Get core and chip IDs
  coreID = spin1_get_core_id ();
  chipID = spin1_get_chip_id ();

  // Setup CRC tables
  configure_crc_tables();

  io_printf (IO_BUF, ">> dma_test - chip (%d, %d) core %d\n",
	     chipID >> 8, chipID & 255, coreID);

  // Set timer tick value (in microseconds)
  spin1_set_timer_tick (TIMER_TICK_PERIOD);

  // Register callbacks
  spin1_callback_on (DMA_TRANSFER_DONE, SDRAM_Read, 0);
  spin1_callback_on (TIMER_TICK, count_ticks, 2);

  // Schedule 1st DMA write
  spin1_schedule_callback(SDRAM_Write, 0, 0, 1);

  // Initialize application
  app_init ();

  // Go
  spin1_start (SYNC_WAIT);

  // Report results
  app_done ();
}


/****f* dma_test.c/app_init
*
* SUMMARY
*  This function is called at application start-up.
*  It's used to say hello, setup multicast routing table entries,
*  and initialise application variables.
*
* SYNOPSIS
*  void app_init ()
*
* SOURCE
*/
void app_init ()
{
  /* ------------------------------------------------------------------- */
  /* initialize the application processor resources                      */
  /* ------------------------------------------------------------------- */
  /* say hello */

  io_printf (IO_BUF, "[core %d] -----------------------\n", coreID);
  io_printf (IO_BUF, "[core %d] starting simulation\n", coreID);

  // Allocate a buffer in SDRAM
  sdram_buffer = (uint *) sark_xalloc (sv->sdram_heap,
					(SDRAM_SIZE/CORES_USED) * sizeof(uint),
					0,
					ALLOC_LOCK);

  // and a buffer in DTCM
  dtcm_buffer = (uint *) sark_alloc (BLOCK_SIZE, sizeof(uint));

  if (dtcm_buffer == NULL ||  sdram_buffer == NULL)
  {
    test_DMA = FALSE;
    io_printf (IO_BUF, "[core %d] error - cannot allocate buffer\n", coreID);
  }
  else
  {
    test_DMA = TRUE;
    // initialize DTCM
    for (uint i = 0; i < BLOCK_SIZE+1; i++)
      dtcm_buffer[i]   = BLOCK_SIZE-i;

    // initialize SDRAM
    for (uint i=0; i < SDRAM_SIZE/CORES_USED; i++)
      sdram_buffer[i]  = 0;

    io_printf (IO_BUF, "[core %d] dtcm buffer @ 0x%08x sdram buffer @ 0x%08x\n", coreID, (uint) dtcm_buffer, (uint)sdram_buffer);
  }
}


/****f* dma_test.c/app_done
*
* SUMMARY
*  This function is called at application exit.
*  It's used to report some statistics and say goodbye.
*
* SYNOPSIS
*  void app_done ()
*
* SOURCE
*/
void app_done ()
{
	char tput_s[20], mb_s[20];

  // report simulation time
  io_printf (IO_BUF, "[core %d] simulation lasted %d ticks\n", coreID,
             spin1_get_simulation_time());

  // report number of packets
  io_printf (IO_BUF, "[core %d] read count: %d\n", coreID, read_count);

  // report bandwidth
  ftoa(BUFFER_SIZE*4.0*DMA_REPS/(t2-t1)/1e3, tput_s, 2);
  ftoa(BUFFER_SIZE*4.0*DMA_REPS/1e6, mb_s, 2);
  io_printf(IO_BUF, "Throughput: %s MB/s (%s MB in %d ms)\n\n", tput_s, mb_s, t2-t1);


  // report number of DMA errors
  io_printf (IO_BUF, "[core %d] failed %d DMA transfers\n", coreID, dma_errors);

  // end
  io_printf (IO_BUF, "[core %d] stopping simulation\n", coreID);
  io_printf (IO_BUF, "[core %d] -------------------\n", coreID);
}


/****f* dma_test.c/count_ticks
*
* SUMMARY
*  This function is used as a callback for timer tick events.
*  It inverts the state of LED 1 and sends packets.
*
* SYNOPSIS
*  void count_ticks (uint ticks, uint null)
*
* INPUTS
*   uint ticks: simulation time (in ticks) - provided by the RTS
*   uint null: padding - all callbacks must have two uint arguments!
*
* SOURCE
*/
void count_ticks (uint ticks, uint null)
{
	uint transfer_id;

	// check for DMA errors
  if ((dma[DMA_STAT] >> 13)&0x01)  
  {
    dma_errors++;

    // clear DMA errors and restart DMA controller
    spin1_dma_clear_errors();
    
    io_printf(IO_BUF, "Err:%d CRCC:%08x CRCR:%08x\n", err_count, dma[DMA_CRCC], dma[DMA_CRCR]);

    if (err_count==3)
	 	{	
	 		io_printf(IO_BUF, "- Clearing SDRAM error\n");
	    sdram_buffer[400] = sdram_tmp;
	  }
 		
    // Restart DMA reads
  	transfer_id = spin1_dma_transfer_crc(DMA_READ,
					sdram_buffer,
					dtcm_buffer,
					DMA_READ,
					BUFFER_SIZE*sizeof(uint));

  	err_count++;
  }

  // stop if desired number of ticks reached
  if (ticks >= TOTAL_TICKS)
    spin1_exit (0);
}


// reverses a string 's' of length 'len'
void reverse(char *s, int len)
{
  int i=0, j=len-1;
  char temp;
  while (i<j)
  {
    temp = s[i];
    s[i] = s[j];
    s[j] = temp;
    i++;
    j--;
  }
}

// Converts a given integer num to string str[].  len is the number
// of digits required in output. If len is more than the number
// of digits in num, then 0s are added at the beginning.
uint itoa(uint num, char s[], uint len)
{
  uint i = 0;

  do {
    s[i++] = '0' + num%10;
    num /= 10;
  } while (num>0);

  // If number of digits required is more, then
  // add 0s at the beginning
  while (i < len)
    s[i++] = '0';

  reverse(s, i);
  s[i] = '\0';
  return i;
}


// Converts a floating point number to string.
void ftoa(float n, char *res, int precision)
{
  // Extract integer part
  int ipart = (int)n;

  // Extract floating part
  float fpart = n - (int)n;

  // convert integer part to string
  uint i = itoa(ipart, res, 0);

  // check for display option after point
  if (precision != 0)
  {
    res[i] = '.';  // add dot

    // This computes pow(10,precision)
    uint m=1;
    if (precision>0)
    for (int i=0; i<precision; i++)
      m*=10;

    // Get the value of fraction part upto given no.
    // of points after dot. The third parameter is needed
    // to handle cases like 233.007
    fpart = fpart * m;

    itoa((int)fpart, res+i+1, precision);
  }
}


void SDRAM_Write(uint none1, uint none2)
{
	uint transfer_id;

	transfer_id = spin1_dma_transfer_crc(DMA_WRITE,
						sdram_buffer,
						dtcm_buffer,
						DMA_WRITE,
						BUFFER_SIZE*sizeof(uint));
}

void SDRAM_Read(uint tid, uint ttag)
{
	uint transfer_id;

	if (read_count==0)
		t1 = sv->clock_ms;
	else if (read_count==DMA_REPS)
		t2 = sv->clock_ms;	

	if (coreID==2 && read_count==2)
	{
	  io_printf(IO_BUF, "- Corrupting SDRAM\n");
    sdram_tmp = sdram_buffer[400];
    sdram_buffer[400] = 0xf0f0f0f0;
  }

	transfer_id = spin1_dma_transfer_crc(DMA_READ,
						sdram_buffer,
						dtcm_buffer,
						DMA_READ,
						BUFFER_SIZE*sizeof(uint));

	read_count++;
}

// Configure CRC table
void configure_crc_tables(void)
{
  dma[DMA_CRCT +   0] = 0xFB808B20;
  dma[DMA_CRCT +   4] = 0x7DC04590;
  dma[DMA_CRCT +   8] = 0xBEE022C8;
  dma[DMA_CRCT +  12] = 0x5F701164;
  dma[DMA_CRCT +  16] = 0x2FB808B2;
  dma[DMA_CRCT +  20] = 0x97DC0459;
  dma[DMA_CRCT +  24] = 0xB06E890C;
  dma[DMA_CRCT +  28] = 0x58374486;
  dma[DMA_CRCT +  32] = 0xAC1BA243;
  dma[DMA_CRCT +  36] = 0xAD8D5A01;
  dma[DMA_CRCT +  40] = 0xAD462620;
  dma[DMA_CRCT +  44] = 0x56A31310;
  dma[DMA_CRCT +  48] = 0x2B518988;
  dma[DMA_CRCT +  52] = 0x95A8C4C4;
  dma[DMA_CRCT +  56] = 0xCAD46262;
  dma[DMA_CRCT +  60] = 0x656A3131;
  dma[DMA_CRCT +  64] = 0x493593B8;
  dma[DMA_CRCT +  68] = 0x249AC9DC;
  dma[DMA_CRCT +  72] = 0x924D64EE;
  dma[DMA_CRCT +  76] = 0xC926B277;
  dma[DMA_CRCT +  80] = 0x9F13D21B;
  dma[DMA_CRCT +  84] = 0xB409622D;
  dma[DMA_CRCT +  86] = 0x21843A36;
  dma[DMA_CRCT +  90] = 0x90C21D1B;
  dma[DMA_CRCT +  94] = 0x33E185AD;
  dma[DMA_CRCT +  98] = 0x627049F6;
  dma[DMA_CRCT + 102] = 0x313824FB;
  dma[DMA_CRCT + 106] = 0xE31C995D;
  dma[DMA_CRCT + 110] = 0x8A0EC78E;
  dma[DMA_CRCT + 114] = 0xC50763C7;
  dma[DMA_CRCT + 118] = 0x19033AC3;
  dma[DMA_CRCT + 122] = 0xF7011641;
}

