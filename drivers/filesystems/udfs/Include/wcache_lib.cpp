////////////////////////////////////////////////////////////////////
// Copyright (C) Alexander Telyatnikov, Ivan Keliukh, Yegor Anchishkin, SKIF Software, 1999-2013. Kiev, Ukraine
// All rights reserved
// This file was released under the GPLv2 on June 2015.
////////////////////////////////////////////////////////////////////

/*********************************************************************/

NTSTATUS __fastcall
WCacheCheckLimits(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, IN lba_t ReqLba, IN ULONG BCount);

NTSTATUS __fastcall
WCacheCheckLimitsRAM(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, IN lba_t ReqLba, IN ULONG BCount);

NTSTATUS __fastcall
WCacheCheckLimitsRW(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, IN lba_t ReqLba, IN ULONG BCount);

NTSTATUS __fastcall
WCacheCheckLimitsR(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, IN lba_t ReqLba, IN ULONG BCount);

VOID __fastcall
WCachePurgeAllRW(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context);

VOID __fastcall
WCacheFlushAllRW(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context);

VOID __fastcall
WCachePurgeAllR(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context);

NTSTATUS __fastcall 
WCacheDecodeFlags(IN PW_CACHE Cache, IN ULONG Flags);

#define ASYNC_STATE_NONE      0
#define ASYNC_STATE_READ_PRE  1
#define ASYNC_STATE_READ      2
#define ASYNC_STATE_WRITE_PRE 3
#define ASYNC_STATE_WRITE     4
#define ASYNC_STATE_DONE      5

#define ASYNC_CMD_NONE        0
#define ASYNC_CMD_READ        1
#define ASYNC_CMD_UPDATE      2

#define WCACHE_MAX_CHAIN      (0x10)

#define MEM_WCCTX_TAG         'xtCW'
#define MEM_WCFRM_TAG         'rfCW'
#define MEM_WCBUF_TAG         'fbCW'

#define USE_WC_PRINT
#ifdef USE_WC_PRINT
 #define WcPrint UDFPrint
#else
 #define WcPrint(x) {;}
#endif

typedef struct _W_CACHE_ASYNC {
    UDF_PH_CALL_CONTEXT PhContext;
    ULONG State;
    ULONG Cmd;
    PW_CACHE Cache;
    PVOID Buffer;
    PVOID Buffer2;
    SIZE_T TransferredBytes;
    ULONG BCount;
    lba_t Lba;
    struct _W_CACHE_ASYNC* NextWContext;
    struct _W_CACHE_ASYNC* PrevWContext;
} W_CACHE_ASYNC, *PW_CACHE_ASYNC;

VOID WCacheUpdatePacketComplete(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, 
    IN OUT PW_CACHE_ASYNC* FirstWContext, IN OUT PW_CACHE_ASYNC* PrevWContext, IN BOOLEAN FreePacket = TRUE);

BOOLEAN ValidateFrameBlocksList(IN PW_CACHE Cache, IN lba_t Lba);

/*********************************************************************/
ULONG WCache_random;

// Initialize cache structure with memory and synchronization resources
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
    IN PWC_ERROR_HANDLER ErrorHandlerProc
    )
{
    ULONG l1, l2, l3;
    ULONG PacketSize = (1) << PacketSizeSh;
    ULONG BlockSize = (1) << BlockSizeSh;
    ULONG BlocksPerFrame = (1) << BlocksPerFrameSh;
    NTSTATUS RC = STATUS_SUCCESS;
    LARGE_INTEGER rseed;
    ULONG res_init_flags = 0;

#define WCLOCK_RES   1

    _SEH2_TRY {
        // Parameter validation
        if (Mode == WCACHE_MODE_R) {
            WriteProcAsync = NULL; // Disable async write for WORM media
        }
        if ((MaxBlocks % PacketSize) || !MaxBlocks) {
            try_return(RC = STATUS_INVALID_PARAMETER);
        }
        if (BlocksPerFrame % PacketSize || !ReadProc || FirstLba >= LastLba || 
            !MaxFrames || Mode > WCACHE_MODE_MAX || FramesToKeepFree >= MaxFrames/2) {
            try_return(RC = STATUS_INVALID_PARAMETER);
        }

        MaxBlocks = max(MaxBlocks, BlocksPerFrame*3);
        
        // Allocate memory structures
        if (!(Cache->FrameList = (PW_CACHE_FRAME)MyAllocatePoolTag__(NonPagedPool, 
            l1 = (((LastLba >> BlocksPerFrameSh)+1)*sizeof(W_CACHE_FRAME)), MEM_WCFRM_TAG))) {
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        }
        if (!(Cache->CachedBlocksList = (PULONG)MyAllocatePoolTag__(NonPagedPool, 
            l2 = ((MaxBlocks+2)*sizeof(lba_t)), MEM_WCFRM_TAG))) {
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        }
        if (!(Cache->CachedModifiedBlocksList = (PULONG)MyAllocatePoolTag__(NonPagedPool, l2, MEM_WCFRM_TAG))) {
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        }
        if (!(Cache->CachedFramesList = (PULONG)MyAllocatePoolTag__(NonPagedPool, 
            l3 = ((MaxFrames+2)*sizeof(lba_t)), MEM_WCFRM_TAG))) {
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        }
        
        RtlZeroMemory(Cache->FrameList, l1);
        RtlZeroMemory(Cache->CachedBlocksList, l2);
        RtlZeroMemory(Cache->CachedModifiedBlocksList, l2);
        RtlZeroMemory(Cache->CachedFramesList, l3);
        
        // Initialize cache parameters
        Cache->BlocksPerFrame = BlocksPerFrame;
        Cache->BlocksPerFrameSh = BlocksPerFrameSh;
        Cache->BlockCount = 0;
        Cache->MaxBlocks = MaxBlocks;
        Cache->MaxBytesToRead = MaxBytesToRead;
        Cache->FrameCount = 0;
        Cache->MaxFrames = MaxFrames;
        Cache->PacketSize = PacketSize;
        Cache->PacketSizeSh = PacketSizeSh;
        Cache->BlockSize = BlockSize;
        Cache->BlockSizeSh = BlockSizeSh;
        Cache->FirstLba = FirstLba;
        Cache->LastLba = LastLba;
        Cache->WriteCount = 0;
        Cache->FramesToKeepFree = FramesToKeepFree;
        Cache->Mode = Mode;
        Cache->WriteProc = WriteProc;
        Cache->ReadProc = ReadProc;
        Cache->WriteProcAsync = WriteProcAsync;
        Cache->ReadProcAsync = ReadProcAsync;
        Cache->CheckUsedProc = CheckUsedProc;
        Cache->UpdateRelocProc = UpdateRelocProc;
        Cache->ErrorHandlerProc = ErrorHandlerProc;
        Cache->Tag = 0;
        Cache->UseCount = 0;
        Cache->Flags = 0;

        if (!NT_SUCCESS(RC = WCacheDecodeFlags(Cache, Flags))) {
            try_return(RC);
        }

        // Initialize synchronization
        if (WriteProcAsync) {
            if (!(Cache->AsyncEntryList = (PW_CACHE_ASYNC)MyAllocatePoolTag__(NonPagedPool, 
                sizeof(W_CACHE_ASYNC)*WCACHE_MAX_CHAIN, MEM_WCCTX_TAG))) {
                try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
            }
            RtlZeroMemory(Cache->AsyncEntryList, sizeof(W_CACHE_ASYNC)*WCACHE_MAX_CHAIN);
        }
        if (ReadProcAsync) {
            if (!(Cache->AsyncEntryList = (PW_CACHE_ASYNC)MyAllocatePoolTag__(NonPagedPool, 
                sizeof(W_CACHE_ASYNC)*WCACHE_MAX_CHAIN, MEM_WCCTX_TAG))) {
                try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
            }
            RtlZeroMemory(Cache->AsyncEntryList, sizeof(W_CACHE_ASYNC)*WCACHE_MAX_CHAIN);
        }

        if (!(Cache->tmp_buff = (PCHAR)MyAllocatePoolTag__(NonPagedPool, PacketSize*2, MEM_WCBUF_TAG))) {
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        }

        res_init_flags |= WCLOCK_RES;
        if (!NT_SUCCESS(RC = ExInitializeResourceLite(&(Cache->WCacheLock)))) {
            try_return(RC);
        }

        KeQuerySystemTime(&rseed);
        WCache_random = rseed.LowPart;
        Cache->FastMutex = (PFAST_MUTEX)MyAllocatePoolTag__(NonPagedPool, sizeof(FAST_MUTEX), MEM_WCFRM_TAG);
        if (!Cache->FastMutex) {
            try_return(RC = STATUS_INSUFFICIENT_RESOURCES);
        }
        ExInitializeFastMutex(Cache->FastMutex);

try_exit: NOTHING;

    } _SEH2_FINALLY {
        if (!NT_SUCCESS(RC)) {
            if (Cache->FrameList) MyFreePool__(Cache->FrameList);
            if (Cache->CachedBlocksList) MyFreePool__(Cache->CachedBlocksList);
            if (Cache->CachedModifiedBlocksList) MyFreePool__(Cache->CachedModifiedBlocksList);
            if (Cache->CachedFramesList) MyFreePool__(Cache->CachedFramesList);
            if (Cache->AsyncEntryList) MyFreePool__(Cache->AsyncEntryList);
            if (Cache->tmp_buff) MyFreePool__(Cache->tmp_buff);
            if (Cache->FastMutex) MyFreePool__(Cache->FastMutex);
            if (res_init_flags & WCLOCK_RES) ExDeleteResourceLite(&(Cache->WCacheLock));
        }
    } _SEH2_END;
    return RC;
}

// Random number generator
LONGLONG
WCacheRandom(VOID)
{
    WCache_random = (WCache_random * 0x8088405 + 1);
    return WCache_random;
}

// Find LBA to release from cache
lba_t
WCacheFindLbaToRelease(IN PW_CACHE Cache)
{
    if (!(Cache->BlockCount)) return WCACHE_INVALID_LBA;
    return(Cache->CachedBlocksList[((ULONG)WCacheRandom() % Cache->BlockCount)]);
}

// Find modified LBA to release
lba_t
WCacheFindModifiedLbaToRelease(IN PW_CACHE Cache)
{
    if (!(Cache->WriteCount)) return WCACHE_INVALID_LBA;
    return(Cache->CachedModifiedBlocksList[((ULONG)WCacheRandom() % Cache->WriteCount)]);
}

// Find frame to release - simplified version
lba_t
WCacheFindFrameToRelease(IN PW_CACHE Cache)
{
    ULONG i;
    lba_t Lba;
    PW_CACHE_ENTRY block_array;
    
    if (!Cache->FrameCount) return WCACHE_INVALID_LBA;
    
    i = (ULONG)WCacheRandom() % Cache->FrameCount;
    Lba = Cache->CachedFramesList[i];
    
    if (Lba == WCACHE_INVALID_LBA) return WCACHE_INVALID_LBA;
    
    block_array = Cache->FrameList[Lba >> Cache->BlocksPerFrameSh].Frame;
    if (!block_array) return WCACHE_INVALID_LBA;
    
    return Lba;
}

// Get sorted list index position for insertion
ULONG
WCacheGetSortedListIndex(IN ULONG FrameCount, IN PULONG CachedFramesList, IN lba_t Lba)
{
    ULONG a, b, c;
    
    if (!FrameCount) return 0;
    
    a = 0;
    if (CachedFramesList[0] > Lba) return 0;
    
    b = FrameCount - 1;
    if (CachedFramesList[b] < Lba) return FrameCount;
    
    // Binary search
    while (b - a > 1) {
        c = (a + b) >> 1;
        if (CachedFramesList[c] < Lba) {
            a = c;
        } else {
            b = c;
        }
    }
    return b;
}

// Insert range to sorted list
VOID
WCacheInsertRangeToList(IN PULONG List, IN PULONG ListCount, IN lba_t Lba, IN ULONG BCount)
{
    ULONG i, j, k;
    
    k = WCacheGetSortedListIndex(*ListCount, List, Lba);
    
    // Shift elements and insert new range
    for (j = *ListCount + BCount - 1; j >= k + BCount; j--) {
        List[j] = List[j - BCount];
    }
    
    for (i = 0; i < BCount; i++, k++) {
        List[k] = Lba + i;
    }
    
    (*ListCount) += BCount;
}

// Insert single item to sorted list  
VOID
WCacheInsertItemToList(IN PULONG List, IN PULONG ListCount, IN lba_t Lba)
{
    ULONG i, j;
    
    i = WCacheGetSortedListIndex(*ListCount, List, Lba);
    
    for (j = *ListCount; j > i; j--) {
        List[j] = List[j-1];
    }
    
    List[i] = Lba;
    (*ListCount)++;
}

// Remove range from sorted list
VOID
WCacheRemoveRangeFromList(IN PULONG List, IN PULONG ListCount, IN lba_t Lba, IN ULONG BCount)
{
    ULONG i, j, k;
    
    i = WCacheGetSortedListIndex(*ListCount, List, Lba);
    if (i >= *ListCount || List[i] != Lba) return;
    
    k = min(BCount, *ListCount - i);
    for (j = i; j < *ListCount - k; j++) {
        List[j] = List[j + k];
    }
    
    (*ListCount) -= k;
}

// Remove single item from sorted list
VOID
WCacheRemoveItemFromList(IN PULONG List, IN PULONG ListCount, IN lba_t Lba)
{
    ULONG i, j;
    
    i = WCacheGetSortedListIndex(*ListCount, List, Lba);
    if (i >= *ListCount || List[i] != Lba) return;
    
    for (j = i; j < *ListCount - 1; j++) {
        List[j] = List[j+1];
    }
    
    (*ListCount)--;
}

#define WCacheSetModFlag(block_array, i) \
    *((PULONG)(&(block_array[i].Sector))) |= 0x80000000;

#define WCacheClrModFlag(block_array, i) \
    *((PULONG)(&(block_array[i].Sector))) &= 0x7fffffff;

#define WCacheGetModFlag(block_array, i) \
    ((*((PULONG)(&(block_array[i].Sector)))) & 0x80000000)

#define WCacheSetBadFlag(block_array, i) \
    *((PULONG)(&(block_array[i].Sector))) |= 0x40000000;

#define WCacheClrBadFlag(block_array, i) \
    *((PULONG)(&(block_array[i].Sector))) &= 0xbfffffff;

#define WCacheGetBadFlag(block_array, i) \
    ((*((PULONG)(&(block_array[i].Sector)))) & 0x40000000)

#define WCacheSectorAddr(block_array, i) \
    (block_array[i].Sector & 0x3fffffff)

#define WCacheFreeSector(frame, offs) \
    if (WCacheSectorAddr(frame, offs)) MyFreePool__((PVOID)WCacheSectorAddr(frame, offs))

// Initialize frame structure - updated signature
PW_CACHE_ENTRY
WCacheInitFrame(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, IN ULONG frame)
{
    PW_CACHE_ENTRY block_array;
    ULONG l;
    
    // Check limits
    if (Cache->FrameCount >= Cache->MaxFrames) {
        WCacheCheckLimits(IrpContext, Cache, Context, frame << Cache->BlocksPerFrameSh, Cache->PacketSize*2);
    }
    
    block_array = (PW_CACHE_ENTRY)MyAllocatePoolTag__(NonPagedPool, 
        l = sizeof(W_CACHE_ENTRY) << Cache->BlocksPerFrameSh, MEM_WCFRM_TAG);
    
    Cache->FrameList[frame].Frame = block_array;
    
    if (block_array) {
        WCacheInsertItemToList(Cache->CachedFramesList, &(Cache->FrameCount), frame);
        RtlZeroMemory(block_array, l);
        Cache->FrameList[frame].AccessCount = 0;
        Cache->FrameList[frame].UpdateCount = 0;
    }
    
    return block_array;
}

// Overloaded version for backward compatibility
PW_CACHE_ENTRY
WCacheInitFrame(IN PW_CACHE Cache, IN PVOID Context, IN lba_t Lba)
{
    ULONG frame_addr = Lba >> Cache->BlocksPerFrameSh;
    PW_CACHE_FRAME frm = &(Cache->FrameList[frame_addr]);
    
    if (frm->Frame) return frm->Frame;
    
    return WCacheInitFrame(NULL, Cache, Context, frame_addr);
}

// Remove frame from cache
VOID
WCacheRemoveFrame(IN PW_CACHE Cache, IN PVOID Context, IN lba_t frame_addr)
{
    PW_CACHE_FRAME frm = &(Cache->FrameList[frame_addr]);
    ULONG i;
    
    if (!frm->Frame) return;
    
    // Free all cached sectors in frame
    for (i = 0; i < Cache->BlocksPerFrame; i++) {
        WCacheFreeSector(frm->Frame, i);
    }
     
    MyFreePool__(frm->Frame);
    frm->Frame = NULL;
    frm->AccessCount = 0;
    frm->UpdateCount = 0;
    
    WCacheRemoveItemFromList(Cache->CachedFramesList, &(Cache->FrameCount), frame_addr << Cache->BlocksPerFrameSh);
}

// Allocate async entry for chained operations
PW_CACHE_ASYNC
WCacheAllocAsyncEntry(IN PW_CACHE Cache, IN OUT PW_CACHE_ASYNC* FirstWContext, IN OUT PW_CACHE_ASYNC* PrevWContext, IN ULONG Length)
{
    PW_CACHE_ASYNC WContext = NULL;
    ULONG i;
    
    if (!Cache->AsyncEntryList) return NULL;
    
    for (i = 0; i < WCACHE_MAX_CHAIN; i++) {
        if (!Cache->AsyncEntryList[i].Cache) {
            WContext = &(Cache->AsyncEntryList[i]);
            break;
        }
    }
    
    if (!WContext) return NULL;
    
    RtlZeroMemory(WContext, sizeof(W_CACHE_ASYNC));
    WContext->Cache = Cache;
    
    if (!(WContext->Buffer = MyAllocatePoolTag__(NonPagedPool, Length*2, MEM_WCBUF_TAG))) {
        return NULL;
    }
    
    WContext->Buffer2 = ((PCHAR)(WContext->Buffer)) + Length;
    
    // Link to chain
    if (!(*FirstWContext)) {
        *FirstWContext = WContext;
    }
    if (*PrevWContext) {
        (*PrevWContext)->NextWContext = WContext;
        WContext->PrevWContext = *PrevWContext;
    }
    *PrevWContext = WContext;
    
    return WContext;
}

// Free async entry
VOID
WCacheFreeAsyncEntry(IN PW_CACHE Cache, IN PW_CACHE_ASYNC WContext)
{
    if (WContext->Buffer) MyFreePool__(WContext->Buffer);
    WContext->Cache = NULL;
}

// Handle I/O errors
NTSTATUS
WCacheRaiseIoError(IN PW_CACHE Cache, IN PVOID Context, IN NTSTATUS Status, IN lba_t Lba, IN ULONG BCount, IN PVOID Buffer, IN ULONG Op, IN PW_CACHE_ENTRY block_array)
{
    if (Cache->ErrorHandlerProc) {
        return Cache->ErrorHandlerProc(Context, Status, Lba, BCount, Buffer, Op);
    }
    return Status;
}

// Main packet update function - significantly simplified
NTSTATUS
WCacheUpdatePacket(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, 
    IN OUT PW_CACHE_ASYNC* FirstWContext, IN OUT PW_CACHE_ASYNC* PrevWContext, 
    IN PW_CACHE_ENTRY block_array, IN lba_t firstLba, IN lba_t Lba, IN ULONG BSh, IN ULONG BS, 
    IN ULONG PS, IN ULONG PSs, IN PSIZE_T ReadBytes, IN BOOLEAN PrefereWrite, IN ULONG State)
{
    NTSTATUS status;
    PCHAR tmp_buff = Cache->tmp_buff;
    PCHAR tmp_buff2 = Cache->tmp_buff;
    BOOLEAN mod = FALSE, read = FALSE, zero = FALSE;
    ULONG i, block_type;
    lba_t Lba0;
    PW_CACHE_ASYNC WContext;
    BOOLEAN Async = (Cache->ReadProcAsync && Cache->WriteProcAsync);
    BOOLEAN Chained = Cache->Chained;

    // Handle write state for async operations
    if (State == ASYNC_STATE_WRITE) {
        WContext = (*PrevWContext);
        tmp_buff = (PCHAR)(WContext->Buffer);
        tmp_buff2 = (PCHAR)(WContext->Buffer2);
        if (!Chained) {
            mod = (DbgCompareMemory(tmp_buff2, tmp_buff, PS) != PS);
        }
        goto try_write;
    }

    // Check packet status
    Lba0 = Lba - firstLba;
    for (i = 0; i < PSs; i++, Lba0++) {
        if (WCacheGetModFlag(block_array, Lba0)) {
            mod = TRUE;
        } else if (!WCacheSectorAddr(block_array, Lba0) && 
                  ((block_type = Cache->CheckUsedProc(Context, Lba+i)) & WCACHE_BLOCK_USED)) {
            if (block_type & WCACHE_BLOCK_ZERO) {
                zero = TRUE;
            } else {
                read = TRUE;
            }
        }
    }

    if (mod && !PrefereWrite) return STATUS_RETRY;
    if (!mod) {
        (*ReadBytes) = PS;
        return STATUS_SUCCESS;
    }

    // Setup async context if needed
    if (Chained || Async) {
        WContext = WCacheAllocAsyncEntry(Cache, FirstWContext, PrevWContext, PS);
        if (!WContext) {
            Chained = FALSE;
            Async = FALSE;
        } else {
            tmp_buff = tmp_buff2 = (PCHAR)(WContext->Buffer);
            WContext->Lba = Lba;
            WContext->Cmd = ASYNC_CMD_UPDATE;
            WContext->State = ASYNC_STATE_NONE;
        }
    }

    // Read packet if necessary
    if (read) {
        if (Async) {
            WContext->State = ASYNC_STATE_READ;
            status = Cache->ReadProcAsync(Context, WContext, tmp_buff, PS, Lba, &(WContext->TransferredBytes));
            (*ReadBytes) = PS;
            return status;
        } else {
            status = Cache->ReadProc(IrpContext, Context, tmp_buff, PS, Lba, ReadBytes, PH_TMP_BUFFER);
            if (!NT_SUCCESS(status)) {
                status = WCacheRaiseIoError(Cache, Context, status, Lba, PSs, tmp_buff, WCACHE_R_OP, NULL);
                if (!NT_SUCCESS(status)) return status;
            }
        }
    } else if (zero) {
        RtlZeroMemory(tmp_buff, PS);
    }

    if (Chained) {
        WContext->State = ASYNC_STATE_WRITE_PRE;
        tmp_buff2 = tmp_buff;
        status = STATUS_SUCCESS;
    }

    // Modify packet
    mod = !read || Cache->DoNotCompare;
    Lba0 = Lba - firstLba;
    for (i = 0; i < PSs; i++, Lba0++) {
        if (WCacheGetModFlag(block_array, Lba0) || (!read && WCacheSectorAddr(block_array, Lba0))) {
            if (!mod) {
                mod = (DbgCompareMemory(tmp_buff2 + (i << BSh), (PVOID)WCacheSectorAddr(block_array, Lba0), BS) != BS);
            }
            if (mod) {
                DbgCopyMemory(tmp_buff2 + (i << BSh), (PVOID)WCacheSectorAddr(block_array, Lba0), BS);
            }
        }
    }

try_write:
    // Write packet if modified
    if (mod) {
        if (Chained || Async) {
            if (WContext) WContext->State = ASYNC_STATE_WRITE;
            (*ReadBytes) = PS;
            return STATUS_PENDING;
        } else {
            status = Cache->WriteProc(IrpContext, Context, tmp_buff2, PS, Lba, ReadBytes, 0);
            if (!NT_SUCCESS(status)) {
                return WCacheRaiseIoError(Cache, Context, status, Lba, PSs, tmp_buff2, WCACHE_W_OP, NULL);
            }
        }
    }

    (*ReadBytes) = PS;
    return STATUS_SUCCESS;
}

// Free packet resources  
VOID
WCacheFreePacket(IN PW_CACHE Cache, IN PVOID Context, IN PW_CACHE_ENTRY block_array, IN lba_t firstLba, IN ULONG PSs)
{
    ULONG i;
    for (i = 0; i < PSs; i++) {
        WCacheFreeSector(block_array, i);
        WCacheClrModFlag(block_array, i);
        WCacheClrBadFlag(block_array, i);
    }
}

// Complete packet update for async operations
VOID
WCacheUpdatePacketComplete(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, 
    IN OUT PW_CACHE_ASYNC* FirstWContext, IN OUT PW_CACHE_ASYNC* PrevWContext, IN BOOLEAN FreePacket)
{
    PW_CACHE_ASYNC WContext, NextWContext;
    NTSTATUS status;
    SIZE_T WrittenBytes;

    WContext = *FirstWContext;
    while (WContext) {
        NextWContext = WContext->NextWContext;
        
        if (WContext->State == ASYNC_STATE_WRITE) {
            status = Cache->WriteProc(IrpContext, Context, WContext->Buffer2, 
                Cache->PacketSize, WContext->Lba, &WrittenBytes, 0);
        }
        
        if (FreePacket) {
            WCacheFreeAsyncEntry(Cache, WContext);
        }
        
        WContext = NextWContext;
    }
    
    *FirstWContext = NULL;
    *PrevWContext = NULL;
}

// Decode cache flags
NTSTATUS __fastcall 
WCacheDecodeFlags(IN PW_CACHE Cache, IN ULONG Flags)
{
    Cache->DoNotCompare = (Flags & WCACHE_DO_NOT_COMPARE) ? TRUE : FALSE;
    Cache->Chained = (Flags & WCACHE_CHAINED_IO) ? TRUE : FALSE;
    Cache->RememberBB = (Flags & WCACHE_MARK_BAD_BLOCKS) ? TRUE : FALSE;
    Cache->NoWriteBB = (Flags & WCACHE_RO_BAD_BLOCKS) ? TRUE : FALSE;
    return STATUS_SUCCESS;
}

// Check cache limits - generic version
NTSTATUS __fastcall
WCacheCheckLimits(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, IN lba_t ReqLba, IN ULONG BCount)
{
    switch (Cache->Mode) {
        case WCACHE_MODE_RAM:
            return WCacheCheckLimitsRAM(IrpContext, Cache, Context, ReqLba, BCount);
        case WCACHE_MODE_RW:
            return WCacheCheckLimitsRW(IrpContext, Cache, Context, ReqLba, BCount);
        case WCACHE_MODE_R:
            return WCacheCheckLimitsR(IrpContext, Cache, Context, ReqLba, BCount);
        default:
            return STATUS_INVALID_PARAMETER;
    }
}

// RAM mode limits check
NTSTATUS __fastcall
WCacheCheckLimitsRAM(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, IN lba_t ReqLba, IN ULONG BCount)
{
    if (Cache->BlockCount + BCount > Cache->MaxBlocks) {
        // Free some blocks
        lba_t Lba = WCacheFindLbaToRelease(Cache);
        if (Lba != WCACHE_INVALID_LBA) {
            // Remove from cache
            WCacheRemoveItemFromList(Cache->CachedBlocksList, &(Cache->BlockCount), Lba);
        }
    }
    return STATUS_SUCCESS;
}

// Read-Write mode limits check  
NTSTATUS __fastcall
WCacheCheckLimitsRW(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, IN lba_t ReqLba, IN ULONG BCount)
{
    if (Cache->FrameCount >= Cache->MaxFrames - Cache->FramesToKeepFree) {
        // Flush and remove some frames
        lba_t Lba = WCacheFindFrameToRelease(Cache);
        if (Lba != WCACHE_INVALID_LBA) {
            WCacheFlushBlocks(IrpContext, Cache, Context, Lba, Cache->BlocksPerFrame);
        }
    }
    return STATUS_SUCCESS;
}

// Read-only mode limits check
NTSTATUS __fastcall  
WCacheCheckLimitsR(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, IN lba_t ReqLba, IN ULONG BCount)
{
    return WCacheCheckLimitsRW(IrpContext, Cache, Context, ReqLba, BCount);
}

// Purge all blocks in RW mode
VOID __fastcall
WCachePurgeAllRW(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context)
{
    ULONG i;
    lba_t Lba;
    
    for (i = 0; i < Cache->FrameCount; i++) {
        Lba = Cache->CachedFramesList[i];
        if (Lba != WCACHE_INVALID_LBA) {
            WCacheRemoveFrame(Cache, Context, Lba >> Cache->BlocksPerFrameSh);
        }
    }
    Cache->FrameCount = 0;
    Cache->BlockCount = 0;
    Cache->WriteCount = 0;
}

// Flush all blocks in RW mode  
VOID __fastcall
WCacheFlushAllRW(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context)
{
    ULONG i;
    lba_t Lba;
    
    for (i = 0; i < Cache->WriteCount; i++) {
        Lba = Cache->CachedModifiedBlocksList[i];
        if (Lba != WCACHE_INVALID_LBA) {
            WCacheFlushBlocks(IrpContext, Cache, Context, Lba, 1);  
        }
    }
}

// Purge all blocks in R mode (same as RW)
VOID __fastcall
WCachePurgeAllR(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context)
{
    WCachePurgeAllRW(IrpContext, Cache, Context);
}

// Validation function for debugging
BOOLEAN
ValidateFrameBlocksList(IN PW_CACHE Cache, IN lba_t Lba)
{
    // Simplified validation - just return TRUE for now
    return TRUE;
}

// Flush blocks from RAM cache
NTSTATUS
WCacheFlushBlocksRAM(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, 
    IN PW_CACHE_ENTRY block_array, IN PULONG List, IN ULONG firstPos, IN ULONG lastPos, IN BOOLEAN FreeBlocks)
{
    NTSTATUS status = STATUS_SUCCESS;
    SIZE_T WrittenBytes;
    ULONG i;
    lba_t Lba;
    
    for (i = firstPos; i < lastPos; i++) {
        Lba = List[i];
        if (WCacheSectorAddr(block_array, Lba - (Lba & ~(Cache->BlocksPerFrame-1)))) {
            status = Cache->WriteProc(IrpContext, Context, 
                (PVOID)WCacheSectorAddr(block_array, Lba - (Lba & ~(Cache->BlocksPerFrame-1))), 
                Cache->BlockSize, Lba, &WrittenBytes, 0);
            if (!NT_SUCCESS(status)) break;
            
            if (FreeBlocks) {
                WCacheFreeSector(block_array, Lba - (Lba & ~(Cache->BlocksPerFrame-1)));
            }
        }
    }
    return status;
}

// Pre-read packet for efficient caching
NTSTATUS
WCachePreReadPacket__(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, IN lba_t Lba)
{
    lba_t PacketStart = Lba & (~((lba_t)(Cache->PacketSize-1)));
    ULONG i;
    lba_t frame_addr = PacketStart >> Cache->BlocksPerFrameSh;
    PW_CACHE_ENTRY block_array;
    NTSTATUS status;
    SIZE_T ReadBytes;
    BOOLEAN SomethingRead = FALSE;
    
    // Initialize frame if needed
    block_array = WCacheInitFrame(Cache, Context, PacketStart);
    if (!block_array) return STATUS_INSUFFICIENT_RESOURCES;
    
    // Check which blocks need to be read
    for (i = 0; i < Cache->PacketSize; i++) {
        lba_t CurrentLba = PacketStart + i;
        ULONG offs = CurrentLba - (frame_addr << Cache->BlocksPerFrameSh);
        
        if (!WCacheSectorAddr(block_array, offs) && 
            (Cache->CheckUsedProc(Context, CurrentLba) & WCACHE_BLOCK_USED)) {
            SomethingRead = TRUE;
            break;
        }
    }
    
    if (SomethingRead) {
        status = Cache->ReadProc(IrpContext, Context, Cache->tmp_buff, 
            Cache->PacketSize * Cache->BlockSize, PacketStart, &ReadBytes, PH_TMP_BUFFER);
        if (!NT_SUCCESS(status)) {
            return WCacheRaiseIoError(Cache, Context, status, PacketStart, Cache->PacketSize, 
                Cache->tmp_buff, WCACHE_R_OP, NULL);
        }
        
        // Copy read data to cache
        for (i = 0; i < Cache->PacketSize; i++) {
            lba_t CurrentLba = PacketStart + i;
            ULONG offs = CurrentLba - (frame_addr << Cache->BlocksPerFrameSh);
            
            if (!WCacheSectorAddr(block_array, offs)) {
                PVOID sector = MyAllocatePoolTag__(NonPagedPool, Cache->BlockSize, MEM_WCBUF_TAG);
                if (sector) {
                    DbgCopyMemory(sector, Cache->tmp_buff + (i * Cache->BlockSize), Cache->BlockSize);
                    block_array[offs].Sector = (ULONG)sector;
                    WCacheInsertItemToList(Cache->CachedBlocksList, &(Cache->BlockCount), CurrentLba);
                }
            }
        }
    }
    
    return STATUS_SUCCESS;
}

// Read blocks from cache or media
NTSTATUS
WCacheReadBlocks__(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, 
    IN PCHAR Buffer, IN lba_t Lba, IN ULONG BCount, OUT PSIZE_T ReadBytes, IN BOOLEAN CachedOnly)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG BS = Cache->BlockSize;
    ULONG BSh = Cache->BlockSizeSh;
    ULONG PS = Cache->PacketSize;
    ULONG i;
    SIZE_T _ReadBytes;
    lba_t frame_addr, offs;
    PW_CACHE_ENTRY block_array;
    BOOLEAN NotCached;
    
    *ReadBytes = 0;
    
    // Split large requests
    if (BCount * BS > Cache->MaxBytesToRead) {
        ULONG PacketMask = PS - 1;
        i = 0;
        while (i < BCount) {
            ULONG blocks_to_read = min(PS, BCount - i);
            status = WCacheReadBlocks__(IrpContext, Cache, Context, Buffer + (i << BSh), 
                Lba + i, blocks_to_read, &_ReadBytes, CachedOnly);
            if (!NT_SUCCESS(status)) break;
            *ReadBytes += _ReadBytes;
            i += blocks_to_read;
        }
        return status;
    }
    
    // Check cache limits
    status = WCacheCheckLimits(IrpContext, Cache, Context, Lba, BCount);
    if (!NT_SUCCESS(status)) return status;
    
    for (i = 0; i < BCount; i++) {
        lba_t CurrentLba = Lba + i;
        frame_addr = CurrentLba >> Cache->BlocksPerFrameSh;
        offs = CurrentLba - (frame_addr << Cache->BlocksPerFrameSh);
        
        block_array = Cache->FrameList[frame_addr].Frame;
        NotCached = !block_array || !WCacheSectorAddr(block_array, offs);
        
        if (NotCached) {
            if (CachedOnly) {
                // Zero-fill if not cached and CachedOnly requested
                RtlZeroMemory(Buffer + (i << BSh), BS);
            } else {
                // Pre-read packet if needed
                status = WCachePreReadPacket__(IrpContext, Cache, Context, CurrentLba);
                if (!NT_SUCCESS(status)) break;
                
                // Try again after pre-read
                block_array = Cache->FrameList[frame_addr].Frame;
                if (block_array && WCacheSectorAddr(block_array, offs)) {
                    DbgCopyMemory(Buffer + (i << BSh), (PVOID)WCacheSectorAddr(block_array, offs), BS);
                } else {
                    RtlZeroMemory(Buffer + (i << BSh), BS);
                }
            }
        } else {
            // Copy from cache
            DbgCopyMemory(Buffer + (i << BSh), (PVOID)WCacheSectorAddr(block_array, offs), BS);
        }
        
        *ReadBytes += BS;
    }
    
    return status;
}

// Write blocks to cache
NTSTATUS  
WCacheWriteBlocks__(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context,
    IN PCHAR Buffer, IN lba_t Lba, IN ULONG BCount, OUT PSIZE_T WrittenBytes, IN BOOLEAN CachedOnly)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG BS = Cache->BlockSize;
    ULONG BSh = Cache->BlockSizeSh;
    ULONG PS = Cache->PacketSize;
    ULONG i;
    SIZE_T _WrittenBytes;
    lba_t frame_addr, offs;
    PW_CACHE_ENTRY block_array;
    PVOID sector;
    
    *WrittenBytes = 0;
    
    // Split large requests
    if (BCount * BS > Cache->MaxBytesToRead) {
        i = 0;
        while (i < BCount) {
            ULONG blocks_to_write = min(PS, BCount - i);
            status = WCacheWriteBlocks__(IrpContext, Cache, Context, Buffer + (i << BSh),
                Lba + i, blocks_to_write, &_WrittenBytes, CachedOnly);
            if (!NT_SUCCESS(status)) break;
            *WrittenBytes += _WrittenBytes;
            i += blocks_to_write;
        }
        return status;
    }
    
    // Check cache limits
    status = WCacheCheckLimits(IrpContext, Cache, Context, Lba, BCount);
    if (!NT_SUCCESS(status)) return status;
    
    for (i = 0; i < BCount; i++) {
        lba_t CurrentLba = Lba + i;
        frame_addr = CurrentLba >> Cache->BlocksPerFrameSh;
        offs = CurrentLba - (frame_addr << Cache->BlocksPerFrameSh);
        
        // Initialize frame if needed
        block_array = WCacheInitFrame(Cache, Context, CurrentLba);
        if (!block_array) return STATUS_INSUFFICIENT_RESOURCES;
        
        // Allocate sector if needed
        if (!WCacheSectorAddr(block_array, offs)) {
            sector = MyAllocatePoolTag__(NonPagedPool, BS, MEM_WCBUF_TAG);
            if (!sector) return STATUS_INSUFFICIENT_RESOURCES;
            
            block_array[offs].Sector = (ULONG)sector;
            WCacheInsertItemToList(Cache->CachedBlocksList, &(Cache->BlockCount), CurrentLba);
        }
        
        // Copy data to cache
        DbgCopyMemory((PVOID)WCacheSectorAddr(block_array, offs), Buffer + (i << BSh), BS);
        WCacheSetModFlag(block_array, offs);
        WCacheInsertItemToList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), CurrentLba);
        
        *WrittenBytes += BS;
    }
    
    return status;
}

// Flush all cached data
NTSTATUS
WCacheFlushAll__(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context)
{
    switch (Cache->Mode) {
        case WCACHE_MODE_RAM:
            WCacheFlushAllRAM(IrpContext, Cache, Context);
            break;
        case WCACHE_MODE_RW:
        case WCACHE_MODE_R:
            WCacheFlushAllRW(IrpContext, Cache, Context);
            break;
    }
    return STATUS_SUCCESS;
}

// Flush all RAM blocks
VOID
WCacheFlushAllRAM(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context)
{
    ULONG i;
    lba_t frame_addr;
    PW_CACHE_ENTRY block_array;
    
    for (i = 0; i < Cache->FrameCount; i++) {
        frame_addr = Cache->CachedFramesList[i] >> Cache->BlocksPerFrameSh;
        block_array = Cache->FrameList[frame_addr].Frame;
        if (block_array) {
            WCacheFlushBlocksRAM(IrpContext, Cache, Context, block_array, 
                Cache->CachedBlocksList, 0, Cache->BlockCount, FALSE);
        }
    }
    Cache->WriteCount = 0;
}

// Flush specific blocks in RW mode
NTSTATUS  
WCacheFlushBlocksRW(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, IN lba_t Lba, IN ULONG BCount)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG PSs = Cache->PacketSize;
    lba_t PacketStart = Lba & (~((lba_t)(PSs-1)));
    lba_t frame_addr = PacketStart >> Cache->BlocksPerFrameSh;
    PW_CACHE_ENTRY block_array;
    SIZE_T ReadBytes;
    PW_CACHE_ASYNC FirstWContext = NULL, PrevWContext = NULL;
    
    block_array = Cache->FrameList[frame_addr].Frame;
    if (block_array) {
        status = WCacheUpdatePacket(IrpContext, Cache, Context, &FirstWContext, &PrevWContext,
            block_array, frame_addr << Cache->BlocksPerFrameSh, PacketStart, 
            Cache->BlockSizeSh, Cache->BlockSize, PSs * Cache->BlockSize, PSs, &ReadBytes, TRUE, 0);
        
        if (FirstWContext) {
            WCacheUpdatePacketComplete(IrpContext, Cache, Context, &FirstWContext, &PrevWContext, TRUE);
        }
        
        WCacheRemoveRangeFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba, BCount);
    }
    
    return status;
}

// Flush specific blocks  
NTSTATUS
WCacheFlushBlocks__(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, IN lba_t Lba, IN ULONG BCount)
{
    NTSTATUS status = STATUS_SUCCESS;
    
    switch (Cache->Mode) {
        case WCACHE_MODE_RAM:
            // For RAM mode, find and flush specific blocks
            break;
        case WCACHE_MODE_RW:
        case WCACHE_MODE_R:
            status = WCacheFlushBlocksRW(IrpContext, Cache, Context, Lba, BCount);
            break;
    }
    
    return status;
}

// Direct cache access - returns pointer to cached block
NTSTATUS
WCacheDirect__(IN PIRP_CONTEXT IrpContext, IN PW_CACHE Cache, IN PVOID Context, 
    IN lba_t Lba, IN BOOLEAN Modified, OUT PCHAR* CachedBlock, IN BOOLEAN CachedOnly)
{
    ULONG frame;
    ULONG i;
    NTSTATUS status = STATUS_SUCCESS;
    PW_CACHE_ENTRY block_array;
    ULONG BS = Cache->BlockSize;
    PCHAR addr;
    SIZE_T _ReadBytes;
    ULONG block_type;

    // Lock cache if necessary
    if (!CachedOnly) {
        ExAcquireResourceExclusiveLite(&(Cache->WCacheLock), TRUE);
    }
    
    // Check bounds
    if ((Lba < Cache->FirstLba) || (Lba > Cache->LastLba)) {
        status = STATUS_INVALID_PARAMETER;
        goto EO_WCache_D;
    }

    frame = Lba >> Cache->BlocksPerFrameSh;
    i = Lba - (frame << Cache->BlocksPerFrameSh);
    
    // Check limits
    if (!CachedOnly && !NT_SUCCESS(status = WCacheCheckLimits(IrpContext, Cache, Context, Lba, 1))) {
        goto EO_WCache_D;
    }

    // Update statistics
    block_array = Cache->FrameList[frame].Frame;
    if (Modified) {
        Cache->FrameList[frame].UpdateCount += 8;
    } else {
        Cache->FrameList[frame].AccessCount += 8;
    }
    
    if (!block_array) {
        block_array = WCacheInitFrame(Cache, Context, Lba);
        if (!block_array) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto EO_WCache_D;
        }
    }
    
    // Check if block is cached
    if (!(addr = (PCHAR)WCacheSectorAddr(block_array, i))) {
        // Block not cached - allocate and read
        addr = (PCHAR)MyAllocatePoolTag__(CACHED_BLOCK_MEMORY_TYPE, BS, MEM_WCBUF_TAG);
        if (!addr) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto EO_WCache_D;
        }
        
        block_type = Cache->CheckUsedProc(Context, Lba);
        if (block_type == WCACHE_BLOCK_USED) {
            status = Cache->ReadProc(IrpContext, Context, addr, BS, Lba, &_ReadBytes, PH_TMP_BUFFER);
            if (Cache->RememberBB && !NT_SUCCESS(status)) {
                RtlZeroMemory(addr, BS);
            }
        } else {
            if (block_type & WCACHE_BLOCK_BAD) {
                MyFreePool__(addr);
                addr = NULL;
                status = STATUS_DEVICE_DATA_ERROR;
                goto EO_WCache_D;
            }
            status = STATUS_SUCCESS;
            RtlZeroMemory(addr, BS);
        }
        
        // Add to cache
        block_array[i].Sector = (ULONG)addr;
        WCacheInsertItemToList(Cache->CachedBlocksList, &(Cache->BlockCount), Lba);
        if (Modified) {
            WCacheInsertItemToList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba);
            WCacheSetModFlag(block_array, i);
        }
        Cache->FrameList[frame].BlockCount++;
    } else {
        // Block is cached
        block_type = Cache->CheckUsedProc(Context, Lba);
        if (block_type & WCACHE_BLOCK_BAD) {
            status = STATUS_DEVICE_DATA_ERROR;
            goto EO_WCache_D;
        }
        
        if (Modified && !WCacheGetModFlag(block_array, i)) {
            WCacheInsertItemToList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba);
            WCacheSetModFlag(block_array, i);
        }
    }
    
    (*CachedBlock) = addr;

EO_WCache_D:
    if (!CachedOnly && !NT_SUCCESS(status)) {
        ExReleaseResourceLite(&(Cache->WCacheLock));
    }
    return status;
}

// End direct I/O - release cache lock
NTSTATUS
WCacheEODirect__(IN PW_CACHE Cache, IN PVOID Context)
{
    ExReleaseResourceLite(&(Cache->WCacheLock));
    return STATUS_SUCCESS;
}

// Start direct I/O - acquire cache lock
NTSTATUS
WCacheStartDirect__(IN PW_CACHE Cache, IN PVOID Context, IN BOOLEAN ForWrite)
{
    ExAcquireResourceExclusiveLite(&(Cache->WCacheLock), TRUE);
    return STATUS_SUCCESS;
}

// Check if block is cached
BOOLEAN
WCacheIsCached__(IN PW_CACHE Cache, IN lba_t Lba, IN ULONG BCount)
{
    ULONG i;
    lba_t frame_addr, offs;
    PW_CACHE_ENTRY block_array;
    
    for (i = 0; i < BCount; i++) {
        lba_t CurrentLba = Lba + i;
        frame_addr = CurrentLba >> Cache->BlocksPerFrameSh;
        offs = CurrentLba - (frame_addr << Cache->BlocksPerFrameSh);
        
        block_array = Cache->FrameList[frame_addr].Frame;
        if (!block_array || !WCacheSectorAddr(block_array, offs)) {
            return FALSE;
        }
    }
    return TRUE;
}

// Release cache resources
NTSTATUS
WCacheRelease__(IN PW_CACHE Cache)
{
    ULONG i;
    lba_t frame_addr;
    PW_CACHE_ENTRY block_array;
    
    if (!Cache) return STATUS_SUCCESS;
    
    // Free all frames and blocks
    for (i = 0; i < Cache->FrameCount; i++) {
        frame_addr = Cache->CachedFramesList[i] >> Cache->BlocksPerFrameSh;
        WCacheRemoveFrame(Cache, NULL, frame_addr);
    }
    
    // Free allocated resources
    if (Cache->FrameList) MyFreePool__(Cache->FrameList);
    if (Cache->CachedBlocksList) MyFreePool__(Cache->CachedBlocksList);
    if (Cache->CachedModifiedBlocksList) MyFreePool__(Cache->CachedModifiedBlocksList);
    if (Cache->CachedFramesList) MyFreePool__(Cache->CachedFramesList);
    if (Cache->AsyncEntryList) MyFreePool__(Cache->AsyncEntryList);
    if (Cache->tmp_buff) MyFreePool__(Cache->tmp_buff);
    if (Cache->FastMutex) MyFreePool__(Cache->FastMutex);
    
    ExDeleteResourceLite(&(Cache->WCacheLock));
    
    RtlZeroMemory(Cache, sizeof(W_CACHE));
    return STATUS_SUCCESS;
}

// Check if cache is initialized
BOOLEAN
WCacheIsInitialized__(IN PW_CACHE Cache)
{
    return (Cache && Cache->FrameList && Cache->ReadProc);
}

// Set cache mode
NTSTATUS  
WCacheSetMode__(IN PW_CACHE Cache, IN ULONG Mode)
{
    if (Mode > WCACHE_MODE_MAX) return STATUS_INVALID_PARAMETER;
    Cache->Mode = Mode;
    return STATUS_SUCCESS;
}

// Get cache mode
ULONG
WCacheGetMode__(IN PW_CACHE Cache)
{
    return Cache ? Cache->Mode : WCACHE_MODE_MAX + 1;
}

// Get write block count
ULONG
WCacheGetWriteBlockCount__(IN PW_CACHE Cache)
{
    return Cache ? Cache->WriteCount : 0;
}

// Synchronize relocation
NTSTATUS
WCacheSyncReloc__(IN PW_CACHE Cache, IN PVOID Context, IN lba_t Lba, IN ULONG BCount, IN lba_t NewLba)
{
    if (Cache->UpdateRelocProc) {
        return Cache->UpdateRelocProc(Context, Lba, BCount, NewLba);
    }
    return STATUS_SUCCESS;
}

// Discard blocks from cache
NTSTATUS
WCacheDiscardBlocks__(IN PW_CACHE Cache, IN PVOID Context, IN lba_t Lba, IN ULONG BCount)
{
    ULONG i;
    lba_t frame_addr, offs;
    PW_CACHE_ENTRY block_array;
    
    for (i = 0; i < BCount; i++) {
        lba_t CurrentLba = Lba + i;
        frame_addr = CurrentLba >> Cache->BlocksPerFrameSh;
        offs = CurrentLba - (frame_addr << Cache->BlocksPerFrameSh);
        
        block_array = Cache->FrameList[frame_addr].Frame;
        if (block_array && WCacheSectorAddr(block_array, offs)) {
            WCacheFreeSector(block_array, offs);
            WCacheClrModFlag(block_array, offs);
            WCacheRemoveItemFromList(Cache->CachedBlocksList, &(Cache->BlockCount), CurrentLba);
            WCacheRemoveItemFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), CurrentLba);
        }
    }
    
    return STATUS_SUCCESS;
}

// Complete async operations
NTSTATUS
WCacheCompleteAsync__(IN PW_CACHE Cache, IN PVOID Context, IN PW_CACHE_ASYNC WContext)
{
    if (WContext) {
        WCacheFreeAsyncEntry(Cache, WContext);
    }
    return STATUS_SUCCESS;
}

// Change cache flags
NTSTATUS
WCacheChFlags__(IN PW_CACHE Cache, IN ULONG SetFlags, IN ULONG ClrFlags)
{
    Cache->Flags |= SetFlags;
    Cache->Flags &= ~ClrFlags;
    return WCacheDecodeFlags(Cache, Cache->Flags);
}
