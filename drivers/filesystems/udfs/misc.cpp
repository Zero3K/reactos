////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*

 File: Misc.cpp

 Module: UDF File System Driver (Kernel mode execution only)

 Description:
   This file contains some miscellaneous support routines.

*/

#include            "udffs.h"
// define the file specific bug-check id
#define         UDF_BUG_CHECK_ID                UDF_FILE_MISC

//  The following constant is the maximum number of ExWorkerThreads that we
//  will allow to be servicing a particular target device at any one time.

#define FSP_PER_DEVICE_THRESHOLD         (2)

/*

 Function: UDFInitializeZones()

 Description:
   Allocates some memory for global zones used to allocate FSD structures.
   Either all memory will be allocated or we will back out gracefully.

 Expected Interrupt Level (for execution) :

  IRQL_PASSIVE_LEVEL

 Return Value: STATUS_SUCCESS/Error

*/
NTSTATUS
UDFInitializeZones(VOID)
{
    NTSTATUS RC = STATUS_UNSUCCESSFUL;

    _SEH2_TRY {

        // determine memory requirements
        switch (MmQuerySystemSize()) {
        case MmMediumSystem:
            UdfData.MaxDelayedCloseCount = 32;
            UdfData.MinDelayedCloseCount = 8;
            break;
        case MmLargeSystem:
            UdfData.MaxDelayedCloseCount = 72;
            UdfData.MinDelayedCloseCount = 18;
            break;
        case MmSmallSystem:
        default:
            UdfData.MaxDelayedCloseCount = 10;
            UdfData.MinDelayedCloseCount = 2;
        }

        ExInitializeNPagedLookasideList(&UdfData.IrpContextLookasideList,
                                        NULL,
                                        NULL,
                                        POOL_NX_ALLOCATION | POOL_RAISE_IF_ALLOCATION_FAILURE,
                                        sizeof(IRP_CONTEXT),
                                        TAG_IRP_CONTEXT,
                                        0);

        // TODO: move to Paged?
        ExInitializeNPagedLookasideList(&UdfData.ObjectNameLookasideList,
                                        NULL,
                                        NULL,
                                        POOL_NX_ALLOCATION | POOL_RAISE_IF_ALLOCATION_FAILURE,
                                        sizeof(UDFObjectName),
                                        TAG_OBJECT_NAME,
                                        0);

        ExInitializeNPagedLookasideList(&UdfData.NonPagedFcbLookasideList,
                                        NULL,
                                        NULL,
                                        POOL_NX_ALLOCATION | POOL_RAISE_IF_ALLOCATION_FAILURE,
                                        sizeof(FCB),
                                        TAG_FCB_NONPAGED,
                                        0);

        ExInitializeNPagedLookasideList(&UdfData.UDFNonPagedFcbLookasideList,
                                        NULL,
                                        NULL,
                                        POOL_NX_ALLOCATION | POOL_RAISE_IF_ALLOCATION_FAILURE,
                                        sizeof(FCB_NONPAGED),
                                        TAG_FCB_NONPAGED,
                                        0);

        ExInitializePagedLookasideList(&UdfData.UDFFcbIndexLookasideList,
                                       NULL,
                                       NULL,
                                       POOL_NX_ALLOCATION | POOL_RAISE_IF_ALLOCATION_FAILURE,
                                       sizeof(FCB), //TODO:
                                       TAG_FCB_NONPAGED,
                                       0);

        ExInitializePagedLookasideList(&UdfData.UDFFcbDataLookasideList,
                                       NULL,
                                       NULL,
                                       POOL_NX_ALLOCATION | POOL_RAISE_IF_ALLOCATION_FAILURE,
                                       sizeof(FCB), //TODO:
                                       TAG_FCB_NONPAGED,
                                       0);

        ExInitializePagedLookasideList(&UdfData.CcbLookasideList,
                                        NULL,
                                        NULL,
                                        POOL_NX_ALLOCATION | POOL_RAISE_IF_ALLOCATION_FAILURE,
                                        sizeof(CCB),
                                        TAG_CCB,
                                        0);

        try_return(RC = STATUS_SUCCESS);

try_exit:   NOTHING;

    } _SEH2_FINALLY {
        if (!NT_SUCCESS(RC)) {
            // invoke the destroy routine now ...
            UDFDestroyZones();
        } else {
            // mark the fact that we have allocated zones ...
            SetFlag(UdfData.Flags, UDF_DATA_FLAGS_ZONES_INITIALIZED);
        }
    } _SEH2_END;

    return(RC);
}


/*************************************************************************
*
* Function: UDFDestroyZones()
*
* Description:
*   Free up the previously allocated memory. NEVER do this once the
*   driver has been successfully loaded.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: None
*
*************************************************************************/
VOID UDFDestroyZones(VOID)
{
    ExDeleteNPagedLookasideList(&UdfData.IrpContextLookasideList);
    ExDeleteNPagedLookasideList(&UdfData.ObjectNameLookasideList);
    ExDeleteNPagedLookasideList(&UdfData.NonPagedFcbLookasideList);

    ExDeletePagedLookasideList(&UdfData.CcbLookasideList);
}


/*************************************************************************
*
* Function: UDFIsIrpTopLevel()
*
* Description:
*   Helps the FSD determine who the "top level" caller is for this
*   request. A request can originate directly from a user process
*   (in which case, the "top level" will be NULL when this routine
*   is invoked), OR the user may have originated either from the NT
*   Cache Manager/VMM ("top level" may be set), or this could be a
*   recursion into our code in which we would have set the "top level"
*   field the last time around.
*
* Expected Interrupt Level (for execution) :
*
*  whatever level a particular dispatch routine is invoked at.
*
* Return Value: TRUE/FALSE (TRUE if top level was NULL when routine invoked)
*
*************************************************************************/
BOOLEAN
__fastcall
UDFIsIrpTopLevel(
    PIRP            Irp)            // the IRP sent to our dispatch routine
{
    if (!IoGetTopLevelIrp()) {
        // OK, so we can set ourselves to become the "top level" component
        IoSetTopLevelIrp(Irp);
        return TRUE;
    }
    return FALSE;
}


/*************************************************************************
*
* Function: UDFExceptionFilter()
*
* Description:
*   This routines allows the driver to determine whether the exception
*   is an "allowed" exception i.e. one we should not-so-quietly consume
*   ourselves, or one which should be propagated onwards in which case
*   we will most likely bring down the machine.
*
*   This routine employs the services of FsRtlIsNtstatusExpected(). This
*   routine returns a BOOLEAN result. A RC of FALSE will cause us to return
*   EXCEPTION_CONTINUE_SEARCH which will probably cause a panic.
*   The FsRtl.. routine returns FALSE iff exception values are (currently) :
*       STATUS_DATATYPE_MISALIGNMENT    ||  STATUS_ACCESS_VIOLATION ||
*       STATUS_ILLEGAL_INSTRUCTION  ||  STATUS_INSTRUCTION_MISALIGNMENT
*
* Expected Interrupt Level (for execution) :
*
*  ?
*
* Return Value: EXCEPTION_EXECUTE_HANDLER/EXECEPTION_CONTINUE_SEARCH
*
*************************************************************************/
long
UDFExceptionFilter(
    PIRP_CONTEXT IrpContext,
    PEXCEPTION_POINTERS PtrExceptionPointers
    )
{
    long                            ReturnCode = EXCEPTION_EXECUTE_HANDLER;
    NTSTATUS                        ExceptionCode = STATUS_SUCCESS;
#if defined UDF_DBG || defined PRINT_ALWAYS
    ULONG i;

    UDFPrint(("UDFExceptionFilter\n"));
    UDFPrint(("    Ex. Code: %x\n",PtrExceptionPointers->ExceptionRecord->ExceptionCode));
    UDFPrint(("    Ex. Addr: %x\n",PtrExceptionPointers->ExceptionRecord->ExceptionAddress));
    UDFPrint(("    Ex. Flag: %x\n",PtrExceptionPointers->ExceptionRecord->ExceptionFlags));
    UDFPrint(("    Ex. Pnum: %x\n",PtrExceptionPointers->ExceptionRecord->NumberParameters));
    for(i=0;i<PtrExceptionPointers->ExceptionRecord->NumberParameters;i++) {
        UDFPrint(("       %x\n",PtrExceptionPointers->ExceptionRecord->ExceptionInformation[i]));
    }
#ifdef _X86_
    UDFPrint(("Exception context:\n"));
    if (PtrExceptionPointers->ContextRecord->ContextFlags & CONTEXT_INTEGER) {
        UDFPrint(("EAX=%8.8x   ",PtrExceptionPointers->ContextRecord->Eax));
        UDFPrint(("EBX=%8.8x   ",PtrExceptionPointers->ContextRecord->Ebx));
        UDFPrint(("ECX=%8.8x   ",PtrExceptionPointers->ContextRecord->Ecx));
        UDFPrint(("EDX=%8.8x\n",PtrExceptionPointers->ContextRecord->Edx));

        UDFPrint(("ESI=%8.8x   ",PtrExceptionPointers->ContextRecord->Esi));
        UDFPrint(("EDI=%8.8x   ",PtrExceptionPointers->ContextRecord->Edi));
    }
    if (PtrExceptionPointers->ContextRecord->ContextFlags & CONTEXT_CONTROL) {
        UDFPrint(("EBP=%8.8x   ",PtrExceptionPointers->ContextRecord->Esp));
        UDFPrint(("ESP=%8.8x\n",PtrExceptionPointers->ContextRecord->Ebp));

        UDFPrint(("EIP=%8.8x\n",PtrExceptionPointers->ContextRecord->Eip));
    }
//    UDFPrint(("Flags: %s %s    ",PtrExceptionPointers->ContextRecord->Eip));
#endif //_X86_

#endif // UDF_DBG

    // figure out the exception code
    ExceptionCode = PtrExceptionPointers->ExceptionRecord->ExceptionCode;

    if ((ExceptionCode == STATUS_IN_PAGE_ERROR) && (PtrExceptionPointers->ExceptionRecord->NumberParameters >= 3)) {
        ExceptionCode = PtrExceptionPointers->ExceptionRecord->ExceptionInformation[2];
    }

    if (IrpContext) {
        IrpContext->ExceptionStatus = ExceptionCode;
    }

    // check if we should propagate this exception or not
    if (!(FsRtlIsNtstatusExpected(ExceptionCode))) {

        // better free up the IrpContext now ...
        if (IrpContext) {
            UDFPrint(("    UDF Driver internal error\n"));
            BrutePoint();
        } else {
            // we are not ok, propagate this exception.
            //  NOTE: we will bring down the machine ...
            ReturnCode = EXCEPTION_CONTINUE_SEARCH;
        }
    }


    // return the appropriate code
    return(ReturnCode);
} // end UDFExceptionFilter()


/*************************************************************************
*
* Function: UDFExceptionHandler()
*
* Description:
*   One of the routines in the FSD or in the modules we invoked encountered
*   an exception. We have decided that we will "handle" the exception.
*   Therefore we will prevent the machine from a panic ...
*   You can do pretty much anything you choose to in your commercial
*   driver at this point to ensure a graceful exit. In the UDF
*   driver, We shall simply free up the IrpContext (if any), set the
*   error code in the IRP and complete the IRP at this time ...
*
* Expected Interrupt Level (for execution) :
*
*  ?
*
* Return Value: Error code
*
*************************************************************************/
NTSTATUS
UDFProcessException(
    PIRP_CONTEXT IrpContext,
    PIRP             Irp
    )
{
//    NTSTATUS                        RC;
    NTSTATUS            ExceptionCode = STATUS_INSUFFICIENT_RESOURCES;
    PDEVICE_OBJECT      Device;
    PVPB Vpb;
    PETHREAD Thread;

    UDFPrint(("UDFExceptionHandler \n"));

//    ASSERT(Irp);

    if (!Irp) {
        UDFPrint(("  !Irp, return\n"));
        ASSERT(!IrpContext);
        return ExceptionCode;
    }
    // If it was a queued close (or something like this) then we need not
    // completing it because of MUST_SUCCEED requirement.

    if (IrpContext) {
        ExceptionCode = IrpContext->ExceptionStatus;
        // Free irp context here
//        UDFReleaseIrpContext(IrpContext);
    } else {
        UDFPrint(("  complete Irp and return\n"));
        // must be insufficient resources ...?
        ExceptionCode = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Status = ExceptionCode;
        Irp->IoStatus.Information = 0;
        // complete the IRP
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return ExceptionCode;
    }

    //  Check if we are posting this request.  One of the following must be true
    //  if we are to post a request.
    //
    //      - Status code is STATUS_CANT_WAIT and the request is asynchronous
    //          or we are forcing this to be posted.
    //
    //      - Status code is STATUS_VERIFY_REQUIRED and we are at APC level
    //          or higher.  Can't wait for IO in the verify path in this case.
    //
    //  Set the MORE_PROCESSING flag in the IrpContext to keep if from being
    //  deleted if this is a retryable condition.

    if (ExceptionCode == STATUS_VERIFY_REQUIRED) {
        if (KeGetCurrentIrql() >= APC_LEVEL) {
            UDFPrint(("  use UDFPostRequest()\n"));
            ExceptionCode = UDFPostRequest(IrpContext, Irp);
        }
    }

    //  If we posted the request or our caller will retry then just return here.
    if ((ExceptionCode == STATUS_PENDING) ||
        (ExceptionCode == STATUS_CANT_WAIT)) {

        UDFPrint(("  STATUS_PENDING/STATUS_CANT_WAIT, return\n"));
        return ExceptionCode;
    }

    //  Store this error into the Irp for posting back to the Io system.
    Irp->IoStatus.Status = ExceptionCode;
    if (IoIsErrorUserInduced( ExceptionCode )) {

        //  Check for the various error conditions that can be caused by,
        //  and possibly resolved my the user.
        if (ExceptionCode == STATUS_VERIFY_REQUIRED) {

            //  Now we are at the top level file system entry point.
            //
            //  If we have already posted this request then the device to
            //  verify is in the original thread.  Find this via the Irp.
            Device = IoGetDeviceToVerify( Irp->Tail.Overlay.Thread );
            IoSetDeviceToVerify( Irp->Tail.Overlay.Thread, NULL );

            //  If there is no device in that location then check in the
            //  current thread.
            if (Device == NULL) {

                Device = IoGetDeviceToVerify( PsGetCurrentThread() );
                IoSetDeviceToVerify( PsGetCurrentThread(), NULL );

                ASSERT( Device != NULL );

                //  Let's not BugCheck just because the driver screwed up.
                if (Device == NULL) {

                    UDFPrint(("  Device == NULL, return\n"));
                    ExceptionCode = STATUS_DRIVER_INTERNAL_ERROR;
                    Irp->IoStatus.Status = ExceptionCode;
                    Irp->IoStatus.Information = 0;
                    // complete the IRP
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);

                    UDFCleanupIrpContext(IrpContext);

                    return ExceptionCode;
                }
            }

            UDFPrint(("  use UDFPerformVerify()\n"));
            //  UDFPerformVerify() will do the right thing with the Irp.
            //  If we return STATUS_CANT_WAIT then the current thread
            //  can retry the request.
            return UDFPerformVerify( IrpContext, Irp, Device );
        }

        //
        //  The other user induced conditions generate an error unless
        //  they have been disabled for this request.
        //

        if (FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_DISABLE_POPUPS)) {

            UDFPrint(("  DISABLE_POPUPS, complete Irp and return\n"));

            UDFCompleteRequest(IrpContext, Irp, ExceptionCode);
            return ExceptionCode;

        } else {

            //  Generate a pop-up
            if (IoGetCurrentIrpStackLocation( Irp )->FileObject != NULL) {

                Vpb = IoGetCurrentIrpStackLocation( Irp )->FileObject->Vpb;
            } else {

                Vpb = NULL;
            }
            //  The device to verify is either in my thread local storage
            //  or that of the thread that owns the Irp.
            Thread = Irp->Tail.Overlay.Thread;
            Device = IoGetDeviceToVerify( Thread );

            if (Device == NULL) {

                Thread = PsGetCurrentThread();
                Device = IoGetDeviceToVerify( Thread );
                ASSERT( Device != NULL );

                //  Let's not BugCheck just because the driver screwed up.
                if (Device == NULL) {
                    UDFPrint(("  Device == NULL, return(2)\n"));
                    Irp->IoStatus.Status = ExceptionCode;
                    Irp->IoStatus.Information = 0;
                    // complete the IRP
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);

                    UDFCleanupIrpContext(IrpContext);

                    return ExceptionCode;
                }
            }

            //  This routine actually causes the pop-up.  It usually
            //  does this by queuing an APC to the callers thread,
            //  but in some cases it will complete the request immediately,
            //  so it is very important to IoMarkIrpPending() first.
            IoMarkIrpPending( Irp );
            IoRaiseHardError( Irp, Vpb, Device );

            //  We will be handing control back to the caller here, so
            //  reset the saved device object.

            UDFPrint(("  use IoSetDeviceToVerify()\n"));
            IoSetDeviceToVerify( Thread, NULL );
            //  The Irp will be completed by Io or resubmitted.  In either
            //  case we must clean up the IrpContext here.

            UDFCleanupIrpContext(IrpContext);
            return STATUS_PENDING;
        }
    }

    // If it was a normal request from IOManager then complete it
    if (Irp) {
        UDFPrint(("  complete Irp\n"));
        // set the error code in the IRP
        Irp->IoStatus.Status = ExceptionCode;
        Irp->IoStatus.Information = 0;

        // complete the IRP
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        UDFCleanupIrpContext(IrpContext);
    }

    UDFPrint(("  return from exception handler with code %x\n", ExceptionCode));
    return(ExceptionCode);
} // end UDFExceptionHandler()

/*************************************************************************
*
* Function: UDFLogEvent()
*
* Description:
*   Log a message in the NT Event Log. This is a rather simplistic log
*   methodology since we can potentially utilize the event log to
*   provide a lot of information to the user (and you should too!)
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: None
*
*************************************************************************/
VOID
UDFLogEvent(
    NTSTATUS UDFEventLogId,      // the UDF private message id
    NTSTATUS RC)                 // any NT error code we wish to log ...
{
    _SEH2_TRY {

        // Implement a call to IoAllocateErrorLogEntry() followed by a call
        // to IoWriteErrorLogEntry(). You should note that the call to IoWriteErrorLogEntry()
        // will free memory for the entry once the write completes (which in actuality
        // is an asynchronous operation).

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        // nothing really we can do here, just do not wish to crash ...
        NOTHING;
    } _SEH2_END;

    return;
} // end UDFLogEvent()


/*************************************************************************
*
* Function: UDFAllocateObjectName()
*
* Description:
*   Allocate a new ObjectName structure to represent an open on-disk object.
*   Also initialize the ObjectName structure to NULL.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: A pointer to the ObjectName structure OR NULL.
*
*************************************************************************/
PtrUDFObjectName
UDFAllocateObjectName(VOID)
{
    PtrUDFObjectName NewObjectName = NULL;

    NewObjectName = (PtrUDFObjectName)ExAllocateFromNPagedLookasideList(&UdfData.ObjectNameLookasideList);

    if (!NewObjectName) {
        return NULL;
    }

    // zero out the allocated memory block
    RtlZeroMemory(NewObjectName, sizeof(UDFObjectName));

    // set up some fields ...
    NewObjectName->NodeIdentifier.NodeTypeCode = UDF_NODE_TYPE_OBJECT_NAME;
    NewObjectName->NodeIdentifier.NodeByteSize = sizeof(UDFObjectName);

    return NewObjectName;
} // end UDFAllocateObjectName()


/*************************************************************************
*
* Function: UDFReleaseObjectName()
*
* Description:
*   Deallocate a previously allocated structure.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: None
*
*************************************************************************/
VOID
UDFReleaseObjectName(
    PtrUDFObjectName ObjectName)
{
    ASSERT(ObjectName);

    ExFreeToNPagedLookasideList(&UdfData.ObjectNameLookasideList, ObjectName);

    return;
} // end UDFReleaseObjectName()


/*************************************************************************
*
* Function: UDFCreateCcb()
*
* Description:
*   Allocate a new CCB structure to represent an open on-disk object.
*   Also initialize the CCB structure to NULL.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: A pointer to the CCB structure OR NULL.
*
*************************************************************************/
PCCB
UDFCreateCcb()
{
    PCCB NewCcb = NULL;

    NewCcb = (PCCB)ExAllocateFromPagedLookasideList(&UdfData.CcbLookasideList);

    if (!NewCcb) {
        return NULL;
    }

    // zero out the allocated memory block
    RtlZeroMemory(NewCcb, sizeof(CCB));

    // set up some fields ...
    NewCcb->NodeIdentifier.NodeTypeCode = UDF_NODE_TYPE_CCB;
    NewCcb->NodeIdentifier.NodeByteSize = sizeof(CCB);

    return NewCcb;
} // end UDFCreateCcb()


/*************************************************************************
*
* Function: UDFReleaseCCB()
*
* Description:
*   Deallocate a previously allocated structure.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: None
*
*************************************************************************/
VOID
UDFReleaseCCB(
    PCCB Ccb
    )
{
    ASSERT(Ccb);

    ExFreeToPagedLookasideList(&UdfData.CcbLookasideList, Ccb);

} // end UDFReleaseCCB()

/*
  Function: UDFCleanupCCB()

  Description:
    Cleanup and deallocate a previously allocated structure.

  Expected Interrupt Level (for execution) :

   IRQL_PASSIVE_LEVEL

  Return Value: None

*/
VOID
UDFDeleteCcb(
    PCCB Ccb
)
{
    ASSERT(Ccb);
    if (!Ccb) return; // probably, we havn't allocated it...
    ASSERT(Ccb->NodeIdentifier.NodeTypeCode == UDF_NODE_TYPE_CCB);

    _SEH2_TRY {
        if (Ccb->Fcb) {
            UDFTouch(&(Ccb->Fcb->CcbListResource));
            UDFAcquireResourceExclusive(&(Ccb->Fcb->CcbListResource),TRUE);
            RemoveEntryList(&(Ccb->NextCCB));
            UDFReleaseResource(&(Ccb->Fcb->CcbListResource));
        } else {
            BrutePoint();
        }

        if (Ccb->DirectorySearchPattern) {
            if (Ccb->DirectorySearchPattern->Buffer) {
                MyFreePool__(Ccb->DirectorySearchPattern->Buffer);
                Ccb->DirectorySearchPattern->Buffer = NULL;
            }

            MyFreePool__(Ccb->DirectorySearchPattern);
            Ccb->DirectorySearchPattern = NULL;
        }

        UDFReleaseCCB(Ccb);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        BrutePoint();
    } _SEH2_END;
} // end UDFCleanUpCCB()

/*************************************************************************
*
* Function: UDFCreateIrpContext()
*
* Description:
*   The UDF FSD creates an IRP context for each request received. This
*   routine simply allocates (and initializes to NULL) a UDFIrpContext
*   structure.
*   Most of the fields in the context structure are then initialized here.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: A pointer to the IrpContext structure OR NULL.
*
*************************************************************************/
PIRP_CONTEXT
UDFCreateIrpContext(
    PIRP           Irp,
    PDEVICE_OBJECT PtrTargetDeviceObject
    )
{
    ASSERT(Irp);

    PIRP_CONTEXT NewIrpContext = NULL;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);

    //  The only operations a filesystem device object should ever receive
    //  are create/teardown of fsdo handles and operations which do not
    //  occur in the context of fileobjects (i.e., mount).

    if (UdfDeviceIsFsdo(IrpSp->DeviceObject)) {

        if (IrpSp->FileObject != NULL &&
            IrpSp->MajorFunction != IRP_MJ_CREATE &&
            IrpSp->MajorFunction != IRP_MJ_CLEANUP &&
            IrpSp->MajorFunction != IRP_MJ_CLOSE) {

            ExRaiseStatus(STATUS_INVALID_DEVICE_REQUEST);
        }

        NT_ASSERT( IrpSp->FileObject != NULL ||

                (IrpSp->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL &&
                 IrpSp->MinorFunction == IRP_MN_USER_FS_REQUEST &&
                 IrpSp->Parameters.FileSystemControl.FsControlCode == FSCTL_INVALIDATE_VOLUMES) ||

                (IrpSp->MajorFunction == IRP_MJ_FILE_SYSTEM_CONTROL &&
                 IrpSp->MinorFunction == IRP_MN_MOUNT_VOLUME ) ||

                IrpSp->MajorFunction == IRP_MJ_SHUTDOWN );
    }

    NewIrpContext = (PIRP_CONTEXT)ExAllocateFromNPagedLookasideList(&UdfData.IrpContextLookasideList);

    if (NewIrpContext == NULL) {
        return NULL;
    }

    // zero out the allocated memory block
    RtlZeroMemory(NewIrpContext, sizeof(IRP_CONTEXT));

    // Set the proper node type code and node byte size
    NewIrpContext->NodeIdentifier.NodeTypeCode = UDF_NODE_TYPE_IRP_CONTEXT;
    NewIrpContext->NodeIdentifier.NodeByteSize = sizeof(IRP_CONTEXT);

    // Set the originating Irp field
    NewIrpContext->Irp = Irp;

    NewIrpContext->RealDevice = PtrTargetDeviceObject;

    // TODO: fix
    if (false && IrpSp->FileObject != NULL) {

        PFILE_OBJECT FileObject = IrpSp->FileObject;

        ASSERT(FileObject->DeviceObject == PtrTargetDeviceObject);
        NewIrpContext->RealDevice = FileObject->DeviceObject;

        //
        //  See if the request is Write Through. Look for both FileObjects opened
        //  as write through, and non-cached requests with the SL_WRITE_THROUGH flag set.
        //
        //  The latter can only originate from kernel components. (Note - NtWriteFile()
        //  does redundantly set the SL_W_T flag for all requests it issues on write
        //  through file objects)
        //

        if (IsFileWriteThrough( FileObject, NewIrpContext->Vcb ) ||
            ( (IrpSp->MajorFunction == IRP_MJ_WRITE) &&
              BooleanFlagOn( Irp->Flags, IRP_NOCACHE) &&
              BooleanFlagOn( IrpSp->Flags, SL_WRITE_THROUGH))) {

            SetFlag(NewIrpContext->Flags, IRP_CONTEXT_FLAG_WRITE_THROUGH);
        }
    }

    if (!UdfDeviceIsFsdo(IrpSp->DeviceObject)) {

        NewIrpContext->Vcb = (PVCB)IrpSp->DeviceObject->DeviceExtension;
    }

    //  Major/Minor Function codes
    NewIrpContext->MajorFunction = IrpSp->MajorFunction;
    NewIrpContext->MinorFunction = IrpSp->MinorFunction;

    // Often, a FSD cannot honor a request for asynchronous processing
    // of certain critical requests. For example, a "close" request on
    // a file object can typically never be deferred. Therefore, do not
    // be surprised if sometimes our FSD (just like all other FSD
    // implementations on the Windows NT system) has to override the flag
    // below.
    if (IrpSp->FileObject == NULL) {
        NewIrpContext->Flags |= IRP_CONTEXT_FLAG_WAIT;
    } else {
        if (IoIsOperationSynchronous(Irp)) {
            NewIrpContext->Flags |= IRP_CONTEXT_FLAG_WAIT;
        }
    }

    // Are we top-level ? This information is used by the dispatching code
    // later (and also by the FSD dispatch routine)
    if (IoGetTopLevelIrp() != Irp) {
        // We are not top-level. Note this fact in the context structure
        SetFlag(NewIrpContext->Flags, UDF_IRP_CONTEXT_NOT_TOP_LEVEL);
    }

    return NewIrpContext;
} // end UDFCreateIrpContext()


/*************************************************************************
*
* Function: UDFCleanupIrpContext()
*
* Description:
*   Deallocate a previously allocated structure.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: None
*
*************************************************************************/
VOID
UDFCleanupIrpContext(
    _In_ PIRP_CONTEXT IrpContext,
    _In_ BOOLEAN Post
    )
{
    ASSERT(IrpContext);

    if (!FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_ON_STACK)) {

        ExFreeToNPagedLookasideList(&UdfData.IrpContextLookasideList, IrpContext);
    }
} // end UDFCleanupIrpContext()

_When_(RaiseOnError || return, _At_(Fcb->FileLock, _Post_notnull_))
_When_(RaiseOnError, _At_(IrpContext, _Pre_notnull_))
BOOLEAN
UDFCreateFileLock (
    _In_opt_ PIRP_CONTEXT IrpContext,
    _Inout_ PFCB Fcb,
    _In_ BOOLEAN RaiseOnError
    )

/*++

Routine Description:

    This routine is called when we want to attach a file lock structure to the
    given Fcb.  It is possible the file lock is already attached.

    This routine is sometimes called from the fast path and sometimes in the
    Irp-based path.  We don't want to raise in the fast path, just return FALSE.

Arguments:

    Fcb - This is the Fcb to create the file lock for.

    RaiseOnError - If TRUE, we will raise on an allocation failure.  Otherwise we
        return FALSE on an allocation failure.

Return Value:

    BOOLEAN - TRUE if the Fcb has a filelock, FALSE otherwise.

--*/

{
    BOOLEAN Result = TRUE;
    PFILE_LOCK FileLock;

    PAGED_CODE();

    ASSERT(RaiseOnError == FALSE);

    //  Lock the Fcb and check if there is really any work to do.

    //TODO: impl
    //UDFLockFcb( IrpContext, Fcb );

    if (Fcb->FileLock != NULL) {

        //TODO: impl
        //UDFUnlockFcb( IrpContext, Fcb );
        return TRUE;
    }

    Fcb->FileLock = FileLock = FsRtlAllocateFileLock(NULL, NULL);

    //TODO: impl
    //UDFUnlockFcb( IrpContext, Fcb );

    //  Return or raise as appropriate.
    if (FileLock == NULL) {
         
        if (RaiseOnError) {

            NT_ASSERT(ARGUMENT_PRESENT(IrpContext));

            UDFRaiseStatus(IrpContext, STATUS_INSUFFICIENT_RESOURCES);
        }

        Result = FALSE;
    }

    return Result;
}

/*************************************************************************
*
* Function: UDFPostRequest()
*
* Description:
*   Queue up a request for deferred processing (in the context of a system
*   worker thread). The caller must have locked the user buffer (if required)
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: STATUS_PENDING
*
*************************************************************************/
NTSTATUS
UDFPostRequest(
    IN PIRP_CONTEXT IrpContext,
    IN PIRP             Irp
    )
{
    KIRQL SavedIrql;
//    PIO_STACK_LOCATION IrpSp;
    PVCB Vcb;

//    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    // mark the IRP pending if this is not double post
    if (Irp)
        IoMarkIrpPending(Irp);

    Vcb = (PVCB)(IrpContext->RealDevice->DeviceExtension);
    KeAcquireSpinLock(&(Vcb->OverflowQueueSpinLock), &SavedIrql);

    if ( Vcb->PostedRequestCount > FSP_PER_DEVICE_THRESHOLD) {

        //  We cannot currently respond to this IRP so we'll just enqueue it
        //  to the overflow queue on the volume.
        //  Note: we just reuse LIST_ITEM field inside WorkQueueItem, this
        //  doesn't matter to regular processing of WorkItems.
        InsertTailList( &(Vcb->OverflowQueue),
                        &(IrpContext->WorkQueueItem.List) );
        Vcb->OverflowQueueCount++;
        KeReleaseSpinLock( &(Vcb->OverflowQueueSpinLock), SavedIrql );

    } else {

        //  We are going to send this Irp to an ex worker thread so up
        //  the count.
        Vcb->PostedRequestCount++;

        KeReleaseSpinLock( &(Vcb->OverflowQueueSpinLock), SavedIrql );

        // queue up the request
        ExInitializeWorkItem(&(IrpContext->WorkQueueItem), UDFFspDispatch, IrpContext);

        ExQueueWorkItem(&(IrpContext->WorkQueueItem), CriticalWorkQueue);
    }

    // return status pending
    return STATUS_PENDING;
} // end UDFPostRequest()


/*************************************************************************
*
* Function: UDFFspDispatch()
*
* Description:
*   The common dispatch routine invoked in the context of a system worker
*   thread. All we do here is pretty much case off the major function
*   code and invoke the appropriate FSD dispatch routine for further
*   processing.
*
* Expected Interrupt Level (for execution) :
*
*   IRQL PASSIVE_LEVEL
*
* Return Value: None
*
*************************************************************************/
VOID
NTAPI
UDFFspDispatch(
    IN PVOID Context   // actually is a pointer to IRPContext structure
    )
{
    NTSTATUS         RC = STATUS_SUCCESS;
    PIRP_CONTEXT IrpContext = NULL;
    PIRP             Irp = NULL;
    PVCB             Vcb;
    KIRQL            SavedIrql;
    PLIST_ENTRY      Entry;
    BOOLEAN          SpinLock = FALSE;

    // The context must be a pointer to an IrpContext structure
    IrpContext = (PIRP_CONTEXT)Context;

    // Assert that the Context is legitimate
    if ( !IrpContext ||
         (IrpContext->NodeIdentifier.NodeTypeCode != UDF_NODE_TYPE_IRP_CONTEXT) ||
         (IrpContext->NodeIdentifier.NodeByteSize != sizeof(IRP_CONTEXT)) /*||
        !(IrpContext->Irp)*/) {
        UDFPrint(("    Invalid Context\n"));
        BrutePoint();
        return;
    }

    Vcb = (PVCB)(IrpContext->RealDevice->DeviceExtension);
    ASSERT(Vcb);

    UDFPrint(("  *** Thr: %x  ThCnt: %x  QCnt: %x  Started!\n", PsGetCurrentThread(), Vcb->PostedRequestCount, Vcb->OverflowQueueCount));

    while(TRUE) {

        UDFPrint(("    Next IRP\n"));
        FsRtlEnterFileSystem();

        //  Get a pointer to the IRP structure
        // in some cases we can get Zero pointer to Irp
        Irp = IrpContext->Irp;
        // Now, check if the FSD was top level when the IRP was originally invoked
        // and set the thread context (for the worker thread) appropriately
        if (IrpContext->Flags & UDF_IRP_CONTEXT_NOT_TOP_LEVEL) {
            // The FSD is not top level for the original request
            // Set a constant value in TLS to reflect this fact
            IoSetTopLevelIrp((PIRP)FSRTL_FSP_TOP_LEVEL_IRP);
        } else {
            IoSetTopLevelIrp(Irp);
        }

        // Since the FSD routine will now be invoked in the context of this worker
        // thread, we should inform the FSD that it is perfectly OK to block in
        // the context of this thread
        IrpContext->Flags |= IRP_CONTEXT_FLAG_WAIT;

        _SEH2_TRY {

            // Pre-processing has been completed; check the Major Function code value
            // either in the IrpContext (copied from the IRP), or directly from the
            //  IRP itself (we will need a pointer to the stack location to do that),
            //  Then, switch based on the value on the Major Function code
            UDFPrint(("  *** MJ: %x, Thr: %x\n", IrpContext->MajorFunction, PsGetCurrentThread()));
            switch (IrpContext->MajorFunction) {
            case IRP_MJ_CREATE:
                // Invoke the common create routine
                RC = UDFCommonCreate(IrpContext, Irp);
                break;
            case IRP_MJ_READ:
                // Invoke the common read routine
                RC = UDFCommonRead(IrpContext, Irp);
                break;
            case IRP_MJ_WRITE:
                // Invoke the common write routine
                RC = UDFCommonWrite(IrpContext, Irp);
                break;
            case IRP_MJ_CLEANUP:
                // Invoke the common cleanup routine
                RC = UDFCommonCleanup(IrpContext, Irp);
                break;
            case IRP_MJ_CLOSE:
                // Invoke the common close routine
                RC = UDFCommonClose(IrpContext, Irp, TRUE);
                break;
            case IRP_MJ_DIRECTORY_CONTROL:
                // Invoke the common directory control routine
                RC = UDFCommonDirControl(IrpContext, Irp);
                break;
            case IRP_MJ_QUERY_INFORMATION:
                // Invoke the common query information routine
                RC = UDFCommonQueryInfo(IrpContext, Irp);
                break;
            case IRP_MJ_SET_INFORMATION:
                // Invoke the common set information routine
                RC = UDFCommonSetInfo(IrpContext, Irp);
                break;
            case IRP_MJ_QUERY_VOLUME_INFORMATION:
                // Invoke the common query volume routine
                RC = UDFCommonQueryVolInfo(IrpContext, Irp);
                break;
            case IRP_MJ_SET_VOLUME_INFORMATION:
                // Invoke the common set volume routine
                RC = UDFCommonSetVolInfo(IrpContext, Irp);
                break;
            // Continue with the remaining possible dispatch routines below ...
            default:

                UDFPrint(("  unhandled *** MJ: %x, Thr: %x\n", IrpContext->MajorFunction, PsGetCurrentThread()));

                RC = STATUS_INVALID_DEVICE_REQUEST;
                UDFCompleteRequest( IrpContext, Irp, RC );
                break;
            }

            // Note: IrpContext is invalid here
            UDFPrint(("  *** Thr: %x  Done!\n", PsGetCurrentThread()));

        } _SEH2_EXCEPT(UDFExceptionFilter(IrpContext, _SEH2_GetExceptionInformation())) {

            RC = UDFProcessException(IrpContext, Irp);

            UDFLogEvent(UDF_ERROR_INTERNAL_ERROR, RC);
        }  _SEH2_END;

        // Enable preemption
        FsRtlExitFileSystem();

        // Ensure that the "top-level" field is cleared
        IoSetTopLevelIrp(NULL);

        //  If there are any entries on this volume's overflow queue, service
        //  them.
        if (!Vcb) {
            BrutePoint();
            break;
        }

        KeAcquireSpinLock(&(Vcb->OverflowQueueSpinLock), &SavedIrql);
        SpinLock = TRUE;
        if (!Vcb->OverflowQueueCount)
            break;

        Vcb->OverflowQueueCount--;
        Entry = RemoveHeadList(&Vcb->OverflowQueue);
        KeReleaseSpinLock(&(Vcb->OverflowQueueSpinLock), SavedIrql);
        SpinLock = FALSE;

        IrpContext = CONTAINING_RECORD(Entry,
                                          IRP_CONTEXT,
                                          WorkQueueItem.List);
    }

    if (!SpinLock)
        KeAcquireSpinLock(&(Vcb->OverflowQueueSpinLock), &SavedIrql);
    Vcb->PostedRequestCount--;
    KeReleaseSpinLock(&(Vcb->OverflowQueueSpinLock), SavedIrql);

    UDFPrint(("  *** Thr: %x  ThCnt: %x  QCnt: %x  Terminated!\n", PsGetCurrentThread(), Vcb->PostedRequestCount, Vcb->OverflowQueueCount));

    return;
} // end UDFFspDispatch()

typedef ULONG
(*ptrUDFGetParameter)(
    IN PVCB Vcb,
    IN PCWSTR Name,
    IN ULONG DefValue
    );

VOID
UDFUpdateCompatOption(
    PVCB Vcb,
    BOOLEAN Update,
    BOOLEAN UseCfg,
    PCWSTR Name,
    ULONG Flag,
    BOOLEAN Default
    )
{
    ptrUDFGetParameter UDFGetParameter = UseCfg ? UDFGetCfgParameter : UDFGetRegParameter;

    if (UDFGetParameter(Vcb, Name, Update ? ((Vcb->CompatFlags & Flag) ? TRUE : FALSE) : Default)) {
        Vcb->CompatFlags |= Flag;
    } else {
        Vcb->CompatFlags &= ~Flag;
    }
} // end UDFUpdateCompatOption()

VOID
UDFReadRegKeys(
    PVCB Vcb,
    BOOLEAN Update,
    BOOLEAN UseCfg
    )
{
    ULONG mult = 1;
    ptrUDFGetParameter UDFGetParameter = UseCfg ? UDFGetCfgParameter : UDFGetRegParameter;

    Vcb->DefaultRegName = REG_DEFAULT_UNKNOWN;

    // Should we use Extended FE by default ?
    Vcb->UseExtendedFE = (UCHAR)UDFGetParameter(Vcb, REG_USEEXTENDEDFE_NAME,
        Update ? Vcb->UseExtendedFE : FALSE);
    // What type of AllocDescs should we use
    Vcb->DefaultAllocMode = (USHORT)UDFGetParameter(Vcb, REG_DEFALLOCMODE_NAME,
        Update ? Vcb->DefaultAllocMode : ICB_FLAG_AD_SHORT);
    if (Vcb->DefaultAllocMode > ICB_FLAG_AD_LONG) Vcb->DefaultAllocMode = ICB_FLAG_AD_SHORT;

    // FE allocation charge for plain Dirs
    Vcb->FECharge = UDFGetParameter(Vcb, UDF_FE_CHARGE_NAME, Update ? Vcb->FECharge : 0);
    if (!Vcb->FECharge)
        Vcb->FECharge = UDF_DEFAULT_FE_CHARGE;
    // FE allocation charge for Stream Dirs (SDir)
    Vcb->FEChargeSDir = UDFGetParameter(Vcb, UDF_FE_CHARGE_SDIR_NAME,
        Update ? Vcb->FEChargeSDir : 0);
    if (!Vcb->FEChargeSDir)
        Vcb->FEChargeSDir = UDF_DEFAULT_FE_CHARGE_SDIR;
    // How many Deleted entries should contain Directory to make us
    // start packing it.
    Vcb->PackDirThreshold = UDFGetParameter(Vcb, UDF_DIR_PACK_THRESHOLD_NAME,
        Update ? Vcb->PackDirThreshold : 0);
    if (Vcb->PackDirThreshold == 0xffffffff)
        Vcb->PackDirThreshold = UDF_DEFAULT_DIR_PACK_THRESHOLD;

    // Timeouts for FreeSpaceBitMap & TheWholeDirTree flushes
    Vcb->BM_FlushPriod = UDFGetParameter(Vcb, UDF_BM_FLUSH_PERIOD_NAME,
        Update ? Vcb->BM_FlushPriod : 0);
    if (!Vcb->BM_FlushPriod) {
        Vcb->BM_FlushPriod = UDF_DEFAULT_BM_FLUSH_TIMEOUT;
    } else
    if (Vcb->BM_FlushPriod == (ULONG)-1) {
        Vcb->BM_FlushPriod = 0;
    }
    Vcb->Tree_FlushPriod = UDFGetParameter(Vcb, UDF_TREE_FLUSH_PERIOD_NAME,
        Update ? Vcb->Tree_FlushPriod : 0);
    if (!Vcb->Tree_FlushPriod) {
        Vcb->Tree_FlushPriod = UDF_DEFAULT_TREE_FLUSH_TIMEOUT;
    } else
    if (Vcb->Tree_FlushPriod == (ULONG)-1) {
        Vcb->Tree_FlushPriod = 0;
    }
    Vcb->SkipCountLimit = UDFGetParameter(Vcb, UDF_NO_UPDATE_PERIOD_NAME,
        Update ? Vcb->SkipCountLimit : 0);
    if (!Vcb->SkipCountLimit)
        Vcb->SkipCountLimit = -1;

    // The mimimum FileSize increment when we'll decide not to allocate
    // on-disk space.
    Vcb->SparseThreshold = UDFGetParameter(Vcb, UDF_SPARSE_THRESHOLD_NAME,
        Update ? Vcb->SparseThreshold : 0);
    if (!Vcb->SparseThreshold)
        Vcb->SparseThreshold = UDF_DEFAULT_SPARSE_THRESHOLD;
    // This option is used to VERIFY all the data written. It decreases performance
    Vcb->VerifyOnWrite = UDFGetParameter(Vcb, UDF_VERIFY_ON_WRITE_NAME,
        Update ? Vcb->VerifyOnWrite : FALSE) ? TRUE : FALSE;

    // Should we update AttrFileTime on Attr changes
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_UPDATE_TIMES_ATTR, UDF_VCB_IC_UPDATE_ATTR_TIME, FALSE);
    // Should we update ModifyFileTime on Writes changes
    // It also affects ARCHIVE bit setting on write operations
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_UPDATE_TIMES_MOD, UDF_VCB_IC_UPDATE_MODIFY_TIME, FALSE);
    // Should we update AccessFileTime on Exec & so on.
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_UPDATE_TIMES_ACCS, UDF_VCB_IC_UPDATE_ACCESS_TIME, FALSE);
    // Should we update Archive bit
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_UPDATE_ATTR_ARCH, UDF_VCB_IC_UPDATE_ARCH_BIT, FALSE);
    // Should we update Dir's Times & Attrs on Modify
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_UPDATE_DIR_TIMES_ATTR_W, UDF_VCB_IC_UPDATE_DIR_WRITE, FALSE);
    // Should we update Dir's Times & Attrs on Access
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_UPDATE_DIR_TIMES_ATTR_R, UDF_VCB_IC_UPDATE_DIR_READ, FALSE);
    // Should we allow user to write into Read-Only Directory
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_ALLOW_WRITE_IN_RO_DIR, UDF_VCB_IC_WRITE_IN_RO_DIR, TRUE);
    // Should we allow user to change Access Time for unchanged Directory
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_ALLOW_UPDATE_TIMES_ACCS_UCHG_DIR, UDF_VCB_IC_UPDATE_UCHG_DIR_ACCESS_TIME, FALSE);
    // Should we record Allocation Descriptors in W2k-compatible form
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_W2K_COMPAT_ALLOC_DESCS, UDF_VCB_IC_W2K_COMPAT_ALLOC_DESCS, TRUE);
    // Should we read LONG_ADs with invalid PartitionReferenceNumber (generated by Nero Instant Burner)
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_INSTANT_COMPAT_ALLOC_DESCS, UDF_VCB_IC_INSTANT_COMPAT_ALLOC_DESCS, TRUE);
    // Should we make a copy of VolumeLabel in LVD
    // usually only PVD is updated
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_W2K_COMPAT_VLABEL, UDF_VCB_IC_W2K_COMPAT_VLABEL, TRUE);
    // Should we handle or ignore HW_RO flag
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_HANDLE_HW_RO, UDF_VCB_IC_HW_RO, FALSE);
    // Should we handle or ignore SOFT_RO flag
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_HANDLE_SOFT_RO, UDF_VCB_IC_SOFT_RO, TRUE);

    // Should we ignore FO_SEQUENTIAL_ONLY
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_IGNORE_SEQUENTIAL_IO, UDF_VCB_IC_IGNORE_SEQUENTIAL_IO, FALSE);
// Force Read-only mounts
    UDFUpdateCompatOption(Vcb, Update, UseCfg, UDF_FORCE_HW_RO, UDF_VCB_IC_FORCE_HW_RO, FALSE);

    // compare data from packet with data to be writen there
    // before physical writing
    if (!UDFGetParameter(Vcb, UDF_COMPARE_BEFORE_WRITE, Update ? Vcb->DoNotCompareBeforeWrite : FALSE)) {
        Vcb->DoNotCompareBeforeWrite = TRUE;
    } else {
        Vcb->DoNotCompareBeforeWrite = FALSE;
    }
    if (!Update)  {
        if (UDFGetParameter(Vcb, UDF_CHAINED_IO, TRUE)) {
            Vcb->CacheChainedIo = TRUE;
        }

        // Should we show Blank.Cd file on damaged/unformatted,
        // but UDF-compatible disks
        Vcb->ShowBlankCd = (UCHAR)UDFGetParameter(Vcb, UDF_SHOW_BLANK_CD, FALSE);
        if (Vcb->ShowBlankCd) {
            Vcb->CompatFlags |= UDF_VCB_IC_SHOW_BLANK_CD;
            if (Vcb->ShowBlankCd > 2) {
                Vcb->ShowBlankCd = 2;
            }
        }

        // Set partitially damaged volume mount mode
        Vcb->PartitialDamagedVolumeAction = (UCHAR)UDFGetParameter(Vcb, UDF_PART_DAMAGED_BEHAVIOR, UDF_PART_DAMAGED_RW);
        if (Vcb->PartitialDamagedVolumeAction > 2) {
            Vcb->PartitialDamagedVolumeAction = UDF_PART_DAMAGED_RW;
        }

        // Set partitially damaged volume mount mode
        Vcb->NoFreeRelocationSpaceVolumeAction = (UCHAR)UDFGetParameter(Vcb, UDF_NO_SPARE_BEHAVIOR, UDF_PART_DAMAGED_RW);
        if (Vcb->NoFreeRelocationSpaceVolumeAction > 1) {
            Vcb->NoFreeRelocationSpaceVolumeAction = UDF_PART_DAMAGED_RW;
        }

        // Set dirty volume mount mode
        if (UDFGetParameter(Vcb, UDF_DIRTY_VOLUME_BEHAVIOR, UDF_PART_DAMAGED_RO)) {
            Vcb->CompatFlags |= UDF_VCB_IC_DIRTY_RO;
        }
    }
    return;
} // end UDFReadRegKeys()

ULONG
UDFGetRegParameter(
    IN PVCB Vcb,
    IN PCWSTR Name,
    IN ULONG DefValue
    )
{
    return UDFRegCheckParameterValue(&(UdfData.SavedRegPath),
                                     Name,
                                     NULL,
                                     Vcb ? Vcb->DefaultRegName : NULL,
                                     DefValue);
} // end UDFGetRegParameter()

ULONG
UDFGetCfgParameter(
    IN PVCB Vcb,
    IN PCWSTR Name,
    IN ULONG DefValue
    )
{
    ULONG len;
    CHAR NameA[128];
    ULONG ret_val=0;
    CHAR a;
    BOOLEAN wait_name=TRUE;
    BOOLEAN wait_val=FALSE;
    BOOLEAN wait_nl=FALSE;
    ULONG radix=10;
    ULONG i;

    PUCHAR Cfg    = Vcb->Cfg;
    ULONG  Length = Vcb->CfgLength;

    if (!Cfg || !Length)
        return DefValue;

    len = wcslen(Name);
    if (len >= sizeof(NameA))
        return DefValue;
    sprintf(NameA, "%S", Name);

    for(i=0; i<Length; i++) {
        a=Cfg[i];
        switch(a) {
        case '\n':
        case '\r':
        case ',':
            if (wait_val)
                return DefValue;
            continue;
        case ';':
        case '#':
        case '[': // ignore sections for now, treat as comment
            if (!wait_name)
                return DefValue;
            wait_nl = TRUE;
            continue;
        case '=':
            if (!wait_val)
                return DefValue;
            continue;
        case ' ':
        case '\t':
            continue;
        default:
            if (wait_nl)
                continue;
        }
        if (wait_name) {
            if (i+len+2 > Length)
                return DefValue;
            if (RtlCompareMemory(Cfg+i, NameA, len) == len) {
                a=Cfg[i+len];
                switch(a) {
                case '\n':
                case '\r':
                case ',':
                case ';':
                case '#':
                    return DefValue;
                case '=':
                case ' ':
                case '\t':
                    break;
                default:
                    wait_nl = TRUE;
                    wait_val = FALSE;
                    i+=len;
                    continue;
                }
                wait_name = FALSE;
                wait_nl = FALSE;
                wait_val = TRUE;
                i+=len;

            } else {
                wait_nl = TRUE;
            }
            continue;
        }
        if (wait_val) {
            if (i+3 > Length) {
                if (a=='0' && Cfg[i+1]=='x') {
                    i+=2;
                    radix=16;
                }
            }
            if (i >= Length) {
                return DefValue;
            }
            while(i<Length) {
                a=Cfg[i];
                switch(a) {
                case '\n':
                case '\r':
                case ' ':
                case '\t':
                case ',':
                case ';':
                case '#':
                    if (wait_val)
                        return DefValue;
                    return ret_val;
                }
                if (a >= '0' && a <= '9') {
                    a -= '0';
                } else {
                    if (radix != 16)
                        return DefValue;
                    if (a >= 'a' && a <= 'f') {
                        a -= 'a';
                    } else
                    if (a >= 'A' && a <= 'F') {
                        a -= 'A';
                    } else {
                        return DefValue;
                    }
                    a += 0x0a;
                }
                ret_val = ret_val*radix + a;
                wait_val = FALSE;
                i++;
            }
            return ret_val;
        }
    }
    return DefValue;

} // end UDFGetCfgParameter()

VOID
UDFDeleteVCB(
    PIRP_CONTEXT IrpContext,
    PVCB  Vcb
    )
{
    LARGE_INTEGER delay;
    UDFPrint(("UDFDeleteVCB\n"));

    delay.QuadPart = -500000; // 0.05 sec
    while(Vcb->PostedRequestCount) {
        UDFPrint(("UDFDeleteVCB: PostedRequestCount = %d\n", Vcb->PostedRequestCount));
        // spin until all queues IRPs are processed
        KeDelayExecutionThread(KernelMode, FALSE, &delay);
        delay.QuadPart -= 500000; // grow delay 0.05 sec
    }

    _SEH2_TRY {
        UDFPrint(("UDF: Flushing buffers\n"));
        UDFVRelease(Vcb);
        // Cache flushing is now handled by Windows Cache Manager

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        BrutePoint();
    } _SEH2_END;

#ifdef UDF_DBG
    _SEH2_TRY {
        if (!ExIsResourceAcquiredShared(&UdfData.GlobalDataResource)) {
            UDFPrint(("UDF: attempt to access to not protected data\n"));
            UDFPrint(("UDF: UDFGlobalData\n"));
            BrutePoint();
        }
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        BrutePoint();
    } _SEH2_END;
#endif

    _SEH2_TRY {
        RemoveEntryList(&(Vcb->NextVCB));
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        BrutePoint();
    } _SEH2_END;

    _SEH2_TRY {
        UDFPrint(("UDF: Delete resources\n"));
        UDFDeleteResource(&(Vcb->VcbResource));
        UDFDeleteResource(&(Vcb->BitMapResource1));
        UDFDeleteResource(&(Vcb->FileIdResource));
        UDFDeleteResource(&(Vcb->DlocResource));
        UDFDeleteResource(&(Vcb->DlocResource2));
        UDFDeleteResource(&(Vcb->FlushResource));
        UDFDeleteResource(&(Vcb->PreallocResource));
        UDFDeleteResource(&(Vcb->IoResource));
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        BrutePoint();
    } _SEH2_END;

    _SEH2_TRY {
        UDFPrint(("UDF: Cleanup VCB\n"));
        ASSERT(IsListEmpty(&(Vcb->NextNotifyIRP)));
        FsRtlNotifyUninitializeSync(&(Vcb->NotifyIRPMutex));
        UDFCleanupVCB(Vcb);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        BrutePoint();
    } _SEH2_END;

    // Chuck the backpocket Vpb we kept just in case.
    UDFFreePool((PVOID*)&Vcb->SwapVpb);

    // If there is a Vpb then we must delete it ourselves.
    UDFFreePool((PVOID*)&Vcb->Vpb);

    _SEH2_TRY {
        UDFPrint(("UDF: Delete DO\n"));
        IoDeleteDevice(Vcb->VCBDeviceObject);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        BrutePoint();
    } _SEH2_END;

} // end UDFDeleteVCB()

/*
    Read DWORD from Registry
*/
ULONG
UDFRegCheckParameterValue(
    IN PUNICODE_STRING RegistryPath,
    IN PCWSTR Name,
    IN PUNICODE_STRING PtrVolumePath,
    IN PCWSTR DefaultPath,
    IN ULONG DefValue
    )
{
    NTSTATUS          status;

    ULONG             val = DefValue;

    UNICODE_STRING    paramStr;
    UNICODE_STRING    defaultParamStr;
    UNICODE_STRING    paramPathUnknownStr;

    UNICODE_STRING    paramSuffix;
    UNICODE_STRING    paramPath;
    UNICODE_STRING    paramPathUnknown;
    UNICODE_STRING    paramDevPath;
    UNICODE_STRING    defaultParamPath;

    _SEH2_TRY {

        paramPath.Buffer = NULL;
        paramDevPath.Buffer = NULL;
        paramPathUnknown.Buffer = NULL;
        defaultParamPath.Buffer = NULL;

        // First append \Parameters to the passed in registry path
        // Note, RtlInitUnicodeString doesn't allocate memory
        RtlInitUnicodeString(&paramStr, L"\\Parameters");
        RtlInitUnicodeString(&paramPath, NULL);

        RtlInitUnicodeString(&paramPathUnknownStr, REG_DEFAULT_UNKNOWN);
        RtlInitUnicodeString(&paramPathUnknown, NULL);

        paramPathUnknown.MaximumLength = RegistryPath->Length + paramPathUnknownStr.Length + paramStr.Length + sizeof(WCHAR);
        paramPath.MaximumLength = RegistryPath->Length + paramStr.Length + sizeof(WCHAR);

        paramPath.Buffer = (PWCH)MyAllocatePool__(PagedPool, paramPath.MaximumLength);
        if (!paramPath.Buffer) {
            UDFPrint(("UDFCheckRegValue: couldn't allocate paramPath\n"));
            try_return(val = DefValue);
        }
        paramPathUnknown.Buffer = (PWCH)MyAllocatePool__(PagedPool, paramPathUnknown.MaximumLength);
        if (!paramPathUnknown.Buffer) {
            UDFPrint(("UDFCheckRegValue: couldn't allocate paramPathUnknown\n"));
            try_return(val = DefValue);
        }

        RtlZeroMemory(paramPath.Buffer, paramPath.MaximumLength);
        status = RtlAppendUnicodeToString(&paramPath, RegistryPath->Buffer);
        if (!NT_SUCCESS(status)) {
            try_return(val = DefValue);
        }
        status = RtlAppendUnicodeToString(&paramPath, paramStr.Buffer);
        if (!NT_SUCCESS(status)) {
            try_return(val = DefValue);
        }
        UDFPrint(("UDFCheckRegValue: (1) |%S|\n", paramPath.Buffer));

        RtlZeroMemory(paramPathUnknown.Buffer, paramPathUnknown.MaximumLength);
        status = RtlAppendUnicodeToString(&paramPathUnknown, RegistryPath->Buffer);
        if (!NT_SUCCESS(status)) {
            try_return(val = DefValue);
        }
        status = RtlAppendUnicodeToString(&paramPathUnknown, paramStr.Buffer);
        if (!NT_SUCCESS(status)) {
            try_return(val = DefValue);
        }
        status = RtlAppendUnicodeToString(&paramPathUnknown, paramPathUnknownStr.Buffer);
        if (!NT_SUCCESS(status)) {
            try_return(val = DefValue);
        }
        UDFPrint(("UDFCheckRegValue: (2) |%S|\n", paramPathUnknown.Buffer));

        // First append \Parameters\Default_XXX to the passed in registry path
        if (DefaultPath) {
            RtlInitUnicodeString(&defaultParamStr, DefaultPath);
            RtlInitUnicodeString(&defaultParamPath, NULL);
            defaultParamPath.MaximumLength = paramPath.Length + defaultParamStr.Length + sizeof(WCHAR);
            defaultParamPath.Buffer = (PWCH)MyAllocatePool__(PagedPool, defaultParamPath.MaximumLength);
            if (!defaultParamPath.Buffer) {
                UDFPrint(("UDFCheckRegValue: couldn't allocate defaultParamPath\n"));
                try_return(val = DefValue);
            }

            RtlZeroMemory(defaultParamPath.Buffer, defaultParamPath.MaximumLength);
            status = RtlAppendUnicodeToString(&defaultParamPath, paramPath.Buffer);
            if (!NT_SUCCESS(status)) {
                try_return(val = DefValue);
            }
            status = RtlAppendUnicodeToString(&defaultParamPath, defaultParamStr.Buffer);
            if (!NT_SUCCESS(status)) {
                try_return(val = DefValue);
            }
            UDFPrint(("UDFCheckRegValue: (3) |%S|\n", defaultParamPath.Buffer));
        }

        if (PtrVolumePath) {
            paramSuffix = *PtrVolumePath;
        } else {
            RtlInitUnicodeString(&paramSuffix, NULL);
        }

        RtlInitUnicodeString(&paramDevPath, NULL);
        // now build the device specific path
        paramDevPath.MaximumLength = paramPath.Length + paramSuffix.Length + sizeof(WCHAR);
        paramDevPath.Buffer = (PWCH)MyAllocatePool__(PagedPool, paramDevPath.MaximumLength);
        if (!paramDevPath.Buffer) {
            try_return(val = DefValue);
        }

        RtlZeroMemory(paramDevPath.Buffer, paramDevPath.MaximumLength);
        status = RtlAppendUnicodeToString(&paramDevPath, paramPath.Buffer);
        if (!NT_SUCCESS(status)) {
            try_return(val = DefValue);
        }
        if (paramSuffix.Buffer) {
            status = RtlAppendUnicodeToString(&paramDevPath, paramSuffix.Buffer);
            if (!NT_SUCCESS(status)) {
                try_return(val = DefValue);
            }
        }

        UDFPrint(( " Parameter = %ws\n", Name));

        {
            HKEY hk = NULL;
            status = RegTGetKeyHandle(NULL, RegistryPath->Buffer, &hk);
            if (NT_SUCCESS(status)) {
                RegTCloseKeyHandle(hk);
            }
        }


        // *** Read GLOBAL_DEFAULTS from
        // "\DwUdf\Parameters_Unknown\"

        status = RegTGetDwordValue(NULL, paramPath.Buffer, Name, &val);

        // *** Read DEV_CLASS_SPEC_DEFAULTS (if any) from
        // "\DwUdf\Parameters_%DevClass%\"

        if (DefaultPath) {
            status = RegTGetDwordValue(NULL, defaultParamPath.Buffer, Name, &val);
        }

        // *** Read DEV_SPEC_PARAMS from (if device supports GetDevName)
        // "\DwUdf\Parameters\%DevName%\"

        status = RegTGetDwordValue(NULL, paramDevPath.Buffer, Name, &val);

try_exit:   NOTHING;

    } _SEH2_FINALLY {

        if (DefaultPath && defaultParamPath.Buffer) {
            MyFreePool__(defaultParamPath.Buffer);
        }
        if (paramPath.Buffer) {
            MyFreePool__(paramPath.Buffer);
        }
        if (paramDevPath.Buffer) {
            MyFreePool__(paramDevPath.Buffer);
        }
        if (paramPathUnknown.Buffer) {
            MyFreePool__(paramPathUnknown.Buffer);
        }
    } _SEH2_END;

    UDFPrint(( "UDFCheckRegValue: %ws for drive %s is %x\n\n", Name, PtrVolumePath, val));
    return val;
} // end UDFRegCheckParameterValue()

/*
Routine Description:
    This routine is called to initialize an IrpContext for the current
    UDFFS request.  The IrpContext is on the stack and we need to initialize
    it for the current request.  The request is a close operation.

Arguments:

    IrpContext - IrpContext to initialize.

    IrpContextLite - source for initialization

Return Value:

    None

*/
VOID
UDFInitializeStackIrpContextFromLite(
    OUT PIRP_CONTEXT IrpContext,
    IN PIRP_CONTEXT_LITE IrpContextLite
    )
{
    ASSERT(IrpContextLite->NodeIdentifier.NodeTypeCode == UDF_NODE_TYPE_IRP_CONTEXT_LITE);
    ASSERT(IrpContextLite->NodeIdentifier.NodeByteSize == sizeof(IRP_CONTEXT_LITE));

    // Zero and then initialize the structure.
    RtlZeroMemory(IrpContext, sizeof(IRP_CONTEXT));

    // Set the proper node type code and node byte size
    IrpContext->NodeIdentifier.NodeTypeCode = UDF_NODE_TYPE_IRP_CONTEXT;
    IrpContext->NodeIdentifier.NodeByteSize = sizeof(IRP_CONTEXT);

    //  Major/Minor Function codes
    IrpContext->MajorFunction = IRP_MJ_CLOSE;
    IrpContext->Vcb = IrpContextLite->Fcb->Vcb;
    IrpContext->Fcb = IrpContextLite->Fcb;
    IrpContext->TreeLength = IrpContextLite->TreeLength;
    IrpContext->RealDevice = IrpContextLite->RealDevice;

    // Note that this is from the stack.
    SetFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_ON_STACK);

    // Set the wait parameter
    SetFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT);

} // end UDFInitializeStackIrpContextFromLite()

/*
Routine Description:
    This routine is called to initialize an IrpContext for the current
    UDFFS request.  The IrpContext is on the stack and we need to initialize
    it for the current request.  The request is a close operation.

Arguments:

    IrpContext - IrpContext to initialize.

    IrpContextLite - source for initialization

Return Value:

    None

*/
NTSTATUS
UDFInitializeIrpContextLite(
    OUT PIRP_CONTEXT_LITE *IrpContextLite,
    IN PIRP_CONTEXT IrpContext,
    IN PFCB                Fcb
    )
{
    PIRP_CONTEXT_LITE LocalIrpContextLite = (PIRP_CONTEXT_LITE)MyAllocatePool__(NonPagedPool, sizeof(IRP_CONTEXT_LITE));
    if (!LocalIrpContextLite)
        return STATUS_INSUFFICIENT_RESOURCES;
    //  Zero and then initialize the structure.
    RtlZeroMemory(LocalIrpContextLite, sizeof(IRP_CONTEXT_LITE));

    LocalIrpContextLite->NodeIdentifier.NodeTypeCode = UDF_NODE_TYPE_IRP_CONTEXT_LITE;
    LocalIrpContextLite->NodeIdentifier.NodeByteSize = sizeof(IRP_CONTEXT_LITE);

    LocalIrpContextLite->Fcb = Fcb;
    LocalIrpContextLite->TreeLength = IrpContext->TreeLength;
    //  Copy RealDevice for workque algorithms.
    LocalIrpContextLite->RealDevice = IrpContext->RealDevice;
    *IrpContextLite = LocalIrpContextLite;

    return STATUS_SUCCESS;
} // end UDFInitializeIrpContextLite()

ULONG
UDFIsResourceAcquired(
    IN PERESOURCE Resource
    )
{
    ULONG ReAcqRes =
        ExIsResourceAcquiredExclusiveLite(Resource) ? 1 :
        (ExIsResourceAcquiredSharedLite(Resource) ? 2 : 0);
    return ReAcqRes;
} // end UDFIsResourceAcquired()

BOOLEAN
UDFAcquireResourceExclusiveWithCheck(
    IN PERESOURCE Resource
    )
{
    ULONG ReAcqRes =
        ExIsResourceAcquiredExclusiveLite(Resource) ? 1 :
        (ExIsResourceAcquiredSharedLite(Resource) ? 2 : 0);
    if (ReAcqRes) {
        UDFPrint(("UDFAcquireResourceExclusiveWithCheck: ReAcqRes, %x\n", ReAcqRes));
    } else {
//        BrutePoint();
    }

    if (ReAcqRes == 1) {
        // OK
    } else
    if (ReAcqRes == 2) {
        UDFPrint(("UDFAcquireResourceExclusiveWithCheck: !!! Shared !!!\n"));
        //BrutePoint();
    } else {
        UDFAcquireResourceExclusive(Resource, TRUE);
        return TRUE;
    }
    return FALSE;
} // end UDFAcquireResourceExclusiveWithCheck()

BOOLEAN
UDFAcquireResourceSharedWithCheck(
    IN PERESOURCE Resource
    )
{
    ULONG ReAcqRes =
        ExIsResourceAcquiredExclusiveLite(Resource) ? 1 :
        (ExIsResourceAcquiredSharedLite(Resource) ? 2 : 0);
    if (ReAcqRes) {
        UDFPrint(("UDFAcquireResourceSharedWithCheck: ReAcqRes, %x\n", ReAcqRes));
/*    } else {
        BrutePoint();*/
    }

    if (ReAcqRes == 2) {
        // OK
    } else
    if (ReAcqRes == 1) {
        UDFPrint(("UDFAcquireResourceSharedWithCheck: Exclusive\n"));
        //BrutePoint();
    } else {
        UDFAcquireResourceShared(Resource, TRUE);
        return TRUE;
    }
    return FALSE;
} // end UDFAcquireResourceSharedWithCheck()

VOID
UDFSetModified(
    IN PVCB        Vcb
    )
{
    if (UDFInterlockedIncrement((PLONG) & (Vcb->Modified)) & 0x80000000)
        Vcb->Modified = 2;
} // end UDFSetModified()

VOID
UDFPreClrModified(
    IN PVCB Vcb
    )
{
    Vcb->Modified = 1;
} // end UDFPreClrModified()

VOID
UDFClrModified(
    IN PVCB        Vcb
    )
{
    UDFPrint(("ClrModified\n"));
    UDFInterlockedDecrement((PLONG) & (Vcb->Modified));
} // end UDFClrModified()

NTSTATUS
UDFToggleMediaEjectDisable (
    IN PVCB Vcb,
    IN BOOLEAN PreventRemoval
    )
{
    PREVENT_MEDIA_REMOVAL Prevent;

    //  If PreventRemoval is the same as UDF_VCB_FLAGS_MEDIA_LOCKED,
    //  no-op this call, otherwise toggle the state of the flag.

    if ((PreventRemoval ^ BooleanFlagOn(Vcb->VcbState, UDF_VCB_FLAGS_MEDIA_LOCKED)) == 0) {

        return STATUS_SUCCESS;

    } else {

        Vcb->VcbState ^= UDF_VCB_FLAGS_MEDIA_LOCKED;
    }

    Prevent.PreventMediaRemoval = PreventRemoval;

    return UDFPhSendIOCTL(IOCTL_DISK_MEDIA_REMOVAL,
                          Vcb->TargetDeviceObject,
                          &Prevent,
                          sizeof(Prevent),
                          NULL,
                          0,
                          FALSE,
                          NULL);
}

/*++

Routine Description:

    This routine completes a Irp and cleans up the IrpContext.  Either or
    both of these may not be specified.

Arguments:

    Irp - Supplies the Irp being processed.

    Status - Supplies the status to complete the Irp with

Return Value:

    None.

--*/
VOID
UDFCompleteRequest (
    _Inout_opt_ PIRP_CONTEXT IrpContext OPTIONAL,
    _Inout_opt_ PIRP Irp OPTIONAL,
    _In_ NTSTATUS Status
    )
{
    ASSERT_OPTIONAL_IRP_CONTEXT(IrpContext);
    ASSERT_OPTIONAL_IRP(Irp);

    //  Cleanup the IrpContext if passed in here.

    if (ARGUMENT_PRESENT(IrpContext)) {

        UDFCleanupIrpContext(IrpContext, FALSE);
    }

    //  If we have an Irp then complete the irp.

    if (ARGUMENT_PRESENT(Irp)) {

        //  Clear the information field in case we have used this Irp
        //  internally.

        if (NT_ERROR( Status ) &&
            FlagOn(Irp->Flags, IRP_INPUT_OPERATION)) {

            Irp->IoStatus.Information = 0;
        }

        Irp->IoStatus.Status = Status;

        AssertVerifyDeviceIrp(Irp);

        IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    }

    return;
}

VOID
UDFSetThreadContext(
    _Inout_ PIRP_CONTEXT IrpContext,
    _In_ PTHREAD_CONTEXT ThreadContext
    )

/*++

Routine Description:

    This routine is called at each Fsd/Fsp entry point set up the IrpContext
    and thread local storage to track top level requests.  If there is
    not a Udfs context in the thread local storage then we use the input one.
    Otherwise we use the one already there.  This routine also updates the
    IrpContext based on the state of the top-level context.

    If the TOP_LEVEL flag in the IrpContext is already set when we are called
    then we force this request to appear top level.

Arguments:

    ThreadContext - Address on stack for local storage if not already present.

    ForceTopLevel - We force this request to appear top level regardless of
        any previous stack value.

Return Value:

    None

--*/

{
    PTHREAD_CONTEXT CurrentThreadContext;
#ifdef __REACTOS__
    ULONG_PTR StackTop;
    ULONG_PTR StackBottom;
#endif

    PAGED_CODE();

    ASSERT_IRP_CONTEXT(IrpContext);

    //  Get the current top-level irp out of the thread storage.
    //  If NULL then this is the top-level request.

    CurrentThreadContext = (PTHREAD_CONTEXT) IoGetTopLevelIrp();

    if (CurrentThreadContext == NULL) {

        SetFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_TOP_LEVEL);
    }

    // Initialize the input context unless we are using the current
    // thread context block.  We use the new block if our caller
    // specified this or the existing block is invalid.
    //
    // The following must be true for the current to be a valid Cdfs context.
    //
    //      Structure must lie within current stack.
    //      Address must be ULONG aligned.
    //      Cdfs signature must be present.
    //
    // If this is not a valid Cdfs context then use the input thread
    // context and store it in the top level context.

#ifdef __REACTOS__
    IoGetStackLimits( &StackTop, &StackBottom);
#endif

#pragma warning(suppress: 6011) // Bug in PREFast around bitflag operations
    if (FlagOn( IrpContext->Flags, IRP_CONTEXT_FLAG_TOP_LEVEL ) ||
#ifndef __REACTOS__
        (!IoWithinStackLimits( (ULONG_PTR)CurrentThreadContext, sizeof(THREAD_CONTEXT) ) ||
#else
        (((ULONG_PTR) CurrentThreadContext > StackBottom - sizeof( THREAD_CONTEXT )) ||
         ((ULONG_PTR) CurrentThreadContext <= StackTop) ||
#endif
         FlagOn( (ULONG_PTR) CurrentThreadContext, 0x3 ) ||
         (CurrentThreadContext->Udfs != 0x53464444))) {

        ThreadContext->Udfs = 0x53464444;
        ThreadContext->SavedTopLevelIrp = (PIRP) CurrentThreadContext;
        ThreadContext->TopLevelIrpContext = IrpContext;
        IoSetTopLevelIrp((PIRP)ThreadContext);

        IrpContext->TopLevel = IrpContext;
        IrpContext->ThreadContext = ThreadContext;

        SetFlag(IrpContext->Flags, IRP_CONTEXT_FLAG_TOP_LEVEL_UDFS);

    //
    //  Otherwise use the IrpContext in the thread context.
    //

    } else {

        IrpContext->TopLevel = CurrentThreadContext->TopLevelIrpContext;
    }

    return;
}


_Requires_lock_held_(_Global_critical_region_)
_When_(Type == AcquireExclusive && return != FALSE, _Acquires_exclusive_lock_(*Resource))
_When_(Type == AcquireShared && return != FALSE, _Acquires_shared_lock_(*Resource))
_When_(Type == AcquireSharedStarveExclusive && return != FALSE, _Acquires_shared_lock_(*Resource))
_When_(IgnoreWait == FALSE, _Post_satisfies_(return == TRUE))
BOOLEAN
UDFAcquireResource(
    _In_ PIRP_CONTEXT IrpContext,
    _Inout_ PERESOURCE Resource,
    _In_ BOOLEAN IgnoreWait,
    _In_ TYPE_OF_ACQUIRE Type
    )

/*++

Routine Description:

    This is the single routine used to acquire file system resources.  It
    looks at the IgnoreWait flag to determine whether to try to acquire the
    resource without waiting.  Returning TRUE/FALSE to indicate success or
    failure.  Otherwise it is driven by the WAIT flag in the IrpContext and
    will raise CANT_WAIT on a failure.

Arguments:

    Resource - This is the resource to try and acquire.

    IgnoreWait - If TRUE then this routine will not wait to acquire the
        resource and will return a boolean indicating whether the resource was
        acquired.  Otherwise we use the flag in the IrpContext and raise
        if the resource is not acquired.

    Type - Indicates how we should try to get the resource.

Return Value:

    BOOLEAN - TRUE if the resource is acquired.  FALSE if not acquired and
        IgnoreWait is specified.  Otherwise we raise CANT_WAIT.

--*/

{
    BOOLEAN Wait = FALSE;
    BOOLEAN Acquired;
    PAGED_CODE();

    //  We look first at the IgnoreWait flag, next at the flag in the Irp
    // Context to decide how to acquire this resource.

    if (!IgnoreWait && FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT)) {

        Wait = TRUE;
    }

    // Attempt to acquire the resource either shared or exclusively.

    switch (Type) {
        case AcquireExclusive:

#pragma prefast( suppress:28137, "prefast believes Wait should be a constant, but this is ok for CDFS" )
            Acquired = ExAcquireResourceExclusiveLite( Resource, Wait );
            break;

        case AcquireShared:

            Acquired = ExAcquireResourceSharedLite( Resource, Wait );
            break;

        case AcquireSharedStarveExclusive:

            Acquired = ExAcquireSharedStarveExclusive( Resource, Wait );
            break;

        default:
            Acquired = FALSE;
            NT_ASSERT( FALSE );
    }

    // If not acquired and the user didn't specifiy IgnoreWait then
    // raise CANT_WAIT.

    if (!Acquired && !IgnoreWait) {

        UDFRaiseStatus(IrpContext, STATUS_CANT_WAIT);
    }

    return Acquired;
}

#include "Include/regtools.cpp"

