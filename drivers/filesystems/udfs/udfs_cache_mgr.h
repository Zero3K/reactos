////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

#ifndef __UDFS_CACHE_MGR_H__
#define __UDFS_CACHE_MGR_H__

#include "udffs.h"

extern "C" {

// Cache manager wrapper functions that choose between implementations

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
    );

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
    );

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
    );

// Flush blocks (wrapper)
NTSTATUS
UdfCacheFlushBlocks(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PVOID Context,
    IN lba_t Lba,
    IN ULONG BCount
    );

// Discard blocks (wrapper)
VOID
UdfCacheDiscardBlocks(
    IN PVCB Vcb,
    IN PVOID Context,
    IN lba_t Lba,
    IN ULONG BCount
    );

// Flush all (wrapper)
VOID
UdfCacheFlushAll(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PVOID Context
    );

// Purge all (wrapper)
VOID
UdfCachePurgeAll(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PVOID Context
    );

// Release cache (wrapper)
VOID
UdfCacheRelease(
    IN PVCB Vcb
    );

// Check if initialized (wrapper)
BOOLEAN
UdfCacheIsInitialized(
    IN PVCB Vcb
    );

// Additional compatibility functions for direct cache access

// Start direct cache access
VOID
UdfCacheStartDirect(
    IN PVCB Vcb,
    IN PVOID Context,
    IN BOOLEAN IsReadOperation
    );

// End direct cache access
VOID
UdfCacheEODirect(
    IN PVCB Vcb,
    IN PVOID Context
    );

// Check if block is cached
BOOLEAN
UdfCacheIsCached(
    IN PVCB Vcb,
    IN lba_t Lba,
    IN ULONG BCount
    );

// Direct cache access
NTSTATUS
UdfCacheDirect(
    IN PIRP_CONTEXT IrpContext,
    IN PVCB Vcb,
    IN PVOID Context,
    IN lba_t Lba,
    IN BOOLEAN Modified,
    OUT PCHAR* CachedBlock,
    IN BOOLEAN CachedOnly
    );

// Additional advanced cache functions

// Get write block count
ULONG
UdfCacheGetWriteBlockCount(
    IN PVCB Vcb
    );

// Change cache flags
NTSTATUS
UdfCacheChFlags(
    IN PVCB Vcb,
    IN ULONG SetFlags,
    IN ULONG ClrFlags
    );

// Set cache mode
NTSTATUS  
UdfCacheSetMode(
    IN PVCB Vcb,
    IN ULONG Mode
    );

// Sync relocation
VOID
UdfCacheSyncReloc(
    IN PVCB Vcb,
    IN PVOID Context
    );

} // extern "C"

#endif // __UDFS_CACHE_MGR_H__