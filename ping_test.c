#include "spin1_api.h"
#include <string.h>

#define TIMER_TICK_PERIOD 10000

void process_sdp(uint m, uint port);
void swap_sdp_hdr (sdp_msg_t *msg);

uint k;
char s[100];
sdp_msg_t my_msg;

void c_main()
{
	spin1_set_timer_tick (TIMER_TICK_PERIOD);

	// Register callback for when an SDP packet is received
	spin1_callback_on (SDP_PACKET_RX, process_sdp, 1);
  
  // Go
  spin1_start (SYNC_NOWAIT);
}

// Send SDP packet to host (when pinged by host)
void process_sdp(uint m, uint port)
{
  sdp_msg_t *msg = (sdp_msg_t *) m;
  int s_len;
  
  if (port == 1) // Port 1 - echo to sender
  {
    swap_sdp_hdr(msg);

    //strcpy(s, "X,Y,Errs,T1,B1,T2,B2,T3,B3,T4,B4");
    io_printf(s, "%d:X,Y,Errs,T1,B1,T2,B2,T3,B3,T4,B4", ++k);
	  s_len = strlen(s);
	  spin1_memcpy(msg->data, (void *)s, s_len);
  
	  msg->length = sizeof(sdp_hdr_t) + sizeof(cmd_hdr_t) + s_len;
		(void) spin1_send_sdp_msg (msg, 10);
  }

	spin1_msg_free (msg);

  // Exit only if program executed till the end
  // if (spinn_state_next==Exit)
  // 	spin1_exit(0);
}

void swap_sdp_hdr (sdp_msg_t *msg)
{
  uint dest_port = msg->dest_port;
  uint dest_addr = msg->dest_addr;

  msg->dest_port = msg->srce_port;
  msg->srce_port = dest_port;

  msg->dest_addr = msg->srce_addr;
  msg->srce_addr = dest_addr;
}
