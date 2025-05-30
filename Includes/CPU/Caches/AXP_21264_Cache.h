/*
 * Copyright (C) Jonathan D. Belanger 2017.
 * All Rights Reserved.
 *
 * This software is furnished under a license and may be used and copied only
 * in accordance with the terms of such license and with the inclusion of the
 * above copyright notice.  This software or any other copies thereof may not
 * be provided or otherwise made available to any other person.  No title to
 * and ownership of the software is hereby transferred.
 *
 * The information in this software is subject to change without notice and
 * should not be construed as a commitment by the author or co-authors.
 *
 * The author and any co-authors assume no responsibility for the use or
 * reliability of this software.
 *
 * Description:
 *
 *	This header file contains the definitions to implemented the ITB, DTB,
 *	Icache and Dcache.
 *
 * Revision History:
 *
 *	V01.000		29-Jul-2017	Jonathan D. Belanger
 *	Initially written.
 */
#ifndef _AXP_21264_CACHE_DEFS_
#define _AXP_21264_CACHE_DEFS_

#include "CommonUtilities/AXP_Utility.h"
#include "CPU/AXP_21264_CPU.h"
#include "CPU/Ibox/AXP_21264_Ibox_InstructionInfo.h"

/*
 * Cache Prototypes
 */
AXP_21264_TLB *AXP_findTLBEntry(AXP_21264_CPU *, u64, bool);
AXP_21264_TLB *AXP_getNextFreeTLB(AXP_21264_TLB *, u32 *);
void AXP_addTLBEntry(AXP_21264_CPU *, u64, u64, bool);
void AXP_tbia(AXP_21264_CPU *, bool);
void AXP_tbiap(AXP_21264_CPU *, bool);
void AXP_tbis(AXP_21264_CPU *, u64, bool);
AXP_EXCEPTIONS AXP_21264_checkMemoryAccess(
    AXP_21264_CPU *,
    AXP_21264_TLB *,
    AXP_21264_ACCESS);
u64 AXP_va2pa(
    AXP_21264_CPU *,
    u64,
    AXP_PC,
    bool,
    AXP_21264_ACCESS,
    bool *,
    u32 *,
    AXP_EXCEPTIONS *);
AXP_EXCEPTIONS AXP_Dcache_Status(
    AXP_21264_CPU *,
    u64,
    u64,
    u32,
    bool,
    u32 *,
    AXP_DCACHE_LOC *,
    bool);
bool AXP_DcacheWrite(AXP_21264_CPU *, AXP_DCACHE_LOC *, u32, void *, u8);
void AXP_CopyBcacheToDcache(AXP_21264_CPU *, AXP_DCACHE_LOC *, u64);
void AXP_DcacheFlush(AXP_21264_CPU *);
void AXP_DcacheEvict(AXP_21264_CPU *, u64, AXP_PC);
bool AXP_DcacheRead(AXP_21264_CPU *, u64, u64, u32, void *, AXP_DCACHE_LOC *);
void AXP_Dcache_Lock(AXP_21264_CPU *, u64, u64);
void AXP_IcacheAdd(AXP_21264_CPU *, AXP_PC, u32 *, AXP_21264_TLB *);
void AXP_IcacheFlush(AXP_21264_CPU *, bool);
bool AXP_IcacheFetch(AXP_21264_CPU *, AXP_PC, AXP_INS_LINE *);
bool AXP_IcacheValid(AXP_21264_CPU *, AXP_PC);

#endif /* _AXP_21264_CACHE_DEFS_ */
