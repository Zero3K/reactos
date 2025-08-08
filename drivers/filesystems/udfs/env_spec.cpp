////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*************************************************************************
*
* File: Env_Spec.cpp
*
* Module: UDF File System Driver (Kernel mode execution only)
*
* Description:
*   Contains environment-secific code to handle physical
*   operations: read, write and device IOCTLS
*
*************************************************************************/

#include "udffs.h"
// define the file specific bug-check id
#define         UDF_BUG_CHECK_ID        UDF_FILE_ENV_SPEC

#define MEASURE_IO_PERFORMANCE

#ifdef MEASURE_IO_PERFORMANCE
LONGLONG IoReadTime=0;
LONGLONG IoWriteTime=0;
LONGLONG WrittenData=0;
LONGLONG IoRelWriteTime=0;
#endif //MEASURE_IO_PERFORMANCE

#ifdef DBG
ULONG UDF_SIMULATE_WRITES=0;
#endif //DBG

/*

 */
NTSTATUS
NTAPI
UDFAsyncCompletionRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Contxt
    )
{
    UDFPrint(("UDFAsyncCompletionRoutine ctx=%x\n", Contxt));
    PUDF_PH_CALL_CONTEXT Context = (PUDF_PH_CALL_CONTEXT)Contxt;
    PMDL Mdl, NextMdl;

    Context->IosbToUse = Irp->IoStatus;
#if 1
    // Unlock pages that are described by MDL (if any)...
    Mdl = Irp->MdlAddress;
    while(Mdl) {
        MmPrint(("    Unlock MDL=%x\n", Mdl));
        MmUnlockPages(Mdl);
        Mdl = Mdl->Next;
    }
    // ... and free MDL
    Mdl = Irp->MdlAddress;
    while(Mdl) {
        MmPrint(("    Free MDL=%x\n", Mdl));
        NextMdl = Mdl->Next;
        IoFreeMdl(Mdl);
        Mdl = NextMdl;
    }
    Irp->MdlAddress = NULL;
    IoFreeIrp(Irp);

    KeSetEvent( &(Context->event), 0, FALSE );

    return STATUS_MORE_PROCESSING_REQUIRED;
#else
    KeSetEvent( &(Context->event), 0, FALSE );

    return STATUS_SUCCESS;
#endif
} // end UDFAsyncCompletionRoutine()

NTSTATUS
NTAPI
UDFSyncCompletionRoutine(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Contxt
    )
{
    UDFPrint(("UDFSyncCompletionRoutine ctx=%x\n", Contxt));
    PUDF_PH_CALL_CONTEXT Context = (PUDF_PH_CALL_CONTEXT)Contxt;

    Context->IosbToUse = Irp->IoStatus;
    //KeSetEvent( &(Context->event), 0, FALSE );

    return STATUS_SUCCESS;
} // end UDFSyncCompletionRoutine()

/*
NTSTATUS
UDFSyncCompletionRoutine2(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Contxt
    )
{
    UDFPrint(("UDFSyncCompletionRoutine2\n"));
    PKEVENT SyncEvent = (PKEVENT)Contxt;

    KeSetEvent( SyncEvent, 0, FALSE );

    return STATUS_SUCCESS;
} // end UDFSyncCompletionRoutine2()
*/

/*

 Function: UDFPhReadSynchronous()

 Description:
    UDFFSD will invoke this rotine to read physical device synchronously/asynchronously

 Expected Interrupt Level (for execution) :

  <= IRQL_DISPATCH_LEVEL

 Return Value: STATUS_SUCCESS/Error

*/
NTSTATUS
NTAPI
UDFPhReadSynchronous(
    PIRP_CONTEXT IrpContext,
    PDEVICE_OBJECT      DeviceObject,   // the physical device object
    PVOID               Buffer,
    SIZE_T              Length,
    LONGLONG            Offset,
    PSIZE_T             ReadBytes,
    ULONG               Flags
    )
{
    NTSTATUS            RC = STATUS_SUCCESS;
    LARGE_INTEGER       ROffset;
    PUDF_PH_CALL_CONTEXT Context;
    PIRP                Irp;
    PIO_STACK_LOCATION IrpSp;
    KIRQL               CurIrql = KeGetCurrentIrql();
    PVOID               IoBuf = NULL;
//    ULONG i;
#ifdef MEASURE_IO_PERFORMANCE
    LONGLONG IoEnterTime;
    LONGLONG IoExitTime;
    ULONG dt;
    ULONG dtm;
#endif //MEASURE_IO_PERFORMANCE
#ifdef _BROWSE_UDF_
    PVCB Vcb = NULL;
    if (Flags & PH_VCB_IN_RETLEN) {
        Vcb = (PVCB)(*ReadBytes);
    }
#endif //_BROWSE_UDF_

#ifdef MEASURE_IO_PERFORMANCE
    KeQuerySystemTime((PLARGE_INTEGER)&IoEnterTime);
#endif //MEASURE_IO_PERFORMANCE

    UDFPrint(("UDFPhRead: Length: %x Lba: %lx\n",Length>>0xb,Offset>>0xb));
//    UDFPrint(("UDFPhRead: Length: %x Lba: %lx\n",Length>>0x9,Offset>>0x9));

    ROffset.QuadPart = Offset;
    (*ReadBytes) = 0;
/*
    // DEBUG !!!
    Flags |= PH_TMP_BUFFER;
*/
    if (Flags & PH_TMP_BUFFER) {
        IoBuf = Buffer;
    } else {
        IoBuf = DbgAllocatePoolWithTag(NonPagedPool, Length, 'bNWD');
    }
    if (!IoBuf) {
        UDFPrint(("    !IoBuf\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Context = (PUDF_PH_CALL_CONTEXT)MyAllocatePool__( NonPagedPool, sizeof(UDF_PH_CALL_CONTEXT) );
    if (!Context) {
        UDFPrint(("    !Context\n"));
        try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
    }
    // Create notification event object to be used to signal the request completion.
    KeInitializeEvent(&(Context->event), NotificationEvent, FALSE);

    if (TRUE || CurIrql > PASSIVE_LEVEL) {
        Irp = IoBuildAsynchronousFsdRequest(IRP_MJ_READ, DeviceObject, IoBuf,
                                               Length, &ROffset, &(Context->IosbToUse) );
        if (!Irp) {
            UDFPrint(("    !irp Async\n"));
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        }
        MmPrint(("    Alloc async Irp MDL=%x, ctx=%x\n", Irp->MdlAddress, Context));
        IoSetCompletionRoutine(Irp, &UDFAsyncCompletionRoutine,
                                Context, TRUE, TRUE, TRUE );
    } else {
        Irp = IoBuildSynchronousFsdRequest(IRP_MJ_READ, DeviceObject, IoBuf,
                                               Length, &ROffset, &(Context->event), &(Context->IosbToUse) );
        if (!Irp) {
            UDFPrint(("    !irp Sync\n"));
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        }
        MmPrint(("    Alloc Irp MDL=%x, ctx=%x\n", Irp->MdlAddress, Context));
    }

    // Setup the next IRP stack location in the associated Irp for the disk
    // driver beneath us.

    IrpSp = IoGetNextIrpStackLocation(Irp);

    //  If this Irp is the result of a WriteThough operation,
    //  tell the device to write it through.

    if (FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WRITE_THROUGH)) {

        SetFlag(IrpSp->Flags, SL_WRITE_THROUGH);
    }

    SetFlag(IrpSp->Flags, SL_OVERRIDE_VERIFY_VOLUME);

    RC = IoCallDriver(DeviceObject, Irp);

    if (RC == STATUS_PENDING) {
        DbgWaitForSingleObject(&(Context->event), NULL);
        if ((RC = Context->IosbToUse.Status) == STATUS_DATA_OVERRUN) {
            RC = STATUS_SUCCESS;
        }
//        *ReadBytes = Context->IosbToUse.Information;
    } else {
//        *ReadBytes = irp->IoStatus.Information;
    }
    if (NT_SUCCESS(RC)) {
        (*ReadBytes) = Context->IosbToUse.Information;
    }
    if (!(Flags & PH_TMP_BUFFER)) {
        RtlCopyMemory(Buffer, IoBuf, *ReadBytes);
    }

    if (NT_SUCCESS(RC)) {
/*
        for(i=0; i<(*ReadBytes); i+=2048) {
            UDFPrint(("IOCRC %8.8x R %x\n", crc32((PUCHAR)Buffer+i, 2048), (ULONG)((Offset+i)/2048) ));
        }
*/
#ifdef _BROWSE_UDF_
        if (Vcb) {
            RC = UDFVRead(Vcb, IoBuf, Length >> Vcb->BlockSizeBits, (ULONG)(Offset >> Vcb->BlockSizeBits), Flags);
        }
#endif //_BROWSE_UDF_
    }

try_exit: NOTHING;

    if (Context) MyFreePool__(Context);
    if (IoBuf && !(Flags & PH_TMP_BUFFER)) DbgFreePool(IoBuf);

#ifdef MEASURE_IO_PERFORMANCE
    KeQuerySystemTime((PLARGE_INTEGER)&IoExitTime);
    IoReadTime += (IoExitTime-IoEnterTime);
    dt = (ULONG)((IoExitTime-IoEnterTime)/10/1000);
    dtm = (ULONG)(((IoExitTime-IoEnterTime)/10)%1000);
    PerfPrint(("\nUDFPhReadSynchronous() exit: %08X, after %d.%4.4d msec.\n", RC, dt, dtm));
#else
    UDFPrint(("UDFPhReadSynchronous() exit: %08X\n", RC));
#endif //MEASURE_IO_PERFORMANCE

    return(RC);
} // end UDFPhReadSynchronous()


/*

 Function: UDFPhWriteSynchronous()

 Description:
    UDFFSD will invoke this rotine to write physical device synchronously

 Expected Interrupt Level (for execution) :

  <= IRQL_DISPATCH_LEVEL

 Return Value: STATUS_SUCCESS/Error

*/
NTSTATUS
NTAPI
UDFPhWriteSynchronous(
    PDEVICE_OBJECT  DeviceObject,   // the physical device object
    PVOID           Buffer,
    SIZE_T          Length,
    LONGLONG        Offset,
    PSIZE_T         WrittenBytes,
    ULONG           Flags
    )
{
    NTSTATUS            RC = STATUS_SUCCESS;
    LARGE_INTEGER       ROffset;
    PUDF_PH_CALL_CONTEXT Context = NULL;
    PIRP                irp;
//    LARGE_INTEGER       timeout;
    KIRQL               CurIrql = KeGetCurrentIrql();
    PVOID               IoBuf = NULL;
//    ULONG i;
#ifdef MEASURE_IO_PERFORMANCE
    LONGLONG IoEnterTime;
    LONGLONG IoExitTime;
    ULONG dt;
    ULONG dtm;
#endif //MEASURE_IO_PERFORMANCE
#ifdef _BROWSE_UDF_
    PVCB Vcb = NULL;
    if (Flags & PH_VCB_IN_RETLEN) {
        Vcb = (PVCB)(*WrittenBytes);
    }
#endif //_BROWSE_UDF_

#ifdef MEASURE_IO_PERFORMANCE
    KeQuerySystemTime((PLARGE_INTEGER)&IoEnterTime);
#endif //MEASURE_IO_PERFORMANCE

#ifdef USE_PERF_PRINT
    ULONG Lba = (ULONG)(Offset>>0xb);
//    ASSERT(!(Lba & (32-1)));
    PerfPrint(("UDFPhWrite: Length: %x Lba: %lx\n",Length>>0xb,Lba));
//    UDFPrint(("UDFPhWrite: Length: %x Lba: %lx\n",Length>>0x9,Offset>>0x9));
#endif //DBG

#ifdef DBG
    if (UDF_SIMULATE_WRITES) {
/* FIXME ReactOS
   If this function is to force a read from the bufffer to simulate any segfaults, then it makes sense.
   Else, this forloop is useless.
        UCHAR a;
        for(ULONG i=0; i<Length; i++) {
            a = ((PUCHAR)Buffer)[i];
        }
*/
        *WrittenBytes = Length;
        return STATUS_SUCCESS;
    }
#endif //DBG

    ROffset.QuadPart = Offset;
    (*WrittenBytes) = 0;

   // Utilizing a temporary buffer to circumvent the situation where the IO buffer contains TransitionPage pages.
   // This typically occurs during IRP_NOCACHE. Otherwise, an assert occurs within IoBuildAsynchronousFsdRequest.
    if (Flags & PH_TMP_BUFFER) {
        IoBuf = Buffer;
    } else {
        IoBuf = DbgAllocatePool(NonPagedPool, Length);
        if (!IoBuf) try_return (RC = STATUS_INSUFFICIENT_RESOURCES);
        RtlCopyMemory(IoBuf, Buffer, Length);
    }

    Context = (PUDF_PH_CALL_CONTEXT)MyAllocatePool__( NonPagedPool, sizeof(UDF_PH_CALL_CONTEXT) );
    if (!Context) try_return (RC = STATUS_INSUFFICIENT_RESOURCES);
    // Create notification event object to be used to signal the request completion.
    KeInitializeEvent(&(Context->event), NotificationEvent, FALSE);

    if (TRUE || CurIrql > PASSIVE_LEVEL) {
        irp = IoBuildAsynchronousFsdRequest(IRP_MJ_WRITE, DeviceObject, IoBuf,
                                               Length, &ROffset, &(Context->IosbToUse) );
        if (!irp) try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        MmPrint(("    Alloc async Irp MDL=%x, ctx=%x\n", irp->MdlAddress, Context));
        IoSetCompletionRoutine( irp, &UDFAsyncCompletionRoutine,
                                Context, TRUE, TRUE, TRUE );
    } else {
        irp = IoBuildSynchronousFsdRequest(IRP_MJ_WRITE, DeviceObject, IoBuf,
                                               Length, &ROffset, &(Context->event), &(Context->IosbToUse) );
        if (!irp) try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        MmPrint(("    Alloc Irp MDL=%x\n, ctx=%x", irp->MdlAddress, Context));
    }

    (IoGetNextIrpStackLocation(irp))->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    RC = IoCallDriver(DeviceObject, irp);
/*
    for(i=0; i<Length; i+=2048) {
        UDFPrint(("IOCRC %8.8x W %x\n", crc32((PUCHAR)Buffer+i, 2048), (ULONG)((Offset+i)/2048) ));
    }
*/
#ifdef _BROWSE_UDF_
    if (Vcb) {
        UDFVWrite(Vcb, IoBuf, Length >> Vcb->BlockSizeBits, (ULONG)(Offset >> Vcb->BlockSizeBits), Flags);
    }
#endif //_BROWSE_UDF_

    if (RC == STATUS_PENDING) {
        DbgWaitForSingleObject(&(Context->event), NULL);
        if ((RC = Context->IosbToUse.Status) == STATUS_DATA_OVERRUN) {
            RC = STATUS_SUCCESS;
        }
//        *WrittenBytes = Context->IosbToUse.Information;
    } else {
//        *WrittenBytes = irp->IoStatus.Information;
    }
    if (NT_SUCCESS(RC)) {
        (*WrittenBytes) = Context->IosbToUse.Information;
    }

try_exit: NOTHING;

    if (Context) MyFreePool__(Context);
    if (IoBuf && !(Flags & PH_TMP_BUFFER)) DbgFreePool(IoBuf);
    if (!NT_SUCCESS(RC)) {
        UDFPrint(("WriteError\n"));
    }

#ifdef MEASURE_IO_PERFORMANCE
    KeQuerySystemTime((PLARGE_INTEGER)&IoExitTime);
    IoWriteTime += (IoExitTime-IoEnterTime);
    if (WrittenData > 1024*1024*8) {
        PerfPrint(("\nUDFPhWriteSynchronous() Relative size=%I64d, time=%I64d.\n", WrittenData, IoRelWriteTime));
        WrittenData = IoRelWriteTime = 0;
    }
    WrittenData += Length;
    IoRelWriteTime += (IoExitTime-IoEnterTime);
    dt = (ULONG)((IoExitTime-IoEnterTime)/10/1000);
    dtm = (ULONG)(((IoExitTime-IoEnterTime)/10)%1000);
    PerfPrint(("\nUDFPhWriteSynchronous() exit: %08X, after %d.%4.4d msec.\n", RC, dt, dtm));
#else
    UDFPrint(("nUDFPhWriteSynchronous() exit: %08X\n", RC));
#endif //MEASURE_IO_PERFORMANCE

    return(RC);
} // end UDFPhWriteSynchronous()


#if 0
/*
 Function: UDFDeviceSupportsScatterGather()

 Description:
    This function has been disabled because filesystem drivers should not
    directly access DMA adapters. The I/O subsystem automatically handles
    scatter-gather optimization when MDLs are used properly.

 Expected Interrupt Level (for execution) :
  IRQL_PASSIVE_LEVEL

 Return Value: TRUE if SGL supported, FALSE otherwise
*/
BOOLEAN
NTAPI
UDFDeviceSupportsScatterGather(
    PDEVICE_OBJECT DeviceObject
    )
{
    DEVICE_DESCRIPTION DeviceDescription;
    PDMA_ADAPTER DmaAdapter;
    ULONG NumberOfMapRegisters;
    BOOLEAN SupportsScatterGather = FALSE;

    UDFPrint(("UDFDeviceSupportsScatterGather: Checking SGL support\n"));

    // Initialize device description for DMA adapter query
    RtlZeroMemory(&DeviceDescription, sizeof(DEVICE_DESCRIPTION));
    DeviceDescription.Version = DEVICE_DESCRIPTION_VERSION;
    DeviceDescription.Master = TRUE;
    DeviceDescription.ScatterGather = TRUE;  // Request SGL capability
    DeviceDescription.Dma32BitAddresses = TRUE;
    DeviceDescription.Dma64BitAddresses = FALSE;
    DeviceDescription.InterfaceType = Internal;
    DeviceDescription.DmaWidth = Width32Bits;
    DeviceDescription.DmaSpeed = Compatible;
    DeviceDescription.MaximumLength = MAXULONG;

    // Try to get DMA adapter - this indicates hardware DMA capability
    DmaAdapter = IoGetDmaAdapter(DeviceObject, &DeviceDescription, &NumberOfMapRegisters);
    
    if (DmaAdapter != NULL) {
        // Check if the adapter supports scatter-gather operations
        if (DmaAdapter->DmaOperations->GetScatterGatherList != NULL &&
            DmaAdapter->DmaOperations->PutScatterGatherList != NULL &&
            DmaAdapter->DmaOperations->BuildScatterGatherList != NULL) {
            
            UDFPrint(("UDFDeviceSupportsScatterGather: SGL operations available\n"));
            SupportsScatterGather = TRUE;
        }
        
        // Release the DMA adapter
        DmaAdapter->DmaOperations->PutDmaAdapter(DmaAdapter);
    }

    UDFPrint(("UDFDeviceSupportsScatterGather: SGL support = %s\n", 
              SupportsScatterGather ? "TRUE" : "FALSE"));
    
    return SupportsScatterGather;
} // end UDFDeviceSupportsScatterGather()
#endif


/*
 Function: UDFValidateSGLConfiguration()

 Description:
    Validates the SGL configuration and reports the current settings.
    This function can be called during driver initialization to verify
    that SGL support is properly configured.

 Expected Interrupt Level (for execution) :
  IRQL_PASSIVE_LEVEL

 Return Value: STATUS_SUCCESS if configuration is valid
*/
NTSTATUS
NTAPI
UDFValidateSGLConfiguration(
    VOID
    )
{
    UDFPrint(("UDFValidateSGLConfiguration: Validating SGL enhancement settings\n"));

#ifdef UDF_USE_SGL_OPTIMIZATION
    UDFPrint(("UDFValidateSGLConfiguration: SGL optimization is ENABLED\n"));
    UDFPrint(("UDFValidateSGLConfiguration: - Large transfers (>=4KB) will use SGL when supported\n"));
    UDFPrint(("UDFValidateSGLConfiguration: - Automatic fallback to synchronous IO available\n"));
    UDFPrint(("UDFValidateSGLConfiguration: - Device capability detection enabled\n"));
#else
    UDFPrint(("UDFValidateSGLConfiguration: SGL optimization is DISABLED\n"));
    UDFPrint(("UDFValidateSGLConfiguration: - Using traditional synchronous IO only\n"));
    UDFPrint(("UDFValidateSGLConfiguration: - To enable SGL, define UDF_USE_SGL_OPTIMIZATION\n"));
#endif // UDF_USE_SGL_OPTIMIZATION

    // Validate that required structures are available
    if (sizeof(SCATTER_GATHER_ELEMENT) == 0 || sizeof(SCATTER_GATHER_LIST) == 0) {
        UDFPrint(("UDFValidateSGLConfiguration: ERROR - SGL structures not available\n"));
        return STATUS_NOT_SUPPORTED;
    }

    UDFPrint(("UDFValidateSGLConfiguration: SGL structures validated successfully\n"));
    UDFPrint(("UDFValidateSGLConfiguration: - SCATTER_GATHER_ELEMENT size: %d bytes\n", 
              sizeof(SCATTER_GATHER_ELEMENT)));
    UDFPrint(("UDFValidateSGLConfiguration: - SCATTER_GATHER_LIST base size: %d bytes\n", 
              sizeof(SCATTER_GATHER_LIST)));

    return STATUS_SUCCESS;
} // end UDFValidateSGLConfiguration()


/*
 Function: UDFPhReadSGL()

 Description:
    Enhanced read function using Scatter-Gather Lists for improved performance.
    This function eliminates the need for intermediate buffer allocation and
    memory copying by using direct DMA to the target MDL.

 Expected Interrupt Level (for execution) :
  <= IRQL_DISPATCH_LEVEL

 Return Value: STATUS_SUCCESS/Error
*/
NTSTATUS
NTAPI
UDFPhReadSGL(
    PIRP_CONTEXT IrpContext,
    PDEVICE_OBJECT DeviceObject,
    PMDL Mdl,
    LONGLONG Offset,
    PSIZE_T ReadBytes,
    ULONG Flags
    )
{
    NTSTATUS RC = STATUS_SUCCESS;
    LARGE_INTEGER ROffset;
    PUDF_PH_CALL_CONTEXT Context = NULL;
    PIRP Irp = NULL;
    PIO_STACK_LOCATION IrpSp;
    KIRQL CurIrql = KeGetCurrentIrql();
    SIZE_T MdlLength;
#ifdef MEASURE_IO_PERFORMANCE
    LONGLONG IoEnterTime;
    LONGLONG IoExitTime;
    ULONG dt;
    ULONG dtm;
#endif //MEASURE_IO_PERFORMANCE

    UDFPrint(("UDFPhReadSGL: Using SGL for enhanced IO performance\n"));

#ifdef MEASURE_IO_PERFORMANCE
    KeQuerySystemTime((PLARGE_INTEGER)&IoEnterTime);
#endif //MEASURE_IO_PERFORMANCE

    if (!Mdl) {
        UDFPrint(("UDFPhReadSGL: Invalid MDL\n"));
        return STATUS_INVALID_PARAMETER;
    }

    MdlLength = MmGetMdlByteCount(Mdl);
    ROffset.QuadPart = Offset;
    (*ReadBytes) = 0;

    UDFPrint(("UDFPhReadSGL: Length: %x Offset: %lx\n", MdlLength, Offset));

    // Allocate context for completion handling
    Context = (PUDF_PH_CALL_CONTEXT)MyAllocatePool__(NonPagedPool, sizeof(UDF_PH_CALL_CONTEXT));
    if (!Context) {
        UDFPrint(("UDFPhReadSGL: Failed to allocate context\n"));
        try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
    }

    // Initialize completion event
    KeInitializeEvent(&(Context->event), NotificationEvent, FALSE);

    // Build IRP using the provided MDL directly - no intermediate buffer needed
    if (TRUE || CurIrql > PASSIVE_LEVEL) {
        Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
        if (!Irp) {
            UDFPrint(("UDFPhReadSGL: Failed to allocate IRP\n"));
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        }

        // Set up the IRP for read operation with SGL
        Irp->MdlAddress = Mdl;
        Irp->UserBuffer = NULL;  // We're using MDL directly
        Irp->Tail.Overlay.Thread = PsGetCurrentThread();
        Irp->RequestorMode = KernelMode;
        Irp->Flags = IRP_READ_OPERATION | IRP_DEFER_IO_COMPLETION;

        // Set up IRP stack location
        IrpSp = IoGetNextIrpStackLocation(Irp);
        IrpSp->MajorFunction = IRP_MJ_READ;
        IrpSp->Parameters.Read.Length = (ULONG)MdlLength;
        IrpSp->Parameters.Read.ByteOffset = ROffset;

        // Set completion routine
        IoSetCompletionRoutine(Irp, &UDFAsyncCompletionRoutine,
                               Context, TRUE, TRUE, TRUE);
    } else {
        // For PASSIVE_LEVEL, we can use synchronous IRP
        Irp = IoBuildSynchronousFsdRequest(IRP_MJ_READ, DeviceObject, 
                                          MmGetSystemAddressForMdl(Mdl),
                                          (ULONG)MdlLength, &ROffset, 
                                          &(Context->event), &(Context->IosbToUse));
        if (!Irp) {
            UDFPrint(("UDFPhReadSGL: Failed to build synchronous IRP\n"));
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        }
        
        // Replace the MDL to use SGL optimization
        if (Irp->MdlAddress) {
            IoFreeMdl(Irp->MdlAddress);
        }
        Irp->MdlAddress = Mdl;
    }

    // Set flags for volume verification override
    IrpSp = IoGetNextIrpStackLocation(Irp);
    if (FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WRITE_THROUGH)) {
        SetFlag(IrpSp->Flags, SL_WRITE_THROUGH);
    }
    SetFlag(IrpSp->Flags, SL_OVERRIDE_VERIFY_VOLUME);

    UDFPrint(("UDFPhReadSGL: Sending IRP with MDL optimization\n"));

    // Submit the IRP
    RC = IoCallDriver(DeviceObject, Irp);

    if (RC == STATUS_PENDING) {
        DbgWaitForSingleObject(&(Context->event), NULL);
        if ((RC = Context->IosbToUse.Status) == STATUS_DATA_OVERRUN) {
            RC = STATUS_SUCCESS;
        }
    }

    if (NT_SUCCESS(RC)) {
        (*ReadBytes) = Context->IosbToUse.Information;
    }

try_exit:
    if (Context) {
        MyFreePool__(Context);
    }

#ifdef MEASURE_IO_PERFORMANCE
    KeQuerySystemTime((PLARGE_INTEGER)&IoExitTime);
    IoReadTime += (IoExitTime - IoEnterTime);
    dt = (ULONG)((IoExitTime - IoEnterTime) / 10 / 1000);
    dtm = (ULONG)(((IoExitTime - IoEnterTime) / 10) % 1000);
    PerfPrint(("\nUDFPhReadSGL() exit: %08X, after %d.%4.4d msec.\n", RC, dt, dtm));
#else
    UDFPrint(("UDFPhReadSGL() exit: %08X\n", RC));
#endif //MEASURE_IO_PERFORMANCE

    return RC;
} // end UDFPhReadSGL()


/*
 Function: UDFPhWriteSGL()

 Description:
    Enhanced write function using Scatter-Gather Lists for improved performance.
    This function eliminates the need for intermediate buffer allocation and
    memory copying by using direct DMA from the source MDL.

 Expected Interrupt Level (for execution) :
  <= IRQL_DISPATCH_LEVEL

 Return Value: STATUS_SUCCESS/Error
*/
NTSTATUS
NTAPI
UDFPhWriteSGL(
    PDEVICE_OBJECT DeviceObject,
    PMDL Mdl,
    LONGLONG Offset,
    PSIZE_T WrittenBytes,
    ULONG Flags
    )
{
    NTSTATUS RC = STATUS_SUCCESS;
    LARGE_INTEGER ROffset;
    PUDF_PH_CALL_CONTEXT Context = NULL;
    PIRP Irp = NULL;
    PIO_STACK_LOCATION IrpSp;
    KIRQL CurIrql = KeGetCurrentIrql();
    SIZE_T MdlLength;
#ifdef MEASURE_IO_PERFORMANCE
    LONGLONG IoEnterTime;
    LONGLONG IoExitTime;
    ULONG dt;
    ULONG dtm;
#endif //MEASURE_IO_PERFORMANCE

    UDFPrint(("UDFPhWriteSGL: Using SGL for enhanced IO performance\n"));

#ifdef MEASURE_IO_PERFORMANCE
    KeQuerySystemTime((PLARGE_INTEGER)&IoEnterTime);
#endif //MEASURE_IO_PERFORMANCE

    if (!Mdl) {
        UDFPrint(("UDFPhWriteSGL: Invalid MDL\n"));
        return STATUS_INVALID_PARAMETER;
    }

    MdlLength = MmGetMdlByteCount(Mdl);
    ROffset.QuadPart = Offset;
    (*WrittenBytes) = 0;

    UDFPrint(("UDFPhWriteSGL: Length: %x Offset: %lx\n", MdlLength, Offset));

    // Allocate context for completion handling
    Context = (PUDF_PH_CALL_CONTEXT)MyAllocatePool__(NonPagedPool, sizeof(UDF_PH_CALL_CONTEXT));
    if (!Context) {
        UDFPrint(("UDFPhWriteSGL: Failed to allocate context\n"));
        try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
    }

    // Initialize completion event
    KeInitializeEvent(&(Context->event), NotificationEvent, FALSE);

    // Build IRP using the provided MDL directly - no intermediate buffer needed
    if (TRUE || CurIrql > PASSIVE_LEVEL) {
        Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
        if (!Irp) {
            UDFPrint(("UDFPhWriteSGL: Failed to allocate IRP\n"));
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        }

        // Set up the IRP for write operation with SGL
        Irp->MdlAddress = Mdl;
        Irp->UserBuffer = NULL;  // We're using MDL directly
        Irp->Tail.Overlay.Thread = PsGetCurrentThread();
        Irp->RequestorMode = KernelMode;
        Irp->Flags = IRP_WRITE_OPERATION | IRP_DEFER_IO_COMPLETION;

        // Set up IRP stack location
        IrpSp = IoGetNextIrpStackLocation(Irp);
        IrpSp->MajorFunction = IRP_MJ_WRITE;
        IrpSp->Parameters.Write.Length = (ULONG)MdlLength;
        IrpSp->Parameters.Write.ByteOffset = ROffset;

        // Set completion routine
        IoSetCompletionRoutine(Irp, &UDFAsyncCompletionRoutine,
                               Context, TRUE, TRUE, TRUE);
    } else {
        // For PASSIVE_LEVEL, we can use synchronous IRP
        Irp = IoBuildSynchronousFsdRequest(IRP_MJ_WRITE, DeviceObject, 
                                          MmGetSystemAddressForMdl(Mdl),
                                          (ULONG)MdlLength, &ROffset, 
                                          &(Context->event), &(Context->IosbToUse));
        if (!Irp) {
            UDFPrint(("UDFPhWriteSGL: Failed to build synchronous IRP\n"));
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        }
        
        // Replace the MDL to use SGL optimization
        if (Irp->MdlAddress) {
            IoFreeMdl(Irp->MdlAddress);
        }
        Irp->MdlAddress = Mdl;
    }

    // Set flags for volume verification override
    IrpSp = IoGetNextIrpStackLocation(Irp);
    SetFlag(IrpSp->Flags, SL_OVERRIDE_VERIFY_VOLUME);

    UDFPrint(("UDFPhWriteSGL: Sending IRP with MDL optimization\n"));

    // Submit the IRP
    RC = IoCallDriver(DeviceObject, Irp);

    if (RC == STATUS_PENDING) {
        DbgWaitForSingleObject(&(Context->event), NULL);
        if ((RC = Context->IosbToUse.Status) == STATUS_DATA_OVERRUN) {
            RC = STATUS_SUCCESS;
        }
    }

    if (NT_SUCCESS(RC)) {
        (*WrittenBytes) = Context->IosbToUse.Information;
    }

try_exit:
    if (Context) {
        MyFreePool__(Context);
    }

#ifdef MEASURE_IO_PERFORMANCE
    KeQuerySystemTime((PLARGE_INTEGER)&IoExitTime);
    IoWriteTime += (IoExitTime - IoEnterTime);
    dt = (ULONG)((IoExitTime - IoEnterTime) / 10 / 1000);
    dtm = (ULONG)(((IoExitTime - IoEnterTime) / 10) % 1000);
    PerfPrint(("\nUDFPhWriteSGL() exit: %08X, after %d.%4.4d msec.\n", RC, dt, dtm));
#else
    UDFPrint(("UDFPhWriteSGL() exit: %08X\n", RC));
#endif //MEASURE_IO_PERFORMANCE

    return RC;
} // end UDFPhWriteSGL()


/*
 Function: UDFPhReadEnhanced()

 Description:
    Enhanced read function that uses MDL-based direct I/O for optimal performance
    on larger transfers. This eliminates intermediate buffer allocation and memory
    copying by using Memory Descriptor Lists (MDLs) to describe user buffers directly.
    The I/O subsystem automatically leverages hardware scatter-gather capabilities
    when available.

 Expected Interrupt Level (for execution) :
  <= IRQL_DISPATCH_LEVEL

 Return Value: STATUS_SUCCESS/Error
*/
NTSTATUS
NTAPI
UDFPhReadEnhanced(
    PIRP_CONTEXT IrpContext,
    PDEVICE_OBJECT DeviceObject,
    PVOID Buffer,
    SIZE_T Length,
    LONGLONG Offset,
    PSIZE_T ReadBytes,
    ULONG Flags
    )
{
    PMDL Mdl = NULL;
    NTSTATUS RC;
    BOOLEAN UseSGL = FALSE;

    // Determine if we should use MDL-based direct I/O for this operation
    // Use MDL optimization for larger transfers where the benefit is significant
    // and when caller doesn't require temporary buffer behavior
    if (!(Flags & PH_TMP_BUFFER) &&  // Don't use MDL when caller wants temp buffer behavior
        Length >= PAGE_SIZE) {        // Use MDL for larger transfers where benefit is significant
        
        UDFPrint(("UDFPhReadEnhanced: Using MDL direct I/O path for length %x\n", Length));
        
        // Create MDL for the buffer
        Mdl = IoAllocateMdl(Buffer, (ULONG)Length, FALSE, FALSE, NULL);
        if (Mdl) {
            _SEH2_TRY {
                // Lock the pages in memory
                MmProbeAndLockPages(Mdl, KernelMode, IoWriteAccess);
                UseSGL = TRUE;
                
                RC = UDFPhReadSGL(IrpContext, DeviceObject, Mdl, Offset, ReadBytes, Flags);
                
            } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                UDFPrint(("UDFPhReadEnhanced: Exception during MDL setup, falling back\n"));
                UseSGL = FALSE;
                RC = STATUS_INVALID_USER_BUFFER;
            } _SEH2_END;
            
            if (UseSGL) {
                // When using SGL path, the I/O subsystem takes ownership of the MDL
                // and automatically unlocks/frees it when the IRP completes.
                // We should NOT manually unlock it here to avoid double-unlock BSOD.
                if (NT_SUCCESS(RC)) {
                    UDFPrint(("UDFPhReadEnhanced: MDL read completed successfully (I/O subsystem handled MDL cleanup)\n"));
                    return RC;
                } else {
                    // Only cleanup manually if SGL operation failed
                    _SEH2_TRY {
                        MmUnlockPages(Mdl);
                    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                        UDFPrint(("UDFPhReadEnhanced: Exception during MDL unlock after failure\n"));
                    } _SEH2_END;
                    IoFreeMdl(Mdl);
                }
            } else {
                IoFreeMdl(Mdl);
            }
        }
    }

    // Fall back to traditional synchronous IO
    UDFPrint(("UDFPhReadEnhanced: Using traditional synchronous IO path\n"));
    return UDFPhReadSynchronous(IrpContext, DeviceObject, Buffer, Length, Offset, ReadBytes, Flags);
    
} // end UDFPhReadEnhanced()


/*
 Function: UDFPhWriteEnhanced()

 Description:
    Enhanced write function that uses MDL-based direct I/O for optimal performance
    on larger transfers. This eliminates intermediate buffer allocation and memory
    copying by using Memory Descriptor Lists (MDLs) to describe user buffers directly.
    The I/O subsystem automatically leverages hardware scatter-gather capabilities
    when available.

 Expected Interrupt Level (for execution) :
  <= IRQL_DISPATCH_LEVEL

 Return Value: STATUS_SUCCESS/Error
*/
NTSTATUS
NTAPI
UDFPhWriteEnhanced(
    PDEVICE_OBJECT DeviceObject,
    PVOID Buffer,
    SIZE_T Length,
    LONGLONG Offset,
    PSIZE_T WrittenBytes,
    ULONG Flags
    )
{
    PMDL Mdl = NULL;
    NTSTATUS RC;
    BOOLEAN UseSGL = FALSE;

    // Determine if we should use MDL-based direct I/O for this operation
    // Use MDL optimization for larger transfers where the benefit is significant
    // and when caller doesn't require temporary buffer behavior
    if (!(Flags & PH_TMP_BUFFER) &&  // Don't use MDL when caller wants temp buffer behavior
        Length >= PAGE_SIZE) {        // Use MDL for larger transfers where benefit is significant
        
        UDFPrint(("UDFPhWriteEnhanced: Using MDL direct I/O path for length %x\n", Length));
        
        // Create MDL for the buffer
        Mdl = IoAllocateMdl(Buffer, (ULONG)Length, FALSE, FALSE, NULL);
        if (Mdl) {
            _SEH2_TRY {
                // Lock the pages in memory
                MmProbeAndLockPages(Mdl, KernelMode, IoReadAccess);
                UseSGL = TRUE;
                
                RC = UDFPhWriteSGL(DeviceObject, Mdl, Offset, WrittenBytes, Flags);
                
            } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                UDFPrint(("UDFPhWriteEnhanced: Exception during MDL setup, falling back\n"));
                UseSGL = FALSE;
                RC = STATUS_INVALID_USER_BUFFER;
            } _SEH2_END;
            
            if (UseSGL) {
                // When using SGL path, the I/O subsystem takes ownership of the MDL
                // and automatically unlocks/frees it when the IRP completes.
                // We should NOT manually unlock it here to avoid double-unlock BSOD.
                if (NT_SUCCESS(RC)) {
                    UDFPrint(("UDFPhWriteEnhanced: MDL write completed successfully (I/O subsystem handled MDL cleanup)\n"));
                    return RC;
                } else {
                    // Only cleanup manually if SGL operation failed
                    _SEH2_TRY {
                        MmUnlockPages(Mdl);
                    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                        UDFPrint(("UDFPhWriteEnhanced: Exception during MDL unlock after failure\n"));
                    } _SEH2_END;
                    IoFreeMdl(Mdl);
                }
            } else {
                IoFreeMdl(Mdl);
            }
        }
    }

    // Fall back to traditional synchronous IO
    UDFPrint(("UDFPhWriteEnhanced: Using traditional synchronous IO path\n"));
    return UDFPhWriteSynchronous(DeviceObject, Buffer, Length, Offset, WrittenBytes, Flags);
    
} // end UDFPhWriteEnhanced()


#if 0
NTSTATUS
UDFPhWriteVerifySynchronous(
    PDEVICE_OBJECT  DeviceObject,   // the physical device object
    PVOID           Buffer,
    SIZE_T          Length,
    LONGLONG        Offset,
    PSIZE_T         WrittenBytes,
    ULONG           Flags
    )
{
    NTSTATUS RC;
    //PUCHAR v_buff = NULL;
    //ULONG ReadBytes;

#ifdef UDF_USE_SGL_OPTIMIZATION
    RC = UDFPhWriteEnhanced(DeviceObject, Buffer, Length, Offset, WrittenBytes, Flags);
#else
#ifdef UDF_USE_SGL_OPTIMIZATION
    RC = UDFPhWriteEnhanced(DeviceObject, Buffer, Length, Offset, WrittenBytes, Flags);
#else
    RC = UDFPhWriteSynchronous(DeviceObject, Buffer, Length, Offset, WrittenBytes, Flags);
#endif // UDF_USE_SGL_OPTIMIZATION
#endif // UDF_USE_SGL_OPTIMIZATION
/*
    if (!Verify)
        return RC;
    v_buff = (PUCHAR)DbgAllocatePoolWithTag(NonPagedPool, Length, 'bNWD');
    if (!v_buff)
        return RC;
    RC = UDFPhReadSynchronous(DeviceObject, v_buff, Length, Offset, &ReadBytes, Flags);
    if (!NT_SUCCESS(RC)) {
        BrutePoint();
        DbgFreePool(v_buff);
        return RC;
    }
    if (RtlCompareMemory(v_buff, Buffer, ReadBytes) == Length) {
        DbgFreePool(v_buff);
        return RC;
    }
    BrutePoint();
    DbgFreePool(v_buff);
    return STATUS_LOST_WRITEBEHIND_DATA;
*/
    return RC;
} // end UDFPhWriteVerifySynchronous()
#endif //0

NTSTATUS
NTAPI
UDFTSendIOCTL(
    IN ULONG IoControlCode,
    IN PVCB Vcb,
    IN PVOID InputBuffer ,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer ,
    IN ULONG OutputBufferLength,
    IN BOOLEAN OverrideVerify,
    OUT PIO_STATUS_BLOCK Iosb OPTIONAL
    )
{
    NTSTATUS            RC = STATUS_SUCCESS;
    BOOLEAN Acquired;

    Acquired = UDFAcquireResourceExclusiveWithCheck(&(Vcb->IoResource));

    _SEH2_TRY {

        RC = UDFPhSendIOCTL(IoControlCode,
                            Vcb->TargetDeviceObject,
                            InputBuffer ,
                            InputBufferLength,
                            OutputBuffer ,
                            OutputBufferLength,
                            OverrideVerify,
                            Iosb
                            );

    } _SEH2_FINALLY {
        if (Acquired)
            UDFReleaseResource(&(Vcb->IoResource));
    } _SEH2_END;

    return RC;
} // end UDFTSendIOCTL()

/*

 Function: UDFPhSendIOCTL()

 Description:
    UDF FSD will invoke this rotine to send IOCTL's to physical
    device

 Return Value: STATUS_SUCCESS/Error

*/
NTSTATUS
NTAPI
UDFPhSendIOCTL(
    IN ULONG IoControlCode,
    IN PDEVICE_OBJECT DeviceObject,
    IN PVOID InputBuffer ,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer ,
    IN ULONG OutputBufferLength,
    IN BOOLEAN OverrideVerify,
    OUT PIO_STATUS_BLOCK Iosb OPTIONAL
    )
{
    NTSTATUS            RC = STATUS_SUCCESS;
    PIRP                irp;
    PUDF_PH_CALL_CONTEXT Context;
    LARGE_INTEGER timeout;

    UDFPrint(("UDFPhDevIOCTL: Code %8x  \n",IoControlCode));

    Context = (PUDF_PH_CALL_CONTEXT)MyAllocatePool__( NonPagedPool, sizeof(UDF_PH_CALL_CONTEXT) );
    if (!Context) return STATUS_INSUFFICIENT_RESOURCES;
    //  Check if the user gave us an Iosb.

    // Create notification event object to be used to signal the request completion.
    KeInitializeEvent(&(Context->event), NotificationEvent, FALSE);

    irp = IoBuildDeviceIoControlRequest(IoControlCode, DeviceObject, InputBuffer ,
        InputBufferLength, OutputBuffer, OutputBufferLength,FALSE,&(Context->event),&(Context->IosbToUse));

    if (!irp) try_return (RC = STATUS_INSUFFICIENT_RESOURCES);
    MmPrint(("    Alloc Irp MDL=%x, ctx=%x\n", irp->MdlAddress, Context));
/*
    if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
        UDFPrint(("Setting completion routine\n"));
        IoSetCompletionRoutine( irp, &UDFSyncCompletionRoutine,
                                Context, TRUE, TRUE, TRUE );
    }
*/
    if (OverrideVerify) {
        (IoGetNextIrpStackLocation(irp))->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    }

    RC = IoCallDriver(DeviceObject, irp);

    if (RC == STATUS_PENDING) {
        ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
        UDFPrint(("Enter wait state on evt %x\n", Context));

        if (KeGetCurrentIrql() > PASSIVE_LEVEL) {
            timeout.QuadPart = -1000;
            UDFPrint(("waiting, TO=%I64d\n", timeout.QuadPart));
            RC = DbgWaitForSingleObject(&(Context->event), &timeout);
            while(RC == STATUS_TIMEOUT) {
                timeout.QuadPart *= 2;
                UDFPrint(("waiting, TO=%I64d\n", timeout.QuadPart));
                RC = DbgWaitForSingleObject(&(Context->event), &timeout);
            }

        } else {
            DbgWaitForSingleObject(&(Context->event), NULL);
        }
        if ((RC = Context->IosbToUse.Status) == STATUS_DATA_OVERRUN) {
            RC = STATUS_SUCCESS;
        }
        UDFPrint(("Exit wait state on evt %x, status %8.8x\n", Context, RC));
/*        if (Iosb) {
            (*Iosb) = Context->IosbToUse;
        }*/
    } else {
        UDFPrint(("No wait completion on evt %x\n", Context));
/*        if (Iosb) {
            (*Iosb) = irp->IoStatus;
        }*/
    }

    if (Iosb) {
        (*Iosb) = Context->IosbToUse;
    }

try_exit: NOTHING;

    if (Context) MyFreePool__(Context);
    return(RC);
} // end UDFPhSendIOCTL()

VOID
UDFNotifyFullReportChange(
    PVCB Vcb,
    PFCB Fcb,
    ULONG Filter,
    ULONG Action
    )
{
    USHORT TargetNameOffset = 0;

    // Skip parent name length and leading backslash from the beginning of object name

    if (Fcb->ParentFcb) {

        if (Fcb->ParentFcb->FCBName->ObjectName.Length == 2) {

            ASSERT(Fcb->ParentFcb->FCBName->ObjectName.Buffer[0] == L'\\');
            TargetNameOffset = Fcb->ParentFcb->FCBName->ObjectName.Length;
        }
        else {

            TargetNameOffset = Fcb->ParentFcb->FCBName->ObjectName.Length + sizeof(WCHAR);
        }
    }

    FsRtlNotifyFullReportChange(Vcb->NotifyIRPMutex,
                                &Vcb->NextNotifyIRP,
                                (PSTRING)&Fcb->FCBName->ObjectName,
                                TargetNameOffset,
                                NULL,
                                NULL,
                                Filter,
                                Action,
                                NULL);
}

