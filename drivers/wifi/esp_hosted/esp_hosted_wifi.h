/*
 * Copyright (c) 2025 Arduino SA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_WIFI_ESP_HOSTED_WIFI_H_
#define ZEPHYR_DRIVERS_WIFI_ESP_HOSTED_WIFI_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>

#define DT_DRV_COMPAT espressif_esp_hosted

#define ESP_HOSTED_IPV4_ADDR_LEN    (4)
#define ESP_HOSTED_MAC_ADDR_LEN     (6)
#define ESP_HOSTED_MAC_STR_LEN      (18)
#define ESP_HOSTED_MAX_SSID_LEN     (32)
#define ESP_HOSTED_MAX_WEP_LEN      (13)
#define ESP_HOSTED_MAX_WPA_LEN      (63)
#define ESP_HOSTED_MAX_AP_CLIENTS   (3)

#define ESP_HOSTED_SPI_CONFIG       (SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8))

typedef enum {
    ESP_HOSTED_STA_IF = 0,
    ESP_HOSTED_AP_IF,
    ESP_HOSTED_SERIAL_IF,
    ESP_HOSTED_HCI_IF,
    ESP_HOSTED_PRIV_IF,
    ESP_HOSTED_TEST_IF,
    ESP_HOSTED_MAX_IF,
} esp_hosted_interface_t;

typedef enum {
    ESP_HOSTED_SEC_INVALID = -1,
    ESP_HOSTED_SEC_OPEN,
    ESP_HOSTED_SEC_WEP,
    ESP_HOSTED_SEC_WPA_PSK,
    ESP_HOSTED_SEC_WPA2_PSK,
    ESP_HOSTED_SEC_WPA_WPA2_PSK,
    ESP_HOSTED_SEC_WPA2_ENTERPRISE,
    ESP_HOSTED_SEC_WPA3_PSK,
    ESP_HOSTED_SEC_WPA2_WPA3_PSK,
    ESP_HOSTED_SEC_MAX,
} esp_hosted_security_t;

typedef struct {
    uint8_t ip_addr[ESP_HOSTED_IPV4_ADDR_LEN];
    uint8_t subnet_addr[ESP_HOSTED_IPV4_ADDR_LEN];
    uint8_t gateway_addr[ESP_HOSTED_IPV4_ADDR_LEN];
    uint8_t dns_addr[ESP_HOSTED_IPV4_ADDR_LEN];
} esp_hosted_ifconfig_t;

typedef struct {
    int32_t rssi;
    esp_hosted_security_t security;
    uint8_t channel;
    char ssid[ESP_HOSTED_MAX_SSID_LEN];
    uint8_t bssid[ESP_HOSTED_MAC_ADDR_LEN];
} esp_hosted_scan_result_t;

typedef struct {
    int32_t rssi;
    esp_hosted_security_t security;
    uint8_t channel;
    char ssid[ESP_HOSTED_MAX_SSID_LEN];
    uint8_t bssid[ESP_HOSTED_MAC_ADDR_LEN];
} esp_hosted_netinfo_t;

typedef struct {
	struct gpio_dt_spec cs_gpio;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec dataready_gpio;
	struct gpio_dt_spec handshake_gpio;
	struct spi_dt_spec spi_bus;
} esp_hosted_config_t ;

typedef struct {
    uint8_t chip_id;
    uint8_t spi_clk;
    uint8_t chip_flags;
    uint8_t flags;
    uint16_t seq_num;
    uint64_t last_hb_ms;
	struct net_if *iface;
	struct k_sem sema;
	//bool is_ap_up;
	//bool is_sta_connected;
	uint8_t mac_addr[6];
	//scan_result_cb_t scan_rslt_cb;
	//whd_ssid_t ssid;
	//whd_scan_result_t scan_result;
	//struct k_sem sema_scan;
	//whd_driver_t whd_drv;
	//struct gpio_callback host_oob_pin_cb;
	//uint8_t frame_buf[NET_ETH_MAX_FRAME_SIZE];
#if defined(CONFIG_NET_STATISTICS_WIFI)
	struct net_stats_wifi stats;
#endif
} esp_hosted_data_t;

typedef int (*esp_hosted_scan_callback_t)(esp_hosted_scan_result_t *, void *);

#define ESP_FRAME_MAX_SIZE          (1600)
#define ESP_FRAME_MAX_PAYLOAD       (ESP_FRAME_MAX_SIZE - sizeof(esp_header_t))
#define ESP_FRAME_FLAGS_FRAGMENT    (1 << 0)

#define ESP_STATE_NUM_ITFS          (2)
#define ESP_STATE_BUF_SIZE          (ESP_FRAME_MAX_SIZE * 2)
#define ESP_STACK_CAPACITY          (32)
#define ESP_SYNC_REQ_TIMEOUT        (5000)

#define TLV_HEADER_TYPE_EP          (1)
#define TLV_HEADER_TYPE_DATA        (2)
#define TLV_HEADER_EP_RESP          "ctrlResp"
#define TLV_HEADER_EP_EVENT         "ctrlEvnt"

typedef enum {
    ESP_HOSTED_FLAGS_RESET          = (0 << 0),
    ESP_HOSTED_FLAGS_INIT           = (1 << 0),
    ESP_HOSTED_FLAGS_ACTIVE         = (1 << 1),
    ESP_HOSTED_FLAGS_STATIC_IP      = (1 << 2),
    ESP_HOSTED_FLAGS_AP_STARTED     = (1 << 3),
    ESP_HOSTED_FLAGS_STA_CONNECTED  = (1 << 4),
} esp_hosted_flags_t;

typedef enum {
    ESP_PACKET_TYPE_EVENT,
} esp_hosted_priv_packet_t;

typedef enum {
    ESP_PRIV_EVENT_INIT,
} esp_hosted_priv_event_t;

typedef struct esp_hosted_stack {
    size_t capacity;
    size_t top;
    void *buff[ESP_STACK_CAPACITY];
} esp_hosted_stack_t;

typedef struct __attribute__((packed)) {
    uint8_t ep_type;
    uint16_t ep_length;
    uint8_t ep_value[8];
    uint8_t data_type;
    uint16_t data_length;
    uint8_t data[];
} tlv_header_t;

typedef struct __attribute__((packed)) {
    uint8_t event_type;
    uint8_t event_len;
    uint8_t event_data[];
} esp_event_t;

typedef struct __attribute__((packed)) {
    uint8_t if_type    : 4;
    uint8_t if_num     : 4;
    uint8_t flags;
    uint16_t len;
    uint16_t offset;
    uint16_t checksum;
    uint16_t seq_num;
    uint8_t reserved2;
    union {
        uint8_t hci_pkt_type;
        uint8_t priv_pkt_type;
    };
    uint8_t payload[];
} esp_header_t;

#endif // ZEPHYR_DRIVERS_WIFI_ESP_HOSTED_WIFI_H_
