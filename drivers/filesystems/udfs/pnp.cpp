////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*************************************************************************
*
* File: Pnp.cpp
*
* Module: UDF File System Driver (Kernel mode execution only)
*
* Description:
*    This module implements the Plug and Play routines for UDF called by
*    the dispatch driver.
*
*************************************************************************/

#include            "udffs.h"

// define the file specific bug-check id
#define         UDF_BUG_CHECK_ID                UDF_FILE_PNP


NTSTATUS
UDFPnpQueryRemove (
    PIRP_CONTEXT IrpContext,
    PIRP Irp,
    PVCB Vcb
    );

NTSTATUS
UDFPnpRemove (
    PIRP_CONTEXT IrpContext,
    PIRP Irp,
    PVCB Vcb
    );

NTSTATUS
UDFPnpSurpriseRemove (
    PIRP_CONTEXT IrpContext,
    PIRP Irp,
    PVCB Vcb
    );

NTSTATUS
UDFPnpCancelRemove (
    PIRP_CONTEXT IrpContext,
    PIRP Irp,
    PVCB Vcb
    );

NTSTATUS
NTAPI
UDFPnpCompletionRoutine (
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Contxt
    );

NTSTATUS
UDFCommonPnp (
    PIRP_CONTEXT IrpContext,
    IN PIRP Irp
    );

/*
    This routine implements the FSD part of PnP operations

Arguments:

    VolumeDeviceObject - Supplies the volume device object where the
        file exists
    Irp - Supplies the Irp being processed

Return Value:

    NTSTATUS - The FSD status for the IRP

 */
NTSTATUS
NTAPI
UDFPnp (
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
    )
{
    NTSTATUS RC;
    PIRP_CONTEXT IrpContext = NULL;
    BOOLEAN AreWeTopLevel;

    UDFPrint(("UDFPnp\n"));

    FsRtlEnterFileSystem();
    ASSERT(DeviceObject);
    ASSERT(Irp);

    // set the top level context
    AreWeTopLevel = UDFIsIrpTopLevel(Irp);

    _SEH2_TRY {
        //  We expect there to never be a fileobject, in which case we will always
        //  wait.  Since at the moment we don't have any concept of pending Pnp
        //  operations, this is a bit nitpicky.

        // get an IRP context structure and issue the request
        IrpContext = UDFCreateIrpContext(Irp, DeviceObject);
        if (IrpContext) {
            RC = UDFCommonPnp(IrpContext, Irp);
        } else {

            UDFCompleteRequest(IrpContext, Irp, STATUS_INSUFFICIENT_RESOURCES);
            RC = STATUS_INSUFFICIENT_RESOURCES;
        }

    } _SEH2_EXCEPT(UDFExceptionFilter( IrpContext, _SEH2_GetExceptionInformation() )) {

        RC = UDFProcessException(IrpContext, Irp);
        UDFLogEvent(UDF_ERROR_INTERNAL_ERROR, RC);
    } _SEH2_END;

    if (AreWeTopLevel) {
        IoSetTopLevelIrp(NULL);
    }

    FsRtlExitFileSystem();

    return RC;
}

/*
    This is the common routine for doing PnP operations called
    by both the fsd and fsp threads

Arguments:

    Irp - Supplies the Irp to process

Return Value:

    NTSTATUS - The return status for the operation
 */
NTSTATUS
UDFCommonPnp (
    PIRP_CONTEXT IrpContext,
    IN PIRP Irp
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN PassThrough = FALSE;

    PIO_STACK_LOCATION IrpSp;

    PVOLUME_DEVICE_OBJECT OurDeviceObject;
    PVCB Vcb;

    PAGED_CODE();

    // Global lock object is acquired based on internal book-keeping
    _Analysis_suppress_lock_checking_(UdfData.GlobalDataResource);

    UDFPrint(("UDFCommonPnp\n"));

    // Get the current Irp stack location.
    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    // Find our Vcb. This is tricky since we have no file object in the Irp.
    OurDeviceObject = (PVOLUME_DEVICE_OBJECT) IrpSp->DeviceObject;

    // IO holds a handle reference on our VDO and holds the device lock, which 
    // syncs us against mounts/verifies.  However we hold no reference on the 
    // volume, which may already have been torn down (and the Vpb freed), for 
    // example by a force dismount. Check for this condition. We must hold this
    // lock until the pnp worker functions take additional locks/refs on the Vcb.

    UDFAcquireResourceExclusive(&UdfData.GlobalDataResource, TRUE);

    // Make sure this device object really is big enough to be a volume device
    // object.  If it isn't, we need to get out before we try to reference some
    // field that takes us past the end of an ordinary device object. VOLUME_DEVICE_OBJECT

    #pragma prefast(suppress: 28175, "this is a filesystem driver, touching the size member is allowed")
   // if (OurDeviceObject->DeviceObject.Size != sizeof(VOLUME_DEVICE_OBJECT) ||
   //     OurDeviceObject->Vcb.NodeIdentifier.NodeTypeCode != UDF_NODE_TYPE_VCB) {

    if (OurDeviceObject->DeviceObject.Size != sizeof(DEVICE_OBJECT) + sizeof(VCB) ||
        ((PVCB)IrpSp->DeviceObject->DeviceExtension)->NodeIdentifier.NodeTypeCode != UDF_NODE_TYPE_VCB) {
        
        // We were called with something we don't understand.

        Status = STATUS_INVALID_PARAMETER;
        UDFReleaseResource(&UdfData.GlobalDataResource);
        UDFCompleteRequest( IrpContext, Irp, Status );
        return Status;
    }

    Vcb = (PVCB)(IrpSp->DeviceObject->DeviceExtension);

    // Force all PnP operations to be synchronous.
    IrpContext->Flags |= IRP_CONTEXT_FLAG_WAIT;

    //Vcb = &OurDeviceObject->Vcb;

    //  Check that the Vcb hasn't already been deleted.  If so,  just pass the
    //  request through to the driver below,  we don't need to do anything.
    //
    
    if (NULL == Vcb->Vpb) {

        PassThrough = TRUE;
    }
    else {

        //
        //  Case on the minor code.
        //

        switch (IrpSp->MinorFunction) {

            case IRP_MN_QUERY_REMOVE_DEVICE:

                Status = UDFPnpQueryRemove(IrpContext, Irp, Vcb);
                break;
            
            case IRP_MN_SURPRISE_REMOVAL:

                Status = UDFPnpSurpriseRemove(IrpContext, Irp, Vcb);
                break;

            case IRP_MN_REMOVE_DEVICE:

                Status = UDFPnpRemove(IrpContext, Irp, Vcb);
                break;

            case IRP_MN_CANCEL_REMOVE_DEVICE:
        
                Status = UDFPnpCancelRemove(IrpContext, Irp, Vcb);
                break;

            default:

                PassThrough = TRUE;
                break;
        }
    }

    if (PassThrough) {

        UDFReleaseResource(&UdfData.GlobalDataResource);

        //  Just pass the IRP on.  As we do not need to be in the
        //  way on return, ellide ourselves out of the stack.

        IoSkipCurrentIrpStackLocation(Irp);

        Status = IoCallDriver(Vcb->TargetDeviceObject, Irp);
        
        //  Cleanup our Irp Context.  The driver has completed the Irp.
    
        UDFCompleteRequest( IrpContext, NULL, STATUS_SUCCESS );
    }
        
    return Status;
}

_Requires_lock_held_(_Global_critical_region_)
_Releases_nonreentrant_lock_(UdfData.GlobalDataResource)
NTSTATUS
UDFPnpQueryRemove (
    _Inout_ PIRP_CONTEXT IrpContext,
    _Inout_ PIRP Irp,
    _Inout_ PVCB Vcb
    )

/*++

Routine Description:

    This routine handles the PnP query remove operation.  The filesystem
    is responsible for answering whether there are any reasons it sees
    that the volume can not go away (and the device removed).  Initiation
    of the dismount begins when we answer yes to this question.
    
    Query will be followed by a Cancel or Remove.

Arguments:

    Irp - Supplies the Irp to process
    
    Vcb - Supplies the volume being queried.

Return Value:

    NTSTATUS - The return status for the operation

--*/

{
    NTSTATUS Status;
    KEVENT Event;
    BOOLEAN VcbPresent = TRUE;

    PAGED_CODE();

    ASSERT_EXCLUSIVE_CDDATA;

    // Having said yes to a QUERY, any communication with the
    // underlying storage stack is undefined (and may block)
    // until the bounding CANCEL or REMOVE is sent.

    // Acquire the global resource so that we can try to vaporize the volume, 
    // and the vcb resource itself.

    BOOLEAN CanWait = FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT);

    UDFAcquireResourceExclusive(&Vcb->VcbResource, CanWait);

    // Drop a reference on the Vcb to keep it around after we drop the locks.
    
    UDFLockVcb(IrpContext, Vcb);
    Vcb->VcbReference += 1;
    UDFUnlockVcb(IrpContext, Vcb);
    
    UDFReleaseResource(&UdfData.GlobalDataResource);

    Status = UDFLockVolumeInternal(IrpContext, Vcb, NULL);

    // Reacquire the global lock,  which means dropping the Vcb resource.
    
    UDFReleaseResource(&Vcb->VcbResource);
    
    UDFAcquireResourceExclusive(&UdfData.GlobalDataResource, CanWait);
    UDFAcquireResourceExclusive(&Vcb->VcbResource, CanWait);

    //  Remove our extra reference.
    
    UDFLockVcb(IrpContext, Vcb);
    Vcb->VcbReference -= 1;
    UDFUnlockVcb(IrpContext, Vcb);
    
    if (NT_SUCCESS(Status)) {

        //  We need to pass this down before starting the dismount, which
        //  could disconnect us immediately from the stack.
        
        //  Get the next stack location, and copy over the stack location

        IoCopyCurrentIrpStackLocationToNext(Irp);

        //  Set up the completion routine

        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        IoSetCompletionRoutine(Irp,
                               UDFPnpCompletionRoutine,
                               &Event,
                               TRUE,
                               TRUE,
                               TRUE);

        //  Send the request and wait.

        Status = IoCallDriver(Vcb->TargetDeviceObject, Irp);

        if (Status == STATUS_PENDING) {

            (VOID)KeWaitForSingleObject( &Event,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL );

            Status = Irp->IoStatus.Status;
        }

        //  Now if no one below us failed already, initiate the dismount
        //  on this volume, make it go away.  PnP needs to see our internal
        //  streams close and drop their references to the target device.

        //  Since we were able to lock the volume, we are guaranteed to
        //  move this volume into dismount state and disconnect it from
        //  the underlying storage stack.  The force on our part is actually
        //  unnecesary, though complete.

        //  What is not strictly guaranteed, though, is that the closes
        //  for the metadata streams take effect synchronously underneath
        //  of this call.  This would leave references on the target device
        //  even though we are disconnected!

        if (NT_SUCCESS(Status)) {
            
            VcbPresent = UDFCheckForDismount(IrpContext, Vcb, TRUE);
    
            NT_ASSERT(!VcbPresent || Vcb->VcbCondition == VcbDismountInProgress);
        }

        //
        //  Note: Normally everything will complete and the internal streams will 
        //  vaporise.  However there is some code in the system which drops additional
        //  references on fileobjects,  including our internal stream file objects,
        //  for (WMI) tracing purposes.  If that happens to run concurrently with our
        //  teardown, our internal streams will not vaporise until those references
        //  are removed.  So it's possible that the volume still remains at this 
        //  point.  The pnp query remove will fail due to our references on the device.
        //  To be cleaner we will return an error here.  We could pend the pnp
        //  IRP until the volume goes away, but since we don't know when that will
        //  be, and this is a very rare case, we'll just fail the query.
        //
        // The reason this is the case is that handles/fileobjects place a reference
        // on the device objects they overly.  In the filesystem case, these references
        // are on our target devices.  PnP correcly thinks that if references remain
        // on the device objects in the stack that someone has a handle, and that this
        // counts as a reason to not succeed the query - even though every interrogated
        // driver thinks that it is OK.

        if (NT_SUCCESS( Status) && VcbPresent && (Vcb->VcbReference != 0)) {

            Status = STATUS_DEVICE_BUSY;
        }
    }
    
    //  Release the Vcb if it could still remain.
    
    if (VcbPresent) {

        UDFReleaseResource(&Vcb->VcbResource);
    }
    else {
        _Analysis_assume_lock_not_held_(Vcb->VcbResource);
    }

    UDFReleaseResource(&UdfData.GlobalDataResource);
    
    //  Cleanup our IrpContext and complete the IRP if neccesary.

    UDFCompleteRequest( IrpContext, Irp, Status );

    return Status;
}


_Requires_lock_held_(_Global_critical_region_)
_Releases_nonreentrant_lock_(UdfData.GlobalDataResource)
NTSTATUS
UDFPnpRemove (
    _Inout_ PIRP_CONTEXT IrpContext,
    _Inout_ PIRP Irp,
    _Inout_ PVCB Vcb
    )

/*++

Routine Description:

    This routine handles the PnP remove operation.  This is our notification
    that the underlying storage device for the volume we have is gone, and
    an excellent indication that the volume will never reappear. The filesystem
    is responsible for initiation or completion the dismount.

Arguments:

    Irp - Supplies the Irp to process
    
    Vcb - Supplies the volume being removed.

Return Value:

    NTSTATUS - The return status for the operation

--*/

{
    NTSTATUS Status;
    KEVENT Event;
    BOOLEAN VcbPresent = TRUE;

    PAGED_CODE();

    ASSERT_EXCLUSIVE_CDDATA;

    //
    //  REMOVE - a storage device is now gone.  We either got
    //  QUERY'd and said yes OR got a SURPRISE OR a storage
    //  stack failed to spin back up from a sleep/stop state
    //  (the only case in which this will be the first warning).

    //  Note that it is entirely unlikely that we will be around
    //  for a REMOVE in the first two cases, as we try to intiate
    //  dismount.

    //  Acquire the global resource so that we can try to vaporize
    //  the volume, and the vcb resource itself.

    BOOLEAN CanWait = FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT);

    UDFAcquireResourceExclusive(&Vcb->VcbResource, CanWait);

    //  The device will be going away.  Remove our lock and find
    //  out if we ever had one in the first place.

    Status = UDFUnlockVolumeInternal(Vcb, NULL);

    //  If the volume had not been locked, we must invalidate the
    //  volume to ensure it goes away properly.  The remove will
    //  succeed.

    if (!NT_SUCCESS(Status)) {

        UDFLockVcb(IrpContext, Vcb);
        
        if (Vcb->VcbCondition != VcbDismountInProgress) {
            
            UDFUpdateVcbCondition(Vcb, VcbInvalid);
        }
        
        UDFUnlockVcb(IrpContext, Vcb);
        
        Status = STATUS_SUCCESS;
    }
    
    //  We need to pass this down before starting the dismount, which
    //  could disconnect us immediately from the stack.
    
    //  Get the next stack location, and copy over the stack location

    IoCopyCurrentIrpStackLocationToNext(Irp);

    //  Set up the completion routine

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp,
                           UDFPnpCompletionRoutine,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    //  Send the request and wait.

    Status = IoCallDriver(Vcb->TargetDeviceObject, Irp);

    if (Status == STATUS_PENDING) {

        (VOID)KeWaitForSingleObject( &Event,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL );

        Status = Irp->IoStatus.Status;
    }

    // Now make our dismount happen.  This may not vaporize the
    // Vcb, of course, since there could be any number of handles
    // outstanding if we were not preceeded by a QUERY.

    // PnP will take care of disconnecting this stack if we
    // couldn't get off of it immediately.

    VcbPresent = UDFCheckForDismount(IrpContext, Vcb, TRUE);

    //  Release the Vcb if it could still remain.
    
    if (VcbPresent) {

        UDFReleaseResource(&Vcb->VcbResource);
    }
    else {
        _Analysis_assume_lock_not_held_(Vcb->VcbResource);
    }

    UDFReleaseResource(&UdfData.GlobalDataResource);
    
    //  Cleanup our IrpContext and complete the IRP.

    UDFCompleteRequest(IrpContext, Irp, Status);

    return Status;
}

_Requires_lock_held_(_Global_critical_region_)
_Releases_nonreentrant_lock_(CdData.DataResource)
NTSTATUS
UDFPnpSurpriseRemove (
    _Inout_ PIRP_CONTEXT IrpContext,
    _Inout_ PIRP Irp,
    _Inout_ PVCB Vcb
    )

/*++

Routine Description:

    This routine handles the PnP surprise remove operation.  This is another
    type of notification that the underlying storage device for the volume we
    have is gone, and is excellent indication that the volume will never reappear.
    The filesystem is responsible for initiation or completion the dismount.
    
    For the most part, only "real" drivers care about the distinction of a
    surprise remove, which is a result of our noticing that a user (usually)
    physically reached into the machine and pulled something out.
    
    Surprise will be followed by a Remove when all references have been shut down.

Arguments:

    Irp - Supplies the Irp to process
    
    Vcb - Supplies the volume being removed.

Return Value:

    NTSTATUS - The return status for the operation

--*/

{
    NTSTATUS Status;
    KEVENT Event;
    BOOLEAN VcbPresent = TRUE;

    PAGED_CODE();

    ASSERT_EXCLUSIVE_CDDATA;
    
    //  SURPRISE - a device was physically yanked away without
    //  any warning.  This means external forces.
    
    BOOLEAN CanWait = FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT);

    UDFAcquireResourceExclusive(&Vcb->VcbResource, CanWait);
        
    //  Invalidate the volume right now.

    //  The intent here is to make every subsequent operation
    //  on the volume fail and grease the rails toward dismount.
    //  By definition there is no going back from a SURPRISE.
        
    UDFLockVcb(IrpContext, Vcb);
    
    if (Vcb->VcbCondition != VcbDismountInProgress) {
        
        UDFUpdateVcbCondition(Vcb, VcbInvalid);
    }
    
    UDFUnlockVcb(IrpContext, Vcb);
    
    //  We need to pass this down before starting the dismount, which
    //  could disconnect us immediately from the stack.
    
    //  Get the next stack location, and copy over the stack location

    IoCopyCurrentIrpStackLocationToNext( Irp );

    //
    //  Set up the completion routine
    //

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp,
                           UDFPnpCompletionRoutine,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE );

    //  Send the request and wait.

    Status = IoCallDriver(Vcb->TargetDeviceObject, Irp);

    if (Status == STATUS_PENDING) {

        (VOID)KeWaitForSingleObject(&Event,
                               Executive,
                               KernelMode,
                               FALSE,
                               NULL );

        Status = Irp->IoStatus.Status;
    }
    
    //  Now make our dismount happen.  This may not vaporize the
    //  Vcb, of course, since there could be any number of handles
    //  outstanding since this is an out of band notification.
        
    VcbPresent = UDFCheckForDismount(IrpContext, Vcb, TRUE);
    
    //  Release the Vcb if it could still remain.
    
    if (VcbPresent) {

        UDFReleaseResource(&Vcb->VcbResource);
    }
    else {
        _Analysis_assume_lock_not_held_(Vcb->VcbResource);
    }

    UDFReleaseResource(&UdfData.GlobalDataResource);
    
    //  Cleanup our IrpContext and complete the IRP.

    UDFCompleteRequest(IrpContext, Irp, Status);

    return Status;
}

_Requires_lock_held_(_Global_critical_region_)
_Releases_nonreentrant_lock_(UdfData.GlobalDataResource)
NTSTATUS
UDFPnpCancelRemove (
    _Inout_ PIRP_CONTEXT IrpContext,
    _Inout_ PIRP Irp,
    _Inout_ PVCB Vcb
    )

/*++

Routine Description:

    This routine handles the PnP cancel remove operation.  This is our
    notification that a previously proposed remove (query) was eventually
    vetoed by a component.  The filesystem is responsible for cleaning up
    and getting ready for more IO.
    
Arguments:

    Irp - Supplies the Irp to process
    
    Vcb - Supplies the volume being removed.

Return Value:

    NTSTATUS - The return status for the operation

--*/

{
    NTSTATUS Status;

    PAGED_CODE();

    ASSERT_EXCLUSIVE_CDDATA;

    // CANCEL - a previous QUERY has been rescinded as a result
    // of someone vetoing.  Since PnP cannot figure out who may
    // have gotten the QUERY (think about it: stacked drivers),
    // we must expect to deal with getting a CANCEL without having
    // seen the QUERY.

    // For UDFS, this is quite easy.  In fact, we can't get a
    // CANCEL if the underlying drivers succeeded the QUERY since
    // we disconnect the Vpb on our dismount initiation.  This is
    // actually pretty important because if PnP could get to us
    // after the disconnect we'd be thoroughly unsynchronized
    // with respect to the Vcb getting torn apart - merely referencing
    // the volume device object is insufficient to keep us intact.

    BOOLEAN CanWait = FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT);

    UDFAcquireResourceExclusive(&Vcb->VcbResource, CanWait);
    UDFReleaseResource(&UdfData.GlobalDataResource);

    // Unlock the volume.  This is benign if we never had seen
    // a QUERY.

    (VOID) UDFUnlockVolumeInternal(Vcb, NULL);

    UDFReleaseResource(&Vcb->VcbResource);

    //  Send the request.  The underlying driver will complete the
    //  IRP.  Since we don't need to be in the way, simply ellide
    //  ourselves out of the IRP stack.

    IoSkipCurrentIrpStackLocation(Irp);

    Status = IoCallDriver(Vcb->TargetDeviceObject, Irp);

    UDFCompleteRequest(IrpContext, NULL, STATUS_SUCCESS);

    return Status;
}


//  Local support routine
NTSTATUS
NTAPI
UDFPnpCompletionRoutine (
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Contxt
    )
{
    PKEVENT Event = (PKEVENT) Contxt;

    KeSetEvent( Event, 0, FALSE );

    return STATUS_MORE_PROCESSING_REQUIRED;

    UNREFERENCED_PARAMETER( DeviceObject );
    UNREFERENCED_PARAMETER( Contxt );
}


