/*
 *  ASPEED LPC Controller
 *
 *  Copyright (C) 2017-2018 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/misc/aspeed_lpc.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "trace.h"

#define TO_REG(offset) ((offset) >> 2)

#define HICR0                TO_REG(0x00)
#define   HICR0_LPC3E        BIT(7)
#define   HICR0_LPC2E        BIT(6)
#define   HICR0_LPC1E        BIT(5)
#define HICR1                TO_REG(0x04)
#define HICR2                TO_REG(0x08)
#define   HICR2_IBFIE3       BIT(3)
#define   HICR2_IBFIE2       BIT(2)
#define   HICR2_IBFIE1       BIT(1)
#define HICR3                TO_REG(0x0C)
#define HICR4                TO_REG(0x10)
#define   HICR4_KCSENBL      BIT(2)
#define IDR1                 TO_REG(0x24)
#define IDR2                 TO_REG(0x28)
#define IDR3                 TO_REG(0x2C)
#define ODR1                 TO_REG(0x30)
#define ODR2                 TO_REG(0x34)
#define ODR3                 TO_REG(0x38)
#define STR1                 TO_REG(0x3C)
#define   STR_OBF            BIT(0)
#define   STR_IBF            BIT(1)
#define   STR_CMD_DATA       BIT(3)
#define STR2                 TO_REG(0x40)
#define STR3                 TO_REG(0x44)
#define HICR5                TO_REG(0x80)
#define HICR6                TO_REG(0x84)
#define HICR7                TO_REG(0x88)
#define HICR8                TO_REG(0x8C)
#define HICRB                TO_REG(0x100)
#define   HICRB_IBFIE4       BIT(1)
#define   HICRB_LPC4E        BIT(0)
#define IDR4                 TO_REG(0x114)
#define ODR4                 TO_REG(0x118)
#define STR4                 TO_REG(0x11C)

enum aspeed_kcs_channel_id {
    kcs_channel_1 = 0,
    kcs_channel_2,
    kcs_channel_3,
    kcs_channel_4,
};

static const enum aspeed_lpc_subdevice aspeed_kcs_subdevice_map[] = {
    [kcs_channel_1] = aspeed_lpc_kcs_1,
    [kcs_channel_2] = aspeed_lpc_kcs_2,
    [kcs_channel_3] = aspeed_lpc_kcs_3,
    [kcs_channel_4] = aspeed_lpc_kcs_4,
};

struct aspeed_kcs_channel {
    enum aspeed_kcs_channel_id id;

    int idr;
    int odr;
    int str;
};

static const struct aspeed_kcs_channel aspeed_kcs_channel_map[] = {
    [kcs_channel_1] = {
        .id = kcs_channel_1,
        .idr = IDR1,
        .odr = ODR1,
        .str = STR1
    },

    [kcs_channel_2] = {
        .id = kcs_channel_2,
        .idr = IDR2,
        .odr = ODR2,
        .str = STR2
    },

    [kcs_channel_3] = {
        .id = kcs_channel_3,
        .idr = IDR3,
        .odr = ODR3,
        .str = STR3
    },

    [kcs_channel_4] = {
        .id = kcs_channel_4,
        .idr = IDR4,
        .odr = ODR4,
        .str = STR4
    },
};

struct aspeed_kcs_register_data {
    const char *name;
    int reg;
    const struct aspeed_kcs_channel *chan;
};

static const struct aspeed_kcs_register_data aspeed_kcs_registers[] = {
    {
        .name = "idr1",
        .reg = IDR1,
        .chan = &aspeed_kcs_channel_map[kcs_channel_1],
    },
    {
        .name = "odr1",
        .reg = ODR1,
        .chan = &aspeed_kcs_channel_map[kcs_channel_1],
    },
    {
        .name = "str1",
        .reg = STR1,
        .chan = &aspeed_kcs_channel_map[kcs_channel_1],
    },
    {
        .name = "idr2",
        .reg = IDR2,
        .chan = &aspeed_kcs_channel_map[kcs_channel_2],
    },
    {
        .name = "odr2",
        .reg = ODR2,
        .chan = &aspeed_kcs_channel_map[kcs_channel_2],
    },
    {
        .name = "str2",
        .reg = STR2,
        .chan = &aspeed_kcs_channel_map[kcs_channel_2],
    },
    {
        .name = "idr3",
        .reg = IDR3,
        .chan = &aspeed_kcs_channel_map[kcs_channel_3],
    },
    {
        .name = "odr3",
        .reg = ODR3,
        .chan = &aspeed_kcs_channel_map[kcs_channel_3],
    },
    {
        .name = "str3",
        .reg = STR3,
        .chan = &aspeed_kcs_channel_map[kcs_channel_3],
    },
    {
        .name = "idr4",
        .reg = IDR4,
        .chan = &aspeed_kcs_channel_map[kcs_channel_4],
    },
    {
        .name = "odr4",
        .reg = ODR4,
        .chan = &aspeed_kcs_channel_map[kcs_channel_4],
    },
    {
        .name = "str4",
        .reg = STR4,
        .chan = &aspeed_kcs_channel_map[kcs_channel_4],
    },
    { },
};

/* LPC KCS host side */
/* IPMI 2.0 Table 9.1 status register bits */
#define KCS_ST_STATE(rv)    extract8(rv, 6, 2)
#define KCS_ST_CMD          BIT(3)
#define KCS_ST_SMS_ATN      BIT(2)
#define KCS_ST_IBF          BIT(1)
#define KCS_ST_OBF          BIT(0)

/* IPMI 2.0 Table 9.2 state bits */
enum KCSState {
    IDLE_STATE,
    READ_STATE,
    WRITE_STATE,
    ERROR_STATE,
};

/* IPMI 2.0 Table 9.3 control codes */
#define KCS_CMD_GET_STATUS_ABORT    0x60
#define KCS_CMD_WRITE_START         0x61
#define KCS_CMD_WRITE_END           0x62
#define KCS_CMD_READ                0x68

static bool aspeed_kcs_channel_enabled(AspeedLPCState *s,
                                       const struct aspeed_kcs_channel *channel)
{
    switch (channel->id) {
    case kcs_channel_1: return s->regs[HICR0] & HICR0_LPC1E;
    case kcs_channel_2: return s->regs[HICR0] & HICR0_LPC2E;
    case kcs_channel_3:
        return (s->regs[HICR0] & HICR0_LPC3E) &&
               (s->regs[HICR4] & HICR4_KCSENBL);
    case kcs_channel_4: return s->regs[HICRB] & HICRB_LPC4E;
    default: return false;
    }
}

static bool
aspeed_kcs_channel_ibf_irq_enabled(AspeedLPCState *s,
                                   const struct aspeed_kcs_channel *channel)
{
    if (!aspeed_kcs_channel_enabled(s, channel)) {
            return false;
    }

    switch (channel->id) {
    case kcs_channel_1: return s->regs[HICR2] & HICR2_IBFIE1;
    case kcs_channel_2: return s->regs[HICR2] & HICR2_IBFIE2;
    case kcs_channel_3: return s->regs[HICR2] & HICR2_IBFIE3;
    case kcs_channel_4: return s->regs[HICRB] & HICRB_IBFIE4;
    default: return false;
    }
}

/* Host Side Operations. */
static uint8_t aspeed_kcs_host_read_byte(AspeedKCSChannel *c)
{
    uint8_t v;
    const struct aspeed_kcs_channel *this_channel = \
                                    &aspeed_kcs_channel_map[c->channel_id];

    v = c->owner->regs[this_channel->odr];
    c->owner->regs[this_channel->str] &= ~KCS_ST_OBF;

    /*
     * ASPEED doesn't have OBE IRQ, as the driver is actually
     * emulating one. So don't raise IRQ.
     */
    trace_aspeed_kcs_host_read_byte(DEVICE(c)->canonical_path, v);
    return v;
}

static void aspeed_kcs_host_write_byte(AspeedKCSChannel *c, uint8_t value,
        bool is_cmd)
{
    const struct aspeed_kcs_channel *this_channel = \
                                    &aspeed_kcs_channel_map[c->channel_id];
    bool irq_enabled = aspeed_kcs_channel_ibf_irq_enabled(c->owner,
                                                  this_channel);

    c->owner->regs[this_channel->idr] = value;
    c->owner->regs[this_channel->str] |= KCS_ST_IBF;

    if (is_cmd) {
        c->owner->regs[this_channel->str] |= KCS_ST_CMD;
    } else {
        c->owner->regs[this_channel->str] &= ~KCS_ST_CMD;
    }

    trace_aspeed_kcs_host_write_byte(DEVICE(c)->canonical_path,
                                     value,
                                     irq_enabled,
                                     is_cmd,
                                     c->owner->regs[this_channel->str]);

    if (irq_enabled) {
        qemu_irq_raise(c->owner->subdevice_irqs[c->channel_id]);
    }
}

static void aspeed_kcs_handle_event(AspeedKCSChannel *c)
{
    uint8_t v;
    IPMICoreClass *hk;
    int str_index = aspeed_kcs_channel_map[c->channel_id].str;
    trace_aspeed_kcs_handle_event(DEVICE(c)->canonical_path,
                                  KCS_ST_STATE(c->owner->regs[str_index]));

    /* IPMI spec Figure 9.6 and 9.7 */
    switch (KCS_ST_STATE(c->owner->regs[str_index])) {
    case IDLE_STATE:
        if (c->owner->regs[str_index] & KCS_ST_OBF) {
            /* Read a dummy byte. */
            aspeed_kcs_host_read_byte(c);
            if (c->outlen > 0) {
                /* Send to ipmi host when msg ends. */
                if (c->host) {
                    hk = IPMI_CORE_GET_CLASS(c->host);
                    hk->handle_command(c->host, c->outmsg, c->outlen,
                            MAX_IPMI_MSG_SIZE, c->last_msg_id);
                }
                /* The last byte has been read. return to empty state. */
                c->outlen = 0;
            }
        }
        if (c->inlen > 0) {
            /* Send to bmc the next request */
            aspeed_kcs_host_write_byte(c, KCS_CMD_WRITE_START, true);
            c->last_byte_not_ready = true;
        }
        break;

    case READ_STATE:
        if (c->owner->regs[str_index] & KCS_ST_OBF) {
            /* Read in a byte from BMC */
            v = aspeed_kcs_host_read_byte(c);
            if (c->outlen < MAX_IPMI_MSG_SIZE) {
                c->outmsg[c->outlen++] = v;
            }
            aspeed_kcs_host_write_byte(c, KCS_CMD_READ, false);
        }
        break;

    case WRITE_STATE:
        if (c->owner->regs[str_index] & KCS_ST_IBF) {
            /*
             * The guest hasn't read the byte yet. We'll wait.
             * This step will handle the case that during a write, ASPEED
             * driver writes 0 to ODR and calls this handler, just skip it.
             */
            break;
        }
        /* Clear OBF. */
        c->owner->regs[str_index] &= ~KCS_ST_OBF;
        /* If it's the last byte written, we need to first send a command. */
        if (c->last_byte_not_ready && c->inpos == c->inlen - 1) {
            aspeed_kcs_host_write_byte(c, KCS_CMD_WRITE_END, true);
            c->last_byte_not_ready = false;
        } else {
            aspeed_kcs_host_write_byte(c, c->inmsg[c->inpos++], false);
            if (!c->last_byte_not_ready) {
                /* The last byte has been sent. */
                c->inlen = 0;
                c->inpos = 0;
            }
        }
        break;

    case ERROR_STATE:
        if (c->owner->regs[str_index] & KCS_ST_OBF) {
            /* Read in error byte from BMC */
            aspeed_kcs_host_read_byte(c);
        }
        /* Force abort */
        c->outlen = 0;
        c->inlen = 0;
        c->inpos = 0;
        c->owner->regs[str_index] = 0;
        break;

    default:
        g_assert_not_reached();
    }
}

/* Received a request from the host and send it to BMC core. */
static void aspeed_kcs_handle_req(IPMIInterface *ii, uint8_t msg_id,
                                unsigned char *req, unsigned req_len)
{
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    AspeedKCSChannel *c = iic->get_backend_data(ii);

    /* Drop the request if the last request is still not handled. */
    if (c->inlen > 0) {
        return;
    }
    if (req_len == 0) {
        return;
    }
    if (req_len > MAX_IPMI_MSG_SIZE) {
        /* Truncate the extra bytes that don't fit. */
        req_len = MAX_IPMI_MSG_SIZE;
    }
    memcpy(c->inmsg, req, req_len);
    c->inpos = 0;
    c->inlen = req_len;

    c->last_msg_id = msg_id;
    trace_aspeed_kcs_handle_req(msg_id, req_len);
    aspeed_kcs_handle_event(c);
}

static const struct aspeed_kcs_register_data *
aspeed_kcs_get_register_data_by_name(const char *name)
{
    const struct aspeed_kcs_register_data *pos = aspeed_kcs_registers;

    while (pos->name) {
        if (!strcmp(pos->name, name)) {
            return pos;
        }
        pos++;
    }

    return NULL;
}

static const struct aspeed_kcs_channel *
aspeed_kcs_get_channel_by_register(int reg)
{
    const struct aspeed_kcs_register_data *pos = aspeed_kcs_registers;

    while (pos->name) {
        if (pos->reg == reg) {
            return pos->chan;
        }
        pos++;
    }

    return NULL;
}

static void aspeed_kcs_get_register_property(Object *obj,
                                             Visitor *v,
                                             const char *name,
                                             void *opaque,
                                             Error **errp)
{
    const struct aspeed_kcs_register_data *data;
    AspeedLPCState *s = ASPEED_LPC(obj);
    uint32_t val;

    data = aspeed_kcs_get_register_data_by_name(name);
    if (!data) {
        return;
    }

    if (!strncmp("odr", name, 3)) {
        s->regs[data->chan->str] &= ~STR_OBF;
    }

    val = s->regs[data->reg];

    visit_type_uint32(v, name, &val, errp);
}

static void aspeed_kcs_set_register_property(Object *obj,
                                             Visitor *v,
                                             const char *name,
                                             void *opaque,
                                             Error **errp)
{
    const struct aspeed_kcs_register_data *data;
    AspeedLPCState *s = ASPEED_LPC(obj);
    uint32_t val;

    data = aspeed_kcs_get_register_data_by_name(name);
    if (!data) {
        return;
    }

    if (!visit_type_uint32(v, name, &val, errp)) {
        return;
    }

    if (strncmp("str", name, 3)) {
        s->regs[data->reg] = val;
    }

    if (!strncmp("idr", name, 3)) {
        s->regs[data->chan->str] |= STR_IBF;
        if (aspeed_kcs_channel_ibf_irq_enabled(s, data->chan)) {
            enum aspeed_lpc_subdevice subdev;

            subdev = aspeed_kcs_subdevice_map[data->chan->id];
            qemu_irq_raise(s->subdevice_irqs[subdev]);
        }
    }
}

static void aspeed_lpc_set_irq(void *opaque, int irq, int level)
{
    AspeedLPCState *s = (AspeedLPCState *)opaque;

    if (level) {
        s->subdevice_irqs_pending |= BIT(irq);
    } else {
        s->subdevice_irqs_pending &= ~BIT(irq);
    }
    trace_aspeed_kcs_lpc_set_irq(irq, level);
    qemu_set_irq(s->irq, !!s->subdevice_irqs_pending);
}

/* LPC KCS core side, share the APIs with LPC */

static uint64_t aspeed_lpc_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedLPCState *s = ASPEED_LPC(opaque);
    int reg = TO_REG(offset);
    uint32_t res = s->regs[reg];
    trace_aspeed_kcs_read(offset, s->regs[reg]);

    if (reg >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    switch (reg) {
    case IDR1:
    case IDR2:
    case IDR3:
    case IDR4:
    {
        const struct aspeed_kcs_channel *channel;

        channel = aspeed_kcs_get_channel_by_register(reg);

        if (s->regs[channel->str] & STR_IBF) {
            enum aspeed_lpc_subdevice subdev;

            subdev = aspeed_kcs_subdevice_map[channel->id];
            qemu_irq_lower(s->subdevice_irqs[subdev]);
        }

        s->regs[channel->str] &= ~STR_IBF;
        aspeed_kcs_handle_event(&s->channels[channel->id]);
        break;
    }
    default:
        break;
    }

    return res;
}

static void aspeed_lpc_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned int size)
{
    AspeedLPCState *s = ASPEED_LPC(opaque);
    int reg = TO_REG(offset);

    trace_aspeed_kcs_write(offset, data);

    if (reg >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    /*
     * To avoid data corruption, first write the value to addr
     * and later adjust it if necessary.
     */
    s->regs[reg] = data;

    switch (reg) {
    case ODR1:
    case ODR2:
    case ODR3:
    case ODR4:
    {
        const struct aspeed_kcs_channel *channel;

        channel = aspeed_kcs_get_channel_by_register(reg);
        s->regs[channel->str] |= STR_OBF;
        aspeed_kcs_handle_event(&s->channels[channel->id]);
        break;
    }
    case STR1:
    case STR2:
    case STR3:
    case STR4:
    {
        static const uint8_t mask = KCS_ST_CMD | KCS_ST_IBF | KCS_ST_OBF;
        const struct aspeed_kcs_channel *channel;

        channel = aspeed_kcs_get_channel_by_register(reg);

        if (data & mask) {
            qemu_log_mask(LOG_GUEST_ERROR,
                 "%s: read-only bits in 0x%02lx ignored\n",
                      __func__, data);
            data &= ~mask;
        }

        s->regs[reg] = (s->regs[channel->str] & mask) | data;
        trace_aspeed_kcs_write_str(offset, data);
        break;
    }
    default:
        break;
    }
}

static const MemoryRegionOps aspeed_lpc_ops = {
    .read = aspeed_lpc_read,
    .write = aspeed_lpc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_lpc_reset(DeviceState *dev)
{
    struct AspeedLPCState *s = ASPEED_LPC(dev);

    s->subdevice_irqs_pending = 0;

    memset(s->regs, 0, sizeof(s->regs));

    s->regs[HICR7] = s->hicr7;
}

static void aspeed_lpc_realize(DeviceState *dev, Error **errp)
{
    AspeedLPCState *s = ASPEED_LPC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->subdevice_irqs[aspeed_lpc_kcs_1]);
    sysbus_init_irq(sbd, &s->subdevice_irqs[aspeed_lpc_kcs_2]);
    sysbus_init_irq(sbd, &s->subdevice_irqs[aspeed_lpc_kcs_3]);
    sysbus_init_irq(sbd, &s->subdevice_irqs[aspeed_lpc_kcs_4]);
    sysbus_init_irq(sbd, &s->subdevice_irqs[aspeed_lpc_ibt]);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_lpc_ops, s,
            TYPE_ASPEED_LPC, 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);

    /* Only connect KCS channel onwer for KCS */
    for (i = 0; i < APSEED_KCS_NR_CHANNELS; ++i) {
        s->channels[i].owner = s;
        if (!qdev_realize(DEVICE(&s->channels[i]), NULL, errp)) {
            return;
        }
    }
    qdev_init_gpio_in(dev, aspeed_lpc_set_irq, ASPEED_LPC_NR_SUBDEVS);
}

static void aspeed_lpc_init(Object *obj)
{
    object_property_add(obj, "idr1", "uint32", aspeed_kcs_get_register_property,
                        aspeed_kcs_set_register_property, NULL, NULL);
    object_property_add(obj, "odr1", "uint32", aspeed_kcs_get_register_property,
                        aspeed_kcs_set_register_property, NULL, NULL);
    object_property_add(obj, "str1", "uint32", aspeed_kcs_get_register_property,
                        aspeed_kcs_set_register_property, NULL, NULL);
    object_property_add(obj, "idr2", "uint32", aspeed_kcs_get_register_property,
                        aspeed_kcs_set_register_property, NULL, NULL);
    object_property_add(obj, "odr2", "uint32", aspeed_kcs_get_register_property,
                        aspeed_kcs_set_register_property, NULL, NULL);
    object_property_add(obj, "str2", "uint32", aspeed_kcs_get_register_property,
                        aspeed_kcs_set_register_property, NULL, NULL);
    object_property_add(obj, "idr3", "uint32", aspeed_kcs_get_register_property,
                        aspeed_kcs_set_register_property, NULL, NULL);
    object_property_add(obj, "odr3", "uint32", aspeed_kcs_get_register_property,
                        aspeed_kcs_set_register_property, NULL, NULL);
    object_property_add(obj, "str3", "uint32", aspeed_kcs_get_register_property,
                        aspeed_kcs_set_register_property, NULL, NULL);
    object_property_add(obj, "idr4", "uint32", aspeed_kcs_get_register_property,
                        aspeed_kcs_set_register_property, NULL, NULL);
    object_property_add(obj, "odr4", "uint32", aspeed_kcs_get_register_property,
                        aspeed_kcs_set_register_property, NULL, NULL);
    object_property_add(obj, "str4", "uint32", aspeed_kcs_get_register_property,
                        aspeed_kcs_set_register_property, NULL, NULL);

    /* Init the KCS subdevices */
    AspeedLPCState *s = ASPEED_LPC(obj);
    int i;
    for (i = 0; i < APSEED_KCS_NR_CHANNELS; ++i) {
        object_initialize_child(obj, g_strdup_printf("channels[%d]", i),
            &s->channels[i], TYPE_ASPEED_KCS_CHANNEL);
        s->channels[i].channel_id = i;
    }
}

static const VMStateDescription vmstate_aspeed_lpc = {
    .name = TYPE_ASPEED_LPC,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedLPCState, ASPEED_LPC_NR_REGS),
        VMSTATE_UINT32(subdevice_irqs_pending, AspeedLPCState),
        VMSTATE_END_OF_LIST(),
    }
};

static const VMStateDescription vmstate_aspeed_kcs_channel = {
    .name = TYPE_ASPEED_KCS_CHANNEL,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        /* TODO: add all host side buffer into VM state */
        VMSTATE_END_OF_LIST(),
    },
};

static Property aspeed_lpc_properties[] = {
    DEFINE_PROP_UINT32("hicr7", AspeedLPCState, hicr7, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_lpc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_lpc_realize;
    dc->reset = aspeed_lpc_reset;
    dc->desc = "Aspeed LPC Controller",
    dc->vmsd = &vmstate_aspeed_lpc;
    device_class_set_props(dc, aspeed_lpc_properties);
}

static void aspeed_kcs_channel_realize(DeviceState *dev, Error **errp)
{
    IPMIInterface *ii = IPMI_INTERFACE(dev);
    AspeedKCSChannel *c = ASPEED_KCS_CHANNEL(ii);

    if (c->host) {
        c->host->intf = ii;
    }
}

static void aspeed_kcs_set_ipmi_handler(IPMIInterface *ii, IPMICore *h)
{
    AspeedKCSChannel *c = ASPEED_KCS_CHANNEL(ii);

    c->host = h;
}

static void *aspeed_kcs_get_backend_data(IPMIInterface *ii)
{
    AspeedKCSChannel *c = ASPEED_KCS_CHANNEL(ii);

    return c;
}

static void aspeed_kcs_set_atn(struct IPMIInterface *s, int val, int irq) {
    /* TODO: implement this if needed */
    /* Set attention bit is not support for now */
}

static void aspeed_kcs_channel_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_CLASS(klass);

    dc->desc = "Aspeed KCS Channel";
    dc->vmsd = &vmstate_aspeed_kcs_channel;
    dc->realize = aspeed_kcs_channel_realize;

    iic->get_backend_data = aspeed_kcs_get_backend_data;
    iic->handle_msg = aspeed_kcs_handle_req;
    iic->set_ipmi_handler = aspeed_kcs_set_ipmi_handler;
    iic->set_atn = aspeed_kcs_set_atn;
}

static const TypeInfo aspeed_lpc_info[] = {
    {
      .name = TYPE_ASPEED_LPC,
      .parent = TYPE_SYS_BUS_DEVICE,
      .instance_size = sizeof(AspeedLPCState),
      .class_init = aspeed_lpc_class_init,
      .instance_init = aspeed_lpc_init,
    },
    {
      .name = TYPE_ASPEED_KCS_CHANNEL,
      .parent = TYPE_DEVICE,
      .instance_size = sizeof(AspeedKCSChannel),
      .class_init = aspeed_kcs_channel_class_init,
      .interfaces         = (InterfaceInfo[]) {
          { TYPE_IPMI_INTERFACE },
          { },
      },
    }
};

DEFINE_TYPES(aspeed_lpc_info);
