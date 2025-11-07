#include "unity_config.h"
#include <unity.h>

#include "phoenix_ble_server.hpp"

static const char* k_device_name = "Phoenix Mock";
static const char* k_service_uuid = "b5c5d4f4-7207-4e8d-9d6b-60b98e65ce09";
static const char* k_command_uuid = "c1883ec3-d984-4dcd-9d67-41ade54c5f2a";
static const char* k_notification_uuid = "cd1411cc-4ab8-46e8-9ad5-c41a871caf41";

static PhoenixBleConfig create_valid_config(void)
{
    PhoenixBleConfig config = {};
    config.device_name = k_device_name;
    config.service_uuid = k_service_uuid;
    config.command_characteristic_uuid = k_command_uuid;
    config.notification_characteristic_uuid = k_notification_uuid;
    config.preferred_mtu = 247U;
    config.preferred_connection_interval_min_ms = 20U;
    config.preferred_connection_interval_max_ms = 40U;
    return config;
}

static PhoenixBleServerContext g_context;

static PhoenixBleStatus test_command_handler(const uint8_t* payload, uint16_t payload_length)
{
    (void) payload;
    (void) payload_length;
    return PHX_OK;
}

void setUp(void)
{
    g_context.is_initialized = 0U;
    g_context.characteristic_ids.command_characteristic_id = 0U;
    g_context.characteristic_ids.notification_characteristic_id = 0U;
    g_context.command_handler = NULL;
}

void tearDown(void)
{
}

static void test_initialize_rejects_null_context(void)
{
    PhoenixBleConfig config = create_valid_config();

    const PhoenixBleStatus status = phoenix_ble_server_initialize(NULL, &config);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_initialize_rejects_null_config(void)
{
    const PhoenixBleStatus status = phoenix_ble_server_initialize(&g_context, NULL);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_initialize_rejects_missing_device_name(void)
{
    PhoenixBleConfig config = create_valid_config();
    config.device_name = NULL;

    const PhoenixBleStatus status = phoenix_ble_server_initialize(&g_context, &config);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_initialize_rejects_missing_service_uuid(void)
{
    PhoenixBleConfig config = create_valid_config();
    config.service_uuid = NULL;

    const PhoenixBleStatus status = phoenix_ble_server_initialize(&g_context, &config);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_initialize_rejects_missing_command_uuid(void)
{
    PhoenixBleConfig config = create_valid_config();
    config.command_characteristic_uuid = NULL;

    const PhoenixBleStatus status = phoenix_ble_server_initialize(&g_context, &config);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_initialize_rejects_missing_notification_uuid(void)
{
    PhoenixBleConfig config = create_valid_config();
    config.notification_characteristic_uuid = NULL;

    const PhoenixBleStatus status = phoenix_ble_server_initialize(&g_context, &config);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_initialize_rejects_mtu_below_minimum(void)
{
    PhoenixBleConfig config = create_valid_config();
    config.preferred_mtu = 22U;

    const PhoenixBleStatus status = phoenix_ble_server_initialize(&g_context, &config);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_initialize_rejects_mtu_above_maximum(void)
{
    PhoenixBleConfig config = create_valid_config();
    config.preferred_mtu = 248U;

    const PhoenixBleStatus status = phoenix_ble_server_initialize(&g_context, &config);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_initialize_rejects_invalid_connection_interval_range(void)
{
    PhoenixBleConfig config = create_valid_config();
    config.preferred_connection_interval_min_ms = 50U;
    config.preferred_connection_interval_max_ms = 40U;

    const PhoenixBleStatus status = phoenix_ble_server_initialize(&g_context, &config);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_initialize_rejects_zero_connection_interval(void)
{
    PhoenixBleConfig config = create_valid_config();
    config.preferred_connection_interval_min_ms = 0U;

    const PhoenixBleStatus status = phoenix_ble_server_initialize(&g_context, &config);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_register_command_handler_requires_init(void)
{
    const PhoenixBleStatus status = phoenix_ble_server_register_command_handler(&g_context, NULL);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_NOT_INITIALIZED, status);
}

static void test_register_command_handler_rejects_null_context(void)
{
    const PhoenixBleStatus status = phoenix_ble_server_register_command_handler(NULL, test_command_handler);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_register_command_handler_rejects_null_handler_after_init(void)
{
    g_context.is_initialized = 1U;

    const PhoenixBleStatus status = phoenix_ble_server_register_command_handler(&g_context, NULL);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_start_advertising_rejects_null_context(void)
{
    const PhoenixBleStatus status = phoenix_ble_server_start_advertising(NULL);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_start_advertising_requires_initialization(void)
{
    const PhoenixBleStatus status = phoenix_ble_server_start_advertising(&g_context);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_NOT_INITIALIZED, status);
}

static void test_stop_advertising_rejects_null_context(void)
{
    const PhoenixBleStatus status = phoenix_ble_server_stop_advertising(NULL);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_stop_advertising_requires_initialization(void)
{
    const PhoenixBleStatus status = phoenix_ble_server_stop_advertising(&g_context);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_NOT_INITIALIZED, status);
}

static void test_is_connected_rejects_null_context(void)
{
    uint8_t is_connected = 0U;

    const PhoenixBleStatus status = phoenix_ble_server_is_connected(NULL, &is_connected);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_is_connected_rejects_null_output_parameter(void)
{
    const PhoenixBleStatus status = phoenix_ble_server_is_connected(&g_context, NULL);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_is_connected_requires_initialization(void)
{
    uint8_t is_connected = 0U;

    const PhoenixBleStatus status = phoenix_ble_server_is_connected(&g_context, &is_connected);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_NOT_INITIALIZED, status);
}

static void test_send_notification_rejects_null_context(void)
{
    static const uint8_t k_payload[] = {0x01U};

    const PhoenixBleStatus status = phoenix_ble_server_send_notification(NULL, k_payload, sizeof(k_payload));

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_send_notification_rejects_null_payload(void)
{
    const PhoenixBleStatus status = phoenix_ble_server_send_notification(&g_context, NULL, 1U);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_send_notification_rejects_zero_length_payload(void)
{
    static const uint8_t k_payload[] = {0x01U};

    const PhoenixBleStatus status = phoenix_ble_server_send_notification(&g_context, k_payload, 0U);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_send_notification_requires_initialization(void)
{
    static const uint8_t k_payload[] = {0x01U};

    const PhoenixBleStatus status = phoenix_ble_server_send_notification(&g_context, k_payload, sizeof(k_payload));

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_NOT_INITIALIZED, status);
}

extern "C" void setup(void)
{
    UNITY_SETUP_SERIAL_DEFAULT();
    UNITY_BEGIN();

    RUN_TEST(test_initialize_rejects_null_context);
    RUN_TEST(test_initialize_rejects_null_config);
    RUN_TEST(test_initialize_rejects_missing_device_name);
    RUN_TEST(test_initialize_rejects_missing_service_uuid);
    RUN_TEST(test_initialize_rejects_missing_command_uuid);
    RUN_TEST(test_initialize_rejects_missing_notification_uuid);
    RUN_TEST(test_initialize_rejects_mtu_below_minimum);
    RUN_TEST(test_initialize_rejects_mtu_above_maximum);
    RUN_TEST(test_initialize_rejects_invalid_connection_interval_range);
    RUN_TEST(test_initialize_rejects_zero_connection_interval);
    RUN_TEST(test_register_command_handler_requires_init);
    RUN_TEST(test_register_command_handler_rejects_null_context);
    RUN_TEST(test_register_command_handler_rejects_null_handler_after_init);
    RUN_TEST(test_start_advertising_rejects_null_context);
    RUN_TEST(test_start_advertising_requires_initialization);
    RUN_TEST(test_stop_advertising_rejects_null_context);
    RUN_TEST(test_stop_advertising_requires_initialization);
    RUN_TEST(test_is_connected_rejects_null_context);
    RUN_TEST(test_is_connected_rejects_null_output_parameter);
    RUN_TEST(test_is_connected_requires_initialization);
    RUN_TEST(test_send_notification_rejects_null_context);
    RUN_TEST(test_send_notification_rejects_null_payload);
    RUN_TEST(test_send_notification_rejects_zero_length_payload);
    RUN_TEST(test_send_notification_requires_initialization);

    UNITY_END();
}

extern "C" void loop(void)
{
}
