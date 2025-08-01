////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

#ifndef __CDRW_WCACHE_LIB_H__
#define __CDRW_WCACHE_LIB_H__

extern "C" {

#include "platform.h"

#define WCACHE_BOUND_CHECKS

struct IRP_CONTEXT;
typedef struct IRP_CONTEXT *PIRP_CONTEXT;

typedef NTSTATUS (*PWRITE_BLOCK) (IN PIRP_CONTEXT IrpContext, IN PVOID Context, IN PVOID Buffer, 
    IN SIZE_T Length, IN lba_t Lba, OUT PSIZE_T WrittenBytes, IN uint32 Flags);

typedef NTSTATUS (*PREAD_BLOCK) (IN PIRP_CONTEXT IrpContext, IN PVOID Context, IN PVOID Buffer, 
    IN SIZE_T Length, IN lba_t Lba, OUT PSIZE_T ReadBytes, IN uint32 Flags);

typedef NTSTATUS (*PWRITE_BLOCK_ASYNC) (IN PVOID Context, IN PVOID WContext, IN PVOID Buffer, 
    IN SIZE_T Length, IN lba_t Lba, OUT PSIZE_T WrittenBytes, IN BOOLEAN FreeBuffer);

typedef NTSTATUS (*PREAD_BLOCK_ASYNC) (IN PVOID Context, IN PVOID WContext, IN PVOID Buffer, 
    IN SIZE_T Length, IN lba_t Lba, OUT PSIZE_T ReadBytes);

#define WCACHE_BLOCK_USED    0x01
#define WCACHE_BLOCK_ZERO    0x02
#define WCACHE_BLOCK_BAD     0x04

typedef ULONG (*PCHECK_BLOCK) (IN PVOID Context, IN lba_t Lba);

typedef NTSTATUS (*PUPDATE_RELOC) (IN PVOID Context, IN lba_t Lba, IN PULONG RelocTab, IN ULONG BCount);

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

typedef NTSTATUS (*PWC_ERROR_HANDLER) (IN PVOID Context, IN PWCACHE_ERROR_CONTEXT ErrorInfo);

typedef struct _W_CACHE_ENTRY {
    union {
        PCHAR Sector;
        UCHAR Flags:3;
    };
} W_CACHE_ENTRY, *PW_CACHE_ENTRY;

typedef struct _W_CACHE_FRAME {
    PW_CACHE_ENTRY Frame;
    ULONG BlockCount;
    ULONG UpdateCount;
    ULONG AccessCount;
} W_CACHE_FRAME, *PW_CACHE_FRAME;

#define CACHED_BLOCK_MEMORY_TYPE PagedPool
#define MAX_TRIES_FOR_NA         3

#ifdef _WIN64
    #define WCACHE_ADDR_MASK     0xfffffffffffffff8
#else
    #define WCACHE_ADDR_MASK     0xfffffff8
#endif
#define WCACHE_FLAG_MASK     0x00000007
#define WCACHE_FLAG_MODIFIED 0x00000001
#define WCACHE_FLAG_ZERO     0x00000002
#define WCACHE_FLAG_BAD      0x00000004

#define WCACHE_MODE_ROM      0x00000000
#define WCACHE_MODE_RW       0x00000001
#define WCACHE_MODE_R        0x00000002
#define WCACHE_MODE_RAM      0x00000003
#define WCACHE_MODE_EWR      0x00000004
#define WCACHE_MODE_MAX      WCACHE_MODE_RAM

#define PH_TMP_BUFFER          1

struct _W_CACHE_ASYNC;

typedef struct _W_CACHE {
    ULONG Tag;
    PW_CACHE_FRAME FrameList;
    lba_t* CachedBlocksList;
    lba_t* CachedFramesList;
    lba_t* CachedModifiedBlocksList;
    ULONG BlocksPerFrame;
    ULONG BlocksPerFrameSh;
    ULONG BlockCount;
    ULONG MaxBlocks;
    ULONG MaxBytesToRead;
    ULONG FrameCount;
    ULONG MaxFrames;
    ULONG PacketSize;
    ULONG PacketSizeSh;
    ULONG BlockSize;
    ULONG BlockSizeSh;
    lba_t FirstLba;
    lba_t LastLba;
    ULONG WriteCount;
    ULONG FramesToKeepFree;
    ULONG Mode;
    ULONG Flags;
    ULONG UseCount;
    BOOLEAN DoNotCompare;
    BOOLEAN Chained;
    BOOLEAN RememberBB;
    BOOLEAN NoWriteBB;
    PWRITE_BLOCK WriteProc;
    PREAD_BLOCK ReadProc;
    PWRITE_BLOCK_ASYNC WriteProcAsync;
    PREAD_BLOCK_ASYNC ReadProcAsync;
    PCHECK_BLOCK CheckUsedProc;
    PUPDATE_RELOC UpdateRelocProc;
    PWC_ERROR_HANDLER ErrorHandlerProc;
    struct _W_CACHE_ASYNC* AsyncEntryList;
    PCHAR tmp_buff;
    ERESOURCE WCacheLock;
    PFAST_MUTEX FastMutex;
} W_CACHE, *PW_CACHE;

#define WCACHE_INVALID_LBA      0xFFFFFFFF

// Cache flags
#define WCACHE_CACHE_WHOLE_PACKET    0x00000001
#define WCACHE_DO_NOT_COMPARE        0x00000002
#define WCACHE_CHAINED_IO           0x00000004
#define WCACHE_MARK_BAD_BLOCKS      0x00000008
#define WCACHE_RO_BAD_BLOCKS        0x00000010

// Function declarations
NTSTATUS WCacheInit__(IN PW_CACHE Cache, IN ULONG MaxFrames, IN ULONG MaxBlocks, IN SIZE_T MaxBytesToRead,
    IN ULONG PacketSizeSh, IN ULONG BlockSizeSh, IN ULONG BlocksPerFrameSh, IN lba_t FirstLba, IN lba_t LastLba,
    IN ULONG Mode, IN ULONG Flags, IN ULONG FramesToKeepFree, IN PWRITE_BLOCK WriteProc, IN PREAD_BLOCK ReadProc,
    IN PWRITE_BLOCK_ASYNC WriteProcAsync, IN PREAD_BLOCK_ASYNC ReadProcAsync, IN PCHECK_BLOCK CheckUsedProc,
    IN PUPDATE_RELOC UpdateRelocProc, IN PWC_ERROR_HANDLER ErrorHandlerProc);

NTSTATUS WCacheRelease__(IN PW_CACHE Cache);
BOOLEAN WCacheIsInitialized__(IN PW_CACHE Cache);
NTSTATUS WCacheSetMode__(IN PW_CACHE Cache, IN ULONG Mode);
ULONG WCacheGetMode__(IN PW_CACHE Cache);
ULONG WCacheGetWriteBlockCount__(IN PW_CACHE Cache);

NTSTATUS WCacheReadBlocks__(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, 
    IN PCHAR Buffer, IN lba_t Lba, IN ULONG BCount, OUT PSIZE_T ReadBytes, IN BOOLEAN CachedOnly);

NTSTATUS WCacheWriteBlocks__(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context,
    IN PCHAR Buffer, IN lba_t Lba, IN ULONG BCount, OUT PSIZE_T WrittenBytes, IN BOOLEAN CachedOnly);

NTSTATUS WCacheFlushAll__(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context);
NTSTATUS WCacheFlushBlocks__(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, 
    IN lba_t Lba, IN ULONG BCount);

VOID WCachePurgeAll__(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context);

NTSTATUS WCacheDirect__(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, 
    IN lba_t Lba, IN BOOLEAN Modified, OUT PCHAR* CachedBlock, IN BOOLEAN CachedOnly);

NTSTATUS WCacheEODirect__(IN PW_CACHE Cache, IN PVOID Context);
NTSTATUS WCacheStartDirect__(IN PW_CACHE Cache, IN PVOID Context, IN BOOLEAN ForWrite);

BOOLEAN WCacheIsCached__(IN PW_CACHE Cache, IN lba_t Lba, IN ULONG BCount);

VOID WCacheSyncReloc__(IN PW_CACHE Cache, IN PVOID Context);
NTSTATUS WCacheDiscardBlocks__(IN PW_CACHE Cache, IN PVOID Context, IN lba_t Lba, IN ULONG BCount);
NTSTATUS WCacheCompleteAsync__(IN PW_CACHE Cache, IN PVOID Context, IN struct _W_CACHE_ASYNC* WContext);
NTSTATUS WCacheChFlags__(IN PW_CACHE Cache, IN ULONG SetFlags, IN ULONG ClrFlags);

} // extern "C"

#endif // __CDRW_WCACHE_LIB_H__