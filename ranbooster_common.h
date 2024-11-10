#ifndef RANBOOSTER_COMMON_H_
#define RANBOOSTER_COMMON_H_

#include <linux/if_ether.h>

#define ECPRI_IQ_DATA 0x00  // eCPRI Message Type for IQ Data
#define ECPRI_RT_CONTROL_DATA \
  0x02                          // eCPRI Message Type for Real-Time Control Data

union xran_ecpri_cmn_hdr
{
    struct
    {
        __u8     ecpri_concat:1;     
        __u8     ecpri_resv:3;      
        __u8     ecpri_ver:4;       
        __u8     ecpri_mesg_type;    
        __u16    ecpri_payl_size;   
    } bits;
    struct
    {
        __u32    data_num_1;
    } data;
} __attribute__((packed));

union ecpri_seq_id
{
    struct
    {
        __u8 seq_id:8;     
        __u8 sub_seq_id:7;  
        __u8 e_bit:1;        
    } bits;
    struct
    {
        __u16 data_num_1;
    } data;
} __attribute__((packed));;

struct xran_ecpri_hdr {
    union xran_ecpri_cmn_hdr cmnhdr;
    __u16 ecpri_xtc_id;               
    union ecpri_seq_id ecpri_seq_id;
} __attribute__((packed));

#endif