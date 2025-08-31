#ifndef RANBOOSTER_COMMON_H_
#define RANBOOSTER_COMMON_H_

#include <linux/if_ether.h>
#include <stdbool.h>
#include <stdint.h>

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

struct xran_ecpri_hdr 
{
    union xran_ecpri_cmn_hdr cmnhdr;
    __u16 ecpri_xtc_id;               
    union ecpri_seq_id ecpri_seq_id;
} __attribute__((packed));

struct radio_app_common_hdr
{
   /* Octet 9 */
    union {
        __u8 value;
        struct {
            __u8 filter_id:4; /**< This parameter defines an index to the channel filter to be
                              used between IQ data and air interface, both in DL and UL.
                              For most physical channels filterIndex =0000b is used which
                              indexes the standard channel filter, e.g. 100MHz channel filter
                              for 100MHz nominal carrier bandwidth. (see 5.4.4.3 for more) */
            __u8 payl_ver:3; /**< This parameter defines the payload protocol version valid
                            for the following IEs in the application layer. In this version of
                            the specification payloadVersion=001b shall be used. */
            __u8 data_direction:1; /**< This parameter indicates the gNB data direction. */
        };
    }data_feature;

   /* Octet 10 */
   __u8 frame_id:8;    /**< This parameter is a counter for 10 ms frames (wrapping period 2.56 seconds) */

   /* Octet 11 */
   /* Octet 12 */
   union {
       __u16 value;
       struct {
           __u16 symb_id:6; /**< This parameter identifies the first symbol number within slot,
                                          to which the information of this message is applies. */
           __u16 slot_id:6; /**< This parameter is the slot number within a 1ms sub-frame. All slots in
                                   one sub-frame are counted by this parameter, slotId running from 0 to Nslot-1.
                                   In this version of the specification the maximum Nslot=16, All
                                   other values of the 6 bits are reserved for future use. */
           __u16 subframe_id:4; /**< This parameter is a counter for 1 ms sub-frames within 10ms frame. */
       };
   }sf_slot_sym;

} __attribute__((packed));

struct compression_hdr
{
    __u8 ud_comp_meth:4;
    /**< udCompMeth|  compression method         |udIqWidth meaning
    ---------------+-----------------------------+--------------------------------------------
    0000b          | no compression              |bitwidth of each uncompressed I and Q value
    0001b          | block floating point        |bitwidth of each I and Q mantissa value
    0010b          | block scaling               |bitwidth of each I and Q scaled value
    0011b          | mu-law                      |bitwidth of each compressed I and Q value
    0100b          | modulation compression      |bitwidth of each compressed I and Q value
    0100b - 1111b  | reserved for future methods |depends on the specific compression method
    */
    __u8 ud_iq_width:4; /**< Bit width of each I and each Q
                                16 for udIqWidth=0, otherwise equals udIqWidth e.g. udIqWidth = 0000b means I and Q are each 16 bits wide;
                                e.g. udIQWidth = 0001b means I and Q are each 1 bit wide;
                                e.g. udIqWidth = 1111b means I and Q are each 15 bits wide
                                */
} __attribute__((packed));;

struct data_section_compression_hdr
{
    struct compression_hdr ud_comp_hdr;
    __u8 rsrvd; /**< This parameter provides 1 byte for future definition,
    should be set to all zeros by the sender and ignored by the receiver.
    This field is only present when udCompHdr is present, and is absent when
    the static IQ format and compression method is configured via the M-Plane */

    /* TODO: support for Block Floating Point compression */
    /* udCompMeth  0000b = no compression	absent*/
};

struct data_section_hdr 
{
    union {
        __u32 all_bits;
        struct {
            __u32     num_prbu:8;    /**< 5.4.5.6 number of contiguous PRBs per control section */
            __u32     start_prbu:10; /**< 5.4.5.4 starting PRB of control section */
            __u32     sym_inc:1;     /**< 5.4.5.3 symbol number increment command XRAN_SYMBOLNUMBER_xxxx */
            __u32     rb:1;          /**< 5.4.5.2 resource block indicator, XRAN_RBIND_xxx */
            __u32     sect_id:12;    /**< 5.4.5.1 section identifier */
        };
    }fields;
} __attribute__((packed));

struct compression_params
{
    __u8 exponent:4;
    __u8 reserved:4;
}__attribute__((packed));

struct xran_cp_radioapp_common_header {     /* 6bytes, first 4bytes need the conversion for byte order */
    union {
        uint32_t all_bits;
        struct {
            uint32_t    startSymbolId:6;        /**< 5.4.4.7 start symbol identifier */
            uint32_t    slotId:6;               /**< 5.4.4.6 slot identifier */
            uint32_t    subframeId:4;           /**< 5.4.4.5 subframe identifier */
            uint32_t    frameId:8;              /**< 5.4.4.4 frame identifier */
            uint32_t    filterIndex:4;          /**< 5.4.4.3 filter index, XRAN_FILTERINDEX_xxxx */
            uint32_t    payloadVer:3;           /**< 5.4.4.2 payload version, should be 1 */
            uint32_t    dataDirection:1;        /**< 5.4.4.1 data direction (gNB Tx/Rx) */
        };
    } field;
    uint8_t     numOfSections;          /**< 5.4.4.8 number of sections */
    uint8_t     sectionType;            /**< 5.4.4.9 section type */
    } __attribute__((__packed__));

struct xran_radioapp_udComp_header {
    uint8_t     udCompMeth:4;           /**< Compression method, XRAN_COMPMETHOD_xxxx */
    uint8_t     udIqWidth:4;            /**< IQ bit width, 1 ~ 16 */
    } __attribute__((__packed__));

struct xran_cp_radioapp_section1_header {   // 8bytes (6+1+1)
    struct xran_cp_radioapp_common_header cmnhdr;
    struct xran_radioapp_udComp_header udComp;
    uint8_t     reserved;
    } __attribute__((__packed__));

struct xran_cp_radioapp_section_header {    /* 8bytes, need the conversion for byte order */
    union {
        uint32_t first_4byte;
        struct {
            uint32_t    reserved:16;
            uint32_t    numSymbol:4;    /**< 5.4.5.7 number of symbols */
            uint32_t    reMask:12;      /**< 5.4.5.5 resource element mask */
            } s0;
        struct {
            uint32_t     beamId:15;     /**< 5.4.5.9 beam identifier */
            uint32_t     ef:1;          /**< 5.4.5.8 extension flag */
            uint32_t     numSymbol:4;   /**< 5.4.5.7 number of symbols */
            uint32_t     reMask:12;     /**< 5.4.5.5 resource element mask */
            } s1;
        struct {
            uint32_t    beamId:15;      /**< 5.4.5.9 beam identifier */
            uint32_t    ef:1;           /**< 5.4.5.8 extension flag */
            uint32_t    numSymbol:4;    /**< 5.4.5.7 number of symbols */
            uint32_t    reMask:12;      /**< 5.4.5.5 resource element mask */
            } s3;
        struct {
            uint32_t    ueId:15;        /**< 5.4.5.10 UE identifier */
            uint32_t    ef:1;           /**< 5.4.5.8 extension flag */
            uint32_t    numSymbol:4;    /**< 5.4.5.7 number of symbols */
            uint32_t    reMask:12;      /**< 5.4.5.5 resource element mask */
            } s5;
        } u;
    union {
        uint32_t second_4byte;
        struct {
            uint32_t    numPrbc:8;              /**< 5.4.5.6 number of contiguous PRBs per control section  0000 0000b = all PRBs */
            uint32_t    startPrbc:10;           /**< 5.4.5.4 starting PRB of control section */
            uint32_t    symInc:1;               /**< 5.4.5.3 symbol number increment command XRAN_SYMBOLNUMBER_xxxx */
            uint32_t    rb:1;                   /**< 5.4.5.2 resource block indicator, XRAN_RBIND_xxx */
            uint32_t    sectionId:12;           /**< 5.4.5.1 section identifier */
            } common;
        } u1;
    } __attribute__((__packed__));

struct xran_cp_radioapp_section1 {          // 8bytes (4+4)
    struct xran_cp_radioapp_section_header hdr;

    // section extensions               // 5.4.6 & 5.4.7
    //  .........
    } __attribute__((__packed__));

struct xran_cp_radioapp_frameStructure {
    uint8_t     uScs:4;                 /**< sub-carrier spacing, XRAN_SCS_xxx */
    uint8_t     fftSize:4;              /**< FFT size,  XRAN_FFTSIZE_xxx */
    } __attribute__((__packed__));

struct xran_cp_radioapp_section3_header {   // 12bytes (6+2+1+2+1)
    struct xran_cp_radioapp_common_header cmnhdr;
    uint16_t    timeOffset;             /**< 5.4.4.12 time offset */

    struct xran_cp_radioapp_frameStructure  frameStructure;
    uint16_t    cpLength;               /**< 5.4.4.14 cyclic prefix length */
    struct xran_radioapp_udComp_header udComp;
    } __attribute__((__packed__));

struct xran_cp_radioapp_section3 {          // 12bytes (4+4+4)
    struct xran_cp_radioapp_section_header hdr;
    uint32_t    freqOffset:24;          /**< 5.4.5.11 frequency offset */
    uint32_t    reserved:8;

    // section extensions               // 5.4.6 & 5.4.7
    //  .........
    } __attribute__((__packed__));

#endif
