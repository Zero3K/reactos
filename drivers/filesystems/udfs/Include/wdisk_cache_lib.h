////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

#ifndef __UDF_WDISK_CACHE_LIB_H__
#define __UDF_WDISK_CACHE_LIB_H__

extern "C" {

#include "platform.h"

// Forward declarations
struct IRP_CONTEXT;
typedef struct IRP_CONTEXT *PIRP_CONTEXT;

// Function pointer types (shared with wcache_lib.h)
typedef NTSTATUS     (*PWRITE_BLOCK) (IN PIRP_CONTEXT IrpContext,
                                      IN PVOID Context,
                                      IN PVOID Buffer,     // Target buffer
                                      IN SIZE_T Length,
                                      IN lba_t Lba,
                                      OUT PSIZE_T WrittenBytes,
                                      IN uint32 Flags);

typedef NTSTATUS     (*PREAD_BLOCK) (IN PIRP_CONTEXT IrpContext,
                                     IN PVOID Context,
                                     IN PVOID Buffer,     // Target buffer
                                     IN SIZE_T Length,
                                     IN lba_t Lba,
                                     OUT PSIZE_T ReadBytes,
                                     IN uint32 Flags);

typedef NTSTATUS     (*PWRITE_BLOCK_ASYNC) (IN PVOID Context,
                                            IN PVOID WContext,
                                            IN PVOID Buffer,     // Target buffer
                                            IN SIZE_T Length,
                                            IN lba_t Lba,
                                            OUT PSIZE_T WrittenBytes,
                                            IN BOOLEAN FreeBuffer);

typedef NTSTATUS     (*PREAD_BLOCK_ASYNC) (IN PVOID Context,
                                           IN PVOID WContext,
                                           IN PVOID Buffer,     // Source buffer
                                           IN SIZE_T Length,
                                           IN lba_t Lba,
                                           OUT PSIZE_T ReadBytes);

typedef ULONG        (*PCHECK_BLOCK) (IN PVOID Context,
                                      IN lba_t Lba);

typedef NTSTATUS     (*PUPDATE_RELOC) (IN PVOID Context,
                                       IN lba_t Lba,
                                       IN PULONG RelocTab,
                                       IN ULONG BCount);

// Error handling types
#define WDISK_ERROR_READ     0x0001
#define WDISK_ERROR_WRITE    0x0002
#define WDISK_ERROR_INTERNAL 0x0003

typedef struct _WDISK_ERROR_CONTEXT {
    ULONG WCErrorCode;
    NTSTATUS Status;
    BOOLEAN  Retry;
    UCHAR    Padding[3];
    union {
        struct {
            ULONG    Lba;
            ULONG    BCount;
            PVOID    Buffer;
        } ReadWrite;
        struct {
            ULONG    p1;
            ULONG    p2;
            ULONG    p3;
            ULONG    p4;
        } Internal;
    };
} WDISK_ERROR_CONTEXT, *PWDISK_ERROR_CONTEXT;

typedef NTSTATUS     (*PWC_ERROR_HANDLER) (IN PVOID Context,
                                           IN PWDISK_ERROR_CONTEXT ErrorInfo);

// WinDiskCache structure - lightweight cache implementation
// This is designed to be a simplified interface for WinDiskCache from
// https://github.com/ogir-ok/WinDiskCache
typedef struct _WDISK_CACHE {
    // Cache state
    ULONG               MaxFrames;
    ULONG               MaxBlocks;
    ULONG               BlockSize;
    ULONG               BlockSizeSh;
    lba_t               FirstLba;
    lba_t               LastLba;
    ULONG               Mode;
    ULONG               Flags;
    BOOLEAN             Initialized;
    
    // Function pointers for I/O operations
    PWRITE_BLOCK        WriteProc;
    PREAD_BLOCK         ReadProc;
    PCHECK_BLOCK        CheckUsedProc;
    PUPDATE_RELOC       UpdateRelocProc;
    PWC_ERROR_HANDLER   ErrorHandlerProc;
    
    // WinDiskCache specific context (placeholder for external implementation)
    PVOID               WDiskContext;
    
} WDISK_CACHE, *PWDISK_CACHE;

// WinDiskCache mode constants - similar to WCache modes
#define WDISK_MODE_ROM       0x00000001
#define WDISK_MODE_RW        0x00000002
#define WDISK_MODE_R         0x00000004

// WinDiskCache flag constants - simplified version of WCache flags
#define WDISK_CACHE_WHOLE_PACKET    0x00000001
#define WDISK_DO_NOT_COMPARE        0x00000002
#define WDISK_NO_WRITE_THROUGH      0x00000004

#define WDISK_VALID_FLAGS          (WDISK_CACHE_WHOLE_PACKET | \
                                    WDISK_DO_NOT_COMPARE | \
                                    WDISK_NO_WRITE_THROUGH)

// Function declarations for WinDiskCache interface
// These functions provide a simplified interface matching WCache functionality

// Initialize WinDiskCache
NTSTATUS WDiskCacheInit__(IN PWDISK_CACHE Cache,
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
                          IN PWC_ERROR_HANDLER ErrorHandlerProc);

// Write blocks through WinDiskCache
NTSTATUS WDiskCacheWriteBlocks__(IN PIRP_CONTEXT IrpContext,
                                 IN PWDISK_CACHE Cache,
                                 IN PVOID Context,
                                 IN PCHAR Buffer,
                                 IN lba_t Lba,
                                 IN ULONG BCount,
                                 OUT PSIZE_T WrittenBytes,
                                 IN BOOLEAN CachedOnly);

// Read blocks through WinDiskCache
NTSTATUS WDiskCacheReadBlocks__(IN PIRP_CONTEXT IrpContext,
                                IN PWDISK_CACHE Cache,
                                IN PVOID Context,
                                IN PCHAR Buffer,
                                IN lba_t Lba,
                                IN ULONG BCount,
                                OUT PSIZE_T ReadBytes,
                                IN BOOLEAN CachedOnly);

// Flush blocks in WinDiskCache
NTSTATUS WDiskCacheFlushBlocks__(IN PIRP_CONTEXT IrpContext,
                                 IN PWDISK_CACHE Cache,
                                 IN PVOID Context,
                                 IN lba_t Lba,
                                 IN ULONG BCount);

// Discard blocks from WinDiskCache
VOID WDiskCacheDiscardBlocks__(IN PWDISK_CACHE Cache,
                               IN PVOID Context,
                               IN lba_t Lba,
                               IN ULONG BCount);

// Flush entire WinDiskCache
VOID WDiskCacheFlushAll__(IN PIRP_CONTEXT IrpContext,
                          IN PWDISK_CACHE Cache,
                          IN PVOID Context);

// Purge entire WinDiskCache
VOID WDiskCachePurgeAll__(IN PIRP_CONTEXT IrpContext,
                          IN PWDISK_CACHE Cache,
                          IN PVOID Context);

// Release WinDiskCache resources
VOID WDiskCacheRelease__(IN PWDISK_CACHE Cache);

// Check if WinDiskCache is initialized
BOOLEAN WDiskCacheIsInitialized__(IN PWDISK_CACHE Cache);

// Set WinDiskCache mode
VOID WDiskCacheSetMode__(IN PWDISK_CACHE Cache, IN ULONG Mode);

// Check if blocks are cached
BOOLEAN WDiskCacheIsCached__(IN PWDISK_CACHE Cache, IN lba_t Lba, IN ULONG BCount);

// Direct access to cached data
NTSTATUS WDiskCacheDirect__(IN PIRP_CONTEXT IrpContext,
                            IN PWDISK_CACHE Cache,
                            IN PVOID Context,
                            IN lba_t Lba,
                            IN BOOLEAN Modified,
                            OUT PCHAR* CachedBlock,
                            IN BOOLEAN CachedOnly);

// Start direct access mode
VOID WDiskCacheStartDirect__(IN PWDISK_CACHE Cache, IN PVOID Context, IN BOOLEAN ForWrite);

// End direct access mode
VOID WDiskCacheEODirect__(IN PWDISK_CACHE Cache, IN PVOID Context);

// Get write block count
ULONG WDiskCacheGetWriteBlockCount__(IN PWDISK_CACHE Cache);

// Synchronize relocation
VOID WDiskCacheSyncReloc__(IN PWDISK_CACHE Cache, IN PVOID Context);

// Change cache flags
VOID WDiskCacheChFlags__(IN PWDISK_CACHE Cache, IN ULONG SetFlags, IN ULONG ClrFlags);

} // extern "C"

#endif // __UDF_WDISK_CACHE_LIB_H__