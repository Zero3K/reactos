////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*

 Module name: Cleanup.cpp

 Abstract:

    Contains code to handle the "Cleanup" dispatch entry point.

 Environment:

    Kernel mode only
*/

#include            "udffs.h"

// define the file specific bug-check id
#define         UDF_BUG_CHECK_ID                UDF_FILE_CLEANUP

VOID
UDFAutoUnlock (
    IN PVCB Vcb
    );

/*************************************************************************
*
* Function: UDFCleanup()
*
* Description:
*   The I/O Manager will invoke this routine to handle a cleanup
*   request
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL (invocation at higher IRQL will cause execution
*   to be deferred to a worker thread context)
*
* Return Value: STATUS_SUCCESS
*
*************************************************************************/
NTSTATUS
NTAPI
UDFCleanup(
    PDEVICE_OBJECT  DeviceObject,  // the logical volume device object
    PIRP            Irp            // I/O Request Packet
    )
{
    NTSTATUS                RC = STATUS_SUCCESS;
    PIRP_CONTEXT IrpContext = NULL;
    BOOLEAN                 AreWeTopLevel = FALSE;

    TmPrint(("UDFCleanup\n"));

    FsRtlEnterFileSystem();
    ASSERT(DeviceObject);
    ASSERT(Irp);

    // set the top level context
    AreWeTopLevel = UDFIsIrpTopLevel(Irp);

    _SEH2_TRY {

        // get an IRP context structure and issue the request
        IrpContext = UDFCreateIrpContext(Irp, DeviceObject);
        if (IrpContext) {
            RC = UDFCommonCleanup(IrpContext, Irp);
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
} // end UDFCleanup()

/*************************************************************************
*
* Function: UDFCommonCleanup()
*
* Description:
*   The actual work is performed here. This routine may be invoked in one'
*   of the two possible contexts:
*   (a) in the context of a system worker thread
*   (b) in the context of the original caller
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: Does not matter!
*
*************************************************************************/
NTSTATUS
UDFCommonCleanup(
    PIRP_CONTEXT IrpContext,
    PIRP             Irp)
{
    IO_STATUS_BLOCK         IoStatus;
    NTSTATUS                RC = STATUS_SUCCESS;
    NTSTATUS                RC2;
    PFILE_OBJECT            FileObject = NULL;
    PFCB                    Fcb = NULL;
    PCCB                    Ccb = NULL;
    PVCB                    Vcb = NULL;
    ULONG                   lc = 0;
    BOOLEAN                 AcquiredVcb = FALSE;
    BOOLEAN                 AcquiredFCB = FALSE;
    BOOLEAN                 AcquiredParentFCB = FALSE;
    BOOLEAN SendUnlockNotification = FALSE;

//    BOOLEAN                 CompleteIrp = TRUE;
//    BOOLEAN                 PostRequest = FALSE;
    BOOLEAN                 ChangeTime = FALSE;
#ifdef UDF_DBG
    BOOLEAN                 CanWait = FALSE;
#endif // UDF_DBG
    BOOLEAN                 ForcedCleanUp = FALSE;

    PUDF_FILE_INFO          NextFileInfo = NULL;
    TYPE_OF_OPEN            TypeOfOpen;
#ifdef UDF_DBG
    UNICODE_STRING          CurName;
    PDIR_INDEX_HDR          DirNdx;
#endif // UDF_DBG
//    PUDF_DATALOC_INFO       Dloc;

    TmPrint(("UDFCommonCleanup\n"));

   // Get the file object out of the Irp and decode the type of open.

    FileObject = IoGetCurrentIrpStackLocation(Irp)->FileObject;

    TypeOfOpen = UDFDecodeFileObject(FileObject, &Fcb, &Ccb);

    //  No work here for either an UnopenedFile object or a StreamFileObject.

    if (TypeOfOpen <= StreamFileOpen) {

        UDFCompleteRequest(IrpContext, Irp, STATUS_SUCCESS);

        return STATUS_SUCCESS;
    }

    //  Keep a local pointer to the Vcb.
    Vcb = Fcb->Vcb;

    ASSERT_CCB(Ccb);
    ASSERT_FCB(Fcb);
    ASSERT_VCB(Vcb);

    _SEH2_TRY {

#ifdef UDF_DBG
        CanWait = (IrpContext->Flags & IRP_CONTEXT_FLAG_WAIT) ? TRUE : FALSE;
        AdPrint(("   %s\n", CanWait ? "Wt" : "nw"));
        ASSERT(CanWait);
#endif // UDF_DBG
        UDFAcquireResourceShared(&(Vcb->VcbResource), TRUE);
        AcquiredVcb = TRUE;
        // Steps we shall take at this point are:
        // (a) Acquire the file (FCB) exclusively
        // (b) Flush file data to disk
        // (c) Talk to the FSRTL package (if we use it) about pending oplocks.
        // (d) Notify the FSRTL package for use with pending notification IRPs
        // (e) Unlock byte-range locks (if any were acquired by process)
        // (f) Update time stamp values (e.g. fast-IO had been performed)
        // (g) Inform the Cache Manager to uninitialize Cache Maps ...
        // and other similar stuff.
        //  BrutePoint();

        if (Fcb == Fcb->Vcb->VolumeDasdFcb) {
            AdPrint(("Cleaning up Volume\n"));
            AdPrint(("UDF: FcbCleanup: %x\n", Fcb->FcbCleanup));

            // For a force dismount, physically disconnect this Vcb from the device so 
            // a new mount can occur.  Vcb deletion cannot happen at this time since 
            // there is a reference on it associated with this very request,  but we'll 
            // call check for dismount again later after we process this close.

            if (FlagOn(Ccb->Flags, UDF_CCB_FLAG_DISMOUNT_ON_CLOSE)) {
        
                UDFAcquireResourceExclusive(&UdfData.GlobalDataResource, TRUE);
        
                UDFCheckForDismount(IrpContext, Vcb, TRUE);

                UDFReleaseResource(&(UdfData.GlobalDataResource));

            // If this handle actually wrote something, flush the device buffers,
            // and then set the verify bit now just to be safe (in case there is no
            // dismount).
        
            } else if (FlagOn(FileObject->Flags, FO_FILE_MODIFIED )) {
        
                UDFHijackIrpAndFlushDevice(IrpContext, Irp, Vcb->TargetDeviceObject);
                UDFUpdateMediaChangeCount(Vcb, 0);
                UDFMarkDevForVerifyIfVcbMounted(Vcb);
            }

            //  If the volume is locked by this file object then release
            //  the volume and send notification.

            if (FlagOn(Vcb->VcbState, VCB_STATE_LOCKED) &&
                FileObject == Vcb->VolumeLockFileObject) {

                UDFAutoUnlock(Vcb);
                SendUnlockNotification = TRUE;
            }

            UDFInterlockedDecrement((PLONG)&(Fcb->FcbCleanup));
            UDFInterlockedDecrement((PLONG)&(Vcb->VcbCleanup));
            if (FileObject->Flags & FO_CACHE_SUPPORTED) {
                // we've cached close
                UDFInterlockedDecrement((PLONG)&(Fcb->CachedOpenHandleCount));
            }
            ASSERT(Fcb->FcbCleanup <= (Fcb->FcbReference-1));


            MmPrint(("    CcUninitializeCacheMap()\n"));
            CcUninitializeCacheMap(FileObject, NULL, NULL);

            //  We must clean up the share access at this time, since we may not
            //  get a Close call for awhile if the file was mapped through this
            //  File Object.
            IoRemoveShareAccess( FileObject, &Fcb->ShareAccess);

            try_return(RC = STATUS_SUCCESS);
        }
//        BrutePoint();
#ifdef UDF_DBG
        DirNdx = UDFGetDirIndexByFileInfo(Fcb->FileInfo);
        if (DirNdx) {
            CurName = UDFDirIndex(DirNdx, Fcb->FileInfo->Index)->FName;
            if (CurName.Length) {
                AdPrint(("Cleaning up file: %wZ %8.8x\n", &CurName, FileObject))
            } else {
                AdPrint(("Cleaning up file: ??? \n"));
            }
        }
#endif //UDF_DBG
        AdPrint(("UDF: FcbCleanup: %x\n", Fcb->FcbCleanup));
        // Acquire parent object
        if (Fcb->FileInfo->ParentFile) {
            UDF_CHECK_PAGING_IO_RESOURCE(Fcb->FileInfo->ParentFile->Fcb);
            UDFAcquireResourceExclusive(&(Fcb->FileInfo->ParentFile->Fcb->FcbNonpaged->FcbResource), TRUE);
        } else {
            UDFAcquireResourceShared(&(Vcb->VcbResource), TRUE);
        }
        AcquiredParentFCB = TRUE;
        // Acquire current object
        UDF_CHECK_PAGING_IO_RESOURCE(Fcb);
        UDFAcquireResourceExclusive(&Fcb->FcbNonpaged->FcbResource, TRUE);
        AcquiredFCB = TRUE;
        // dereference object
        UDFInterlockedDecrement((PLONG)&Fcb->FcbCleanup);
        UDFInterlockedDecrement((PLONG)&Vcb->VcbCleanup);
        if (FileObject->Flags & FO_CACHE_SUPPORTED) {
            // we've cached close
            UDFInterlockedDecrement((PLONG)&Fcb->CachedOpenHandleCount);
        }
        ASSERT(Fcb->FcbCleanup <= (Fcb->FcbReference-1));

        // check if Ccb being cleaned up has DeleteOnClose flag set
        if (Ccb->Flags & UDF_CCB_DELETE_ON_CLOSE) {
            AdPrint(("    DeleteOnClose\n"));
            // Ok, now we'll become 'delete on close'...
            ASSERT(!(Fcb->FcbState & UDF_FCB_ROOT_DIRECTORY));
            Fcb->FcbState |= UDF_FCB_DELETE_ON_CLOSE;
            FileObject->DeletePending = TRUE;
            //  Report this to the dir notify package for a directory.
            if (Fcb->FcbState & UDF_FCB_DIRECTORY) {
                FsRtlNotifyFullChangeDirectory( Vcb->NotifyIRPMutex, &(Vcb->NextNotifyIRP),
                                                (PVOID)Ccb, NULL, FALSE, FALSE,
                                                0, NULL, NULL, NULL );
            }
        }

        if (!(Fcb->FcbState & UDF_FCB_DIRECTORY)) {

            //  Unlock all outstanding file locks.
            if (Fcb->FileLock != NULL) {

                FsRtlFastUnlockAll(Fcb->FileLock,
                                   FileObject,
                                   IoGetRequestorProcess(Irp),
                                   NULL);
            }
        }
        // get Link count
        lc = UDFGetFileLinkCount(Fcb->FileInfo);

        if ( (Fcb->FcbState & UDF_FCB_DELETE_ON_CLOSE) &&
           !(Fcb->FcbCleanup)) {
            // This can be useful for Streams, those were brutally deleted
            // (together with parent object)
            ASSERT(!(Fcb->FcbState & UDF_FCB_ROOT_DIRECTORY));
            FileObject->DeletePending = TRUE;

            // we should mark all streams of the file being deleted
            // for deletion too, if there are no more Links to
            // main data stream
            if ((lc <= 1) &&
               !UDFIsSDirDeleted(Fcb->FileInfo->Dloc->SDirInfo)) {
                RC = UDFMarkStreamsForDeletion(IrpContext, Vcb, Fcb, TRUE); // Delete
            }
            // we can release these resources 'cause UDF_FCB_DELETE_ON_CLOSE
            // flag is already set & the file can't be opened
            UDF_CHECK_PAGING_IO_RESOURCE(Fcb);
            UDFReleaseResource(&Fcb->FcbNonpaged->FcbResource);
            AcquiredFCB = FALSE;
            if (Fcb->FileInfo->ParentFile) {
                UDF_CHECK_PAGING_IO_RESOURCE(Fcb->ParentFcb);
                UDFReleaseResource(&Fcb->ParentFcb->FcbNonpaged->FcbResource);
            } else {
                UDFReleaseResource(&Vcb->VcbResource);
            }
            AcquiredParentFCB = FALSE;
            UDFReleaseResource(&(Vcb->VcbResource));
            AcquiredVcb = FALSE;

            // Make system to issue last Close request
            // for our Target ...
            UDFRemoveFromSystemDelayedQueue(Fcb);

#ifdef UDF_DELAYED_CLOSE
            // remove file from our DelayedClose queue
            UDFRemoveFromDelayedQueue(Fcb);
            ASSERT(!Fcb->IrpContextLite);
#endif //UDF_DELAYED_CLOSE

            UDFAcquireResourceShared(&Vcb->VcbResource, TRUE);
            AcquiredVcb = TRUE;
            if (Fcb->FileInfo->ParentFile) {
                UDF_CHECK_PAGING_IO_RESOURCE(Fcb->ParentFcb);
                UDFAcquireResourceExclusive(&(Fcb->ParentFcb->FcbNonpaged->FcbResource), TRUE);
            } else {
                UDFAcquireResourceShared(&Vcb->VcbResource, TRUE);
            }
            AcquiredParentFCB = TRUE;
            UDF_CHECK_PAGING_IO_RESOURCE(Fcb);
            UDFAcquireResourceExclusive(&Fcb->FcbNonpaged->FcbResource, TRUE);
            AcquiredFCB = TRUE;

            // we should set file sizes to zero if there are no more
            // links to this file
            if (lc <= 1) {
                // Synchronize here with paging IO
                UDFAcquireResourceExclusive(&Fcb->FcbNonpaged->FcbPagingIoResource, TRUE);
                // set file size to zero (for system cache manager)
//                Fcb->CommonFCBHeader.ValidDataLength.QuadPart =
                Fcb->Header.FileSize.QuadPart =
                    Fcb->Header.ValidDataLength.QuadPart = 0;
                CcSetFileSizes(FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize);

                UDFReleaseResource(&Fcb->FcbNonpaged->FcbPagingIoResource);
            }
        }

#ifdef UDF_DELAYED_CLOSE
        if ((Fcb->FcbReference == 1) &&
         /*(Fcb->NodeIdentifier.NodeType != UDF_NODE_TYPE_VCB) &&*/ // see above
            (!(Fcb->FcbState & UDF_FCB_DELETE_ON_CLOSE)) ) {
            Fcb->FcbState |= UDF_FCB_DELAY_CLOSE;
        }
#endif //UDF_DELAYED_CLOSE

        NextFileInfo = Fcb->FileInfo;

        // do we need to delete it now ?
        if ( (Fcb->FcbState & UDF_FCB_DELETE_ON_CLOSE) &&
           !(Fcb->FcbCleanup)) {

            // can we do it ?
            if (Fcb->FcbState & UDF_FCB_DIRECTORY) {
                ASSERT(!(Fcb->FcbState & UDF_FCB_ROOT_DIRECTORY));
                if (!UDFIsDirEmpty__(NextFileInfo)) {
                    // forget about it
                    Fcb->FcbState &= ~UDF_FCB_DELETE_ON_CLOSE;
                    goto DiscardDelete;
                }
            } else
            if (lc <= 1) {
                // Synchronize here with paging IO
                BOOLEAN AcquiredPagingIo;
                AcquiredPagingIo = UDFAcquireResourceExclusiveWithCheck(&Fcb->FcbNonpaged->FcbPagingIoResource);
                // set file size to zero (for UdfInfo package)
                // we should not do this for directories and linked files
                UDFResizeFile__(IrpContext, Vcb, NextFileInfo, 0);
                if (AcquiredPagingIo) {
                    UDFReleaseResource(&Fcb->FcbNonpaged->FcbPagingIoResource);
                }
            }
            // mark parent object for deletion if requested
            if ((Fcb->FcbState & UDF_FCB_DELETE_PARENT) &&
                Fcb->ParentFcb) {
                ASSERT(!(Fcb->ParentFcb->FcbState & UDF_FCB_ROOT_DIRECTORY));
                Fcb->ParentFcb->FcbState |= UDF_FCB_DELETE_ON_CLOSE;
            }
            // flush file. It is required by UDFUnlinkFile__()
            RC = UDFFlushFile__(IrpContext, Vcb, NextFileInfo);
            if (!NT_SUCCESS(RC)) {
                AdPrint(("Error flushing file !!!\n"));
            }
            // try to unlink
            if ((RC = UDFUnlinkFile__(IrpContext, Vcb, NextFileInfo, TRUE)) == STATUS_CANNOT_DELETE) {
                // If we can't delete file with Streams due to references,
                // mark SDir & Streams
                // for Deletion. We shall also set DELETE_PARENT flag to
                // force Deletion of the current file later... when curently
                // opened Streams would be cleaned up.

                // WARNING! We should keep SDir & Streams if there is a
                // link to this file
                if (NextFileInfo->Dloc &&
                   NextFileInfo->Dloc->SDirInfo &&
                   NextFileInfo->Dloc->SDirInfo->Fcb) {

                    BrutePoint();
                    if (!UDFIsSDirDeleted(NextFileInfo->Dloc->SDirInfo)) {
//                        RC = UDFMarkStreamsForDeletion(Vcb, Fcb, TRUE); // Delete
//#ifdef UDF_ALLOW_PRETEND_DELETED
                        UDFPretendFileDeleted__(Vcb, Fcb->FileInfo);
//#endif //UDF_ALLOW_PRETEND_DELETED
                    }
                    goto NotifyDelete;

                } else {
                    // Getting here means that we can't delete file because of
                    // References/PemissionsDenied/Smth.Else,
                    // but not Linked+OpenedStream
                    BrutePoint();
//                    RC = STATUS_SUCCESS;
                    goto DiscardDelete_1;
                }
            } else {
DiscardDelete_1:
                // We have got an ugly ERROR, or
                // file is deleted, so forget about it
                ASSERT(!(Fcb->FcbState & UDF_FCB_ROOT_DIRECTORY));
                ForcedCleanUp = TRUE;
                if (NT_SUCCESS(RC))
                    Fcb->FcbState &= ~UDF_FCB_DELETE_ON_CLOSE;
                Fcb->FcbState |= UDF_FCB_DELETED;
                RC = STATUS_SUCCESS;
            }
NotifyDelete:
            // We should prevent SetEOF operations on completly
            // deleted data streams
            if (lc < 1) {
                Fcb->NtReqFCBFlags |= UDF_NTREQ_FCB_DELETED;
            }
            // Report that we have removed an entry.
            if (UDFIsAStream(NextFileInfo)) {
                UDFNotifyFullReportChange( Vcb, NextFileInfo->Fcb,
                                       FILE_NOTIFY_CHANGE_STREAM_NAME,
                                       FILE_ACTION_REMOVED_STREAM);
            } else {
                UDFNotifyFullReportChange( Vcb, NextFileInfo->Fcb,
                                       UDFIsADirectory(NextFileInfo) ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                                       FILE_ACTION_REMOVED);
            }
        } else
        if (Fcb->FcbState & UDF_FCB_DELETE_ON_CLOSE) {
DiscardDelete:
            UDFNotifyFullReportChange( Vcb, NextFileInfo->Fcb,
                                     ((Ccb->Flags & UDF_CCB_ACCESS_TIME_SET) ? FILE_NOTIFY_CHANGE_LAST_ACCESS : 0) |
                                     ((Ccb->Flags & UDF_CCB_WRITE_TIME_SET) ? (FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE) : 0) |
                                     0,
                                     UDFIsAStream(NextFileInfo) ? FILE_ACTION_MODIFIED_STREAM : FILE_ACTION_MODIFIED);
        }

        if (Fcb->FcbState & UDF_FCB_DIRECTORY) {
            //  Report to the dir notify package for a directory.
            FsRtlNotifyCleanup( Vcb->NotifyIRPMutex, &(Vcb->NextNotifyIRP), (PVOID)Ccb );
        }

        // we can't purge Cache when more than one link exists
        if (lc > 1) {
            ForcedCleanUp = FALSE;
        }

        if (FileObject->Flags & FO_CACHE_SUPPORTED &&
             Fcb->FcbNonpaged->SegmentObject.DataSectionObject) {
            BOOLEAN LastNonCached = (!Fcb->CachedOpenHandleCount &&
                                      Fcb->FcbCleanup);
            // If this was the last cached open, and there are open
            // non-cached handles, attempt a flush and purge operation
            // to avoid cache coherency overhead from these non-cached
            // handles later.  We ignore any I/O errors from the flush.
            // We shall not flush deleted files
            RC = STATUS_SUCCESS;
            if (  LastNonCached
                      ||
                (!Fcb->FcbCleanup &&
                 !ForcedCleanUp) ) {

                LONGLONG OldFileSize, NewFileSize;

                if ((OldFileSize = Fcb->Header.ValidDataLength.QuadPart) <
                    (NewFileSize = Fcb->Header.FileSize.QuadPart)) {
                    UDFZeroData(Vcb,
                                FileObject,
                                OldFileSize,
                                NewFileSize - OldFileSize,
                                TRUE);

                    Fcb->Header.ValidDataLength.QuadPart = NewFileSize;
                }

                MmPrint(("    CcFlushCache()\n"));
                CcFlushCache(&Fcb->FcbNonpaged->SegmentObject, NULL, 0, &IoStatus);
                if (!NT_SUCCESS(IoStatus.Status)) {
                    MmPrint(("    CcFlushCache() error: %x\n", IoStatus.Status));
                    RC = IoStatus.Status;
                }
            }
            // If file is deleted or it is last cached open, but there are
            // some non-cached handles we should purge cache section
            if (ForcedCleanUp || LastNonCached) {
                if (Fcb->FcbNonpaged->SegmentObject.DataSectionObject) {
                    MmPrint(("    CcPurgeCacheSection()\n"));
                    CcPurgeCacheSection(&Fcb->FcbNonpaged->SegmentObject, NULL, 0, FALSE);
                }
/*                MmPrint(("    CcPurgeCacheSection()\n"));
                CcPurgeCacheSection(&Fcb->SectionObject, NULL, 0, FALSE);*/
            }
            // we needn't Flush here. It will be done in UDFCloseFileInfoChain()
        }

        // Update FileTimes & Attrs
        if (!(Vcb->VcbState & VCB_STATE_VOLUME_READ_ONLY) &&
           !(Fcb->FcbState & (UDF_FCB_DELETE_ON_CLOSE |
                              UDF_FCB_DELETED /*|
                              UDF_FCB_DIRECTORY |
                              UDF_FCB_READ_ONLY*/)) &&
           !UDFIsAStreamDir(NextFileInfo)) {
            LONGLONG NtTime;
            LONGLONG ASize;
            KeQuerySystemTime((PLARGE_INTEGER)&NtTime);
            // Check if we should set ARCHIVE bit & LastWriteTime
            if (FileObject->Flags & FO_FILE_MODIFIED) {
                ULONG Attr;
                PDIR_INDEX_ITEM DirNdx;
                DirNdx = UDFDirIndex(UDFGetDirIndexByFileInfo(NextFileInfo), NextFileInfo->Index);
                ASSERT(DirNdx);
                // Archive bit
                if (!(Ccb->Flags & UDF_CCB_ATTRIBUTES_SET) &&
                    (Vcb->CompatFlags & UDF_VCB_IC_UPDATE_ARCH_BIT)) {
                    Attr = UDFAttributesToNT(DirNdx, NextFileInfo->Dloc->FileEntry);
                    if (!(Attr & FILE_ATTRIBUTE_ARCHIVE))
                        UDFAttributesToUDF(DirNdx, NextFileInfo->Dloc->FileEntry, Attr | FILE_ATTRIBUTE_ARCHIVE);
                }
                // WriteTime
                if (!(Ccb->Flags & UDF_CCB_WRITE_TIME_SET) &&
                    (Vcb->CompatFlags & UDF_VCB_IC_UPDATE_MODIFY_TIME)) {
                    UDFSetFileXTime(NextFileInfo, NULL, &NtTime, NULL, &NtTime);
                    Fcb->LastWriteTime.QuadPart =
                    Fcb->LastAccessTime.QuadPart = NtTime;
                    ChangeTime = TRUE;
                }
            }
            if (!(Fcb->FcbState & UDF_FCB_DIRECTORY)) {
                // Update sizes in DirIndex
                if (!Fcb->FcbCleanup) {
                    ASize = UDFGetFileAllocationSize(Vcb, NextFileInfo);
//                        Fcb->CommonFCBHeader.AllocationSize.QuadPart;
                    UDFSetFileSizeInDirNdx(Vcb, NextFileInfo, &ASize);
                } else
                if (FileObject->Flags & FO_FILE_SIZE_CHANGED) {
                    ASize = //UDFGetFileAllocationSize(Vcb, NextFileInfo);
                    Fcb->Header.AllocationSize.QuadPart;
                    UDFSetFileSizeInDirNdx(Vcb, NextFileInfo, &ASize);
                }
            }
            // AccessTime
            if ((FileObject->Flags & FO_FILE_FAST_IO_READ) &&
               !(Ccb->Flags & UDF_CCB_ACCESS_TIME_SET) &&
                (Vcb->CompatFlags & UDF_VCB_IC_UPDATE_ACCESS_TIME)) {
                UDFSetFileXTime(NextFileInfo, NULL, &NtTime, NULL, NULL);
                Fcb->LastAccessTime.QuadPart = NtTime;
//                ChangeTime = TRUE;
            }
            // ChangeTime (AttrTime)
            if (!(Ccb->Flags & UDF_CCB_MODIFY_TIME_SET) &&
                (Vcb->CompatFlags & UDF_VCB_IC_UPDATE_ATTR_TIME) &&
                (ChangeTime || (Ccb->Flags & (UDF_CCB_ATTRIBUTES_SET |
                                                 UDF_CCB_CREATE_TIME_SET |
                                                 UDF_CCB_ACCESS_TIME_SET |
                                                 UDF_CCB_WRITE_TIME_SET))) ) {
                UDFSetFileXTime(NextFileInfo, NULL, NULL, &NtTime, NULL);
                Fcb->ChangeTime.QuadPart = NtTime;
            }
        }

        if (!(Fcb->FcbState & UDF_FCB_DIRECTORY) &&
            ForcedCleanUp) {
            // flush system cache
            MmPrint(("    CcUninitializeCacheMap()\n"));
            CcUninitializeCacheMap(FileObject, &(UdfData.UDFLargeZero), NULL);
        } else {
            MmPrint(("    CcUninitializeCacheMap()\n"));
            CcUninitializeCacheMap(FileObject, NULL, NULL);
        }

        // release resources now.
        // they'll be acquired in UDFCloseFileInfoChain()
        UDF_CHECK_PAGING_IO_RESOURCE(Fcb);
        UDFReleaseResource(&Fcb->FcbNonpaged->FcbResource);
        AcquiredFCB = FALSE;

        if (Fcb->FileInfo->ParentFile) {
            UDF_CHECK_PAGING_IO_RESOURCE(Fcb->FileInfo->ParentFile->Fcb);
            UDFReleaseResource(&Fcb->FileInfo->ParentFile->Fcb->FcbNonpaged->FcbResource);
        } else {
            UDFReleaseResource(&Vcb->VcbResource);
        }
        AcquiredParentFCB = FALSE;
        // close the chain
        ASSERT(AcquiredVcb);
        RC2 = UDFCloseFileInfoChain(IrpContext, Vcb, NextFileInfo, Ccb->TreeLength, TRUE);
        if (NT_SUCCESS(RC))
            RC = RC2;

        Ccb->Flags |= UDF_CCB_CLEANED;

        //  We must clean up the share access at this time, since we may not
        //  get a Close call for awhile if the file was mapped through this
        //  File Object.
        IoRemoveShareAccess(FileObject, &Fcb->ShareAccess);

        Fcb->Header.IsFastIoPossible = UDFIsFastIoPossible(Fcb);

        FileObject->Flags |= FO_CLEANUP_COMPLETE;

try_exit: NOTHING;

    } _SEH2_FINALLY {

        if (AcquiredFCB) {
            UDF_CHECK_PAGING_IO_RESOURCE(Fcb);
            UDFReleaseResource(&Fcb->FcbNonpaged->FcbResource);
        }

        if (AcquiredParentFCB) {
            if (Fcb->FileInfo->ParentFile) {
                UDF_CHECK_PAGING_IO_RESOURCE(Fcb->FileInfo->ParentFile->Fcb);
                UDFReleaseResource(&Fcb->FileInfo->ParentFile->Fcb->FcbNonpaged->FcbResource);
            } else {
                UDFReleaseResource(&Vcb->VcbResource);
            }
        }

        if (AcquiredVcb) {
            UDFReleaseResource(&Vcb->VcbResource);
            AcquiredVcb = FALSE;
        }

        if (SendUnlockNotification) {

            FsRtlNotifyVolumeEvent(FileObject, FSRTL_VOLUME_UNLOCK);
        }

        if (!_SEH2_AbnormalTermination()) {

                UDFCompleteRequest(IrpContext, Irp, RC);
        }

    } _SEH2_END; // end of "__finally" processing
    return(RC);
} // end UDFCommonCleanup()

/*
    This routine walks through the tree to RootDir &
    calls UDFCloseFile__() for each file instance
    imho, Useful feature
 */
NTSTATUS
UDFCloseFileInfoChain(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PUDF_FILE_INFO fi,
    IN ULONG TreeLength,
    IN BOOLEAN VcbAcquired
    )
{
    PUDF_FILE_INFO ParentFI;
    PFCB Fcb;
    PFCB ParentFcb = NULL;
    NTSTATUS RC = STATUS_SUCCESS;
    NTSTATUS RC2;

    // we can't process Tree until we can acquire Vcb
    if (!VcbAcquired)
        UDFAcquireResourceShared(&(Vcb->VcbResource),TRUE);

    AdPrint(("UDFCloseFileInfoChain\n"));
    for(; TreeLength && fi; TreeLength--) {

        // close parent chain (if any)
        // if we started path parsing not from RootDir on Create,
        // we would never get RootDir here
        ValidateFileInfo(fi);

        // acquire parent
        if ((ParentFI = fi->ParentFile)) {
            ParentFcb = fi->Fcb->ParentFcb;
            ASSERT(ParentFcb);
            UDF_CHECK_PAGING_IO_RESOURCE(ParentFcb);
            UDFAcquireResourceExclusive(&ParentFcb->FcbNonpaged->FcbResource, TRUE);
            ASSERT_FCB(ParentFcb);
        } else {
            AdPrint(("Acquiring VCB...\n"));
            UDFAcquireResourceShared(&Vcb->VcbResource, TRUE);
            AdPrint(("Done\n"));
        }
        // acquire current file/dir
        // we must assure that no more threads try to reuse this object
        if ((Fcb = fi->Fcb)) {
            UDF_CHECK_PAGING_IO_RESOURCE(Fcb);
            UDFAcquireResourceExclusive(&Fcb->FcbNonpaged->FcbResource, TRUE);
            ASSERT(Fcb->FcbReference >= fi->RefCount);
            RC2 = UDFCloseFile__(IrpContext, Vcb, fi);
            if (!NT_SUCCESS(RC2))
                RC = RC2;
            ASSERT(Fcb->FcbReference > fi->RefCount);
            UDF_CHECK_PAGING_IO_RESOURCE(Fcb);
            UDFReleaseResource(&Fcb->FcbNonpaged->FcbResource);
        } else {
            BrutePoint();
            RC2 = UDFCloseFile__(IrpContext, Vcb, fi);
            if (!NT_SUCCESS(RC2))
                RC = RC2;
        }

        if (ParentFI) {
            UDF_CHECK_PAGING_IO_RESOURCE(ParentFcb);
            UDFReleaseResource(&ParentFcb->FcbNonpaged->FcbResource);
        } else {
            UDFReleaseResource(&Vcb->VcbResource);
        }
        fi = ParentFI;
    }

    if (!VcbAcquired)
        UDFReleaseResource(&Vcb->VcbResource);

    return RC;

} // end UDFCloseFileInfoChain()

VOID
UDFAutoUnlock (
    IN PVCB Vcb
    )
{
    KIRQL SavedIrql;

    //  Unlock the volume.
 
    IoAcquireVpbSpinLock( &SavedIrql );

    ClearFlag(Vcb->Vpb->Flags, VPB_LOCKED | VPB_DIRECT_WRITES_ALLOWED);
    ClearFlag(Vcb->VcbState, VCB_STATE_LOCKED);
    Vcb->VolumeLockFileObject = NULL;

    IoReleaseVpbSpinLock( SavedIrql );
}
