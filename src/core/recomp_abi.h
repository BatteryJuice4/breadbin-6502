#pragma once
/*
    breadbin-recomp ABI: the contract between generated code and the machine
    that runs it (Breadbin-6502's core, or a test harness).

    The launcher keeps an identical copy (src/core/recomp_abi.h in breadbin-6502).
    The struct tag bb_rc_program is part of the contract (core.h forward-declares it).
    Change both together and bump BB_RC_ABI_VERSION; the launcher refuses a
    program built for another version.

    Model (docs/codegen.md): generated code runs whole instructions. Each bus
    cycle goes through bb_rc_cycle(), which ticks the real chips, so timing,
    RDY stalls and interrupts behave exactly as with the interpreter. A
    generated function is entered right after its opcode fetch (the SYNC cycle
    is done, pins hold the opcode) and returns right after the next opcode
    fetch. The machine decides between instructions whether to call the next
    function or hand over to the interpreter (no code, cart not mapped,
    interrupt due, frame about to end).

    Pin layout: chips' m6502 (address A0-A15 = bits 0-15, data = bits 16-23).
*/
#include <stdint.h>

#define BB_RC_ABI_VERSION 1

#define BB_RC_RW    (1ULL << 24)    // 1 = read
#define BB_RC_SYNC  (1ULL << 25)    // opcode fetch

#define BB_RC_CF (1 << 0)
#define BB_RC_ZF (1 << 1)
#define BB_RC_IF (1 << 2)
#define BB_RC_DF (1 << 3)
#define BB_RC_BF (1 << 4)
#define BB_RC_XF (1 << 5)
#define BB_RC_VF (1 << 6)
#define BB_RC_NF (1 << 7)

typedef struct {
    uint8_t A, X, Y, S, P;
    uint8_t bcd;                // decimal mode works (always 1 on a 6510)
} bb_rc_cpu;

typedef struct bb_rc {
    bb_rc_cpu cpu;              // P is live: the machine reads the I flag every cycle
    uint64_t pins;              // pins after the last bus cycle
    void* machine;              // the machine's own state
} bb_rc;

// ---- provided by the machine ------------------------------------------------
// One CPU bus cycle with the given output pins; returns the pins after it
// (data filled in for reads), after any RDY stall has passed.
uint64_t bb_rc_cycle(bb_rc* r, uint64_t pins);
// Taken branch without page crossing: interrupts are recognized one
// instruction later (the 6502 quirk the interpreter models).
void bb_rc_branch_delay(bb_rc* r);

// ---- provided by the generated code -----------------------------------------
typedef void (*bb_rc_fn)(bb_rc* r);

typedef struct bb_rc_program {
    uint32_t abi;               // BB_RC_ABI_VERSION
    const char* sha1;           // ROM the code was generated from
    const char* generator;      // "breadbin-recomp x.y.z"
    uint32_t count;
    const uint16_t* addr;       // ascending
    const uint8_t* opcode;      // opcode at addr (must match the fetch)
    const bb_rc_fn* fn;
} bb_rc_program;
