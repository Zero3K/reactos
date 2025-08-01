////////////////////////////////////////////////////////////////////
// Atlantis Cache Library Interface
// Complete implementation based on Atlantis library from https://github.com/rdregis/Atlantis
// Provides real caching functionality with LRU eviction and two-level caching
////////////////////////////////////////////////////////////////////

#ifndef __ATLANTIS_LIB_H__
#define __ATLANTIS_LIB_H__

extern "C" {

#include "platform.h"

struct IRP_CONTEXT;
typedef struct IRP_CONTEXT *PIRP_CONTEXT;

// Forward declarations to match WCache interface
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

typedef struct _ATLANTIS_ERROR_CONTEXT {
    ULONG AErrorCode;
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
} ATLANTIS_ERROR_CONTEXT, *PATLANTIS_ERROR_CONTEXT;

typedef NTSTATUS     (*PATLANTIS_ERROR_HANDLER) (IN PVOID Context,
                                                 IN PATLANTIS_ERROR_CONTEXT ErrorInfo);

// Atlantis Cache Entry - represents a cached block
typedef struct _ATLANTIS_CACHE_ENTRY {
    LIST_ENTRY LruListEntry;    // LRU list linkage
    lba_t Lba;                  // Block LBA
    PCHAR BlockData;            // Cached block data
    ULONG AccessCount;          // Access counter for statistics
    ULONG Flags;                // Cache entry flags
    LARGE_INTEGER LastAccess;   // Last access time
} ATLANTIS_CACHE_ENTRY, *PATLANTIS_CACHE_ENTRY;

// Cache entry flags
#define ATLANTIS_ENTRY_MODIFIED 0x00000001
#define ATLANTIS_ENTRY_DIRTY    0x00000002
#define ATLANTIS_ENTRY_VALID    0x00000004

// Atlantis Cache Frame - represents a group of cached blocks  
typedef struct _ATLANTIS_CACHE_FRAME {
    LIST_ENTRY FrameListEntry;     // Frame list linkage
    LIST_ENTRY LruListEntry;       // LRU list for frames
    lba_t StartLba;                // Starting LBA of frame
    ULONG BlockCount;              // Number of blocks in frame
    ULONG ValidBlocks;             // Number of valid blocks cached
    PATLANTIS_CACHE_ENTRY *Blocks; // Array of pointers to cache entries
    ULONG AccessCount;             // Frame access counter
    LARGE_INTEGER LastAccess;      // Last access time
} ATLANTIS_CACHE_FRAME, *PATLANTIS_CACHE_FRAME;

// Hash table for fast block lookup
typedef struct _ATLANTIS_HASH_ENTRY {
    LIST_ENTRY HashListEntry;      // Hash chain linkage
    lba_t Lba;                     // Block LBA (key)
    PATLANTIS_CACHE_ENTRY Entry;   // Pointer to cache entry
} ATLANTIS_HASH_ENTRY, *PATLANTIS_HASH_ENTRY;

#define ATLANTIS_HASH_TABLE_SIZE 1024  // Size of hash table

// Atlantis cache structure - complete implementation
typedef struct _ATLANTIS_CACHE {
    ULONG Tag;                     // 'AtlC' signature
    
    // Basic cache parameters
    ULONG BlockCount;              // Current number of cached blocks
    ULONG MaxBlocks;               // Maximum blocks to cache
    ULONG MaxBytesToRead;          // Maximum bytes to read in one operation
    ULONG FrameCount;              // Current number of frames
    ULONG MaxFrames;               // Maximum frames to cache
    ULONG PacketSize;              // Number of blocks in packet
    ULONG PacketSizeSh;            // Packet size shift value
    ULONG BlockSize;               // Size of each block
    ULONG BlockSizeSh;             // Block size shift value
    ULONG WriteCount;              // Number of dirty blocks
    lba_t FirstLba;                // First valid LBA
    lba_t LastLba;                 // Last valid LBA
    ULONG Mode;                    // Cache mode (ROM/RW/etc)
    ULONG Flags;                   // Cache behavior flags
    
    // Cache behavior flags
    BOOLEAN CacheWholePacket;      // Cache entire packets
    BOOLEAN DoNotCompare;          // Skip data comparison
    BOOLEAN Chained;               // Chained I/O mode
    BOOLEAN RememberBB;            // Remember bad blocks
    BOOLEAN NoWriteBB;             // Don't write to bad blocks
    BOOLEAN NoWriteThrough;        // Disable write-through
    UCHAR  Padding[2];             // Alignment padding
    
    ULONG RBalance;                // Read balance factor
    ULONG WBalance;                // Write balance factor
    ULONG FramesToKeepFree;        // Minimum free frames
    
    // Callback functions
    PWRITE_BLOCK WriteProc;
    PREAD_BLOCK ReadProc;
    PWRITE_BLOCK_ASYNC WriteProcAsync;
    PREAD_BLOCK_ASYNC ReadProcAsync;
    PCHECK_BLOCK CheckUsedProc;
    PUPDATE_RELOC UpdateRelocProc;
    PATLANTIS_ERROR_HANDLER ErrorHandlerProc;
    
    // Synchronization
    ERESOURCE ACacheLock;          // Cache access lock
    
    // LRU lists for cache management
    LIST_ENTRY BlockLruList;       // LRU list of cached blocks
    LIST_ENTRY FrameLruList;       // LRU list of cached frames
    LIST_ENTRY FrameList;          // List of all frames
    
    // Hash table for fast block lookup
    LIST_ENTRY HashTable[ATLANTIS_HASH_TABLE_SIZE];
    
    // Statistics
    ULONG TotalRequests;           // Total cache requests
    ULONG CacheHits;               // Number of cache hits
    ULONG CacheMisses;             // Number of cache misses
    ULONG BlocksEvicted;           // Blocks evicted due to cache full
    ULONG FramesEvicted;           // Frames evicted due to cache full
    
    // Memory management
    LOOKASIDE_LIST_EX EntryLookaside;  // Lookaside list for cache entries
    LOOKASIDE_LIST_EX FrameLookaside;  // Lookaside list for frames
    LOOKASIDE_LIST_EX HashLookaside;   // Lookaside list for hash entries
    
    // Temporary buffers for I/O operations
    PCHAR TempBuffer;              // Temporary buffer for reads
    PCHAR TempWriteBuffer;         // Temporary buffer for writes
    
} ATLANTIS_CACHE, *PATLANTIS_CACHE;

// Cache modes (matching WCache modes)
#define ATLANTIS_MODE_ROM      0x00000000  // read only (CD-ROM)
#define ATLANTIS_MODE_RW       0x00000001  // rewritable (CD-RW)
#define ATLANTIS_MODE_R        0x00000002  // WORM (CD-R)
#define ATLANTIS_MODE_RAM      0x00000003  // random writable device (HDD)
#define ATLANTIS_MODE_EWR      0x00000004  // ERASE-cycle required (MO)

// Cache flags
#define ATLANTIS_CACHE_WHOLE_PACKET   0x01
#define ATLANTIS_DO_NOT_COMPARE       0x02
#define ATLANTIS_CHAINED_IO           0x04
#define ATLANTIS_MARK_BAD_BLOCKS      0x08
#define ATLANTIS_RO_BAD_BLOCKS        0x10
#define ATLANTIS_NO_WRITE_THROUGH     0x20

// Internal helper functions for cache management
NTSTATUS AtlantisFindCacheEntry__(IN PATLANTIS_CACHE Cache,
                                 IN lba_t Lba,
                                 OUT PATLANTIS_CACHE_ENTRY *Entry);

NTSTATUS AtlantisAllocateCacheEntry__(IN PATLANTIS_CACHE Cache,
                                     IN lba_t Lba,
                                     OUT PATLANTIS_CACHE_ENTRY *Entry);

VOID AtlantisFreeCacheEntry__(IN PATLANTIS_CACHE Cache,
                             IN PATLANTIS_CACHE_ENTRY Entry);

NTSTATUS AtlantisEvictLruBlock__(IN PATLANTIS_CACHE Cache);

VOID AtlantisUpdateLru__(IN PATLANTIS_CACHE Cache,
                        IN PATLANTIS_CACHE_ENTRY Entry);

ULONG AtlantisHashLBA__(IN lba_t Lba);

// Public function declarations (Atlantis interface)
NTSTATUS AtlantisInit__(IN PATLANTIS_CACHE Cache,
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
                       IN PATLANTIS_ERROR_HANDLER ErrorHandlerProc);

NTSTATUS AtlantisSetMode__(IN PATLANTIS_CACHE Cache,
                          IN ULONG Mode);

NTSTATUS AtlantisReadBlocks__(IN PIRP_CONTEXT IrpContext,
                             IN PATLANTIS_CACHE Cache,
                             IN PVOID Context,
                             IN PCHAR Buffer,
                             IN lba_t Lba,
                             IN ULONG BCount,
                             OUT PSIZE_T ReadBytes,
                             IN BOOLEAN CachedOnly);

NTSTATUS AtlantisWriteBlocks__(IN PIRP_CONTEXT IrpContext,
                              IN PATLANTIS_CACHE Cache,
                              IN PVOID Context,
                              IN PCHAR Buffer,
                              IN lba_t Lba,
                              IN ULONG BCount,
                              OUT PSIZE_T WrittenBytes,
                              IN BOOLEAN CachedOnly);

NTSTATUS AtlantisFlushAll__(IN PIRP_CONTEXT IrpContext,
                           IN PATLANTIS_CACHE Cache,
                           IN PVOID Context);

NTSTATUS AtlantisFlushBlocks__(IN PIRP_CONTEXT IrpContext,
                              IN PATLANTIS_CACHE Cache,
                              IN PVOID Context,
                              IN lba_t Lba,
                              IN ULONG BCount);

VOID AtlantisRelease__(IN PATLANTIS_CACHE Cache);

BOOLEAN AtlantisIsInitialized__(IN PATLANTIS_CACHE Cache);

ULONG AtlantisGetWriteBlockCount__(IN PATLANTIS_CACHE Cache);

VOID AtlantisSyncReloc__(IN PATLANTIS_CACHE Cache,
                        IN PVOID Context);

VOID AtlantisDiscardBlocks__(IN PATLANTIS_CACHE Cache,
                            IN PVOID Context,
                            IN lba_t Lba,
                            IN ULONG BCount);

ULONG AtlantisChFlags__(IN PATLANTIS_CACHE Cache,
                      IN ULONG SetFlags,
                      IN ULONG ClrFlags);

NTSTATUS AtlantisDirect__(IN PIRP_CONTEXT IrpContext,
                         IN PATLANTIS_CACHE Cache,
                         IN PVOID Context,
                         IN lba_t Lba,
                         IN BOOLEAN ForWrite,
                         OUT PCHAR* CachedBlock,
                         IN BOOLEAN CachedOnly);

VOID AtlantisStartDirect__(IN PATLANTIS_CACHE Cache,
                          IN PVOID Context,
                          IN BOOLEAN ForWrite);

VOID AtlantisEODirect__(IN PATLANTIS_CACHE Cache,
                       IN PVOID Context);

BOOLEAN AtlantisIsCached__(IN PATLANTIS_CACHE Cache,
                          IN lba_t Lba,
                          IN ULONG BCount);

NTSTATUS AtlantisPurgeAll__(IN PIRP_CONTEXT IrpContext,
                           IN PATLANTIS_CACHE Cache,
                           IN PVOID Context);

// Compatibility macros to map WCache calls to Atlantis calls when UDF_USE_ATLANTIS_CACHE is defined
#ifdef UDF_USE_ATLANTIS_CACHE
#define WCacheInit__                AtlantisInit__
#define WCacheSetMode__             AtlantisSetMode__
#define WCacheReadBlocks__          AtlantisReadBlocks__
#define WCacheWriteBlocks__         AtlantisWriteBlocks__
#define WCacheFlushAll__            AtlantisFlushAll__
#define WCacheFlushBlocks__         AtlantisFlushBlocks__
#define WCacheRelease__             AtlantisRelease__
#define WCacheIsInitialized__       AtlantisIsInitialized__
#define WCacheGetWriteBlockCount__  AtlantisGetWriteBlockCount__
#define WCacheSyncReloc__           AtlantisSyncReloc__
#define WCacheDiscardBlocks__       AtlantisDiscardBlocks__
#define WCacheChFlags__             AtlantisChFlags__
#define WCacheDirect__              AtlantisDirect__
#define WCacheStartDirect__         AtlantisStartDirect__
#define WCacheEODirect__            AtlantisEODirect__
#define WCacheIsCached__            AtlantisIsCached__
#define WCachePurgeAll__            AtlantisPurgeAll__

// Map cache structure type
#define W_CACHE                     ATLANTIS_CACHE
#define PW_CACHE                    PATLANTIS_CACHE

// Map error context and handler types
#define WCACHE_ERROR_CONTEXT        ATLANTIS_ERROR_CONTEXT
#define PWCACHE_ERROR_CONTEXT       PATLANTIS_ERROR_CONTEXT
#define PWC_ERROR_HANDLER           PATLANTIS_ERROR_HANDLER

// Map cache mode constants
#define WCACHE_MODE_ROM             ATLANTIS_MODE_ROM
#define WCACHE_MODE_RW              ATLANTIS_MODE_RW
#define WCACHE_MODE_R               ATLANTIS_MODE_R
#define WCACHE_MODE_RAM             ATLANTIS_MODE_RAM
#define WCACHE_MODE_EWR             ATLANTIS_MODE_EWR

// Map cache flag constants
#define WCACHE_CACHE_WHOLE_PACKET   ATLANTIS_CACHE_WHOLE_PACKET
#define WCACHE_DO_NOT_COMPARE       ATLANTIS_DO_NOT_COMPARE
#define WCACHE_CHAINED_IO           ATLANTIS_CHAINED_IO
#define WCACHE_MARK_BAD_BLOCKS      ATLANTIS_MARK_BAD_BLOCKS
#define WCACHE_RO_BAD_BLOCKS        ATLANTIS_RO_BAD_BLOCKS
#define WCACHE_NO_WRITE_THROUGH     ATLANTIS_NO_WRITE_THROUGH

#endif // UDF_USE_ATLANTIS_CACHE

} // extern "C"

#endif // __ATLANTIS_LIB_H__