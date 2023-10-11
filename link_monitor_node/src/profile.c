#include <stdio.h>
#include <string.h>
#include "platform_api.h"
#include "att_db.h"
#include "gap.h"
#include "btstack_event.h"
#include "btstack_defines.h"
#include "ll_api.h"
#include "bluetooth_hci.h"

#include "FreeRTOS.h"
#include "task.h"

#include "../../link_monitor_ctrl/src/pdu_def.h"

conn_event_info_t rx_conn_event_infos[MAX_CONCURRENT_CONNECTIONS] = {0};

struct ll_raw_packet *pkt_rx_conn_info = NULL;

uint64_t rx_conn_info_air_time = 0;

struct conn_rx_ctx
{
    int                      cur_ch_index;
    struct ll_raw_packet    *pkt;
    const conn_event_info_t *event_info;
};

struct conn_rx_ctx conn_rx_infos[MAX_CONCURRENT_CONNECTIONS] = { 0 };

static uint16_t att_read_callback(hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset,
                                  uint8_t * buffer, uint16_t buffer_size)
{
    switch (att_handle)
    {

    default:
        return 0;
    }
}

static int att_write_callback(hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode,
                              uint16_t offset, const uint8_t *buffer, uint16_t buffer_size)
{
    switch (att_handle)
    {

    default:
        return 0;
    }
}

void trigger_rx_info(void);

#define USER_TRIGGER_RX_CH_INFO         1

static void user_msg_handler(uint32_t msg_id, void *data, uint16_t size)
{
    switch (msg_id)
    {
    case USER_TRIGGER_RX_CH_INFO:
        trigger_rx_info();
        break;
    default:
        ;
    }
}

uint8_t ch_id_to_linear_id(uint8_t id)
{
    switch (id)
    {
    case 37:
        return 0;
    case 38:
        return 12;
    case 39:
        return 39;
    default:
        if (id <= 10)
            return id + 1;
        else
            return id + 2;
    }
}

void recv_next_conn_data(struct conn_rx_ctx *conn)
{
    if (conn->event_info->valid == 0) return;

next:
    conn->cur_ch_index++;

    // give up earlier
    if (conn->cur_ch_index >= 5) conn->cur_ch_index = CHANNEL_INFO_NUM;

    if (conn->cur_ch_index >= CHANNEL_INFO_NUM)
    {
        btstack_push_user_msg(USER_TRIGGER_RX_CH_INFO, NULL, 0);
        return;
    }

    ll_raw_packet_set_param(conn->pkt, 0,
                          ch_id_to_linear_id(conn->event_info->channels[conn->cur_ch_index]),
                          conn->event_info->phy,
                          conn->event_info->access_addr,
                          conn->event_info->crc_init);
    uint64_t t = rx_conn_info_air_time + conn->event_info->time_offset;
    t += conn->cur_ch_index * conn->event_info->interval;
    t -= 1000; // go backward for 1ms for clock drifting

    // why 20000? max packet length is coded S8 PDU which takes 17040us to transmit.
    if (ll_ackable_packet_run(conn->pkt, t, 20000))
    {
        // platform_printf("re-try\n");
        goto next;
    }
}

void on_rx_conn_info_done(struct ll_raw_packet *packet, void *user_data)
{
    uint8_t header;
    int len = 0;
    int rssi;
    static uint8_t data[255] = {0};

    if ((ll_raw_packet_get_rx_data(pkt_rx_conn_info, &rx_conn_info_air_time, &header, data, &len, &rssi) != 0)
        || (len != sizeof(rx_conn_event_infos)))
    {
        platform_printf("Rx error\n");
        btstack_push_user_msg(USER_TRIGGER_RX_CH_INFO, NULL, 0);
        return;
    }

    // platform_printf("T: %llu\n", rx_conn_info_air_time);

    memcpy(&rx_conn_event_infos, data, sizeof(rx_conn_event_infos));

    int i;
    int flag = 1;
    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        conn_rx_infos[i].cur_ch_index = -1;
        if (conn_rx_infos[i].event_info->valid)
        {
            flag = 0;
            recv_next_conn_data(conn_rx_infos + i);
        }
    }

    if (flag) btstack_push_user_msg(USER_TRIGGER_RX_CH_INFO, NULL, 0);
}

void on_rx_conn_data_done(struct ll_raw_packet *packet, void *user_data)
{
    uint64_t air_time;
    int len;
    int rssi;
    int acked;
    static uint8_t data[255] = {0};
    int id = (int)(uintptr_t)user_data;
    struct conn_rx_ctx *conn = conn_rx_infos + id;

    int rx_status = ll_ackable_packet_get_status(packet, &acked, &air_time, data, &len, &rssi);

    if (rx_status == 0)
    {
        platform_printf("DATA RSSI: @%d %3ddBm\n", id, rssi);
        // For example, we only report RSSI for one time on a connection for each round
        conn->cur_ch_index = CHANNEL_INFO_NUM;
    }

    recv_next_conn_data(conn);
}

void trigger_rx_info(void)
{
    int i;
    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        if (conn_rx_infos[i].event_info->valid && (conn_rx_infos[i].cur_ch_index < CHANNEL_INFO_NUM))
            return;
    }

    uint64_t t = platform_get_us_time() + 300;
    while (ll_raw_packet_recv(pkt_rx_conn_info, t, 500 * 1000))
        t += 150;
}

static void init_data(void)
{
    int i;

    pkt_rx_conn_info = ll_raw_packet_alloc(0, on_rx_conn_info_done, NULL);
    ll_raw_packet_set_param(pkt_rx_conn_info, 3, CH_ID, CH_PHY, CH_ACC, CH_CRC);

    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        conn_rx_infos[i].cur_ch_index = CHANNEL_INFO_NUM; // mark as done
        conn_rx_infos[i].pkt = ll_ackable_packet_alloc(0, on_rx_conn_data_done, (void *)(uintptr_t)i);
        conn_rx_infos[i].event_info = rx_conn_event_infos + i;
    }
}

static void user_packet_handler(uint8_t packet_type, uint16_t channel, const uint8_t *packet, uint16_t size)
{
    uint8_t event = hci_event_packet_get_type(packet);
    const btstack_user_msg_t *p_user_msg;
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (event)
    {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING)
            break;
        init_data();
        trigger_rx_info();
        break;

    case HCI_EVENT_COMMAND_COMPLETE:
        break;

    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet))
        {
        default:
            break;
        }

        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE:
        break;

    case ATT_EVENT_CAN_SEND_NOW:
        // add your code
        break;

    case BTSTACK_EVENT_USER_MSG:
        p_user_msg = hci_event_packet_get_user_msg(packet);
        user_msg_handler(p_user_msg->msg_id, p_user_msg->data, p_user_msg->len);
        break;

    default:
        break;
    }
}

static btstack_packet_callback_registration_t hci_event_callback_registration;

uint32_t setup_profile(void *data, void *user_data)
{
    platform_printf("setup NODE\n");

    // Note: security has not been enabled.
    att_server_init(att_read_callback, att_write_callback);
    hci_event_callback_registration.callback = &user_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    att_server_register_packet_handler(&user_packet_handler);
    return 0;
}

