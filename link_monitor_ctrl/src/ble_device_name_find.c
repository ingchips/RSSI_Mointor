#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME         0x08 /**< Short local device name. */
#define BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME      0x09 /**< Complete local device name. */


// �㲥���ݽ�����
typedef struct {
    uint8_t data[235];      // �㲥������ʼ��ַ
    uint16_t len;       // �㲥���ݳ���
    uint16_t offset;    // ��ǰ����λ��
} ble_advdata_parser_t;

// �����豸������Ϣ
static bool find_device_name(ble_advdata_parser_t *p_parser,
                             uint8_t type,
                             char *p_name,
                             uint16_t name_len)
{
    uint16_t len;
    uint8_t *p_data = p_parser->data;
    uint16_t field_len;
    uint8_t field_type;
    uint16_t offset = 0;

    while (offset < p_parser->len) {
        // ����AD�ֶ����ͺͳ���
        field_len = p_data[offset++];
        field_type = p_data[offset++];

        // ������豸�����ֶΣ������������Ϣ
        if (field_type == type) {
            len = field_len - 1;
            if (len > name_len) {
                len = name_len;
            }
            memcpy(p_name, &p_data[offset],len); // �����豸������Ϣ
            p_name[len] = '\0'; // ����ַ���������
            return true;
        } else {
            // ��������豸�����ֶΣ����������ֶ�
            offset += (field_len - 1);
        }
    }

    return false;
}
//const char *pLoadName = "Simple Peripheral";
char *pLoadName = "ING I/O";
const char *pLoadShortName = "SP";
// �����㲥���ݰ�

ble_advdata_parser_t parser;
uint8_t parse_adv_data(const uint8_t *p_data, uint8_t len,int8_t rssi)
{
    memcpy(parser.data,p_data,len);
    parser.len = len;
    char name[32];
    if (find_device_name(&parser, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, name, sizeof(name))) {
         
//        printf("complete name:%s,rssi:%d dBm\r\n",name,rssi);
        if(0 == memcmp(name,pLoadName,strlen(pLoadName)) )
        {
            printf("match complete name:%s\r\n",name);
            return 1;
        }
    }
    
    if (find_device_name(&parser, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, name, sizeof(name))) {
  
        if(0 == memcmp(name,pLoadShortName,strlen(pLoadShortName)) )
        {
            printf("short name:%s\r\n",name);
        
            return 1;
        }
    
    } 
    else{
       //printf("Non name\r\n");
        // ...
    }
    return 0;
}