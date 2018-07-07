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
 *  This source file contains the code to support the VHDX formatted virtual
 *  disk image.
 *
 * Revision History:
 *
 *  V01.000	03-Jul-2018	Jonathan D. Belanger
 *  Initially written.
 */
#include "AXP_Utility.h"
#include "AXP_Configure.h"
#include "AXP_Blocks.h"
#include "AXP_Trace.h"
#include "AXP_virtdisk.h"
#include "AXP_VHDX.h"

/*
 * Local Prototypes
 */
static void _AXP_VHD_CreateCleanup(AXP_VHDX_Handle *, char *);


/*
 * _AXP_VHD_CreateCleanup
 *  This function is called when an error occurs after having allocated the
 *  VHDX handle and potentially created and opened the file.  This function
 *  will close the file, clear the file pointer, and delete the file off of
 *  the system.
 *
 * Input Parameters:
 *  vhdx:
 *	A pointer to the VHDX Handle we used to manage the virtual hard disk.
 *  path:
 *	A pointer to a string for the file we potentially opened.  If we did so
 *	successfully, then we need to delete it.
 *
 * Output Parameters:
 *  None.
 *
 * Return Values:
 *  None.
 */
static void _AXP_VHD_CreateCleanup(AXP_VHDX_Handle *vhdx, char *path)
{

    /*
     * Clean-up after ourselves
     */
    if (vhdx->fp != NULL)
	fclose(vhdx->fp);	/* Close the file we opened */
    vhdx->fp = NULL;		/* Prevent Deallocate Blocks closing again */
    remove(path);		/* Delete the file */

    /*
     * Return back to the caller.
     */
    return;
}

/*
 * AXP_VHD_Create
 *  Creates a virtual hard disk (VHD) image file, either using default
 *  parameters or using an existing virtual disk or physical disk.
 *
 * Input Parameters:
 *  storageType:
 *	A pointer to a VIRTUAL_STORAGE_TYPE structure that contains the desired
 *	disk type and vendor information.
 *  path:
 *	A pointer to a valid string that represents the path to the new virtual
 *	disk image file.
 *  accessMask:
 *	The AXP_VHD_ACCESS_MASK value to use when opening the newly created
 *	virtual disk file.  If the version member of the param parameter is set
 *	to AXP_VHD_CREATE_VER_2 then only the AXP_VHD_ACCESS_NONE (0) value may
 *	be specified.
 *  securityDsc:
 *	An optional pointer to a AXP_VHD_SECURITY_DSC to apply to the virtual
 *	disk image file.  If this parameter is NULL, the parent directory's
 *	security descriptor will be used.
 *  flags:
 *	Creation flags, which must be a valid combination of the
 *	AXP_VHD_CREATE_FLAG enumeration.
 *  providerSpecFlags
 *	Flags specific to the type of virtual disk being created. May be zero
 *	if none are required.
 *  param:
 *	A pointer to a valid AXP_VHD_CREATE_PARAM structure that contains
 *	creation parameter data.
 *  async:
 *	An optional pointer to a valid AXP_VHD_ASYNC structure if asynchronous
 *	operation is desired.
 *
 * Output Parameters:
 *  handle:
 *  	A pointer to the handle object that represents the newly created
 *  	virtual disk.
 *
 * Return Values:
 *  AXP_VHD_SUCCESS:		Normal Successful Completion.
 *  AXP_VHD_INV_PARAM:		An invalid parameter or combination of
 *				parameters was detected.
 *  AXP_VHD_FILE_EXISTS:	File already exists.
 *  AXP_VHD_INV_HANDLE:		Failed to create the VHDX file.
 *  AXP_VHD_WRITE_FAULT:	An error occurred writing to the VHDX file.
 */
u32 AXP_VHD_Create(
		AXP_VHD_STORAGE_TYPE *storageType,
		char *path,
		AXP_VHD_ACCESS_MASK accessMask,
		AXP_VHD_SEC_DSC *securityDsc,
		AXP_VHD_CREATE_FLAG flags,
		u32 providerSpecFlags,
		AXP_VHD_CREATE_PARAM *param,
		AXP_VHD_ASYNC *async,
		AXP_VHD_HANDLE *handle)
{
    AXP_VHDX_Handle	*vhdx = NULL;
    char		*creator = "Digital Alpha AXP Emulator 1.0";
    u8			*outBuf = NULL;
    AXP_VHDX_ID		*ID;
    AXP_VHDX_HDR	*hdr;
    AXP_VHDX_REG_HDR	*reg;
    AXP_VHDX_REG_ENT	*regMeta, *regBat;
    AXP_VHDX_LOG_HDR	*logHdr;
    AXP_VHDX_BAT_ENT	*batEnt;
    AXP_VHDX_META_HDR	*metaHdr;
    AXP_VHDX_META_ENT	*metaEnt;
    AXP_VHDX_META_FILE	*metaFile;
    AXP_VHDX_META_DISK 	*metaDisk;
    AXP_VHDX_META_SEC	*metaSec;
    AXP_VHDX_META_PAGE83 *meta83;
    void 		*ptr;
    u64			diskSize, minDisk, maxDisk;
    u32			retVal = AXP_VHD_SUCCESS;
    u32			chunkRatio, dataBlksCnt, totBATEnt; /* secBitmapBlksCnt; */
    u32			blkSize, minBlk, maxBlk;
    u32			sectorSize, minSector, maxSector;
    size_t		outLen;
    size_t		creatorSize = strlen(creator);
    int			metaOff, batOff, ii;
    bool		writeRet = false;

    /*
     * Before we can really do some parameter checking, let's make sure the
     * ones that indicate to use the default have the default value within
     * them.  This will simplify the code later.
     */
    if ((param != NULL) && (storageType != NULL))
    {
	switch (param->ver)
	{
	    case CREATE_VER_UNSPEC:
		break;

	    case CREATE_VER_1:
		if (param->ver_1.blkSize == AXP_VHD_DEF_BLK)
		{
		    switch(storageType->deviceID)
		    {
			case STORAGE_TYPE_DEV_ISO:
			    param->ver_1.blkSize = AXP_ISO_BLK_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHD:
			    param->ver_1.blkSize = AXP_VHD_BLK_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHDX:
			    param->ver_1.blkSize = AXP_VHDX_BLK_DEF;
			    break;

			default:
			    break;
		    }
		}
		if (param->ver_1.sectorSize == AXP_VHD_DEF_SEC)
		{
		    switch(storageType->deviceID)
		    {
			case STORAGE_TYPE_DEV_ISO:
			    param->ver_1.sectorSize = AXP_ISO_SEC_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHD:
			    param->ver_1.sectorSize = AXP_VHD_SEC_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHDX:
			    param->ver_1.sectorSize = AXP_VHDX_SEC_DEF;
			    break;

			default:
			    break;
		    }
		}
		blkSize = param->ver_1.blkSize;
		sectorSize = param->ver_1.sectorSize;
		diskSize = param->ver_1.maxSize;
		break;

	    case CREATE_VER_2:
		if (param->ver_2.blkSize == AXP_VHD_DEF_BLK)
		{
		    switch(storageType->deviceID)
		    {
			case STORAGE_TYPE_DEV_ISO:
			    param->ver_2.blkSize = AXP_ISO_BLK_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHD:
			    param->ver_2.blkSize = AXP_VHD_BLK_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHDX:
			    param->ver_2.blkSize = AXP_VHDX_BLK_DEF;
			    break;

			default:
			    break;
		    }
		}
		if (param->ver_2.sectorSize == AXP_VHD_DEF_SEC)
		{
		    switch(storageType->deviceID)
		    {
			case STORAGE_TYPE_DEV_ISO:
			    param->ver_2.sectorSize = AXP_ISO_SEC_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHD:
			    param->ver_2.sectorSize = AXP_VHD_SEC_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHDX:
			    param->ver_2.sectorSize = AXP_VHDX_SEC_DEF;
			    break;

			default:
			    break;
		    }
		}
		blkSize = param->ver_2.blkSize;
		sectorSize = param->ver_2.sectorSize;
		diskSize = param->ver_2.maxSize;
		break;

	    case CREATE_VER_3:
		if (param->ver_3.blkSize == AXP_VHD_DEF_BLK)
		{
		    switch(storageType->deviceID)
		    {
			case STORAGE_TYPE_DEV_ISO:
			    param->ver_3.blkSize = AXP_ISO_BLK_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHD:
			    param->ver_3.blkSize = AXP_VHD_BLK_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHDX:
			    param->ver_3.blkSize = AXP_VHDX_BLK_DEF;
			    break;

			default:
			    break;
		    }
		}
		if (param->ver_3.sectorSize == AXP_VHD_DEF_SEC)
		{
		    switch(storageType->deviceID)
		    {
			case STORAGE_TYPE_DEV_ISO:
			    param->ver_3.sectorSize = AXP_ISO_SEC_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHD:
			    param->ver_3.sectorSize = AXP_VHD_SEC_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHDX:
			    param->ver_3.sectorSize = AXP_VHDX_SEC_DEF;
			    break;

			default:
			    break;
		    }
		}
		blkSize = param->ver_3.blkSize;
		sectorSize = param->ver_3.sectorSize;
		diskSize = param->ver_3.maxSize;
		break;

	    case CREATE_VER_4:
		if (param->ver_4.blkSize == AXP_VHD_DEF_BLK)
		{
		    switch(storageType->deviceID)
		    {
			case STORAGE_TYPE_DEV_ISO:
			    param->ver_4.blkSize = AXP_ISO_BLK_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHD:
			    param->ver_4.blkSize = AXP_VHD_BLK_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHDX:
			    param->ver_4.blkSize = AXP_VHDX_BLK_DEF;
			    break;

			default:
			    break;
		    }
		}
		if (param->ver_4.sectorSize == AXP_VHD_DEF_SEC)
		{
		    switch(storageType->deviceID)
		    {
			case STORAGE_TYPE_DEV_ISO:
			    param->ver_4.sectorSize = AXP_ISO_SEC_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHD:
			    param->ver_4.sectorSize = AXP_VHD_SEC_DEF;
			    break;

			case STORAGE_TYPE_DEV_VHDX:
			    param->ver_4.sectorSize = AXP_VHDX_SEC_DEF;
			    break;

			default:
			    break;
		    }
		}
		blkSize = param->ver_4.blkSize;
		sectorSize = param->ver_4.sectorSize;
		diskSize = param->ver_4.maxSize;
		break;
	}
	switch(storageType->deviceID)
	{
	    case STORAGE_TYPE_DEV_ISO:
		minDisk = 0;
		maxDisk = 0;
		minBlk = 0;
		maxBlk = 0;
		minSector = 0;
		maxSector = 0;
		break;

	    case STORAGE_TYPE_DEV_VHD:
		minDisk = 3 * ONE_M;
		maxDisk = 2040 * ONE_M;
		minBlk = AXP_VHD_BLK_MIN;
		maxBlk = AXP_VHD_BLK_MAX;
		minSector = AXP_VHD_SEC_MIN;
		maxSector = AXP_VHD_SEC_MAX;
		break;

	    case STORAGE_TYPE_DEV_VHDX:
		minDisk = 3 * ONE_M;
		maxDisk = 6 * ONE_T;
		minBlk = AXP_VHDX_BLK_MIN;
		maxBlk = AXP_VHDX_BLK_MAX;
		minSector = AXP_VHDX_SEC_MIN;
		maxSector = AXP_VHDX_SEC_MAX;
		break;

	    default:
		break;
	}
    }

    /*
     * Let's do some quick parameter checking.
     *
     * First check that the required parameters passed by reference are
     * actually available.
     */
    if ((storageType == NULL) || (param == NULL) || (handle == NULL) || (path == NULL))
	retVal = AXP_VHD_INV_PARAM;

    /*
     * Now let's check the values supplied in various parameters.
     *
     *	1) Only Version 1 and Version 2 are supported at this time.
     *	2) If Version 2, then the Access Mask must be NONE.
     *	3) Block Size needs to be between the minimum and maximum, and be a
     *	   power of 2.
     *	4) Sector Size must be either the minimum or maximum (but not in
     *	   between).
     *	5) Disk Size needs to be between the minimum and maximum allowable
     *	   sized and  be a multiple of Sector Size.
     */
    else if (((param->ver != CREATE_VER_1) && (param->ver != CREATE_VER_2)) ||
	     ((param->ver == CREATE_VER_2) && (accessMask != ACCESS_NONE)) ||
	     (flags > CREATE_FULL_PHYSICAL_ALLOCATION) ||
	     (((blkSize <= minBlk) || (blkSize >= maxBlk)) ||
	      (IS_POWER_OF_2(blkSize) == false)) ||
	     ((sectorSize != minSector) && (sectorSize != maxSector)) ||
	     ((diskSize < minDisk) || (diskSize > maxDisk) ||
	      ((diskSize % sectorSize) != 0)))
	retVal = AXP_VHD_INV_PARAM;

    /*
     * We'll need this a bit later, but let's go get all the memory we are
     * going to need up front.
     */
    if (retVal == AXP_VHD_SUCCESS)
	outBuf = (u8 *) calloc(SIXTYFOUR_K, 1);

    /*
     * Let's allocate the block we need to maintain access to the virtual disk
     * image.
     */
    if (outBuf != NULL)
	vhdx = (AXP_VHDX_Handle *) AXP_Allocate_Block(AXP_VHDX_BLK);

    /*
     * If we allocated the block we need, then continue out processing.
     * Otherwise, return an error.
     */
    if (vhdx != NULL)
    {

	/*
	 * The next call is to see if the file already exists.  If it does
	 * we'll return an error.  Otherwise, we'll try to create the file
	 * we need to create.
	 */
	vhdx->fp = fopen(path, "rb");
	if (vhdx->fp == NULL)
	{
	    vhdx->fp = fopen(path, "wb");
	    if (vhdx->fp == NULL)
	    {
		AXP_Deallocate_Block(&vhdx->header);
		retVal = AXP_VHD_INV_HANDLE;
	    }
	}
	else
	{
	    AXP_Deallocate_Block(&vhdx->header);
	    retVal = AXP_VHD_FILE_EXISTS;
	}
    }
    else if (retVal == AXP_VHD_SUCCESS)
	retVal = AXP_VHD_OUTOFMEMORY;

    /*
     * OK, if we get this far, the parameters are good, the handle has been
     * created, and the file has been opened.  Now it's time to initialize it.
     *
     * We will create the following minimal regions in the VHDX file.  These
     * regions are:
     *	1) Header Region
     *	2) Log Region
     *	3) BAT Region
     *	4) Metadata Region
     *
     * 3.1 - Header Section						Page 14
     * The header section contains four items: the file type identifier, two
     * headers, and the region table.
     *
     * Figure 3: The VHDX Header Section Layout
     * +----------+----------+----------+----------+----------+----------+
     * |   File   |   Head   |   Head   |  Region  |  Region  | Reserved |
     * |    ID    |     1    |     2    |     1    |     2    |          |
     * +----------+----------+----------+----------+----------+----------+
     * 0         64         128        192        256        320         1
     * KB        KB          KB         KB         KB         KB        MB
     *
     * We can now initialize the header information within the structure
     * and the file.
     */
    if (retVal == AXP_VHD_SUCCESS)
    {
	i32			convRet;

	ID = (AXP_VHDX_ID *) outBuf;
	memcpy(ID->sig, "vhdxfile", AXP_VHDX_ID_SIG_LEN);
	outLen = AXP_VHDX_CREATOR_LEN * sizeof(uint16_t);
	convRet = AXP_Ascii2UTF_16(creator, creatorSize, ID->creator, &outLen);
	switch (convRet)
	{
	    case E2BIG:
	    case EMFILE:
	    case ENFILE:
	    case ENOMEM:
		retVal = AXP_VHD_OUTOFMEMORY;
		break;

	    case EINVAL:
	    case EILSEQ:
	    case EBADF:
		retVal = AXP_VHD_INV_PARAM;
		break;

	    default:
		break;
	}
	if (retVal != AXP_VHD_SUCCESS)
	{
	    _AXP_VHD_CreateCleanup(vhdx, path);
	    AXP_Deallocate_Block(&vhdx->header);
	}
	else
	{

	    /*
	     * OK, let's write the File Identifier block out at the correct
	     * offset.
	     */
	    writeRet = AXP_WriteAtOffset(
				vhdx->fp,
				outBuf,
				SIXTYFOUR_K,
				AXP_VHDX_FILE_ID_OFF);
	    if (writeRet == false)
	    {
		_AXP_VHD_CreateCleanup(vhdx, path);
		AXP_Deallocate_Block(&vhdx->header);
		retVal = AXP_VHD_WRITE_FAULT;
	    }
	}
    }

    /*
     * Next we set up the header record.  This record is written to the file
     * twice.  This will be written at offset 64KB and 128KB.
     *
     * 3.1.2 - Headers							Page 14
     * Since the header is used to locate the log, updates to the headers
     * cannot be made through the log. To provide power failure consistency
     * guarantees, there are two headers in every VHDX file.
     *
     * Each of the two headers is a 4 KB structure that is aligned to 64KB
     * boundary.  One header is stored at offset 64 KB and the other at 128KB.
     * Only one header is considered current and in use at any point in time.
     */
    if (retVal == AXP_VHD_SUCCESS)
    {
	memset(outBuf, 0, SIXTYFOUR_K);
	hdr = (AXP_VHDX_HDR *) outBuf;
	memcpy(hdr->sig, "head", AXP_VHDX_SIG_LEN);
	uuid_generate(hdr->fileWriteGuid.uuid);
 	AXP_Convert_To(GUID, &hdr->fileWriteGuid, &hdr->fileWriteGuid);
	hdr->logVer = AXP_VHDX_LOG_VER;
	hdr->ver = AXP_VHDX_CURRENT_VER;
	hdr->logLen = AXP_VHDX_LOG_LEN;
	hdr->logOff = AXP_VHDX_LOG_LOC;
	hdr->checkSum = AXP_Crc32(
				outBuf,
				AXP_VHDX_HDR_LEN,
				false,
				hdr->checkSum);

	/*
	 * OK, let's write the Header 1 and Header 2 blocks out at the
	 * correct offset.
	 */
	writeRet = AXP_WriteAtOffset(
				vhdx->fp,
				outBuf,
				SIXTYFOUR_K,
				AXP_VHDX_HEADER1_OFF);
	if (writeRet == true)
	    writeRet = AXP_WriteAtOffset(
				vhdx->fp,
				outBuf,
				SIXTYFOUR_K,
				AXP_VHDX_HEADER2_OFF);
	if (writeRet == false)
	{
	    _AXP_VHD_CreateCleanup(vhdx, path);
	    AXP_Deallocate_Block(&vhdx->header);
	    retVal = AXP_VHD_WRITE_FAULT;
	}
    }

    /*
     * Next we do the Region Table Header.  This record is also written
     * to the file twice.
     *
     * 3.1.3 - Region Table						Page 17
     * The region table consists of a header followed by a variable number of
     * entries, which specify the identity and location of regions within the
     * file. There are two copies of the region table stored at file offset
     * 192KB and 256 KB. Updates to the region table structures must be made
     * through the log.
     */
    if (retVal == AXP_VHD_SUCCESS)
    {
	memset(outBuf, 0, SIXTYFOUR_K);
	reg = (AXP_VHDX_REG_HDR *) outBuf;
	batOff = AXP_VHDX_REG_HDR_LEN;
	metaOff = batOff + AXP_VHDX_REG_ENT_LEN;
	regBat = (AXP_VHDX_REG_ENT *) &outBuf[batOff];
	regMeta = (AXP_VHDX_REG_ENT *) &outBuf[metaOff];

	/*
	 * Now we can initialize the region table.
	 */
	memcpy(reg->sig, "regi", AXP_VHDX_SIG_LEN);
	reg->entryCnt = 2;

	/*
	 * Next we need at least 2 Region Table Entries (one for BAT and
	 * one for Metadata).
	 */
	AXP_VHDX_BAT_GUID(regBat->guid);
 	AXP_Convert_To(GUID, &regBat->guid, &regBat->guid);
	regBat->fileOff = AXP_VHDX_BAT_LOC;
	regBat->len = AXP_VHDX_BAT_LEN;
	regBat->req = 1;

	AXP_VHDX_META_GUID(regMeta->guid);
 	AXP_Convert_To(GUID, &regMeta->guid, &regMeta->guid);
	regMeta->fileOff = AXP_VHDX_META_LOC;
	regMeta->len = AXP_VHDX_META_LEN;
	regMeta->req = 1;

	reg->checkSum = AXP_Crc32(
			outBuf,
			SIXTYFOUR_K,
			false,
			reg->checkSum);

	/*
	 * OK, let's write the Header 1 and Header 2 blocks out at the
	 * correct offset.
	 */
	writeRet = AXP_WriteAtOffset(
			vhdx->fp,
			outBuf,
			SIXTYFOUR_K,
			AXP_VHDX_REG_TBL_HDR1_OFF);
	if (writeRet == true)
	    AXP_WriteAtOffset(
			vhdx->fp,
			outBuf,
			SIXTYFOUR_K,
			AXP_VHDX_REG_TBL_HDR2_OFF);
	if (writeRet == false)
	{
	    _AXP_VHD_CreateCleanup(vhdx, path);
	    AXP_Deallocate_Block(&vhdx->header);
	    retVal = AXP_VHD_WRITE_FAULT;
	}
    }

    /*
     * Now we need to allocate the Block Allocation Table (BAT).
     *
     * 3.4 - BAT							Page 24
     * BAT is a region consisting of a single array of 64-bit values, with an
     * entry for each block that determines the state and file offset of that
     * block. The entries for the payload block and sector bitmap block are
     * interleaved in a way that the sector bitmap block entry associated with
     * a chunk follows the entries for the payload blocks in that chunk. For
     * example, if the chunk ratio is 4, the table�s interleaving would look
     * like by the figure below.
     *
     *	Figure 6: BAT Layout Example
     *	+---+---+---+---+---+---+---+---+---+---+----------------------+
     *	|PB0|PB1|PB2|PB3|SB0|PB4|PB5|PB6|PB7|SB1|                      |
     *	+---+---+---+---+---+---+---+---+---+---+----------------------+
     *	 PB = Payload Block
     *	 SB = Sector Bitmap Block
     *
     * The BAT region must be at least large enough to contain as many entries
     * as required to describe the possible blocks for a given virtual disk
     * size (VirtualDiskSize - Section 3.5.2.2).
     *
     * The chunk ratio is the number of payload blocks in a chunk, or
     * equivalently, the number of payload blocks per sector bitmap block.
     *	chunkRatio = (2^23 * logSectorSize)/blockSize
     *
     * The number of data blocks can be calculated as:
     *	dataBlksCnt = ceil(virtDiskSize/blkSize)
     *
     * The number of sector bitmap blocks can be calculated as:
     *	secBitmapBlksCnt = ceil(dataBlksCnt/chunkRatio)
     *
     * For a dynamic VHDX, the last BAT entry must locate the last payload
     * block of the virtual disk. The total number of BAT entries can be
     * calculated as:
     *	totBATEnt = dataBlksCnt + floor((dataBlksCnt-1)/chunkRatio)
     *
     * For a differencing VHDX, the last BAT entry must be able to locate the
     * last sector bitmap block that contains the last payload sector. The
     * total number of BAT entries can be calculated as:
     *	totBATEnt = secBitmapBlksCnt * (chunkRatio + 1)
     *
     * TODO: If we are not doing a fixed file, then the following can be skipped.
     */
    if (retVal == AXP_VHD_SUCCESS)
    {
	chunkRatio = ((2^23) * sectorSize) / blkSize;
	dataBlksCnt = ceil((double) diskSize / (double) blkSize);

	/*
	 * TODO: How is a differencing VHDX created?
	    secBitmapBlksCnt = ceil((double) dataBlksCnt / (double) chunkRatio);
	 */
	totBATEnt = dataBlksCnt +
		    floor((double) (dataBlksCnt - 1)/(double) chunkRatio);

	/*
	 * Since we are creating the file, we will repeat the BAT Entry to
	 * account for all possible ones that will ever be needed for the
	 * current virtual disk size.
	 */
	memset(outBuf, 0, SIXTYFOUR_K);
	batEnt = (AXP_VHDX_BAT_ENT *) outBuf;
	batEnt->state = AXP_VHDX_PAYL_BLK_NOT_PRESENT;
	batEnt->fileOff = 0;

	/*
	 * Go write out all the BAT entries to the virtual disk.
	 */
	batOff = AXP_VHDX_BAT_LOC;
	for (ii = 0; ((ii < totBATEnt) && (writeRet == true)); ii++)
	{
	    writeRet = AXP_WriteAtOffset(
				vhdx->fp,
				outBuf,
				AXP_VHDX_BAT_ENT_LEN,
				batOff);
	    batOff += AXP_VHDX_BAT_ENT_LEN;
	}
	if (writeRet == false)
	{
	    _AXP_VHD_CreateCleanup(vhdx, path);
	    AXP_Deallocate_Block(&vhdx->header);
	    retVal = AXP_VHD_WRITE_FAULT;
	}
    }

    /*
     * 3.5 - Metadata Region						Page 29
     * The metadata region consists of a fixed-size, 64 KB, unsorted metadata
     * table, followed by unordered, variable-sized, unaligned metadata items
     * and free space. The metadata items represent both user and system
     * metadata, which are distinguished by a bit in the table.
     *
     * Figure 7: Metadata Region Layout Example
     * +-----+---+---+---+---+--+------+-+---+---+----+------+----+---+
     * | MH  |SE1|UE2|SE3|SE4|  | SM 1 | |SM2|   |UM 3|      |SM 4|   |
     * +-----+---+---+---+---+--+------+-+---+---+----+------+----+---+
     *         v   v   v   v       ^       ^       ^           ^
     *         |   |   |   |       |       |       |           |
     *         |   |   |   +-------|-------+       |           |
     *         |   |   +-----------|---------------|-----------+
     *         |   +---------------|---------------+
     *         +-------------------+
     *	MH = Metadata Header		      SM = System Metadata Item
     *	SE = System Metadata Entry	      UM = User Metadata Item
     *	UE = User Metadata Entry
     *
     * Now for the fun.  The metadata.  When creating the file, we are dealing
     * with the following System Metadata:
     *
     *	1) File Parameters
     *	2) Virtual Disk Size
     *	3) Logical Sector Size
     *	4) Physical Sector Size
     */
    if (retVal == AXP_VHD_SUCCESS)
    {
	memset(outBuf, 0, SIXTYFOUR_K);

	/*
	 * So, first things first.  We need a metadata table header that will
	 * be used to describe the total number of metadata entries.  At some
	 * point, we'll create a few user metadata entries, but for now we are
	 * just going to create the system ones we care about (see the list
	 * above).
	 */
	metaHdr = (AXP_VHDX_META_HDR *) outBuf;
	memcpy(metaHdr->sig, "metadata", AXP_VHDX_ID_SIG_LEN);
	metaHdr->entryCnt = 5;	/* Change below as well */

	/*
	 * The first entry is immediately after the header.
	 */
	metaEnt = (AXP_VHDX_META_ENT *) &outBuf[AXP_VHDX_META_HDR_LEN];
	metaOff = AXP_VHDX_META_START_OFF;
	for (ii = 0; ii < 5; ii++)
	{
	    metaEnt->isRequired = 1;
	    metaEnt->off = metaOff;
	    switch (ii)
	    {
		case 0:	/* File Parameters */
		    AXP_VHDX_FILE_PARAM_GUID(metaEnt->guid);
		    metaEnt->len = AXP_VHDX_META_FILE_LEN;
		    break;

		case 1:	/* Virtual Disk Size */
		    AXP_VHDX_VIRT_DSK_SIZE_GUID(metaEnt->guid);
		    metaEnt->len = AXP_VHDX_META_DISK_LEN;
		    break;

		case 2:	/* Logical Sector Size */
		    AXP_VHDX_LOGI_SEC_SIZE_GUID(metaEnt->guid);
		    metaEnt->len = AXP_VHDX_META_SEC_LEN;
		    break;

		case 3:	/* Physical Sector Size */
		    AXP_VHDX_PHYS_SEC_SIZE_GUID(metaEnt->guid);
		    metaEnt->len = AXP_VHDX_META_SEC_LEN;
		    break;

		case 5:
		    AXP_VHDX_PAGE_83_DATA_GUID(metaEnt->guid);
		    metaEnt->len = AXP_VHDX_META_PAGE83_LEN;
		    break;
	    }

	    /*
	     * Calculate the offset for the next metadata item.
	     */
	    metaOff += metaEnt->len;

	    /*
	     * Don't forget to convert the GUID and length fields.
	     */
 	    AXP_Convert_To(GUID, &metaEnt->guid, &metaEnt->guid);

	    /*
	     * Move to the next metadata table entry.
	     */
	    metaEnt += AXP_VHDX_META_ENT_LEN;
	}

	/*
	 * Write out the Metadata Table.
	 */
	 writeRet = AXP_WriteAtOffset(
				vhdx->fp,
				outBuf,
				SIXTYFOUR_K,
				AXP_VHDX_META_LOC);
	 if (writeRet == false)
	 {
	     _AXP_VHD_CreateCleanup(vhdx, path);
	     AXP_Deallocate_Block(&vhdx->header);
	     retVal = AXP_VHD_WRITE_FAULT;
	 }
    }

    /*
     * 3.5.2 - Known Metadata Items					Page 31
     * There are certain Metadata items that are defined in this specification,
     * some of which are optional and some of which are required. The table
     * below summarizes the known metadata items and their optional or required
     * state.
     *
     *	Table 7: Known Metadata Item Properties
     *	--------------------------------------------------------------------------------
     *	Known							IsUser	Is	Is
     *	Items		GUID						Virtual	Required
     *	--------------------------------------------------------------------------------
     *	File Parameters	CAA16737-FA36-4D43-B3B6-33F0AA44E76B	False	False	True
     *	Virtual Disk	2FA54224-CD1B-4876-B211-5DBED83BF4B8	False	True	True
     *	Size
     *	Page 83 Data	BECA12AB-B2E6-4523-93EF-C309E000C746	False	True	True
     *	Logical Sector	8141BF1D-A96F-4709-BA47-F233A8FAAB5F	False	True	True
     *	Size
     *	Physical Sector	CDA348C7-445D-4471-9CC9-E9885251C556	False	True	True
     *	Size
     *	Parent Locator	A8D35F2D-B30B-454D-ABF7-D3D84834AB0C	False	False	True
     *	--------------------------------------------------------------------------------
     */
    if (retVal == AXP_VHD_SUCCESS)
    {

	/*
	 * Now it's time to write out the Metadata Items.
	 */
	memset(outBuf, 0, SIXTYFOUR_K);

	/*
	 * First, the File Parameters.
	 *
	 * TODO: Do we want to have a fixed or dynamic VHDX file?
	 */
	metaFile = (AXP_VHDX_META_FILE *) outBuf;
	metaOff = AXP_VHDX_META_FILE_LEN;
	metaFile->blkSize = blkSize;

	/*
	 * Now, Virtual Disk Size.
	 */
	metaDisk = (AXP_VHDX_META_DISK *) &outBuf[metaOff];
	metaOff += AXP_VHDX_META_DISK_LEN;
	metaDisk->virDskSize = diskSize;

	/*
	 * Next, Logical Sector Size
	 */
	metaSec = (AXP_VHDX_META_SEC *) &outBuf[metaOff];
	metaOff += AXP_VHDX_META_SEC_LEN;

	/*
	 * Second to last, Physical Sector Size
	 */
	metaSec = (AXP_VHDX_META_SEC *) &outBuf[metaOff];
	metaOff += AXP_VHDX_META_SEC_LEN;
	metaSec->secSize = AXP_VHDX_PHYS_SEC_SIZE;

	/*
	 * Finally, Page 83 Data
	 */
	meta83 = (AXP_VHDX_META_PAGE83 *) &outBuf[metaOff];
	metaOff += AXP_VHDX_META_PAGE83_LEN;
	uuid_generate(meta83->pg83Data.uuid);
 	AXP_Convert_To(GUID, &meta83->pg83Data, &meta83->pg83Data);

	/*
	 * Write out the Metadata Items.
	 */
	writeRet = AXP_WriteAtOffset(
				vhdx->fp,
				outBuf,
				SIXTYFOUR_K,
				(AXP_VHDX_META_LOC + AXP_VHDX_META_START_OFF));

	/*
	 * Before returning to the caller, set the value of the handle to
	 * the address of the VHDX handle.
	 */
	if (writeRet == true)
	    *handle = (AXP_VHD_HANDLE) vhdx;
	else
	 {
	     _AXP_VHD_CreateCleanup(vhdx, path);
	     AXP_Deallocate_Block(&vhdx->header);
	     retVal = AXP_VHD_WRITE_FAULT;
	 }
    }

    /*
     * We need to the write the log, with a Log Header, area last.  This is
     * because the Flushed File Offset and Last File Offset need to consider
     * the entire file size at the time the log entry was written.  In order to
     * be able to do this, we need to have written everything above and now we
     * can get the current file size and use this to calculate these values.
     *
     * 3.2 - Log							Page 18
     * The log is a single circular buffer stored contiguously at a
     * location that is specified in the VHDX header. It consists of an
     * ordered sequence of variable-sized entries, each of which represents
     * a set of 4 KB sector updates that need to be performed to the VHDX
     * structures.
     *
     * Figure 4: Log Layout Example
     *
     * |Older Sequence|         Active Sequence         |Older Sequence|
     * +--------------+-------+-----+---+-----+---------+--------------+
     * |              |   N   | N+1 |N+2| N+3 |   N+4   |              |
     * +--------------+-------+-----+---+-----+---------+--------------+
     *                  Tail                      Head
     *                   ^      v    v    v        v
     *                   |      |    |    |        |
     *                   +------+    |    |        |
     *                   +-----------+    |        |
     *                   +----------------+        |
     *                   +-------------------------+
     *
     * 3.2.1.1 Entry Header						Page 20
     * The FlushedFileOffset field stores the VHDX file size in bytes that must
     * be at least as large as the size of the VHDX file at the time the log
     * entry was written. The file size specified in the log entry must have
     * been stable on the host disk such that, even in the case of a system
     * power failure, a non-corrupted VHDX file will be at least as large as
     * the size specified by the log entry. Before shrinking a file while the
     * log is in use, a parser must write the target size to a log entry and
     * flush the entry so that the update is stable on the log on the host disk
     * storage media; this will ensure that the VHDX file is not treated as
     * truncated during log replay. A parser should write the largest possible
     * value that satisfies these requirements. The value must be a multiple
     * of 1MB.
     *
     * The LastFileOffset field stores a file size in bytes that all allocated
     * file structures fit into, at the time the log entry was written. A
     * parser should write the smallest possible value that satisfies these
     * requirements. The value must be a multiple of 1MB.
     */
    if (retVal == AXP_VHD_SUCCESS)
    {
	memset(outBuf, 0, SIXTYFOUR_K);
	logHdr = (AXP_VHDX_LOG_HDR *) outBuf;
	memcpy(logHdr->sig, "loge",AXP_VHDX_SIG_LEN);
	logHdr->entryLen = FOUR_K;
	logHdr->seqNum = 1;
	uuid_generate(logHdr->logGuid.uuid);
 	AXP_Convert_To(GUID, &logHdr->logGuid, &logHdr->logGuid);
	logHdr->flushedFileOff = (u64) AXP_GetFileSize(vhdx->fp);
	logHdr->lastFileOff = (u64) AXP_GetFileSize(vhdx->fp);
	logHdr->checkSum = AXP_Crc32(
				outBuf,
				FOUR_K,
				false,
				logHdr->checkSum);

	/*
	 * OK, let's write the log header out to the correct offset.
	 */
	writeRet = AXP_WriteAtOffset(
				vhdx->fp,
				outBuf,
				FOUR_K,
				AXP_VHDX_LOG_LOC);
	if (writeRet == false)
	{
	    _AXP_VHD_CreateCleanup(vhdx, path);
	    AXP_Deallocate_Block(&vhdx->header);
	    retVal = AXP_VHD_WRITE_FAULT;
	}
    }

    /*
     * Free what we allocated before we get out of here.
     */
    if (outBuf != NULL)
	free(outBuf);

    /*
     * Return the result of this call back to the caller.
     */
    return(retVal);
}

/*
 * AXP_VHD_CloseHandle
 *  Closes an open object handle.
 *
 * Input Parameters:
 *  handle:
 *	A valid handle to an open object.
 *
 * Output Parameters:
 *  None.
 *
 * Return Values:
 *  AXP_VHD_SUCCESS:		Normal Successful Completion.
 *  AXP_VHD_INV_HANDLE:		Failed to create the VHDX file.
 */
u32 AXP_VHD_CloseHandle(AXP_VHD_HANDLE handle)
{
    AXP_VHDX_Handle	*vhdx = (AXP_VHDX_Handle *) handle;
    u32			retVal = AXP_VHD_SUCCESS;

    /*
     * Verify that we have a proper handle.
     */
    if ((vhdx->header.type == AXP_VHDX_BLK) &&
	(vhdx->header.size == sizeof(AXP_VHDX_Handle)))
    {

	/*
	 * TODO: Verify there are no references to the handle.
	 */
	AXP_Deallocate_Block(&vhdx->header);
    }
    else
	retVal = AXP_VHD_INV_HANDLE;

    /*
     * Return the results of this call back to the caller.
     */
    return(retVal);
}
