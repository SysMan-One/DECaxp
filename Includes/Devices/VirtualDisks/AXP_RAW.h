/*
 * Copyright (C) Jonathan D. Belanger 2018.
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
 *  This header file contains the definitions required to support accessing
 *  either a device (disk) or CD in its raw form.  We do this for devices
 *  because we want those devices to ultimately look and be formatted just like
 *  the real thing.  We do this for CDs, because the format for these has been
 *  predetermined and is a standard, so let the operating system in the
 *  emulator handle the details for both of these disk types.
 *
 * Revision History:
 *
 *  V01.000	15-Jul-2018	Jonathan D. Belanger
 *  Initially written.
 */
#ifndef AXP_RAW_H_
#define AXP_RAW_H_
#include "CommonUtilities/AXP_Utility.h"
#include "CommonUtilities/AXP_Configure.h"
#include "CommonUtilities/AXP_Trace.h"
#include "Devices/VirtualDisks/AXP_VirtualDisk.h"

#ifndef BLKROGET
#define BLKROGET	_IO(0x12,94)	/* get read-only status */
#endif
#ifndef BLKSSZGET
#define BLKSSZGET	_IO(0x12,104)	/* get sector size */
#endif
#ifndef BLKBSZGET
#define BLKBSZGET	_IOR(0x12,112,size_t) /* get logical block size */
#endif
#ifndef BLKPBSZGET
#define BLKPBSZGET	_IO(0x12,123)	/* get physical block size */
#endif
#ifndef BLKGETSIZE64
#define BLKGETSIZE64	_IOR(0x12,114,size_t) /* return device size in bytes */
#endif

/*
 * The below structure is used to maintain information about accessing a disk
 * in RAW format.  The disk can be either an entire hard drive, CDROM, or ISO
 * file.
 */
typedef struct
{

    /*
     * These are parameters provided by the interface and stored for later
     * usage.
     */
    u32			deviceID;
    char		*filePath;
    bool		readOnly;

    /*
     * This is the file descriptor to the device.
     */
    int			fd;

    /*
     * These are things read from (or written to) the VHD file that are used
     * while accessing the contents.
     */
    u64			diskSize;
    u32 		blkSize;
    u32			sectorSize;
    u32			cylinders;
    u32			heads;
    u32			sectors;
} AXP_RAW_Handle;

u32 _AXP_RAW_Open(char *, AXP_VHD_OPEN_FLAG, u32, AXP_VHD_HANDLE *);

#endif /* AXP_RAW_H_ */
