////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*++

Module Name:

    VolInfo.cpp

Abstract:

    This module implements the volume information routines for UDF called by
    the dispatch driver.

--*/

#include            "udffs.h"

// define the file specific bug-check id
#define         UDF_BUG_CHECK_ID                UDF_FILE_VOL_INFORMATION

//  Local support routines
NTSTATUS
UDFQueryFsVolumeInfo (
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_FS_VOLUME_INFORMATION Buffer,
    IN OUT PULONG Length
    );

NTSTATUS
UDFQueryFsSizeInfo (
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_FS_SIZE_INFORMATION Buffer,
    IN OUT PULONG Length
    );

NTSTATUS
UDFQueryFsFullSizeInfo (
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_FS_FULL_SIZE_INFORMATION Buffer,
    IN OUT PULONG Length
    );

NTSTATUS
UDFQueryFsDeviceInfo (
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_FS_DEVICE_INFORMATION Buffer,
    IN OUT PULONG Length
    );

NTSTATUS
UDFQueryFsAttributeInfo (
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_FS_ATTRIBUTE_INFORMATION Buffer,
    IN OUT PULONG Length
    );

NTSTATUS
UDFSetLabelInfo (
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_FS_LABEL_INFORMATION Buffer,
    IN OUT PULONG Length);

/*
    This is the routine for querying volume information

Arguments:

    Irp - Supplies the Irp being processed

Return Value:

    NTSTATUS - The return status for the operation

 */
NTSTATUS
NTAPI
UDFQueryVolInfo(
    PDEVICE_OBJECT      DeviceObject,       // the logical volume device object
    PIRP                Irp                 // I/O Request Packet
    )
{
    NTSTATUS            RC = STATUS_SUCCESS;
    PIRP_CONTEXT IrpContext = NULL;
    BOOLEAN             AreWeTopLevel = FALSE;

    UDFPrint(("UDFQueryVolInfo: \n"));

    FsRtlEnterFileSystem();
    ASSERT(DeviceObject);
    ASSERT(Irp);

    // set the top level context
    AreWeTopLevel = UDFIsIrpTopLevel(Irp);

    _SEH2_TRY {

        // get an IRP context structure and issue the request
        IrpContext = UDFCreateIrpContext(Irp, DeviceObject);
        if (IrpContext) {
            RC = UDFCommonQueryVolInfo(IrpContext, Irp);
        } else {

            UDFCompleteRequest(IrpContext, Irp, STATUS_INSUFFICIENT_RESOURCES);
            RC = STATUS_INSUFFICIENT_RESOURCES;
        }

    } _SEH2_EXCEPT(UDFExceptionFilter(IrpContext, _SEH2_GetExceptionInformation())) {

        RC = UDFProcessException(IrpContext, Irp);

        UDFLogEvent(UDF_ERROR_INTERNAL_ERROR, RC);
    } _SEH2_END;

    if (AreWeTopLevel) {
        IoSetTopLevelIrp(NULL);
    }

    FsRtlExitFileSystem();

    return(RC);
} // end UDFQueryVolInfo()

/*
    This is the common routine for querying volume information called by both
    the fsd and fsp threads.

Arguments:

    Irp - Supplies the Irp being processed

Return Value:

    NTSTATUS - The return status for the operation

 */
NTSTATUS
UDFCommonQueryVolInfo(
    PIRP_CONTEXT IrpContext,
    PIRP             Irp
    )
{
    NTSTATUS RC = STATUS_INVALID_PARAMETER;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation( Irp );
    ULONG Length;
    BOOLEAN CanWait = FALSE;
    PVCB Vcb;
    BOOLEAN PostRequest = FALSE;
    BOOLEAN AcquiredVCB = FALSE;
    PFILE_OBJECT            FileObject = NULL;
    TYPE_OF_OPEN TypeOfOpen;
    PFCB Fcb;
    PCCB Ccb = NULL;

    _SEH2_TRY {

        UDFPrint(("UDFCommonQueryVolInfo: \n"));

        ASSERT(IrpContext);
        ASSERT(Irp);

        PAGED_CODE();

        FileObject = IrpSp->FileObject;
        ASSERT(FileObject);

        // Decode the file object and fail if this an unopened file object.

        TypeOfOpen = UDFDecodeFileObject(IrpSp->FileObject, &Fcb, &Ccb);

        ASSERT_CCB(Ccb);
        ASSERT_FCB(Fcb);

        Vcb = (PVCB)(IrpSp->DeviceObject->DeviceExtension);
        ASSERT(Vcb);
        //Vcb->VcbState |= UDF_VCB_SKIP_EJECT_CHECK;
        //  Reference our input parameters to make things easier
        Length = IrpSp->Parameters.QueryVolume.Length;
        //  Acquire the Vcb for this volume.
        CanWait = ((IrpContext->Flags & IRP_CONTEXT_FLAG_WAIT) ? TRUE : FALSE);

        RtlZeroMemory(Irp->AssociatedIrp.SystemBuffer, Length);

        switch (IrpSp->Parameters.QueryVolume.FsInformationClass) {

        case FileFsVolumeInformation:

            //  This is the only routine we need the Vcb shared because of
            //  copying the volume label.  All other routines copy fields that
            //  cannot change or are just manifest constants.
            UDFFlushTryBreak(Vcb);
            if (!UDFAcquireResourceShared(&(Vcb->VcbResource), CanWait)) {
                PostRequest = TRUE;
                try_return (RC = STATUS_PENDING);
            }
            AcquiredVCB = TRUE;

            RC = UDFQueryFsVolumeInfo( IrpContext, Vcb, (PFILE_FS_VOLUME_INFORMATION)(Irp->AssociatedIrp.SystemBuffer), &Length );
            break;

        case FileFsSizeInformation:

            RC = UDFQueryFsSizeInfo( IrpContext, Vcb, (PFILE_FS_SIZE_INFORMATION)(Irp->AssociatedIrp.SystemBuffer), &Length );
            break;

        case FileFsDeviceInformation:

            RC = UDFQueryFsDeviceInfo( IrpContext, Vcb, (PFILE_FS_DEVICE_INFORMATION)(Irp->AssociatedIrp.SystemBuffer), &Length );
            break;

        case FileFsAttributeInformation:

            RC = UDFQueryFsAttributeInfo( IrpContext, Vcb, (PFILE_FS_ATTRIBUTE_INFORMATION)(Irp->AssociatedIrp.SystemBuffer), &Length );
            break;

        case FileFsFullSizeInformation:

            RC = UDFQueryFsFullSizeInfo( IrpContext, Vcb, (PFILE_FS_FULL_SIZE_INFORMATION)(Irp->AssociatedIrp.SystemBuffer), &Length );
            break;

        default:

            RC = STATUS_INVALID_DEVICE_REQUEST;
            Irp->IoStatus.Information = 0;
            break;

        }

        //  Set the information field to the number of bytes actually filled in
        Irp->IoStatus.Information = IrpSp->Parameters.QueryVolume.Length - Length;

try_exit:   NOTHING;

    } _SEH2_FINALLY {

        if (AcquiredVCB) {
            UDFReleaseResource(&(Vcb->VcbResource));
            AcquiredVCB = FALSE;
        }

        // Post IRP if required
        if (PostRequest) {

            // Since, the I/O Manager gave us a system buffer, we do not
            // need to "lock" anything.

            // Perform the post operation which will mark the IRP pending
            // and will return STATUS_PENDING back to us
            RC = UDFPostRequest(IrpContext, Irp);

        } else {

            // Can complete the IRP here if no exception was encountered
            if (!_SEH2_AbnormalTermination()) {

                UDFCompleteRequest(IrpContext, Irp, RC);
            }
        } 

    } _SEH2_END;

    return RC;
} // end UDFCommonQueryVolInfo()


//  Local support routine

/*
    This routine implements the query volume info call

Arguments:

    Vcb    - Vcb for this volume.
    Buffer - Supplies a pointer to the output buffer where the information
             is to be returned
    Length - Supplies the length of the buffer in byte.  This variable
             upon return recieves the remaining bytes free in the buffer
 */
NTSTATUS
UDFQueryFsVolumeInfo(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_FS_VOLUME_INFORMATION Buffer,
    IN OUT PULONG Length
    )
{
    ULONG BytesToCopy;
    NTSTATUS Status;

    PAGED_CODE();

    UDFPrint(("  UDFQueryFsVolumeInfo: \n"));
    //  Fill in the data from the Vcb.
    Buffer->VolumeCreationTime.QuadPart = Vcb->VolCreationTime;
    Buffer->VolumeSerialNumber = Vcb->PhSerialNumber;
    UDFPrint(("  SN %x\n", Vcb->PhSerialNumber));

    Buffer->SupportsObjects = FALSE;

    *Length -= FIELD_OFFSET( FILE_FS_VOLUME_INFORMATION, VolumeLabel[0] );

    //  Check if the buffer we're given is long enough
    if (*Length >= (ULONG) Vcb->VolIdent.Length) {
        BytesToCopy = Vcb->VolIdent.Length;
        Status = STATUS_SUCCESS;
    } else {
        BytesToCopy = *Length;
        Status = STATUS_BUFFER_OVERFLOW;
    }
    //  Copy over what we can of the volume label, and adjust *Length
    Buffer->VolumeLabelLength = BytesToCopy;

    if (BytesToCopy)
        RtlCopyMemory( &(Buffer->VolumeLabel[0]), Vcb->VolIdent.Buffer, BytesToCopy );
    *Length -= BytesToCopy;

    return Status;
} // end UDFQueryFsVolumeInfo()

/*
    This routine implements the query volume size call.

Arguments:

    Vcb    - Vcb for this volume.
    Buffer - Supplies a pointer to the output buffer where the information
             is to be returned
    Length - Supplies the length of the buffer in byte.  This variable
             upon return recieves the remaining bytes free in the buffer
 */
NTSTATUS
UDFQueryFsSizeInfo(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_FS_SIZE_INFORMATION Buffer,
    IN OUT PULONG Length
    )
{
    PAGED_CODE();

    UDFPrint(("  UDFQueryFsSizeInfo: \n"));
    //  Fill in the output buffer.
    if (Vcb->BitmapModified) {
        Vcb->TotalAllocUnits =
        Buffer->TotalAllocationUnits.QuadPart = UDFGetTotalSpace(Vcb);
        Vcb->FreeAllocUnits =
        Buffer->AvailableAllocationUnits.QuadPart = UDFGetFreeSpace(Vcb);
        Vcb->BitmapModified = FALSE;
    } else {
        Buffer->TotalAllocationUnits.QuadPart = Vcb->TotalAllocUnits;
        Buffer->AvailableAllocationUnits.QuadPart = Vcb->FreeAllocUnits;
    }
    Vcb->LowFreeSpace = (Vcb->FreeAllocUnits < max(Vcb->FECharge,UDF_DEFAULT_FE_CHARGE)*128);
    if (!Buffer->TotalAllocationUnits.QuadPart)
        Buffer->TotalAllocationUnits.QuadPart = max(1, Vcb->LastPossibleLBA);
    Buffer->SectorsPerAllocationUnit = Vcb->LBlockSize / Vcb->BlockSize;
    if (!Buffer->SectorsPerAllocationUnit)
        Buffer->SectorsPerAllocationUnit = 1;
    Buffer->BytesPerSector = Vcb->BlockSize;
    if (!Buffer->BytesPerSector)
        Buffer->BytesPerSector = 2048;

    UDFPrint(("  Space: Total %I64x, Free %I64x\n",
        Buffer->TotalAllocationUnits.QuadPart,
        Buffer->AvailableAllocationUnits.QuadPart));

    //  Adjust the length variable
    *Length -= sizeof( FILE_FS_SIZE_INFORMATION );
    return STATUS_SUCCESS;
} // UDFQueryFsSizeInfo()

/*
    This routine implements the query volume full size call.

Arguments:

    Vcb    - Vcb for this volume.
    Buffer - Supplies a pointer to the output buffer where the information
             is to be returned
    Length - Supplies the length of the buffer in byte.  This variable
             upon return recieves the remaining bytes free in the buffer
 */
NTSTATUS
UDFQueryFsFullSizeInfo(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_FS_FULL_SIZE_INFORMATION Buffer,
    IN OUT PULONG Length
    )
{
    PAGED_CODE();

    UDFPrint(("  UDFQueryFsFullSizeInfo: \n"));
    //  Fill in the output buffer.
    if (Vcb->BitmapModified) {
        Vcb->TotalAllocUnits =
        Buffer->TotalAllocationUnits.QuadPart = UDFGetTotalSpace(Vcb);
        Vcb->FreeAllocUnits =
        Buffer->CallerAvailableAllocationUnits.QuadPart =
        Buffer->ActualAvailableAllocationUnits.QuadPart = UDFGetFreeSpace(Vcb);
        Vcb->BitmapModified = FALSE;
    } else {
        Buffer->TotalAllocationUnits.QuadPart = Vcb->TotalAllocUnits;
        Buffer->CallerAvailableAllocationUnits.QuadPart =
        Buffer->ActualAvailableAllocationUnits.QuadPart = Vcb->FreeAllocUnits;
    }
    if (!Buffer->TotalAllocationUnits.QuadPart)
        Buffer->TotalAllocationUnits.QuadPart = max(1, Vcb->LastPossibleLBA);
    Buffer->SectorsPerAllocationUnit = Vcb->LBlockSize / Vcb->BlockSize;
    if (!Buffer->SectorsPerAllocationUnit)
        Buffer->SectorsPerAllocationUnit = 1;
    Buffer->BytesPerSector = Vcb->BlockSize;
    if (!Buffer->BytesPerSector)
        Buffer->BytesPerSector = 2048;

    UDFPrint(("  Space: Total %I64x, Free %I64x\n",
        Buffer->TotalAllocationUnits.QuadPart,
        Buffer->ActualAvailableAllocationUnits.QuadPart));

    //  Adjust the length variable
    *Length -= sizeof( FILE_FS_FULL_SIZE_INFORMATION );
    return STATUS_SUCCESS;
} // UDFQueryFsSizeInfo()

/*
    This routine implements the query volume device call.

Arguments:

    Vcb    - Vcb for this volume.
    Buffer - Supplies a pointer to the output buffer where the information
             is to be returned
    Length - Supplies the length of the buffer in byte.  This variable
             upon return recieves the remaining bytes free in the buffer
 */
NTSTATUS
UDFQueryFsDeviceInfo(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_FS_DEVICE_INFORMATION Buffer,
    IN OUT PULONG Length
    )
{
    PAGED_CODE();

    UDFPrint(("  UDFQueryFsDeviceInfo: \n"));
    //  Update the output buffer.
    if (Vcb->TargetDeviceObject->DeviceType != FILE_DEVICE_CD_ROM && Vcb->TargetDeviceObject->DeviceType != FILE_DEVICE_DVD)
    {
        ASSERT(! (Vcb->TargetDeviceObject->Characteristics & (FILE_READ_ONLY_DEVICE | FILE_WRITE_ONCE_MEDIA)));
        Buffer->Characteristics = Vcb->TargetDeviceObject->Characteristics & ~(FILE_READ_ONLY_DEVICE | FILE_WRITE_ONCE_MEDIA);
    }
    else
    {
        Buffer->Characteristics = Vcb->TargetDeviceObject->Characteristics;
    }
    Buffer->DeviceType = Vcb->TargetDeviceObject->DeviceType;
    UDFPrint(("    Characteristics %x, DeviceType %x\n", Buffer->Characteristics, Buffer->DeviceType));
    //  Adjust the length variable
    *Length -= sizeof( FILE_FS_DEVICE_INFORMATION );
    return STATUS_SUCCESS;
} // end UDFQueryFsDeviceInfo()

/*
    This routine implements the query volume attribute call.

Arguments:

    Vcb    - Vcb for this volume.
    Buffer - Supplies a pointer to the output buffer where the information
             is to be returned
    Length - Supplies the length of the buffer in byte.  This variable
             upon return recieves the remaining bytes free in the buffer
 */
NTSTATUS
UDFQueryFsAttributeInfo(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_FS_ATTRIBUTE_INFORMATION Buffer,
    IN OUT PULONG Length
    )
{
    ULONG BytesToCopy;

    NTSTATUS Status = STATUS_SUCCESS;
    PCWSTR FsTypeTitle;
    ULONG FsTypeTitleLen;

    PAGED_CODE();
    UDFPrint(("  UDFQueryFsAttributeInfo: \n"));
    //  Fill out the fixed portion of the buffer.
    Buffer->FileSystemAttributes = FILE_CASE_SENSITIVE_SEARCH |
                                   FILE_CASE_PRESERVED_NAMES |
                                   (UDFStreamsSupported(Vcb) ? FILE_NAMED_STREAMS : 0) |
#ifdef ALLOW_SPARSE
                                   FILE_SUPPORTS_SPARSE_FILES |
#endif //ALLOW_SPARSE
                                   ((Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) ? FILE_READ_ONLY_VOLUME : 0) |

                                   FILE_UNICODE_ON_DISK;

    Buffer->MaximumComponentNameLength = UDF_X_NAME_LEN-1;

    *Length -= FIELD_OFFSET( FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName );
    //  Make sure we can copy full unicode characters.
    *Length &= ~1;
    //  Determine how much of the file system name will fit.

#define UDF_FS_TITLE_UDF L"UDF"

#define UDFSetFsTitle(tit) \
                FsTypeTitle = UDF_FS_TITLE_##tit; \
                FsTypeTitleLen = sizeof(UDF_FS_TITLE_##tit) - sizeof(WCHAR);

    UDFSetFsTitle(UDF);

#undef UDFSetFsTitle
#undef UDF_FS_TITLE_UDF

    if (*Length >= FsTypeTitleLen) {
        BytesToCopy = FsTypeTitleLen;
    } else {
        BytesToCopy = *Length;
        Status = STATUS_BUFFER_OVERFLOW;
    }

    *Length -= BytesToCopy;
    //  Do the file system name.
    Buffer->FileSystemNameLength = BytesToCopy;
    RtlCopyMemory( &Buffer->FileSystemName[0], FsTypeTitle, BytesToCopy );
    //  And return to our caller
    return Status;
} // end UDFQueryFsAttributeInfo()

NTSTATUS
NTAPI
UDFSetVolInfo(
    PDEVICE_OBJECT      DeviceObject,       // the logical volume device object
    PIRP                Irp                 // I/O Request Packet
    )
{
    NTSTATUS            RC = STATUS_SUCCESS;
    PIRP_CONTEXT IrpContext = NULL;
    BOOLEAN             AreWeTopLevel = FALSE;

    UDFPrint(("UDFSetVolInfo: \n"));

    FsRtlEnterFileSystem();
    ASSERT(DeviceObject);
    ASSERT(Irp);

    // set the top level context
    AreWeTopLevel = UDFIsIrpTopLevel(Irp);

    _SEH2_TRY {

        // get an IRP context structure and issue the request
        IrpContext = UDFCreateIrpContext(Irp, DeviceObject);
        ASSERT(IrpContext);

        RC = UDFCommonSetVolInfo(IrpContext, Irp);

    } _SEH2_EXCEPT(UDFExceptionFilter(IrpContext, _SEH2_GetExceptionInformation())) {

        RC = UDFProcessException(IrpContext, Irp);

        UDFLogEvent(UDF_ERROR_INTERNAL_ERROR, RC);
    } _SEH2_END;

    if (AreWeTopLevel) {
        IoSetTopLevelIrp(NULL);
    }

    FsRtlExitFileSystem();

    return(RC);
} // end UDFSetVolInfo()


/*
    This is the common routine for setting volume information called by both
    the fsd and fsp threads.
 */
NTSTATUS
UDFCommonSetVolInfo(
    PIRP_CONTEXT IrpContext,
    PIRP                            Irp
    )
{
    NTSTATUS RC = STATUS_INVALID_PARAMETER;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    ULONG Length;
    BOOLEAN CanWait = FALSE;
    PVCB Vcb;
    BOOLEAN PostRequest = FALSE;
    BOOLEAN AcquiredVCB = FALSE;
    PFILE_OBJECT            FileObject = NULL;
    TYPE_OF_OPEN TypeOfOpen;
    PFCB Fcb;
    PCCB Ccb;

    _SEH2_TRY {

        UDFPrint(("UDFCommonSetVolInfo: \n"));
        ASSERT(IrpContext);
        ASSERT(Irp);

        PAGED_CODE();

        FileObject = IrpSp->FileObject;
        ASSERT(FileObject);

        // Decode the file object and fail if this an unopened file object.

        TypeOfOpen = UDFDecodeFileObject(IrpSp->FileObject, &Fcb, &Ccb);

        ASSERT_CCB(Ccb);
        ASSERT_FCB(Fcb);

        if (Ccb && Ccb->Fcb && (Ccb->Fcb->NodeIdentifier.NodeTypeCode != UDF_NODE_TYPE_VCB)) {
            UDFPrint(("    Can't change Label on Non-volume object\n"));
            try_return(RC = STATUS_ACCESS_DENIED);
        }

        Vcb = (PVCB)(IrpSp->DeviceObject->DeviceExtension);
        ASSERT(Vcb);
        Vcb->VcbState |= UDF_VCB_SKIP_EJECT_CHECK;
        //  Reference our input parameters to make things easier

        Length = IrpSp->Parameters.SetVolume.Length;
        //  Acquire the Vcb for this volume.
        CanWait = ((IrpContext->Flags & IRP_CONTEXT_FLAG_WAIT) ? TRUE : FALSE);
        if (!UDFAcquireResourceShared(&(Vcb->VcbResource), CanWait)) {
            PostRequest = TRUE;
            try_return (RC = STATUS_PENDING);
        }
        AcquiredVCB = TRUE;

        switch (IrpSp->Parameters.SetVolume.FsInformationClass) {

        case FileFsLabelInformation:

            RC = UDFSetLabelInfo( IrpContext, Vcb, (PFILE_FS_LABEL_INFORMATION)(Irp->AssociatedIrp.SystemBuffer), &Length );
            Irp->IoStatus.Information = 0;
            break;

        default:

            RC = STATUS_INVALID_DEVICE_REQUEST;
            Irp->IoStatus.Information = 0;
            break;

        }

        //  Set the information field to the number of bytes actually filled in
        Irp->IoStatus.Information = IrpSp->Parameters.SetVolume.Length - Length;

try_exit:   NOTHING;

    } _SEH2_FINALLY {

        if (AcquiredVCB) {
            UDFReleaseResource(&(Vcb->VcbResource));
            AcquiredVCB = FALSE;
        }

        // Post IRP if required
        if (PostRequest) {

            // Since, the I/O Manager gave us a system buffer, we do not
            // need to "lock" anything.

            // Perform the post operation which will mark the IRP pending
            // and will return STATUS_PENDING back to us
            RC = UDFPostRequest(IrpContext, Irp);

        } else {

            // Can complete the IRP here if no exception was encountered
            if (!_SEH2_AbnormalTermination()) {

                UDFCompleteRequest(IrpContext, Irp, RC);
            }
        }

    } _SEH2_END;

    return RC;
} // end UDFCommonSetVolInfo()

/*
    This sets Volume Label
 */
NTSTATUS
UDFSetLabelInfo (
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PFILE_FS_LABEL_INFORMATION Buffer,
    IN OUT PULONG Length
    )
{
    PAGED_CODE();

    UDFPrint(("  UDFSetLabelInfo: \n"));
    if (Buffer->VolumeLabelLength > UDF_VOL_LABEL_LEN*sizeof(WCHAR)) {
        // Too long Volume Label... NT doesn't like it
        UDFPrint(("  UDFSetLabelInfo: STATUS_INVALID_VOLUME_LABEL\n"));
        return STATUS_INVALID_VOLUME_LABEL;
    }

    if (Vcb->VolIdent.Buffer) MyFreePool__(Vcb->VolIdent.Buffer);
    Vcb->VolIdent.Buffer = (PWCHAR)MyAllocatePool__(NonPagedPool, Buffer->VolumeLabelLength+sizeof(WCHAR));
    if (!Vcb->VolIdent.Buffer) return STATUS_INSUFFICIENT_RESOURCES;

    Vcb->VolIdent.Length = (USHORT)Buffer->VolumeLabelLength;
    Vcb->VolIdent.MaximumLength = (USHORT)Buffer->VolumeLabelLength+sizeof(WCHAR);
    RtlCopyMemory(Vcb->VolIdent.Buffer, &(Buffer->VolumeLabel), Buffer->VolumeLabelLength);
    Vcb->VolIdent.Buffer[Buffer->VolumeLabelLength/sizeof(WCHAR)] = 0;
    UDFSetModified(Vcb);

    UDFPrint(("  UDFSetLabelInfo: OK\n"));
    return STATUS_SUCCESS;
} // end UDFSetLabelInfo ()
