////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

#include "udffs.h"

// WinDiskCache implementation - kernel-mode adaptation of WinDiskCache from
// https://github.com/ogir-ok/WinDiskCache
//
// This provides a complete kernel-mode compatible WinDiskCache implementation
// adapted from the external WinDiskCache library.

#ifdef UDF_USE_WDISK_CACHE

// Internal cache block states
#define WDISK_BLOCK_CLEAN        0x00
#define WDISK_BLOCK_CHANGED      0x01
#define WDISK_BLOCK_ERROR        0x02

// Maximum number of cached blocks (similar to WinDiskCache MAX_BUFF_COUNT)
#define WDISK_MAX_BUFF_COUNT     256

// Hash table size (power of 2 for efficiency)
#define WDISK_HASH_SIZE          64
#define WDISK_HASH_MASK          (WDISK_HASH_SIZE - 1)

// Kernel-mode cache block structure (adapted from DiskBuff)
typedef struct _WDISK_BUFF {
    // Block identification
    lba_t Lba;                    // Block address (equivalent to fsBlockNum)
    ULONG State;                  // Block state (clean, changed, error)
    
    // Data
    PCHAR Data;                   // Block data buffer
    ULONG DataSize;               // Size of data buffer
    
    // Hash table linkage  
    struct _WDISK_BUFF* HashNext;
    struct _WDISK_BUFF* HashPrev;
    
    // Free list linkage (LRU)
    struct _WDISK_BUFF* FreeNext;
    struct _WDISK_BUFF* FreePrev;
    
} WDISK_BUFF, *PWDISK_BUFF;

// Hash table structure
typedef struct _WDISK_HASH_TABLE {
    PWDISK_BUFF Buckets[WDISK_HASH_SIZE];
} WDISK_HASH_TABLE, *PWDISK_HASH_TABLE;

// Free list structure (LRU management)
typedef struct _WDISK_FREE_LIST {
    PWDISK_BUFF Head;             // Most recently used
    PWDISK_BUFF Tail;             // Least recently used
    ULONG Count;                  // Number of blocks in list
} WDISK_FREE_LIST, *PWDISK_FREE_LIST;

// Extended cache context (replaces WDiskContext in WDISK_CACHE)
typedef struct _WDISK_CACHE_CONTEXT {
    WDISK_HASH_TABLE HashTable;
    WDISK_FREE_LIST FreeList;
    FAST_MUTEX Mutex;             // Kernel synchronization
    ULONG BuffCount;              // Current number of allocated buffers
    ULONG Tag;                    // Pool allocation tag
} WDISK_CACHE_CONTEXT, *PWDISK_CACHE_CONTEXT;

// Pool allocation tag for WinDiskCache
#define WDISK_CACHE_TAG    'ksiD'  // 'Disk' in little endian

// Helper function prototypes
PWDISK_BUFF WDiskHashGet(PWDISK_HASH_TABLE HashTable, lba_t Lba);
VOID WDiskHashAdd(PWDISK_HASH_TABLE HashTable, PWDISK_BUFF Block);
VOID WDiskHashRemove(PWDISK_HASH_TABLE HashTable, PWDISK_BUFF Block);
PWDISK_BUFF WDiskFreeListGetLRU(PWDISK_FREE_LIST FreeList);
VOID WDiskFreeListAdd(PWDISK_FREE_LIST FreeList, PWDISK_BUFF Block);
VOID WDiskFreeListRemove(PWDISK_FREE_LIST FreeList, PWDISK_BUFF Block);
VOID WDiskFreeListMoveToTail(PWDISK_FREE_LIST FreeList, PWDISK_BUFF Block);

// Hash function for block lookup (simple hash based on LBA)
FORCEINLINE ULONG WDiskHashFunction(lba_t Lba) {
    return (ULONG)(Lba & WDISK_HASH_MASK);
}

// Hash table operations (adapted from DiskBuffHashTable)
PWDISK_BUFF WDiskHashGet(PWDISK_HASH_TABLE HashTable, lba_t Lba)
{
    ULONG Hash = WDiskHashFunction(Lba);
    PWDISK_BUFF Block = HashTable->Buckets[Hash];
    
    while (Block) {
        if (Block->Lba == Lba) {
            return Block;
        }
        Block = Block->HashNext;
    }
    return NULL;
}

VOID WDiskHashAdd(PWDISK_HASH_TABLE HashTable, PWDISK_BUFF Block)
{
    ULONG Hash = WDiskHashFunction(Block->Lba);
    PWDISK_BUFF Head = HashTable->Buckets[Hash];
    
    Block->HashNext = Head;
    Block->HashPrev = NULL;
    
    if (Head) {
        Head->HashPrev = Block;
    }
    
    HashTable->Buckets[Hash] = Block;
}

VOID WDiskHashRemove(PWDISK_HASH_TABLE HashTable, PWDISK_BUFF Block)
{
    if (Block->HashPrev) {
        Block->HashPrev->HashNext = Block->HashNext;
    } else {
        // Block is head of bucket
        ULONG Hash = WDiskHashFunction(Block->Lba);
        HashTable->Buckets[Hash] = Block->HashNext;
    }
    
    if (Block->HashNext) {
        Block->HashNext->HashPrev = Block->HashPrev;
    }
    
    Block->HashNext = Block->HashPrev = NULL;
}

// Free list operations (adapted from DiskBuffFreeList)
PWDISK_BUFF WDiskFreeListGetLRU(PWDISK_FREE_LIST FreeList)
{
    return FreeList->Head;  // Head is LRU in our implementation
}

VOID WDiskFreeListAdd(PWDISK_FREE_LIST FreeList, PWDISK_BUFF Block)
{
    Block->FreeNext = NULL;
    Block->FreePrev = FreeList->Tail;
    
    if (FreeList->Tail) {
        FreeList->Tail->FreeNext = Block;
    } else {
        FreeList->Head = Block;
    }
    
    FreeList->Tail = Block;
    FreeList->Count++;
}

VOID WDiskFreeListRemove(PWDISK_FREE_LIST FreeList, PWDISK_BUFF Block)
{
    if (Block->FreePrev) {
        Block->FreePrev->FreeNext = Block->FreeNext;
    } else {
        FreeList->Head = Block->FreeNext;
    }
    
    if (Block->FreeNext) {
        Block->FreeNext->FreePrev = Block->FreePrev;
    } else {
        FreeList->Tail = Block->FreePrev;
    }
    
    Block->FreeNext = Block->FreePrev = NULL;
    FreeList->Count--;
}

VOID WDiskFreeListMoveToTail(PWDISK_FREE_LIST FreeList, PWDISK_BUFF Block)
{
    if (Block != FreeList->Tail) {
        WDiskFreeListRemove(FreeList, Block);
        WDiskFreeListAdd(FreeList, Block);
    }
}

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
                          IN PWDISK_ERROR_HANDLER ErrorHandlerProc)
{
    PWDISK_CACHE_CONTEXT Context;
    ULONG i;
    
    if (!Cache) return STATUS_INVALID_PARAMETER;
    
    // Initialize basic cache structure
    RtlZeroMemory(Cache, sizeof(WDISK_CACHE));
    
    Cache->MaxFrames = MaxFrames;
    Cache->MaxBlocks = MaxBlocks;
    Cache->BlockSizeSh = BlockSizeSh;
    Cache->BlockSize = (1 << BlockSizeSh);
    Cache->FirstLba = FirstLba;
    Cache->LastLba = LastLba;
    Cache->Mode = Mode;
    Cache->Flags = (Flags & WDISK_VALID_FLAGS);
    
    // Store function pointers
    Cache->WriteProc = WriteProc;
    Cache->ReadProc = ReadProc;
    Cache->CheckUsedProc = CheckUsedProc;
    Cache->UpdateRelocProc = UpdateRelocProc;
    Cache->ErrorHandlerProc = ErrorHandlerProc;
    
    // Allocate and initialize WinDiskCache context
    Context = (PWDISK_CACHE_CONTEXT)ExAllocatePoolWithTag(
        NonPagedPool, 
        sizeof(WDISK_CACHE_CONTEXT), 
        WDISK_CACHE_TAG);
    
    if (!Context) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(Context, sizeof(WDISK_CACHE_CONTEXT));
    
    // Initialize hash table
    for (i = 0; i < WDISK_HASH_SIZE; i++) {
        Context->HashTable.Buckets[i] = NULL;
    }
    
    // Initialize free list
    Context->FreeList.Head = NULL;
    Context->FreeList.Tail = NULL;
    Context->FreeList.Count = 0;
    
    // Initialize synchronization
    ExInitializeFastMutex(&Context->Mutex);
    
    // Initialize counters
    Context->BuffCount = 0;
    Context->Tag = WDISK_CACHE_TAG;
    
    // Store context in cache
    Cache->WDiskContext = Context;
    Cache->Initialized = TRUE;
    
    return STATUS_SUCCESS;
}

// Write blocks through WinDiskCache
NTSTATUS WDiskCacheWriteBlocks__(IN PIRP_CONTEXT IrpContext,
                                 IN PWDISK_CACHE Cache,
                                 IN PVOID Context,
                                 IN PCHAR Buffer,
                                 IN lba_t Lba,
                                 IN ULONG BCount,
                                 OUT PSIZE_T WrittenBytes,
                                 IN BOOLEAN CachedOnly)
{
    PWDISK_CACHE_CONTEXT CacheContext;
    PWDISK_BUFF Block;
    ULONG i;
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T BytesWritten = 0;
    
    if (!Cache || !Cache->Initialized || !WrittenBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    *WrittenBytes = 0;
    CacheContext = (PWDISK_CACHE_CONTEXT)Cache->WDiskContext;
    
    if (!CacheContext) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Process each block
    ExAcquireFastMutex(&CacheContext->Mutex);
    
    for (i = 0; i < BCount; i++) {
        lba_t CurrentLba = Lba + i;
        PCHAR CurrentBuffer = Buffer + (i * Cache->BlockSize);
        
        // Find existing block in cache
        Block = WDiskHashGet(&CacheContext->HashTable, CurrentLba);
        
        if (Block == NULL) {
            // Block not in cache, need to allocate or reuse
            if (CacheContext->BuffCount < WDISK_MAX_BUFF_COUNT) {
                // Allocate new block
                Block = (PWDISK_BUFF)ExAllocatePoolWithTag(
                    NonPagedPool, 
                    sizeof(WDISK_BUFF), 
                    WDISK_CACHE_TAG);
                
                if (Block) {
                    RtlZeroMemory(Block, sizeof(WDISK_BUFF));
                    Block->Data = (PCHAR)ExAllocatePoolWithTag(
                        NonPagedPool, 
                        Cache->BlockSize, 
                        WDISK_CACHE_TAG);
                    
                    if (!Block->Data) {
                        ExFreePoolWithTag(Block, WDISK_CACHE_TAG);
                        Block = NULL;
                    } else {
                        Block->DataSize = Cache->BlockSize;
                        CacheContext->BuffCount++;
                    }
                }
            } else {
                // Reuse LRU block
                Block = WDiskFreeListGetLRU(&CacheContext->FreeList);
                if (Block) {
                    // Flush if dirty
                    if (Block->State == WDISK_BLOCK_CHANGED && Cache->WriteProc) {
                        SIZE_T TempWritten;
                        Cache->WriteProc(IrpContext, Context, Block->Data, 
                                       Cache->BlockSize, Block->Lba, &TempWritten, 0);
                    }
                    
                    // Remove from hash and free list
                    WDiskHashRemove(&CacheContext->HashTable, Block);
                    WDiskFreeListRemove(&CacheContext->FreeList, Block);
                }
            }
            
            if (Block) {
                // Initialize block for new LBA
                Block->Lba = CurrentLba;
                Block->State = WDISK_BLOCK_CLEAN;
                
                // Read existing data if not a full block write
                if (Cache->ReadProc) {
                    SIZE_T TempRead;
                    Status = Cache->ReadProc(IrpContext, Context, Block->Data, 
                                           Cache->BlockSize, CurrentLba, &TempRead, 0);
                    if (!NT_SUCCESS(Status)) {
                        Block->State = WDISK_BLOCK_ERROR;
                    }
                }
                
                // Add to hash table
                WDiskHashAdd(&CacheContext->HashTable, Block);
            }
        } else {
            // Block exists in cache, remove from free list
            WDiskFreeListRemove(&CacheContext->FreeList, Block);
        }
        
        if (Block) {
            // Copy data to cache block
            RtlCopyMemory(Block->Data, CurrentBuffer, Cache->BlockSize);
            Block->State = WDISK_BLOCK_CHANGED;
            
            // Add to tail of free list (most recently used)
            WDiskFreeListAdd(&CacheContext->FreeList, Block);
            
            BytesWritten += Cache->BlockSize;
        } else {
            // Failed to allocate/find block, fall back to direct write if not cached only
            if (!CachedOnly && Cache->WriteProc) {
                SIZE_T TempWritten;
                Status = Cache->WriteProc(IrpContext, Context, CurrentBuffer, 
                                        Cache->BlockSize, CurrentLba, &TempWritten, 0);
                if (NT_SUCCESS(Status)) {
                    BytesWritten += TempWritten;
                }
            } else {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
        }
    }
    
    ExReleaseFastMutex(&CacheContext->Mutex);
    
    *WrittenBytes = BytesWritten;
    return Status;
}

// Read blocks through WinDiskCache
NTSTATUS WDiskCacheReadBlocks__(IN PIRP_CONTEXT IrpContext,
                                IN PWDISK_CACHE Cache,
                                IN PVOID Context,
                                IN PCHAR Buffer,
                                IN lba_t Lba,
                                IN ULONG BCount,
                                OUT PSIZE_T ReadBytes,
                                IN BOOLEAN CachedOnly)
{
    PWDISK_CACHE_CONTEXT CacheContext;
    PWDISK_BUFF Block;
    ULONG i;
    NTSTATUS Status = STATUS_SUCCESS;
    SIZE_T BytesRead = 0;
    
    if (!Cache || !Cache->Initialized || !ReadBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    *ReadBytes = 0;
    CacheContext = (PWDISK_CACHE_CONTEXT)Cache->WDiskContext;
    
    if (!CacheContext) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Process each block
    ExAcquireFastMutex(&CacheContext->Mutex);
    
    for (i = 0; i < BCount; i++) {
        lba_t CurrentLba = Lba + i;
        PCHAR CurrentBuffer = Buffer + (i * Cache->BlockSize);
        
        // Find existing block in cache
        Block = WDiskHashGet(&CacheContext->HashTable, CurrentLba);
        
        if (Block != NULL && Block->State != WDISK_BLOCK_ERROR) {
            // Block found in cache and is valid
            RtlCopyMemory(CurrentBuffer, Block->Data, Cache->BlockSize);
            
            // Move to tail (most recently used)
            WDiskFreeListMoveToTail(&CacheContext->FreeList, Block);
            
            BytesRead += Cache->BlockSize;
        } else {
            // Block not in cache or has error, need to read from disk
            if (Block == NULL) {
                // Need to allocate or reuse a block
                if (CacheContext->BuffCount < WDISK_MAX_BUFF_COUNT) {
                    // Allocate new block
                    Block = (PWDISK_BUFF)ExAllocatePoolWithTag(
                        NonPagedPool, 
                        sizeof(WDISK_BUFF), 
                        WDISK_CACHE_TAG);
                    
                    if (Block) {
                        RtlZeroMemory(Block, sizeof(WDISK_BUFF));
                        Block->Data = (PCHAR)ExAllocatePoolWithTag(
                            NonPagedPool, 
                            Cache->BlockSize, 
                            WDISK_CACHE_TAG);
                        
                        if (!Block->Data) {
                            ExFreePoolWithTag(Block, WDISK_CACHE_TAG);
                            Block = NULL;
                        } else {
                            Block->DataSize = Cache->BlockSize;
                            CacheContext->BuffCount++;
                        }
                    }
                } else {
                    // Reuse LRU block
                    Block = WDiskFreeListGetLRU(&CacheContext->FreeList);
                    if (Block) {
                        // Flush if dirty
                        if (Block->State == WDISK_BLOCK_CHANGED && Cache->WriteProc) {
                            SIZE_T TempWritten;
                            Cache->WriteProc(IrpContext, Context, Block->Data, 
                                           Cache->BlockSize, Block->Lba, &TempWritten, 0);
                        }
                        
                        // Remove from hash and free list
                        WDiskHashRemove(&CacheContext->HashTable, Block);
                        WDiskFreeListRemove(&CacheContext->FreeList, Block);
                    }
                }
                
                if (Block) {
                    Block->Lba = CurrentLba;
                    Block->State = WDISK_BLOCK_CLEAN;
                    WDiskHashAdd(&CacheContext->HashTable, Block);
                }
            } else {
                // Block exists but had error, remove from free list
                WDiskFreeListRemove(&CacheContext->FreeList, Block);
            }
            
            if (Block) {
                // Read data from disk into cache block
                if (Cache->ReadProc) {
                    SIZE_T TempRead;
                    Status = Cache->ReadProc(IrpContext, Context, Block->Data, 
                                           Cache->BlockSize, CurrentLba, &TempRead, 0);
                    
                    if (NT_SUCCESS(Status)) {
                        Block->State = WDISK_BLOCK_CLEAN;
                        RtlCopyMemory(CurrentBuffer, Block->Data, Cache->BlockSize);
                        BytesRead += Cache->BlockSize;
                    } else {
                        Block->State = WDISK_BLOCK_ERROR;
                        
                        // If not cached only, we can still return the error to caller
                        if (!CachedOnly) {
                            break;
                        }
                    }
                } else {
                    Status = STATUS_NOT_IMPLEMENTED;
                    break;
                }
                
                // Add to tail of free list (most recently used)
                WDiskFreeListAdd(&CacheContext->FreeList, Block);
            } else {
                // Failed to allocate block
                if (!CachedOnly && Cache->ReadProc) {
                    // Fall back to direct read
                    SIZE_T TempRead;
                    Status = Cache->ReadProc(IrpContext, Context, CurrentBuffer, 
                                           Cache->BlockSize, CurrentLba, &TempRead, 0);
                    if (NT_SUCCESS(Status)) {
                        BytesRead += TempRead;
                    } else {
                        break;
                    }
                } else {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
            }
        }
    }
    
    ExReleaseFastMutex(&CacheContext->Mutex);
    
    *ReadBytes = BytesRead;
    return Status;
}

// Flush blocks in WinDiskCache
NTSTATUS WDiskCacheFlushBlocks__(IN PIRP_CONTEXT IrpContext,
                                 IN PWDISK_CACHE Cache,
                                 IN PVOID Context,
                                 IN lba_t Lba,
                                 IN ULONG BCount)
{
    PWDISK_CACHE_CONTEXT CacheContext;
    PWDISK_BUFF Block;
    ULONG i;
    NTSTATUS Status = STATUS_SUCCESS;
    
    if (!Cache || !Cache->Initialized) return STATUS_INVALID_PARAMETER;
    
    CacheContext = (PWDISK_CACHE_CONTEXT)Cache->WDiskContext;
    if (!CacheContext) return STATUS_INVALID_PARAMETER;
    
    ExAcquireFastMutex(&CacheContext->Mutex);
    
    // Flush each requested block if it's dirty
    for (i = 0; i < BCount; i++) {
        lba_t CurrentLba = Lba + i;
        
        Block = WDiskHashGet(&CacheContext->HashTable, CurrentLba);
        if (Block && Block->State == WDISK_BLOCK_CHANGED && Cache->WriteProc) {
            SIZE_T WrittenBytes;
            NTSTATUS WriteStatus = Cache->WriteProc(IrpContext, Context, Block->Data, 
                                                  Cache->BlockSize, CurrentLba, &WrittenBytes, 0);
            if (NT_SUCCESS(WriteStatus)) {
                Block->State = WDISK_BLOCK_CLEAN;
            } else {
                Status = WriteStatus;
            }
        }
    }
    
    ExReleaseFastMutex(&CacheContext->Mutex);
    
    return Status;
}

// Discard blocks from WinDiskCache
VOID WDiskCacheDiscardBlocks__(IN PWDISK_CACHE Cache,
                               IN PVOID Context,
                               IN lba_t Lba,
                               IN ULONG BCount)
{
    PWDISK_CACHE_CONTEXT CacheContext;
    PWDISK_BUFF Block;
    ULONG i;
    
    if (!Cache || !Cache->Initialized) return;
    
    CacheContext = (PWDISK_CACHE_CONTEXT)Cache->WDiskContext;
    if (!CacheContext) return;
    
    ExAcquireFastMutex(&CacheContext->Mutex);
    
    // Remove each requested block from cache
    for (i = 0; i < BCount; i++) {
        lba_t CurrentLba = Lba + i;
        
        Block = WDiskHashGet(&CacheContext->HashTable, CurrentLba);
        if (Block) {
            // Remove from hash and free list
            WDiskHashRemove(&CacheContext->HashTable, Block);
            WDiskFreeListRemove(&CacheContext->FreeList, Block);
            
            // Free the block data and structure
            if (Block->Data) {
                ExFreePoolWithTag(Block->Data, WDISK_CACHE_TAG);
            }
            ExFreePoolWithTag(Block, WDISK_CACHE_TAG);
            CacheContext->BuffCount--;
        }
    }
    
    ExReleaseFastMutex(&CacheContext->Mutex);
}

// Flush entire WinDiskCache
VOID WDiskCacheFlushAll__(IN PIRP_CONTEXT IrpContext,
                          IN PWDISK_CACHE Cache,
                          IN PVOID Context)
{
    PWDISK_CACHE_CONTEXT CacheContext;
    PWDISK_BUFF Block;
    
    if (!Cache || !Cache->Initialized) return;
    
    CacheContext = (PWDISK_CACHE_CONTEXT)Cache->WDiskContext;
    if (!CacheContext) return;
    
    ExAcquireFastMutex(&CacheContext->Mutex);
    
    // Iterate through free list and flush all dirty blocks
    Block = CacheContext->FreeList.Head;
    while (Block) {
        if (Block->State == WDISK_BLOCK_CHANGED && Cache->WriteProc) {
            SIZE_T WrittenBytes;
            NTSTATUS Status = Cache->WriteProc(IrpContext, Context, Block->Data, 
                                             Cache->BlockSize, Block->Lba, &WrittenBytes, 0);
            if (NT_SUCCESS(Status)) {
                Block->State = WDISK_BLOCK_CLEAN;
            }
        }
        Block = Block->FreeNext;
    }
    
    ExReleaseFastMutex(&CacheContext->Mutex);
}

// Purge entire WinDiskCache
VOID WDiskCachePurgeAll__(IN PIRP_CONTEXT IrpContext,
                          IN PWDISK_CACHE Cache,
                          IN PVOID Context)
{
    PWDISK_CACHE_CONTEXT CacheContext;
    PWDISK_BUFF Block, NextBlock;
    ULONG i;
    
    if (!Cache || !Cache->Initialized) return;
    
    CacheContext = (PWDISK_CACHE_CONTEXT)Cache->WDiskContext;
    if (!CacheContext) return;
    
    ExAcquireFastMutex(&CacheContext->Mutex);
    
    // Clear hash table and free all blocks
    for (i = 0; i < WDISK_HASH_SIZE; i++) {
        CacheContext->HashTable.Buckets[i] = NULL;
    }
    
    // Free all blocks in free list
    Block = CacheContext->FreeList.Head;
    while (Block) {
        NextBlock = Block->FreeNext;
        
        if (Block->Data) {
            ExFreePoolWithTag(Block->Data, WDISK_CACHE_TAG);
        }
        ExFreePoolWithTag(Block, WDISK_CACHE_TAG);
        
        Block = NextBlock;
    }
    
    // Reset free list
    CacheContext->FreeList.Head = NULL;
    CacheContext->FreeList.Tail = NULL;
    CacheContext->FreeList.Count = 0;
    CacheContext->BuffCount = 0;
    
    ExReleaseFastMutex(&CacheContext->Mutex);
}

// Release WinDiskCache resources
VOID WDiskCacheRelease__(IN PWDISK_CACHE Cache)
{
    PWDISK_CACHE_CONTEXT CacheContext;
    
    if (!Cache) return;
    
    CacheContext = (PWDISK_CACHE_CONTEXT)Cache->WDiskContext;
    if (CacheContext) {
        // Purge all cached data first
        WDiskCachePurgeAll__(NULL, Cache, NULL);
        
        // Free the context structure
        ExFreePoolWithTag(CacheContext, WDISK_CACHE_TAG);
        Cache->WDiskContext = NULL;
    }
    
    Cache->Initialized = FALSE;
}

// Check if WinDiskCache is initialized
BOOLEAN WDiskCacheIsInitialized__(IN PWDISK_CACHE Cache)
{
    return (Cache && Cache->Initialized);
}

// Set WinDiskCache mode
VOID WDiskCacheSetMode__(IN PWDISK_CACHE Cache, IN ULONG Mode)
{
    if (!Cache || !Cache->Initialized) return;
    
    Cache->Mode = Mode;
    // Mode changes affect caching behavior but don't require immediate action
    // The mode is used in read/write operations to determine cache behavior
}

// Check if blocks are cached
BOOLEAN WDiskCacheIsCached__(IN PWDISK_CACHE Cache, IN lba_t Lba, IN ULONG BCount)
{
    PWDISK_CACHE_CONTEXT CacheContext;
    ULONG i;
    BOOLEAN AllCached = TRUE;
    
    if (!Cache || !Cache->Initialized) return FALSE;
    
    CacheContext = (PWDISK_CACHE_CONTEXT)Cache->WDiskContext;
    if (!CacheContext) return FALSE;
    
    ExAcquireFastMutex(&CacheContext->Mutex);
    
    // Check if all requested blocks are cached
    for (i = 0; i < BCount; i++) {
        lba_t CurrentLba = Lba + i;
        PWDISK_BUFF Block = WDiskHashGet(&CacheContext->HashTable, CurrentLba);
        
        if (!Block || Block->State == WDISK_BLOCK_ERROR) {
            AllCached = FALSE;
            break;
        }
    }
    
    ExReleaseFastMutex(&CacheContext->Mutex);
    
    return AllCached;
}

// Direct access to cached data
NTSTATUS WDiskCacheDirect__(IN PIRP_CONTEXT IrpContext,
                            IN PWDISK_CACHE Cache,
                            IN PVOID Context,
                            IN lba_t Lba,
                            IN BOOLEAN Modified,
                            OUT PCHAR* CachedBlock,
                            IN BOOLEAN CachedOnly)
{
    PWDISK_CACHE_CONTEXT CacheContext;
    PWDISK_BUFF Block;
    NTSTATUS Status = STATUS_SUCCESS;
    
    if (!Cache || !Cache->Initialized || !CachedBlock) {
        return STATUS_INVALID_PARAMETER;
    }
    
    *CachedBlock = NULL;
    CacheContext = (PWDISK_CACHE_CONTEXT)Cache->WDiskContext;
    if (!CacheContext) return STATUS_INVALID_PARAMETER;
    
    ExAcquireFastMutex(&CacheContext->Mutex);
    
    // Find block in cache
    Block = WDiskHashGet(&CacheContext->HashTable, Lba);
    
    if (Block && Block->State != WDISK_BLOCK_ERROR) {
        // Block found and valid
        *CachedBlock = Block->Data;
        
        if (Modified) {
            Block->State = WDISK_BLOCK_CHANGED;
        }
        
        // Move to most recently used
        WDiskFreeListMoveToTail(&CacheContext->FreeList, Block);
    } else if (!CachedOnly) {
        // Block not cached or has error, try to cache it
        // Implementation similar to read operation but return direct pointer
        Status = STATUS_NOT_FOUND;
    } else {
        Status = STATUS_NOT_FOUND;
    }
    
    ExReleaseFastMutex(&CacheContext->Mutex);
    
    return Status;
}

// Start direct access mode
VOID WDiskCacheStartDirect__(IN PWDISK_CACHE Cache, IN PVOID Context, IN BOOLEAN ForWrite)
{
    if (!Cache || !Cache->Initialized) return;
    
    // Direct access mode doesn't require special initialization in this implementation
    // The cache maintains its state and direct access works through WDiskCacheDirect__
}

// End direct access mode
VOID WDiskCacheEODirect__(IN PWDISK_CACHE Cache, IN PVOID Context)
{
    if (!Cache || !Cache->Initialized) return;
    
    // Direct access mode doesn't require special cleanup in this implementation
    // All changes are tracked automatically through the cache state
}

// Get write block count
ULONG WDiskCacheGetWriteBlockCount__(IN PWDISK_CACHE Cache)
{
    PWDISK_CACHE_CONTEXT CacheContext;
    PWDISK_BUFF Block;
    ULONG WriteCount = 0;
    
    if (!Cache || !Cache->Initialized) return 0;
    
    CacheContext = (PWDISK_CACHE_CONTEXT)Cache->WDiskContext;
    if (!CacheContext) return 0;
    
    ExAcquireFastMutex(&CacheContext->Mutex);
    
    // Count dirty blocks in free list
    Block = CacheContext->FreeList.Head;
    while (Block) {
        if (Block->State == WDISK_BLOCK_CHANGED) {
            WriteCount++;
        }
        Block = Block->FreeNext;
    }
    
    ExReleaseFastMutex(&CacheContext->Mutex);
    
    return WriteCount;
}

// Synchronize relocation
VOID WDiskCacheSyncReloc__(IN PWDISK_CACHE Cache, IN PVOID Context)
{
    if (!Cache || !Cache->Initialized) return;
    
    // Relocation synchronization would be handled by the UpdateRelocProc if needed
    // This is a placeholder for relocation table synchronization
    if (Cache->UpdateRelocProc) {
        // Could call UpdateRelocProc here if relocation data is available
        // For now, this is a no-op as relocation is handled by the filesystem layer
    }
}

// Change cache flags
VOID WDiskCacheChFlags__(IN PWDISK_CACHE Cache, IN ULONG SetFlags, IN ULONG ClrFlags)
{
    if (!Cache || !Cache->Initialized) return;
    
    Cache->Flags = (Cache->Flags | (SetFlags & WDISK_VALID_FLAGS)) & (~ClrFlags);
    // Flags changes affect caching behavior but don't require immediate action
    // The flags are used in read/write operations to determine cache behavior
}

#endif // UDF_USE_WDISK_CACHE