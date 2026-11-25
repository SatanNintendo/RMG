/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - rdram.c                                                 *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2014 Bobby Smiles                                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "rdram.h"

#include "api/m64p_types.h"
#include "api/callbacks.h"
#include "device/device.h"
#include "device/memory/memory.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/ri/ri_controller.h"

#include <string.h>
#include <stdio.h>

#define RDRAM_BCAST_ADDRESS_MASK UINT32_C(0x00080000)
#define RDRAM_MODE_CE_MASK UINT32_C(0x80000000)

/* XXX: deduce # of RDRAM modules from it's total size
 * Assume only 2Mo RDRAM modules.
 * Proper way of doing it would be to declare in init_rdram
 * what kind of modules we insert and deduce dram_size from
 * that configuration.
 */
static size_t get_modules_count(const struct rdram* rdram)
{
    return (rdram->dram_size) / 0x200000;
}

static uint8_t cc_value(uint32_t mode_reg)
{
    return ((mode_reg & 0x00000040) >>  6)
        |  ((mode_reg & 0x00004000) >> 13)
        |  ((mode_reg & 0x00400000) >> 20)
        |  ((mode_reg & 0x00000080) >>  4)
        |  ((mode_reg & 0x00008000) >> 11)
        |  ((mode_reg & 0x00800000) >> 18);
}


static osal_inline uint16_t idfield_value(uint32_t device_id)
{
    return ((((device_id >> 26) & 0x3f) <<  0)
          | (((device_id >> 23) & 0x01) <<  6)
          | (((device_id >>  8) & 0xff) <<  7)
          | (((device_id >>  7) & 0x01) << 15));
}

static osal_inline uint8_t swapfield_value(uint32_t address_select)
{
    return ((((address_select >> 25) & 0x7f) << 0)
          | (((address_select >> 15) & 0x01) << 7));
}

static size_t get_module(const struct rdram* rdram, uint32_t address)
{
    size_t module;
    size_t modules = get_modules_count(rdram);
    uint16_t id_field;

    for (module = 0; module < modules; ++module) {
        id_field = ri_address_to_id_field(ri_address(address), swapfield_value(rdram->regs[module][RDRAM_ADDR_SELECT_REG]));
        if (id_field == idfield_value(rdram->regs[module][RDRAM_DEVICE_ID_REG])) {
            return module;
        }
    }

    /* can happen during memory detection because
     * it probes potentialy non present RDRAM */
    return RDRAM_MAX_MODULES_COUNT;
}

static void read_rdram_dram_corrupted(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t addr = rdram_dram_address(address);
    size_t module;

    module = get_module(rdram, address);
    if (module == RDRAM_MAX_MODULES_COUNT) {
        *value = 0;
        return;
    }

    /* corrupt read value if CC value is not calibrated */
    uint32_t mode = rdram->regs[module][RDRAM_MODE_REG] ^ UINT32_C(0xc0c0c0c0);
    if ((mode & RDRAM_MODE_CE_MASK) && (cc_value(mode) == 0)) {
        *value = 0;
        return;
    }

    if (address < rdram->dram_size) {
        *value = rdram->dram[addr];
    } else {
        *value = 0;
    }
}

static void map_corrupt_rdram(struct rdram* rdram, int corrupt)
{
    struct mem_mapping mapping;

    mapping.begin = MM_RDRAM_DRAM;
    mapping.end = MM_RDRAM_DRAM + 0x3efffff;
    mapping.type = M64P_MEM_RDRAM;
    mapping.handler.opaque = rdram;
    mapping.handler.read32 = (corrupt)
        ? read_rdram_dram_corrupted
        : read_rdram_dram;
    mapping.handler.write32 = write_rdram_dram;

    apply_mem_mapping(rdram->r4300->mem, &mapping);
#ifndef NEW_DYNAREC
    rdram->r4300->recomp.fast_memory = (corrupt) ? 0 : 1;
    invalidate_r4300_cached_code(rdram->r4300, 0, 0);
#endif
}


void init_rdram(struct rdram* rdram,
                uint32_t* dram,
                size_t dram_size,
                struct r4300_core* r4300)
{
    rdram->dram = dram;
    rdram->dram_size = dram_size;
    rdram->r4300 = r4300;
    rdram->corrupted_handler = 0;
}

void poweron_rdram(struct rdram* rdram)
{
    size_t module;
    size_t modules = get_modules_count(rdram);
    memset(rdram->regs, 0, RDRAM_MAX_MODULES_COUNT*RDRAM_REGS_COUNT*sizeof(uint32_t));
    memset(rdram->dram, 0, rdram->dram_size);

    DebugMessage(M64MSG_INFO, "Initializing %u RDRAM modules for a total of %u MB",
        (uint32_t) modules, (uint32_t) rdram->dram_size / (1024*1024));

    for (module = 0; module < modules; ++module) {
        rdram->regs[module][RDRAM_CONFIG_REG] = UINT32_C(0xb5190010);
        rdram->regs[module][RDRAM_DEVICE_ID_REG] = UINT32_C(0x00000000);
        rdram->regs[module][RDRAM_DELAY_REG] = UINT32_C(0x230b0223);
        rdram->regs[module][RDRAM_MODE_REG] = UINT32_C(0xc4c0c0c0);
        rdram->regs[module][RDRAM_REF_ROW_REG] = UINT32_C(0x00000000);
        rdram->regs[module][RDRAM_MIN_INTERVAL_REG] = UINT32_C(0x0040c0e0);
        rdram->regs[module][RDRAM_ADDR_SELECT_REG] = UINT32_C(0x00000000);
        rdram->regs[module][RDRAM_DEVICE_MANUF_REG] = UINT32_C(0x00000500);
    }
}


void read_rdram_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t reg = rdram_reg(address);
    size_t module;

    if (address & RDRAM_BCAST_ADDRESS_MASK) {
        DebugMessage(M64MSG_WARNING, "Reading from broadcast address is unsupported %08x", address);
        return;
    }

    module = get_module(rdram, address);
    if (module == RDRAM_MAX_MODULES_COUNT) {
        *value = 0;
        return;
    }

    if (reg < RDRAM_REGS_COUNT) {
        *value = rdram->regs[module][reg];
    } else {
        *value = 0;
    }

    /* some bits are inverted when read */
    if (reg == RDRAM_MODE_REG) {
        *value ^= UINT32_C(0xc0c0c0c0);
    }
}

void write_rdram_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t reg = rdram_reg(address);
    uint32_t mode;
    uint8_t corrupted_handler = 0;
    size_t module;
    size_t modules = get_modules_count(rdram);

    if (reg >= RDRAM_REGS_COUNT) {
        return;
    }

    if (address & RDRAM_BCAST_ADDRESS_MASK) {
        for (module = 0; module < modules; ++module) {
            masked_write(&rdram->regs[module][reg], value, mask);
        }
    }
    else {
        module = get_module(rdram, address);
        if (module != RDRAM_MAX_MODULES_COUNT) {
            masked_write(&rdram->regs[module][reg], value, mask);
        }
    }

    /* toggle corrupt handler based on CC value for all modules,
     * only check values when writing to the mode register */
    if (reg == RDRAM_MODE_REG) {
        for (module = 0; module < modules; ++module) {
            mode = rdram->regs[module][RDRAM_MODE_REG] ^ UINT32_C(0xc0c0c0c0);
            corrupted_handler |= ((mode & RDRAM_MODE_CE_MASK) && (cc_value(mode) == 0));
        }
        if (rdram->corrupted_handler != corrupted_handler) {
            map_corrupt_rdram(rdram, corrupted_handler);
            rdram->corrupted_handler = corrupted_handler;
        }
    }
}


void read_rdram_dram(void* opaque, uint32_t address, uint32_t* value)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t addr = rdram_dram_address(address);

    if (address < rdram->dram_size)
    {
        *value = rdram->dram[addr];
    }
    else
    {
        *value = 0;
    }
}

/* RMG-DIAG (temporary, logging only), sessions 8-10.
 * (1) WATCH: CPU stores to three words of the MTM64 manager-thread object
 *     (thread 0x80143d60): +0x264 (0x80143fc4), +0x350 (0x801440b0), +0x35c
 *     (0x801440bc). Rarely written, so the cap is enough. (+0x268 is the "current
 *     request" pointer, written twice per controller poll: noise, no longer watched.)
 * (2) PIDMA: every cart (PI) DMA with cart/dram address, length and guest pc, so
 *     that the cold boot and the post-reset run can be compared line by line.
 * Both are capped per epoch (a new epoch = g_rmg_diag_vi went backwards, i.e. a
 * soft reset re-armed the heartbeat). Remove with the other RMG-DIAG code. */
extern unsigned int g_rmg_diag_vi;
static unsigned int rmg_diag_watch_lines = 0, rmg_diag_watch_last_vi = 0;
static unsigned int rmg_diag_pi_lines = 0, rmg_diag_pi_last_vi = 0;

#define RMG_DIAG_WATCHED(a) ((a) == UINT32_C(0x50ff1) || (a) == UINT32_C(0x5102c) || (a) == UINT32_C(0x5102f))

static void rmg_diag_watch_cpu(struct rdram* rdram, uint32_t address, uint32_t value, uint32_t mask, uint32_t old)
{
    const int64_t* g = r4300_regs(rdram->r4300);

    if (g_rmg_diag_vi < rmg_diag_watch_last_vi)
        rmg_diag_watch_lines = 0;
    rmg_diag_watch_last_vi = g_rmg_diag_vi;
    if (rmg_diag_watch_lines >= 60)
        return;
    ++rmg_diag_watch_lines;
    DebugMessage(M64MSG_VERBOSE,
        "WATCH cpu store addr=%08x value=%08x mask=%08x old=%08x new=%08x pc=%08x ra=%08x sp=%08x vi=%u",
        (unsigned)(address | 0x80000000u), (unsigned)value, (unsigned)mask, (unsigned)old,
        (unsigned)rdram->dram[rdram_dram_address(address)],
        (unsigned)*r4300_pc(rdram->r4300), (unsigned)g[31], (unsigned)g[29], g_rmg_diag_vi);
}

/* RMG-DIAG session 11: whole RDRAM (plain big-endian bytes) into the working directory,
 * used to compare the guest state of the cold boot and of the post-reset run right before
 * they start loading different assets (first PIDMA divergence is #42). */
static void rmg_diag_dump_rdram_epoch(struct rdram* rdram, unsigned int epoch, unsigned int pidma)
{
    static uint8_t buf[65536];
    char name[96];
    const uint32_t* dram = rdram->dram;
    const size_t size = rdram->dram_size;
    size_t off, i, n;
    FILE* f;

    sprintf(name, "rmg_rdram_epoch%u_at_pidma%u.bin", epoch, pidma);
    f = fopen(name, "wb");
    if (f == NULL)
    {
        DebugMessage(M64MSG_VERBOSE, "RDRAM dump: cannot create %s", name);
        return;
    }
    for (off = 0; off < size; off += sizeof(buf))
    {
        n = size - off;
        if (n > sizeof(buf))
            n = sizeof(buf);
        for (i = 0; i < n / 4; ++i)
        {
            const uint32_t w = dram[off / 4 + i];
            buf[4 * i] = (uint8_t)(w >> 24);
            buf[4 * i + 1] = (uint8_t)(w >> 16);
            buf[4 * i + 2] = (uint8_t)(w >> 8);
            buf[4 * i + 3] = (uint8_t)w;
        }
        if (fwrite(buf, 1, n, f) != n)
            break;
    }
    fclose(f);
    DebugMessage(M64MSG_VERBOSE, "RDRAM dump: wrote %s (%u bytes)", name, (unsigned)size);
}

void rmg_diag_pi_dma(struct rdram* rdram, uint32_t dram_addr, uint32_t length, uint32_t cart_addr)
{
    static unsigned int rmg_diag_pi_epoch = 0;

    if (g_rmg_diag_vi < rmg_diag_pi_last_vi)
    {
        rmg_diag_pi_lines = 0;
        ++rmg_diag_pi_epoch;
    }
    rmg_diag_pi_last_vi = g_rmg_diag_vi;
    if (rmg_diag_pi_lines >= 450)
        return;
    ++rmg_diag_pi_lines;
    DebugMessage(M64MSG_VERBOSE,
        "PIDMA #%u cart=%08x dram=%08x len=%06x pc=%08x vi=%u%s",
        rmg_diag_pi_lines, (unsigned)cart_addr, (unsigned)dram_addr, (unsigned)length,
        (unsigned)*r4300_pc(rdram->r4300), g_rmg_diag_vi,
        (dram_addr < UINT32_C(0x1440c0) && (uint64_t)dram_addr + length > UINT32_C(0x143fc4)) ? " TOUCHES-WATCH" : "");

    /* session 11: guest call chain of the DMAs #36..#60 (64 words of stack, saved return
     * addresses inside), and a full RDRAM dump at DMA #41 of the cold boot (epoch 0) and of
     * the first post-reset run (epoch 1). Read-only, no effect on emulation. */
    if (rmg_diag_pi_lines >= 36 && rmg_diag_pi_lines <= 60)
    {
        const int64_t* g = r4300_regs(rdram->r4300);
        const uint32_t sp = (uint32_t)g[29];
        const uint32_t phys = sp & 0x1ffffffcu;
        unsigned int row, col;

        if (sp >= 0x80000000u && sp < 0xa0000000u && (size_t)phys + 0x100 <= rdram->dram_size)
        {
            const uint32_t* k = &rdram->dram[phys / 4];
            for (row = 0; row < 4; ++row)
            {
                char line[16 * 9 + 1];
                for (col = 0; col < 16; ++col)
                    sprintf(line + 9 * col, "%08x ", (unsigned)k[row * 16 + col]);
                DebugMessage(M64MSG_VERBOSE, "PIDMA #%u STACK ra=%08x sp=%08x +%02x: %s",
                    rmg_diag_pi_lines, (unsigned)g[31], (unsigned)sp, row * 0x40, line);
            }
        }
    }
    if (rmg_diag_pi_lines == 41 && rmg_diag_pi_epoch <= 1)
        rmg_diag_dump_rdram_epoch(rdram, rmg_diag_pi_epoch, rmg_diag_pi_lines);
}

void write_rdram_dram(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rdram* rdram = (struct rdram*)opaque;
    uint32_t addr = rdram_dram_address(address);

    if (address < rdram->dram_size)
    {
        uint32_t rmg_old = rdram->dram[addr];
        masked_write(&rdram->dram[addr], value, mask);
        if (RMG_DIAG_WATCHED(addr))
            rmg_diag_watch_cpu(rdram, address, value, mask, rmg_old);
    }
}
