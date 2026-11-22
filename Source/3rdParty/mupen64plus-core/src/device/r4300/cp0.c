/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - cp0.c                                                   *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2002 Hacktarux                                          *
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

#include <stdint.h>
#include <string.h>

#include "cp0.h"
#include "r4300_core.h"
#include "new_dynarec/new_dynarec.h"
#include "recomp.h"
#include "api/callbacks.h"
#include "api/m64p_types.h"
#include "device/rdram/rdram.h"

#ifdef COMPARE_CORE
#include "api/debugger.h"
#endif

#ifdef DBG
#include "debugger/dbg_debugger.h"
#endif

/* global functions */
void init_cp0(struct cp0* cp0, unsigned int count_per_op, unsigned int count_per_op_denom_pot, struct new_dynarec_hot_state* new_dynarec_hot_state, const struct interrupt_handler* interrupt_handlers)
{
    cp0->count_per_op = count_per_op;
    cp0->count_per_op_denom_pot = count_per_op_denom_pot;
#ifdef NEW_DYNAREC
    cp0->new_dynarec_hot_state = new_dynarec_hot_state;
#endif

    memcpy(cp0->interrupt_handlers, interrupt_handlers, CP0_INTERRUPT_HANDLERS_COUNT*sizeof(*interrupt_handlers));
}

void poweron_cp0(struct cp0* cp0)
{
    uint32_t* cp0_regs;
    unsigned int* cp0_next_interrupt;
    int* cp0_cycle_count;

    cp0_regs = r4300_cp0_regs(cp0);
    cp0_next_interrupt = r4300_cp0_next_interrupt(cp0);
    cp0_cycle_count = r4300_cp0_cycle_count(cp0);

    memset(cp0_regs, 0, CP0_REGS_COUNT * sizeof(cp0_regs[0]));
    cp0_regs[CP0_RANDOM_REG] = UINT32_C(31);
    cp0_regs[CP0_STATUS_REG]= UINT32_C(0x34000000);
    cp0_regs[CP0_CONFIG_REG]= UINT32_C(0x6e463);
    cp0_regs[CP0_PREVID_REG] = UINT32_C(0xb10);
    cp0_regs[CP0_COUNT_REG] = UINT32_C(0x5000);
    cp0_regs[CP0_CAUSE_REG] = UINT32_C(0x5c);
    cp0_regs[CP0_CONTEXT_REG] = UINT32_C(0x7ffff0);
    cp0_regs[CP0_EPC_REG] = UINT32_C(0xffffffff);
    cp0_regs[CP0_BADVADDR_REG] = UINT32_C(0xffffffff);
    cp0_regs[CP0_ERROREPC_REG] = UINT32_C(0xffffffff);

    /* XXX: clarify what is done on poweron, in soft_reset and in execute... */
    cp0->interrupt_unsafe_state = 0;
    *cp0_next_interrupt = 0;
    *cp0_cycle_count = 0;
    cp0->last_addr = UINT32_C(0xbfc00000);

    init_interrupt(cp0);

    poweron_tlb(&cp0->tlb);
}


uint32_t* r4300_cp0_regs(struct cp0* cp0)
{
#ifndef NEW_DYNAREC
    return cp0->regs;
#else
	/* New dynarec uses a different memory layout */
    return cp0->new_dynarec_hot_state->cp0_regs;
#endif
}

uint64_t* r4300_cp0_latch(struct cp0* cp0)
{
#ifndef NEW_DYNAREC
    return &cp0->latch;
#else
    /* New dynarec uses a different memory layout */
    return &cp0->new_dynarec_hot_state->cp0_latch;
#endif
}

uint32_t* r4300_cp0_last_addr(struct cp0* cp0)
{
    return &cp0->last_addr;
}

unsigned int* r4300_cp0_next_interrupt(struct cp0* cp0)
{
    return &cp0->next_interrupt;
}

int* r4300_cp0_cycle_count(struct cp0* cp0)
{
#ifndef NEW_DYNAREC
    return &cp0->cycle_count;
#else
    /* New dynarec uses a different memory layout */
    return &cp0->new_dynarec_hot_state->cycle_count;
#endif
}


int check_cop1_unusable(struct r4300_core* r4300)
{
    uint32_t* cp0_regs = r4300_cp0_regs(&r4300->cp0);

    if (!(cp0_regs[CP0_STATUS_REG] & CP0_STATUS_CU1))
    {
        cp0_regs[CP0_CAUSE_REG] = CP0_CAUSE_EXCCODE_CPU | CP0_CAUSE_CE1;
        exception_general(r4300);
        return 1;
    }
    return 0;
}

int check_cop2_unusable(struct r4300_core* r4300)
{
    uint32_t* cp0_regs = r4300_cp0_regs(&r4300->cp0);

    if (!(cp0_regs[CP0_STATUS_REG] & CP0_STATUS_CU2))
    {
        cp0_regs[CP0_CAUSE_REG] = CP0_CAUSE_EXCCODE_CPU | CP0_CAUSE_CE2;
        exception_general(r4300);
        return 1;
    }
    return 0;
}

void cp0_update_count(struct r4300_core* r4300)
{
    struct cp0* cp0 = &r4300->cp0;
    uint32_t* cp0_regs = r4300_cp0_regs(cp0);

#ifdef NEW_DYNAREC
    if (r4300->emumode != EMUMODE_DYNAREC)
    {
#endif
        uint32_t count = ((*r4300_pc(r4300) - cp0->last_addr) >> 2) * cp0->count_per_op;
        if (r4300->cp0.count_per_op_denom_pot) {
            count += (1 << r4300->cp0.count_per_op_denom_pot) - 1;
            count >>= r4300->cp0.count_per_op_denom_pot;
        }
        cp0_regs[CP0_COUNT_REG] += count;
        *r4300_cp0_cycle_count(cp0) += count;
        cp0->last_addr = *r4300_pc(r4300);
#ifdef NEW_DYNAREC
    }
    else
        cp0_regs[CP0_COUNT_REG] = *r4300_cp0_next_interrupt(cp0) + *r4300_cp0_cycle_count(cp0);
#endif

#ifdef COMPARE_CORE
   if (r4300->delay_slot)
     CoreCompareCallback();
#endif
/*#ifdef DBG
   if (g_DebuggerActive && !r4300->delay_slot) update_debugger(*r4300_pc(r4300));
#endif
*/
}

static void exception_epilog(struct r4300_core* r4300)
{
#ifndef NO_ASM
#ifndef NEW_DYNAREC
    if (r4300->emumode == EMUMODE_DYNAREC)
    {
        dyna_jump();
        if (!r4300->recomp.dyna_interp) { r4300->delay_slot = 0; }
    }
#endif
#endif

#ifndef NEW_DYNAREC
    if (r4300->emumode != EMUMODE_DYNAREC || r4300->recomp.dyna_interp)
    {
        r4300->recomp.dyna_interp = 0;
#else
    if (r4300->emumode != EMUMODE_DYNAREC)
    {
#endif
        if (r4300->delay_slot)
        {
            r4300->skip_jump = *r4300_pc(r4300);
            *r4300_cp0_next_interrupt(&r4300->cp0) = 0;
            *r4300_cp0_cycle_count(&r4300->cp0) = 0;
        }
    }
}



/* RMG-DIAG (temporary, logging only): record the first TLB exceptions of a run
 * (cause, EPC, delay-slot state, the guest instructions around the faulting PC,
 * all GPRs and the libultra scheduler globals) to find which guest instruction
 * dereferences NULL after a soft reset. Remove together with the other
 * RMG-DIAG heartbeat code once the reset problem is solved. */
static void rmg_diag_tlb_exception(struct r4300_core* r4300, uint32_t address, int w,
                                   uint32_t pc, unsigned int ds)
{
    static unsigned int count = 0;
    const uint32_t* cp0_regs = r4300_cp0_regs(&r4300->cp0);
    const int64_t* g = r4300_regs(r4300);
    const uint32_t* dram = r4300->rdram->dram;
    const size_t size = r4300->rdram->dram_size;
    uint32_t phys;
    uint32_t rt;

    if (count >= 12)
        return;
    count++;

    DebugMessage(M64MSG_VERBOSE,
        "TLB exc #%u: w=%d addr=%08x pc=%08x delay_slot=%u emumode=%u -> cause=%08x epc=%08x status=%08x",
        count, w, (unsigned)address, (unsigned)pc, ds, (unsigned)r4300->emumode,
        (unsigned)cp0_regs[CP0_CAUSE_REG], (unsigned)cp0_regs[CP0_EPC_REG],
        (unsigned)cp0_regs[CP0_STATUS_REG]);

    phys = pc & 0x1ffffffcu;
    if (pc >= 0x80000000u && pc < 0xc0000000u && phys >= 0x10 && (size_t)phys + 0x10 <= size)
    {
        const uint32_t* c = &dram[(phys - 0x10) / 4];
        DebugMessage(M64MSG_VERBOSE,
            "TLB exc #%u CODE from=%08x: %08x %08x %08x %08x %08x %08x %08x %08x",
            count, (unsigned)(pc & ~3u) - 0x10,
            (unsigned)c[0], (unsigned)c[1], (unsigned)c[2], (unsigned)c[3],
            (unsigned)c[4], (unsigned)c[5], (unsigned)c[6], (unsigned)c[7]);
    }

    DebugMessage(M64MSG_VERBOSE,
        "TLB exc #%u GPR1-31: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x "
        "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
        count,
        (unsigned)g[1], (unsigned)g[2], (unsigned)g[3], (unsigned)g[4], (unsigned)g[5], (unsigned)g[6],
        (unsigned)g[7], (unsigned)g[8], (unsigned)g[9], (unsigned)g[10], (unsigned)g[11], (unsigned)g[12],
        (unsigned)g[13], (unsigned)g[14], (unsigned)g[15], (unsigned)g[16], (unsigned)g[17], (unsigned)g[18],
        (unsigned)g[19], (unsigned)g[20], (unsigned)g[21], (unsigned)g[22], (unsigned)g[23], (unsigned)g[24],
        (unsigned)g[25], (unsigned)g[26], (unsigned)g[27], (unsigned)g[28], (unsigned)g[29], (unsigned)g[30],
        (unsigned)g[31]);

    /* libultra data (this ROM): 80010070.. = ?, RunQueue, ActiveQueue, RunningThread, FaultedThread */
    if (size >= 0x10090)
    {
        const uint32_t* q = &dram[0x10070 / 4];
        DebugMessage(M64MSG_VERBOSE,
            "TLB exc #%u OSQ[80010070..8001008c]: %08x %08x %08x %08x %08x %08x %08x %08x",
            count, (unsigned)q[0], (unsigned)q[1], (unsigned)q[2], (unsigned)q[3],
            (unsigned)q[4], (unsigned)q[5], (unsigned)q[6], (unsigned)q[7]);

        rt = dram[0x10080 / 4];
        phys = rt & 0x1ffffffcu;
        if (rt >= 0x80000000u && rt < 0xa0000000u && (size_t)phys + 0x20 <= size)
        {
            const uint32_t* t = &dram[phys / 4];
            DebugMessage(M64MSG_VERBOSE,
                "TLB exc #%u RunningThread %08x: %08x %08x %08x %08x %08x %08x %08x %08x",
                count, (unsigned)rt,
                (unsigned)t[0], (unsigned)t[1], (unsigned)t[2], (unsigned)t[3],
                (unsigned)t[4], (unsigned)t[5], (unsigned)t[6], (unsigned)t[7]);
        }
    }
}

void TLB_refill_exception(struct r4300_core* r4300, uint32_t address, int w)
{
    uint32_t* cp0_regs = r4300_cp0_regs(&r4300->cp0);
    int usual_handler = 0, i;
    const uint32_t rmg_pc = *r4300_pc(r4300);
    const unsigned int rmg_ds = r4300->delay_slot;

    if (r4300->emumode != EMUMODE_DYNAREC && w != 2) {
        cp0_update_count(r4300);
    }

    cp0_regs[CP0_CAUSE_REG] = (w == 1)
        ? CP0_CAUSE_EXCCODE_TLBS
        : CP0_CAUSE_EXCCODE_TLBL;

    cp0_regs[CP0_BADVADDR_REG] = address;
    cp0_regs[CP0_CONTEXT_REG] = (cp0_regs[CP0_CONTEXT_REG] & UINT32_C(0xFF80000F))
        | ((address >> 9) & UINT32_C(0x007FFFF0));
    cp0_regs[CP0_ENTRYHI_REG] = address & UINT32_C(0xFFFFE000);

    if (cp0_regs[CP0_STATUS_REG] & CP0_STATUS_EXL)
    {
        generic_jump_to(r4300, UINT32_C(0x80000180));


        if (r4300->delay_slot == 1 || r4300->delay_slot == 3) {
            cp0_regs[CP0_CAUSE_REG] |= CP0_CAUSE_BD;
        }
        else {
            cp0_regs[CP0_CAUSE_REG] &= ~CP0_CAUSE_BD;
        }
    }
    else
    {
        if (r4300->emumode != EMUMODE_PURE_INTERPRETER)
        {
            cp0_regs[CP0_EPC_REG] = (w != 2)
                ? *r4300_pc(r4300)
                : address;
        }
        else {
            cp0_regs[CP0_EPC_REG] = *r4300_pc(r4300);
        }

        cp0_regs[CP0_CAUSE_REG] &= ~CP0_CAUSE_BD;
        cp0_regs[CP0_STATUS_REG] |= CP0_STATUS_EXL;

        if (address >= UINT32_C(0x80000000) && address < UINT32_C(0xc0000000)) {
            usual_handler = 1;
        }

        for (i = 0; i < 32; i++)
        {
            if (/*r4300->cp0.tlb.entries[i].v_even &&*/ address >= r4300->cp0.tlb.entries[i].start_even &&
                    address <= r4300->cp0.tlb.entries[i].end_even) {
                usual_handler = 1;
            }
            if (/*r4300->cp0.tlb.entries[i].v_odd &&*/ address >= r4300->cp0.tlb.entries[i].start_odd &&
                    address <= r4300->cp0.tlb.entries[i].end_odd) {
                usual_handler = 1;
            }
        }

        generic_jump_to(r4300, (usual_handler)
                ? UINT32_C(0x80000180)
                : UINT32_C(0x80000000));
    }

    if (r4300->delay_slot == 1 || r4300->delay_slot == 3)
    {
        cp0_regs[CP0_CAUSE_REG] |= CP0_CAUSE_BD;
        cp0_regs[CP0_EPC_REG] -= 4;
    }
    else
    {
        cp0_regs[CP0_CAUSE_REG] &= ~CP0_CAUSE_BD;
    }
    if (w != 2) {
        cp0_regs[CP0_EPC_REG] -= 4;
    }

    rmg_diag_tlb_exception(r4300, address, w, rmg_pc, rmg_ds);

    r4300->cp0.last_addr = *r4300_pc(r4300);

    exception_epilog(r4300);
}

void exception_general(struct r4300_core* r4300)
{
    uint32_t* cp0_regs = r4300_cp0_regs(&r4300->cp0);

    cp0_update_count(r4300);
    cp0_regs[CP0_STATUS_REG] |= CP0_STATUS_EXL;

    cp0_regs[CP0_EPC_REG] = *r4300_pc(r4300);

    if (r4300->delay_slot == 1 || r4300->delay_slot == 3)
    {
        cp0_regs[CP0_CAUSE_REG] |= CP0_CAUSE_BD;
        cp0_regs[CP0_EPC_REG] -= 4;
    }
    else
    {
        cp0_regs[CP0_CAUSE_REG] &= ~CP0_CAUSE_BD;
    }

    generic_jump_to(r4300, UINT32_C(0x80000180));

    r4300->cp0.last_addr = *r4300_pc(r4300);

    exception_epilog(r4300);
}

