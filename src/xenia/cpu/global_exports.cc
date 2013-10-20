/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/global_exports.h>

#include <xenia/logging.h>
#include <xenia/cpu/cpu-private.h>
#include <xenia/cpu/processor.h>
#include <xenia/cpu/sdb.h>
#include <xenia/cpu/ppc/instr.h>
#include <xenia/cpu/ppc/state.h>
#include <xenia/kernel/export.h>


using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::sdb;
using namespace xe::kernel;


namespace {


void _cdecl XeTrap(
    xe_ppc_state_t* state, uint64_t cia) {
  XELOGE("TRAP");
  XEASSERTALWAYS();
}

void* _cdecl XeIndirectBranch(
    xe_ppc_state_t* state, uint64_t target, uint64_t br_ia) {
  // TODO(benvanik): track this statistic - this path is very slow!
  XEASSERT(target != 0xBEBEBEBE);
  Processor* processor = state->processor;
  void* target_ptr = processor->GetFunctionPointer((uint32_t)target);
  // target_ptr will be null when the given target is not a function.
  XEASSERTNOTNULL(target_ptr);
  return target_ptr;
}

void _cdecl XeInvalidInstruction(
    xe_ppc_state_t* state, uint64_t cia, uint64_t data) {
  ppc::InstrData i;
  i.address = (uint32_t)cia;
  i.code = (uint32_t)data;
  i.type = ppc::GetInstrType(i.code);

  if (!i.type) {
    XELOGCPU("INVALID INSTRUCTION %.8X: %.8X ???",
             i.address, i.code);
  } else if (i.type->disassemble) {
    ppc::InstrDisasm d;
    i.type->disassemble(i, d);
    std::string disasm;
    d.Dump(disasm);
    XELOGCPU("INVALID INSTRUCTION %.8X: %.8X %s",
             i.address, i.code, disasm.c_str());
  } else {
    XELOGCPU("INVALID INSTRUCTION %.8X: %.8X %s",
             i.address, i.code, i.type->name);
  }
}

void _cdecl XeAccessViolation(
    xe_ppc_state_t* state, uint64_t cia, uint64_t ea) {
  XELOGE("INVALID ACCESS %.8X: tried to touch %.8X",
         cia, (uint32_t)ea);
  XEASSERTALWAYS();
}

void _cdecl XeTraceKernelCall(
    xe_ppc_state_t* state, uint64_t cia, uint64_t call_ia,
    KernelExport* kernel_export) {
  uint32_t thread_id = state->thread_state->thread_id();
  if (!((1ull << thread_id) & FLAGS_trace_thread_mask)) {
    return;
  }

  xe_log_line("", thread_id, "XeTraceKernelCall", 't',
              "KERNEL CALL: %.8X -> k.%.8X (%s)%s",
              (uint32_t)call_ia - 4, (uint32_t)cia,
              kernel_export ? kernel_export->name : "unknown",
              (kernel_export ? kernel_export->is_implemented : 0) ?
                  "" : " NOT IMPLEMENTED");
}

void _cdecl XeTraceUserCall(
    xe_ppc_state_t* state, uint64_t cia, uint64_t call_ia,
    FunctionSymbol* fn) {
  uint32_t thread_id = state->thread_state->thread_id();
  if (!((1ull << thread_id) & FLAGS_trace_thread_mask)) {
    return;
  }

  xe_log_line("", thread_id, "XeTraceUserCall", 't',
              "USER CALL %.8X -> u.%.8X (%s)",
              (uint32_t)call_ia - 4, (uint32_t)cia, fn->name());
}

void _cdecl XeTraceBranch(
    xe_ppc_state_t* state, uint64_t cia, uint64_t target_ia) {
  uint32_t thread_id = state->thread_state->thread_id();
  if (!((1ull << thread_id) & FLAGS_trace_thread_mask)) {
    return;
  }

  switch (target_ia) {
  case kXEPPCRegLR:
    target_ia = state->lr;
    break;
  case kXEPPCRegCTR:
    target_ia = state->ctr;
    break;
  }

  xe_log_line("", thread_id, "XeTraceBranch", 't',
              "BRANCH %.8X -> b.%.8X",
              (uint32_t)cia, (uint32_t)target_ia);
}

void _cdecl XeTraceFPR(
    xe_ppc_state_t* state, uint64_t fpr0, uint64_t fpr1, uint64_t fpr2,
    uint64_t fpr3, uint64_t fpr4) {
  char buffer[2048];
  buffer[0] = 0;
  int offset = 0;

  uint32_t thread_id = state->thread_state->thread_id();
  if (!((1ull << thread_id) & FLAGS_trace_thread_mask)) {
    return;
  }

  offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
    "%.8X:", state->cia);

  offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
      "\nf%.2d = %e", fpr0, state->f[fpr0]);
  if (fpr1 != UINT_MAX) {
    offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
        "\nf%.2d = %e", fpr1, state->f[fpr1]);
  }
  if (fpr2 != UINT_MAX) {
    offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
        "\nf%.2d = %e", fpr2, state->f[fpr2]);
  }
  if (fpr3 != UINT_MAX) {
    offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
        "\nf%.2d = %e", fpr3, state->f[fpr3]);
  }
  if (fpr4 != UINT_MAX) {
    offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
        "\nf%.2d = %e", fpr4, state->f[fpr4]);
  }

  xe_log_line("", thread_id, "XeTraceFPR", 't', buffer);
}

void _cdecl XeTraceVR(
    xe_ppc_state_t* state, uint64_t vr0, uint64_t vr1, uint64_t vr2,
    uint64_t vr3, uint64_t vr4) {
  char buffer[2048];
  buffer[0] = 0;
  int offset = 0;

  uint32_t thread_id = state->thread_state->thread_id();
  if (!((1ull << thread_id) & FLAGS_trace_thread_mask)) {
    return;
  }

  offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
    "%.8X:", state->cia);

  offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
      "\nvr%.3d=[%.8X, %.8X, %.8X, %.8X] [%e, %e, %e, %e]", vr0,
      state->v[vr0].ix, state->v[vr0].iy, state->v[vr0].iz, state->v[vr0].iw,
      state->v[vr0].x, state->v[vr0].y, state->v[vr0].z, state->v[vr0].w);
  if (vr1 != UINT_MAX) {
    offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
        "\nvr%.3d=[%.8X, %.8X, %.8X, %.8X] [%e, %e, %e, %e]", vr1,
        state->v[vr1].ix, state->v[vr1].iy, state->v[vr1].iz, state->v[vr1].iw,
        state->v[vr1].x, state->v[vr1].y, state->v[vr1].z, state->v[vr1].w);
  }
  if (vr2 != UINT_MAX) {
    offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
        "\nvr%.3d=[%.8X, %.8X, %.8X, %.8X] [%e, %e, %e, %e]", vr2,
        state->v[vr2].ix, state->v[vr2].iy, state->v[vr2].iz, state->v[vr2].iw,
        state->v[vr2].x, state->v[vr2].y, state->v[vr2].z, state->v[vr2].w);
  }
  if (vr3 != UINT_MAX) {
    offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
        "\nvr%.3d=[%.8X, %.8X, %.8X, %.8X] [%e, %e, %e, %e]", vr3,
        state->v[vr3].ix, state->v[vr3].iy, state->v[vr3].iz, state->v[vr3].iw,
        state->v[vr3].x, state->v[vr3].y, state->v[vr3].z, state->v[vr3].w);
  }
  if (vr4 != UINT_MAX) {
    offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
        "\nvr%.3d=[%.8X, %.8X, %.8X, %.8X] [%e, %e, %e, %e]", vr4,
        state->v[vr4].ix, state->v[vr4].iy, state->v[vr4].iz, state->v[vr4].iw,
        state->v[vr4].x, state->v[vr4].y, state->v[vr4].z, state->v[vr4].w);
  }

  xe_log_line("", thread_id, "XeTraceVR", 't', buffer);
}

void _cdecl XeTraceInstruction(
    xe_ppc_state_t* state, uint64_t cia, uint64_t data) {
  char buffer[2048];
  buffer[0] = 0;
  int offset = 0;

  uint32_t thread_id = state->thread_state->thread_id();
  if (!((1ull << thread_id) & FLAGS_trace_thread_mask)) {
    return;
  }

  if (FLAGS_trace_registers) {
    offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
        "\n"
        " lr=%.16llX ctr=%.16llX  cr=%.4X         xer=%.16llX\n"
        " r0=%.16llX  r1=%.16llX  r2=%.16llX  r3=%.16llX\n"
        " r4=%.16llX  r5=%.16llX  r6=%.16llX  r7=%.16llX\n"
        " r8=%.16llX  r9=%.16llX r10=%.16llX r11=%.16llX\n"
        "r12=%.16llX r13=%.16llX r14=%.16llX r15=%.16llX\n"
        "r16=%.16llX r17=%.16llX r18=%.16llX r19=%.16llX\n"
        "r20=%.16llX r21=%.16llX r22=%.16llX r23=%.16llX\n"
        "r24=%.16llX r25=%.16llX r26=%.16llX r27=%.16llX\n"
        "r28=%.16llX r29=%.16llX r30=%.16llX r31=%.16llX\n",
        state->lr, state->ctr, state->cr.value, state->xer,
        state->r[0], state->r[1], state->r[2], state->r[3],
        state->r[4], state->r[5], state->r[6], state->r[7],
        state->r[8], state->r[9], state->r[10], state->r[11],
        state->r[12], state->r[13], state->r[14], state->r[15],
        state->r[16], state->r[17], state->r[18], state->r[19],
        state->r[20], state->r[21], state->r[22], state->r[23],
        state->r[24], state->r[25], state->r[26], state->r[27],
        state->r[28], state->r[29], state->r[30], state->r[31]);
  }

  ppc::InstrData i;
  i.address = (uint32_t)cia;
  i.code = (uint32_t)data;
  i.type = ppc::GetInstrType(i.code);
  if (i.type && i.type->disassemble) {
    ppc::InstrDisasm d;
    i.type->disassemble(i, d);
    std::string disasm;
    d.Dump(disasm);
    offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
        "%.8X %.8X %s %s",
        i.address, i.code,
        i.type && i.type->emit ? " " : "X",
        disasm.c_str());
  } else {
    offset += xesnprintfa(buffer + offset, XECOUNT(buffer) - offset,
        "%.8X %.8X %s %s",
        i.address, i.code,
        i.type && i.type->emit ? " " : "X",
        i.type ? i.type->name : "<unknown>");
  }

  xe_log_line("", thread_id, "XeTraceInstruction", 't', buffer);

  // if (cia == 0x82012074) {
  //   printf("BREAKBREAKBREAK\n");
  // }

  // TODO(benvanik): better disassembly, printing of current register values/etc
}


}


void xe::cpu::GetGlobalExports(GlobalExports* global_exports) {
  global_exports->XeTrap                = XeTrap;
  global_exports->XeIndirectBranch      = XeIndirectBranch;
  global_exports->XeInvalidInstruction  = XeInvalidInstruction;
  global_exports->XeAccessViolation     = XeAccessViolation;
  global_exports->XeTraceKernelCall     = XeTraceKernelCall;
  global_exports->XeTraceUserCall       = XeTraceUserCall;
  global_exports->XeTraceBranch         = XeTraceBranch;
  global_exports->XeTraceFPR            = XeTraceFPR;
  global_exports->XeTraceVR             = XeTraceVR;
  global_exports->XeTraceInstruction    = XeTraceInstruction;
}
