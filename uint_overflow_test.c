#include "spin1_api.h"
#include <string.h>

// ------------------------------------------------------------------------
// simulation constants
// ------------------------------------------------------------------------

// 10ms timer tick
#define TIMER_TICK_PERIOD  10000

#define DMA_REPS           10000
#define BLOCK_SIZE         1000
#define MEM_SIZE           ((BLOCK_SIZE+1)*28000)
#define TRANSFERS_T        (MEM_SIZE/BLOCK_SIZE*DMA_REPS)
#define TIMER_CONV         (1000000/TIMER_TICK_PERIOD)

void app_init();

// chip and core IDs
uint coreID;
uint chipID;

long long transfers_k = 50000000;
uint t=450000;

void c_main()
{
	// Get core and chip IDs
  coreID = spin1_get_core_id();
  chipID = spin1_get_chip_id();

  // Set timer tick value (in microseconds)
  spin1_set_timer_tick(TIMER_TICK_PERIOD);

  // Initialize application
  app_init();

  // Go
  spin1_start(SYNC_NOWAIT);
}

void app_init()
{
	double percent_done;
	percent_done = 100.0*transfers_k/((MEM_SIZE)/BLOCK_SIZE*DMA_REPS);

  io_printf(IO_BUF, "MEM_SIZE:%d\n", MEM_SIZE);
  io_printf(IO_BUF, "BLOCK_SIZE:%d\n", BLOCK_SIZE);
  io_printf(IO_BUF, "DMA_REPS:%d\n", DMA_REPS);
  io_printf(IO_BUF, "TIMER_CONV:%d\n", TIMER_CONV);
  io_printf(IO_BUF, "TRANSFERS_T:%d transfers_k:%d\n\n", TRANSFERS_T, (int)transfers_k);

  io_printf(IO_BUF, "t:%d %d%% %d%%\n", t/TIMER_CONV, (int)percent_done,
              100*transfers_k/TRANSFERS_T );
}



