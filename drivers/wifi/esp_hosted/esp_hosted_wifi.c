/*
 * Copyright (c) 2025 Arduino SA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/conn_mgr/connectivity_wifi_mgmt.h>

#include <esp_hosted_wifi.h>
#include <esp_hosted_hal.h>
#include <esp_hosted_pb.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(esp_hosted, CONFIG_WIFI_LOG_LEVEL);

static ProtobufCAllocator protobuf_alloc = {
    .alloc = &esp_hosted_hal_alloc,
    .free = &esp_hosted_hal_free,
    .allocator_data = NULL,
};

static esp_hosted_data_t esp_hosted_data = {0};

static esp_hosted_config_t esp_hosted_config = {
	.cs_gpio = GPIO_DT_SPEC_INST_GET(0, cs_gpios),
	.reset_gpio = GPIO_DT_SPEC_INST_GET(0, reset_gpios),
	.dataready_gpio = GPIO_DT_SPEC_INST_GET(0, dataready_gpios),
	.handshake_gpio = GPIO_DT_SPEC_INST_GET(0, handshake_gpios),
	.spi_bus = SPI_DT_SPEC_INST_GET(0, ESP_HOSTED_SPI_CONFIG, 10U)
};

#define CONFIG_ESP_HOSTED_EVENT_TASK_PRIORITY   (4)
#define CONFIG_ESP_HOSTED_EVENT_TASK_STACK_SIZE (16*1024)
K_THREAD_STACK_DEFINE(esp_hosted_event_stack, CONFIG_ESP_HOSTED_EVENT_TASK_STACK_SIZE);
static struct k_thread esp_hosted_event_thread;

K_MSGQ_DEFINE(esp_hosted_msgq, sizeof(void *), 32, 4);

static uint16_t esp_hosted_checksum(esp_header_t *esp_header) {
    uint16_t checksum = 0;
    esp_header->checksum = 0;
    uint8_t *buf = (uint8_t *)esp_header;
    for (size_t i = 0; i < (esp_header->len + sizeof(esp_header_t)); i++) {
        checksum += buf[i];
    }
    return checksum;
}

#if ESP_HOSTED_DEBUG
static void esp_hosted_dump_header(esp_header_t *esp_header) {
    static const char *if_strs[] = { "STA", "AP", "SERIAL", "HCI", "PRIV", "TEST" };
    if (esp_header->if_type > ESP_HOSTED_MAX_IF) {
        return;
    }
    LOG_DBG("esp header: if %s_IF length %d offset %d checksum %d seq %d flags %x\n",
        if_strs[esp_header->if_type], esp_header->len, esp_header->offset,
        esp_header->checksum, esp_header->seq_num, esp_header->flags);

    if (esp_header->if_type == ESP_HOSTED_SERIAL_IF) {
        tlv_header_t *tlv_header = (tlv_header_t *)(esp_header->payload);
        LOG_DBG("tlv header: ep_type %d ep_length %d ep_value %.8s data_type %d data_length %d\n",
            tlv_header->ep_type, tlv_header->ep_length,
            tlv_header->ep_value, tlv_header->data_type, tlv_header->data_length);
    }
}
#endif

static int32_t esp_hosted_resp_value(CtrlMsg *ctrl_msg) {
    // Each response struct return value is located at a different offset,
    // the following array maps response CtrlMsgs to return values (resp)
    // offsets within each response struct.
    const static size_t ctrl_msg_resp_offset[] = {
        offsetof(CtrlMsgRespGetMacAddress, resp),
        offsetof(CtrlMsgRespSetMacAddress, resp),
        offsetof(CtrlMsgRespGetMode, resp),
        offsetof(CtrlMsgRespSetMode, resp),
        offsetof(CtrlMsgRespScanResult, resp),
        offsetof(CtrlMsgRespGetAPConfig, resp),
        offsetof(CtrlMsgRespConnectAP, resp),
        offsetof(CtrlMsgRespGetStatus, resp),
        offsetof(CtrlMsgRespGetSoftAPConfig, resp),
        offsetof(CtrlMsgRespSetSoftAPVendorSpecificIE, resp),
        offsetof(CtrlMsgRespStartSoftAP, resp),
        offsetof(CtrlMsgRespSoftAPConnectedSTA, resp),
        offsetof(CtrlMsgRespGetStatus, resp),
        offsetof(CtrlMsgRespSetMode, resp),
        offsetof(CtrlMsgRespGetMode, resp),
        offsetof(CtrlMsgRespOTABegin, resp),
        offsetof(CtrlMsgRespOTAWrite, resp),
        offsetof(CtrlMsgRespOTAEnd, resp),
        offsetof(CtrlMsgRespSetWifiMaxTxPower, resp),
        offsetof(CtrlMsgRespGetWifiCurrTxPower, resp),
        offsetof(CtrlMsgRespConfigHeartbeat, resp),
    };

    int32_t resp = -1;
    size_t index = ctrl_msg->msg_id - CTRL_MSG_ID__Resp_Base;

    // All types of messages share the same payload base address.
    if (ctrl_msg->resp_get_mac_address != NULL &&
        ctrl_msg->msg_type == CTRL_MSG_TYPE__Resp &&
        index > 0 && index <= ARRAY_SIZE(ctrl_msg_resp_offset)) {
        // Return the response struct's return value.
        size_t offset = ctrl_msg_resp_offset[index - 1];
        resp = *((int32_t *)((char *)ctrl_msg->resp_get_mac_address + offset));
    }
    return resp;
}

static int esp_hosted_request(const struct device *dev, CtrlMsgId msg_id, void *ctrl_payload) {
    esp_hosted_data_t *data = (esp_hosted_data_t *)dev->data;

    CtrlMsg ctrl_msg = {0};

    ctrl_msg__init(&ctrl_msg);
    ctrl_msg.msg_id = msg_id;
    ctrl_msg.payload_case = msg_id;

    // All types of messages share the same payload base address.
    ctrl_msg.req_get_mac_address = ctrl_payload;

    // Pack protobuf
    size_t payload_size = ctrl_msg__get_packed_size(&ctrl_msg);
    if ((payload_size + sizeof(tlv_header_t)) > ESP_FRAME_MAX_PAYLOAD) {
        LOG_ERR("esp_hosted_request() payload size > max payload %d\n", msg_id);
        return -1;
    }

    uint8_t buf[ESP_STATE_BUF_SIZE];

    esp_header_t *esp_header = (esp_header_t *)(buf);
    tlv_header_t *tlv_header = (tlv_header_t *)(esp_header->payload);

    esp_header->if_type = ESP_HOSTED_SERIAL_IF;
    esp_header->if_num = 0;
    esp_header->flags = 0;
    esp_header->len = payload_size + sizeof(tlv_header_t);
    esp_header->offset = sizeof(esp_header_t);
    esp_header->seq_num = data->seq_num++;

    tlv_header->ep_type = TLV_HEADER_TYPE_EP;
    tlv_header->ep_length = 8;
    memcpy(tlv_header->ep_value, TLV_HEADER_EP_RESP, 8);
    tlv_header->data_type = TLV_HEADER_TYPE_DATA;
    tlv_header->data_length = payload_size;
    ctrl_msg__pack(&ctrl_msg, tlv_header->data);
    esp_header->checksum = esp_hosted_checksum(esp_header);

    size_t frame_size = (sizeof(esp_header_t) + esp_header->len + 3) & ~3U;
    if (esp_hosted_hal_spi_transfer(dev, buf, NULL, frame_size) != 0) {
        LOG_ERR("esp_hosted_request() request %d failed\n", msg_id);
        return -1;
    }
    return 0;
}

static CtrlMsg *esp_hosted_response(const struct device *dev, CtrlMsgId msg_id, uint32_t timeout) {
    CtrlMsg *ctrl_msg = NULL;
    if (k_msgq_get(&esp_hosted_msgq, &ctrl_msg, K_FOREVER)) {
        return NULL;
    }
    
    // TODO should we peek?
    if (ctrl_msg->msg_id != msg_id) {
        LOG_ERR("esp_hosted_response() expected id %u got id %u\n", msg_id, ctrl_msg->msg_id);
        return NULL;
    }

    // If message type is a response, check the response struct's return value.
    if (ctrl_msg->msg_type == CTRL_MSG_TYPE__Resp && esp_hosted_resp_value(ctrl_msg) != 0) {
        LOG_ERR("esp_hosted_response() response %d failed %d\n", msg_id, esp_hosted_resp_value(ctrl_msg));
        ctrl_msg__free_unpacked(ctrl_msg, &protobuf_alloc);
        return NULL;
    }

    return ctrl_msg;
}

static int esp_hosted_ctrl(const struct device *dev, CtrlMsgId req_id, void *req_payload, CtrlMsg **resp_msg) {
    uint32_t resp_id = (req_id - CTRL_MSG_ID__Req_Base) + CTRL_MSG_ID__Resp_Base;

    if (esp_hosted_request(dev, req_id, req_payload) != 0) {
        return -1;
    }

    if ((*resp_msg = esp_hosted_response(dev, resp_id, ESP_SYNC_REQ_TIMEOUT)) == NULL) {
        return -1;
    }
    return 0;
}

static int esp_hosted_process(const struct device *dev, esp_header_t *esp_header)
{
	esp_hosted_data_t *data = dev->data;
    tlv_header_t *tlv_header = (tlv_header_t *)(esp_header->payload);

    switch (esp_header->if_type) {
        case ESP_HOSTED_STA_IF:
        case ESP_HOSTED_AP_IF: {
            // Networking traffic
            //uint32_t itf = esp_header->if_type;
            //if (netif_is_link_up(&data->netif[itf])) {
            //    if (esp_hosted_netif_input(&esp_state, itf, esp_header->payload, esp_header->len) != 0) {
            //        LOG_ERR("esp_hosted_task netif input failed\n");
            //        return -1;
            //    }
            //    LOG_DBG("esp_hosted_task eth frame input %d\n", esp_header->len);
            //}
            return 0;
        }
        case ESP_HOSTED_PRIV_IF: {
            esp_event_t *priv_event = (esp_event_t *)(esp_header->payload);
            if (esp_header->priv_pkt_type == ESP_PACKET_TYPE_EVENT &&
                priv_event->event_type == ESP_PRIV_EVENT_INIT) {
                data->chip_id = priv_event->event_data[2];
                data->spi_clk = priv_event->event_data[5];
                data->chip_flags = priv_event->event_data[8];
                LOG_INF("esp_hosted_task chip id %d spi_mhz %d caps 0x%x\n",
                    data->chip_id, data->spi_clk, data->chip_flags);
            }
            return 0;
        }
        case ESP_HOSTED_HCI_IF:
        case ESP_HOSTED_TEST_IF:
        case ESP_HOSTED_MAX_IF:
            LOG_ERR("esp_hosted_task unexpected interface type %d\n", esp_header->if_type);
            return 0;
        case ESP_HOSTED_SERIAL_IF:
            // Requires further processing
            break;
    }

    CtrlMsg *ctrl_msg = ctrl_msg__unpack(&protobuf_alloc, tlv_header->data_length, tlv_header->data);
    if (ctrl_msg == NULL) {
        LOG_ERR("esp_hosted_task failed to unpack protobuf\n");
        return -1;
    }

    if (ctrl_msg->msg_type == CTRL_MSG_TYPE__Event) {
        switch (ctrl_msg->msg_id) {
            case CTRL_MSG_ID__Event_ESPInit:
                data->flags |= ESP_HOSTED_FLAGS_ACTIVE;
                break;
            case CTRL_MSG_ID__Event_Heartbeat:
                data->last_hb_ms = k_uptime_get();
                return 0;
            case CTRL_MSG_ID__Event_StationDisconnectFromAP:
                //net_if_dormant_on(iface);
                data->flags &= ~ESP_HOSTED_FLAGS_STA_CONNECTED;
                return 0;
            case CTRL_MSG_ID__Event_StationDisconnectFromESPSoftAP:
                return 0;
            default:
                LOG_ERR("esp_hosted_task unexpected event %d\n", ctrl_msg->msg_id);
                return 0;
        }
    }

    // Responses that should be handled here.
    if (ctrl_msg->msg_type == CTRL_MSG_TYPE__Resp) {
        switch (ctrl_msg->msg_id) {
            case CTRL_MSG_ID__Resp_ConnectAP: {
                if (esp_hosted_resp_value(ctrl_msg) == 0) {
                    data->flags |= ESP_HOSTED_FLAGS_STA_CONNECTED;
                }
                ctrl_msg__free_unpacked(ctrl_msg, &protobuf_alloc);
                LOG_DBG("esp_hosted_task state %d\n", data->flags);
                return 0;
            }
            default:
                break;
        }
    }

    // A control message resp/event will be pushed on the stack for further processing.
	if (k_msgq_put(&esp_hosted_msgq, &ctrl_msg, K_FOREVER)) {
        LOG_ERR("esp_hosted_task message queue full\n");
        return -1;
    }

    LOG_DBG("esp_hosted_task pushed msg_type %u msg_id %u\n", ctrl_msg->msg_type, ctrl_msg->msg_id);
    return 0;
}

static void esp_hosted_event_task(const struct device *dev, void *p2, void *p3)
{
    uint8_t buf[ESP_STATE_BUF_SIZE];
	//esp_hosted_data_t *data = dev->data;

    while (1) {
        size_t offset = 0;
        esp_header_t *esp_header = (esp_header_t *)(buf);

        //if (!(data->flags & ESP_HOSTED_FLAGS_INIT) || !esp_hosted_hal_data_ready()) {
        //    return 0;
        //}

        do {
            esp_header_t *frag_header = (esp_header_t *)(buf + offset);
            if ((ESP_STATE_BUF_SIZE - offset) < ESP_FRAME_MAX_SIZE) {
                // This shouldn't happen, but if it did stop the thread.
                LOG_ERR("esp_hosted_task spi buffer overflow offs %d\n", offset);
                return;
            }

            if (esp_hosted_hal_spi_transfer(dev, NULL, buf + offset, ESP_FRAME_MAX_SIZE) != 0) {
                LOG_ERR("esp_hosted_task spi transfer failed\n");
                continue;
            }

            if (frag_header->len == 0 ||
                frag_header->len > ESP_FRAME_MAX_PAYLOAD ||
                frag_header->offset != sizeof(esp_header_t)) {
                // Invalid or empty packet, just ignore it silently.
                LOG_WRN("esp_hosted_task invalid frame size %d offset %d\n",
                    esp_header->len, esp_header->offset);
                continue;
            }

            uint16_t checksum = frag_header->checksum;
            frag_header->checksum = esp_hosted_checksum(frag_header);
            if (frag_header->checksum != checksum) {
                LOG_ERR("esp_hosted_task invalid checksum, expected %d\n", checksum);
                return;
            }

            if (offset) {
                // Combine fragmented packet
                if ((esp_header->seq_num + 1) != frag_header->seq_num) {
                    LOG_ERR("esp_hosted_task fragmented frame sequence mismatch\n");
                    return;
                }
                esp_header->len += frag_header->len;
                esp_header->seq_num = frag_header->seq_num;
                esp_header->flags = frag_header->flags;
                LOG_INF("esp_hosted_task received fragmented packet %d\n", frag_header->len);
                // Append the current fragment's payload to the previous one.
                memcpy(buf + offset, frag_header->payload, frag_header->len);
            }

            offset = sizeof(esp_header_t) + esp_header->len;
        } while (offset == 0 || (esp_header->flags & ESP_FRAME_FLAGS_FRAGMENT));

        #if ESP_HOSTED_DEBUG
        esp_hosted_dump_header(esp_header);
        #endif

        if (esp_hosted_process(dev, esp_header)) {
            return;
        }

        k_msleep(10);
    }
}

static void esp_hosted_mgmt_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	esp_hosted_data_t *data = dev->data;
	struct ethernet_context *eth_ctx = net_if_l2_data(iface);

	data->iface = iface;
	eth_ctx->eth_if_type = L2_ETH_IF_TYPE_WIFI;

    // Set MAC address.
	net_if_set_link_addr(iface, data->mac_addr, 6, NET_LINK_ETHERNET);

    // Configure interface
	ethernet_init(iface);
	net_if_dormant_on(iface);
	net_if_carrier_on(data->iface);
}

#if defined(CONFIG_NET_STATISTICS_WIFI)
static int esp_hosted_mgmt_wifi_stats(const struct device *dev, struct net_stats_wifi *stats)
{
	esp_hosted_data_t *data = dev->data;

	//stats->bytes.received = data->stats.bytes.received;
	//stats->bytes.sent = data->stats.bytes.sent;
	//stats->pkts.rx = data->stats.pkts.rx;
	//stats->pkts.tx = data->stats.pkts.tx;
	//stats->errors.rx = data->stats.errors.rx;
	//stats->errors.tx = data->stats.errors.tx;
	//stats->broadcast.rx = data->stats.broadcast.rx;
	//stats->broadcast.tx = data->stats.broadcast.tx;
	//stats->multicast.rx = data->stats.multicast.rx;
	//stats->multicast.tx = data->stats.multicast.tx;
	//stats->sta_mgmt.beacons_rx = data->stats.sta_mgmt.beacons_rx;
	//stats->sta_mgmt.beacons_miss = data->stats.sta_mgmt.beacons_miss;

	return 0;
}
#endif

static enum ethernet_hw_caps esp_hosted_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);

	return ETHERNET_HW_FILTERING;
}

static int esp_hosted_init(const struct device *dev)
{
	int ret;
	esp_hosted_data_t *data = dev->data;

    // Pins config and SPI init.
    if (esp_hosted_hal_init(dev)) {
		return -EAGAIN;
	}

    // Initialize semaphore.
	ret = k_sem_init(&data->sema, 1, 1);
	if (ret != 0) {
		LOG_ERR("k_sem_init(sema) failure");
		return ret;
	}

	k_tid_t tid = k_thread_create(&esp_hosted_event_thread,
            esp_hosted_event_stack, K_THREAD_STACK_SIZEOF(esp_hosted_event_stack),
            (k_thread_entry_t) esp_hosted_event_task, (void *) dev, NULL, NULL,
            CONFIG_ESP_HOSTED_EVENT_TASK_PRIORITY, K_INHERIT_PERMS, K_NO_WAIT);
	if (!tid) {
		LOG_ERR("ERROR spawning tx thread");
		return -EAGAIN;
	}
	k_thread_name_set(tid, "esp_hosted_event");

    // Wait for an ESPInit control event.
    CtrlMsg *ctrl_msg = NULL;

    ctrl_msg = esp_hosted_response(dev, CTRL_MSG_ID__Event_ESPInit, ESP_SYNC_REQ_TIMEOUT);
    if (ctrl_msg == NULL) {
        return -EIO;
    }
    ctrl_msg__free_unpacked(ctrl_msg, &protobuf_alloc);

    // Set WiFi mode to STA/AP.
    CtrlMsgReqSetMode ctrl_payload;

    ctrl_msg__req__set_mode__init(&ctrl_payload);
    ctrl_payload.mode = CTRL__WIFI_MODE__APSTA;
    if (esp_hosted_ctrl(dev, CTRL_MSG_ID__Req_SetWifiMode, &ctrl_payload, &ctrl_msg) != 0) {
        return -EIO;
    }
    ctrl_msg__free_unpacked(ctrl_msg, &protobuf_alloc);

    // Re/enable IRQ pin.
    //esp_hosted_hal_irq_enable(true);
	return 0;
}

static const struct wifi_mgmt_ops esp_hosted_mgmt = {
	//.scan 		= esp_hosted_mgmt_scan,
	//.connect 	    = esp_hosted_mgmt_connect,
	//.disconnect   = esp_hosted_mgmt_disconnect,
	//.ap_enable    = esp_hosted_mgmt_ap_enable,
	//.ap_disable   = esp_hosted_mgmt_ap_disable,
#if defined(CONFIG_NET_STATISTICS_WIFI)
	.get_stats	    = esp_hosted_mgmt_wifi_stats,
#endif
};

static const struct net_wifi_mgmt_offload esp_hosted_api = {
	.wifi_iface.iface_api.init = esp_hosted_mgmt_init,
	//.wifi_iface.send = esp_hosted_mgmt_send,
	.wifi_iface.get_capabilities = esp_hosted_get_capabilities,
	//.wifi_iface.set_config = esp_hosted_set_config,
	.wifi_mgmt_api = &esp_hosted_mgmt,
};

NET_DEVICE_DT_INST_DEFINE(0,
		esp_hosted_init, NULL,
		&esp_hosted_data, &esp_hosted_config,
		CONFIG_WIFI_INIT_PRIORITY, &esp_hosted_api, ETHERNET_L2,
		NET_L2_GET_CTX_TYPE(ETHERNET_L2), NET_ETH_MTU);

CONNECTIVITY_WIFI_MGMT_BIND(Z_DEVICE_DT_DEV_ID(DT_DRV_INST(0)));
