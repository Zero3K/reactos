////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

#ifndef __UDF_CACHE_COMPAT_H__
#define __UDF_CACHE_COMPAT_H__

//
// Cache Compatibility Layer
// This header provides a unified interface for both WCache and WinDiskCache
// implementations. When UDF_USE_WDISK_CACHE is defined, WCache function
// calls are redirected to their WinDiskCache equivalents.
//

#ifdef UDF_USE_WDISK_CACHE

// Include WinDiskCache header
#include "wdisk_cache_lib.h"

// Type aliases for compatibility
typedef PWDISK_CACHE           PW_CACHE;
typedef WDISK_CACHE            W_CACHE;
typedef PWDISK_ERROR_HANDLER   PWC_ERROR_HANDLER;
typedef WDISK_ERROR_CONTEXT    WC_ERROR_CONTEXT;
typedef PWDISK_ERROR_CONTEXT   PWC_ERROR_CONTEXT;
typedef PWDISK_ERROR_CONTEXT   PWCACHE_ERROR_CONTEXT;

// WCache mode constants (original)
#define WCACHE_MODE_ROM      0x00000000
#define WCACHE_MODE_RW       0x00000001
#define WCACHE_MODE_R        0x00000002
#define WCACHE_MODE_RAM      0x00000003
#define WCACHE_MODE_EWR      0x00000004
#define WCACHE_MODE_MAX      WCACHE_MODE_RAM

// Mode conversion functions
static inline ULONG WCacheToWDiskMode(ULONG wcacheMode) {
    switch (wcacheMode) {
        case WCACHE_MODE_ROM: return WDISK_MODE_ROM;
        case WCACHE_MODE_RW:  return WDISK_MODE_RW;
        case WCACHE_MODE_R:   return WDISK_MODE_R;
        case WCACHE_MODE_RAM: return WDISK_MODE_RW; // Map RAM mode to RW as closest equivalent
        case WCACHE_MODE_EWR: return WDISK_MODE_RW; // Map EWR mode to RW as closest equivalent
        default:              return WDISK_MODE_RW; // default fallback
    }
}

// Wrapper functions for mode-dependent operations
static inline NTSTATUS WCacheInit__(IN PW_CACHE Cache,
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
                                    IN PWC_ERROR_HANDLER ErrorHandlerProc) {
    return WDiskCacheInit__(Cache, MaxFrames, MaxBlocks, MaxBytesToRead,
                           PacketSizeSh, BlockSizeSh, BlocksPerFrameSh,
                           FirstLba, LastLba, WCacheToWDiskMode(Mode), Flags,
                           FramesToKeepFree, WriteProc, ReadProc, WriteProcAsync,
                           ReadProcAsync, CheckUsedProc, UpdateRelocProc, ErrorHandlerProc);
}

static inline VOID WCacheSetMode__(IN PW_CACHE Cache, IN ULONG Mode) {
    WDiskCacheSetMode__(Cache, WCacheToWDiskMode(Mode));
}

// Direct macro redirections for functions that don't need mode conversion
#define WCacheReadBlocks__           WDiskCacheReadBlocks__
#define WCacheWriteBlocks__          WDiskCacheWriteBlocks__
#define WCacheFlushBlocks__          WDiskCacheFlushBlocks__
#define WCacheDiscardBlocks__        WDiskCacheDiscardBlocks__
#define WCacheFlushAll__             WDiskCacheFlushAll__
#define WCachePurgeAll__             WDiskCachePurgeAll__
#define WCacheRelease__              WDiskCacheRelease__
#define WCacheIsInitialized__        WDiskCacheIsInitialized__
#define WCacheIsCached__             WDiskCacheIsCached__
#define WCacheDirect__               WDiskCacheDirect__
#define WCacheStartDirect__          WDiskCacheStartDirect__
#define WCacheEODirect__             WDiskCacheEODirect__
#define WCacheGetWriteBlockCount__   WDiskCacheGetWriteBlockCount__
#define WCacheSyncReloc__            WDiskCacheSyncReloc__
#define WCacheChFlags__              WDiskCacheChFlags__

// Redirect flag constants  
#define WCACHE_CACHE_WHOLE_PACKET    WDISK_CACHE_WHOLE_PACKET
#define WCACHE_DO_NOT_COMPARE        WDISK_DO_NOT_COMPARE
#define WCACHE_NO_WRITE_THROUGH      WDISK_NO_WRITE_THROUGH

// Additional WCACHE flag constants (not supported by WinDiskCache, mapped to 0)
#define WCACHE_CHAINED_IO            0x00000000  // Not supported in WinDiskCache
#define WCACHE_MARK_BAD_BLOCKS       0x00000000  // Not supported in WinDiskCache  
#define WCACHE_RO_BAD_BLOCKS         0x00000000  // Not supported in WinDiskCache

// WCACHE_VALID_FLAGS redefinition for WinDiskCache compatibility
#define WCACHE_VALID_FLAGS          (WCACHE_CACHE_WHOLE_PACKET | \
                                     WCACHE_DO_NOT_COMPARE | \
                                     WCACHE_CHAINED_IO | \
                                     WCACHE_MARK_BAD_BLOCKS | \
                                     WCACHE_RO_BAD_BLOCKS | \
                                     WCACHE_NO_WRITE_THROUGH)

// Redirect error constants
#define WC_ERROR_READ                WDISK_ERROR_READ
#define WC_ERROR_WRITE               WDISK_ERROR_WRITE
#define WC_ERROR_INTERNAL            WDISK_ERROR_INTERNAL

// Block status constants (for PCHECK_BLOCK return values)
#define WCACHE_BLOCK_USED            0x01
#define WCACHE_BLOCK_ZERO            0x02
#define WCACHE_BLOCK_BAD             0x04

#elif defined(UDF_USE_WCACHE)

// Include WCache header
#include "wcache_lib.h"

#else

// Neither cache implementation is enabled
#error "Either UDF_USE_WCACHE or UDF_USE_WDISK_CACHE must be defined"

#endif

#endif // __UDF_CACHE_COMPAT_H__