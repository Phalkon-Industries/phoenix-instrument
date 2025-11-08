#include "unity_config.h"
#include <unity.h>

#include <string.h>

#include "phoenix_ble_data_packing.hpp"

static void test_pack_notification_writes_expected_json(void)
{
    char   buffer[128] = {};
    size_t bytes_written = 0U;

    const PhoenixBleStatus status = phoenix_ble_pack_notification_message(
        "sample", "{\"value\":42}", buffer, sizeof(buffer), &bytes_written);

    TEST_ASSERT_EQUAL_INT32(PHX_OK, status);
    TEST_ASSERT_EQUAL_STRING("{\"command\":\"sample\",\"parameters\":{\"value\":42}}\n", buffer);
    TEST_ASSERT_EQUAL_UINT32(strlen(buffer), static_cast<uint32_t>(bytes_written));
}

static void test_pack_notification_rejects_null_command(void)
{
    char   buffer[16] = {};
    size_t bytes_written = 0U;

    const PhoenixBleStatus status = phoenix_ble_pack_notification_message(
        NULL, "{}", buffer, sizeof(buffer), &bytes_written);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_pack_notification_rejects_null_parameters(void)
{
    char   buffer[16] = {};
    size_t bytes_written = 0U;

    const PhoenixBleStatus status = phoenix_ble_pack_notification_message(
        "cmd", NULL, buffer, sizeof(buffer), &bytes_written);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_pack_notification_rejects_null_buffer(void)
{
    size_t bytes_written = 0U;

    const PhoenixBleStatus status = phoenix_ble_pack_notification_message(
        "cmd", "{}", NULL, 0U, &bytes_written);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_pack_notification_rejects_null_output_length(void)
{
    char buffer[16] = {};

    const PhoenixBleStatus status = phoenix_ble_pack_notification_message(
        "cmd", "{}", buffer, sizeof(buffer), NULL);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_pack_notification_rejects_empty_command(void)
{
    char   buffer[16] = {};
    size_t bytes_written = 0U;

    const PhoenixBleStatus status = phoenix_ble_pack_notification_message(
        "", "{}", buffer, sizeof(buffer), &bytes_written);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

static void test_pack_notification_rejects_insufficient_buffer(void)
{
    char   buffer[16] = {};
    size_t bytes_written = 0U;

    const PhoenixBleStatus status = phoenix_ble_pack_notification_message(
        "cmd", "{}", buffer, 8U, &bytes_written);

    TEST_ASSERT_EQUAL_INT32(PHX_ERR_INVALID_ARG, status);
}

extern "C" void phoenix_ble_register_data_packing_tests(void)
{
    RUN_TEST(test_pack_notification_writes_expected_json);
    RUN_TEST(test_pack_notification_rejects_null_command);
    RUN_TEST(test_pack_notification_rejects_null_parameters);
    RUN_TEST(test_pack_notification_rejects_null_buffer);
    RUN_TEST(test_pack_notification_rejects_null_output_length);
    RUN_TEST(test_pack_notification_rejects_empty_command);
    RUN_TEST(test_pack_notification_rejects_insufficient_buffer);
}
