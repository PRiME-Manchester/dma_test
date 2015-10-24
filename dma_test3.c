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

// 10ms timer tick
#define TIMER_TICK_PERIOD  10000

// Block size in words (4 bytes each)
#define BLOCK_SIZE         1000
#define DMA_REPS           10

// MEM_SIZE in words (4 bytes each)
#define MEM_SIZE           28000000
//#define MEM_SIZE           30
#define MEM_VALUE          0x5f5f5f5f
#define ERR_VALUE          0x0f0f0f0f

// Controls which part of the program generates verbose messages for debugging purposes
#define NODEBUG_WRITE
#define NODEBUG_READ
#define NODEBUG_CORRUPT

// Determines whether SDRAM blocks are intentionally corrupted
#define NOCORRUPT

// CORRUPT_ID corresponds to the number of DMA transfers done when error is introduced
#define CORRUPT_TID        6
// This corresponds to the sdram index that's corraupted
#define ERR_ID1 					 1500
#define ERR_ID2            1600

// ------------------------
// Enumerations
// ------------------------
typedef enum state
{
	Read,
	Write,
	Rewrite,
	Exit
} state;

// ------------------------
// Variables
// ------------------------

uint coreID;
uint chipID;
uint test_DMA;
uint dma_errors   = 0;
uint read_count   = 0;
uint err_count    = 1;
uint DMA_blockread_step = 0;
uint read_step    = 0;
uint transfers    = 0;
uint ufailed      = 0;
uint DMAtransfers = 0;

uint *dtcm_buffer_r;
uint *dtcm_buffer_w;
uint *sdram_buffer;

uint t1,t2;

state spinn_state_next = Write;
state spinn_state      = Write;

// ------------------------
// Function prototypes
// ------------------------

void app_init ();
void app_done ();
void count_ticks (uint ticks, uint null);
void reverse(char *s, int len);
uint itoa(uint num, char s[], uint len);
void ftoa(float n, char *res, int precision);
void configure_crc_tables(void);
void initialize_DTCM(void);
void dma_transfer(uint tid, uint ttag);

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
  spin1_callback_on (DMA_TRANSFER_DONE, dma_transfer, 1);
  spin1_callback_on (TIMER_TICK, count_ticks, 0);

  // Schedule 1st DMA write (and fill MEM_SIZE)
	spin1_schedule_callback(dma_transfer, 0, 0, 1);

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
*  and initialize application variables.
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
  io_printf (IO_BUF, "[core %d] Starting simulation\n", coreID);

  // Allocate a buffer in SDRAM
  sdram_buffer = (uint *) sark_xalloc (sv->sdram_heap,
					MEM_SIZE * sizeof(uint),
					0,
					ALLOC_LOCK);

  // and a buffer in DTCM - separate ones for reading and writing
  dtcm_buffer_w = (uint *) sark_alloc (BLOCK_SIZE, sizeof(uint));
  dtcm_buffer_r = (uint *) sark_alloc (BLOCK_SIZE, sizeof(uint));

  if (dtcm_buffer_w == NULL || dtcm_buffer_r == NULL || sdram_buffer == NULL)
  {
    test_DMA = FALSE;
    io_printf (IO_BUF, "[core %d] Error - cannot allocate buffer\n", coreID);
  }
  else
  {
    test_DMA = TRUE;
    // initialize DTCM
    initialize_DTCM();

    // initialize SDRAM
    for (uint i=0; i < MEM_SIZE; i++)
      sdram_buffer[i]  = 0;

    io_printf (IO_BUF, "[core %d] DTCM buffer @ 0x%08x sdram buffer @ 0x%08x\n", coreID, (uint)dtcm_buffer_r, (uint)dtcm_buffer_w, (uint)sdram_buffer);
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

  // report bandwidth
  if((t2-t1)>0)
  {
	  ftoa(MEM_SIZE*4.0*DMA_REPS/(t2-t1)/1e3, tput_s, 2);
	  ftoa(MEM_SIZE*4.0*DMA_REPS/1e6, mb_s, 2);
	  io_printf(IO_BUF, "[core %d] Throughput: %s MB/s (%s MB in %d ms)\n", coreID, tput_s, mb_s, t2-t1);
	}
	else
		io_printf(IO_BUF, "[core %d] Not enough data to compute throughput.\n", coreID);

  // report number of DMA errors
  io_printf (IO_BUF, "[core %d] Failed %d DMA transfers\n", coreID, dma_errors);

  // end
  io_printf (IO_BUF, "[core %d] Stopping simulation\n", coreID);
  io_printf (IO_BUF, "[core %d] -----------------------\n", coreID);

  // Free sdram and dtcm memory
  sark_xfree(sv->sdram_heap, sdram_buffer, ALLOC_LOCK);
  sark_free(dtcm_buffer_r);
  sark_free(dtcm_buffer_w);
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
void count_ticks(uint ticks, uint null)
{
	uint transferid, k=0;

	// check for DMA errors
  if ((dma[DMA_STAT] >> 13)&0x01)  
  {
    dma_errors++;

 		// Print DTCM contents
    // At this point no DMA_transfer_done even is generated,
    // that's why the printf has to be executed here.
#ifdef DEBUG_CORRUPT
		io_printf(IO_BUF, "\n* (R) %d: ", read_step);
		for(uint k=0; k<BLOCK_SIZE; k++)
		{
			io_printf(IO_BUF, "%x ", dtcm_buffer_r[k]);
			dtcm_buffer_r[k] = 0;
		}
		io_printf(IO_BUF, "DMAerr:%d CRCC:%08x CRCR:%08x ", (dma[DMA_STAT] >> 13)&0x01, dma[DMA_CRCC], dma[DMA_CRCR]);
		io_printf(IO_BUF, "\n");
#endif

    // clear DMA errors and restart DMA controller
    spin1_dma_clear_errors();

		// Rewrite corrupted SDRAM block
    spinn_state_next = Rewrite;
    spin1_schedule_callback(dma_transfer, 0, 0, 1);
  }
}

void dma_transfer(uint tid, uint ttag)
{
	uint transferid = 0;

	switch(spinn_state_next)
	{
		
		// Fill up SDRAM (initialization phase)
		case Write:
			do {
				transferid = spin1_dma_transfer_crc(DMA_WRITE,
						sdram_buffer + DMA_blockread_step,
						dtcm_buffer_w,
						DMA_WRITE,
						BLOCK_SIZE*sizeof(uint));
			} while (!transferid);
			
			DMA_blockread_step += BLOCK_SIZE+1;

			DMAtransfers++;

#ifdef DEBUG_WRITE
			io_printf(IO_BUF, "(W) tid:%d\n", transferid);
#endif			

      // Update state
      spinn_state      = Write;
      spinn_state_next = Write;

			if (DMAtransfers==MEM_SIZE/(BLOCK_SIZE+1))
			{
				spinn_state_next = Read;
				DMAtransfers = 0;
				DMA_blockread_step = 0;
			}
			break;

		// Read from SDRAM
		// In case a CRC error is encountered, the error is trapped during the next
		// timer tick
		case Read:
			if (read_count==0)
				t1 = sv->clock_ms;

			// Start the DMA reads after all the writes are done
#ifdef CORRUPT
			if (DMAtransfers == CORRUPT_TID)
			{
  #ifdef DEBUG_CORRUPT
			  io_printf(IO_BUF, "- Corrupting SDRAM location [%d] and [%d]\n", ERR_ID1, ERR_ID2);
  #endif
		    sdram_buffer[ERR_ID1] = ERR_VALUE;
        sdram_buffer[ERR_ID2] = ERR_VALUE;        
		  }
#endif

#ifdef DEBUG_READ
			if (spinn_state==Read)
			{
        if (DMA_blockread_step==(BLOCK_SIZE+1))
					io_printf(IO_BUF, "\nRepetition %d\n", read_count+1);

        io_printf(IO_BUF, "(R) %d: ", read_step);
			
        for(uint k=0; k<BLOCK_SIZE; k++)
				{
					io_printf(IO_BUF, "%x ", dtcm_buffer_r[k]);
					dtcm_buffer_r[k] = 0;
				}
				
				io_printf(IO_BUF, "DMAerr:%d CRCC:%08x CRCR:%08x ", (dma[DMA_STAT] >> 13)&0x01, dma[DMA_CRCC], dma[DMA_CRCR]);
				io_printf(IO_BUF, "\n");
			}
#endif

      //io_printf(IO_BUF, "step:%d\n", DMA_blockread_step);
			transferid = spin1_dma_transfer_crc(DMA_READ,
					sdram_buffer + DMA_blockread_step,
					dtcm_buffer_r,
					DMA_READ,
					BLOCK_SIZE*sizeof(uint));

      read_step = DMA_blockread_step;
			DMA_blockread_step += BLOCK_SIZE+1;

			if (DMA_blockread_step>MEM_SIZE-(BLOCK_SIZE+1))
			{
				DMA_blockread_step = 0;
				read_count++;
			}

      // Update state
      spinn_state      = Read;
      spinn_state_next = Read;

 			if (DMAtransfers%(MEM_SIZE/BLOCK_SIZE*DMA_REPS*10/100)==0)
        io_printf(IO_BUF, "%d%%\n", 10+100*DMAtransfers/(MEM_SIZE/BLOCK_SIZE*DMA_REPS));

	  	if (read_count==DMA_REPS)
			{	
				t2 = sv->clock_ms;
				spinn_state_next = Exit;
			}

			DMAtransfers++;
			break;

		// Rewrite corrupted SDRAM block
		case Rewrite:

#ifdef DEBUG_CORRUPT
			io_printf(IO_BUF, "* Rewriting corrupted SDRAM block\n");
#endif

	    // Reinitialize DTCM
	    initialize_DTCM();

      // Move pointer back one BLOCK_SIZE+1 to correct the error
      DMA_blockread_step -= BLOCK_SIZE+1;
      // Rewrite SDRAM block
      do {
				transferid = spin1_dma_transfer_crc(DMA_WRITE,
						sdram_buffer + DMA_blockread_step,
						dtcm_buffer_w,
						DMA_WRITE,
						BLOCK_SIZE*sizeof(uint));
			} while (!transferid);

      spinn_state = Rewrite;
      spinn_state_next = Read;
			break;

		case Exit:
			//Printout last DMA read
			#ifdef DEBUG_READ
				io_printf(IO_BUF, "(R) %d: ", read_step);
				for(uint k=0; k<BLOCK_SIZE; k++)
				{
					io_printf(IO_BUF, "%x ", dtcm_buffer_r[k]);
					dtcm_buffer_r[k] = 0;
				}
				
				io_printf(IO_BUF, "DMAerr:%d CRCC:%08x CRCR:%08x ", (dma[DMA_STAT] >> 13)&0x01, dma[DMA_CRCC], dma[DMA_CRCR]);
				io_printf(IO_BUF, "\n\n");
			#endif

			spin1_exit(0);
			break;
	}

}


void initialize_DTCM(void)
{
  // initialize DTCM
  for (uint i = 0; i < BLOCK_SIZE+1; i++)
  {
		dtcm_buffer_w[i] = MEM_VALUE;
  	dtcm_buffer_r[i] = 0;
  }
}


// Configure CRC table using 32-bit Ethernet CRC
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
