#ifndef RANBOOSTER_COMMON_H_
#define RANBOOSTER_COMMON_H_

#include <linux/if_ether.h>
#include <stdbool.h>
#include <stdint.h>

#include <immintrin.h>

#include "xran_pkt_up.h"
#include "xran_pkt_cp.h"

#define ECPRI_IQ_DATA 0x00  // eCPRI Message Type for IQ Data
#define ECPRI_RT_CONTROL_DATA \
  0x02                          // eCPRI Message Type for Real-Time Control Data

#define NUM_PRB (273)
#define MAX_PDSCH_PUSCH_PORT (4)

#define NUM_SYMBOLS (14)
#define NUM_SUBFRAMES (10)
#define NUM_SLOTS (2)
#define NUM_ANTENNA_PORTS (8)

#define SCS_MHZ (0.03)

#define UPLINK_DIRECTION (1)
#define DOWNLINK_DIRECTION (2)

#define NUM_SUBCARRIERS_PRB (12)
#define COMP_PARAM_HEADER_SIZE (1)
#define IQ_BIT_WIDTH_COMPRESSED (9)
#define IQ_BIT_WIDTH_UNCOMPRESSED (16)

#define SELECTED_RU_PORT_ID (0)
#define MAX_NUM_RBS (273)
#define COMPRESSED_RB_SIZE_BYTES (28)

#define MONITORING_SYMBOL_INGRESS (0)
#define MONITORING_SYMBOL_EGRESS (8)


struct prb_stats_event 
{
    __u32 num_prbs_used;
    __u32 total_num_prbs;
};

#endif
