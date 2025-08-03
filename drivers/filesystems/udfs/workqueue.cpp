////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////
/*************************************************************************
*
* File: workqueue.cpp
*
* Module: UDF File System Driver (Kernel mode execution only)
*
* Description:
*   This file contains the improved work and overflow queue management
*   system for the UDF file system driver. This replaces the original
*   simple threshold-based queuing with a more sophisticated system
*   that provides better scalability, dynamic thresholds, and reduced
*   contention.
*
*************************************************************************/

#include "udffs.h"
#define UDF_BUG_CHECK_ID    UDF_FILE_WORKQUEUE

/*************************************************************************
*
* Function: UDFInitializeWorkQueueManager()
*
* Description:
*   Initialize a new work queue manager for a VCB. This sets up the
*   priority queues, statistics tracking, and initial thresholds.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: STATUS_SUCCESS/Error
*
*************************************************************************/
NTSTATUS
UDFInitializeWorkQueueManager(
    _Out_ PUDF_WORK_QUEUE_MANAGER* Manager,
    _In_ PVCB Vcb
    )
{
    PUDF_WORK_QUEUE_MANAGER NewManager = NULL;
    ULONG i;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    *Manager = NULL;

    // Allocate the work queue manager structure
    NewManager = (PUDF_WORK_QUEUE_MANAGER)MyAllocatePool__(
        NonPagedPool, 
        sizeof(UDF_WORK_QUEUE_MANAGER)
    );
    
    if (!NewManager) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Initialize the structure
    RtlZeroMemory(NewManager, sizeof(UDF_WORK_QUEUE_MANAGER));

    // Set up node identification
    NewManager->NodeIdentifier.NodeTypeCode = UDF_NODE_TYPE_WORK_QUEUE_MANAGER;
    NewManager->NodeIdentifier.NodeByteSize = sizeof(UDF_WORK_QUEUE_MANAGER);

    // Initialize priority queues
    for (i = 0; i < UdfWorkQueueMax; i++) {
        InitializeListHead(&NewManager->PriorityQueues[i].QueueHead);
        NewManager->PriorityQueues[i].QueueCount = 0;
        NewManager->PriorityQueues[i].ProcessedCount = 0;
        KeInitializeSpinLock(&NewManager->PriorityQueues[i].QueueLock);
    }

    // Initialize statistics
    RtlZeroMemory(&NewManager->Stats, sizeof(UDF_WORK_QUEUE_STATS));
    KeInitializeSpinLock(&NewManager->StatsLock);
    KeQuerySystemTime(&NewManager->Stats.LastStatsReset);

    // Set up dynamic thresholds based on system size
    switch (MmQuerySystemSize()) {
        case MmLargeSystem:
            NewManager->MaxWorkerThreads = UDF_DEFAULT_MAX_WORKERS * 2;
            NewManager->WorkerThreshold = UDF_DEFAULT_WORKER_THRESHOLD * 2;
            NewManager->OverflowThreshold = UDF_DEFAULT_OVERFLOW_THRESHOLD * 2;
            break;
        case MmMediumSystem:
            NewManager->MaxWorkerThreads = UDF_DEFAULT_MAX_WORKERS;
            NewManager->WorkerThreshold = UDF_DEFAULT_WORKER_THRESHOLD;
            NewManager->OverflowThreshold = UDF_DEFAULT_OVERFLOW_THRESHOLD;
            break;
        case MmSmallSystem:
        default:
            NewManager->MaxWorkerThreads = UDF_DEFAULT_MAX_WORKERS / 2;
            NewManager->WorkerThreshold = UDF_DEFAULT_WORKER_THRESHOLD / 2;
            NewManager->OverflowThreshold = UDF_DEFAULT_OVERFLOW_THRESHOLD / 2;
            break;
    }

    // Set minimum thresholds
    if (NewManager->MaxWorkerThreads < UDF_DEFAULT_MIN_WORKERS) {
        NewManager->MaxWorkerThreads = UDF_DEFAULT_MIN_WORKERS;
    }
    if (NewManager->WorkerThreshold < 2) {
        NewManager->WorkerThreshold = 2;
    }
    if (NewManager->OverflowThreshold < NewManager->WorkerThreshold) {
        NewManager->OverflowThreshold = NewManager->WorkerThreshold * 2;
    }

    NewManager->MinWorkerThreads = UDF_DEFAULT_MIN_WORKERS;
    NewManager->CurrentWorkerThreads = 0;
    NewManager->BackpressureThreshold = UDF_DEFAULT_BACKPRESSURE_THRESHOLD;
    NewManager->RejectThreshold = UDF_DEFAULT_REJECT_THRESHOLD;

    // Initialize flow control
    NewManager->AcceptingRequests = TRUE;
    NewManager->SystemLoadFactor = 50; // Assume moderate load initially
    KeQuerySystemTime(&NewManager->LastLoadCheck);

    // Initialize worker thread management
    ExInitializeWorkItem(
        &NewManager->WorkerItem,
        UDFWorkQueueWorkerThread,
        NewManager
    );
    KeInitializeEvent(&NewManager->WorkerEvent, NotificationEvent, FALSE);
    NewManager->ShutdownRequested = FALSE;

    // Store reference to parent VCB
    NewManager->Vcb = Vcb;

    *Manager = NewManager;
    return STATUS_SUCCESS;
}

/*************************************************************************
*
* Function: UDFCleanupWorkQueueManager()
*
* Description:
*   Clean up and deallocate a work queue manager. This waits for all
*   outstanding work items to complete before cleaning up.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: None
*
*************************************************************************/
VOID
UDFCleanupWorkQueueManager(
    _In_ PUDF_WORK_QUEUE_MANAGER Manager
    )
{
    LARGE_INTEGER Timeout;
    ULONG WaitCount = 0;
    PUDF_WORK_CONTEXT WorkContext;
    ULONG i;
    KIRQL OldIrql;

    PAGED_CODE();

    if (!Manager) {
        return;
    }

    UDFPrint(("UDFCleanupWorkQueueManager: Shutting down work queue manager\n"));

    // Signal shutdown to worker threads
    Manager->ShutdownRequested = TRUE;
    Manager->AcceptingRequests = FALSE;
    KeSetEvent(&Manager->WorkerEvent, 0, FALSE);

    // Wait for worker threads to finish (up to 30 seconds)
    Timeout.QuadPart = -300000000LL; // 30 seconds
    
    while (Manager->CurrentWorkerThreads > 0 && WaitCount < 60) {
        Timeout.QuadPart = -5000000LL; // 500ms
        KeDelayExecutionThread(KernelMode, FALSE, &Timeout);
        WaitCount++;
        
        if (WaitCount % 10 == 0) {
            UDFPrint(("UDFCleanupWorkQueueManager: Still waiting for %d worker threads\n", 
                     Manager->CurrentWorkerThreads));
        }
    }

    // Force completion of any remaining work items
    for (i = 0; i < UdfWorkQueueMax; i++) {
        KeAcquireSpinLock(&Manager->PriorityQueues[i].QueueLock, &OldIrql);
        
        while (!IsListEmpty(&Manager->PriorityQueues[i].QueueHead)) {
            PLIST_ENTRY Entry = RemoveHeadList(&Manager->PriorityQueues[i].QueueHead);
            Manager->PriorityQueues[i].QueueCount--;
            
            WorkContext = CONTAINING_RECORD(Entry, UDF_WORK_CONTEXT, WorkQueueLinks);
            
            KeReleaseSpinLock(&Manager->PriorityQueues[i].QueueLock, OldIrql);
            
            UDFPrint(("UDFCleanupWorkQueueManager: Force completing work item at priority %d\n", i));
            
            // Complete the IRP with an error status
            if (WorkContext->IrpContext && WorkContext->IrpContext->Irp) {
                UDFCompleteRequest(WorkContext->IrpContext, 
                                 WorkContext->IrpContext->Irp, 
                                 STATUS_CANCELLED);
            } else if (WorkContext->IrpContext) {
                UDFCleanupIrpContext(WorkContext->IrpContext, FALSE);
            }
            
            // Free the work context
            MyFreePool__(WorkContext);
            
            KeAcquireSpinLock(&Manager->PriorityQueues[i].QueueLock, &OldIrql);
        }
        
        KeReleaseSpinLock(&Manager->PriorityQueues[i].QueueLock, OldIrql);
    }

    UDFPrint(("UDFCleanupWorkQueueManager: Final statistics - Queued: %d, Processed: %d, Max Concurrent: %d\n",
             Manager->Stats.TotalQueued, Manager->Stats.TotalProcessed, Manager->Stats.MaxConcurrent));

    // Free the manager structure
    MyFreePool__(Manager);
}

/*************************************************************************
*
* Function: UDFQueueWorkItem()
*
* Description:
*   Queue a work item for processing. This determines the appropriate
*   priority queue and handles overflow and backpressure situations.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL <= DISPATCH_LEVEL
*
* Return Value: STATUS_SUCCESS/Error
*
*************************************************************************/
NTSTATUS
UDFQueueWorkItem(
    _In_ PUDF_WORK_QUEUE_MANAGER Manager,
    _In_ PIRP_CONTEXT IrpContext,
    _In_ UDF_WORK_QUEUE_PRIORITY Priority
    )
{
    PUDF_WORK_CONTEXT WorkContext = NULL;
    KIRQL OldIrql;
    ULONG TotalQueued;
    BOOLEAN ShouldCreateWorker = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(Manager != NULL);
    ASSERT(IrpContext != NULL);
    ASSERT(Priority < UdfWorkQueueMax);

    // Check if we're accepting new requests
    if (!Manager->AcceptingRequests) {
        return STATUS_DEVICE_NOT_READY;
    }

    // Update system load periodically
    UDFUpdateSystemLoad(Manager);

    // Calculate total queued items across all priorities
    TotalQueued = 0;
    for (ULONG i = 0; i < UdfWorkQueueMax; i++) {
        TotalQueued += Manager->PriorityQueues[i].QueueCount;
    }

    // Apply backpressure if we're getting overloaded
    if (TotalQueued > Manager->BackpressureThreshold) {
        UDFPrint(("UDFQueueWorkItem: Applying backpressure, total queued: %d\n", TotalQueued));
        
        // For non-critical operations, delay them a bit
        if (Priority != UdfWorkQueueCritical) {
            LARGE_INTEGER Delay;
            Delay.QuadPart = -100000LL; // 10ms delay
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        }
    }

    // Reject requests if we're completely overloaded
    if (TotalQueued > Manager->RejectThreshold) {
        UDFPrint(("UDFQueueWorkItem: Rejecting request, total queued: %d\n", TotalQueued));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Allocate work context
    WorkContext = (PUDF_WORK_CONTEXT)MyAllocatePool__(
        NonPagedPool, 
        sizeof(UDF_WORK_CONTEXT)
    );
    
    if (!WorkContext) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Initialize work context
    RtlZeroMemory(WorkContext, sizeof(UDF_WORK_CONTEXT));
    WorkContext->NodeIdentifier.NodeTypeCode = UDF_NODE_TYPE_WORK_CONTEXT;
    WorkContext->NodeIdentifier.NodeByteSize = sizeof(UDF_WORK_CONTEXT);
    WorkContext->Priority = Priority;
    WorkContext->IrpContext = IrpContext;
    WorkContext->Manager = Manager;
    KeQuerySystemTime(&WorkContext->QueueTime);

    // Queue the work item to the appropriate priority queue
    KeAcquireSpinLock(&Manager->PriorityQueues[Priority].QueueLock, &OldIrql);
    
    InsertTailList(&Manager->PriorityQueues[Priority].QueueHead, 
                   &WorkContext->WorkQueueLinks);
    Manager->PriorityQueues[Priority].QueueCount++;
    
    KeReleaseSpinLock(&Manager->PriorityQueues[Priority].QueueLock, OldIrql);

    // Update statistics
    KeAcquireSpinLock(&Manager->StatsLock, &OldIrql);
    Manager->Stats.TotalQueued++;
    Manager->Stats.CurrentQueued = TotalQueued + 1;
    KeReleaseSpinLock(&Manager->StatsLock, OldIrql);

    // Determine if we should create a new worker thread
    if (Manager->CurrentWorkerThreads < Manager->MaxWorkerThreads) {
        // Create worker if we have enough queued items
        if (TotalQueued >= Manager->WorkerThreshold || 
            Priority == UdfWorkQueueCritical) {
            ShouldCreateWorker = TRUE;
        }
        
        // Always maintain minimum number of workers
        if (Manager->CurrentWorkerThreads < Manager->MinWorkerThreads) {
            ShouldCreateWorker = TRUE;
        }
    }

    // Create worker thread if needed
    if (ShouldCreateWorker) {
        InterlockedIncrement((PLONG)&Manager->CurrentWorkerThreads);
        
        // Update max concurrent statistic
        KeAcquireSpinLock(&Manager->StatsLock, &OldIrql);
        if (Manager->CurrentWorkerThreads > Manager->Stats.MaxConcurrent) {
            Manager->Stats.MaxConcurrent = Manager->CurrentWorkerThreads;
        }
        KeReleaseSpinLock(&Manager->StatsLock, OldIrql);
        
        ExQueueWorkItem(&Manager->WorkerItem, CriticalWorkQueue);
        
        UDFPrint(("UDFQueueWorkItem: Created worker thread, now have %d workers\n", 
                 Manager->CurrentWorkerThreads));
    } else {
        // Signal existing worker threads that there's work available
        KeSetEvent(&Manager->WorkerEvent, 0, FALSE);
    }

    return STATUS_SUCCESS;
}

/*************************************************************************
*
* Function: UDFDequeueWorkItem()
*
* Description:
*   Dequeue the next work item for processing. This implements priority
*   scheduling and returns the highest priority work available.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL <= DISPATCH_LEVEL
*
* Return Value: STATUS_SUCCESS if work item found, STATUS_NO_MORE_ENTRIES if none
*
*************************************************************************/
NTSTATUS
UDFDequeueWorkItem(
    _In_ PUDF_WORK_QUEUE_MANAGER Manager,
    _Out_ PUDF_WORK_CONTEXT* WorkContext
    )
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    ULONG i;

    ASSERT(Manager != NULL);
    ASSERT(WorkContext != NULL);

    *WorkContext = NULL;

    // Search priority queues from highest to lowest priority
    for (i = 0; i < UdfWorkQueueMax; i++) {
        KeAcquireSpinLock(&Manager->PriorityQueues[i].QueueLock, &OldIrql);
        
        if (!IsListEmpty(&Manager->PriorityQueues[i].QueueHead)) {
            Entry = RemoveHeadList(&Manager->PriorityQueues[i].QueueHead);
            Manager->PriorityQueues[i].QueueCount--;
            Manager->PriorityQueues[i].ProcessedCount++;
            
            KeReleaseSpinLock(&Manager->PriorityQueues[i].QueueLock, OldIrql);
            
            *WorkContext = CONTAINING_RECORD(Entry, UDF_WORK_CONTEXT, WorkQueueLinks);
            
            // Update statistics
            KeAcquireSpinLock(&Manager->StatsLock, &OldIrql);
            Manager->Stats.TotalProcessed++;
            Manager->Stats.CurrentQueued--;
            KeReleaseSpinLock(&Manager->StatsLock, OldIrql);
            
            return STATUS_SUCCESS;
        }
        
        KeReleaseSpinLock(&Manager->PriorityQueues[i].QueueLock, OldIrql);
    }

    return STATUS_NO_MORE_ENTRIES;
}

/*************************************************************************
*
* Function: UDFWorkQueueWorkerThread()
*
* Description:
*   Main worker thread routine. This processes work items from the
*   priority queues until shutdown is requested or no more work is available.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL_PASSIVE_LEVEL
*
* Return Value: None
*
*************************************************************************/
VOID
NTAPI
UDFWorkQueueWorkerThread(
    _In_ PVOID Context
    )
{
    PUDF_WORK_QUEUE_MANAGER Manager = (PUDF_WORK_QUEUE_MANAGER)Context;
    PUDF_WORK_CONTEXT WorkContext;
    NTSTATUS Status;
    ULONG IdleCount = 0;
    LARGE_INTEGER WaitTime;
    BOOLEAN ShouldExit = FALSE;

    PAGED_CODE();

    ASSERT(Manager != NULL);

    UDFPrint(("UDFWorkQueueWorkerThread: Worker thread starting, current workers: %d\n", 
             Manager->CurrentWorkerThreads));

    FsRtlEnterFileSystem();

    while (!Manager->ShutdownRequested && !ShouldExit) {
        
        // Try to get work
        Status = UDFDequeueWorkItem(Manager, &WorkContext);
        
        if (NT_SUCCESS(Status) && WorkContext) {
            // Process the work item
            IdleCount = 0;
            
            ASSERT(WorkContext->IrpContext != NULL);
            
            // Set up thread context for FSP processing
            IoSetTopLevelIrp((PIRP)FSRTL_FSP_TOP_LEVEL_IRP);
            WorkContext->IrpContext->Flags |= IRP_CONTEXT_FLAG_WAIT;
            
            _SEH2_TRY {
                // Process the request based on major function
                switch (WorkContext->IrpContext->MajorFunction) {
                    case IRP_MJ_CREATE:
                        Status = UDFCommonCreate(WorkContext->IrpContext, 
                                               WorkContext->IrpContext->Irp);
                        break;
                    case IRP_MJ_READ:
                        Status = UDFCommonRead(WorkContext->IrpContext, 
                                             WorkContext->IrpContext->Irp);
                        break;
                    case IRP_MJ_WRITE:
                        Status = UDFCommonWrite(WorkContext->IrpContext, 
                                              WorkContext->IrpContext->Irp);
                        break;
                    case IRP_MJ_CLEANUP:
                        Status = UDFCommonCleanup(WorkContext->IrpContext, 
                                                WorkContext->IrpContext->Irp);
                        break;
                    case IRP_MJ_CLOSE:
                        Status = UDFCommonClose(WorkContext->IrpContext, 
                                              WorkContext->IrpContext->Irp, TRUE);
                        break;
                    case IRP_MJ_DIRECTORY_CONTROL:
                        Status = UDFCommonDirControl(WorkContext->IrpContext, 
                                                   WorkContext->IrpContext->Irp);
                        break;
                    case IRP_MJ_QUERY_INFORMATION:
                        Status = UDFCommonQueryInfo(WorkContext->IrpContext, 
                                                  WorkContext->IrpContext->Irp);
                        break;
                    case IRP_MJ_SET_INFORMATION:
                        Status = UDFCommonSetInfo(WorkContext->IrpContext, 
                                                WorkContext->IrpContext->Irp);
                        break;
                    case IRP_MJ_QUERY_VOLUME_INFORMATION:
                        Status = UDFCommonQueryVolInfo(WorkContext->IrpContext, 
                                                     WorkContext->IrpContext->Irp);
                        break;
                    case IRP_MJ_SET_VOLUME_INFORMATION:
                        Status = UDFCommonSetVolInfo(WorkContext->IrpContext, 
                                                   WorkContext->IrpContext->Irp);
                        break;
                    default:
                        UDFPrint(("UDFWorkQueueWorkerThread: Unhandled major function %d\n", 
                                 WorkContext->IrpContext->MajorFunction));
                        Status = STATUS_INVALID_DEVICE_REQUEST;
                        UDFCompleteRequest(WorkContext->IrpContext, 
                                         WorkContext->IrpContext->Irp, Status);
                        break;
                }
            } _SEH2_EXCEPT(UDFExceptionFilter(WorkContext->IrpContext, _SEH2_GetExceptionInformation())) {
                Status = UDFProcessException(WorkContext->IrpContext, 
                                           WorkContext->IrpContext->Irp);
                UDFLogEvent(UDF_ERROR_INTERNAL_ERROR, Status);
            } _SEH2_END;
            
            IoSetTopLevelIrp(NULL);
            
            // Free the work context
            MyFreePool__(WorkContext);
            
        } else {
            // No work available, increment idle count
            IdleCount++;
            
            // If we've been idle too long and we have more than minimum workers, exit
            if (IdleCount > 10 && Manager->CurrentWorkerThreads > Manager->MinWorkerThreads) {
                ShouldExit = TRUE;
                break;
            }
            
            // Wait for work to become available or timeout
            WaitTime.QuadPart = -50000000LL; // 5 second timeout
            KeWaitForSingleObject(&Manager->WorkerEvent, Executive, KernelMode, FALSE, &WaitTime);
            
            // Reset the event if we're continuing
            if (!Manager->ShutdownRequested) {
                KeClearEvent(&Manager->WorkerEvent);
            }
        }
    }

    FsRtlExitFileSystem();
    
    // Decrement worker thread count
    InterlockedDecrement((PLONG)&Manager->CurrentWorkerThreads);
    
    UDFPrint(("UDFWorkQueueWorkerThread: Worker thread exiting, remaining workers: %d\n", 
             Manager->CurrentWorkerThreads));
}

/*************************************************************************
*
* Function: UDFUpdateSystemLoad()
*
* Description:
*   Update the system load factor used for dynamic threshold adjustment.
*   This is a simplified load estimation based on available system resources.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL <= DISPATCH_LEVEL
*
* Return Value: None
*
*************************************************************************/
VOID
UDFUpdateSystemLoad(
    _In_ PUDF_WORK_QUEUE_MANAGER Manager
    )
{
    LARGE_INTEGER CurrentTime;
    LARGE_INTEGER TimeDiff;
    KIRQL OldIrql;

    KeQuerySystemTime(&CurrentTime);
    TimeDiff.QuadPart = CurrentTime.QuadPart - Manager->LastLoadCheck.QuadPart;

    // Only update load every second to avoid overhead
    if (TimeDiff.QuadPart >= UDF_SYSTEM_LOAD_CHECK_INTERVAL) {
        
        KeAcquireSpinLock(&Manager->StatsLock, &OldIrql);
        
        // Simple load estimation based on queue depth and worker utilization
        ULONG QueueDepth = Manager->Stats.CurrentQueued;
        ULONG WorkerUtilization = (Manager->CurrentWorkerThreads * 100) / 
                                 (Manager->MaxWorkerThreads + 1);
        
        // Combine queue depth and worker utilization for load factor
        Manager->SystemLoadFactor = min(100, (QueueDepth * 10) + WorkerUtilization);
        
        Manager->LastLoadCheck = CurrentTime;
        
        KeReleaseSpinLock(&Manager->StatsLock, OldIrql);
        
        // Adjust thresholds based on load
        UDFAdjustWorkerThreads(Manager);
    }
}

/*************************************************************************
*
* Function: UDFAdjustWorkerThreads()
*
* Description:
*   Dynamically adjust worker thread limits and thresholds based on
*   current system load and performance metrics.
*
* Expected Interrupt Level (for execution) :
*
*  IRQL <= DISPATCH_LEVEL
*
* Return Value: None
*
*************************************************************************/
VOID
UDFAdjustWorkerThreads(
    _In_ PUDF_WORK_QUEUE_MANAGER Manager
    )
{
    // Under high load, be more aggressive about creating workers
    if (Manager->SystemLoadFactor > 80) {
        Manager->WorkerThreshold = max(1, Manager->WorkerThreshold / 2);
    } 
    // Under low load, be more conservative
    else if (Manager->SystemLoadFactor < 20) {
        Manager->WorkerThreshold = min(8, Manager->WorkerThreshold * 2);
    }
    
    // Adjust overflow threshold based on current performance
    if (Manager->Stats.CurrentQueued > Manager->OverflowThreshold) {
        // Increase overflow threshold if we're consistently over it
        Manager->OverflowThreshold = min(32, Manager->OverflowThreshold + 2);
    } else if (Manager->Stats.CurrentQueued < Manager->OverflowThreshold / 4) {
        // Decrease overflow threshold if we're consistently under it
        Manager->OverflowThreshold = max(4, Manager->OverflowThreshold - 1);
    }
}