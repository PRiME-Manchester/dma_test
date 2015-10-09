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

#define TIMER_TICK_PERIOD  1000000
#define TOTAL_TICKS        4
#define ITER_TIMES         1

#define BUFFER_SIZE        4000
#define DMA_REPS           10000

#define NODEBUG

// ------------------------------------------------------------------------
// variables
// ------------------------------------------------------------------------

uint coreID;
uint chipID;
uint test_DMA;
uint dma_errors=0;

uint packets = 0;
uint pfailed = 0;

uint transfers = 0;
uint tfailed = 0;
uint tfaddr = 0;
uint tfvald;
uint tfvals;

uint ufailed = 0;

uint *dtcm_buffer;
uint *sdram_buffer;

uint count = 0, count2 = 0, count_failed = 0;
uint sdram_tmp;


// ---------------------------------------------
// Function prototypes
// ---------------------------------------------

void app_init ();
void app_done ();
void send_packets (uint ticks, uint none);
void flip_led (uint ticks, uint null);
void reverse(char *s, int len);
uint itoa(uint num, char s[], uint len);
void ftoa(float n, char *res, int precision);
void do_transfer (uint val, uint none);
void count_packets (uint key, uint payload);
void check_memcopy(uint tid, uint ttag);
void configure_crc_tables(void);


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

  spin1_callback_on (MCPL_PACKET_RECEIVED, count_packets, -1);
  //spin1_callback_on (DMA_TRANSFER_DONE, check_memcopy, 0);
  spin1_callback_on (USER_EVENT, send_packets, 2);
  spin1_callback_on (TIMER_TICK, flip_led, 3);

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
  /* initialise routing entries                                          */
  /* ------------------------------------------------------------------- */
  /* set a MC routing table entry to send my packets back to me */

  uint e = rtr_alloc (1);
  if (e == 0)
    rt_error (RTE_ABORT);

  rtr_mc_set (e,			// entry
	      coreID, 			// key
	      0xffffffff,		// mask
	      MC_CORE_ROUTE (coreID)	// route
	      );

  /* ------------------------------------------------------------------- */
  /* initialize the application processor resources                      */
  /* ------------------------------------------------------------------- */
  /* say hello */

  io_printf (IO_BUF, "[core %d] -----------------------\n", coreID);
  io_printf (IO_BUF, "[core %d] starting simulation\n", coreID);

  // Allocate a buffer in SDRAM
  sdram_buffer = (uint *) sark_xalloc (sv->sdram_heap,
					(BUFFER_SIZE+1) * sizeof(uint),
					0,
					ALLOC_LOCK);

  // and a buffer in DTCM
  dtcm_buffer = (uint *) sark_alloc (BUFFER_SIZE+1, sizeof(uint));

  if (dtcm_buffer == NULL ||  sdram_buffer == NULL)
  {
    test_DMA = FALSE;
    io_printf (IO_BUF, "[core %d] error - cannot allocate buffer\n", coreID);
  }
  else
  {
    test_DMA = TRUE;
    // initialize sections of DTCM, system RAM and SDRAM
    for (uint i = 0; i < BUFFER_SIZE+1; i++)
    {
      dtcm_buffer[i]   = BUFFER_SIZE-i;
      //sysram_buffer[i] = 0xa5a5a5a5;
      //sdram_buffer[i]  = 0x5a5a5a5a;
      sdram_buffer[i]  = 0;
    }

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
  // report simulation time
  io_printf (IO_BUF, "[core %d] simulation lasted %d ticks\n", coreID,
             spin1_get_simulation_time());

  // report number of packets
  io_printf (IO_BUF, "[core %d] received %d packets\n", coreID, packets);

  // report number of DMA errors
  io_printf (IO_BUF, "[core %d] failed %d DMA transfers\n", coreID, dma_errors);

/*
  // report number of failed packets
  io_printf (IO_BUF, "[core %d] failed %d packets\n", coreID, pfailed);

  // report number of failed user events
  io_printf (IO_BUF, "[core %d] failed %d USER events\n", coreID, ufailed);

  // report number of DMA transfers
  io_printf (IO_BUF, "[core %d] completed %d DMA transfers\n", coreID, transfers);

  // report number of failed transfers
  io_printf (IO_BUF, "[core %d] failed %d DMA transfers\n", coreID, tfailed);

  if (tfailed)
  {
    io_printf (IO_BUF, "\t%d : %d @ %d\n", tfvald, tfvals, tfaddr);

    io_printf (IO_BUF, "\t%d : %d @ %d\n", dtcm_buffer[tfaddr],
               sdram_buffer[tfaddr], tfaddr);
  }
*/

  // say goodbye
  io_printf (IO_BUF, "[core %d] stopping simulation\n", coreID);
  io_printf (IO_BUF, "[core %d] -------------------\n", coreID);
}


/****f* dma_test.c/send_packets
*
* SUMMARY
*  This function is used by a core to send packets to itself.
*  It's used to test the triggering of USER_EVENT
*
* SYNOPSIS
*  void send_packets (uint null_a, uint null_b)
*
* INPUTS
*   uint null_a: padding - all callbacks must have two uint arguments!
*   uint null_b: padding - all callbacks must have two uint arguments!
*
* SOURCE
*/
void send_packets (uint ticks, uint none)
{
  for (uint i = 0; i < ITER_TIMES; i++)
  {
    //io_printf(IO_BUF, "send packet %d\n", i);
    if (! spin1_send_mc_packet (coreID, ticks, 1))
      pfailed++;
  }
}


/****f* dma_test.c/flip_led
*
* SUMMARY
*  This function is used as a callback for timer tick events.
*  It inverts the state of LED 1 and sends packets.
*
* SYNOPSIS
*  void flip_led (uint ticks, uint null)
*
* INPUTS
*   uint ticks: simulation time (in ticks) - provided by the RTS
*   uint null: padding - all callbacks must have two uint arguments!
*
* SOURCE
*/
void flip_led (uint ticks, uint null)
{
  // flip led 1
  // Only 1 core should flip leds!
  if (leadAp)
    spin1_led_control (LED_INV (1));

  // trigger user event to send packets
  if (spin1_trigger_user_event(ticks, NULL) == FAILURE)
    ufailed++;

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


/****f* dma_test.c/do_transfer
*
* SUMMARY
*  This function is used as a task example
*  It triggers a dma transfers.
*
* SYNOPSIS
*  void do_transfer (uint val, uint none)
*
* INPUTS
*   uint val: argument
*   uint none: padding argument - task must have 2
*
* SOURCE
*/
void do_transfer (uint val, uint none)
{
  uint transfer_id, crc_error;
  uint t1, t2;
  char tput_s[20], mb_s[20];

  if (val == 1)
  {
    io_printf(IO_BUF, "\n- Performing DMA Write (DTCM->SDRAM)\n");
    transfer_id = spin1_dma_transfer_crc(DMA_WRITE, sdram_buffer, dtcm_buffer, DMA_WRITE, BUFFER_SIZE*sizeof(uint));
    
    // Wait for DMA operation to finish
    while((dma[DMA_STAT]&0x01));
  }
  else if (val == 2)
  {
    io_printf(IO_BUF, "- Performing DMA Read (SDRAM->DTCM)\n");
    
    t1 = sv->clock_ms;
    
    for(uint i=0; i<DMA_REPS; i++)
    {
      if(coreID==2 && i==5)
      {
        io_printf(IO_BUF, "- Corrupting SDRAM %d\n", i);
        //sdram_buffer[402] ^= sdram_buffer[402]; //0x0f0f0f0f;
        sdram_tmp = sdram_buffer[400];
        sdram_buffer[400] = 0xf0f0f0f0;
      }
      else if (coreID==2 && i==8)
      {
        sdram_buffer[400] = sdram_tmp;
      }
      
#ifdef DEBUG      
      io_printf(IO_BUF, "i:%d DMA Status: 0x%08x\n", i, dma[DMA_STAT]);
#endif

      transfer_id = spin1_dma_transfer_crc(DMA_READ, sdram_buffer, dtcm_buffer, DMA_READ, BUFFER_SIZE*sizeof(uint));

#ifdef DEBUG
      io_printf(IO_BUF, "i:%d tid:%d DMA Status: 0x%08x\n", i, transfer_id, dma[DMA_STAT]);
#endif

      // Wait for DMA operation to finish
      while((dma[DMA_STAT]&0x01));
#ifdef DEBUG
      io_printf(IO_BUF, "i:%d DMA Status: 0x%08x\n", i, dma[DMA_STAT]);
#endif

      crc_error = (dma[DMA_STAT] >> 13)&0x01;

      if (crc_error)  
      {
        dma_errors++;

        // clear DMA errors and restart DMA controller
        spin1_dma_clear_errors();
        
        io_printf(IO_BUF, "CRCC:%08x CRCR:%08x CRC error:%d\n", dma[DMA_CRCC], dma[DMA_CRCR], crc_error);
        //io_printf(IO_BUF, "t2-t1:%d ms\n", t2-t1);
#ifdef DEBUG
        io_printf(IO_BUF, "DMA Status: 0x%08x\n", dma[DMA_STAT]);
#endif
      }

    }
    t2 = sv->clock_ms;
    
    ftoa(BUFFER_SIZE*4.0*DMA_REPS/(t2-t1)/1e3, tput_s, 2);
    ftoa(BUFFER_SIZE*4.0*DMA_REPS/1e6, mb_s, 2);
    io_printf(IO_BUF, "Throughput: %s MB/s (%s MB in %d ms) Transfers:%d\n\n", tput_s, mb_s, t2-t1, transfers);
  }
}


/****f* dma_test.c/count_packets
*
* SUMMARY
*  This function is used as a callback for packet received events.
*  It counts the number of received packets and triggers dma transfers.
*
* SYNOPSIS
*  void count_packets (uint key, uint payload)
*
* INPUTS
*   uint key: packet routing key - provided by the RTS
*   uint payload: packet payload - provided by the RTS
*
* SOURCE
*/
void count_packets (uint key, uint payload)
{
  uint transfer_id;

  // count packets
  packets++;

  // if testing DMA trigger transfer requests at the right time
  if (test_DMA)
  {
    // schedule task (with priority 1) to trigger DMA
    //if (payload==3) count++;
    if (spin1_schedule_callback (do_transfer, payload, 0, 1))
        count++;
  }
}


/****f* dma_test.c/check_memcopy
*
* SUMMARY
*  This function is used as a callback for dma done events.
*  It counts the number of transfers and checks that
*  data was copied correctly.
*
* SYNOPSIS
*  void check_memcopy(uint tid, uint ttag)
*
* INPUTS
*   uint tid: transfer ID - provided by the RTS
*   uint ttag: transfer tag - provided by application
*
* SOURCE
*/
void check_memcopy(uint tid, uint ttag)
{
  // count transfers

  transfers++;

  // check if DMA transfer completed correctly

  if (tid <= ITER_TIMES)
  {
/*    for (uint i = 0; i < BUFFER_SIZE; i++)
    {
      //io_printf(IO_BUF, "A: %d %d %x %x\n", count, i, dtcm_buffer[i], sysram_buffer[i]);
      if (dtcm_buffer[i] != sysram_buffer[i])
      {
        tfailed++;
        tfaddr = i;
        break;
      }
    }*/
    //count++;
  }

  if (tid > ITER_TIMES)
  {
    for (uint i = 0; i < BUFFER_SIZE; i++)
    {
      tfvald = dtcm_buffer[i];
      tfvals = sdram_buffer[i];
      //io_printf(IO_BUF, "B: %x %x\n", dtcm_buffer[i], sdram_buffer[i]);      
      if (tfvald != tfvals)
      {
        tfailed++;
        tfaddr = i;
        break;
      }
    }
  }
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

