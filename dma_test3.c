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
#include <string.h>

// ------------------------------------------------------------------------
// simulation constants
// ------------------------------------------------------------------------

// 10ms timer tick
#define TIMER_TICK_PERIOD  10000

// Block size in words (4 bytes each)
#define BLOCK_SIZE         1000
#define DMA_REPS           100

// MEM_SIZE in words (4 bytes each)
// This is equivalent to   112,112,000 bytes if BLOCK_SIZE=1000
#define MEM_SIZE           (BLOCK_SIZE+1)*28000
#define MEM_VALUE          0x5f5f5f5f
#define ERR_VALUE          0x0f0f0f0f
#define NUM_FAULTS         5

// Controls which part of the program generates verbose messages for debugging purposes
#define NODEBUG_WRITE
#define NODEBUG_READ
#define NODEBUG_CORRUPT
#define SHOW_PROGRESS

// Determines whether SDRAM blocks are intentionally corrupted
#define CORRUPT

// ------------------------
// Type definitions
// ------------------------
typedef enum state
{
	Read,
	Write,
	Rewrite,
	Exit
} state_t;

typedef struct 
{
  uint ticks;
  uint block_id;
} error_t;

// ------------------------
// Variables
// ------------------------

// chip and core IDs
uint coreID;
uint chipID;

// DMA error counter
uint error_k  = 0;

// keeps track of the number of times ALL of the SDRAM has been read
uint rep_count    = 0;

// 'pointer' that keeps track of DMA block to read
uint block_step = 0;

// 'pointer' that keeps track of the previous DMA block before it's updated
// necessary for handling error conditions
uint block_step_pre = 0;

// number of DMA transfers performed
uint transfers_k = 0;

// separate DTCM buffers for reading and writing (easier to test memory contents while debugging)
uint *dtcm_buffer_r;
uint *dtcm_buffer_w;
// SDRAM buffer
uint *sdram_buffer;

// used to store timing information when starting and stopping the transfers
// to calculate the throughput
uint t1,t2;

// enumeration which keep track of the current and next state
state_t spinn_state_next = Write;
state_t spinn_state      = Write;

// structure that keeps time and block information when errors are registered (on read)
error_t errors[50];

// Artifically introduced faults
uint faults[NUM_FAULTS]={432*3000, 520*6000, 604*8000, 834*17000, 934*25000};

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
void print_block(void);
void process_sdp(uint m, uint port);

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
  io_printf (IO_STD, ">> sdping\n");

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

	// Register callback for when an SDP packet is received
	spin1_callback_on (SDP_PACKET_RX, process_sdp, 2);
  
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
    io_printf (IO_BUF, "[core %d] Error - cannot allocate buffer\n", coreID);
  }
  else
  {
    // initialize DTCM
    initialize_DTCM();

    // initialize SDRAM
    for (uint i=0; i < MEM_SIZE; i++)
      sdram_buffer[i]  = 0;

    io_printf (IO_BUF, "[core %d] DTCM buffer (R) @0x%08x DTCM buffer (W) @0x%08x sdram buffer @0x%08x\n", coreID, (uint)dtcm_buffer_r, (uint)dtcm_buffer_w, (uint)sdram_buffer);
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
	char tput_s[20], mb_s[20], time_s[20];

  // report simulation time
  io_printf (IO_BUF, "[core %d] Simulation lasted %d (%d ms) ticks\n", coreID,
             spin1_get_simulation_time(), TIMER_TICK_PERIOD/1000);

  // report bandwidth
  if((t2-t1)>0)
  {
	  ftoa(MEM_SIZE*4.0*DMA_REPS/(t2-t1)/1e3, tput_s, 2);
	  ftoa(MEM_SIZE*4.0*DMA_REPS/1e6, mb_s, 2);
	  ftoa((t2-t1)/1000.0, time_s, 1);
	  io_printf(IO_BUF, "[core %d] Throughput: %s MB/s (%s MB in %s s)\n", coreID, tput_s, mb_s, time_s);
	}
	else
		io_printf(IO_BUF, "[core %d] Not enough data to compute throughput.\n", coreID);

  // report number of DMA errors
  io_printf (IO_BUF, "[core %d] Failed %d DMA transfers\n", coreID, error_k);

  // report information about the errors (time and block_id)
  for(uint i=0; i<error_k; i++)
  {
    ftoa(errors[i].ticks*TIMER_TICK_PERIOD/1e6, time_s, 2);
    io_printf(IO_BUF, "[core %d] Err:%d T:%s s block_id:%d\n", coreID, i+1, time_s, errors[i].block_id);
  }

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
	// check for DMA errors
  if ((dma[DMA_STAT] >> 13)&0x01)  
  {
    errors[error_k].ticks = spin1_get_simulation_time();
    errors[error_k].block_id = block_step_pre;
    error_k++;

 		// Print DTCM contents
    // At this point no DMA_transfer_done even is generated,
    // that's why the printf has to be executed here.
#ifdef DEBUG_CORRUPT
		print_block();
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
		
    // -----------------------------------------------------
		// Fill up SDRAM (initialization phase)
    // -----------------------------------------------------
		case Write:
			do {
				transferid = spin1_dma_transfer_crc(DMA_WRITE,
						sdram_buffer + block_step,
						dtcm_buffer_w,
						DMA_WRITE,
						BLOCK_SIZE*sizeof(uint));
			} while (!transferid);
			
			block_step += BLOCK_SIZE+1;

			transfers_k++;

#ifdef DEBUG_WRITE
			io_printf(IO_BUF, "(W) tid:%d\n", transferid);
#endif			

      // Update state
      spinn_state      = Write;
      spinn_state_next = Write;

			if (transfers_k==MEM_SIZE/(BLOCK_SIZE+1))
			{
				spinn_state_next = Read;
				transfers_k = 0;
				block_step = 0;
			}
			break;

    // -----------------------------------------------------
		// Read from SDRAM
		// In case a CRC error is encountered, the error is trapped during the next
		// timer tick
    // -----------------------------------------------------
		case Read:
			// Start timer
			if (transfers_k==0)
				t1 = sv->clock_ms;
				
			// Start the DMA reads after all the writes are done
#ifdef DEBUG_READ
			if (spinn_state==Read)
			{
        if (block_step==(BLOCK_SIZE+1))
					io_printf(IO_BUF, "\nRepetition %d\n", rep_count+1);

				print_block();			
			}
#endif

// Introduce errors right after the SDRAM is filled up
#ifdef CORRUPT
			if (transfers_k==0)
			{
			  io_printf(IO_BUF, "[core %d] Corrupting SDRAM locations ", coreID);
			  for (uint i=0; i<NUM_FAULTS; i++)
		    {
		    	io_printf(IO_BUF, "[%d]", faults[i]);
		    	if (i<NUM_FAULTS-1)
		    		io_printf(IO_BUF, ", ");
		    	else
		    		io_printf(IO_BUF, ".\n");
		    	sdram_buffer[faults[i]] = ERR_VALUE;
		    }
		  }
#endif

			transferid = spin1_dma_transfer_crc(DMA_READ,
					sdram_buffer + block_step,
					dtcm_buffer_r,
					DMA_READ,
					BLOCK_SIZE*sizeof(uint));

      block_step_pre = block_step;
			block_step += BLOCK_SIZE+1;

			if (block_step>MEM_SIZE-(BLOCK_SIZE+1))
			{
				block_step = 0;
				rep_count++;
			}

      // Update state
      spinn_state      = Read;
      spinn_state_next = Read;

#ifdef SHOW_PROGRESS
 			if (transfers_k%(MEM_SIZE/BLOCK_SIZE*DMA_REPS*10/100)==0)
 			{
        io_printf(IO_BUF, "[core %d] %3d%% T:%d s\n", coreID,
        											100*transfers_k/(MEM_SIZE/BLOCK_SIZE*DMA_REPS),
        											(int)(spin1_get_simulation_time()*TIMER_TICK_PERIOD/1e6));
 			}
#endif

	  	if (rep_count==DMA_REPS)
			{	
				t2 = sv->clock_ms;
				spinn_state_next = Exit;
			}

			transfers_k++;
			break;

		// -----------------------------------------------------
    // Rewrite corrupted SDRAM block
    // -----------------------------------------------------
		case Rewrite:
      // Reinitialize DTCM
	    initialize_DTCM();

	    // Adjust rep_count counter if the error is in the last block
	    if (block_step==0)
	    	rep_count--;

      // Move pointer back to previous block_step_pre
      block_step = block_step_pre;
      // Rewrite SDRAM block
      do {
				transferid = spin1_dma_transfer_crc(DMA_WRITE,
						sdram_buffer + block_step,
						dtcm_buffer_w,
						DMA_WRITE,
						BLOCK_SIZE*sizeof(uint));
			} while (!transferid);

      spinn_state = Rewrite;
      spinn_state_next = Read;
			break;

    // -----------------------------------------------------
    // Exit
    // -----------------------------------------------------
		case Exit:
			//Printout last DMA read
#ifdef DEBUG_READ
				print_block();
				io_printf(IO_BUF, "\n");
#endif

#ifdef SHOW_PROGRESS
        io_printf(IO_BUF, "[core %d] 100%% T:%d s\n", coreID,	(int)(spin1_get_simulation_time()*TIMER_TICK_PERIOD/1e6));
#endif

			//spin1_exit(0);
			break;
	}

}

// Send SDP packet to host (when pinged by host)
void process_sdp(uint m, uint port)
{
  // sdp_msg_t my_msg;
  // char s[100];
  // uint s_len;

  // // Initialize SDP
  // my_msg.tag       = 1;             // IPTag 1
  // my_msg.dest_port = PORT_ETH;      // Ethernet
  // my_msg.dest_addr = sv->dbg_addr;  // Root chip

  // my_msg.flags     = 0x07;          // Flags = 7
  // my_msg.srce_port = spin1_get_core_id ();  // Source port
  // my_msg.srce_addr = spin1_get_chip_id ();  // Source addr

  sdp_msg_t *msg = (sdp_msg_t *) m;
	io_printf (IO_BUF, "SDP len %d, port %d - %s\n", msg->length, port, msg->data);
  io_printf (IO_STD, "SDP len %d, port %d - %s\n", msg->length, port, msg->data);

	// // Compose message  
 //  strcpy(s, "This is a test");

 //  spin1_memcpy(my_msg.data, (void *)s, s_len);
 //  my_msg.length = sizeof(sdp_hdr_t) + sizeof(cmd_hdr_t) + s_len;

 //  // Send SDP message
 //  (void)spin1_send_sdp_msg(&my_msg, 100); // 100ms timeout

  // Exit only if program executed till the end
  // if (spinn_state_next==Exit)
  // 	spin1_exit(0);
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

// Print out block values
void print_block(void)
{
	if (spinn_state==Read || spinn_state==Exit)
  {
  	//io_printf(IO_BUF, "(R) %2d %2d %2d %2d: ", block_step_pre, block_step, rep_count, transfers_k);
  	io_printf(IO_BUF, "(R) %2d: ", block_step_pre);
  }

	for(uint k=0; k<BLOCK_SIZE; k++)
	{
		if (dtcm_buffer_r[k]==MEM_VALUE)
			io_printf(IO_BUF, "-");
		else
			io_printf(IO_BUF, "*");
		//io_printf(IO_BUF, "%x ", dtcm_buffer_r[k]);
		dtcm_buffer_r[k] = 0;
	}
	//io_printf(IO_BUF, " DMAerr:%d CRCC:%08x CRCR:%08x\n", (dma[DMA_STAT] >> 13)&0x01, dma[DMA_CRCC], dma[DMA_CRCR]);
	io_printf(IO_BUF, " DMAerr:%d\n", (dma[DMA_STAT] >> 13)&0x01);
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
