#include <stdio.h>
#include <string.h>
#include "platform_api.h"
#include "att_db.h"
#include "gap.h"
#include "btstack_event.h"
#include "btstack_defines.h"
#include "ll_api.h"
#include "bluetooth_hci.h"
#include "l2cap.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "pdu_def.h"

#define INVALID_HANDLE  0xffff

// GATT characteristic handles
#include "../data/gatt.const"

const static uint8_t adv_data[] = {
    #include "../data/advertising.adv"
};

#include "../data/advertising.const"

const static uint8_t scan_data[] = {
    #include "../data/scan_response.adv"
};

#include "../data/scan_response.const"

const static uint8_t profile_data[] = {
    #include "../data/gatt.profile"
};

static const scan_phy_config_t configs[2] =
{
    {
        .phy = PHY_1M,
        .type = SCAN_PASSIVE,
        .interval = 300,
        .window = 50
    },
    {
        .phy = PHY_CODED,
        .type = SCAN_PASSIVE,
        .interval = 300,
        .window = 50
    }
};

static initiating_phy_config_t phy_configs[] =
{
    {
        .phy = PHY_1M,
        .conn_param =
        {
            .scan_int = 200,
            .scan_win = 100,
            .interval_min = 350,
            .interval_max = 350,
            .latency = 0,
            .supervision_timeout = 500,
            .min_ce_len = 4,
            .max_ce_len = 10
        }
    },
    {
        .phy = PHY_CODED,
        .conn_param =
        {
            .scan_int = 150,
            .scan_win = 100,
            .interval_min = 350,
            .interval_max = 350,
            .latency = 0,
            .supervision_timeout = 800,
            .min_ce_len = 4,
            .max_ce_len = 4
        }
    }
};

conn_event_info_t tx_conn_event_infos[MAX_CONCURRENT_CONNECTIONS] = { 0 };

hci_con_handle_t connected_handles[MAX_CONCURRENT_CONNECTIONS];

typedef struct le_meta_event_vendor_channel_map_update
{
    uint16_t conn_handle;
    uint8_t  channel_map[5];
}le_meta_event_vendor_channel_map_update_t;

typedef struct
{
    uint8_t  role;
    hci_con_handle_t con_handle;
}ble_info_t;

struct ll_raw_packet *pkt_tx_conn_info = NULL;

typedef struct{
    uint16_t central_con_num;
    uint16_t peripheral_con_num;
    ble_info_t ble_info[MAX_CONCURRENT_CONNECTIONS];
}app_info_t;

volatile app_info_t t_app = {
                    .central_con_num = 0,
                    .peripheral_con_num=0
                    };
volatile uint8_t is_initiating = 0;
static TimerHandle_t app_timer = NULL;                    
uint8_t pkt_busy = 0;

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

#define USER_TRIGGER_BROADCAST        1
#define USER_TRIGGER_BLE_ADV          2
#define USER_MSG_INITIATE_TIMOUT      3
void broadcast_conn_info(void);
void read_all_rssi(void);
void start_scan_if_needed(void);

const static ext_adv_set_en_t adv_sets_en[] = { {.handle = 0, .duration = 0, .max_events = 0} };

static void user_msg_handler(uint32_t msg_id, void *data, uint16_t size)
{
    switch (msg_id)
    {
    case USER_TRIGGER_BROADCAST:
        broadcast_conn_info();
        //read_all_rssi();
        break;
    case USER_MSG_INITIATE_TIMOUT:
        if (is_initiating)
        {
            platform_printf("initiate timout\n");
            is_initiating = 0;
            gap_create_connection_cancel();
            start_scan_if_needed();
        }
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

void on_tx_conn_info_done(struct ll_raw_packet *packet, void *user_data)
{
    pkt_busy = 0;
    btstack_push_user_msg(USER_TRIGGER_BROADCAST, NULL, 0);
}

const static bd_addr_t rand_addr = { 0xE8, 0x70, 0xB7, 0x31, 0xCA, 0xF3 };
static void setup_adv(void)
{
    gap_set_adv_set_random_addr(0, rand_addr);
    gap_set_ext_adv_para(0,
                            CONNECTABLE_ADV_BIT | SCANNABLE_ADV_BIT | LEGACY_PDU_BIT,
                            800, 800,            // Primary_Advertising_Interval_Min, Primary_Advertising_Interval_Max
                            PRIMARY_ADV_ALL_CHANNELS,  // Primary_Advertising_Channel_Map
                            BD_ADDR_TYPE_LE_RANDOM,    // Own_Address_Type
                            BD_ADDR_TYPE_LE_PUBLIC,    // Peer_Address_Type (ignore)
                            NULL,                      // Peer_Address      (ignore)
                            ADV_FILTER_ALLOW_ALL,      // Advertising_Filter_Policy
                            0x00,                      // Advertising_Tx_Power
                            PHY_1M,                    // Primary_Advertising_PHY
                            0,                         // Secondary_Advertising_Max_Skip
                            PHY_1M,                    // Secondary_Advertising_PHY
                            0x00,                      // Advertising_SID
                            0x00);                     // Scan_Request_Notification_Enable
    gap_set_ext_adv_data(0, sizeof(adv_data), (uint8_t*)adv_data);
    gap_set_ext_scan_response_data(0, sizeof(scan_data), (uint8_t*)scan_data);

    pkt_tx_conn_info = ll_raw_packet_alloc(1, on_tx_conn_info_done, NULL);
    ll_raw_packet_set_param(pkt_tx_conn_info, 3, CH_ID, CH_PHY, CH_ACC, CH_CRC);
}
static void setup_scan(void)
{
    gap_set_random_device_address(rand_addr);
    gap_set_ext_scan_para(BD_ADDR_TYPE_LE_RANDOM, SCAN_ACCEPT_ALL_EXCEPT_NOT_DIRECTED,
                            sizeof(configs) / sizeof(configs[0]),configs);

}
void enable_adv_if_needed(void)
{
    int i;
    if(CENTRAL_CONCURRENT_CONNECTIONS == t_app.central_con_num) return;
    
    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        if (INVALID_HANDLE == t_app.ble_info[i].con_handle)
        {
            gap_set_ext_adv_enable(1, sizeof(adv_sets_en) / sizeof(adv_sets_en[0]), adv_sets_en);
            printf("Advertise continue\r\n");
            break;
        }
    }
}
void start_scan_if_needed(void)
{
    int i;
    if(PERIPHERAL_CONCURRENT_CONNECTIONS == t_app.peripheral_con_num) return;
    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        if (INVALID_HANDLE == t_app.ble_info[i].con_handle)
        {
            // start continuous scanning
            gap_set_ext_scan_enable(1, 0, 0, 0);
            printf("Scan continue\r\n");
            return;
        }
    }
}
void init_data(void)
{
    int i;
    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        t_app.ble_info[i].con_handle = INVALID_HANDLE;
        tx_conn_event_infos[i].phy = PHY_1M;
        t_app.ble_info[i].role = 0xff;
    }
}
uint8_t get_role_by_handle(hci_con_handle_t handle)
{
    if(INVALID_HANDLE != handle)
    {
        for (int i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
        {
            if (handle == t_app.ble_info[i].con_handle)
            {
                printf("get role:%d\r\n",t_app.ble_info[i].role);
                return t_app.ble_info[i].role;
            }
        }    
    }
    return 0xff;
}
void insert_conn_handle(hci_con_handle_t handle,uint8_t role)
{
    int i;
    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        if (INVALID_HANDLE == t_app.ble_info[i].con_handle)
        {
            t_app.ble_info[i].role = role;
            t_app.ble_info[i].con_handle = handle;
            break;
        }
    }
}

void read_all_rssi(void)
{
    int i;
    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        if (INVALID_HANDLE != t_app.ble_info[i].con_handle)
            gap_read_rssi(t_app.ble_info[i].con_handle);
    }
}

void remove_conn_handle(hci_con_handle_t handle)
{
    int i;
    printf("remove handle:%d\r\n",handle);
    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        if (handle == t_app.ble_info[i].con_handle)
        {
            t_app.ble_info[i].con_handle = INVALID_HANDLE;
            t_app.ble_info[i].role = 0xff;
            tx_conn_event_infos[i].phy = PHY_1M;
        }
    }
}

conn_event_info_t *get_conn_info_of_handle(hci_con_handle_t handle)
{
    int i;
    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        if (handle == t_app.ble_info[i].con_handle)
            return tx_conn_event_infos + i;
    }
    return NULL;
}

void update_phy_of_conn(hci_con_handle_t handle, phy_type_t phy)
{
    int i;
    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        if (handle == t_app.ble_info[i].con_handle)
        {
            tx_conn_event_infos[i].phy = phy;
            break;
        }
    }
}

int has_connection(void)
{
    int i;
    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        if (INVALID_HANDLE != t_app.ble_info[i].con_handle)
        {
           // printf("has con:%d\r\n ",t_app.ble_info[i].con_handle);
            return 1;
        }
    }
    return 0;
}

void broadcast_conn_info(void)
{
    uint64_t t;
    uint16_t event_count = 0;
    static uint64_t next_time = 0;
    uint32_t max_interval = 0;

    int i;

    if (has_connection() == 0) return;

    if (pkt_busy) return;

    t = platform_get_us_time() + 700;
    if (next_time > t) t = next_time;
    while (ll_raw_packet_send(pkt_tx_conn_info, t))
        t += 300;

    pkt_busy = 1;

#define EXTRA_T 1000

    t += EXTRA_T;

    for (i = 0; i < MAX_CONCURRENT_CONNECTIONS; i++)
    {
        hci_con_handle_t conn_handle = t_app.ble_info[i].con_handle;
        conn_event_info_t *p_info = tx_conn_event_infos + i;

        p_info->valid = 0;

        if (INVALID_HANDLE == conn_handle)
            continue;

        if (ll_get_conn_events_info(conn_handle,
                            CHANNEL_INFO_NUM, t,
                            &p_info->interval,
                            &p_info->time_offset,
                            &p_info->event_count,
                            p_info->channels) != 0) continue;
        
        platform_printf("handle:%d; offset:%d; count:%d\r\n", conn_handle, p_info->time_offset, p_info->event_count);
        platform_printf("handle:%d  aa:%4x\r\n", conn_handle, p_info->access_addr);
        p_info->valid = 1;
        p_info->time_offset += EXTRA_T;
    }

    max_interval = 200000;

    // 503 is the next prime after 500.
    next_time = t + (platform_rand() & 0x1f) * 503
                + max_interval;

    ll_raw_packet_set_tx_data(pkt_tx_conn_info, 0, &tx_conn_event_infos, sizeof(tx_conn_event_infos));
}

void start_boardcast(void)
{
    btstack_push_user_msg(USER_TRIGGER_BROADCAST, NULL, 0);
}

static void app_timer_callback(TimerHandle_t xTimer)
{
    btstack_push_user_msg(USER_MSG_INITIATE_TIMOUT, NULL, 0);
}

extern uint8_t parse_adv_data(const uint8_t *p_data, uint8_t len,int8_t rssi);

#include "gatt_client_util.h"
struct gatt_client_discoverer *discoverer = NULL;


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
        if (sizeof(tx_conn_event_infos) > 200)
        {
            platform_printf("too large!!!\n");
            while (1);
        }
        init_data();
        ll_set_max_conn_number(MAX_CONCURRENT_CONNECTIONS);
        setup_adv();
        setup_scan();
        enable_adv_if_needed();
        start_scan_if_needed();
        platform_printf("phy set:%d\r\n",gap_set_def_phy(0,1,1));
        break;

    case HCI_EVENT_COMMAND_COMPLETE:
        {
            if (hci_event_command_complete_get_command_opcode(packet) == HCI_RD_RSSI_CMD_OPCODE)
            {
                const event_command_complete_return_param_read_rssi_t *cmpl =
                    (const event_command_complete_return_param_read_rssi_t *)hci_event_command_complete_get_return_parameters(packet);
               //handle platform_printf("RSSI: @%d %3ddBm\n", cmpl->conn_handle, cmpl->rssi);
            }
        }
        break;

    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet))
        {
        case HCI_SUBEVENT_LE_EXTENDED_ADVERTISING_REPORT:
            {
                const le_ext_adv_report_t *report = decode_hci_le_meta_event(packet, le_meta_event_ext_adv_report_t)->reports;
                bd_addr_t peer_addr;
                reverse_bd_addr(report->address, peer_addr);
               // if (is_pending_slave(peer_addr))
                if(parse_adv_data(report->data,report->data_len,report->rssi))
                {
                    gap_set_ext_adv_enable(0, sizeof(adv_sets_en) / sizeof(adv_sets_en[0]), adv_sets_en);
                    gap_set_ext_scan_enable(0, 0, 0, 0);
                    platform_printf("connecting ... "); 

                    if (report->evt_type & HCI_EXT_ADV_PROP_USE_LEGACY)
                        phy_configs[0].phy = PHY_1M;
                    else
                        phy_configs[0].phy = (phy_type_t)(report->s_phy != 0 ? report->s_phy : report->p_phy);
                    gap_ext_create_connection(    INITIATING_ADVERTISER_FROM_PARAM, // Initiator_Filter_Policy,
                                                  BD_ADDR_TYPE_LE_RANDOM,           // Own_Address_Type,
                                                  report->addr_type,                // Peer_Address_Type,
                                                  peer_addr,                        // Peer_Address,
                                                  sizeof(phy_configs) / sizeof(phy_configs[0]),
                                                  phy_configs);
                   is_initiating = 1;
                   xTimerReset(app_timer, portMAX_DELAY);
                }
            }
            break;            
        case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE:
            {
                const le_meta_event_enh_create_conn_complete_t *complete =
                    decode_hci_le_meta_event(packet, le_meta_event_enh_create_conn_complete_t);
                if (complete->status) 
                {
                    platform_printf("status: %d\r\n", complete->status);
                    gap_disconnect(complete->handle);
                    break;
                }
                
                insert_conn_handle(complete->handle,complete->role);
                if (HCI_ROLE_SLAVE == complete->role)
                {
                    att_set_db(complete->handle, profile_data);
                    ll_hint_on_ce_len(complete->handle, 4, 4);
                    t_app.central_con_num += 1;
                    l2cap_request_connection_parameter_update(complete->handle, 160, 160, 0, 500);  
                    //gap_set_phy(complete->handle, 1, 2, 2, 0);
                }
                else
                {
                    xTimerStop(app_timer, portMAX_DELAY);
                    t_app.peripheral_con_num += 1;
                    //discoverer = gatt_client_util_discover_all(complete->handle, gatt_client_util_dump_profile, NULL);
                
                }
             
                platform_set_timer(start_boardcast, 2000);

                {
                    uint8_t hop_inc = 0;
                    conn_event_info_t *p_info = get_conn_info_of_handle(complete->handle);
                    ll_get_conn_info(complete->handle,
                                &p_info->access_addr,
                                &p_info->crc_init,
                                &hop_inc);
                    platform_printf("handle:%d  aa:%4x  crc:%4x\r\n", complete->handle, p_info->access_addr,  p_info->crc_init);
                }
                start_scan_if_needed();
                enable_adv_if_needed();
            }
            break;
        case HCI_SUBEVENT_LE_PHY_UPDATE_COMPLETE:
            {
                const le_meta_phy_update_complete_t *complete = decode_hci_le_meta_event(packet, le_meta_phy_update_complete_t);
                update_phy_of_conn(complete->handle, complete->rx_phy);
                platform_printf("updatae phy: %d\r\n",complete->rx_phy);
            }
            break;
        case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
            {
                const le_meta_event_conn_update_complete_t *complete = decode_hci_le_meta_event(packet, le_meta_event_conn_update_complete_t);
                ll_hint_on_ce_len(complete->handle, 4, 4);
                    platform_printf("con pram updata: #%d,%.2f ms\n", complete->handle,complete->interval*1.25);
            }
            break;
        case HCI_SUBEVENT_LE_ADVERTISING_SET_TERMINATED:
            enable_adv_if_needed();
            break;
        case HCI_SUBEVENT_LE_CHANNEL_SELECTION_ALGORITHM:
            {
                const le_meta_ch_sel_algo_t *complete = decode_hci_le_meta_event(packet, le_meta_ch_sel_algo_t);
                platform_printf("algo: #%d\n", complete->algo);
            }
            break;
        case HCI_SUBEVENT_LE_VENDOR_CHANNEL_MAP_UPDATE:
            {
                const le_meta_event_vendor_channel_map_update_t *update =
                    decode_hci_le_meta_event(packet, le_meta_event_vendor_channel_map_update_t);
                platform_printf("chmap %d: ", update->conn_handle);
                printf_hexdump(update->channel_map, sizeof(update->channel_map));
                platform_printf("\n");
            }
        default:
            break;
        }

        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE:
        {
            const event_disconn_complete_t *complete = decode_hci_event_disconn_complete(packet);
            printf("disc\r\n");
            if(HCI_ROLE_SLAVE == get_role_by_handle(complete->conn_handle))
            {
                t_app.central_con_num -= 1;
            }
            else  if(HCI_ROLE_MASTER == get_role_by_handle(complete->conn_handle))
            {
                t_app.peripheral_con_num -= 1;
//                if (discoverer)
//                {
//                    gatt_client_util_free(discoverer);
//                    discoverer = NULL;
//                }
            }
            else
            {
                platform_printf("illegal role\r\n");
            }
                
            remove_conn_handle(complete->conn_handle);
            enable_adv_if_needed();
            start_scan_if_needed();
        }
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
    platform_printf("setup profile\n");
    app_timer = xTimerCreate("a",
                            pdMS_TO_TICKS(5000),
                            pdFALSE,
                            NULL,
                            app_timer_callback);
    // Note: security has not been enabled.
    att_server_init(att_read_callback, att_write_callback);
    hci_event_callback_registration.callback = &user_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    att_server_register_packet_handler(&user_packet_handler);
    return 0;
}
