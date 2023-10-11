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
#include "timers.h"

#include "pdu_def.h"


static TimerHandle_t app_timer = 0;
conn_event_info_t rx_conn_event_infos[MAX_CONCURRENT_CONNECTIONS] = {0};
conn_event_info_t rx_conn_event_infos_bak[MAX_CONCURRENT_CONNECTIONS] = {0};
static signed short current_handle = -1;

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
#define USER_HOST_STATE_INFO         2

static void user_msg_handler(uint32_t msg_id, void *data, uint16_t size)
{
    switch (msg_id)
    {
    case USER_TRIGGER_RX_CH_INFO:
        trigger_rx_info();
        break;
    case USER_HOST_STATE_INFO:
        platform_printf("host OK\r\n");
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

    if (ll_channel_monitor_run(conn->pkt, t, 8000))
    {
        goto next;
    }
    else
    {
        return;
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
    
    memcpy(&rx_conn_event_infos, data, sizeof(rx_conn_event_infos));

    int i;
    int flag = 1;
    uint16_t flag2 = 1;
    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        current_handle = -1;
        conn_rx_infos[i].cur_ch_index = -1;
        if (conn_rx_infos[i].event_info->valid)
        {
            flag = 0;
            current_handle = i;
            rx_conn_event_infos[i].link_num = i;
            if((conn_rx_infos[i].event_info->time_offset > 1000000)|| ((conn_rx_infos[i].event_info->time_offset < 0))||(conn_rx_infos[i].event_info->event_count == rx_conn_event_infos_bak[i].event_count))
            {
                conn_rx_infos[i].cur_ch_index = CHANNEL_INFO_NUM;
                flag2 =0;
            }
            recv_next_conn_data(conn_rx_infos + i);
        }
    }
    if(flag2)
    {
        memcpy(&rx_conn_event_infos_bak, &rx_conn_event_infos, sizeof(rx_conn_event_infos));
    }

    if (flag) btstack_push_user_msg(USER_TRIGGER_RX_CH_INFO, NULL, 0);
}

void channel_monitor_pdu_visitor(int index, int status, uint8_t resevered,
              const void *data, int size, int rssi, void *user_data)
{
    int id = (int)(uintptr_t)user_data;
    struct conn_rx_ctx *conn = conn_rx_infos + id;
    if (status == 0)
    {
        platform_printf("DATA RSSI: @%d (%d) %3ddBm\r\n", id, index, rssi);
        // For example, we only report RSSI for one time on a connection for each round
        conn->cur_ch_index = CHANNEL_INFO_NUM;
    }
}

void on_rx_conn_data_done(struct ll_raw_packet *packet, void *user_data)
{
    int id = (int)(uintptr_t)user_data;
    struct conn_rx_ctx *conn = conn_rx_infos + id;
    uint16_t pdu_num;
    pdu_num = ll_channel_monitor_check_each_pdu(packet, channel_monitor_pdu_visitor, user_data);
    if(pdu_num == 0)
    {
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

    uint64_t t = platform_get_us_time() + 600;
    if (ll_raw_packet_recv(pkt_rx_conn_info, t, 1000 * 1000))
        btstack_push_user_msg(USER_TRIGGER_RX_CH_INFO, NULL, 0);
}

static void init_data(void)
{
    int i;

    pkt_rx_conn_info = ll_raw_packet_alloc(0, on_rx_conn_info_done, NULL);
    ll_raw_packet_set_param(pkt_rx_conn_info, 3, CH_ID, CH_PHY, CH_ACC, CH_CRC);

    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        conn_rx_infos[i].cur_ch_index = CHANNEL_INFO_NUM; // mark as done
        conn_rx_infos[i].pkt = ll_channel_monitor_alloc(2, on_rx_conn_data_done, (void *)(uintptr_t)i);
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
        //xTimerStart(app_timer, portMAX_DELAY);
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

static void app_timer_callback(TimerHandle_t xTimer)
{
    platform_printf("cur_han:%d\r\n", current_handle);
    btstack_push_user_msg(USER_HOST_STATE_INFO, NULL, 0);
}

static btstack_packet_callback_registration_t hci_event_callback_registration;

uint32_t setup_profile(void *data, void *user_data)
{
    platform_printf("setup NODE\n");
        app_timer = xTimerCreate("t1",
                            pdMS_TO_TICKS(5000),
                            pdTRUE,
                            NULL,
                            app_timer_callback);

    // Note: security has not been enabled.
    att_server_init(att_read_callback, att_write_callback);
    hci_event_callback_registration.callback = &user_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    att_server_register_packet_handler(&user_packet_handler);
    return 0;
}

