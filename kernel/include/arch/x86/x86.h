/**
 * \file
 * \brief X86 inline asm utilities and defines
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef KERNEL_X86_H
#define KERNEL_X86_H

/***** MSRs *****/

#define MSR_IA32_EFER   0xc0000080      ///< Extended features enables
#define MSR_IA32_STAR   0xc0000081      ///< System call segment selectors MSR
#define MSR_IA32_LSTAR  0xc0000082      ///< System call target address MSR
#define MSR_IA32_FMASK  0xc0000084      ///< System call flag mask MSR
#define MSR_IA32_FSBASE 0xc0000100      ///< 64-bit FS base register
#define MSR_IA32_GSBASE 0xc0000101      ///< 64-bit GS base register
#define MSR_AMD_HWCR    0xc0010015      ///< AMD hardware configuration
#define MSR_AMD_VMCR    0xc0010114      ///< Global aspects of SVM
#define MSR_AMD_VM_HSAVE 0xc0010117     ///< Physical address of host save area

/*** IA32_EFER flags ***/

#define IA32_EFER_SCE   (1 << 0)        ///< Fast system call enable
#define IA32_EFER_LME   (1 << 8)        ///< Long mode enable
#define IA32_EFER_LMA   (1 << 10)       ///< Long mode active
#define IA32_EFER_NXE   (1 << 11)       ///< No execute enable
#define IA32_EFER_SVME  (1 << 12)       ///< Switch to enable/disable SVM

/*** AMD_HWCR flags ***/

#define AMD_HWCR_FFDIS  (1 << 6)        ///< TLB flush filter

/*** AMD_VMCR flags ***/
#define AMD_VMCR_SVMDIS (1 << 4)        ///< SVM disabled indicator

#ifndef __ASSEMBLER__

static inline uint8_t inb(uint16_t port)
{
    uint8_t  data;
    __asm __volatile("inb %%dx,%0" : "=a" (data) : "d" (port));
    return data;
}

static inline void outb(uint16_t port, uint8_t data)
{
    __asm __volatile("outb %0,%%dx" : : "a" (data), "d" (port));
}

static inline uint32_t ind(uint16_t port)
{
    uint32_t  data;
    __asm __volatile("in %%dx,%0" : "=a" (data) : "d" (port));
    return data;
}

static inline void outd(uint16_t port, uint32_t data)
{
    __asm __volatile("out %0,%%dx" : : "a" (data), "d" (port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t  data;
    __asm __volatile("inw %%dx, %0" : "=a" (data) : "d" (port));
    return data;
}

static inline void outw(uint16_t port, uint16_t data)
{
    __asm __volatile("outw %0,%%dx" : : "a" (data), "d" (port));
}

/** \brief This function reads a model specific register */
static inline uint64_t rdmsr(uint32_t msr_number)
{
    uint32_t eax, edx;
    __asm volatile ("rdmsr" : "=a" (eax), "=d" (edx) : "c" (msr_number));
    return ((uint64_t)edx << 32) | eax;
}

/** \brief this function writes a model specific register */
static inline void wrmsr(uint32_t msr_number, uint64_t value)
{
    uint32_t eax, edx;

    eax = value & 0xffffffff;
    edx = value >> 32;
    __asm__ __volatile__ ("wrmsr" : : "a" (eax), "d" (edx), "c" (msr_number));
}

/** \brief Add bitmask to model specific register */
static inline void addmsr(uint32_t msr_number, uint64_t mask)
{
    wrmsr(msr_number, rdmsr(msr_number) | mask);
}

/** \brief Triggers a hardware breakpoint exception */
static inline void hw_breakpoint(void)
{
    __asm__ __volatile__("int $3" ::);
}

/** \brief Issue WBINVD instruction, invalidating all caches */
static inline void wbinvd(void)
{
    __asm volatile("wbinvd" ::: "memory");
}

static inline void monitor(lvaddr_t base, uint32_t extensions, uint32_t hints)
{
    __asm volatile("monitor"
                   : // No output
                   :
                   "a" (base),
                   "c" (extensions),
                   "d" (hints)
                   );
}

static inline void mwait(uint32_t hints, uint32_t extensions)
{
    __asm volatile("mwait"
                   : // No output
                   :
                   "a" (hints),
                   "c" (extensions)
                   );
}

static inline void clts(void)
{
    __asm volatile("clts");
}

#endif //__ASSEMBLER__

/*** Test whether real processor or M5 ***/

#define CPU_IS_M5_SIMULATOR                                        \
  ({uint32_t _eax, _ebx, _ecx, _edx;                               \
    cpuid(0,&_eax,&_ebx,&_ecx,&_edx);                              \
    /* Expect "M5 Simulator"      */                               \
    /* 5320354d 6c756d69 726f7461 */                               \
    ((_ebx==0x5320354d && _edx==0x6c756d69 && _ecx==0x726f7461));})  

#endif
