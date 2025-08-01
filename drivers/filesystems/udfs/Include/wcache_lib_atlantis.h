////////////////////////////////////////////////////////////////////
// Atlantis Cache System - WCache Library Header - Atlantis Backend
// Copyright (C) Rogerio Regis, adapted for ReactOS
// This replaces the original wcache_lib.h with Atlantis implementation
////////////////////////////////////////////////////////////////////

#ifndef __ATLANTIS_WCACHE_LIB_H__
#define __ATLANTIS_WCACHE_LIB_H__

extern "C" {

#include "platform.h"

#define WCACHE_BOUND_CHECKS

struct IRP_CONTEXT;
typedef struct IRP_CONTEXT *PIRP_CONTEXT;

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

#define WCACHE_BLOCK_USED    0x01
#define WCACHE_BLOCK_ZERO    0x02
#define WCACHE_BLOCK_BAD     0x04

typedef ULONG        (*PCHECK_BLOCK) (IN PVOID Context,
                                      IN lba_t Lba);

typedef NTSTATUS     (*PUPDATE_RELOC) (IN PVOID Context,
                                       IN lba_t Lba,
                                       IN PULONG RelocTab,
                                       IN ULONG BCount);

#define WCACHE_ERROR_READ     0x0001
#define WCACHE_ERROR_WRITE    0x0002
#define WCACHE_ERROR_INTERNAL 0x0003

#define WCACHE_W_OP     FALSE
#define WCACHE_R_OP     TRUE

typedef struct _WCACHE_ERROR_CONTEXT {
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
} WCACHE_ERROR_CONTEXT, *PWCACHE_ERROR_CONTEXT;

typedef NTSTATUS     (*PWC_ERROR_HANDLER) (IN PVOID Context,
                                           IN PWCACHE_ERROR_CONTEXT ErrorInfo);

#define WCACHE_MODE_ROM      0x00000000  // read only (CD-ROM)
#define WCACHE_MODE_RW       0x00000001  // rewritable (CD-RW)
#define WCACHE_MODE_R        0x00000002  // WORM (CD-R)
#define WCACHE_MODE_RAM      0x00000003  // random writable device (HDD)
#define WCACHE_MODE_EWR      0x00000004  // ERASE-cycle required (MO)
#define WCACHE_MODE_MAX      WCACHE_MODE_RAM

#define WCACHE_CACHE_WHOLE_PACKET   0x01
#define WCACHE_DO_NOT_COMPARE       0x02
#define WCACHE_CHAINED_IO           0x04
#define WCACHE_MARK_BAD_BLOCKS      0x08
#define WCACHE_RO_BAD_BLOCKS        0x10
#define WCACHE_NO_WRITE_THROUGH     0x20

#define WCACHE_VALID_FLAGS          (WCACHE_CACHE_WHOLE_PACKET | \
                                     WCACHE_DO_NOT_COMPARE | \
                                     WCACHE_CHAINED_IO | \
                                     WCACHE_MARK_BAD_BLOCKS | \
                                     WCACHE_RO_BAD_BLOCKS | \
                                     WCACHE_NO_WRITE_THROUGH)

#define WCACHE_INVALID_FLAGS        (0xffffffff)
#define WCACHE_INVALID_LBA          ((lba_t)(-1))

// W_CACHE structure - contains Atlantis cache implementation
typedef struct _W_CACHE {
    ULONG Tag;
    PVOID AtlantisCache;  // Pointer to Atlantis::AtlantisWCache instance
    ERESOURCE WCacheLock;
} W_CACHE, *PW_CACHE;

// Function declarations with same signatures as original wcache

// Initialize cache
NTSTATUS WCacheInit__(IN PW_CACHE Cache,
                      IN ULONG MaxFrames,
                      IN ULONG MaxBlocks,
                      IN SIZE_T MaxBytesToRead,
                      IN ULONG PacketSizeSh,    // number of blocks in packet (bit shift)
                      IN ULONG BlockSizeSh,     // bit shift
                      IN ULONG BlocksPerFrameSh,// bit shift
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

// Write cached blocks
NTSTATUS
WCacheWriteBlocks__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN PCHAR Buffer,
    IN lba_t Lba,
    IN ULONG BCount,
    OUT PSIZE_T WrittenBytes,
    IN BOOLEAN CachedOnly
    );

// Read cached blocks
NTSTATUS
WCacheReadBlocks__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN PCHAR Buffer,
    IN lba_t Lba,
    IN ULONG BCount,
    OUT PSIZE_T ReadBytes,
    IN BOOLEAN CachedOnly
    );

// Flush blocks
NTSTATUS WCacheFlushBlocks__(IN PIRP_CONTEXT IrpContext,
                             IN PW_CACHE Cache,
                             IN PVOID Context,
                             IN lba_t Lba,
                             IN ULONG BCount);

// Discard blocks
VOID     WCacheDiscardBlocks__(IN PW_CACHE Cache,
                               IN PVOID Context,
                               IN lba_t Lba,
                               IN ULONG BCount);

// Flush whole cache
VOID
WCacheFlushAll__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context
    );

// Purge whole cache
VOID
WCachePurgeAll__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context
    );

// Free structures
VOID     WCacheRelease__(IN PW_CACHE Cache);

// Check if initialized
BOOLEAN  WCacheIsInitialized__(IN PW_CACHE Cache);

// Direct access to cached data
NTSTATUS
WCacheDirect__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN lba_t Lba,
    IN BOOLEAN Modified,
    OUT PCHAR* CachedBlock,
    IN BOOLEAN CachedOnly
    );

// Release resources after direct access
NTSTATUS WCacheEODirect__(IN PW_CACHE Cache,
                          IN PVOID Context);

// Release resources before direct access
NTSTATUS WCacheStartDirect__(IN PW_CACHE Cache,
                             IN PVOID Context,
                             IN BOOLEAN Exclusive);

// Check if requested extent completely cached
BOOLEAN  WCacheIsCached__(IN PW_CACHE Cache,
                          IN lba_t Lba,
                          IN ULONG BCount);

// Change cache media mode
NTSTATUS WCacheSetMode__(IN PW_CACHE Cache,
                         IN ULONG Mode);

// Get cache mode
ULONG    WCacheGetMode__(IN PW_CACHE Cache);

// Get write block count
ULONG    WCacheGetWriteBlockCount__(IN PW_CACHE Cache);

// Sync relocation
VOID     WCacheSyncReloc__(IN PW_CACHE Cache,
                           IN PVOID Context);

// Change flags
ULONG    WCacheChFlags__(IN PW_CACHE Cache,
                         IN ULONG SetFlags,
                         IN ULONG ClrFlags);

}; // extern "C"

// Complete async request (callback)
NTSTATUS WCacheCompleteAsync__(IN PVOID WContext,
                               IN NTSTATUS Status);

#endif // __ATLANTIS_WCACHE_LIB_H__