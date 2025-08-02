////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

#include "udffs.h"
#include "udfs_cache_mgr.h"

// define the file specific bug-check id
#define UDF_BUG_CHECK_ID UDF_FILE_WCACHE

// Initialize cache (wrapper)
NTSTATUS 
UdfCacheInit(
    IN PVCB Vcb,
    IN ULONG MaxFrames,
    IN ULONG MaxBlocks,
    IN SIZE_T MaxBytesToRead,
    IN ULONG PacketSizeSh,
    IN ULONG BlockSizeSh,
    IN ULONG BlocksPerFrameSh,
    IN lba_t FirstLba,
    IN lba_t LastLba,
    IN ULONG Mode,
    IN ULONG Flags,
    IN ULONG FramesToKeepFree,
    IN PWRITE_BLOCK WriteProc,
    IN PREAD_BLOCK ReadProc,
    IN PWRITE_BLOCK_ASYNC WriteProcAsync,
    IN PREAD_BLOCK_ASYNC ReadProcAsync,
    IN PCHECK_BLOCK CheckUsedProc,
    IN PUPDATE_RELOC UpdateRelocProc,
    IN PWC_ERROR_HANDLER ErrorHandlerProc
    )
{
#ifdef UDF_USE_SIMPLE_CACHE
    // Use new simplified cache
    ULONG BlockSize = 1 << BlockSizeSh;
    ULONG SimpleCacheMode = (Mode == WCACHE_MODE_RAM || Mode == WCACHE_MODE_RW) ? 
                           UDFS_CACHE_MODE_RW : UDFS_CACHE_MODE_RO;
                           
    return UdfsCacheInit(
        &Vcb->SimpleCache,
        MaxBlocks,
        BlockSize,
        FirstLba,
        LastLba,
        SimpleCacheMode,
        WriteProc,
        ReadProc,
        ErrorHandlerProc
        );
#else
    // Use original WCache implementation
    return WCacheInit__(
        &Vcb->FastCache,
        MaxFrames,
        MaxBlocks,
        MaxBytesToRead,
        PacketSizeSh,
        BlockSizeSh,
        BlocksPerFrameSh,
        FirstLba,
        LastLba,
        Mode,
        Flags,
        FramesToKeepFree,
        WriteProc,
        ReadProc,
        WriteProcAsync,
        ReadProcAsync,
        CheckUsedProc,
        UpdateRelocProc,
        ErrorHandlerProc
        );
#endif
}

// Read blocks (wrapper)
NTSTATUS
UdfCacheReadBlocks(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PVOID Context,
    IN PCHAR Buffer,
    IN lba_t Lba,
    IN ULONG BCount,
    OUT PSIZE_T ReadBytes,
    IN BOOLEAN CachedOnly
    )
{
#ifdef UDF_USE_SIMPLE_CACHE
    return UdfsCacheReadBlocks(
        IrpContext,
        &Vcb->SimpleCache,
        Context,
        Buffer,
        Lba,
        BCount,
        ReadBytes,
        CachedOnly
        );
#else
    return WCacheReadBlocks__(
        IrpContext,
        &Vcb->FastCache,
        Context,
        Buffer,
        Lba,
        BCount,
        ReadBytes,
        CachedOnly
        );
#endif
}

// Write blocks (wrapper)
NTSTATUS
UdfCacheWriteBlocks(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PVOID Context,
    IN PCHAR Buffer,
    IN lba_t Lba,
    IN ULONG BCount,
    OUT PSIZE_T WrittenBytes,
    IN BOOLEAN CachedOnly
    )
{
#ifdef UDF_USE_SIMPLE_CACHE
    return UdfsCacheWriteBlocks(
        IrpContext,
        &Vcb->SimpleCache,
        Context,
        Buffer,
        Lba,
        BCount,
        WrittenBytes,
        CachedOnly
        );
#else
    return WCacheWriteBlocks__(
        IrpContext,
        &Vcb->FastCache,
        Context,
        Buffer,
        Lba,
        BCount,
        WrittenBytes,
        CachedOnly
        );
#endif
}

// Flush blocks (wrapper)
NTSTATUS
UdfCacheFlushBlocks(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PVOID Context,
    IN lba_t Lba,
    IN ULONG BCount
    )
{
#ifdef UDF_USE_SIMPLE_CACHE
    return UdfsCacheFlushBlocks(
        IrpContext,
        &Vcb->SimpleCache,
        Context,
        Lba,
        BCount
        );
#else
    return WCacheFlushBlocks__(
        IrpContext,
        &Vcb->FastCache,
        Context,
        Lba,
        BCount
        );
#endif
}

// Discard blocks (wrapper)
VOID
UdfCacheDiscardBlocks(
    IN PVCB Vcb,
    IN PVOID Context,
    IN lba_t Lba,
    IN ULONG BCount
    )
{
#ifdef UDF_USE_SIMPLE_CACHE
    UdfsCacheDiscardBlocks(
        &Vcb->SimpleCache,
        Lba,
        BCount
        );
#else
    WCacheDiscardBlocks__(
        &Vcb->FastCache,
        Context,
        Lba,
        BCount
        );
#endif
}

// Flush all (wrapper)
VOID
UdfCacheFlushAll(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PVOID Context
    )
{
#ifdef UDF_USE_SIMPLE_CACHE
    UdfsCacheFlushAll(
        IrpContext,
        &Vcb->SimpleCache,
        Context
        );
#else
    WCacheFlushAll__(
        IrpContext,
        &Vcb->FastCache,
        Context
        );
#endif
}

// Purge all (wrapper)
VOID
UdfCachePurgeAll(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PVOID Context
    )
{
#ifdef UDF_USE_SIMPLE_CACHE
    UNREFERENCED_PARAMETER(IrpContext);
    UNREFERENCED_PARAMETER(Context);
    UdfsCachePurgeAll(&Vcb->SimpleCache);
#else
    WCachePurgeAll__(IrpContext, &Vcb->FastCache, Context);
#endif
}

// Release cache (wrapper)
VOID
UdfCacheRelease(
    IN PVCB Vcb
    )
{
#ifdef UDF_USE_SIMPLE_CACHE
    UdfsCacheRelease(&Vcb->SimpleCache);
#else
    WCacheRelease__(&Vcb->FastCache);
#endif
}

// Check if initialized (wrapper)
BOOLEAN
UdfCacheIsInitialized(
    IN PVCB Vcb
    )
{
#ifdef UDF_USE_SIMPLE_CACHE
    return UdfsCacheIsInitialized(&Vcb->SimpleCache);
#else
    return WCacheIsInitialized__(&Vcb->FastCache);
#endif
}