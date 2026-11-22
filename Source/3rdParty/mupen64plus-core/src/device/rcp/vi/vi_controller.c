/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - vi_controller.c                                         *
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

#include "vi_controller.h"

#include <string.h>

#include "api/callbacks.h"
#include "api/m64p_types.h"
#include "device/memory/memory.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rdram/rdram.h"
#include <time.h>
#include "device/r4300/cp0.h"
#include "device/rcp/rdp/rdp_core.h"
#include "device/rcp/rsp/rsp_core.h"
#include "main/main.h"
#include "plugin/plugin.h"

unsigned int vi_clock_from_tv_standard(m64p_system_type tv_standard)
{
    switch(tv_standard)
    {
    case SYSTEM_PAL:
        return 49656530;
    case SYSTEM_MPAL:
        return 48628316;
    case SYSTEM_NTSC:
    default:
        return 48681812;
    }
}

unsigned int vi_expected_refresh_rate_from_tv_standard(m64p_system_type tv_standard)
{
    switch (tv_standard)
    {
    case SYSTEM_PAL:
        return 50;
    case SYSTEM_NTSC:
    case SYSTEM_MPAL:
    default:
        return 60;
    }
}

void set_vi_vertical_interrupt(struct vi_controller* vi)
{
    if (!get_event(&vi->mi->r4300->cp0.q, VI_INT) && (vi->regs[VI_V_INTR_REG] < vi->regs[VI_V_SYNC_REG]))
    {
        cp0_update_count(vi->mi->r4300);
        add_interrupt_event(&vi->mi->r4300->cp0, VI_INT, vi->delay);
    }
}

void init_vi(struct vi_controller* vi, unsigned int clock, unsigned int expected_refresh_rate,
             struct mi_controller* mi, struct rdp_core* dp)
{
    vi->clock = clock;
    vi->expected_refresh_rate = expected_refresh_rate;
    vi->mi = mi;
    vi->dp = dp;
}

void poweron_vi(struct vi_controller* vi)
{
    memset(vi->regs, 0, VI_REGS_COUNT*sizeof(uint32_t));
    vi->field = 0;
    vi->delay = 0;
    vi->count_per_scanline = 0;
}

void read_vi_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct vi_controller* vi = (struct vi_controller*)opaque;
    uint32_t reg = vi_reg(address);
    const uint32_t* cp0_regs = r4300_cp0_regs(&vi->mi->r4300->cp0);

    if (reg == VI_CURRENT_REG)
    {
        uint32_t* next_vi = get_event(&vi->mi->r4300->cp0.q, VI_INT);
        if (next_vi != NULL) {
            cp0_update_count(vi->mi->r4300);
            vi->regs[VI_CURRENT_REG] = (vi->delay - (*next_vi - cp0_regs[CP0_COUNT_REG])) / vi->count_per_scanline;

            /* wrap around VI_CURRENT_REG if needed */
            if (vi->regs[VI_CURRENT_REG] >= vi->regs[VI_V_SYNC_REG])
                vi->regs[VI_CURRENT_REG] -= vi->regs[VI_V_SYNC_REG];
        }

        /* update current field */
        vi->regs[VI_CURRENT_REG] = (vi->regs[VI_CURRENT_REG] & (~1)) | vi->field;
    }

    if (reg < VI_REGS_COUNT)
    {
        *value = vi->regs[reg];
    }
}

void write_vi_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct vi_controller* vi = (struct vi_controller*)opaque;
    uint32_t reg = vi_reg(address);

    switch(reg)
    {
    case VI_STATUS_REG:
        if ((vi->regs[VI_STATUS_REG] & mask) != (value & mask))
        {
            masked_write(&vi->regs[VI_STATUS_REG], value, mask);
            gfx.viStatusChanged();
        }
        return;

    case VI_WIDTH_REG:
        if ((vi->regs[VI_WIDTH_REG] & mask) != (value & mask))
        {
            masked_write(&vi->regs[VI_WIDTH_REG], value, mask);
            gfx.viWidthChanged();
        }
        return;

    case VI_CURRENT_REG:
        clear_rcp_interrupt(vi->mi, MI_INTR_VI);
        return;

    case VI_V_SYNC_REG:
        if ((vi->regs[VI_V_SYNC_REG] & mask) != (value & mask))
        {
            masked_write(&vi->regs[VI_V_SYNC_REG], value, mask);
            vi->count_per_scanline = (vi->clock / vi->expected_refresh_rate) / (vi->regs[VI_V_SYNC_REG] + 1);
            vi->delay = (vi->regs[VI_V_SYNC_REG] + 1) * vi->count_per_scanline;
            set_vi_vertical_interrupt(vi);
        }
        return;

    case VI_V_INTR_REG:
        masked_write(&vi->regs[VI_V_INTR_REG], value, mask);
        set_vi_vertical_interrupt(vi);
        return;
    }

    if (reg < VI_REGS_COUNT)
    {
        masked_write(&vi->regs[reg], value, mask);
    }
}

/* Post soft-reset heartbeat: once a second for ~20 s after a soft reset it
 * logs the state of the CPU/RCP so that a black screen can be traced. */
/* Armed from power-on as well (not only after a soft reset), so that a cold-boot
 * hang can be traced with the same PIF/heartbeat lines. A soft reset re-arms it. */
int g_rmg_diag_active = 1;
unsigned int g_rmg_diag_pif_lines = 0;
unsigned int g_rmg_diag_vi = 0, g_rmg_diag_sp_gfx = 0, g_rmg_diag_sp_audio = 0, g_rmg_diag_sp_other = 0;
unsigned int g_rmg_diag_ev[6] = {0, 0, 0, 0, 0, 0};
clock_t g_rmg_diag_t0 = 0;

static void rmg_diag_tick(struct vi_controller* vi)
{
    static unsigned int lines = 0;
    struct r4300_core* r4300 = vi->mi->r4300;
    struct rdp_core* dp = vi->dp;
    struct rsp_core* sp = dp->sp;
    const uint32_t* cp0r = r4300_cp0_regs(&r4300->cp0);

    if (g_rmg_diag_vi == 0)
    {
        lines = 0;
        if (g_rmg_diag_t0 == 0)
            g_rmg_diag_t0 = clock();   /* cold boot: start the timer at the first VI */
    }

    g_rmg_diag_vi++;
    if ((g_rmg_diag_vi % 60) != 0)
        return;

    /* VERBOSE on purpose: INFO/STATUS core messages are shown in the RMG status
     * bar and a long text there makes the main window grow. */
    DebugMessage(M64MSG_VERBOSE,
        "Reset heartbeat #%u: ms=%ld vi=%u pc=%08x ev(cmp/si/pi/ai/sp/dp)=%u/%u/%u/%u/%u/%u "
        "sp_tasks(gfx/audio/other)=%u/%u/%u SP_STATUS=%08x SP_PC=%08x "
        "DPC(st/start/end/cur)=%x/%x/%x/%x unfreeze=%u MI(intr/mask)=%x/%x VI(status/origin/hstart)=%x/%x/%x "
        "CP0(status/cause/compare/count/epc)=%x/%x/%x/%x/%x",
        lines + 1, (long)((g_rmg_diag_t0 == 0) ? 0 : (clock() - g_rmg_diag_t0) * 1000 / CLOCKS_PER_SEC),
        g_rmg_diag_vi, (unsigned)*r4300_pc(r4300),
        g_rmg_diag_ev[0], g_rmg_diag_ev[1], g_rmg_diag_ev[2], g_rmg_diag_ev[3], g_rmg_diag_ev[4], g_rmg_diag_ev[5],
        g_rmg_diag_sp_gfx, g_rmg_diag_sp_audio, g_rmg_diag_sp_other,
        (unsigned)sp->regs[SP_STATUS_REG], (unsigned)sp->regs2[SP_PC_REG],
        (unsigned)dp->dpc_regs[DPC_STATUS_REG], (unsigned)dp->dpc_regs[DPC_START_REG],
        (unsigned)dp->dpc_regs[DPC_END_REG], (unsigned)dp->dpc_regs[DPC_CURRENT_REG],
        (unsigned)dp->do_on_unfreeze,
        (unsigned)vi->mi->regs[MI_INTR_REG], (unsigned)vi->mi->regs[MI_INTR_MASK_REG],
        (unsigned)vi->regs[VI_STATUS_REG], (unsigned)vi->regs[VI_ORIGIN_REG], (unsigned)vi->regs[VI_H_START_REG],
        (unsigned)cp0r[CP0_STATUS_REG], (unsigned)cp0r[CP0_CAUSE_REG], (unsigned)cp0r[CP0_COMPARE_REG],
        (unsigned)cp0r[CP0_COUNT_REG], (unsigned)cp0r[CP0_EPC_REG]);

    /* RMG-DIAG (temporary): TLB/exception details and a few GPRs, to see what the
     * guest dereferenced when it crashed after a reset. Remove with the heartbeat. */
    {
        const int64_t* gpr = r4300_regs(r4300);
        DebugMessage(M64MSG_VERBOSE,
            "Reset heartbeat #%u CP0b: badvaddr=%x context=%x entryhi=%x index=%x wired=%x random=%x errorepc=%x "
            "GPR(sp/ra/a0/v0)=%08x/%08x/%08x/%08x",
            lines + 1,
            (unsigned)cp0r[CP0_BADVADDR_REG], (unsigned)cp0r[CP0_CONTEXT_REG], (unsigned)cp0r[CP0_ENTRYHI_REG],
            (unsigned)cp0r[CP0_INDEX_REG], (unsigned)cp0r[CP0_WIRED_REG], (unsigned)cp0r[CP0_RANDOM_REG],
            (unsigned)cp0r[CP0_ERROREPC_REG],
            (unsigned)gpr[29], (unsigned)gpr[31], (unsigned)gpr[4], (unsigned)gpr[2]);
    }

    /* RMG-DIAG (temporary): value of the watched guest word 0x80143fc8 (and the
     * thread object header at 0x80143d60) once per heartbeat, catches any writer. */
    if (r4300->rdram->dram_size > 0x144000)
    {
        const uint32_t* d = r4300->rdram->dram;
        DebugMessage(M64MSG_VERBOSE,
            "Reset heartbeat #%u WATCHVAL 80143fc8=%08x 80143fcc=%08x thread80143d60: %08x %08x %08x %08x %08x %08x",
            lines + 1, (unsigned)d[0x143fc8 / 4], (unsigned)d[0x143fcc / 4],
            (unsigned)d[0x143d60 / 4], (unsigned)d[0x143d64 / 4], (unsigned)d[0x143d68 / 4],
            (unsigned)d[0x143d6c / 4], (unsigned)d[0x143d70 / 4], (unsigned)d[0x143d74 / 4]);
    }

    /* RMG-DIAG (temporary): once per distinct EPC, log the 16 instruction words that
     * end 0x0c after EPC (read-only, straight from RDRAM) and all GPRs, so the guest
     * code that faulted can be decoded. Remove with the heartbeat. */
    {
        static uint32_t last_epc = 0;
        const uint32_t epc = cp0r[CP0_EPC_REG];
        const uint32_t phys = epc & 0x1ffffffc;

        if (epc != last_epc && epc >= 0x80000000u && epc < 0xc0000000u
            && phys >= 0x30 && (size_t)phys + 0x10 <= r4300->rdram->dram_size)
        {
            const uint32_t* w = &r4300->rdram->dram[(phys - 0x30) / 4];
            const int64_t* g = r4300_regs(r4300);
            last_epc = epc;
            DebugMessage(M64MSG_VERBOSE,
                "Reset heartbeat #%u CODE epc=%08x from=%08x: %08x %08x %08x %08x %08x %08x %08x %08x "
                "%08x %08x %08x %08x %08x %08x %08x %08x",
                lines + 1, (unsigned)epc, (unsigned)(epc & ~3u) - 0x30,
                (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3],
                (unsigned)w[4], (unsigned)w[5], (unsigned)w[6], (unsigned)w[7],
                (unsigned)w[8], (unsigned)w[9], (unsigned)w[10], (unsigned)w[11],
                (unsigned)w[12], (unsigned)w[13], (unsigned)w[14], (unsigned)w[15]);
            DebugMessage(M64MSG_VERBOSE,
                "Reset heartbeat #%u GPR1-31: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x "
                "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
                lines + 1,
                (unsigned)g[1], (unsigned)g[2], (unsigned)g[3], (unsigned)g[4], (unsigned)g[5], (unsigned)g[6],
                (unsigned)g[7], (unsigned)g[8], (unsigned)g[9], (unsigned)g[10], (unsigned)g[11], (unsigned)g[12],
                (unsigned)g[13], (unsigned)g[14], (unsigned)g[15], (unsigned)g[16], (unsigned)g[17], (unsigned)g[18],
                (unsigned)g[19], (unsigned)g[20], (unsigned)g[21], (unsigned)g[22], (unsigned)g[23], (unsigned)g[24],
                (unsigned)g[25], (unsigned)g[26], (unsigned)g[27], (unsigned)g[28], (unsigned)g[29], (unsigned)g[30],
                (unsigned)g[31]);
        }
    }

    if (++lines >= 20)
        g_rmg_diag_active = 0;
}

void vi_vertical_interrupt_event(void* opaque)
{
    struct vi_controller* vi = (struct vi_controller*)opaque;

    if (g_rmg_diag_active)
        rmg_diag_tick(vi);

    if (vi->dp->do_on_unfreeze & DELAY_DP_INT)
        vi->dp->do_on_unfreeze |= DELAY_UPDATESCREEN;
    else
        gfx.updateScreen();

    /* allow main module to do things on VI event */
    new_vi();

    /* toggle vi field if in interlaced mode */
    vi->field ^= (vi->regs[VI_STATUS_REG] >> 6) & 0x1;

    /* schedule next vertical interrupt */
    uint32_t next_vi = *get_event(&vi->mi->r4300->cp0.q, VI_INT) + vi->delay;
    remove_interrupt_event(&vi->mi->r4300->cp0);
    add_interrupt_event_count(&vi->mi->r4300->cp0, VI_INT, next_vi);

    /* trigger interrupt */
    raise_rcp_interrupt(vi->mi, MI_INTR_VI);
}

