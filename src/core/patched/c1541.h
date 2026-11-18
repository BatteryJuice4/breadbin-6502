#pragma once
/*#
    # c1541.h

    BREADBIN PATCH: altered copy of chips `systems/c1541.h` @ 7ef8776, which was an
    empty stub (a 6502 with RAM and ROM; no VIAs wired, no disk). Everything below
    the original license header is Breadbin-6502 code (AGPL-3.0, see LICENSE):

    - the 1541's address decoding: 2 KB RAM at $0000 (mirrored every 8 KB below
      $8000), VIA1 at $1800, VIA2 at $1C00 (each mirrored over 1 KB), 16 KB DOS ROM at
      $C000 (mirrored at $8000);
    - VIA1 on the serial bus: PB0 DATA in, PB1 DATA out, PB2 CLK in, PB3 CLK out,
      PB4 ATN acknowledge, PB5/PB6 device number jumpers (both 0 = device 8), PB7 and
      CA1 ATN in. The hardware ATN answer is included: the drive pulls DATA low
      whenever ATN-in differs from ATNA (an XOR gate), without any code running;
    - VIA2 on the mechanism: PB0/PB1 stepper phase, PB2 motor, PB3 LED, PB4 write
      protect sense (0 = protected), PB5/PB6 density (ignored: see below), PB7 SYNC
      (0 = in a sync mark), PA the GCR byte read or to be written, CA1 BYTE READY,
      CA2 byte-ready enable (SOE), CB2 read (1) / write (0) mode. BYTE READY also
      sets the CPU's overflow flag (its SO pin), which the DOS polls with BVC;
    - the disk: a GCR track ring under the head, read and written bit by bit. A track
      of N bytes passes once per revolution (200000 cycles at 300 rpm), so each bit
      takes 200000 / (8 N) cycles: the speed zones of a normal disk and odd .g64 track
      lengths both play back at their recorded density. 10 or more 1 bits make a SYNC,
      which holds the bit counter at 0; the byte after it starts at the first 0 bit.

    chips' m6522 is used as is; its CA2/CB1/CB2 input edge detection has small slips,
    but the 1541 only uses CA1 as an input (ATN on VIA1, BYTE READY on VIA2).

    Not emulated: the VIA shift registers (unused by the DOS), flux noise on empty
    tracks (they read as zeros), the bit-shifting effects of mismatched density.

    ## zlib/libpng license (of the original chips file)

    Copyright (c) 2019 Andre Weissflog
    This software is provided 'as-is', without any express or implied warranty.
    In no event will the authors be held liable for any damages arising from the
    use of this software.
    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:
        1. The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software in a
        product, an acknowledgment in the product documentation would be
        appreciated but is not required.
        2. Altered source versions must be plainly marked as such, and must not
        be misrepresented as being the original software.
        3. This notice may not be removed or altered from any source
        distribution.
#*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// IEC port bits, same as C64_IECPORT_*
#define C1541_IECPORT_RESET (1<<0)
#define C1541_IECPORT_SRQIN (1<<1)
#define C1541_IECPORT_DATA  (1<<2)
#define C1541_IECPORT_CLK   (1<<3)
#define C1541_IECPORT_ATN   (1<<4)

#define C1541_FREQUENCY (1000000)

#define C1541_HALFTRACKS 84
#define C1541_REVOLUTION_CYCLES 200000u     // 300 rpm at 1 MHz

// config params for c1541_init()
typedef struct {
    // pointer to a shared byte with IEC serial bus line state (unused, kept for
    // c64.h; the bus lines live in c1541_t, see c64.h)
    uint8_t* iec_port;
    // rom images
    struct {
        chips_range_t c000_dfff;
        chips_range_t e000_ffff;
    } roms;
} c1541_desc_t;

// BREADBIN PATCH: the disk the drive works on (fields of the caller's core_disk_t).
typedef struct {
    uint8_t** track;            // [C1541_HALFTRACKS]
    uint16_t* size;             // [C1541_HALFTRACKS]
    bool* write_protected;
    bool* dirty;
} c1541_disk_t;

// 1541 emulator state
typedef struct {
    uint64_t pins;
    uint8_t* iec;
    m6502_t cpu;
    m6522_t via_1;
    m6522_t via_2;
    bool valid;
    uint8_t ram[0x0800];
    uint8_t rom[0x4000];
    // BREADBIN PATCH: serial bus. The C64 side is set by c64.h before each tick
    // (true = the C64 pulls that line low); the drive's own outputs follow.
    bool c64_atn, c64_clk, c64_data;
    bool out_clk, out_data, atna;
    // BREADBIN PATCH: mechanism and disk
    c1541_disk_t disk;          // track == NULL: no disk
    int halftrack;              // 0..83 (0 = track 1)
    uint8_t phase;              // last stepper phase (VIA2 PB0/PB1)
    bool motor, led;
    uint32_t bitpos;            // next bit on the current track
    uint32_t bit_acc;           // rotation accumulator (see header comment)
    uint32_t ones;              // 1 bits in a row (SYNC detection)
    uint8_t bitcount;           // bits of the current byte (0..7)
    uint8_t read_shift;         // last 8 bits read
    uint8_t read_latch;         // last full byte (VIA2 PA input)
    uint8_t write_shift;        // byte being written
    uint8_t byte_ready;         // cycles left of the BYTE READY pulse
    uint32_t disk_change;       // cycles left of the write-protect blink after a disk change
} c1541_t;

// initialize a new c1541_t instance
void c1541_init(c1541_t* sys, const c1541_desc_t* desc);
// discard a c1541_t instance
void c1541_discard(c1541_t* sys);
// reset a c1541_t instance
void c1541_reset(c1541_t* sys);
// tick a c1541_t instance forward
void c1541_tick(c1541_t* sys);
// insert a disc image file (.d64): BREADBIN PATCH, takes a GCR disk instead
void c1541_insert_disc(c1541_t* sys, c1541_disk_t disk);
// remove current disc
void c1541_remove_disc(c1541_t* sys);
// prepare a c1541_t snapshot for saving
void c1541_snapshot_onsave(c1541_t* snapshot, void* base);
// prepare a c1541_t snapshot for loading
void c1541_snapshot_onload(c1541_t* snapshot, c1541_t* sys, void* base);

// BREADBIN PATCH: serial bus lines as seen on the cable (true = pulled low).
static inline bool c1541_bus_clk(const c1541_t* sys) { return sys->c64_clk || sys->out_clk; }
static inline bool c1541_bus_data(const c1541_t* sys) {
    return sys->c64_data || sys->out_data || (sys->c64_atn != sys->atna);
}

#ifdef __cplusplus
} // extern "C"
#endif

/*-- IMPLEMENTATION ----------------------------------------------------------*/
#ifdef CHIPS_IMPL
#include <string.h>
#ifndef CHIPS_ASSERT
    #include <assert.h>
    #define CHIPS_ASSERT(c) assert(c)
#endif

void c1541_init(c1541_t* sys, const c1541_desc_t* desc) {
    CHIPS_ASSERT(sys && desc);
    memset(sys, 0, sizeof(c1541_t));
    sys->valid = true;
    sys->iec = desc->iec_port;
    // copy ROM images
    CHIPS_ASSERT(desc->roms.c000_dfff.ptr && (0x2000 == desc->roms.c000_dfff.size));
    CHIPS_ASSERT(desc->roms.e000_ffff.ptr && (0x2000 == desc->roms.e000_ffff.size));
    memcpy(&sys->rom[0x0000], desc->roms.c000_dfff.ptr, 0x2000);
    memcpy(&sys->rom[0x2000], desc->roms.e000_ffff.ptr, 0x2000);
    // initialize the hardware
    m6502_desc_t cpu_desc;
    memset(&cpu_desc, 0, sizeof(cpu_desc));
    sys->pins = m6502_init(&sys->cpu, &cpu_desc);
    m6522_init(&sys->via_1);
    m6522_init(&sys->via_2);
    sys->halftrack = 34;        // track 18, where the head usually rests
}

void c1541_discard(c1541_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    sys->valid = false;
}

void c1541_reset(c1541_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    sys->pins |= M6502_RES;
    m6522_reset(&sys->via_1);
    m6522_reset(&sys->via_2);
}

// BREADBIN PATCH: the disk mechanism ------------------------------------------------
static inline uint32_t _c1541_track_bits(const c1541_t* sys, int ht) {
    if (sys->disk.track && sys->disk.track[ht]) {
        return (uint32_t)sys->disk.size[ht] * 8u;
    }
    // Nothing recorded here: keep the timing of a normal track in this zone.
    static const uint16_t zone_bytes[4] = { 6250, 6666, 7142, 7692 };
    const int t = ht / 2 + 1;
    return 8u * zone_bytes[t <= 17 ? 3 : t <= 24 ? 2 : t <= 30 ? 1 : 0];
}

static void _c1541_step(c1541_t* sys, uint8_t phase) {
    if (phase == sys->phase) return;
    int ht = sys->halftrack;
    if (phase == ((sys->phase + 1) & 3)) {
        ht++;
    } else if (phase == ((sys->phase + 3) & 3)) {
        ht--;
    }
    sys->phase = phase;
    if (ht < 0) ht = 0;                     // the head bumps against its stop
    if (ht > C1541_HALFTRACKS - 1) ht = C1541_HALFTRACKS - 1;
    if (ht != sys->halftrack) {
        // Keep the angle: same fraction of a revolution on the new track.
        const uint32_t old_bits = _c1541_track_bits(sys, sys->halftrack);
        const uint32_t new_bits = _c1541_track_bits(sys, ht);
        sys->bitpos = (uint32_t)(((uint64_t)sys->bitpos * new_bits) / old_bits) % new_bits;
        sys->halftrack = ht;
    }
}

// One bit cell passes under the head.
static void _c1541_bit(c1541_t* sys, bool write_mode, bool soe) {
    uint8_t* data = (sys->disk.track) ? sys->disk.track[sys->halftrack] : NULL;
    const uint32_t bits = _c1541_track_bits(sys, sys->halftrack);
    const uint32_t pos = sys->bitpos;
    sys->bitpos = (pos + 1 < bits) ? pos + 1 : 0;
    const uint8_t mask = (uint8_t)(0x80 >> (pos & 7));
    if (write_mode) {
        const bool bit = (sys->write_shift & 0x80) != 0;
        sys->write_shift = (uint8_t)(sys->write_shift << 1);
        if (data && !*sys->disk.write_protected) {
            uint8_t* p = &data[pos >> 3];
            const uint8_t v = bit ? (uint8_t)(*p | mask) : (uint8_t)(*p & ~mask);
            if (v != *p) {
                *p = v;
                *sys->disk.dirty = true;
            }
        }
        sys->ones = 0;
        if (++sys->bitcount == 8) {
            sys->bitcount = 0;
            sys->write_shift = sys->via_2.pa.outr;  // next byte from VIA2 port A
            if (soe) {
                sys->cpu.P |= M6502_VF;
                sys->byte_ready = 2;
            }
        }
        return;
    }
    const bool bit = data && (data[pos >> 3] & mask);
    sys->read_shift = (uint8_t)((sys->read_shift << 1) | (bit ? 1 : 0));
    if (bit) {
        sys->ones++;
    } else {
        sys->ones = 0;
    }
    if (sys->ones >= 10) {
        sys->bitcount = 0;                  // in a SYNC: no bytes
        return;
    }
    if (++sys->bitcount == 8) {
        sys->bitcount = 0;
        sys->read_latch = sys->read_shift;
        sys->write_shift = sys->via_2.pa.outr;
        if (soe) {
            sys->cpu.P |= M6502_VF;
            sys->byte_ready = 2;
        }
    }
}

void c1541_tick(c1541_t* sys) {
    uint64_t pins = sys->pins;
    pins = m6502_tick(&sys->cpu, pins);
    const uint16_t addr = M6502_GET_ADDR(pins);

    // Address decoding (BREADBIN PATCH)
    uint64_t via1_pins = pins & M6502_PIN_MASK;
    uint64_t via2_pins = pins & M6502_PIN_MASK;
    if (addr & 0x8000) {
        if (pins & M6502_RW) {
            M6502_SET_DATA(pins, sys->rom[addr & 0x3FFF]);
        }
    } else {
        const uint16_t a = addr & 0x1FFF;
        if (a < 0x0800) {
            if (pins & M6502_RW) {
                M6502_SET_DATA(pins, sys->ram[a]);
            } else {
                sys->ram[a] = M6502_GET_DATA(pins);
            }
        } else if (a >= 0x1C00) {
            via2_pins |= M6522_CS1;
        } else if (a >= 0x1800) {
            via1_pins |= M6522_CS1;
        } else if (pins & M6502_RW) {
            M6502_SET_DATA(pins, (uint8_t)(addr >> 8));     // open bus
        }
    }

    // VIA1: serial bus
    {
        const bool atn = sys->c64_atn;
        uint8_t pb = 0;
        if (c1541_bus_data(sys)) pb |= 1 << 0;
        if (c1541_bus_clk(sys)) pb |= 1 << 2;
        if (atn) pb |= 1 << 7;
        // PB5/PB6: device number jumpers, both closed (0) = device 8
        M6522_SET_PAB(via1_pins, 0xFF, pb);
        if (atn) via1_pins |= M6522_CA1;
        via1_pins = m6522_tick(&sys->via_1, via1_pins);
        const uint8_t out = sys->via_1.pb.outr & sys->via_1.pb.ddr;
        sys->out_data = (out & (1 << 1)) != 0;
        sys->out_clk = (out & (1 << 3)) != 0;
        sys->atna = (out & (1 << 4)) != 0;
        if (via1_pins & M6502_IRQ) {
            pins |= M6502_IRQ;
        } else {
            pins &= ~M6502_IRQ;
        }
        if ((via1_pins & (M6522_CS1|M6522_RW)) == (M6522_CS1|M6522_RW)) {
            pins = M6502_COPY_DATA(pins, via1_pins);
        }
    }

    // VIA2: mechanism
    {
        uint8_t pb = 0;
        const bool have_disk = sys->disk.track != NULL;
        const bool wp = have_disk && *sys->disk.write_protected;
        // Write protect sense: 0 = protected; blinks while a disk goes in or out.
        if (!wp && !(sys->disk_change > 0 && (sys->disk_change / 50000) % 2 == 0)) pb |= 1 << 4;
        const bool write_mode = !sys->via_2.pb.c2_out;
        if (!(sys->ones >= 10 && !write_mode && sys->motor)) pb |= 1 << 7;   // SYNC, active low
        M6522_SET_PAB(via2_pins, sys->read_latch, pb);
        if (sys->byte_ready == 0) via2_pins |= M6522_CA1;   // BYTE READY, active low
        via2_pins = m6522_tick(&sys->via_2, via2_pins);
        const uint8_t out = sys->via_2.pb.outr & sys->via_2.pb.ddr;
        _c1541_step(sys, out & 3);
        sys->motor = (out & (1 << 2)) != 0;
        sys->led = (out & (1 << 3)) != 0;
        if (via2_pins & M6502_IRQ) {
            pins |= M6502_IRQ;
        }
        if ((via2_pins & (M6522_CS1|M6522_RW)) == (M6522_CS1|M6522_RW)) {
            pins = M6502_COPY_DATA(pins, via2_pins);
        }
        if (sys->byte_ready > 0) sys->byte_ready--;
        if (sys->disk_change > 0) sys->disk_change--;
        if (sys->motor) {
            const bool soe = sys->via_2.pa.c2_out;
            sys->bit_acc += _c1541_track_bits(sys, sys->halftrack);
            while (sys->bit_acc >= C1541_REVOLUTION_CYCLES) {
                sys->bit_acc -= C1541_REVOLUTION_CYCLES;
                _c1541_bit(sys, write_mode, soe);
            }
        }
    }
    sys->pins = pins;
}

void c1541_insert_disc(c1541_t* sys, c1541_disk_t disk) {
    sys->disk = disk;
    sys->bitpos = 0;
    sys->ones = 0;
    sys->disk_change = 400000;      // ~0.4 s of write-protect blinking
}

void c1541_remove_disc(c1541_t* sys) {
    memset(&sys->disk, 0, sizeof(sys->disk));
    sys->ones = 0;
    sys->disk_change = 400000;
}

void c1541_snapshot_onsave(c1541_t* snapshot, void* base) {
    CHIPS_ASSERT(snapshot && base);
    snapshot->iec = 0;
    memset(&snapshot->disk, 0, sizeof(snapshot->disk));     // BREADBIN PATCH
    m6502_snapshot_onsave(&snapshot->cpu);
    (void)base;
}

void c1541_snapshot_onload(c1541_t* snapshot, c1541_t* sys, void* base) {
    CHIPS_ASSERT(snapshot && sys && base);
    snapshot->iec = sys->iec;
    snapshot->disk = sys->disk;                             // BREADBIN PATCH
    m6502_snapshot_onload(&snapshot->cpu, &sys->cpu);
    (void)base;
}
#endif // CHIPS_IMPL
