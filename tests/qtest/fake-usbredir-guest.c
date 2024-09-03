/*
 * Fake USB Redirect Guest
 *
 * Copyright (C) 2024 Google, LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include <libusb-1.0/libusb.h>
#include <usbredirparser.h>
#include <usbredirproto.h>

#include "qemu/osdep.h"
#include "qemu/queue.h"
#include "fake-usbredir-guest.h"

#define EP_INDEX(endpoint_address) \
    ((endpoint_address & 0x80) >> 3 | (endpoint_address & 0x0f))

#define VERSION "qemu fake usb-redir guest " QEMU_VERSION
#define USB_REDIR_MAX_NUM_ENDPOINTS 32
#define USB_REDIR_EP_IN 0x10

static inline void wait_for_flag(FakeUsbredirGuest *faker, sem_t *sem,
                                 bool *flag)
{
    g_mutex_lock(&faker->flag_mu);
    if (*flag) {
        g_mutex_unlock(&faker->flag_mu);
        return;
    }
    g_mutex_unlock(&faker->flag_mu);
    sem_wait(sem);
}

static void parser_log(void *priv, int level, const char *msg)
{
    /* Do nothing. We don't care about logs in test. */
    return;
}

static int parser_read(void *priv, uint8_t *data, int count)
{
    FakeUsbredirGuest *faker = priv;
    return read(faker->fd, data, count);
}

static int parser_write(void *priv, uint8_t *data, int count)
{
    FakeUsbredirGuest *faker = priv;
    return write(faker->fd, data, count);
}

static void parser_hello(void *priv, struct usb_redir_hello_header *h)
{
    FakeUsbredirGuest *faker = priv;

    sem_post(&faker->helloed_sem);
}

static void parser_interface_info(
    void *priv, struct usb_redir_interface_info_header *interface_info)
{
    FakeUsbredirGuest *faker = priv;

    g_mutex_lock(&faker->flag_mu);
    faker->received_if_info = true;
    memcpy(&faker->if_info, interface_info,
           sizeof(struct usb_redir_interface_info_header));
    sem_post(&faker->if_info_sem);
    g_mutex_unlock(&faker->flag_mu);
}

static void parser_ep_info(void *priv, struct usb_redir_ep_info_header *ep_info)
{
    FakeUsbredirGuest *faker = priv;

    g_mutex_lock(&faker->flag_mu);
    faker->received_ep_info = true;
    memcpy(&faker->ep_info, ep_info, sizeof(struct usb_redir_ep_info_header));
    sem_post(&faker->ep_info_sem);
    g_mutex_unlock(&faker->flag_mu);
}

static void parser_device_connect(
    void *priv, struct usb_redir_device_connect_header *device_connect)
{
    FakeUsbredirGuest *faker = priv;

    /* Interface info and ep info must be received before device connect */
    g_mutex_lock(&faker->flag_mu);
    g_assert(faker->received_if_info && faker->received_ep_info);
    faker->device_connected = true;
    g_mutex_unlock(&faker->flag_mu);

    memcpy(&faker->device_info, device_connect,
           sizeof(struct usb_redir_device_connect_header));
    sem_post(&faker->device_info_sem);

    usbredirparser_send_reset(faker->parser);
    usbredirparser_do_write(faker->parser);
}

static void parser_control_transfer(
    void *priv, uint64_t id,
    struct usb_redir_control_packet_header *control_header, uint8_t *data,
    int data_len)
{
    FakeUsbredirGuest *faker = priv;

    g_mutex_lock(&faker->flag_mu);
    g_assert(faker->device_connected);
    g_mutex_unlock(&faker->flag_mu);

    g_assert(faker->control_data_packet);

    faker->control_data_packet->packet.done = true;

    if (faker->control_data_packet->packet.canceled) {
        g_assert(control_header->status == usb_redir_cancelled);
        g_assert_cmphex(control_header->length, ==, 0);
    } else {
        g_assert(control_header->status == usb_redir_success);
        g_assert_cmphex(control_header->length, <=,
                        faker->control_data_packet->header.length);

        if (control_header->requesttype & LIBUSB_ENDPOINT_IN) {
            faker->control_data_packet->packet.data_length = data_len;
            faker->control_data_packet->packet.data = g_new(uint8_t, data_len);
            memcpy(faker->control_data_packet->packet.data, data, data_len);
        }
    }

    g_assert_cmphex(control_header->endpoint, ==,
                    faker->control_data_packet->header.endpoint);
    g_assert_cmphex(control_header->requesttype, ==,
                    faker->control_data_packet->header.requesttype);
    g_assert_cmphex(control_header->request, ==,
                    faker->control_data_packet->header.request);
    g_assert_cmphex(control_header->value, ==,
                    faker->control_data_packet->header.value);
    g_assert_cmphex(control_header->index, ==,
                    faker->control_data_packet->header.index);

    sem_post(&faker->control_transfer_sem);
}

static void parser_bulk_transfer(
    void *priv, uint64_t id, struct usb_redir_bulk_packet_header *bulk_header,
    uint8_t *data, int data_len)
{
    FakeUsbredirGuest *faker = priv;
    BulkDataPacket *bulk_data_packet;

    g_mutex_lock(&faker->flag_mu);
    g_assert(faker->device_connected);
    g_mutex_unlock(&faker->flag_mu);

    QTAILQ_FOREACH(bulk_data_packet, &faker->bulk_data_packet_queue, next)
    {
        if (bulk_data_packet->packet.id == id) {
            break;
        }
    }

    g_assert(bulk_data_packet);

    bulk_data_packet->packet.done = true;

    if (bulk_data_packet->packet.canceled) {
        g_assert(bulk_header->status == usb_redir_cancelled);
    } else {
        g_assert(bulk_header->status == usb_redir_success);

        if (bulk_header->endpoint & LIBUSB_ENDPOINT_IN) {
            g_assert(bulk_data_packet->packet.data == NULL);
            bulk_data_packet->packet.data_length = data_len;
            bulk_data_packet->packet.data = g_new(uint8_t, data_len);
            memcpy(bulk_data_packet->packet.data, data, data_len);
        }
    }

    g_assert_cmphex(bulk_header->endpoint, ==,
                    bulk_data_packet->header.endpoint);
    g_assert_cmphex(bulk_header->stream_id, ==,
                    bulk_data_packet->header.stream_id);

    sem_post(&faker->bulk_transfer_sem);
}

static void parser_configuration_status(
    void *priv, uint64_t id,
    struct usb_redir_configuration_status_header *configuration_status)
{
    FakeUsbredirGuest *faker = priv;

    g_mutex_lock(&faker->flag_mu);
    g_assert(faker->device_connected);
    g_mutex_unlock(&faker->flag_mu);

    g_assert(configuration_status->status == usb_redir_success);
    g_assert_cmphex(configuration_status->configuration, ==,
                    faker->configuration_value);
}

static void parser_alt_setting_status(
    void *priv, uint64_t id,
    struct usb_redir_alt_setting_status_header *alt_setting_status)
{
    FakeUsbredirGuest *faker = priv;

    g_assert_cmphex(alt_setting_status->status, ==, usb_redir_success);
    g_assert_cmphex(alt_setting_status->interface, ==, faker->interface_num);
    g_assert_cmphex(alt_setting_status->alt, ==, faker->alt_setting);
}

void fake_usbredir_guest_init(FakeUsbredirGuest *faker, int fd)
{
    faker->parser = usbredirparser_create();
    g_assert(faker->parser);

    g_mutex_init(&faker->flag_mu);
    sem_init(&faker->helloed_sem, 0, 0);
    sem_init(&faker->if_info_sem, 0, 0);
    sem_init(&faker->ep_info_sem, 0, 0);
    sem_init(&faker->device_info_sem, 0, 0);
    sem_init(&faker->control_transfer_sem, 0, 0);
    sem_init(&faker->bulk_transfer_sem, 0, 0);

    faker->fd = fd;
    faker->packet_id = 0;
    faker->configuration_value = 0;
    faker->control_data_packet = NULL;
    QTAILQ_INIT(&faker->bulk_data_packet_queue);

    faker->parser->priv = faker;
    faker->parser->log_func = parser_log;
    faker->parser->read_func = parser_read;
    faker->parser->write_func = parser_write;
    faker->parser->hello_func = parser_hello;
    faker->parser->interface_info_func = parser_interface_info;
    faker->parser->ep_info_func = parser_ep_info;
    faker->parser->device_connect_func = parser_device_connect;
    faker->parser->control_packet_func = parser_control_transfer;
    faker->parser->bulk_packet_func = parser_bulk_transfer;
    faker->parser->configuration_status_func = parser_configuration_status;
    faker->parser->alt_setting_status_func = parser_alt_setting_status;
}

static void *fake_usbredir_guest_server_thread(void *data)
{
    FakeUsbredirGuest *faker = data;
    while (true) {
        if (usbredirparser_do_read(faker->parser)) {
            return NULL;
        }
    }
    return NULL;
}

void fake_usbredir_guest_start(FakeUsbredirGuest *faker)
{
    uint32_t caps[USB_REDIR_CAPS_SIZE] = {
        0,
    };
    usbredirparser_caps_set_cap(caps, usb_redir_cap_connect_device_version);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_ep_info_max_packet_size);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_64bits_ids);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_32bits_bulk_length);
    usbredirparser_init(faker->parser, VERSION, caps, USB_REDIR_CAPS_SIZE, 0);
    g_assert(usbredirparser_do_write(faker->parser) == 0);
    faker->parser_thread =
        g_thread_new("faker", fake_usbredir_guest_server_thread, faker);
}

void fake_usbredir_guest_stop(FakeUsbredirGuest *faker)
{
    g_thread_join(faker->parser_thread);
    g_mutex_clear(&faker->flag_mu);
    g_free(faker->control_data_packet);
    sem_destroy(&faker->helloed_sem);
    sem_destroy(&faker->if_info_sem);
    sem_destroy(&faker->ep_info_sem);
    sem_destroy(&faker->device_info_sem);
    sem_destroy(&faker->control_transfer_sem);
    sem_destroy(&faker->bulk_transfer_sem);

    if (faker->parser) {
        usbredirparser_destroy(faker->parser);
    }
}

bool fake_usbredir_guest_helloed(FakeUsbredirGuest *faker)
{
    struct timespec ts;
    int result;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1;
    result = sem_timedwait(&faker->helloed_sem, &ts);
    return result == 0;
}

void fake_usbredir_guest_assert_num_interfaces(FakeUsbredirGuest *faker,
                                               int expected_num_interfaces)
{
    wait_for_flag(faker, &faker->if_info_sem, &faker->received_if_info);
    g_assert_cmphex(faker->if_info.interface_count, ==,
                    expected_num_interfaces);
}

void fake_usbredir_guest_assert_contains_interface(
    FakeUsbredirGuest *faker, const struct libusb_interface_descriptor *if_desc)
{
    uint8_t interface_num;

    wait_for_flag(faker, &faker->if_info_sem, &faker->received_if_info);
    interface_num = if_desc->bInterfaceNumber;

    g_assert_cmphex(faker->if_info.interface[interface_num], ==, interface_num);
    g_assert_cmphex(faker->if_info.interface_class[interface_num], ==,
                    if_desc->bInterfaceClass);
    g_assert_cmphex(faker->if_info.interface_subclass[interface_num], ==,
                    if_desc->bInterfaceSubClass);
    g_assert_cmphex(faker->if_info.interface_protocol[interface_num], ==,
                    if_desc->bInterfaceProtocol);
}

void fake_usbredir_guest_assert_num_endpoints(FakeUsbredirGuest *faker,
                                              int expected_num_endpoints)
{
    int actual_num_endpoints = 0;

    wait_for_flag(faker, &faker->ep_info_sem, &faker->received_ep_info);
    for (int i = 0; i < USB_REDIR_MAX_NUM_ENDPOINTS; i++) {
        if (faker->ep_info.type[i] != usb_redir_type_invalid) {
            ++actual_num_endpoints;
        }
    }

    g_assert_cmphex(actual_num_endpoints, ==, expected_num_endpoints);
}

void fake_usbredir_guest_assert_contains_endpoint(
    FakeUsbredirGuest *faker, const struct libusb_endpoint_descriptor *ep_desc,
    uint8_t interface_num)
{
    int ep_index;

    wait_for_flag(faker, &faker->ep_info_sem, &faker->received_ep_info);
    ep_index = EP_INDEX(ep_desc->bEndpointAddress);

    g_assert_cmphex(faker->ep_info.type[ep_index], ==,
                    ep_desc->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK);
    g_assert_cmphex(faker->ep_info.interface[ep_index], ==, interface_num);
    g_assert_cmphex(faker->ep_info.max_packet_size[ep_index], ==,
                    ep_desc->wMaxPacketSize);
    g_assert_cmphex(faker->ep_info.interval[ep_index], ==, ep_desc->bInterval);
}

void fake_usbredir_guest_assert_device(
    FakeUsbredirGuest *faker,
    const struct libusb_device_descriptor *device_desc)
{
    sem_wait(&faker->device_info_sem);
    g_assert_cmphex(faker->device_info.speed, ==, usb_redir_speed_high);
    g_assert_cmphex(faker->device_info.device_class, ==,
                    device_desc->bDeviceClass);
    g_assert_cmphex(faker->device_info.device_subclass, ==,
                    device_desc->bDeviceSubClass);
    g_assert_cmphex(faker->device_info.device_protocol, ==,
                    device_desc->bDeviceProtocol);
    g_assert_cmphex(faker->device_info.device_version_bcd, ==,
                    device_desc->bcdUSB);
    g_assert_cmphex(faker->device_info.vendor_id, ==, device_desc->idVendor);
    g_assert_cmphex(faker->device_info.product_id, ==, device_desc->idProduct);
}

void fake_usbredir_guest_control_transfer(FakeUsbredirGuest *faker,
                                          uint8_t request_type, uint8_t request,
                                          uint16_t value, uint16_t index,
                                          unsigned char *data, uint16_t length)
{
    struct usb_redir_control_packet_header *control_header;
    int write_length = 0;

    g_mutex_lock(&faker->flag_mu);
    g_assert(faker->device_connected);
    g_mutex_unlock(&faker->flag_mu);

    /* Assert that control data packet should be uninitialized. */
    g_assert(faker->control_data_packet == NULL);

    faker->control_data_packet = g_new0(ControlDataPacket, 1);

    faker->control_data_packet->packet.id = faker->packet_id++;

    if (!(request_type & LIBUSB_ENDPOINT_IN)) {
        write_length = length;
    }

    control_header = &faker->control_data_packet->header;
    control_header->endpoint = request_type & LIBUSB_ENDPOINT_IN;
    control_header->requesttype = request_type;
    control_header->request = request;
    control_header->value = value;
    control_header->index = index;
    control_header->length = length;

    usbredirparser_send_control_packet(faker->parser,
                                       faker->control_data_packet->packet.id,
                                       control_header, data, write_length);
    g_assert(usbredirparser_do_write(faker->parser) == 0);
}

void fake_usbredir_guest_bulk_transfer(FakeUsbredirGuest *faker,
                                       uint8_t endpoint, uint8_t *data,
                                       uint32_t length)
{
    BulkDataPacket *bulk_data_packet = g_new0(BulkDataPacket, 1);
    int write_length = 0;

    wait_for_flag(faker, &faker->device_info_sem, &faker->device_connected);

    if (!(endpoint & LIBUSB_ENDPOINT_IN)) {
        write_length = length;
    }

    bulk_data_packet->packet.id = faker->packet_id++;
    bulk_data_packet->header.endpoint = endpoint;
    bulk_data_packet->header.length = length & 0xFFFF;
    bulk_data_packet->header.length_high = length >> 16;
    bulk_data_packet->header.stream_id = 1;

    QTAILQ_INSERT_TAIL(&faker->bulk_data_packet_queue, bulk_data_packet, next);

    usbredirparser_send_bulk_packet(faker->parser, bulk_data_packet->packet.id,
                                    &bulk_data_packet->header, data,
                                    write_length);
    g_assert(usbredirparser_do_write(faker->parser) == 0);
}

void fake_usbredir_guest_assert_control_transfer_received(
    FakeUsbredirGuest *faker, uint8_t *data, uint16_t length)
{
    sem_wait(&faker->control_transfer_sem);
    g_assert(faker->control_data_packet != NULL);
    g_assert(faker->control_data_packet->packet.done);

    if (data != NULL) {
        g_assert_cmpmem(data, length, faker->control_data_packet->packet.data,
                        faker->control_data_packet->packet.data_length);
        g_free(faker->control_data_packet->packet.data);
    }

    g_free(faker->control_data_packet);
    faker->control_data_packet = NULL;
}

void fake_usbredir_guest_assert_bulk_transfer(FakeUsbredirGuest *faker,
                                              uint8_t *data, uint32_t length)
{
    BulkDataPacket *bulk_data_packet;

    sem_wait(&faker->bulk_transfer_sem);
    g_assert(!QTAILQ_EMPTY(&faker->bulk_data_packet_queue));

    bulk_data_packet = QTAILQ_FIRST(&faker->bulk_data_packet_queue);
    g_assert(bulk_data_packet->packet.done);

    if (!bulk_data_packet->packet.canceled &&
        (bulk_data_packet->header.endpoint & LIBUSB_ENDPOINT_IN)) {
        g_assert(bulk_data_packet->packet.data != NULL);
        g_assert_cmpmem(data, length, bulk_data_packet->packet.data,
                        bulk_data_packet->packet.data_length);
        g_free(bulk_data_packet->packet.data);
    } else {
        g_assert(data == NULL && length == 0);
    }

    QTAILQ_REMOVE(&faker->bulk_data_packet_queue, bulk_data_packet, next);
    g_free(bulk_data_packet);
}

void fake_usbredir_guest_set_configuration(FakeUsbredirGuest *faker,
                                           uint8_t configuration_value)
{
    struct usb_redir_set_configuration_header set_config;

    g_mutex_lock(&faker->flag_mu);
    g_assert(&faker->device_connected);
    g_mutex_unlock(&faker->flag_mu);

    set_config.configuration = configuration_value;
    usbredirparser_send_set_configuration(faker->parser, 1, &set_config);
    g_assert(usbredirparser_do_write(faker->parser) == 0);
    faker->configuration_value = configuration_value;
}

void fake_usbredir_guest_set_alt_interface(FakeUsbredirGuest *faker,
                                           uint8_t interface_num,
                                           uint8_t alt_setting)
{
    struct usb_redir_set_alt_setting_header set_alt;

    g_mutex_lock(&faker->flag_mu);
    g_assert(&faker->device_connected);
    g_mutex_unlock(&faker->flag_mu);

    set_alt.alt = alt_setting;
    set_alt.interface = interface_num;
    usbredirparser_send_set_alt_setting(faker->parser, 1, &set_alt);
    g_assert(usbredirparser_do_write(faker->parser) == 0);
    faker->interface_num = interface_num;
    faker->alt_setting = alt_setting;
}

void fake_usbredir_guest_cancel_transfer(FakeUsbredirGuest *faker)
{
    BulkDataPacket *bulk_data_packet;

    g_mutex_lock(&faker->flag_mu);
    g_assert(&faker->device_connected);
    g_mutex_unlock(&faker->flag_mu);

    if (faker->control_data_packet &&
        !faker->control_data_packet->packet.canceled) {
        faker->control_data_packet->packet.canceled = true;
        usbredirparser_send_cancel_data_packet(
            faker->parser, faker->control_data_packet->packet.id);
    }

    QTAILQ_FOREACH(bulk_data_packet, &faker->bulk_data_packet_queue, next)
    {
        if (!bulk_data_packet->packet.canceled) {
            bulk_data_packet->packet.canceled = true;
            usbredirparser_send_cancel_data_packet(faker->parser,
                                                   bulk_data_packet->packet.id);
        }
    }

    g_assert(usbredirparser_do_write(faker->parser) == 0);
}
