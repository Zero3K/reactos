////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

#ifndef __UDFS_CACHE_H__
#define __UDFS_CACHE_H__

#include "udffs.h"

extern "C" {

// Simple cache entry structure
typedef struct _UDFS_CACHE_ENTRY {
    lba_t Lba;              // Logical block address
    PCHAR Buffer;           // Cached data buffer
    ULONG Flags;            // Entry flags (modified, valid, etc)
    LARGE_INTEGER LastAccess; // Last access time for LRU
    struct _UDFS_CACHE_ENTRY* Next; // Hash chain
} UDFS_CACHE_ENTRY, *PUDFS_CACHE_ENTRY;

// Cache entry flags
#define UDFS_CACHE_VALID     0x00000001
#define UDFS_CACHE_MODIFIED  0x00000002
#define UDFS_CACHE_BAD       0x00000004

// Simple cache structure  
typedef struct _UDFS_CACHE {
    ULONG Tag;              // Cache signature
    PUDFS_CACHE_ENTRY* HashTable; // Hash table for entries
    ULONG HashSize;         // Size of hash table
    ULONG MaxEntries;       // Maximum number of entries
    ULONG CurrentEntries;   // Current number of entries
    ULONG BlockSize;        // Size of each cached block
    ULONG BlockSizeSh;      // Block size shift
    lba_t FirstLba;         // First valid LBA
    lba_t LastLba;          // Last valid LBA
    ULONG Mode;             // Cache mode (RO/RW)
    
    // Statistics
    ULONG HitCount;         // Cache hit count
    ULONG MissCount;        // Cache miss count
    ULONG WriteCount;       // Number of modified blocks
    
    // Callbacks - simplified interface
    PWRITE_BLOCK WriteProc;
    PREAD_BLOCK ReadProc;
    PWC_ERROR_HANDLER ErrorHandlerProc;
    
    // Synchronization
    ERESOURCE CacheLock;
    BOOLEAN Initialized;
    
    // Memory management
    PCHAR BufferPool;       // Pre-allocated buffer pool
    PUDFS_CACHE_ENTRY EntryPool; // Pre-allocated entry pool
    
} UDFS_CACHE, *PUDFS_CACHE;

// Cache modes (simplified from WCache)
#define UDFS_CACHE_MODE_RO   0x00000000  // Read only
#define UDFS_CACHE_MODE_RW   0x00000001  // Read/Write

// Initialize cache
NTSTATUS 
UdfsCacheInit(
    IN PUDFS_CACHE Cache,
    IN ULONG MaxEntries,
    IN ULONG BlockSize,
    IN lba_t FirstLba,
    IN lba_t LastLba,
    IN ULONG Mode,
    IN PWRITE_BLOCK WriteProc,
    IN PREAD_BLOCK ReadProc,
    IN PWC_ERROR_HANDLER ErrorHandlerProc
    );

// Read blocks (simplified interface)
NTSTATUS
UdfsCacheReadBlocks(
    IN PIRP_CONTEXT IrpContext,
    IN PUDFS_CACHE Cache,
    IN PVOID Context,
    IN PCHAR Buffer,
    IN lba_t Lba,
    IN ULONG BCount,
    OUT PSIZE_T ReadBytes,
    IN BOOLEAN CachedOnly
    );

// Write blocks (simplified interface)  
NTSTATUS
UdfsCacheWriteBlocks(
    IN PIRP_CONTEXT IrpContext,
    IN PUDFS_CACHE Cache,
    IN PVOID Context,
    IN PCHAR Buffer,
    IN lba_t Lba,
    IN ULONG BCount,
    OUT PSIZE_T WrittenBytes,
    IN BOOLEAN CachedOnly
    );

// Flush blocks
NTSTATUS 
UdfsCacheFlushBlocks(
    IN PIRP_CONTEXT IrpContext,
    IN PUDFS_CACHE Cache,
    IN PVOID Context,
    IN lba_t Lba,
    IN ULONG BCount
    );

// Discard blocks
VOID
UdfsCacheDiscardBlocks(
    IN PUDFS_CACHE Cache,
    IN lba_t Lba,
    IN ULONG BCount
    );

// Flush all
VOID
UdfsCacheFlushAll(
    IN PIRP_CONTEXT IrpContext,
    IN PUDFS_CACHE Cache,
    IN PVOID Context
    );

// Purge all
VOID
UdfsCachePurgeAll(
    IN PUDFS_CACHE Cache
    );

// Release cache
VOID
UdfsCacheRelease(
    IN PUDFS_CACHE Cache
    );

// Check if initialized
BOOLEAN
UdfsCacheIsInitialized(
    IN PUDFS_CACHE Cache
    );

} // extern "C"

#endif // __UDFS_CACHE_H__