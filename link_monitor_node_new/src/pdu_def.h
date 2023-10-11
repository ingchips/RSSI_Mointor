#pragma once
#ifndef CENTRAL_CONCURRENT_CONNECTIONS
#define CENTRAL_CONCURRENT_CONNECTIONS  2
#endif
#ifndef PERIPHERAL_CONCURRENT_CONNECTIONS
#define PERIPHERAL_CONCURRENT_CONNECTIONS  2
#endif
#ifndef MAX_CONCURRENT_CONNECTIONS
#define MAX_CONCURRENT_CONNECTIONS  (PERIPHERAL_CONCURRENT_CONNECTIONS+CENTRAL_CONCURRENT_CONNECTIONS)
#endif

#define CH_PHY                  2
#define CH_ID                   5
#define CH_ACC                  0x8E89BED6
#define CH_CRC                  0x916918

#define CHANNEL_INFO_NUM        10

#pragma pack (push, 1)

// NOTE: encryption and integrity checking should be considered.
typedef struct
{
    uint32_t access_addr;
    uint32_t crc_init;
    uint32_t time_offset;
    uint32_t interval;
    uint16_t event_count;
    uint8_t link_num;
    uint8_t phy;
    uint8_t valid;
    uint8_t channels[CHANNEL_INFO_NUM];
} conn_event_info_t;

#pragma pack (pop)
