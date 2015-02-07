// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/arm/skyeye_common/armemu.h"
#include "core/arm/skyeye_common/vfp/vfp.h"

#include "core/arm/dyncom/arm_dyncom.h"
#include "core/arm/dyncom/arm_dyncom_interpreter.h"

#include "core/core.h"
#include "core/core_timing.h"

const static cpu_config_t s_arm11_cpu_info = {
    "armv6", "arm11", 0x0007b000, 0x0007f000, NONCACHE
};

ARM_DynCom::ARM_DynCom() {
    state = std::unique_ptr<ARMul_State>(new ARMul_State);

    ARMul_EmulateInit();
    memset(state.get(), 0, sizeof(ARMul_State));

    ARMul_NewState((ARMul_State*)state.get());

    state->abort_model = 0;
    state->cpu = (cpu_config_t*)&s_arm11_cpu_info;
    state->bigendSig = LOW;

    ARMul_SelectProcessor(state.get(), ARM_v6_Prop | ARM_v5_Prop | ARM_v5e_Prop);
    state->lateabtSig = LOW;

    // Reset the core to initial state
    ARMul_CoProInit(state.get());
    ARMul_Reset(state.get());
    state->NextInstr = RESUME; // NOTE: This will be overwritten by LoadContext
    state->Emulate = 3;

    state->Reg[15] = 0x00000000;
    state->Reg[13] = 0x10000000; // Set stack pointer to the top of the stack
    state->NirqSig = HIGH;

    VFPInit(state.get()); // Initialize the VFP

    ARMul_EmulateInit();
}

ARM_DynCom::~ARM_DynCom() {
}

void ARM_DynCom::SetPC(u32 pc) {
    state->Reg[15] = pc;
}

u32 ARM_DynCom::GetPC() const {
    return state->Reg[15];
}

u32 ARM_DynCom::GetReg(int index) const {
    return state->Reg[index];
}

void ARM_DynCom::SetReg(int index, u32 value) {
    state->Reg[index] = value;
}

u32 ARM_DynCom::GetCPSR() const {
    return state->Cpsr;
}

void ARM_DynCom::SetCPSR(u32 cpsr) {
    state->Cpsr = cpsr;
}

u64 ARM_DynCom::GetTicks() const {
    // TODO(Subv): Remove ARM_DynCom::GetTicks() and use CoreTiming::GetTicks() directly once ARMemu is gone
    return CoreTiming::GetTicks();
}

void ARM_DynCom::AddTicks(u64 ticks) {
    down_count -= ticks;
    if (down_count < 0)
        CoreTiming::Advance();
}

void ARM_DynCom::ExecuteInstructions(int num_instructions) {
    state->NumInstrsToExecute = num_instructions;

    // Dyncom only breaks on instruction dispatch. This only happens on every instruction when
    // executing one instruction at a time. Otherwise, if a block is being executed, more
    // instructions may actually be executed than specified.
    unsigned ticks_executed = InterpreterMainLoop(state.get());
    AddTicks(ticks_executed);
}

void ARM_DynCom::SaveContext(Core::ThreadContext& ctx) {
    memcpy(ctx.cpu_registers, state->Reg, sizeof(ctx.cpu_registers));
    memcpy(ctx.fpu_registers, state->ExtReg, sizeof(ctx.fpu_registers));

    ctx.sp = state->Reg[13];
    ctx.lr = state->Reg[14];
    ctx.pc = state->Reg[15];
    ctx.cpsr = state->Cpsr;

    ctx.fpscr = state->VFP[1];
    ctx.fpexc = state->VFP[2];

    ctx.mode = state->NextInstr;
}

void ARM_DynCom::LoadContext(const Core::ThreadContext& ctx) {
    memcpy(state->Reg, ctx.cpu_registers, sizeof(ctx.cpu_registers));
    memcpy(state->ExtReg, ctx.fpu_registers, sizeof(ctx.fpu_registers));

    state->Reg[13] = ctx.sp;
    state->Reg[14] = ctx.lr;
    state->Reg[15] = ctx.pc;
    state->Cpsr = ctx.cpsr;

    state->VFP[1] = ctx.fpscr;
    state->VFP[2] = ctx.fpexc;

    state->NextInstr = ctx.mode;
}

void ARM_DynCom::PrepareReschedule() {
    state->NumInstrsToExecute = 0;
}
