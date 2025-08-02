////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

#include "udffs.h"
#include "udfs_cache.h"

// define the file specific bug-check id
#define UDF_BUG_CHECK_ID UDF_FILE_WCACHE

// Memory allocation tags
#define MEM_UDCACHE_TAG 'hcDU'
#define MEM_UDENTRY_TAG 'neDU'
#define MEM_UDBUFFER_TAG 'fbDU'

// Hash function for LBA -> hash table index
__inline ULONG
UdfsCacheHash(IN lba_t Lba, IN ULONG HashSize)
{
    return (ULONG)(Lba % HashSize);
}

// Get current time for LRU
__inline VOID
UdfsCacheGetTime(OUT PLARGE_INTEGER Time)
{
    KeQuerySystemTime(Time);
}

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
    )
{
    NTSTATUS Status;
    ULONG HashSize;
    ULONG i;
    
    if (!Cache || !WriteProc || !ReadProc) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Clear the cache structure
    RtlZeroMemory(Cache, sizeof(UDFS_CACHE));
    
    // Set basic parameters
    Cache->Tag = 'hcDU';
    Cache->MaxEntries = MaxEntries;
    Cache->BlockSize = BlockSize;
    Cache->FirstLba = FirstLba;
    Cache->LastLba = LastLba;
    Cache->Mode = Mode;
    Cache->WriteProc = WriteProc;
    Cache->ReadProc = ReadProc;
    Cache->ErrorHandlerProc = ErrorHandlerProc;
    
    // Initialize write-back support
    Cache->DirtyListHead = NULL;
    Cache->DirtyListTail = NULL;
    Cache->DirtyCount = 0;
    Cache->MaxDirtyCount = UDFS_CACHE_DEFAULT_DIRTY_THRESHOLD;
    Cache->FlushInterval = UDFS_CACHE_DEFAULT_FLUSH_INTERVAL;
    UdfsCacheGetTime(&Cache->LastFlushTime);
    
    // Initialize sequential write optimization
    Cache->LastWriteLba = (lba_t)-1;
    Cache->SequentialCount = 0;
    Cache->InSequentialMode = FALSE;
    
    // Initialize statistics
    Cache->FlushCount = 0;
    Cache->BatchCount = 0;
    
    // Calculate block size shift
    Cache->BlockSizeSh = 0;
    ULONG TempBlockSize = BlockSize;
    while (TempBlockSize > 1) {
        TempBlockSize >>= 1;
        Cache->BlockSizeSh++;
    }
    
    // Use prime number for hash table size to reduce collisions
    HashSize = MaxEntries * 2;
    if (HashSize < 127) HashSize = 127;
    else if (HashSize < 251) HashSize = 251;
    else if (HashSize < 509) HashSize = 509;
    else if (HashSize < 1021) HashSize = 1021;
    else HashSize = 2047;
    
    Cache->HashSize = HashSize;
    
    // Allocate hash table
    Cache->HashTable = (PUDFS_CACHE_ENTRY*)MyAllocatePoolTag__(
        NonPagedPool, 
        HashSize * sizeof(PUDFS_CACHE_ENTRY), 
        MEM_UDCACHE_TAG);
        
    if (!Cache->HashTable) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize hash table
    for (i = 0; i < HashSize; i++) {
        Cache->HashTable[i] = NULL;
    }
    
    // Pre-allocate entry pool
    Cache->EntryPool = (PUDFS_CACHE_ENTRY)MyAllocatePoolTag__(
        NonPagedPool,
        MaxEntries * sizeof(UDFS_CACHE_ENTRY),
        MEM_UDENTRY_TAG);
        
    if (!Cache->EntryPool) {
        MyFreePool__(Cache->HashTable);
        Cache->HashTable = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Pre-allocate buffer pool
    Cache->BufferPool = (PCHAR)MyAllocatePoolTag__(
        NonPagedPool,
        (SIZE_T)MaxEntries * BlockSize,
        MEM_UDBUFFER_TAG);
        
    if (!Cache->BufferPool) {
        MyFreePool__(Cache->EntryPool);
        MyFreePool__(Cache->HashTable);
        Cache->EntryPool = NULL;
        Cache->HashTable = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize entry pool
    for (i = 0; i < MaxEntries; i++) {
        Cache->EntryPool[i].Buffer = Cache->BufferPool + (i * BlockSize);
        Cache->EntryPool[i].Flags = 0;
        Cache->EntryPool[i].Next = NULL;
        Cache->EntryPool[i].DirtyNext = NULL;
        Cache->EntryPool[i].DirtyPrev = NULL;
    }
    
    // Initialize synchronization
    Status = ExInitializeResourceLite(&Cache->CacheLock);
    if (!NT_SUCCESS(Status)) {
        MyFreePool__(Cache->BufferPool);
        MyFreePool__(Cache->EntryPool);
        MyFreePool__(Cache->HashTable);
        Cache->BufferPool = NULL;
        Cache->EntryPool = NULL;
        Cache->HashTable = NULL;
        return Status;
    }
    
    Cache->Initialized = TRUE;
    return STATUS_SUCCESS;
}

// Find cache entry by LBA
__inline PUDFS_CACHE_ENTRY
UdfsCacheFindEntry(
    IN PUDFS_CACHE Cache,
    IN lba_t Lba
    )
{
    ULONG Hash = UdfsCacheHash(Lba, Cache->HashSize);
    PUDFS_CACHE_ENTRY Entry = Cache->HashTable[Hash];
    
    while (Entry) {
        if (Entry->Lba == Lba && (Entry->Flags & UDFS_CACHE_VALID)) {
            // Update access time for LRU
            UdfsCacheGetTime(&Entry->LastAccess);
            return Entry;
        }
        Entry = Entry->Next;
    }
    
    return NULL;
}

// Find least recently used entry to evict
PUDFS_CACHE_ENTRY
UdfsCacheFindLRUEntry(
    IN PUDFS_CACHE Cache
    )
{
    PUDFS_CACHE_ENTRY LRUEntry = NULL;
    LARGE_INTEGER OldestTime;
    ULONG i;
    
    OldestTime.QuadPart = MAXLONGLONG;
    
    // Simple linear search through entry pool
    for (i = 0; i < Cache->MaxEntries; i++) {
        PUDFS_CACHE_ENTRY Entry = &Cache->EntryPool[i];
        
        // If entry is not in use, use it
        if (!(Entry->Flags & UDFS_CACHE_VALID)) {
            return Entry;
        }
        
        // Find oldest entry
        if (Entry->LastAccess.QuadPart < OldestTime.QuadPart) {
            OldestTime = Entry->LastAccess;
            LRUEntry = Entry;
        }
    }
    
    return LRUEntry;
}

// Remove entry from hash table
VOID
UdfsCacheRemoveEntry(
    IN PUDFS_CACHE Cache,
    IN PUDFS_CACHE_ENTRY EntryToRemove
    )
{
    ULONG Hash = UdfsCacheHash(EntryToRemove->Lba, Cache->HashSize);
    PUDFS_CACHE_ENTRY Entry = Cache->HashTable[Hash];
    PUDFS_CACHE_ENTRY PrevEntry = NULL;
    
    while (Entry) {
        if (Entry == EntryToRemove) {
            // Remove from hash chain
            if (PrevEntry) {
                PrevEntry->Next = Entry->Next;
            } else {
                Cache->HashTable[Hash] = Entry->Next;
            }
            
            // Remove from dirty list if necessary
            if (Entry->Flags & UDFS_CACHE_MODIFIED) {
                UdfsCacheRemoveFromDirtyList(Cache, Entry);
            }
            
            Entry->Next = NULL;
            Entry->Flags = 0;
            if (Cache->CurrentEntries > 0) {
                Cache->CurrentEntries--;
            }
            return;
        }
        PrevEntry = Entry;
        Entry = Entry->Next;
    }
}

// Add entry to hash table
VOID
UdfsCacheAddEntry(
    IN PUDFS_CACHE Cache,
    IN PUDFS_CACHE_ENTRY Entry,
    IN lba_t Lba
    )
{
    ULONG Hash = UdfsCacheHash(Lba, Cache->HashSize);
    
    Entry->Lba = Lba;
    Entry->Flags = UDFS_CACHE_VALID;
    UdfsCacheGetTime(&Entry->LastAccess);
    Entry->Next = Cache->HashTable[Hash];
    Cache->HashTable[Hash] = Entry;
    Cache->CurrentEntries++;
}

// Add entry to dirty list for write-back caching
VOID
UdfsCacheAddToDirtyList(
    IN PUDFS_CACHE Cache,
    IN PUDFS_CACHE_ENTRY Entry
    )
{
    // Don't add if already in dirty list
    if (Entry->Flags & UDFS_CACHE_MODIFIED) {
        return;
    }
    
    Entry->Flags |= UDFS_CACHE_MODIFIED;
    Entry->DirtyNext = NULL;
    Entry->DirtyPrev = Cache->DirtyListTail;
    
    if (Cache->DirtyListTail) {
        Cache->DirtyListTail->DirtyNext = Entry;
    } else {
        Cache->DirtyListHead = Entry;
    }
    
    Cache->DirtyListTail = Entry;
    Cache->DirtyCount++;
    Cache->WriteCount++;
}

// Remove entry from dirty list
VOID
UdfsCacheRemoveFromDirtyList(
    IN PUDFS_CACHE Cache,
    IN PUDFS_CACHE_ENTRY Entry
    )
{
    if (!(Entry->Flags & UDFS_CACHE_MODIFIED)) {
        return;
    }
    
    if (Entry->DirtyPrev) {
        Entry->DirtyPrev->DirtyNext = Entry->DirtyNext;
    } else {
        Cache->DirtyListHead = Entry->DirtyNext;
    }
    
    if (Entry->DirtyNext) {
        Entry->DirtyNext->DirtyPrev = Entry->DirtyPrev;
    } else {
        Cache->DirtyListTail = Entry->DirtyPrev;
    }
    
    Entry->DirtyNext = NULL;
    Entry->DirtyPrev = NULL;
    Entry->Flags &= ~UDFS_CACHE_MODIFIED;
    
    if (Cache->DirtyCount > 0) {
        Cache->DirtyCount--;
    }
    if (Cache->WriteCount > 0) {
        Cache->WriteCount--;
    }
}

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
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;
    PCHAR CurrentBuffer = Buffer;
    SIZE_T TotalBytesRead = 0;
    
    if (!Cache || !Cache->Initialized || !Buffer || !ReadBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    *ReadBytes = 0;
    
    // Acquire shared lock
    ExAcquireResourceSharedLite(&Cache->CacheLock, TRUE);
    
    _SEH2_TRY {
        for (i = 0; i < BCount; i++) {
            lba_t CurrentLba = Lba + i;
            PUDFS_CACHE_ENTRY Entry;
            
            // Check bounds
            if (CurrentLba < Cache->FirstLba || CurrentLba > Cache->LastLba) {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            
            // Look in cache first
            Entry = UdfsCacheFindEntry(Cache, CurrentLba);
            if (Entry) {
                // Cache hit
                Cache->HitCount++;
                RtlCopyMemory(CurrentBuffer, Entry->Buffer, Cache->BlockSize);
                TotalBytesRead += Cache->BlockSize;
            } else {
                // Cache miss
                Cache->MissCount++;
                
                if (CachedOnly) {
                    // Only read from cache, skip missing blocks
                    RtlZeroMemory(CurrentBuffer, Cache->BlockSize);
                    TotalBytesRead += Cache->BlockSize;
                } else {
                    // Read from disk and cache it
                    SIZE_T BytesRead = 0;
                    
                    // Release cache lock temporarily for disk I/O
                    ExReleaseResourceLite(&Cache->CacheLock);
                    
                    Status = Cache->ReadProc(IrpContext, Context, CurrentBuffer, 
                                           Cache->BlockSize, CurrentLba, &BytesRead, 0);
                    
                    // Re-acquire lock
                    ExAcquireResourceSharedLite(&Cache->CacheLock, TRUE);
                    
                    if (NT_SUCCESS(Status) && BytesRead == Cache->BlockSize) {
                        TotalBytesRead += BytesRead;
                        
                        // Try to cache the block
                        PUDFS_CACHE_ENTRY NewEntry = UdfsCacheFindLRUEntry(Cache);
                        if (NewEntry) {
                            // Remove old entry if it was in use
                            if (NewEntry->Flags & UDFS_CACHE_VALID) {
                                UdfsCacheRemoveEntry(Cache, NewEntry);
                            }
                            
                            // Add new entry
                            RtlCopyMemory(NewEntry->Buffer, CurrentBuffer, Cache->BlockSize);
                            UdfsCacheAddEntry(Cache, NewEntry, CurrentLba);
                        }
                    } else {
                        break;
                    }
                }
            }
            
            CurrentBuffer += Cache->BlockSize;
        }
        
        *ReadBytes = TotalBytesRead;
        
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        Status = STATUS_INVALID_USER_BUFFER;
    } _SEH2_END;
    
    ExReleaseResourceLite(&Cache->CacheLock);
    return Status;
}

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
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;
    PCHAR CurrentBuffer = Buffer;
    SIZE_T TotalBytesWritten = 0;
    
    if (!Cache || !Cache->Initialized || !Buffer || !WrittenBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    *WrittenBytes = 0;
    
    // Read-only cache mode
    if (Cache->Mode == UDFS_CACHE_MODE_RO) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    // Acquire exclusive lock
    ExAcquireResourceExclusiveLite(&Cache->CacheLock, TRUE);
    
    _SEH2_TRY {
        
        // Check for sequential write pattern and use optimized path
        if (BCount > UDFS_CACHE_MIN_BATCH_SIZE && 
            (UdfsCacheIsSequentialWrite(Cache, Lba) || Cache->InSequentialMode)) {
            
            // Update sequential state
            for (i = 0; i < BCount; i++) {
                UdfsCacheUpdateSequentialState(Cache, Lba + i);
            }
            
            // Use optimized sequential write path
            Status = UdfsCacheOptimizedSequentialWrite(
                IrpContext, Cache, Context, Buffer, Lba, BCount, WrittenBytes);
            
        } else {
            // Regular write path for random I/O
            for (i = 0; i < BCount; i++) {
                lba_t CurrentLba = Lba + i;
                PUDFS_CACHE_ENTRY Entry;
                
                // Update sequential write tracking
                UdfsCacheUpdateSequentialState(Cache, CurrentLba);
                
                // Check bounds
                if (CurrentLba < Cache->FirstLba || CurrentLba > Cache->LastLba) {
                    Status = STATUS_INVALID_PARAMETER;
                    break;
                }
                
                // Look in cache first
                Entry = UdfsCacheFindEntry(Cache, CurrentLba);
                if (!Entry && !CachedOnly) {
                    // Not in cache, get a new entry
                    Entry = UdfsCacheFindLRUEntry(Cache);
                    if (Entry) {
                        // Remove old entry if it was in use
                        if (Entry->Flags & UDFS_CACHE_VALID) {
                            UdfsCacheRemoveEntry(Cache, Entry);
                        }
                        UdfsCacheAddEntry(Cache, Entry, CurrentLba);
                    }
                }
                
                if (Entry) {
                    // Update cache entry
                    RtlCopyMemory(Entry->Buffer, CurrentBuffer, Cache->BlockSize);
                    
                    // Add to dirty list for write-back caching
                    if (!(Entry->Flags & UDFS_CACHE_MODIFIED)) {
                        UdfsCacheAddToDirtyList(Cache, Entry);
                    }
                    
                    TotalBytesWritten += Cache->BlockSize;
                }
                
                CurrentBuffer += Cache->BlockSize;
            }
            
            *WrittenBytes = TotalBytesWritten;
        }

#ifdef UDF_CACHE_USE_WRITE_BACK
        // Write-back strategy: Only flush if we need to
        if (!CachedOnly && UdfsCacheShouldFlush(Cache)) {
            // Release cache lock temporarily for batch I/O
            ExReleaseResourceLite(&Cache->CacheLock);
            
            NTSTATUS FlushStatus = UdfsCacheFlushDirtyBlocks(
                IrpContext, Cache, Context, FALSE);
            
            // Re-acquire lock
            ExAcquireResourceExclusiveLite(&Cache->CacheLock, TRUE);
            
            // If batch flush fails, we still report success for the write
            // as the data is safely cached and will be retried later
        }
#else
        // Write through to disk if not cached only (original behavior)
        if (!CachedOnly && TotalBytesWritten > 0) {
            // This would be the old write-through implementation
            // For now, we always use write-back for better performance
        }
#endif
        
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        Status = STATUS_INVALID_USER_BUFFER;
    } _SEH2_END;
    
    ExReleaseResourceLite(&Cache->CacheLock);
    return Status;
}

// Flush blocks
NTSTATUS 
UdfsCacheFlushBlocks(
    IN PIRP_CONTEXT IrpContext,
    IN PUDFS_CACHE Cache,
    IN PVOID Context,
    IN lba_t Lba,
    IN ULONG BCount
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;
    
    if (!Cache || !Cache->Initialized) {
        return STATUS_INVALID_PARAMETER;
    }
    
    ExAcquireResourceSharedLite(&Cache->CacheLock, TRUE);
    
    for (i = 0; i < BCount; i++) {
        lba_t CurrentLba = Lba + i;
        PUDFS_CACHE_ENTRY Entry = UdfsCacheFindEntry(Cache, CurrentLba);
        
        if (Entry && (Entry->Flags & UDFS_CACHE_MODIFIED)) {
            SIZE_T BytesWritten = 0;
            
            Entry->Flags |= UDFS_CACHE_FLUSHING;
            
            // Release lock for I/O
            ExReleaseResourceLite(&Cache->CacheLock);
            
            Status = Cache->WriteProc(IrpContext, Context, Entry->Buffer,
                                    Cache->BlockSize, CurrentLba, &BytesWritten, 0);
            
            // Re-acquire lock
            ExAcquireResourceSharedLite(&Cache->CacheLock, TRUE);
            
            Entry->Flags &= ~UDFS_CACHE_FLUSHING;
            
            if (NT_SUCCESS(Status) && BytesWritten == Cache->BlockSize) {
                UdfsCacheRemoveFromDirtyList(Cache, Entry);
            } else {
                break;
            }
        }
    }
    
    ExReleaseResourceLite(&Cache->CacheLock);
    return Status;
}

// Discard blocks
VOID
UdfsCacheDiscardBlocks(
    IN PUDFS_CACHE Cache,
    IN lba_t Lba,
    IN ULONG BCount
    )
{
    ULONG i;
    
    if (!Cache || !Cache->Initialized) {
        return;
    }
    
    ExAcquireResourceExclusiveLite(&Cache->CacheLock, TRUE);
    
    for (i = 0; i < BCount; i++) {
        lba_t CurrentLba = Lba + i;
        PUDFS_CACHE_ENTRY Entry = UdfsCacheFindEntry(Cache, CurrentLba);
        
        if (Entry) {
            // Remove from dirty list if modified
            if (Entry->Flags & UDFS_CACHE_MODIFIED) {
                UdfsCacheRemoveFromDirtyList(Cache, Entry);
            }
            
            UdfsCacheRemoveEntry(Cache, Entry);
        }
    }
    
    ExReleaseResourceLite(&Cache->CacheLock);
}

// Flush all
VOID
UdfsCacheFlushAll(
    IN PIRP_CONTEXT IrpContext,
    IN PUDFS_CACHE Cache,
    IN PVOID Context
    )
{
    if (!Cache || !Cache->Initialized) {
        return;
    }
    
    ExAcquireResourceSharedLite(&Cache->CacheLock, TRUE);
    
    // Use optimized batch flushing
    if (Cache->DirtyCount > 0) {
        ExReleaseResourceLite(&Cache->CacheLock);
        
        UdfsCacheFlushDirtyBlocks(IrpContext, Cache, Context, TRUE);
        
        ExAcquireResourceSharedLite(&Cache->CacheLock, TRUE);
    }
    
    ExReleaseResourceLite(&Cache->CacheLock);
}

// Purge all
VOID
UdfsCachePurgeAll(
    IN PUDFS_CACHE Cache
    )
{
    ULONG i;
    
    if (!Cache || !Cache->Initialized) {
        return;
    }
    
    ExAcquireResourceExclusiveLite(&Cache->CacheLock, TRUE);
    
    // Clear hash table
    for (i = 0; i < Cache->HashSize; i++) {
        Cache->HashTable[i] = NULL;
    }
    
    // Clear all entries and dirty list
    for (i = 0; i < Cache->MaxEntries; i++) {
        Cache->EntryPool[i].Flags = 0;
        Cache->EntryPool[i].Next = NULL;
        Cache->EntryPool[i].DirtyNext = NULL;
        Cache->EntryPool[i].DirtyPrev = NULL;
    }
    
    Cache->CurrentEntries = 0;
    Cache->WriteCount = 0;
    Cache->DirtyCount = 0;
    Cache->DirtyListHead = NULL;
    Cache->DirtyListTail = NULL;
    
    ExReleaseResourceLite(&Cache->CacheLock);
}

// Release cache
VOID
UdfsCacheRelease(
    IN PUDFS_CACHE Cache
    )
{
    if (!Cache || !Cache->Initialized) {
        return;
    }
    
    Cache->Initialized = FALSE;
    
    ExDeleteResourceLite(&Cache->CacheLock);
    
    if (Cache->BufferPool) {
        MyFreePool__(Cache->BufferPool);
        Cache->BufferPool = NULL;
    }
    
    if (Cache->EntryPool) {
        MyFreePool__(Cache->EntryPool);
        Cache->EntryPool = NULL;
    }
    
    if (Cache->HashTable) {
        MyFreePool__(Cache->HashTable);
        Cache->HashTable = NULL;
    }
    
    RtlZeroMemory(Cache, sizeof(UDFS_CACHE));
}

// Check if cache needs flushing
BOOLEAN
UdfsCacheShouldFlush(
    IN PUDFS_CACHE Cache
    )
{
    LARGE_INTEGER CurrentTime;
    LARGE_INTEGER TimeDiff;
    
    if (!Cache || !Cache->Initialized) {
        return FALSE;
    }
    
    // Check dirty count threshold
    if (Cache->DirtyCount >= Cache->MaxDirtyCount) {
        return TRUE;
    }
    
    // Check time-based flush interval
    if (Cache->DirtyCount > 0) {
        UdfsCacheGetTime(&CurrentTime);
        TimeDiff.QuadPart = CurrentTime.QuadPart - Cache->LastFlushTime.QuadPart;
        
        // Convert to milliseconds (assuming 100ns units)
        if (TimeDiff.QuadPart > (Cache->FlushInterval * 10000LL)) {
            return TRUE;
        }
    }
    
    return FALSE;
}

// Update flush statistics
VOID
UdfsCacheUpdateFlushStats(
    IN PUDFS_CACHE Cache
    )
{
    if (Cache) {
        Cache->FlushCount++;
        UdfsCacheGetTime(&Cache->LastFlushTime);
    }
}

// Check if initialized
BOOLEAN
UdfsCacheIsInitialized(
    IN PUDFS_CACHE Cache
    )
{
    return (Cache && Cache->Initialized && Cache->Tag == 'hcDU');
}

// Check if current write is sequential
BOOLEAN
UdfsCacheIsSequentialWrite(
    IN PUDFS_CACHE Cache,
    IN lba_t Lba
    )
{
    if (!Cache || Cache->LastWriteLba == (lba_t)-1) {
        return FALSE;
    }
    
    // Check if current LBA is adjacent to last write
    return (Lba == Cache->LastWriteLba + 1);
}

// Update sequential write state
VOID
UdfsCacheUpdateSequentialState(
    IN PUDFS_CACHE Cache,
    IN lba_t Lba
    )
{
    if (!Cache) {
        return;
    }
    
    if (UdfsCacheIsSequentialWrite(Cache, Lba)) {
        Cache->SequentialCount++;
        if (Cache->SequentialCount >= UDFS_CACHE_SEQUENTIAL_THRESHOLD) {
            Cache->InSequentialMode = TRUE;
        }
    } else {
        Cache->SequentialCount = 1;
        Cache->InSequentialMode = FALSE;
    }
    
    Cache->LastWriteLba = Lba;
}

// Optimized sequential write implementation
NTSTATUS
UdfsCacheOptimizedSequentialWrite(
    IN PIRP_CONTEXT IrpContext,
    IN PUDFS_CACHE Cache,
    IN PVOID Context,
    IN PCHAR Buffer,
    IN lba_t Lba,
    IN ULONG BCount,
    OUT PSIZE_T WrittenBytes
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;
    PCHAR CurrentBuffer = Buffer;
    SIZE_T TotalBytesWritten = 0;
    PUDFS_CACHE_ENTRY BatchEntries[UDFS_CACHE_BATCH_SIZE];
    ULONG BatchCount = 0;
    
    if (!Cache || !Cache->Initialized || !Buffer || !WrittenBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    *WrittenBytes = 0;
    
    // For sequential writes, we can be more aggressive with batching
    // and less aggressive with immediate flushing
    for (i = 0; i < BCount; i++) {
        lba_t CurrentLba = Lba + i;
        PUDFS_CACHE_ENTRY Entry;
        
        // Check bounds
        if (CurrentLba < Cache->FirstLba || CurrentLba > Cache->LastLba) {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        
        // Look in cache first
        Entry = UdfsCacheFindEntry(Cache, CurrentLba);
        if (!Entry) {
            // Get a new entry
            Entry = UdfsCacheFindLRUEntry(Cache);
            if (Entry) {
                // Remove old entry if it was in use
                if (Entry->Flags & UDFS_CACHE_VALID) {
                    UdfsCacheRemoveEntry(Cache, Entry);
                }
                UdfsCacheAddEntry(Cache, Entry, CurrentLba);
            }
        }
        
        if (Entry) {
            // Update cache entry
            RtlCopyMemory(Entry->Buffer, CurrentBuffer, Cache->BlockSize);
            
            // Add to dirty list for write-back caching
            if (!(Entry->Flags & UDFS_CACHE_MODIFIED)) {
                UdfsCacheAddToDirtyList(Cache, Entry);
            }
            
            // Collect for batch processing
            BatchEntries[BatchCount++] = Entry;
            TotalBytesWritten += Cache->BlockSize;
            
            // Process batch when full
            if (BatchCount >= UDFS_CACHE_BATCH_SIZE) {
                // For sequential writes, we can defer flushing longer
                // unless we're approaching the dirty threshold
                if (Cache->DirtyCount >= (Cache->MaxDirtyCount * 3 / 4)) {
                    NTSTATUS BatchStatus = UdfsCacheBatchFlushBlocks(
                        IrpContext, Cache, Context, BatchEntries, BatchCount);
                    
                    if (!NT_SUCCESS(BatchStatus) && NT_SUCCESS(Status)) {
                        Status = BatchStatus;
                    }
                }
                BatchCount = 0;
            }
        }
        
        CurrentBuffer += Cache->BlockSize;
    }
    
    *WrittenBytes = TotalBytesWritten;
    return Status;
}

// Batch flush multiple dirty blocks efficiently
NTSTATUS
UdfsCacheBatchFlushBlocks(
    IN PIRP_CONTEXT IrpContext,
    IN PUDFS_CACHE Cache,
    IN PVOID Context,
    IN PUDFS_CACHE_ENTRY* Entries,
    IN ULONG EntryCount
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i, j;
    SIZE_T BytesWritten;
    
    if (!Cache || !Cache->Initialized || !Entries || EntryCount == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Sort entries by LBA for sequential writes when possible
    // Simple bubble sort for small arrays (usually < 32 entries)
    for (i = 0; i < EntryCount - 1; i++) {
        for (j = 0; j < EntryCount - i - 1; j++) {
            if (Entries[j]->Lba > Entries[j + 1]->Lba) {
                PUDFS_CACHE_ENTRY temp = Entries[j];
                Entries[j] = Entries[j + 1];
                Entries[j + 1] = temp;
            }
        }
    }
    
    // Try to coalesce adjacent blocks for more efficient I/O
    i = 0;
    while (i < EntryCount) {
        lba_t StartLba = Entries[i]->Lba;
        ULONG CoalesceCount = 1;
        
        // Find contiguous blocks
        while (i + CoalesceCount < EntryCount && 
               Entries[i + CoalesceCount]->Lba == StartLba + CoalesceCount &&
               CoalesceCount < UDFS_CACHE_MAX_COALESCE_DISTANCE) {
            CoalesceCount++;
        }
        
        if (CoalesceCount > 1) {
            // Write coalesced blocks together
            SIZE_T CoalescedSize = CoalesceCount * Cache->BlockSize;
            PCHAR CoalescedBuffer = (PCHAR)MyAllocatePoolTag__(
                NonPagedPool, CoalescedSize, MEM_UDBUFFER_TAG);
            
            if (CoalescedBuffer) {
                // Copy blocks into contiguous buffer
                for (j = 0; j < CoalesceCount; j++) {
                    PUDFS_CACHE_ENTRY Entry = Entries[i + j];
                    
                    if (Entry->Flags & UDFS_CACHE_FLUSHING) {
                        continue; // Skip entries already being flushed
                    }
                    
                    Entry->Flags |= UDFS_CACHE_FLUSHING;
                    RtlCopyMemory(CoalescedBuffer + (j * Cache->BlockSize), 
                                Entry->Buffer, Cache->BlockSize);
                }
                
                // Write coalesced buffer
                Status = Cache->WriteProc(IrpContext, Context, CoalescedBuffer,
                                        CoalescedSize, StartLba, &BytesWritten, 0);
                
                // Update entry states
                for (j = 0; j < CoalesceCount; j++) {
                    PUDFS_CACHE_ENTRY Entry = Entries[i + j];
                    Entry->Flags &= ~UDFS_CACHE_FLUSHING;
                    
                    if (NT_SUCCESS(Status) && BytesWritten >= (j + 1) * Cache->BlockSize) {
                        UdfsCacheRemoveFromDirtyList(Cache, Entry);
                    }
                }
                
                MyFreePool__(CoalescedBuffer);
                i += CoalesceCount;
            } else {
                // Fall back to individual writes if allocation fails
                PUDFS_CACHE_ENTRY Entry = Entries[i];
                
                if (!(Entry->Flags & UDFS_CACHE_FLUSHING)) {
                    Entry->Flags |= UDFS_CACHE_FLUSHING;
                    
                    Status = Cache->WriteProc(IrpContext, Context, Entry->Buffer,
                                            Cache->BlockSize, Entry->Lba, &BytesWritten, 0);
                    
                    Entry->Flags &= ~UDFS_CACHE_FLUSHING;
                    
                    if (NT_SUCCESS(Status) && BytesWritten == Cache->BlockSize) {
                        UdfsCacheRemoveFromDirtyList(Cache, Entry);
                    }
                }
                i++;
            }
        } else {
            // Single block write
            PUDFS_CACHE_ENTRY Entry = Entries[i];
            
            if (!(Entry->Flags & UDFS_CACHE_FLUSHING)) {
                Entry->Flags |= UDFS_CACHE_FLUSHING;
                
                Status = Cache->WriteProc(IrpContext, Context, Entry->Buffer,
                                        Cache->BlockSize, Entry->Lba, &BytesWritten, 0);
                
                Entry->Flags &= ~UDFS_CACHE_FLUSHING;
                
                if (NT_SUCCESS(Status) && BytesWritten == Cache->BlockSize) {
                    UdfsCacheRemoveFromDirtyList(Cache, Entry);
                } else {
                    // If one write fails, we should still try the others
                    // but remember the failure
                    if (NT_SUCCESS(Status)) {
                        Status = STATUS_UNSUCCESSFUL;
                    }
                }
            }
            i++;
        }
    }
    
    Cache->BatchCount++;
    UdfsCacheUpdateFlushStats(Cache);
    
    return Status;
}

// Flush dirty blocks using optimal strategy
NTSTATUS
UdfsCacheFlushDirtyBlocks(
    IN PIRP_CONTEXT IrpContext,
    IN PUDFS_CACHE Cache,
    IN PVOID Context,
    IN BOOLEAN ForceFlush
    )
{
    NTSTATUS Status = STATUS_SUCCESS;
    PUDFS_CACHE_ENTRY BatchEntries[UDFS_CACHE_BATCH_SIZE];
    ULONG BatchCount = 0;
    PUDFS_CACHE_ENTRY Entry;
    
    if (!Cache || !Cache->Initialized) {
        return STATUS_INVALID_PARAMETER;
    }
    
    if (!ForceFlush && !UdfsCacheShouldFlush(Cache)) {
        return STATUS_SUCCESS;
    }
    
    // Collect dirty entries for batch processing
    Entry = Cache->DirtyListHead;
    while (Entry && Cache->DirtyCount > 0) {
        PUDFS_CACHE_ENTRY NextEntry = Entry->DirtyNext;
        
        if (Entry->Flags & UDFS_CACHE_MODIFIED) {
            BatchEntries[BatchCount++] = Entry;
            
            // Process batch when full or at end
            if (BatchCount >= UDFS_CACHE_BATCH_SIZE || NextEntry == NULL) {
                NTSTATUS BatchStatus = UdfsCacheBatchFlushBlocks(
                    IrpContext, Cache, Context, BatchEntries, BatchCount);
                
                if (!NT_SUCCESS(BatchStatus) && NT_SUCCESS(Status)) {
                    Status = BatchStatus;
                }
                
                BatchCount = 0;
            }
        }
        
        Entry = NextEntry;
    }
    
    return Status;
}