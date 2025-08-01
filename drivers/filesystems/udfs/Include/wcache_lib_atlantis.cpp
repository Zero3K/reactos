////////////////////////////////////////////////////////////////////
// Atlantis Cache System - WCache Library Implementation
// Copyright (C) Rogerio Regis, adapted for ReactOS
// This replaces the original wcache_lib.cpp with Atlantis implementation
////////////////////////////////////////////////////////////////////

#include "udffs.h"

// Define the file specific bug-check id
#define UDF_BUG_CHECK_ID UDF_FILE_WCACHE

// Include Atlantis components
#include "atlantis/wcache_compat.h"

extern "C" {

//**************************************************************************************
//*		WCacheInit__ - Initialize Atlantis cache with WCache API
//*************************************************************************************
NTSTATUS
WCacheInit__(
    IN PW_CACHE Cache,
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
    IN PWC_ERROR_HANDLER ErrorHandlerProc)
{
    NTSTATUS status = STATUS_SUCCESS;

    _SEH2_TRY {
        if (!Cache) {
            status = STATUS_INVALID_PARAMETER;
            _SEH2_LEAVE;
        }

        // Initialize the cache structure
        RtlZeroMemory(Cache, sizeof(W_CACHE));
        Cache->Tag = 0x41544C41; // 'ATLA'

        // Create Atlantis cache instance
        Atlantis::AtlantisWCache* atlantisCache = new Atlantis::AtlantisWCache();
        if (!atlantisCache) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            _SEH2_LEAVE;
        }

        // Initialize Atlantis cache
        status = atlantisCache->initialize(MaxFrames, MaxBlocks, MaxBytesToRead,
                                         PacketSizeSh, BlockSizeSh, BlocksPerFrameSh,
                                         FirstLba, LastLba, Mode, Flags, FramesToKeepFree,
                                         WriteProc, ReadProc, WriteProcAsync, ReadProcAsync,
                                         CheckUsedProc, UpdateRelocProc, ErrorHandlerProc);

        if (!NT_SUCCESS(status)) {
            delete atlantisCache;
            _SEH2_LEAVE;
        }

        // Store Atlantis cache instance
        Cache->AtlantisCache = atlantisCache;

        // Initialize resource lock
        ExInitializeResourceLite(&Cache->WCacheLock);

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        status = _SEH2_GetExceptionCode();
    } _SEH2_END;

    return status;
}

//**************************************************************************************
//*		WCacheReadBlocks__ - Read blocks using Atlantis cache
//*************************************************************************************
NTSTATUS
WCacheReadBlocks__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN PCHAR Buffer,
    IN lba_t Lba,
    IN ULONG BCount,
    OUT PSIZE_T ReadBytes,
    IN BOOLEAN CachedOnly)
{
    NTSTATUS status = STATUS_SUCCESS;

    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY {
        Atlantis::AtlantisWCache* atlantisCache = 
            (Atlantis::AtlantisWCache*)Cache->AtlantisCache;

        status = atlantisCache->readBlocks(IrpContext, Context, Buffer, 
                                         Lba, BCount, ReadBytes, CachedOnly);

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        status = _SEH2_GetExceptionCode();
    } _SEH2_END;

    return status;
}

//**************************************************************************************
//*		WCacheWriteBlocks__ - Write blocks using Atlantis cache
//*************************************************************************************
NTSTATUS
WCacheWriteBlocks__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN PCHAR Buffer,
    IN lba_t Lba,
    IN ULONG BCount,
    OUT PSIZE_T WrittenBytes,
    IN BOOLEAN CachedOnly)
{
    NTSTATUS status = STATUS_SUCCESS;

    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY {
        Atlantis::AtlantisWCache* atlantisCache = 
            (Atlantis::AtlantisWCache*)Cache->AtlantisCache;

        status = atlantisCache->writeBlocks(IrpContext, Context, Buffer, 
                                          Lba, BCount, WrittenBytes, CachedOnly);

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        status = _SEH2_GetExceptionCode();
    } _SEH2_END;

    return status;
}

//**************************************************************************************
//*		WCacheFlushBlocks__ - Flush specific blocks
//*************************************************************************************
NTSTATUS 
WCacheFlushBlocks__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN lba_t Lba,
    IN ULONG BCount)
{
    // Atlantis cache is always consistent, so flush is a no-op
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

//**************************************************************************************
//*		WCacheDiscardBlocks__ - Discard blocks from cache
//*************************************************************************************
VOID
WCacheDiscardBlocks__(
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN lba_t Lba,
    IN ULONG BCount)
{
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return;
    }

    // For now, Atlantis doesn't support selective discard
    // This could be implemented by extending the BigFile interface
}

//**************************************************************************************
//*		WCacheFlushAll__ - Flush all cached data
//*************************************************************************************
VOID
WCacheFlushAll__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context)
{
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return;
    }

    _SEH2_TRY {
        Atlantis::AtlantisWCache* atlantisCache = 
            (Atlantis::AtlantisWCache*)Cache->AtlantisCache;

        atlantisCache->flushAll(IrpContext, Context);

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        // Ignore exceptions in flush
    } _SEH2_END;
}

//**************************************************************************************
//*		WCachePurgeAll__ - Purge all cached data
//*************************************************************************************
VOID
WCachePurgeAll__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context)
{
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return;
    }

    _SEH2_TRY {
        Atlantis::AtlantisWCache* atlantisCache = 
            (Atlantis::AtlantisWCache*)Cache->AtlantisCache;

        atlantisCache->purgeAll(IrpContext, Context);

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        // Ignore exceptions in purge
    } _SEH2_END;
}

//**************************************************************************************
//*		WCacheRelease__ - Release cache resources
//*************************************************************************************
VOID
WCacheRelease__(IN PW_CACHE Cache)
{
    if (!Cache || Cache->Tag != 0x41544C41) {
        return;
    }

    _SEH2_TRY {
        if (Cache->AtlantisCache) {
            Atlantis::AtlantisWCache* atlantisCache = 
                (Atlantis::AtlantisWCache*)Cache->AtlantisCache;

            atlantisCache->release();
            delete atlantisCache;
            Cache->AtlantisCache = NULL;
        }

        ExDeleteResourceLite(&Cache->WCacheLock);
        Cache->Tag = 0;

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        // Ignore exceptions during cleanup
    } _SEH2_END;
}

//**************************************************************************************
//*		WCacheIsInitialized__ - Check if cache is initialized
//*************************************************************************************
BOOLEAN
WCacheIsInitialized__(IN PW_CACHE Cache)
{
    if (!Cache || Cache->Tag != 0x41544C41 || !Cache->AtlantisCache) {
        return FALSE;
    }

    _SEH2_TRY {
        Atlantis::AtlantisWCache* atlantisCache = 
            (Atlantis::AtlantisWCache*)Cache->AtlantisCache;

        return atlantisCache->isInitialized() ? TRUE : FALSE;

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    } _SEH2_END;
}

//**************************************************************************************
//*		WCacheDirect__ - Direct access to cached data
//*************************************************************************************
NTSTATUS
WCacheDirect__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN lba_t Lba,
    IN BOOLEAN Modified,
    OUT PCHAR* CachedBlock,
    IN BOOLEAN CachedOnly)
{
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY {
        Atlantis::AtlantisWCache* atlantisCache = 
            (Atlantis::AtlantisWCache*)Cache->AtlantisCache;

        return atlantisCache->directAccess(IrpContext, Context, Lba, 
                                         Modified, CachedBlock, CachedOnly);

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        return _SEH2_GetExceptionCode();
    } _SEH2_END;
}

//**************************************************************************************
//*		WCacheEODirect__ - End direct access
//*************************************************************************************
NTSTATUS 
WCacheEODirect__(
    IN PW_CACHE Cache,
    IN PVOID Context)
{
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return STATUS_INVALID_PARAMETER;
    }

    // Direct access mode not fully implemented in Atlantis yet
    return STATUS_SUCCESS;
}

//**************************************************************************************
//*		WCacheStartDirect__ - Start direct access
//*************************************************************************************
NTSTATUS 
WCacheStartDirect__(
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN BOOLEAN Exclusive)
{
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return STATUS_INVALID_PARAMETER;
    }

    // Direct access mode not fully implemented in Atlantis yet
    return STATUS_SUCCESS;
}

//**************************************************************************************
//*		WCacheIsCached__ - Check if blocks are cached
//*************************************************************************************
BOOLEAN
WCacheIsCached__(
    IN PW_CACHE Cache,
    IN lba_t Lba,
    IN ULONG BCount)
{
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return FALSE;
    }

    _SEH2_TRY {
        Atlantis::AtlantisWCache* atlantisCache = 
            (Atlantis::AtlantisWCache*)Cache->AtlantisCache;

        return atlantisCache->isCached(Lba, BCount) ? TRUE : FALSE;

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    } _SEH2_END;
}

//**************************************************************************************
//*		WCacheSetMode__ - Set cache mode
//*************************************************************************************
NTSTATUS 
WCacheSetMode__(
    IN PW_CACHE Cache,
    IN ULONG Mode)
{
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return STATUS_INVALID_PARAMETER;
    }

    _SEH2_TRY {
        Atlantis::AtlantisWCache* atlantisCache = 
            (Atlantis::AtlantisWCache*)Cache->AtlantisCache;

        return atlantisCache->setMode(Mode);

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        return _SEH2_GetExceptionCode();
    } _SEH2_END;
}

//**************************************************************************************
//*		WCacheGetMode__ - Get cache mode
//*************************************************************************************
ULONG
WCacheGetMode__(IN PW_CACHE Cache)
{
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return WCACHE_MODE_ROM;
    }

    _SEH2_TRY {
        Atlantis::AtlantisWCache* atlantisCache = 
            (Atlantis::AtlantisWCache*)Cache->AtlantisCache;

        return atlantisCache->getMode();

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        return WCACHE_MODE_ROM;
    } _SEH2_END;
}

//**************************************************************************************
//*		WCacheGetWriteBlockCount__ - Get write block count
//*************************************************************************************
ULONG
WCacheGetWriteBlockCount__(IN PW_CACHE Cache)
{
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return 0;
    }

    _SEH2_TRY {
        Atlantis::AtlantisWCache* atlantisCache = 
            (Atlantis::AtlantisWCache*)Cache->AtlantisCache;

        return atlantisCache->getWriteBlockCount();

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    } _SEH2_END;
}

//**************************************************************************************
//*		WCacheSyncReloc__ - Sync relocation
//*************************************************************************************
VOID
WCacheSyncReloc__(
    IN PW_CACHE Cache,
    IN PVOID Context)
{
    // Not implemented in Atlantis - original functionality not clear
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return;
    }
}

//**************************************************************************************
//*		WCacheChFlags__ - Change cache flags
//*************************************************************************************
ULONG
WCacheChFlags__(
    IN PW_CACHE Cache,
    IN ULONG SetFlags,
    IN ULONG ClrFlags)
{
    if (!Cache || !Cache->AtlantisCache || Cache->Tag != 0x41544C41) {
        return 0;
    }

    _SEH2_TRY {
        Atlantis::AtlantisWCache* atlantisCache = 
            (Atlantis::AtlantisWCache*)Cache->AtlantisCache;

        return atlantisCache->changeFlags(SetFlags, ClrFlags);

    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    } _SEH2_END;
}

//**************************************************************************************
//*		WCacheCompleteAsync__ - Complete async request
//*************************************************************************************
NTSTATUS 
WCacheCompleteAsync__(
    IN PVOID WContext,
    IN NTSTATUS Status)
{
    // Async operations not implemented in current Atlantis version
    return STATUS_SUCCESS;
}

} // extern "C"