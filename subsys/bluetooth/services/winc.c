/** @file
 *  @brief GATT WINC Service
 */

/*
 * Copyright (c) 2022 Gaël PORTAY
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <strings.h>

#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>

#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>

#include <idiot-prototypes/bluetooth/services/winc.h>

/*
 * The Wi-Fi® Scan Service, allows a BLE peripheral to retrieve a list of Wi-Fi
 * networks (access points) that are in range of the ATWINC3400.
 */

/* WiFi Scan Service Variables */
static struct bt_uuid_128 wifi_scan_service_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xfb8c0001, 0xd224, 0x11e4, 0x85a1, 0x0002a5d5c51b));

static struct bt_uuid_128 ss_scanning_mode_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xfb8c0002, 0xd224, 0x11e4, 0x85a1, 0x0002a5d5c51b));

static struct bt_uuid_128 ss_ap_count_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xfb8c0003, 0xd224, 0x11e4, 0x85a1, 0x0002a5d5c51b));

static struct bt_uuid_128 ss_ap_details_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0xfb8c0100, 0xd224, 0x11e4, 0x85a1, 0x0002a5d5c51b));

static bool scanning_mode_notify;

static struct wifi_scan_service scan_service;

static ssize_t read_scanning_mode(struct bt_conn *conn,
				  const struct bt_gatt_attr *attr,
				  void *buf,
				  uint16_t len,
				  uint16_t offset)
{
	struct wifi_scan_service *val = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &val->scanning_mode,
				 sizeof(val->scanning_mode));
}

static ssize_t write_scanning_mode(struct bt_conn *conn,
				   const struct bt_gatt_attr *attr,
				   const void *buf,
				   uint16_t len,
				   uint16_t offset,
				   uint8_t flags)
{
	struct wifi_scan_service *val = attr->user_data;
	struct net_if *iface = net_if_get_default();
	uint8_t value = ((uint8_t *)buf)[offset];
	int err;

	if (value != START_SCAN)
		return -EINVAL;

	if (val->scanning_mode == SCAN_RUNNING)
		return -EBUSY;

	if (!iface)
		return -ENODEV;

	val->ap_count = 0U;
	val->scanning_mode = SCAN_RUNNING;
	err = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
	if (err)
		return err;

	return 1;
}

static void scanning_mode_ccc_changed(const struct bt_gatt_attr *attr,
				      uint16_t value)
{
	scanning_mode_notify = value == BT_GATT_CCC_NOTIFY;
}

static ssize_t scanning_mode_valid_range(struct bt_conn *conn,
					 const struct bt_gatt_attr *attr,
					 void *buf,
					 uint16_t len,
					 uint16_t offset)
{
	uint16_t value[] = {
		sys_cpu_to_le16(0),
		sys_cpu_to_le16(AP_DETAILS_MAX_LEN-1),
	};

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &value,
				 sizeof(value));
}

static const struct bt_gatt_cpf ap_count_cpf = {
	.format = BT_GATT_CPF_FORMAT_UINT8,
	.exponent = 0,
	.unit = BT_GATT_UNIT_UNITLESS,
	.name_space = BT_GATT_CPF_NAMESPACE_BTSIG,
	.description = BT_GATT_CPF_NAMESPACE_DESCRIPTION_UNKNOWN,
};

static ssize_t read_ap_count(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     void *buf,
			     uint16_t len,
			     uint16_t offset)
{
	struct wifi_scan_service *val = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &val->ap_count,
				 sizeof(val->ap_count));
}

static const struct bt_gatt_cpf ap_details_cpf = {
	.format = BT_GATT_CPF_FORMAT_STRUCT,
	.exponent = 0,
	.unit = BT_GATT_UNIT_UNITLESS,
	.name_space = BT_GATT_CPF_NAMESPACE_BTSIG,
	.description = BT_GATT_CPF_NAMESPACE_DESCRIPTION_UNKNOWN,
};

static ssize_t read_ap_details(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr,
			       void *buf,
			       uint16_t len,
			       uint16_t offset)
{
	struct wifi_scan_service *val = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, val->ap_details,
				 sizeof(val->ap_details));
}

/*
 * The Wi-Fi Connect Service, allows a BLE peripheral to configure the
 * ATWINC3400’s Wi-Fi radio to connect to an access point by providing
 * information such network name, security type, and passphrase to the
 * ATWINC3400 over a BLE connection.
 */

/* WiFi Connect Service Variables */
static struct bt_uuid_128 wifi_connect_service_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x77880001, 0xd229, 0x11e4, 0x8689, 0x0002a5d5c51b));

static struct bt_uuid_128 ss_connection_state_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x77880002, 0xd229, 0x11e4, 0x8689, 0x0002a5d5c51b));

static struct bt_uuid_128 ss_ap_parameters_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x77880003, 0xd229, 0x11e4, 0x8689, 0x0002a5d5c51b));

static bool connection_state_notify;

static struct wifi_connect_service connect_service;

static ssize_t read_connection_state(struct bt_conn *conn,
				     const struct bt_gatt_attr *attr,
				     void *buf,
				     uint16_t len,
				     uint16_t offset)
{
	struct wifi_connect_service *val = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &val->connection_state,
				 sizeof(val->connection_state));
}

static ssize_t write_connection_state(struct bt_conn *conn,
				      const struct bt_gatt_attr *attr,
				      const void *buf,
				      uint16_t len,
				      uint16_t offset,
				      uint8_t flags)
{
	struct wifi_connect_service *val = attr->user_data;
	struct net_if *iface = net_if_get_default();
	uint8_t value = ((uint8_t *)buf)[offset];
	int err;

	if (!iface)
		return -ENODEV;

	if (value == CONNECT) {
		struct wifi_connect_req_params params = { 0 };
		enum wifi_security_type sec;

		if (val->connection_state != DISCONNECTED)
			return -EINVAL;

		if (val->ap_parameters.security == OPEN)
			sec = WIFI_SECURITY_TYPE_NONE;
		else if (val->ap_parameters.security == WPA)
			sec = WIFI_SECURITY_TYPE_PSK;
		else
			return -EINVAL;

		params.ssid = val->ap_parameters.ssid;
		params.ssid_length = val->ap_parameters.ssid_len;
		if (sec != WIFI_SECURITY_TYPE_NONE) {
			params.psk = val->ap_parameters.passphrase;
			params.psk_length = val->ap_parameters.passphrase_len;
		}
		params.band = WIFI_FREQ_BAND_2_4_GHZ;
		params.channel = WIFI_CHANNEL_ANY;
		params.security = sec;
		params.timeout = SYS_FOREVER_MS;
		params.mfp = WIFI_MFP_OPTIONAL;
		val->connection_state = CONNECTING;
		err = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params,
			       sizeof(params));
		if (err)
			return err;

		return 1;
	}

	if (value != DISCONNECT)
		return -EINVAL;

	err = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
	if (err)
		return err;

	return 1;
}

static void connection_state_ccc_changed(const struct bt_gatt_attr *attr,
					 uint16_t value)
{
	connection_state_notify = value == BT_GATT_CCC_NOTIFY;
}

static const struct bt_gatt_cpf ap_parameters_cpf = {
	.format = BT_GATT_CPF_FORMAT_STRUCT,
	.exponent = 0,
	.unit = BT_GATT_UNIT_UNITLESS,
	.name_space = BT_GATT_CPF_NAMESPACE_BTSIG,
	.description = BT_GATT_CPF_NAMESPACE_DESCRIPTION_UNKNOWN,
};

static ssize_t read_ap_parameters(struct bt_conn *conn,
				  const struct bt_gatt_attr *attr,
				  void *buf,
				  uint16_t len,
				  uint16_t offset)
{
	struct wifi_connect_service *val = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 &val->ap_parameters,
				 sizeof(val->ap_parameters));
}

static ssize_t write_ap_parameters(struct bt_conn *conn,
				   const struct bt_gatt_attr *attr,
				   const void *buf,
				   uint16_t len,
				   uint16_t offset,
				   uint8_t flags)
{
	struct wifi_connect_service *val = attr->user_data;
	struct ap_parameters *value = (struct ap_parameters *)buf;
	int err;

	memcpy(&val->ap_parameters, value, len);
	err = settings_save_one("bt/winc/ap_parameters", &val->ap_parameters,
				sizeof(val->ap_parameters));
	if (err)
		return err;

	return len;
}

/* Wi-Fi Scan Service Declaration */
BT_GATT_SERVICE_DEFINE(scan_svc,
	BT_GATT_PRIMARY_SERVICE(&wifi_scan_service_uuid),
	BT_GATT_CHARACTERISTIC(&ss_scanning_mode_uuid.uuid,
		  BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
		  BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
		  read_scanning_mode,
		  write_scanning_mode,
		  &scan_service),
	BT_GATT_CCC(scanning_mode_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_DESCRIPTOR(BT_UUID_VALID_RANGE,
			   BT_GATT_PERM_READ,
			   scanning_mode_valid_range,
			   NULL,
			   NULL),
	BT_GATT_CUD("Scanning Mode", BT_GATT_PERM_READ),
	BT_GATT_CHARACTERISTIC(&ss_ap_count_uuid.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_ap_count,
			       NULL,
			       &scan_service),
	BT_GATT_CPF(&ap_count_cpf),
	BT_GATT_CUD("AP Count", BT_GATT_PERM_READ),
	BT_GATT_CHARACTERISTIC(&ss_ap_details_uuid.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_ap_details,
			       NULL,
			       &scan_service),
	BT_GATT_CPF(&ap_details_cpf),
	BT_GATT_CUD("AP Details", BT_GATT_PERM_READ),
);

/* Wi-Fi Connect Service Declaration */
BT_GATT_SERVICE_DEFINE(connect_svc,
	BT_GATT_PRIMARY_SERVICE(&wifi_connect_service_uuid),
	BT_GATT_CHARACTERISTIC(&ss_connection_state_uuid.uuid,
		  BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
		  BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
		  read_connection_state,
		  write_connection_state,
		  &connect_service),
	BT_GATT_CCC(connection_state_ccc_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CUD("Connection State", BT_GATT_PERM_READ),
	BT_GATT_CHARACTERISTIC(&ss_ap_parameters_uuid.uuid,
		  BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_AUTH,
		  BT_GATT_PERM_WRITE_AUTHEN | BT_GATT_PERM_WRITE_ENCRYPT | \
		  BT_GATT_PERM_READ_AUTHEN | BT_GATT_PERM_READ_ENCRYPT,
		  read_ap_parameters,
		  write_ap_parameters,
		  &connect_service),
	BT_GATT_CPF(&ap_parameters_cpf),
	BT_GATT_CUD("AP Parameters", BT_GATT_PERM_READ),
);

static struct net_mgmt_event_callback wifi_mgmt_cb;

static void handle_wifi_scan_result(struct net_mgmt_event_callback *cb)
{
	const struct wifi_scan_result *entry =
		(const struct wifi_scan_result *)cb->info;

	if (scan_service.ap_count == 0U) {
		printk("\n%-4s | %-32s %-5s | %-13s | %-4s | %-15s\n",
		       "Num", "SSID", "(len)", "Channel", "RSSI", "Security");
	}

	printk("%-4d | %-32s %-5u | %-4u (%-6s) | %-4d | %-15s\n",
	       scan_service.ap_count, entry->ssid, entry->ssid_length,
	       entry->channel, wifi_band_txt(entry->band), entry->rssi,
	       wifi_security_txt(entry->security));

	if (scan_service.ap_count < AP_DETAILS_MAX_LEN) {
		struct ap_detail *ap;
		uint8_t sec;

		if (entry->security == WIFI_SECURITY_TYPE_NONE)
			sec = OPEN;
		else if (entry->security == WIFI_SECURITY_TYPE_PSK)
			sec = WPA;
		else
			return;

		ap = &scan_service.ap_details[scan_service.ap_count];
		ap->security = sec;
		ap->rssi = entry->rssi;
		ap->ssid_len = entry->ssid_length;
		memcpy(ap->ssid, entry->ssid, entry->ssid_length);
		scan_service.ap_count++;
	}
}

static void handle_wifi_scan_done(struct net_mgmt_event_callback *cb)
{
	const struct wifi_status *status =
		(const struct wifi_status *)cb->info;

	if (status->status)
		printk("Scan request failed (%d)\n", status->status);
	else
		printk("Scan request done\n");

	scan_service.scanning_mode = SCAN_DONE;

	if (scanning_mode_notify) {
		const struct bt_gatt_attr *chrc;
		
		chrc = bt_gatt_find_by_uuid(scan_svc.attrs,
					    scan_svc.attr_count,
					    &ss_scanning_mode_uuid.uuid);
		if (chrc == NULL)
			return;

		bt_gatt_notify(NULL, chrc, &scan_service.scanning_mode,
			       sizeof(scan_service.scanning_mode));
	}
}

static void handle_wifi_connect_result(struct net_mgmt_event_callback *cb)
{
	const struct wifi_status *status =
		(const struct wifi_status *) cb->info;

	if (status->status)
		printk("Connection request failed (%d)\n", status->status);
	else
		printk("Connected\n");

	connect_service.connection_state = CONNECTED;

	if (connection_state_notify) {
		const struct bt_gatt_attr *chrc;
		
		chrc = bt_gatt_find_by_uuid(connect_svc.attrs,
					    connect_svc.attr_count,
					    &ss_connection_state_uuid.uuid);
		if (chrc == NULL)
			return;

		bt_gatt_notify(NULL, chrc, &connect_service.connection_state,
			       sizeof(connect_service.connection_state));
	}
}

static void handle_wifi_disconnect_result(struct net_mgmt_event_callback *cb)
{
	const struct wifi_status *status =
		(const struct wifi_status *) cb->info;

	if (connect_service.connection_state == CONNECTING)
		printk("Disconnection request %s (%d)\n",
		       status->status ? "failed" : "done", status->status);
	else
		printk("Disconnected\n");

	connect_service.connection_state = DISCONNECTED;

	if (connection_state_notify) {
		const struct bt_gatt_attr *chrc;
		
		chrc = bt_gatt_find_by_uuid(connect_svc.attrs,
					    connect_svc.attr_count,
					    &ss_connection_state_uuid.uuid);
		if (chrc == NULL)
			return;

		bt_gatt_notify(NULL, chrc, &connect_service.connection_state,
			       sizeof(connect_service.connection_state));
	}
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				    uint32_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_SCAN_RESULT:
		handle_wifi_scan_result(cb);
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		handle_wifi_scan_done(cb);
		break;
	case NET_EVENT_WIFI_CONNECT_RESULT:
		handle_wifi_connect_result(cb);
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		handle_wifi_disconnect_result(cb);
		break;
	default:
		break;
	}
}

#if defined(CONFIG_BT_WINC_SETTINGS)
static int winc_set(const char *name, size_t len_rd,
		    settings_read_cb read_cb, void *store)
{
	const char *next;
	int len;

	len = settings_name_next(name, &next);
	if (!strncmp(name, "ap_parameters", len)) {
		ssize_t siz;

		siz = read_cb(store, &connect_service.ap_parameters,
			      sizeof(connect_service.ap_parameters));
		if (siz < 0) {
			printk("Read settings failed (%zd)\n", siz);
			return siz;
		}
	}

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(bt_winc, "bt/winc", NULL, winc_set, NULL, NULL);
#endif /* CONFIG_BT_WINC_SETTINGS*/

static int winc_init(const struct device *dev)
{
	net_mgmt_init_event_callback(&wifi_mgmt_cb,
				     wifi_mgmt_event_handler,
				     NET_EVENT_WIFI_SCAN_RESULT |
				     NET_EVENT_WIFI_SCAN_DONE |
				     NET_EVENT_WIFI_CONNECT_RESULT |
				     NET_EVENT_WIFI_DISCONNECT_RESULT);

	net_mgmt_add_event_callback(&wifi_mgmt_cb);

	return 0;
}

struct ap_parameters *bt_winc_get_ap_parameters(void)
{
	return &connect_service.ap_parameters;
}

SYS_INIT(winc_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);