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
            if (PrevEntry) {
                PrevEntry->Next = Entry->Next;
            } else {
                Cache->HashTable[Hash] = Entry->Next;
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
                if (!(Entry->Flags & UDFS_CACHE_MODIFIED)) {
                    Cache->WriteCount++;
                }
                Entry->Flags |= UDFS_CACHE_MODIFIED;
                
                TotalBytesWritten += Cache->BlockSize;
            }
            
            // Write through to disk if not cached only
            if (!CachedOnly) {
                SIZE_T BytesWritten = 0;
                
                // Release cache lock temporarily for disk I/O
                ExReleaseResourceLite(&Cache->CacheLock);
                
                Status = Cache->WriteProc(IrpContext, Context, CurrentBuffer,
                                        Cache->BlockSize, CurrentLba, &BytesWritten, 0);
                
                // Re-acquire lock
                ExAcquireResourceExclusiveLite(&Cache->CacheLock, TRUE);
                
                if (!NT_SUCCESS(Status) || BytesWritten != Cache->BlockSize) {
                    break;
                }
            }
            
            CurrentBuffer += Cache->BlockSize;
        }
        
        *WrittenBytes = TotalBytesWritten;
        
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
            
            // Release lock for I/O
            ExReleaseResourceLite(&Cache->CacheLock);
            
            Status = Cache->WriteProc(IrpContext, Context, Entry->Buffer,
                                    Cache->BlockSize, CurrentLba, &BytesWritten, 0);
            
            // Re-acquire lock
            ExAcquireResourceSharedLite(&Cache->CacheLock, TRUE);
            
            if (NT_SUCCESS(Status) && BytesWritten == Cache->BlockSize) {
                Entry->Flags &= ~UDFS_CACHE_MODIFIED;
                if (Cache->WriteCount > 0) {
                    Cache->WriteCount--;
                }
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
            UdfsCacheRemoveEntry(Cache, Entry);
            if (Entry->Flags & UDFS_CACHE_MODIFIED) {
                if (Cache->WriteCount > 0) {
                    Cache->WriteCount--;
                }
            }
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
    ULONG i;
    
    if (!Cache || !Cache->Initialized) {
        return;
    }
    
    ExAcquireResourceSharedLite(&Cache->CacheLock, TRUE);
    
    // Flush all modified entries
    for (i = 0; i < Cache->MaxEntries; i++) {
        PUDFS_CACHE_ENTRY Entry = &Cache->EntryPool[i];
        
        if ((Entry->Flags & UDFS_CACHE_VALID) && (Entry->Flags & UDFS_CACHE_MODIFIED)) {
            SIZE_T BytesWritten = 0;
            
            // Release lock for I/O
            ExReleaseResourceLite(&Cache->CacheLock);
            
            NTSTATUS Status = Cache->WriteProc(IrpContext, Context, Entry->Buffer,
                                             Cache->BlockSize, Entry->Lba, &BytesWritten, 0);
            
            // Re-acquire lock
            ExAcquireResourceSharedLite(&Cache->CacheLock, TRUE);
            
            if (NT_SUCCESS(Status) && BytesWritten == Cache->BlockSize) {
                Entry->Flags &= ~UDFS_CACHE_MODIFIED;
                if (Cache->WriteCount > 0) {
                    Cache->WriteCount--;
                }
            }
        }
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
    
    // Clear all entries
    for (i = 0; i < Cache->MaxEntries; i++) {
        Cache->EntryPool[i].Flags = 0;
        Cache->EntryPool[i].Next = NULL;
    }
    
    Cache->CurrentEntries = 0;
    Cache->WriteCount = 0;
    
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

// Check if initialized
BOOLEAN
UdfsCacheIsInitialized(
    IN PUDFS_CACHE Cache
    )
{
    return (Cache && Cache->Initialized && Cache->Tag == 'hcDU');
}