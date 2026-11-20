#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace Config
{
// -----------------------------------------------------------------------------
// Device identity / backend protocol
// -----------------------------------------------------------------------------
// Public template defaults are intentionally not connected to a live deployment.
// Configure a unique node ID, broker, MQTT topics and backend record IDs on each
// device before using production telemetry. Existing NVS values are not changed.
constexpr char DEFAULT_NODE_ID[] = "room-monitor-001";
constexpr char DEFAULT_MQTT_HOST[] = "broker.example.invalid";
constexpr uint16_t DEFAULT_MQTT_PORT = 1883;
constexpr char DEFAULT_MQTT_PUBLISH_TOPIC[] = "room-monitor/telemetry";
constexpr char DEFAULT_MQTT_SUBSCRIBE_TOPIC[] = "room-monitor/commands";
constexpr uint8_t DEFAULT_MQTT_QOS = 1;
constexpr char WIFI_MANAGER_AP_NAME[] = "RoomMonitoring-WIFI-MANAGER";

// Replace these non-production IDs through the protected Configuration page.
// The application accepts backend-specific identifiers; these are not real IDs.
constexpr char ID_TEMPERATURE[] = "CONFIGURE_TEMPERATURE_ID";
constexpr char ID_HUMIDITY[] = "CONFIGURE_HUMIDITY_ID";
constexpr char ID_CO2[] = "CONFIGURE_CO2_ID";
constexpr char ID_AIR_QUALITY[] = "CONFIGURE_AIR_QUALITY_ID";
constexpr char ID_LIGHT[] = "CONFIGURE_LIGHT_ID";
constexpr char ID_GAS[] = "CONFIGURE_GAS_ID";
constexpr char ID_PRESSURE[] = "CONFIGURE_PRESSURE_ID";
constexpr char ID_SOUND[] = "CONFIGURE_SOUND_ID";
constexpr char ID_IP[] = "CONFIGURE_IP_ID";
constexpr char ID_MAC[] = "CONFIGURE_MAC_ID";
constexpr char ID_ALIVE[] = "CONFIGURE_ALIVE_ID";
constexpr char ID_LOCAL_ADDRESS[] = "CONFIGURE_LOCAL_ADDRESS_ID";
constexpr char ID_PING[] = "CONFIGURE_PING_ID";

// -----------------------------------------------------------------------------
// MQTT implementation limits (deployment values are stored in NVS)
// -----------------------------------------------------------------------------
constexpr uint8_t MAX_INFLIGHT_MESSAGES = 5;
constexpr size_t MAX_QUEUE_DEPTH = 100;
constexpr uint32_t RECONNECT_DELAY_MS = 2000; // Legacy compatibility
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
constexpr size_t MQTT_CA_CERT_MAX_LENGTH = 3500;
// ESP-MQTT uses separate receive/transmit buffers. Room Monitor command
// messages are small and the event handler already supports fragmented input,
// so a 1 KiB receive buffer saves heap. Keep a 4 KiB transmit buffer because
// the configurable full telemetry payload can exceed 2 KiB with long IDs.
constexpr int MQTT_IN_BUFFER_SIZE = 1024;
constexpr int MQTT_OUT_BUFFER_SIZE = 4096;


// -----------------------------------------------------------------------------
// Preferences / NVS persistent configuration
// -----------------------------------------------------------------------------
// Runtime configuration lives in ESP32 NVS, not in SPIFFS. This separation is
// intentional: a filesystem OTA can replace the dashboard without erasing
// Wi-Fi credentials, calibration offsets, deadbands, or reporting intervals.
constexpr char NVS_NAMESPACE[] = "roommonitor";
constexpr uint16_t SETTINGS_SCHEMA_VERSION = 4;

// ESP32 Preferences keys must stay short (15 characters maximum).
namespace NvsKey
{
constexpr char SCHEMA[] = "schema";
constexpr char WIFI_SSID[] = "wifi_ssid";
constexpr char WIFI_PASSWORD[] = "wifi_pass";
constexpr char LAST_IP[] = "last_ip";
constexpr char NODE_ID[] = "node_id";
constexpr char MQTT_HOST[] = "mqtt_host";
constexpr char MQTT_PORT[] = "mqtt_port";
constexpr char MQTT_PUBLISH[] = "mqtt_pub";
constexpr char MQTT_SUBSCRIBE[] = "mqtt_sub";
constexpr char MQTT_QOS[] = "mqtt_qos";
constexpr char MQTT_USERNAME[] = "mqtt_user";
constexpr char MQTT_PASSWORD[] = "mqtt_pass";
constexpr char MQTT_CLIENT_ID[] = "mqtt_client";
constexpr char MQTT_KEEPALIVE[] = "mqtt_keep";
constexpr char MQTT_CLEAN[] = "mqtt_clean";
constexpr char MQTT_RETAIN[] = "mqtt_retain";
constexpr char MQTT_TLS[] = "mqtt_tls";
constexpr char MQTT_CA[] = "mqtt_ca";
constexpr char SEND_INTERVAL[] = "send_sec";
constexpr char ALIVE_INTERVAL[] = "alive_sec";
constexpr char OFFSET_TEMPERATURE[] = "off_temp";
constexpr char OFFSET_HUMIDITY[] = "off_hum";
constexpr char OFFSET_IAQ[] = "off_iaq";
constexpr char OFFSET_CO2[] = "off_co2";
constexpr char OFFSET_VOC[] = "off_voc";
constexpr char OFFSET_PRESSURE[] = "off_press";
constexpr char OFFSET_LIGHT[] = "off_light";
constexpr char OFFSET_SOUND[] = "off_sound";
constexpr char DB_TEMPERATURE[] = "db_temp";
constexpr char DB_HUMIDITY[] = "db_hum";
constexpr char DB_PRESSURE[] = "db_press";
constexpr char DB_IAQ[] = "db_iaq";
constexpr char DB_CO2[] = "db_co2";
constexpr char DB_VOC[] = "db_voc";
constexpr char DB_SOUND[] = "db_sound";
constexpr char DB_LIGHT[] = "db_light";
constexpr char ID_TEMPERATURE[] = "id_temp";
constexpr char ID_HUMIDITY[] = "id_hum";
constexpr char ID_CO2[] = "id_co2";
constexpr char ID_AIR_QUALITY[] = "id_aq";
constexpr char ID_LIGHT[] = "id_light";
constexpr char ID_GAS[] = "id_gas";
constexpr char ID_PRESSURE[] = "id_press";
constexpr char ID_SOUND[] = "id_sound";
constexpr char ID_IP[] = "id_ip";
constexpr char ID_MAC[] = "id_mac";
constexpr char ID_ALIVE[] = "id_alive";
constexpr char ID_LOCAL_ADDRESS[] = "id_local";
constexpr char ID_PING[] = "id_ping";
constexpr char ADMIN_CREDENTIAL[] = "adm_cred";
constexpr char BOOT_COUNT[] = "boot_count";
constexpr char WDT_COUNT[] = "wdt_count";
constexpr char OTA_RESULT[] = "ota_result";
constexpr char OTA_SHA256[] = "ota_sha";
constexpr char OTA_TARGET[] = "ota_target";
} // namespace NvsKey

// -----------------------------------------------------------------------------
// Legacy SPIFFS setting paths - migration only
// -----------------------------------------------------------------------------
// These are no longer used for active settings. On the first boot of this
// firmware, SettingsManager can import them into NVS so already-deployed units
// keep their configuration. They are retained here only for that one-time
// compatibility path and for factory-reset cleanup.
namespace LegacyPath
{
constexpr char SSID[] = "/ssid.txt";
constexpr char PASSWORD[] = "/pass.txt";
constexpr char IP[] = "/ip.txt";
constexpr char SEND_INTERVAL[] = "/send_interval.txt";
constexpr char ALIVE_INTERVAL[] = "/send_IamAlive_interval.txt";
constexpr char OFFSET_TEMPERATURE[] = "/temperature_offset.txt";
constexpr char OFFSET_HUMIDITY[] = "/humidity_offset.txt";
constexpr char OFFSET_IAQ[] = "/airQuality_offset.txt";
constexpr char OFFSET_CO2[] = "/co2_offset.txt";
constexpr char OFFSET_VOC[] = "/VOC_offset.txt";
constexpr char OFFSET_PRESSURE[] = "/pressur_offset.txt";
constexpr char OFFSET_LIGHT[] = "/ligth_offset.txt";
constexpr char OFFSET_SOUND[] = "/sound_offset.txt";
constexpr char DB_TEMPERATURE[] = "/diff_temperature.txt";
constexpr char DB_HUMIDITY[] = "/diff_humidity.txt";
constexpr char DB_PRESSURE[] = "/diff_pressure.txt";
constexpr char DB_IAQ[] = "/diff_airQuality.txt";
constexpr char DB_CO2[] = "/diff_co2.txt";
constexpr char DB_VOC[] = "/diff_voc.txt";
constexpr char DB_SOUND[] = "/diff_sound.txt";
constexpr char DB_LIGHT[] = "/diff_ligth.txt";
} // namespace LegacyPath

// -----------------------------------------------------------------------------
// Timing
// -----------------------------------------------------------------------------
constexpr uint32_t DEFAULT_SEND_INTERVAL_SEC = 1800;
constexpr uint32_t DEFAULT_ALIVE_INTERVAL_SEC = 1800;
constexpr uint32_t SENSOR_INTERVAL_MS = 20000;
constexpr uint32_t DISPLAY_INTERVAL_MS = 3000;

// -----------------------------------------------------------------------------
// LED matrix
// -----------------------------------------------------------------------------
constexpr uint8_t MAX_DISPLAY_DEVICES = 8;
constexpr uint8_t DISPLAY_CLK_PIN = 18;
constexpr uint8_t DISPLAY_DATA_PIN = 19;
constexpr uint8_t DISPLAY_CS_PIN = 23;
constexpr uint16_t DISPLAY_SPEED = 75;
constexpr size_t DISPLAY_BUFFER_SIZE = 24;

// -----------------------------------------------------------------------------
// Sound sensor
// -----------------------------------------------------------------------------
constexpr uint8_t SOUND_PIN = 36;
constexpr uint16_t SOUND_SAMPLE_COUNT = 25;
constexpr uint16_t SOUND_SAMPLE_DELAY_MS = 5;
constexpr float SOUND_DISCONNECTED_MV = 50.0f;

// -----------------------------------------------------------------------------
// Factory reset
// -----------------------------------------------------------------------------
constexpr uint8_t FACTORY_RESET_PIN = 35;
constexpr int FACTORY_RESET_THRESHOLD = 500;
constexpr uint32_t FACTORY_RESET_CONFIRM_MS = 2000;

// -----------------------------------------------------------------------------
// FreeRTOS tasks
// -----------------------------------------------------------------------------
constexpr uint32_t TASK_STACK_SIZE = 10000;
constexpr uint8_t TASK_PRIORITY = 1;
constexpr uint8_t SENSOR_TASK_CORE = 0;
constexpr uint8_t SERVICE_TASK_CORE = 1;
constexpr uint32_t TASK_DELAY_MS = 20;
constexpr uint32_t TASK_WATCHDOG_TIMEOUT_SEC = 15;

// Diagnostics / health thresholds.
constexpr uint32_t BSEC_STALE_AFTER_MS = 30000;
constexpr uint32_t SOUND_LIGHT_STALE_AFTER_MS = 60000;
constexpr uint32_t MQTT_TEST_TIMEOUT_MS = 5000;
// When the production MQTT client is already connected, an independent
// second ESP-MQTT/TLS session is allowed only with comfortable heap margin.
// Below these thresholds the commissioning test reports a partial result
// instead of risking a false TLS failure caused by memory pressure.
constexpr uint32_t MQTT_TEST_MIN_FREE_HEAP_BYTES = 65536;
constexpr uint32_t MQTT_TEST_MIN_LARGEST_BLOCK_BYTES = 40960;
constexpr uint32_t MQTT_TEST_TCP_TIMEOUT_MS = 2500;
} // namespace Config
