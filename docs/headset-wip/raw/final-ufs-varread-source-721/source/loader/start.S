/*
 * mtkloader entry. LK starts this as a Linux arm64 Image: at 0x40080000, MMU and caches off,
 * x0 = LK's device tree. All memory is device memory until XNU turns its MMU on, so the C code
 * is built without SIMD and with strict alignment (build.sh).
 */
.global _start
.global jump_to_kernel
.global chain_stub
.global chain_stub_end
.global xnu_return_stub
.global psci_system_reset
.global psci_cpu_on
.global watcher_entry
.global mmu_on
.global dcache_clean_range
.global irq_wait
.global mtk_runtime_start
.global mtk_runtime_linked

.section .data.boot
.balign 8
mtk_runtime_start:
    .quad   0
mtk_runtime_linked:
    .quad   0

.section .text.boot

_start:
    /* Linux ARM64 Image header (64 bytes); tools/chainboot.py fills image_size, res2, res3 */
    b       primary_entry       /* code0 */
    .word   0                   /* code1 */
    .quad   0                   /* text_offset: LK here ignores it; see link.ld */
    .quad   0x02000000          /* image_size */
    .quad   0x0a                /* flags: little-endian, 4K pages, anywhere */
    .quad   0                   /* res2: length of Android's Image (at CHAIN_OFFSET) */
    .quad   0                   /* res3: offset of the XNU payload ("XNUP") from _start */
    .quad   0                   /* res4 */
    .ascii  "ARM\x64"
    .word   0

primary_entry:
    msr     daifset, #0xf
    mov     x19, x0

    /*
     * LK does not have to put this where the link map says, and on this board it does not: the
     * Image header read back as something other than start.S writes it, and the payload search
     * only found the payload by sweeping far enough to reach the real data from a wrong base.
     * So measure where we are (adr is PC-relative, the truth) against where the linker put us,
     * and if they differ, move the whole program to the address every symbol in it assumes.
     * After that one move, absolute addresses are right and nothing else has to know.
     */
#ifndef BOARD_NO_RELOC
    adr     x10, _start
    adrp    x11, _start
    add     x11, x11, :lo12:_start
    /*
     * Keep the load address in a register, not in memory: before the move, adrp names the
     * link-time address, so a store there lands in a slot the copy is about to overwrite with
     * clean source bytes -- the record would silently vanish.  x9 is untouched by the copy.
     */
    mov     x9, x10
    cmp     x10, x11
    b.eq    8f

    adrp    x12, __bss_start
    add     x12, x12, :lo12:__bss_start
    sub     x12, x12, x11               /* file-backed length: linked bss_start - linked start */
    add     x12, x12, #15
    and     x12, x12, #~15

    cmp     x11, x10
    b.lo    5f                          /* destination below source: copy forwards */

    /* destination above source: copy backwards, so a tail is read before it is overwritten */
    add     x13, x10, x12
    add     x14, x11, x12
4:  ldp     x15, x16, [x13, #-16]!
    stp     x15, x16, [x14, #-16]!
    subs    x12, x12, #16
    b.gt    4b
    b       7f

5:  mov     x13, x10
    mov     x14, x11
    mov     x15, x12
3:  ldp     x16, x17, [x13], #16
    stp     x16, x17, [x14], #16
    subs    x15, x15, #16
    b.gt    3b

7:  dsb     sy
    ic      iallu
    dsb     sy
    isb
    /*
     * Continue at the matching point in the copy, not at primary_entry: re-entering at the top
     * would run "mov x9, x10" again, and by then adr and adrp both mean the new address, so the
     * load address we measured -- the one fact only this first pass has -- would be overwritten
     * with the address we just moved to.
     */
    adrp    x13, 8f
    add     x13, x13, :lo12:8f
    br      x13
#endif

    .balign 8
/*
 * The one address only the running code can know: adr is PC-relative, so whatever this returns is
 * where the program actually is, independent of what the link map or LK claims.  C cannot express
 * "the address of my own pc" portably, so the loader asks here.
 */
.global mtk_here
mtk_here:
    adr     x0, _start
    ret

8:
    /*
     * Now that the code is where the link map says (moved, or never needed moving), adrp is the
     * truth too, so these stores land where the C code will read them.  x9 still holds the
     * address LK actually chose.
     */
    adr     x10, _start
    adrp    x11, _start
    add     x11, x11, :lo12:_start
    adrp    x12, mtk_runtime_start
    add     x12, x12, :lo12:mtk_runtime_start
    str     x9, [x12]                   /* loaded here */
    add     x12, x12, #8
    str     x11, [x12]                  /* link map says here */

    mrs     x1, CurrentEL
    lsr     x1, x1, #2
    adrp    x2, mtk_vectors
    add     x2, x2, :lo12:mtk_vectors
    cmp     x1, #2
    b.ne    10f
    msr     vbar_el2, x2
    b       20f
10: msr     vbar_el1, x2
20:
    /*
     * Stack above the loader, inside the gap before the Android Image at CHAIN_OFFSET (0x40000),
     * and BSS cleared.  Every stack in this file is a band above _start for the same reason: the
     * link address is 0x40000000 and System RAM on this board begins there, so below the loader
     * there is nothing at all to grow into.  The bands do not overlap; see watcher_entry.
     */
    adr     x0, _start
    add     x0, x0, #0x2f000
    mov     sp, x0
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
1:  cmp     x0, x1
    b.ge    2f
    str     xzr, [x0], #8
    b       1b
2:
    mov     x0, x19
    bl      mtkloader_main
3:  wfe
    b       3b

/* jump_to_kernel(entry_pa, boot_args_pa): enter XNU at EL1h, MMU and caches off */
jump_to_kernel:
    msr     daifset, #0xf
    mov     x16, x0
    mov     x0, x1
    mrs     x2, CurrentEL
    lsr     x2, x2, #2
    cmp     x2, #2
    b.ne    1f
    mov     x2, #(1 << 31)      /* HCR_EL2.RW: EL1 is AArch64 */
    msr     hcr_el2, x2
    mov     x2, #0x3c5          /* EL1h, DAIF masked */
    msr     spsr_el2, x2
    msr     elr_el2, x16
    eret
1:  mrs     x2, sctlr_el1
    bic     x2, x2, #0x1
    bic     x2, x2, #0x4
    bic     x2, x2, #0x1000
    msr     sctlr_el1, x2
    isb
    ic      iallu
    dsb     sy
    isb
    br      x16

/*
 * chain_stub(dtb, dst, src, len, entry): copy Android's Image down over this program and enter
 * it as LK would have. C copies the stub out of the way first. Position independent.
 *
 * The copy overlaps: the source is the Image at _start + CHAIN_OFFSET and the destination is
 * KERNEL_PA, with this program's own RAM in between. Which way it overlaps depends on where the
 * program was linked, and a forward loop is only safe while the destination sits below the
 * source. With the program linked at 0x40000000 -- where LK actually puts it, see link.ld -- the
 * destination (0x40080000) is 0x40000 above the source (0x40040000), so a forward loop would
 * overwrite the bytes it has not read yet and hand Android an image that is only correct in its
 * first 256 KiB. Copy back to front in that case.
 */
chain_stub:
    /*
     * chain_stub(dtb, dst, src, len, entry): copy the kernel Image down over this program and
     * enter it as LK would have. Two things make this less trivial than it looks.
     *
     * The copy overlaps: dst is inside the source range, so the direction has to depend on which
     * side of dst the source is on. A forward copy when dst > src destroys the bytes it has not
     * read yet; that was the bug that left the phone on a dead screen, because chain_android
     * copies from _start + CHAIN_OFFSET down to KERNEL_PA and every byte of the kernel after the
     * first 256 KiB was being overwritten before it was read.
     *
     * And len is not a multiple of eight (42201364 here), so the trailing bytes have to move one
     * at a time. They may not be picked up with a wide load that straddles the boundary: the
     * loader runs with the MMU off, where every access is Device memory and an unaligned one
     * faults, which is a hang rather than a wrong answer.
     */
    lsr     x7, x3, #3
    lsl     x7, x7, #3              /* x7 = len rounded down to a doubleword */
    cmp     x1, x2
    b.ls    2f                      /* dst <= src: a forward copy is safe */
    add     x6, x2, x3
    cmp     x1, x6
    b.hs    2f                      /* dst >= src + len: the two do not overlap */

    /* backward: the tail sits at the highest addresses, so it goes first */
    mov     x8, x7
1:  cmp     x8, x3
    b.hs    3f
    ldrb    w6, [x2, x8]
    strb    w6, [x1, x8]
    add     x8, x8, #1
    b       1b
3:  cbz     x7, 5f
    add     x8, x2, x7              /* x8 walks down from src + whole */
4:  sub     x8, x8, #8
    ldr     x6, [x8]
    add     x9, x1, x8
    sub     x9, x9, x2              /* the same offset in the destination */
    str     x6, [x9]
    cmp     x8, x2
    b.hi    4b
    b       5f

2:  /* forward */
    mov     x8, xzr
6:  cmp     x8, x7
    b.hs    7f
    ldr     x6, [x2, x8]
    str     x6, [x1, x8]
    add     x8, x8, #8
    b       6b
7:  mov     x8, x7
8:  cmp     x8, x3
    b.hs    5f
    ldrb    w6, [x2, x8]
    strb    w6, [x1, x8]
    add     x8, x8, #1
    b       8b

5:
    dsb     sy
    ic      iallu
    dsb     sy
    isb
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    br      x4
chain_stub_end:

/*
 * XNU comes back here (loader_stub_slot[1], see xnu/files/osfmk/arm64/loader_return.c): after a
 * panic or an orderly end with the MMU on and this page mapped 1:1, or from its early exception
 * vector with the MMU off. x0 = log PA, x1 = log length, x2 = reason (0 end, 1 early exception,
 * 2 checkpoint), x3 = code (ESR for an exception), x4 = ELR, x5 = FAR.
 */
xnu_return_stub:
    msr     daifset, #0xf
    mov     x19, x0
    mov     x20, x1
    mov     x21, x2
    mov     x22, x3
    mov     x23, x4
    mov     x24, x5
    mov     x25, x29
    mov     x26, x30
    /* What XNU has only in its caches (its stack, its globals) goes to RAM before they go off */
    bl      dcache_clean_all
    mrs     x9, sctlr_el1
    bic     x9, x9, #0x1
    bic     x9, x9, #0x4
    bic     x9, x9, #0x1000
    msr     sctlr_el1, x9
    isb
    ic      iallu
    tlbi    vmalle1
    dsb     sy
    isb
    adrp    x9, mtk_vectors
    add     x9, x9, :lo12:mtk_vectors
    msr     vbar_el1, x9
    adrp    x9, xnu_fp
    add     x9, x9, :lo12:xnu_fp
    str     x25, [x9]            /* XNU's frame pointer and lr: mtkloader.c walks its stack */
    str     x26, [x9, #8]
    mrs     x10, tcr_el1
    str     x10, [x9, #16]
    mrs     x10, ttbr1_el1
    str     x10, [x9, #24]
    adr     x9, _start
    add     sp, x9, #0x37000      /* its own band: below the Image, above mtkloader_main's (see watcher_entry) */
    mov     x0, x19
    mov     x1, x20
    mov     x2, x21
    mov     x3, x22
    mov     x4, x23
    mov     x5, x24
    bl      xnu_returned
1:  wfe
    b       1b

/* Clean and invalidate every data/unified cache level up to the point of coherency, by set/way. */
dcache_clean_all:
    mrs     x0, clidr_el1
    and     w3, w0, #0x07000000
    lsr     w3, w3, #23          /* LoC * 2 */
    cbz     w3, 5f
    mov     w10, #0              /* level * 2 */
1:  add     w2, w10, w10, lsr #1 /* level * 3 */
    lsr     w1, w0, w2
    and     w1, w1, #7
    cmp     w1, #2
    b.lt    4f                   /* no data cache at this level */
    msr     csselr_el1, x10
    isb
    mrs     x1, ccsidr_el1
    and     w2, w1, #7
    add     w2, w2, #4           /* log2(line size) */
    ubfx    w4, w1, #3, #10      /* ways - 1 */
    clz     w5, w4
    ubfx    w7, w1, #13, #15     /* sets - 1 */
2:  mov     w9, w4
3:  lsl     w6, w9, w5
    orr     w11, w10, w6
    lsl     w6, w7, w2
    orr     w11, w11, w6
    dc      cisw, x11
    subs    w9, w9, #1
    b.ge    3b
    subs    w7, w7, #1
    b.ge    2b
4:  add     w10, w10, #2
    cmp     w3, w10
    b.gt    1b
5:  dsb     sy
    isb
    ret

/* PSCI SYSTEM_RESET through ATF, which keeps DRAM refreshed (unlike a bare watchdog reset). */
psci_system_reset:
    ldr     x0, =0x84000009
    smc     #0
    ret

/* psci_cpu_on(mpidr, entry_pa, context): 0 on success */
psci_cpu_on:
    mov     x3, x2
    mov     x2, x1
    mov     x1, x0
    ldr     x0, =0xc4000003
    smc     #0
    ret

/*
 * Second core, started by PSCI CPU_ON while XNU runs on the first: MMU and caches off.
 *
 * Its stack is a band ABOVE _start like every other one here.  It used to be _start - 0x18000,
 * which was RAM while the link address was 0x40080000 and is a hole now that it is 0x40000000:
 * System RAM on this board begins exactly at _start.  The core took an exception on its first
 * push, the exception handler pushed onto a second hole (see fault_common), and the core died
 * with nothing on the screen and no reset -- which is why the watchdog looked alive in the
 * console line "PSCI CPU_ON 0, running" and never once fired.
 *
 * Bands, top down from the Android Image at _start + CHAIN_OFFSET (0x40000); each grows down and
 * is 32 KiB, far more than these frames need (the deepest, watcher_main -> read_screen ->
 * save_log, is under 1 KiB).  The loader itself ends at _start + 0x15f20.
 *   fault_common     0x4003f000
 *   xnu_return_stub  0x40037000
 *   mtkloader_main   0x4002f000
 *   watcher_entry    0x40027000
 */
watcher_entry:
    msr     daifset, #0xf
    /*
     * Both vectors, not just EL1's.  PSCI CPU_ON hands the core over in whatever EL the caller
     * was in, and the loader may have entered at EL2 (see the CurrentEL test at _start); a fault
     * taken at EL2 with VBAR_EL2 still holding XNU's or nothing at all reports nowhere useful.
     */
    adrp    x9, mtk_vectors
    add     x9, x9, :lo12:mtk_vectors
    msr     vbar_el1, x9
    msr     vbar_el2, x9
    adr     x9, _start
    add     sp, x9, #0x27000
    bl      watcher_main
1:  wfe
    b       1b

/*
 * mmu_on(l1_table_pa): 4K granule, 39-bit addresses from one level-1 table of 1 GB blocks,
 * MAIR attr 0 device nGnRnE, attr 1 normal write-back. Inner shareable, so this core's
 * cached reads see what the other cores have in their caches.
 */
mmu_on:
    ldr     x1, =0xff00
    msr     mair_el1, x1
    ldr     x1, =0x200803519     /* IPS 40 bit, EPD1, SH0 inner, ORGN0/IRGN0 WB, T0SZ 25 */
    msr     tcr_el1, x1
    msr     ttbr0_el1, x0
    isb
    tlbi    vmalle1
    dsb     sy
    isb
    mrs     x1, sctlr_el1
    orr     x1, x1, #0x1
    orr     x1, x1, #0x4
    orr     x1, x1, #0x1000
    msr     sctlr_el1, x1
    isb
    ret

/* dcache_clean_range(va, len): clean and invalidate to the point of coherency */
dcache_clean_range:
    add     x1, x0, x1
    bic     x0, x0, #63
1:  dc      civac, x0
    add     x0, x0, #64
    cmp     x0, x1
    b.lo    1b
    dsb     sy
    ret

/*
 * irq_wait(ticks): unmask IRQs and wait, so a timer interrupt can be taken; returns the number
 * of interrupts the handler saw. irq_count and irq_intid are C globals (mtkloader.c).
 */
irq_wait:
    mov     x19, x0
    msr     daifclr, #0x3        /* IRQ and FIQ on */
    isb
1:  isb
    mrs     x0, cntpct_el0
    cmp     x0, x19
    b.lo    1b
    msr     daifset, #0xf
    isb
    adrp    x0, irq_count
    add     x0, x0, :lo12:irq_count
    ldr     x0, [x0]
    ret

/* IRQ at EL1: take the interrupt, remember its number, silence both timers, end it. */
irq_entry:
    sub     sp, sp, #0x60
    stp     x0, x1, [sp, #0x00]
    stp     x2, x3, [sp, #0x10]
    stp     x4, x5, [sp, #0x20]
    mrs     x0, ICC_IAR1_EL1
    adrp    x1, irq_count
    add     x1, x1, :lo12:irq_count
    ldr     x2, [x1]
    add     x2, x2, #1
    str     x2, [x1]
    adrp    x1, irq_intid
    add     x1, x1, :lo12:irq_intid
    str     x0, [x1]
    mov     x3, #2               /* IMASK: stop the timer asking */
    msr     cntv_ctl_el0, x3
    msr     cntp_ctl_el0, x3
    isb
    cmp     x0, #1020
    b.hs    1f                   /* 1023 = spurious: nothing to end */
    msr     ICC_EOIR1_EL1, x0
    isb
1:  ldp     x0, x1, [sp, #0x00]
    ldp     x2, x3, [sp, #0x10]
    ldp     x4, x5, [sp, #0x20]
    add     sp, sp, #0x60
    eret

/* Any exception in the loader: ESR/ELR/FAR and the vector offset to C (loader_fault). */
.macro vec off
    .balign 0x80
    mov     x0, #\off
    b       fault_common
.endm

fault_common:
    mrs     x4, CurrentEL
    lsr     x4, x4, #2
    cmp     x4, #2
    b.ne    1f
    mrs     x1, esr_el2
    mrs     x2, elr_el2
    mrs     x3, far_el2
    b       2f
1:  mrs     x1, esr_el1
    mrs     x2, elr_el1
    mrs     x3, far_el1
2:  adr     x5, _start
    add     sp, x5, #0x3f000      /* the top band: a fault must not land on the stack it faulted from */
    bl      loader_fault
3:  wfe
    b       3b

    .balign 0x800
mtk_vectors:
    vec 0x000
    .balign 0x80
    b       irq_entry
    vec 0x100
    vec 0x180
    vec 0x200
    .balign 0x80
    b       irq_entry
    vec 0x300
    vec 0x380
    vec 0x400
    vec 0x480
    vec 0x500
    vec 0x580
    vec 0x600
    vec 0x680
    vec 0x700
    vec 0x780
