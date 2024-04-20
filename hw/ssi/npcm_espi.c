/*
 * eSPI Slave Interface Module
 *
 * Targeting the NPCM840G which supports the Intel Enhanced Serial Peripheral
 * Interface (eSPI) Revision 1.0, January 2016
 *
 * Copyright 2024 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/irq.h"
#include "hw/registerfields.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "hw/ssi/npcm_espi.h"
#include "trace.h"

REG32(NPCM_ESPIID, 0x0)                     /* eSPI ID */
REG32(NPCM_ESPICFG, 0x4)                    /* eSPI Configuration */
REG32(NPCM_ESPISTS, 0x8)                    /* eSPI Status */
REG32(NPCM_ESPIIE, 0xC)                     /* eSPI Interrupt Enable */
REG32(NPCM_ESPIERR, 0x3C)                   /* eSPI Error */

#define NPCM_ESPIID_DEFAULT         0x801
#define NPCM_ESPICFG_DEFAULT        0x3000010


static uint64_t npcm_espi_core_read(void *opaque, hwaddr offset, uint32_t size)
{
    NPCMESPIState *es = NPCM_ESPI(opaque);
    uint64_t ret;

    ret = es->regs[offset >> 2];
    trace_npcm_espi_read(offset, ret);
    return ret;
}

static void npcm_espi_core_write(void *opaque, hwaddr offset, uint64_t input,
    unsigned size)
{
    NPCMESPIState *es = NPCM_ESPI(opaque);

    trace_npcm_espi_write(offset, input);

    switch (offset) {
    case A_NPCM_ESPISTS:                /* eSPI Status is Write 1 to clear */
        es->regs[R_NPCM_ESPIERR] &= ~input;
        break;

    case A_NPCM_ESPIIE:
        es->regs[R_NPCM_ESPIIE] = input;
        break;

    case A_NPCM_ESPIERR:                /* eSPI Error is Write 1 to clear */
        es->regs[R_NPCM_ESPIERR] &= ~input;
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: write: data=0x$x offset=0x%lx\n",
                      __func__, offset);
        es->regs[offset >> 2] = input;
        break;
    }
}

static void npcm_espi_enter_reset(Object *dev, ResetType type)
{
    NPCMESPIState *es = NPCM_ESPI(dev);

    es->regs[R_NPCM_ESPIID] = NPCM_ESPIID_DEFAULT;
    es->regs[R_NPCM_ESPICFG] = NPCM_ESPICFG_DEFAULT;
}

static const MemoryRegionOps npcm_espi_memops = {
    .read = npcm_espi_core_read,
    .write = npcm_espi_core_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    }
};


static void npcm_espi_realize(DeviceState *dev, Error **errp)
{
    NPCMESPIState *es = NPCM_ESPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&es->mmio, OBJECT(es), &npcm_espi_memops, es,
                          TYPE_NPCM_ESPI, 4 * KiB);

    sysbus_init_mmio(sbd, &es->mmio);
    sysbus_init_irq(sbd, &es->irq);
}

static void npcm_espi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "NPCM eSPI";
    dc->realize = npcm_espi_realize;
    rc->phases.enter = npcm_espi_enter_reset;
}

static const TypeInfo npcm_espi_types[] = {
    {
        .name = TYPE_NPCM_ESPI,
        .parent = TYPE_SYS_BUS_DEVICE,
        .class_init = npcm_espi_class_init,
        .instance_size = sizeof(NPCMESPIState),
    },
};

DEFINE_TYPES(npcm_espi_types)
