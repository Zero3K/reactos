////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

/*********************************************************************/

NTSTATUS
WCacheCheckLimits(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN lba_t ReqLba,
    IN ULONG BCount
    );

NTSTATUS
WCacheCheckLimitsRAM(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN lba_t ReqLba,
    IN ULONG BCount
    );

NTSTATUS
WCacheCheckLimitsRW(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN lba_t ReqLba,
    IN ULONG BCount
    );

NTSTATUS
WCacheCheckLimitsR(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN lba_t ReqLba,
    IN ULONG BCount
    );

VOID
WCachePurgeAllRW(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context
    );

VOID
WCacheFlushAllRW(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context
    );

VOID
WCachePurgeAllR(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context
    );

NTSTATUS WCacheDecodeFlags(IN PW_CACHE Cache,
                             IN ULONG Flags);


#define WCacheValidateBlockCount(Cache, BCount) \
    ((BCount) <= (Cache)->MaxBlocks)

#define WCacheCheckCacheSpace(Cache, List, ReqLba, BCount) \
    (((Cache)->BlockCount + WCacheGetSortedListIndex((Cache)->BlockCount, (List), (ReqLba)) + \
      (BCount) - WCacheGetSortedListIndex((Cache)->BlockCount, (List), (ReqLba)+(BCount))) <= (Cache)->MaxBlocks)

#define WCACHE_MAX_CHAIN      (0x10)

#define MEM_WCCTX_TAG         'xtCW'
#define MEM_WCFRM_TAG         'rfCW'
#define MEM_WCBUF_TAG         'fbCW'

#define WcPrint(x) {;}

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

VOID
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    // FirstWContext (simplified), // pointer to head async IO context
    // PrevWContext (simplified),  // pointer to tail async IO context
    IN BOOLEAN FreePacket = TRUE
    );

BOOLEAN
ValidateFrameBlocksList(
    IN PW_CACHE Cache,
    IN lba_t Lba);

/*********************************************************************/
ULONG WCache_random;

/*
  WCacheInit__() fills all necesary fileds in passed in PW_CACHE Cache
  structure, allocates memory and synchronization resources.
  Cacheable area is subdiveded on Frames - contiguous sets of blocks.
  Internally each Frame is an array of pointers and attributes of cached
  Blocks. To optimize memory usage WCache keeps in memory limited number
  of frames (MaxFrames).
  Frame length (number of Blocks) must be be a power of 2 and aligned on
  minimum writeable block size - Packet.
  Packet size must be a power of 2 (2, 4, 8, 16, etc.).
  Each cached Block belongs to one of the Frames. To optimize memory usage
  WCache keeps in memory limited number of Blocks (MaxBlocks). Block size
  must be a power of 2.
  WCache splits low-level request(s) into some parts if requested data length
  exceeds MaxBytesToRead.
  If requested data length exceeds maximum cache size WCache makes
  recursive calls to read/write routines with shorter requests

  WCacheInit__() returns initialization status. If initialization failed,
  all allocated memory and resources are automaticelly freed.

  Public routine
 */
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
    IN PWRITE_BLOCK_ASYNC WriteProcAsync,    // Simplified: always NULL
    IN PREAD_BLOCK_ASYNC ReadProcAsync,      // Simplified: always NULL
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

    // Validate basic parameters (simplified)
    if (!ReadProc || !MaxFrames || !MaxBlocks || FirstLba >= LastLba || Mode > WCACHE_MODE_MAX) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Validate alignment requirements
    if ((MaxBlocks % PacketSize) || (BlocksPerFrame % PacketSize) || (FramesToKeepFree >= MaxFrames/2)) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Async operations are simplified away
    WriteProcAsync = NULL;
    ReadProcAsync = NULL;
    
    // Allocate required structures
    MaxBlocks = max(MaxBlocks, BlocksPerFrame*3);
    
    Cache->FrameList = (PW_CACHE_FRAME)MyAllocatePoolTag__(NonPagedPool, 
        l1 = (((LastLba >> BlocksPerFrameSh)+1)*sizeof(W_CACHE_FRAME)), MEM_WCFRM_TAG);
    if (!Cache->FrameList) return STATUS_INSUFFICIENT_RESOURCES;
    
    Cache->CachedBlocksList = (PULONG)MyAllocatePoolTag__(NonPagedPool, 
        l2 = ((MaxBlocks+2)*sizeof(lba_t)), MEM_WCFRM_TAG);
    if (!Cache->CachedBlocksList) {
        MyFreePool__(Cache->FrameList);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    Cache->CachedModifiedBlocksList = (PULONG)MyAllocatePoolTag__(NonPagedPool, l2, MEM_WCFRM_TAG);
    if (!Cache->CachedModifiedBlocksList) {
        MyFreePool__(Cache->FrameList);
        MyFreePool__(Cache->CachedBlocksList);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    Cache->CachedFramesList = (PULONG)MyAllocatePoolTag__(NonPagedPool, 
        l3 = ((MaxFrames+2)*sizeof(lba_t)), MEM_WCFRM_TAG);
    if (!Cache->CachedFramesList) {
        MyFreePool__(Cache->FrameList);
        MyFreePool__(Cache->CachedBlocksList);
        MyFreePool__(Cache->CachedModifiedBlocksList);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize memory
    RtlZeroMemory(Cache->FrameList, l1);
    RtlZeroMemory(Cache->CachedBlocksList, l2);
    RtlZeroMemory(Cache->CachedModifiedBlocksList, l2);
    RtlZeroMemory(Cache->CachedFramesList, l3);
    
    // Set cache parameters
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
    Cache->WriteCount = 0;
    Cache->FirstLba = FirstLba;
    Cache->LastLba = LastLba;
    Cache->Mode = Mode;

    if (!NT_SUCCESS(RC = WCacheDecodeFlags(Cache, Flags))) {
        MyFreePool__(Cache->FrameList);
        MyFreePool__(Cache->CachedBlocksList);
        MyFreePool__(Cache->CachedModifiedBlocksList);
        MyFreePool__(Cache->CachedFramesList);
        return RC;
    }

    Cache->FramesToKeepFree = FramesToKeepFree;
    Cache->WriteProc = WriteProc;
    Cache->ReadProc = ReadProc;
    Cache->WriteProcAsync = NULL;  // Simplified
    Cache->ReadProcAsync = NULL;   // Simplified
    Cache->CheckUsedProc = CheckUsedProc;
    Cache->UpdateRelocProc = UpdateRelocProc;
    Cache->ErrorHandlerProc = ErrorHandlerProc;
    
    // Allocate temporary buffers
    Cache->tmp_buff = (PCHAR)MyAllocatePoolTag__(NonPagedPool, PacketSize*BlockSize, MEM_WCFRM_TAG);
    Cache->tmp_buff_r = (PCHAR)MyAllocatePoolTag__(NonPagedPool, PacketSize*BlockSize, MEM_WCFRM_TAG);
    Cache->reloc_tab = (PULONG)MyAllocatePoolTag__(NonPagedPool, Cache->PacketSize*sizeof(ULONG), MEM_WCFRM_TAG);
    
    if (!Cache->tmp_buff || !Cache->tmp_buff_r || !Cache->reloc_tab) {
        MyFreePool__(Cache->FrameList);
        MyFreePool__(Cache->CachedBlocksList);
        MyFreePool__(Cache->CachedModifiedBlocksList);
        MyFreePool__(Cache->CachedFramesList);
        if (Cache->tmp_buff_r) MyFreePool__(Cache->tmp_buff_r);
        if (Cache->tmp_buff) MyFreePool__(Cache->tmp_buff);
        if (Cache->reloc_tab) MyFreePool__(Cache->reloc_tab);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    if (!NT_SUCCESS(RC = ExInitializeResourceLite(&(Cache->WCacheLock)))) {
        MyFreePool__(Cache->FrameList);
        MyFreePool__(Cache->CachedBlocksList);
        MyFreePool__(Cache->CachedModifiedBlocksList);
        MyFreePool__(Cache->CachedFramesList);
        MyFreePool__(Cache->tmp_buff_r);
        MyFreePool__(Cache->tmp_buff);
        MyFreePool__(Cache->reloc_tab);
        return RC;
    }
    
    KeQuerySystemTime((PLARGE_INTEGER)(&rseed));
    WCache_random = rseed.LowPart;
    Cache->Tag = 0xCAC11E00;

    return STATUS_SUCCESS;
} // end WCacheInit__()

/*
  WCacheRandom() - simple pseudo-random generator for cache eviction
  Returns random LONGLONG number  
  Internal routine
 */
LONGLONG
WCacheRandom(VOID)
{
    WCache_random = (WCache_random * 1103515245 + 12345);
    return WCache_random;
} // end WCacheRandom()

/*
  WCacheFindLbaToRelease() finds Block to be flushed and purged from cache
  Returns random LBA
  Internal routine
 */
lba_t
WCacheFindLbaToRelease(
    IN PW_CACHE Cache
    )
{
    if (!(Cache->BlockCount))
        return WCACHE_INVALID_LBA;
    return(Cache->CachedBlocksList[((ULONG)WCacheRandom() % Cache->BlockCount)]);
} // end WCacheFindLbaToRelease()

/*
  WCacheFindModifiedLbaToRelease() finds Block to be flushed and purged from cache.
  This routine looks for Blocks among modified ones
  Returns random LBA (nodified)
  Internal routine
 */
lba_t
WCacheFindModifiedLbaToRelease(
    IN PW_CACHE Cache
    )
{
    if (!(Cache->WriteCount))
        return WCACHE_INVALID_LBA;
    return(Cache->CachedModifiedBlocksList[((ULONG)WCacheRandom() % Cache->WriteCount)]);
} // end WCacheFindModifiedLbaToRelease()

/*
  WCacheFindFrameToRelease() finds Frame to be flushed and purged with all
  Blocks (from this Frame) from cache
  Returns random Frame number
  Internal routine
 */
lba_t
WCacheFindFrameToRelease(
    IN PW_CACHE Cache
    )
{
    ULONG i, j;
    ULONG frame = 0;
    ULONG prev_uc = -1;
    ULONG uc = -1;
    lba_t lba;
    BOOLEAN mod = FALSE;

    if (!(Cache->FrameCount))
        return 0;

    for(i=0; i<Cache->FrameCount; i++) {
        j = Cache->CachedFramesList[i];
        mod |= (Cache->FrameList[j].UpdateCount != 0);
        uc = Cache->FrameList[j].UpdateCount*32 + Cache->FrameList[j].AccessCount;

        if (prev_uc > uc) {
            prev_uc = uc;
            frame = j;
        }
    }
    
    if (!mod) {
        frame = Cache->CachedFramesList[((ULONG)WCacheRandom() % Cache->FrameCount)];
        lba = frame << Cache->BlocksPerFrameSh;
        WcPrint(("WC:-frm %x\n", lba));
    } else {
        lba = frame << Cache->BlocksPerFrameSh;
        WcPrint(("WC:-frm(mod) %x\n", lba));
        // Decay counters for all frames
        for(i=0; i<Cache->FrameCount; i++) {
            j = Cache->CachedFramesList[i];
            Cache->FrameList[j].UpdateCount = (Cache->FrameList[j].UpdateCount*2)/3;
            Cache->FrameList[j].AccessCount = (Cache->FrameList[j].AccessCount*3)/4;
        }
    }
    return frame;
} // end WCacheFindFrameToRelease()

/*
  WCacheGetSortedListIndex() returns index of searched Lba
  (Lba is ULONG in sorted array) or index of minimal cached Lba
  greater than searched.
  If requested Lba is less than minimum cached, 0 is returned.
  If requested Lba is greater than maximum cached, BlockCount value
  is returned.
  Internal routine
 */

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4035)               // re-enable below
#endif

__inline
ULONG
WCacheGetSortedListIndex(
    IN ULONG BlockCount,      // number of items in array (pointed by List)
    IN lba_t* List,           // pointer to sorted (ASC) array of ULONGs
    IN lba_t Lba              // ULONG value to be searched for
    )
{
    if (!BlockCount)
        return 0;

    ULONG pos;
    ULONG left;
    ULONG right;

    left = 0;
    right = BlockCount - 1;
    pos = 0;
    while(left != right) {
        pos = (left + right) >> 1;
        if (List[pos] == Lba)
            return pos;
        if (right - left == 1) {
            if (List[pos+1] < Lba)
                return (pos+2);
            break;
        }
        if (List[pos] < Lba) {
            left = pos;
        } else {
            right = pos;
        }
    }
    if ((List[pos] < Lba) && ((pos+1) <= BlockCount)) pos++;

    return pos;
}

#ifdef _MSC_VER
#pragma warning(pop) // re-enable warning #4035
#endif

/*
  WCacheInsertRangeToList() inserts values laying in range described
  by Lba (1st value) and BCount (number of sequentially incremented
  values) in sorted array of ULONGs pointed by List.
  Ex.: (Lba, BCount)=(7,3) will insert values {7,8,9}.
  If target array already contains one or more values falling in
  requested range, they will be removed before insertion.
  WCacheInsertRangeToList() updates value of (*BlockCount) to reflect
  performed changes.
  WCacheInsertRangeToList() assumes that target array is of enough size.
  Internal routine
 */
VOID
WCacheInsertRangeToList(
    IN lba_t* List,           // pointer to sorted (ASC) array of ULONGs
    IN PULONG BlockCount,     // pointer to number of items in array (pointed by List)
    IN lba_t Lba,             // initial value for insertion
    IN ULONG BCount           // number of sequentially incremented values to be inserted
    )
{
    if (!BCount)
        return;

    ASSERT(!(BCount & 0x80000000));

    ULONG firstPos = WCacheGetSortedListIndex(*BlockCount, List, Lba);
    ULONG lastPos = WCacheGetSortedListIndex(*BlockCount, List, Lba+BCount);
    ULONG offs = firstPos + BCount - lastPos;

    if (offs) {
        // move list tail
        if (*BlockCount) {
#ifdef WCACHE_BOUND_CHECKS
            MyCheckArray(List, lastPos+offs+(*BlockCount)-lastPos-1);
#endif //WCACHE_BOUND_CHECKS
            DbgMoveMemory(&(List[lastPos+offs]), &(List[lastPos]), ((*BlockCount) - lastPos) * sizeof(ULONG));
        }
        lastPos += offs;
        for(; firstPos<lastPos; firstPos++) {
#ifdef WCACHE_BOUND_CHECKS
            MyCheckArray(List, firstPos);
#endif //WCACHE_BOUND_CHECKS
            List[firstPos] = Lba;
            Lba++;
        }
        (*BlockCount) += offs;
    }
} // end WCacheInsertRangeToList()

/*
  WCacheInsertItemToList() inserts value Lba in sorted array of
  ULONGs pointed by List.
  If target array already contains requested value, no
  operations are performed.
  WCacheInsertItemToList() updates value of (*BlockCount) to reflect
  performed changes.
  WCacheInsertItemToList() assumes that target array is of enough size.
  Internal routine
 */
VOID
WCacheInsertItemToList(
    IN lba_t* List,           // pointer to sorted (ASC) array of lba_t's
    IN PULONG BlockCount,     // pointer to number of items in array (pointed by List)
    IN lba_t Lba              // value to be inserted
    )
{
    ULONG firstPos = WCacheGetSortedListIndex(*BlockCount, List, Lba+1);
    if (firstPos && (List[firstPos-1] == Lba))
        return;

    // move list tail
    if (*BlockCount) {
#ifdef WCACHE_BOUND_CHECKS
        MyCheckArray(List, firstPos+1+(*BlockCount)-firstPos-1);
#endif //WCACHE_BOUND_CHECKS
        DbgMoveMemory(&(List[firstPos+1]), &(List[firstPos]), ((*BlockCount) - firstPos) * sizeof(ULONG));
    }
#ifdef WCACHE_BOUND_CHECKS
    MyCheckArray(List, firstPos);
#endif //WCACHE_BOUND_CHECKS
    List[firstPos] = Lba;
    (*BlockCount) ++;
} // end WCacheInsertItemToList()

/*
  WCacheRemoveRangeFromList() removes values falling in range described
  by Lba (1st value) and BCount (number of sequentially incremented
  values) from sorted array of ULONGs pointed by List.
  Ex.: (Lba, BCount)=(7,3) will remove values {7,8,9}.
  If target array doesn't contain values falling in
  requested range, no operation is performed.
  WCacheRemoveRangeFromList() updates value of (*BlockCount) to reflect
  performed changes.
  Internal routine
 */
VOID
WCacheRemoveRangeFromList(
    IN lba_t* List,           // pointer to sorted (ASC) array of ULONGs
    IN PULONG BlockCount,     // pointer to number of items in array (pointed by List)
    IN lba_t Lba,             // initial value for removal
    IN ULONG BCount           // number of sequentially incremented values to be removed
    )
{
    ULONG firstPos = WCacheGetSortedListIndex(*BlockCount, List, Lba);
    ULONG lastPos = WCacheGetSortedListIndex(*BlockCount, List, Lba+BCount);
    ULONG offs = lastPos - firstPos;

    if (offs) {
        // move list tail
        DbgMoveMemory(&(List[lastPos-offs]), &(List[lastPos]), ((*BlockCount) - lastPos) * sizeof(ULONG));
        (*BlockCount) -= offs;
    }
} // end WCacheRemoveRangeFromList()

/*
  WCacheRemoveItemFromList() removes value Lba from sorted array
  of ULONGs pointed by List.
  If target array doesn't contain requested value, no
  operations are performed.
  WCacheRemoveItemFromList() updates value of (*BlockCount) to reflect
  performed changes.
  Internal routine
 */
VOID
WCacheRemoveItemFromList(
    IN lba_t* List,           // pointer to sorted (ASC) array of ULONGs
    IN PULONG BlockCount,     // pointer to number of items in array (pointed by List)
    IN lba_t Lba              // value to be removed
    )
{
    if (!(*BlockCount)) return;
    ULONG lastPos = WCacheGetSortedListIndex(*BlockCount, List, Lba+1);
    if (!lastPos || (lastPos && (List[lastPos-1] != Lba)))
        return;

    // move list tail
    DbgMoveMemory(&(List[lastPos-1]), &(List[lastPos]), ((*BlockCount) - lastPos) * sizeof(ULONG));
    (*BlockCount) --;
} // end WCacheRemoveItemFromList()

/*
  WCacheInitFrame() allocates storage for Frame (block_array)
  with index 'frame', fills it with 0 (none of Blocks from
  this Frame is cached) and inserts it's index to sorted array
  of frame indexes.
  WCacheInitFrame() also checks if number of frames reaches limit
  and invokes WCacheCheckLimits() to free some Frames/Blocks
  Internal routine
 */
PW_CACHE_ENTRY
WCacheInitFrame(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // caller's context (currently unused)
    IN ULONG frame            // frame index
    )
{
    PW_CACHE_ENTRY block_array;
    ULONG l;
#ifdef DBG
    ULONG old_count = Cache->FrameCount;
#endif //DBG

    // We are about to add new cache frame.
    // Thus check if we have enough free entries and
    // flush unused ones if it is neccessary.
    if (Cache->FrameCount >= Cache->MaxFrames) {
        WCacheCheckLimits(IrpContext, Cache, Context, frame << Cache->BlocksPerFrameSh, Cache->PacketSize*2);
    }
    ASSERT(Cache->FrameCount < Cache->MaxFrames);
    block_array = (PW_CACHE_ENTRY)MyAllocatePoolTag__(NonPagedPool, l = sizeof(W_CACHE_ENTRY) << Cache->BlocksPerFrameSh, MEM_WCFRM_TAG);
    ASSERT(Cache->FrameList[frame].Frame == NULL);
    Cache->FrameList[frame].Frame = block_array;

    // Keep history !!!
    //Cache->FrameList[frame].UpdateCount = 0;
    //Cache->FrameList[frame].AccessCount = 0;

    if (block_array) {
        ASSERT((ULONG_PTR)block_array > 0x1000);
        WCacheInsertItemToList(Cache->CachedFramesList, &(Cache->FrameCount), frame);
        RtlZeroMemory(block_array, l);
    } else {
    }
    ASSERT(Cache->FrameCount <= Cache->MaxFrames);
#ifdef DBG
    ASSERT(old_count < Cache->FrameCount);
#endif //DBG
    return block_array;
} // end WCacheInitFrame()

/*
  WCacheRemoveFrame() frees storage for Frame (block_array) with
  index 'frame' and removes it's index from sorted array of
  frame indexes.
  Internal routine
 */
VOID
WCacheRemoveFrame(
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user's context (currently unused)
    IN ULONG frame            // frame index
    )
{
    PW_CACHE_ENTRY block_array;
#ifdef DBG
    ULONG old_count = Cache->FrameCount;
#endif //DBG

    ASSERT(Cache->FrameCount <= Cache->MaxFrames);
    ASSERT(Cache->FrameList[frame].BlockCount == 0);
    block_array = Cache->FrameList[frame].Frame;

    WCacheRemoveItemFromList(Cache->CachedFramesList, &(Cache->FrameCount), frame);
    MyFreePool__(block_array);
    Cache->FrameList[frame].Frame = NULL;
    ASSERT(Cache->FrameCount < Cache->MaxFrames);
#ifdef DBG
    ASSERT(old_count > Cache->FrameCount);
#endif //DBG

} // end WCacheRemoveFrame()

/*
  WCacheSetModFlag() sets Modified flag for Block with offset 'i'
  in Frame 'block_array'
  Internal routine
 */
#define WCacheSetModFlag(block_array, i) \
    *((PULONG)&(block_array[i].Sector)) |= WCACHE_FLAG_MODIFIED

/*
  WCacheClrModFlag() clears Modified flag for Block with offset 'i'
  in Frame 'block_array'
  Internal routine
 */
#define WCacheClrModFlag(block_array, i) \
    *((PULONG)&(block_array[i].Sector)) &= ~WCACHE_FLAG_MODIFIED

/*
  WCacheGetModFlag() returns non-zero value if Modified flag for
  Block with offset 'i' in Frame 'block_array' is set. Otherwise
  0 is returned.
  Internal routine
 */
#define WCacheGetModFlag(block_array, i) \
    (*((PULONG)&(block_array[i].Sector)) & WCACHE_FLAG_MODIFIED)

#if 0
/*
  WCacheSetBadFlag() sets Modified flag for Block with offset 'i'
  in Frame 'block_array'
  Internal routine
 */
#define WCacheSetBadFlag(block_array, i) \
    *((PULONG)&(block_array[i].Sector)) |= WCACHE_FLAG_BAD

/*
  WCacheClrBadFlag() clears Modified flag for Block with offset 'i'
  in Frame 'block_array'
  Internal routine
 */
#define WCacheClrBadFlag(block_array, i) \
    *((PULONG)&(block_array[i].Sector)) &= ~WCACHE_FLAG_BAD

/*
  WCacheGetBadFlag() returns non-zero value if Modified flag for
  Block with offset 'i' in Frame 'block_array' is set. Otherwise
  0 is returned.
  Internal routine
 */
#define WCacheGetBadFlag(block_array, i) \
    (((UCHAR)(block_array[i].Sector)) & WCACHE_FLAG_BAD)
#endif //0

/*
  WCacheSectorAddr() returns pointer to memory block containing cached
  data for Block described by Frame (block_array) and offset in this
  Frame (i). If requested Block is not cached yet NULL is returned.
  Internal routine
 */
#define WCacheSectorAddr(block_array, i) \
    ((ULONG_PTR)(block_array[i].Sector) & WCACHE_ADDR_MASK)

/*
  WCacheFreeSector() releases memory block containing cached
  data for Block described by Frame (block_array) and offset in this
  Frame (i). Should never be called for non-cached Blocks.
  Internal routine
 */
#define WCacheFreeSector(frame, offs) \
{                          \
    DbgFreePool((PVOID)WCacheSectorAddr(block_array, offs)); \
    block_array[offs].Sector = NULL; \
    Cache->FrameList[frame].BlockCount--; \
}

/*
  WCacheAllocAsyncEntry() allocates storage for async IO context,
  links it to previously allocated async IO context (if any),
  initializes synchronization (completion) event
  and allocates temporary IO buffers.
  Async IO contexts are used to create chained set of IO requests
  durring top-level request precessing and wait for their completion.
  Internal routine
 */
PW_CACHE_ASYNC
WCacheAllocAsyncEntry(
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    // FirstWContext (simplified), // pointer to the pointer to
                              //   the head of async IO context chain
    // PrevWContext (simplified),  // pointer to the storage for pointer
                              //   to newly allocated async IO context chain
    IN ULONG BufferSize       // requested IO buffer size
    )
{
    PW_CACHE_ASYNC WContext;
    PCHAR Buffer;

    WContext = (PW_CACHE_ASYNC)MyAllocatePoolTag__(NonPagedPool,sizeof(W_CACHE_ASYNC), MEM_WCCTX_TAG);
    if (!WContext)
        return NULL;
    Buffer = (PCHAR)DbgAllocatePoolWithTag(NonPagedPool, BufferSize*(2-Cache->Chained), MEM_WCBUF_TAG);
    if (!Buffer) {
        MyFreePool__(WContext);
        return NULL;
    }

    if (!Cache->Chained)
        KeInitializeEvent(&(WContext->PhContext.event), SynchronizationEvent, FALSE);
    WContext->Cache = Cache;
    if (*PrevWContext)
        (*PrevWContext)->NextWContext = WContext;
    WContext->NextWContext = NULL;
    WContext->Buffer = Buffer;
    WContext->Buffer2 = Buffer+(Cache->Chained ? 0 : BufferSize);

    if (!(*FirstWContext))
        (*FirstWContext) = WContext;
    (*PrevWContext) = WContext;

    return WContext;
} // end WCacheAllocAsyncEntry()

/*
  WCacheFreeAsyncEntry() releases storage previously allocated for
  async IO context.
  Internal routine
 */
VOID
WCacheFreeAsyncEntry(
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    PW_CACHE_ASYNC WContext   // pointer to async IO context to release
    )
{
    DbgFreePool(WContext->Buffer);
    MyFreePool__(WContext);
} // end WCacheFreeAsyncEntry()


NTSTATUS
WCacheRaiseIoError(
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,
    IN NTSTATUS Status,
    IN ULONG Lba,
    IN ULONG BCount,
    IN PVOID Buffer,
    IN BOOLEAN ReadOp,
    IN PBOOLEAN Retry
    )
{
    if (!Cache->ErrorHandlerProc)
        return Status;

    WCACHE_ERROR_CONTEXT ec;

    ec.WCErrorCode = ReadOp ? WCACHE_ERROR_READ : WCACHE_ERROR_WRITE;
    ec.Status = Status;
    ec.ReadWrite.Lba    = Lba;
    ec.ReadWrite.BCount = BCount;
    ec.ReadWrite.Buffer = Buffer;
    Status = Cache->ErrorHandlerProc(Context, &ec);
    if (Retry)
        (*Retry) = ec.Retry;

    return Status;

} // end WCacheRaiseIoError()

/*
  WCacheUpdatePacket() attempts to updates packet containing target Block.
  If async IO is enabled new IO context is added to the chain.
  If packet containing target Block is modified and PrefereWrite flag
  is NOT set, function returns with status STATUS_RETRY. This setting is
  user in WCACHE_MODE_R mode to reduce physical writes on flush.
  'State' parameter is used in async mode to determine the next processing
  stege for given request
  Internal routine
 */
NTSTATUS
WCacheUpdatePacket(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,
    IN PVOID Context,
    // FirstWContext (simplified),  // Unused - simplified
    // PrevWContext (simplified),   // Unused - simplified  
    IN PW_CACHE_ENTRY block_array,
    IN lba_t firstLba,
    IN lba_t Lba,
    IN ULONG BSh,
    IN ULONG BS,
    IN ULONG PS,
    IN ULONG PSs,
    IN PSIZE_T ReadBytes,
    IN BOOLEAN PrefereWrite,
    IN ULONG State                         // Unused - simplified
    )
{
    NTSTATUS status;
    PCHAR tmp_buff = Cache->tmp_buff;
    BOOLEAN mod, read, zero;
    ULONG i;
    lba_t Lba0;
    ULONG block_type;

    // Check if packet contains modified blocks
    mod = read = zero = FALSE;
    Lba0 = Lba - firstLba;
    for(i=0; i<PSs; i++, Lba0++) {
        if (WCacheGetModFlag(block_array, Lba0)) {
            mod = TRUE;
        } else if (!WCacheSectorAddr(block_array,Lba0) &&
                  ((block_type = Cache->CheckUsedProc(Context, Lba+i)) & WCACHE_BLOCK_USED) ) {
            if (block_type & WCACHE_BLOCK_ZERO) {
                zero = TRUE;
            } else {
                read = TRUE;
            }
        }
    }
    
    // Check if we are allowed to write to media
    if (mod && !PrefereWrite) {
        return STATUS_RETRY;
    }
    
    // Return if no modifications
    if (!mod) {
        (*ReadBytes) = PS;
        return STATUS_SUCCESS;
    }

    // Read packet if necessary
    if (read) {
        status = Cache->ReadProc(IrpContext, Context, tmp_buff, PS, Lba, ReadBytes, PH_TMP_BUFFER);
        if (!NT_SUCCESS(status)) {
            status = WCacheRaiseIoError(Cache, Context, status, Lba, PSs, tmp_buff, WCACHE_R_OP, NULL);
            if (!NT_SUCCESS(status)) {
                return status;
            }
        }
    } else if (zero) {
        RtlZeroMemory(tmp_buff, PS);
    }

    // Modify packet
    mod = !read || Cache->DoNotCompare;
    Lba0 = Lba - firstLba;
    for(i=0; i<PSs; i++, Lba0++) {
        if (WCacheGetModFlag(block_array, Lba0) ||
            (!read && WCacheSectorAddr(block_array,Lba0))) {
            if (!mod) {
                mod = (DbgCompareMemory(tmp_buff + (i << BSh),
                            (PVOID)WCacheSectorAddr(block_array, Lba0), BS) != BS);
            }
            if (mod) {
                DbgCopyMemory(tmp_buff + (i << BSh),
                            (PVOID)WCacheSectorAddr(block_array, Lba0), BS);
            }
        }
    }

    // Write packet if modified
    if (mod) {
        status = Cache->WriteProc(IrpContext, Context, tmp_buff, PS, Lba, ReadBytes, 0);
        if (!NT_SUCCESS(status)) {
            status = WCacheRaiseIoError(Cache, Context, status, Lba, PSs, tmp_buff, WCACHE_W_OP, NULL);
        }
    } else {
        (*ReadBytes) = PS;
        return STATUS_SUCCESS;
    }

    return status;
}
} // end WCacheUpdatePacket()

/*
  WCacheFreePacket() releases storage for all Blocks in packet.
  'frame' describes Frame, offset - Block in Frame. offset should be
  aligned on Packet size.
  Internal routine
 */
VOID
WCacheFreePacket(
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN ULONG frame,           // Frame index
    IN PW_CACHE_ENTRY block_array, // Frame
    IN ULONG offs,            // offset in Frame
    IN ULONG PSs              // Packet size (in Blocks)
    )
{
    ULONG i;
    // mark as non-cached & free pool
    for(i=0; i<PSs; i++, offs++) {
        if (WCacheSectorAddr(block_array,offs)) {
            WCacheFreeSector(frame, offs);
        }
    }
} // end WCacheFreePacket()

/*
  being updated.
  In async mode it waits for completion of pre-read requests,
  initiates writes, waits for their completion and returns control to
  caller.
  Internal routine
 */
VOID
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    // FirstWContext (simplified), // pointer to head async IO context
    // PrevWContext (simplified),  // pointer to tail async IO context
    IN BOOLEAN FreePacket
    )
{
    PW_CACHE_ASYNC WContext = (*FirstWContext);
    if (!WContext)
        return;
    PW_CACHE_ASYNC NextWContext;
    ULONG PS = Cache->BlockSize << Cache->PacketSizeSh; // packet size (bytes)
    ULONG PSs = Cache->PacketSize;
    ULONG frame;
    lba_t firstLba;

    // Walk through all chained blocks and wait
    // for completion of read operations.
    // Also invoke writes of already prepared packets.
    while(WContext) {
        if (WContext->Cmd == ASYNC_CMD_UPDATE &&
           WContext->State == ASYNC_STATE_READ) {
            // wait for async read for update
            DbgWaitForSingleObject(&(WContext->PhContext.event), NULL);

            WContext->State = ASYNC_STATE_WRITE;
            WCacheUpdatePacket(IrpContext, Cache, Context, NULL, &WContext, NULL, -1, WContext->Lba, -1, -1,
                               PS, -1, &(WContext->TransferredBytes), TRUE, ASYNC_STATE_WRITE);
        } else
        if (WContext->Cmd == ASYNC_CMD_UPDATE &&
           WContext->State == ASYNC_STATE_WRITE_PRE) {
            // invoke physical write it the packet is prepared for writing
            // by previuous call to WCacheUpdatePacket()
            WContext->State = ASYNC_STATE_WRITE;
            WCacheUpdatePacket(IrpContext, Cache, Context, NULL, &WContext, NULL, -1, WContext->Lba, -1, -1,
                               PS, -1, &(WContext->TransferredBytes), TRUE, ASYNC_STATE_WRITE);
            WContext->State = ASYNC_STATE_DONE;
        } else
        if (WContext->Cmd == ASYNC_CMD_READ &&
           WContext->State == ASYNC_STATE_READ) {
            // wait for async read
            DbgWaitForSingleObject(&(WContext->PhContext.event), NULL);
        }
        WContext = WContext->NextWContext;
    }
    // Walk through all chained blocks and wait
    // and wait for completion of async writes (if any).
    // Also free temporary buffers containing already written blocks.
    WContext = (*FirstWContext);
    while(WContext) {
        NextWContext = WContext->NextWContext;
        if (WContext->Cmd == ASYNC_CMD_UPDATE &&
           WContext->State == ASYNC_STATE_WRITE) {

            if (!Cache->Chained)
                DbgWaitForSingleObject(&(WContext->PhContext.event), NULL);

            frame = WContext->Lba >> Cache->BlocksPerFrameSh;
            firstLba = frame << Cache->BlocksPerFrameSh;

            if (FreePacket) {
                WCacheFreePacket(Cache, frame,
                                Cache->FrameList[frame].Frame,
                                WContext->Lba - firstLba, PSs);
            }
        }
        WCacheFreeAsyncEntry(Cache, WContext);
        WContext = NextWContext;
    }
    (*FirstWContext) = NULL;
    (*PrevWContext) = NULL;

/*
  WCacheCheckLimits() checks if we've enough free Frame- &
  Block-entries under Frame- and Block-limit to feet
  requested Blocks.
  If there is not enough entries, WCache initiates flush & purge
  process to satisfy request.
  This is dispatch routine, which calls
  WCacheCheckLimitsR() or WCacheCheckLimitsRW() depending on
  media type.
  Internal routine
 */
NTSTATUS
WCacheCheckLimits(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    IN lba_t ReqLba,          // first LBA to access/cache
    IN ULONG BCount           // number of Blocks to access/cache
    )
{
/*    if (!Cache->FrameCount || !Cache->BlockCount) {
        ASSERT(!Cache->FrameCount);
        ASSERT(!Cache->BlockCount);
        if (!Cache->FrameCount)
            return STATUS_SUCCESS;
    }*/

    // check if we have reached Frame or Block limit
    if (!Cache->FrameCount && !Cache->BlockCount) {
        return STATUS_SUCCESS;
    }

    // check for empty frames
    if (Cache->FrameCount > (Cache->MaxFrames*3)/4) {
        ULONG frame;
        ULONG i;
        for(i=Cache->FrameCount; i>0; i--) {
            frame = Cache->CachedFramesList[i-1];
            // check if frame is empty
            if (!(Cache->FrameList[frame].BlockCount)) {
                WCacheRemoveFrame(Cache, Context, frame);
            } else {
                ASSERT(Cache->FrameList[frame].Frame);
            }
        }
    }

    if (!Cache->BlockCount) {
        return STATUS_SUCCESS;
    }

    // invoke media-specific limit-checker
    switch(Cache->Mode) {
    case WCACHE_MODE_RAM:
        return WCacheCheckLimitsRAM(IrpContext, Cache, Context, ReqLba, BCount);
    case WCACHE_MODE_ROM:
    case WCACHE_MODE_RW:
        return WCacheCheckLimitsRW(IrpContext, Cache, Context, ReqLba, BCount);
    case WCACHE_MODE_R:
        return WCacheCheckLimitsR(IrpContext, Cache, Context, ReqLba, BCount);
    }
    return STATUS_DRIVER_INTERNAL_ERROR;
} // end WCacheCheckLimits()

/*
  WCacheCheckLimitsRW() implements automatic flush and purge of
  unused blocks to keep enough free cache entries for newly
  read/written blocks for Random Access and ReWritable media
  using Read/Modify/Write technology.
  See also WCacheCheckLimits()
  Internal routine
 */
NTSTATUS
WCacheCheckLimitsRW(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    IN lba_t ReqLba,          // first LBA to access/cache
    IN ULONG BCount           // number of Blocks to access/cache
    )
{
    ULONG frame;
    lba_t firstLba;
    lba_t* List = Cache->CachedBlocksList;
    lba_t lastLba;
    lba_t Lba;
    ULONG firstPos;
    ULONG lastPos;
    ULONG BSh = Cache->BlockSizeSh;
    ULONG BS = Cache->BlockSize;
    ULONG PS = BS << Cache->PacketSizeSh; // packet size (bytes)
    ULONG PSs = Cache->PacketSize;
    ULONG try_count = 0;
    PW_CACHE_ENTRY block_array;
    NTSTATUS status;
    SIZE_T ReadBytes;
    ULONG FreeFrameCount = 0;

    if (Cache->FrameCount >= Cache->MaxFrames) {
        FreeFrameCount = Cache->FramesToKeepFree;
    } else
    if ((Cache->BlockCount + WCacheGetSortedListIndex(Cache->BlockCount, List, ReqLba) +
           BCount - WCacheGetSortedListIndex(Cache->BlockCount, List, ReqLba+BCount)) > Cache->MaxBlocks) {
        // we need free space to grow WCache without flushing data
        // for some period of time
        FreeFrameCount = Cache->FramesToKeepFree;
        goto Try_Another_Frame;
    }
    // remove(flush) some frames
    while((Cache->FrameCount >= Cache->MaxFrames) || FreeFrameCount) {
Try_Another_Frame:
        if (!Cache->FrameCount || !Cache->BlockCount) {
            //ASSERT(!Cache->FrameCount);
            if (Cache->FrameCount) {
                UDFPrint(("ASSERT: Cache->FrameCount = %d, when 0 is expected\n", Cache->FrameCount));
            }
            ASSERT(!Cache->BlockCount);
            if (!Cache->FrameCount)
                break;
        }

        frame = WCacheFindFrameToRelease(Cache);
#if 0
        if (Cache->FrameList[frame].WriteCount) {
            try_count++;
            if (try_count < MAX_TRIES_FOR_NA) goto Try_Another_Frame;
        } else {
            try_count = 0;
        }
#else
        if (Cache->FrameList[frame].UpdateCount) {
            try_count = MAX_TRIES_FOR_NA;
        } else {
            try_count = 0;
        }
#endif

        if (FreeFrameCount)
            FreeFrameCount--;

        firstLba = frame << Cache->BlocksPerFrameSh;
        lastLba = firstLba + Cache->BlocksPerFrame;
        firstPos = WCacheGetSortedListIndex(Cache->BlockCount, List, firstLba);
        lastPos = WCacheGetSortedListIndex(Cache->BlockCount, List, lastLba);
        block_array = Cache->FrameList[frame].Frame;

        if (!block_array) {
            return STATUS_DRIVER_INTERNAL_ERROR;
        }

        while(firstPos < lastPos) {
            // flush packet
            Lba = List[firstPos] & ~(PSs-1);

            // write packet out or prepare and add to chain (if chained mode enabled)
            status = WCacheUpdatePacket(IrpContext, Cache, Context, &FirstWContext, &PrevWContext, block_array, firstLba,
                Lba, BSh, BS, PS, PSs, &ReadBytes, TRUE, ASYNC_STATE_NONE);

            if (status != STATUS_PENDING) {
                // free memory
                WCacheFreePacket(Cache, frame, block_array, Lba-firstLba, PSs);
            }

            Lba += PSs;
            while((firstPos < lastPos) && (Lba > List[firstPos])) {
                firstPos++;
            }
            // write chained packets
            if (chain_count >= WCACHE_MAX_CHAIN) {
            }
        }
        // remove flushed blocks from all lists
        WCacheRemoveRangeFromList(List, &(Cache->BlockCount), firstLba, Cache->BlocksPerFrame);
        ASSERT(ValidateFrameBlocksList(Cache, Lba));
        WCacheRemoveRangeFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), firstLba, Cache->BlocksPerFrame);

        WCacheRemoveFrame(Cache, Context, frame);
    }

    // check if we try to read too much data
    if (!WCacheValidateBlockCount(Cache, BCount)) {
        return STATUS_INVALID_PARAMETER;
    }

    // remove(flush) packet
    while((Cache->BlockCount + WCacheGetSortedListIndex(Cache->BlockCount, List, ReqLba) +
           BCount - WCacheGetSortedListIndex(Cache->BlockCount, List, ReqLba+BCount)) > Cache->MaxBlocks) {
        try_count = 0;
Try_Another_Block:

        Lba = WCacheFindLbaToRelease(Cache) & ~(PSs-1);
        if (Lba == WCACHE_INVALID_LBA) {
            ASSERT(!Cache->FrameCount);
            ASSERT(!Cache->BlockCount);
            break;
        }
        frame = Lba >> Cache->BlocksPerFrameSh;
        firstLba = frame << Cache->BlocksPerFrameSh;
        firstPos = WCacheGetSortedListIndex(Cache->BlockCount, List, Lba);
        lastPos = WCacheGetSortedListIndex(Cache->BlockCount, List, Lba+PSs);
        block_array = Cache->FrameList[frame].Frame;
        if (!block_array) {
            // write already prepared blocks to disk and return error
            ASSERT(FALSE);
            return STATUS_DRIVER_INTERNAL_ERROR;
        }

        // write packet out or prepare and add to chain (if chained mode enabled)
        status = WCacheUpdatePacket(IrpContext, Cache, Context, &FirstWContext, &PrevWContext, block_array, firstLba,
            Lba, BSh, BS, PS, PSs, &ReadBytes, (try_count >= MAX_TRIES_FOR_NA), ASYNC_STATE_NONE);

        if (status == STATUS_RETRY) {
            try_count++;
            goto Try_Another_Block;
        }

        // free memory
        WCacheFreePacket(Cache, frame, block_array, Lba-firstLba, PSs);

        WCacheRemoveRangeFromList(List, &(Cache->BlockCount), Lba, PSs);
        ASSERT(ValidateFrameBlocksList(Cache, Lba));
        WCacheRemoveRangeFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba, PSs);
        // check if frame is empty
        if (!(Cache->FrameList[frame].BlockCount)) {
            WCacheRemoveFrame(Cache, Context, frame);
        } else {
            ASSERT(Cache->FrameList[frame].Frame);
        }
        if (chain_count >= WCACHE_MAX_CHAIN) {
        }
    }
    return STATUS_SUCCESS;
} // end WCacheCheckLimitsRW()

NTSTATUS
WCacheFlushBlocksRAM(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    PW_CACHE_ENTRY block_array,
    lba_t* List,
    ULONG firstPos,
    ULONG lastPos,
    BOOLEAN Purge
    )
{
    ULONG frame;
    lba_t Lba;
    lba_t PrevLba;
    lba_t firstLba;
    PCHAR tmp_buff = NULL;
    ULONG n;
    ULONG BSh = Cache->BlockSizeSh;
    ULONG BS = Cache->BlockSize;
    ULONG PSs = Cache->PacketSize;
    SIZE_T _WrittenBytes;
    NTSTATUS status = STATUS_SUCCESS;

    frame = List[firstPos] >> Cache->BlocksPerFrameSh;
    firstLba = frame << Cache->BlocksPerFrameSh;

    while(firstPos < lastPos) {
        // flush blocks
        ASSERT(Cache->FrameCount <= Cache->MaxFrames);
        Lba = List[firstPos];
        if (!WCacheGetModFlag(block_array, Lba - firstLba)) {
            // free memory
            if (Purge) {
                WCacheFreePacket(Cache, frame, block_array, Lba-firstLba, 1);
            }
            firstPos++;
            continue;
        }
        tmp_buff = Cache->tmp_buff;
        PrevLba = Lba;
        n=1;
        while((firstPos+n < lastPos) &&
              (List[firstPos+n] == PrevLba+1)) {
            PrevLba++;
            if (!WCacheGetModFlag(block_array, PrevLba - firstLba))
                break;
            DbgCopyMemory(tmp_buff + (n << BSh),
                        (PVOID)WCacheSectorAddr(block_array, PrevLba - firstLba),
                        BS);
            n++;
            if (n >= PSs)
                break;
        }
        if (n > 1) {
            DbgCopyMemory(tmp_buff,
                        (PVOID)WCacheSectorAddr(block_array, Lba - firstLba),
                        BS);
        } else {
            tmp_buff = (PCHAR)WCacheSectorAddr(block_array, Lba - firstLba);
        }
        // write sectors out
        status = Cache->WriteProc(IrpContext, Context, tmp_buff, n<<BSh, Lba, &_WrittenBytes, 0);
        if (!NT_SUCCESS(status)) {
            status = WCacheRaiseIoError(Cache, Context, status, Lba, n, tmp_buff, WCACHE_W_OP, NULL);
            if (!NT_SUCCESS(status)) {
            }
        }
        firstPos += n;
        if (Purge) {
            // free memory
            WCacheFreePacket(Cache, frame, block_array, Lba-firstLba, n);
        } else {
            // clear Modified flag
            ULONG i;
            Lba -= firstLba;
            for(i=0; i<n; i++) {
                WCacheClrModFlag(block_array, Lba+i);
            }
        }
    }

    return status;
} // end WCacheFlushBlocksRAM()

/*
  WCacheCheckLimitsRAM() implements automatic flush and purge of
  unused blocks to keep enough free cache entries for newly
  read/written blocks for Random Access media.
  See also WCacheCheckLimits()
  Internal routine
 */
NTSTATUS
WCacheCheckLimitsRAM(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    IN lba_t ReqLba,          // first LBA to access/cache
    IN ULONG BCount           // number of Blocks to access/cache
    )
{
    ULONG frame;
    lba_t firstLba;
    lba_t* List = Cache->CachedBlocksList;
    lba_t lastLba;
    lba_t Lba;
    ULONG firstPos;
    ULONG lastPos;
    ULONG PSs = Cache->PacketSize;
    PW_CACHE_ENTRY block_array;
    ULONG FreeFrameCount = 0;

    if (Cache->FrameCount >= Cache->MaxFrames) {
        FreeFrameCount = Cache->FramesToKeepFree;
    } else
    if ((Cache->BlockCount + WCacheGetSortedListIndex(Cache->BlockCount, List, ReqLba) +
           BCount - WCacheGetSortedListIndex(Cache->BlockCount, List, ReqLba+BCount)) > Cache->MaxBlocks) {
        // we need free space to grow WCache without flushing data
        // for some period of time
        FreeFrameCount = Cache->FramesToKeepFree;
        goto Try_Another_Frame;
    }
    // remove(flush) some frames
    while((Cache->FrameCount >= Cache->MaxFrames) || FreeFrameCount) {
        ASSERT(Cache->FrameCount <= Cache->MaxFrames);
Try_Another_Frame:
        if (!Cache->FrameCount || !Cache->BlockCount) {
            ASSERT(!Cache->FrameCount);
            ASSERT(!Cache->BlockCount);
            if (!Cache->FrameCount)
                break;
        }

        frame = WCacheFindFrameToRelease(Cache);
#if 0
        if (Cache->FrameList[frame].WriteCount) {
            try_count++;
            if (try_count < MAX_TRIES_FOR_NA) goto Try_Another_Frame;
        } else {
            try_count = 0;
        }
#else
/*
        if (Cache->FrameList[frame].UpdateCount) {
            try_count = MAX_TRIES_FOR_NA;
        } else {
            try_count = 0;
        }
*/
#endif

        if (FreeFrameCount)
            FreeFrameCount--;

        firstLba = frame << Cache->BlocksPerFrameSh;
        lastLba = firstLba + Cache->BlocksPerFrame;
        firstPos = WCacheGetSortedListIndex(Cache->BlockCount, List, firstLba);
        lastPos = WCacheGetSortedListIndex(Cache->BlockCount, List, lastLba);
        block_array = Cache->FrameList[frame].Frame;

        if (!block_array) {
            return STATUS_DRIVER_INTERNAL_ERROR;
        }
        WCacheFlushBlocksRAM(IrpContext, Cache, Context, block_array, List, firstPos, lastPos, TRUE);

        WCacheRemoveRangeFromList(List, &(Cache->BlockCount), firstLba, Cache->BlocksPerFrame);
        ASSERT(ValidateFrameBlocksList(Cache, firstLba));
        WCacheRemoveRangeFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), firstLba, Cache->BlocksPerFrame);
        ASSERT(Cache->FrameList[frame].BlockCount == 0);
        WCacheRemoveFrame(Cache, Context, frame);
    }

    // check if we try to read too much data
    if (!WCacheValidateBlockCount(Cache, BCount)) {
        return STATUS_INVALID_PARAMETER;
    }

    // remove(flush) packet
    while(!WCacheCheckCacheSpace(Cache, List, ReqLba, BCount)) {

        ASSERT(Cache->FrameCount <= Cache->MaxFrames);
        Lba = WCacheFindLbaToRelease(Cache) & ~(PSs-1);
        if (Lba == WCACHE_INVALID_LBA) {
            ASSERT(!Cache->FrameCount);
            ASSERT(!Cache->BlockCount);
            break;
        }
        frame = Lba >> Cache->BlocksPerFrameSh;
        firstLba = frame << Cache->BlocksPerFrameSh;
        firstPos = WCacheGetSortedListIndex(Cache->BlockCount, List, Lba);
        lastPos = WCacheGetSortedListIndex(Cache->BlockCount, List, Lba+PSs);
        block_array = Cache->FrameList[frame].Frame;
        if (!block_array) {
            ASSERT(FALSE);
            return STATUS_DRIVER_INTERNAL_ERROR;
        }
        WCacheFlushBlocksRAM(IrpContext, Cache, Context, block_array, List, firstPos, lastPos, TRUE);
        WCacheRemoveRangeFromList(List, &(Cache->BlockCount), Lba, PSs);
        ASSERT(ValidateFrameBlocksList(Cache, Lba));
        WCacheRemoveRangeFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba, PSs);
        // check if frame is empty
        if (!(Cache->FrameList[frame].BlockCount)) {
            WCacheRemoveFrame(Cache, Context, frame);
        } else {
            ASSERT(Cache->FrameList[frame].Frame);
        }
    }
    return STATUS_SUCCESS;
} // end WCacheCheckLimitsRAM()

/*
  WCachePurgeAllRAM()
  Internal routine
 */
NTSTATUS
WCachePurgeAllRAM(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context          // user-supplied context for IO callbacks
    )
{
    ULONG frame;
    lba_t firstLba;
    lba_t* List = Cache->CachedBlocksList;
    lba_t lastLba;
    ULONG firstPos;
    ULONG lastPos;
    PW_CACHE_ENTRY block_array;

    // remove(flush) some frames
    while(Cache->FrameCount) {

        frame = Cache->CachedFramesList[0];

        firstLba = frame << Cache->BlocksPerFrameSh;
        lastLba = firstLba + Cache->BlocksPerFrame;
        firstPos = WCacheGetSortedListIndex(Cache->BlockCount, List, firstLba);
        lastPos = WCacheGetSortedListIndex(Cache->BlockCount, List, lastLba);
        block_array = Cache->FrameList[frame].Frame;

        if (!block_array) {
            return STATUS_DRIVER_INTERNAL_ERROR;
        }
        WCacheFlushBlocksRAM(IrpContext, Cache, Context, block_array, List, firstPos, lastPos, TRUE);

        WCacheRemoveRangeFromList(List, &(Cache->BlockCount), firstLba, Cache->BlocksPerFrame);
        ASSERT(ValidateFrameBlocksList(Cache, firstLba));
        WCacheRemoveRangeFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), firstLba, Cache->BlocksPerFrame);
        WCacheRemoveFrame(Cache, Context, frame);
    }

    ASSERT(!Cache->FrameCount);
    ASSERT(!Cache->BlockCount);
    return STATUS_SUCCESS;
} // end WCachePurgeAllRAM()

/*
  WCacheFlushAllRAM()
  Internal routine
 */
NTSTATUS
WCacheFlushAllRAM(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context          // user-supplied context for IO callbacks
    )
{
    ULONG frame;
    lba_t firstLba;
    lba_t* List = Cache->CachedBlocksList;
    lba_t lastLba;
    ULONG firstPos;
    ULONG lastPos;
    PW_CACHE_ENTRY block_array;

    // flush frames
    while(Cache->WriteCount) {

        frame = Cache->CachedModifiedBlocksList[0] >> Cache->BlocksPerFrameSh;

        firstLba = frame << Cache->BlocksPerFrameSh;
        lastLba = firstLba + Cache->BlocksPerFrame;
        firstPos = WCacheGetSortedListIndex(Cache->BlockCount, List, firstLba);
        lastPos = WCacheGetSortedListIndex(Cache->BlockCount, List, lastLba);
        block_array = Cache->FrameList[frame].Frame;

        if (!block_array) {
            return STATUS_DRIVER_INTERNAL_ERROR;
        }
        WCacheFlushBlocksRAM(IrpContext, Cache, Context, block_array, List, firstPos, lastPos, FALSE);

        WCacheRemoveRangeFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), firstLba, Cache->BlocksPerFrame);
    }

    return STATUS_SUCCESS;
} // end WCacheFlushAllRAM()

/*
  WCachePreReadPacket__() reads & caches the whole packet containing
  requested LBA. This routine just caches data, it doesn't copy anything
  to user buffer.
  In general we have no user buffer here... ;)
  Public routine
*/
NTSTATUS
WCachePreReadPacket__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    IN lba_t Lba              // LBA to cache together with whole packet
    )
{
    ULONG frame;
    NTSTATUS status = STATUS_SUCCESS;
    PW_CACHE_ENTRY block_array;
    ULONG BSh = Cache->BlockSizeSh;
    ULONG BS = Cache->BlockSize;
    PCHAR addr;
    SIZE_T _ReadBytes;
    ULONG PS = Cache->PacketSize; // (in blocks)
    ULONG BCount = PS;
    ULONG i, n, err_count;
    BOOLEAN sector_added = FALSE;
    ULONG block_type;
    BOOLEAN zero = FALSE;//TRUE;
/*
    ULONG first_zero=0, last_zero=0;
    BOOLEAN count_first_zero = TRUE;
*/

    Lba &= ~(PS-1);
    frame = Lba >> Cache->BlocksPerFrameSh;
    i = Lba - (frame << Cache->BlocksPerFrameSh);

    // assume successful operation
    block_array = Cache->FrameList[frame].Frame;
    if (!block_array) {
        ASSERT(Cache->FrameCount < Cache->MaxFrames);
        block_array = WCacheInitFrame(IrpContext, Cache, Context, frame);
        if (!block_array)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    // skip cached extent (if any)
    n=0;
    while((n < BCount) &&
          (n < Cache->BlocksPerFrame)) {

        addr = (PCHAR)WCacheSectorAddr(block_array, i+n);
        block_type = Cache->CheckUsedProc(Context, Lba+n);
        if (/*WCacheGetBadFlag(block_array,i+n)*/
           block_type & WCACHE_BLOCK_BAD) {
            // bad packet. no pre-read
            return STATUS_DEVICE_DATA_ERROR;
        }
        if (!(block_type & WCACHE_BLOCK_ZERO)) {
            zero = FALSE;
            //count_first_zero = FALSE;
            //last_zero = 0;
            if (!addr) {
                // sector is not cached, stop search
                break;
            }
        } else {
/*
            if (count_first_zero) {
                first_zero++;
            }
            last_zero++;
*/
        }
        n++;
    }
    // do nothing if all sectors are already cached
    if (n < BCount) {

        // read whole packet
        if (!zero) {
            status = Cache->ReadProc(IrpContext, Context, Cache->tmp_buff_r, PS<<BSh, Lba, &_ReadBytes, PH_TMP_BUFFER);
            if (!NT_SUCCESS(status)) {
                status = WCacheRaiseIoError(Cache, Context, status, Lba, PS, Cache->tmp_buff_r, WCACHE_R_OP, NULL);
            }
        } else {
            status = STATUS_SUCCESS;
            //RtlZeroMemory(Cache->tmp_buff_r, PS<<BSh);
            _ReadBytes = PS<<BSh;
        }
        if (NT_SUCCESS(status)) {
            // and now we'll copy them to cache
            for(n=0; n<BCount; n++, i++) {
                if (WCacheSectorAddr(block_array,i)) {
                    continue;
                }
                ASSERT(block_array[i].Sector == NULL);
                addr = block_array[i].Sector = (PCHAR)DbgAllocatePoolWithTag(CACHED_BLOCK_MEMORY_TYPE, BS, MEM_WCBUF_TAG);
                if (!addr) {
                    break;
                }
                sector_added = TRUE;
                if (!zero) {
                    DbgCopyMemory(addr, Cache->tmp_buff_r+(n<<BSh), BS);
                } else {
                    RtlZeroMemory(addr, BS);
                }
                Cache->FrameList[frame].BlockCount++;
            }
        } else {
            // read sectors one by one and copy them to cache
            // unreadable sectors will be treated as zero-filled
            err_count = 0;
            for(n=0; n<BCount; n++, i++) {
                if (WCacheSectorAddr(block_array,i)) {
                    continue;
                }
                ASSERT(block_array[i].Sector == NULL);
                addr = block_array[i].Sector = (PCHAR)DbgAllocatePoolWithTag(CACHED_BLOCK_MEMORY_TYPE, BS, MEM_WCBUF_TAG);
                if (!addr) {
                    break;
                }
                sector_added = TRUE;
                status = Cache->ReadProc(IrpContext, Context, Cache->tmp_buff_r, BS, Lba+n, &_ReadBytes, PH_TMP_BUFFER);
                if (!NT_SUCCESS(status)) {
                    status = WCacheRaiseIoError(Cache, Context, status, Lba+n, 1, Cache->tmp_buff_r, WCACHE_R_OP, NULL);
                    if (!NT_SUCCESS(status)) {
                        err_count++;
                    }
                }
                if (!zero && NT_SUCCESS(status)) {
                    DbgCopyMemory(addr, Cache->tmp_buff_r, BS);
                } else
                if (Cache->RememberBB) {
                    RtlZeroMemory(addr, BS);
                    /*
                    if (!NT_SUCCESS(status)) {
                        WCacheSetBadFlag(block_array,i);
                    }
                    */
                }
                Cache->FrameList[frame].BlockCount++;
                if (err_count >= 2) {
                    break;
                }
            }
        }
    }

    // we know the number of unread sectors if an error occured
    // so we can need to update BlockCount
    // return number of read bytes
    if (sector_added) {
        WCacheInsertRangeToList(Cache->CachedBlocksList, &(Cache->BlockCount), Lba, n);
        ASSERT(ValidateFrameBlocksList(Cache, Lba));
    }

    return status;
} // end WCachePreReadPacket__()

/*
  WCacheReadBlocks__() reads data from cache or
  read it form media and store in cache.
  Public routine
 */
NTSTATUS
WCacheReadBlocks__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    IN PCHAR Buffer,          // user-supplied buffer for read blocks
    IN lba_t Lba,             // LBA to start read from
    IN ULONG BCount,          // number of blocks to be read
    OUT PSIZE_T ReadBytes,     // user-supplied pointer to ULONG that will
                              //   recieve number of actually read bytes
    IN BOOLEAN CachedOnly     // specifies that cache is already locked
    )
{
    ULONG frame;
    ULONG i, saved_i, saved_BC = BCount, n;
    NTSTATUS status = STATUS_SUCCESS;
    PW_CACHE_ENTRY block_array;
    ULONG BSh = Cache->BlockSizeSh;
    SIZE_T BS = Cache->BlockSize;
    PCHAR addr;
    ULONG to_read, saved_to_read;
    SIZE_T _ReadBytes;
    ULONG PS = Cache->PacketSize;
    ULONG MaxR = Cache->MaxBytesToRead;
    ULONG PacketMask = PS-1; // here we assume that Packet Size value is 2^n
    ULONG d;
    ULONG block_type;

    WcPrint(("WC:R %x (%x)\n", Lba, BCount));

    (*ReadBytes) = 0;
    // check if we try to read too much data
    if (BCount >= Cache->MaxBlocks) {
        i = 0;
        if (CachedOnly) {
            status = STATUS_INVALID_PARAMETER;
            goto EO_WCache_R2;
        }
        while(TRUE) {
            status = WCacheReadBlocks__(IrpContext, Cache, Context, Buffer + (i<<BSh), Lba, PS, &_ReadBytes, FALSE);
            (*ReadBytes) += _ReadBytes;
            if (!NT_SUCCESS(status) || (BCount <= PS)) break;
            BCount -= PS;
            Lba += PS;
            i += PS;
        }
        return status;
    }
    // check if we try to access beyond cached area
    if ((Lba < Cache->FirstLba) ||
       (Lba + BCount - 1 > Cache->LastLba)) {
        status = Cache->ReadProc(IrpContext, Context, Buffer, BCount, Lba, ReadBytes, 0);
        if (!NT_SUCCESS(status)) {
            status = WCacheRaiseIoError(Cache, Context, status, Lba, BCount, Buffer, WCACHE_R_OP, NULL);
        }
        return status;
    }
    if (!CachedOnly) {
        ExAcquireResourceExclusiveLite(&(Cache->WCacheLock), TRUE);
    }

    frame = Lba >> Cache->BlocksPerFrameSh;
    i = Lba - (frame << Cache->BlocksPerFrameSh);

    if (Cache->CacheWholePacket && (BCount < PS)) {
        if (!CachedOnly &&
           !NT_SUCCESS(status = WCacheCheckLimits(IrpContext, Cache, Context, Lba & ~(PS-1), PS*2)) ) {
            ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
            return status;
        }
    } else {
        if (!CachedOnly &&
           !NT_SUCCESS(status = WCacheCheckLimits(IrpContext, Cache, Context, Lba, BCount))) {
            ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
            return status;
        }
    }
    if (!CachedOnly) {
        // convert to shared
    }

    // pre-read packet. It is very useful for
    // highly fragmented files
    if (Cache->CacheWholePacket && (BCount < PS)) {
        // we should not perform IO if user requested CachedOnly data
        if (!CachedOnly) {
            status = WCachePreReadPacket__(IrpContext, Cache, Context, Lba);
        }
        status = STATUS_SUCCESS;
    }

    // assume successful operation
    block_array = Cache->FrameList[frame].Frame;
    if (!block_array) {
        ASSERT(!CachedOnly);
        ASSERT(Cache->FrameCount < Cache->MaxFrames);
        block_array = WCacheInitFrame(IrpContext, Cache, Context, frame);
        if (!block_array) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto EO_WCache_R;
        }
    }

    Cache->FrameList[frame].AccessCount++;
    while(BCount) {
        if (i >= Cache->BlocksPerFrame) {
            frame++;
            block_array = Cache->FrameList[frame].Frame;
            i -= Cache->BlocksPerFrame;
        }
        if (!block_array) {
            ASSERT(Cache->FrameCount < Cache->MaxFrames);
            block_array = WCacheInitFrame(IrpContext, Cache, Context, frame);
            if (!block_array) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                goto EO_WCache_R;
            }
        }
        // 'read' cached extent (if any)
        // it is just copying
        while(BCount &&
              (i < Cache->BlocksPerFrame) &&
              (addr = (PCHAR)WCacheSectorAddr(block_array, i)) ) {
            block_type = Cache->CheckUsedProc(Context, Lba+saved_BC-BCount);
            if (block_type & WCACHE_BLOCK_BAD) {
            //if (WCacheGetBadFlag(block_array,i)) {
                status = STATUS_DEVICE_DATA_ERROR;
                goto EO_WCache_R;
            }
            DbgCopyMemory(Buffer, addr, BS);
            Buffer += BS;
            *ReadBytes += BS;
            i++;
            BCount--;
        }
        // read non-cached packet-size-aligned extent (if any)
        // now we'll calculate total length & decide if it has enough size
        if (!((d = Lba+saved_BC-BCount) & PacketMask) && d ) {
            n = 0;
            while(BCount &&
                  (i < Cache->BlocksPerFrame) &&
                  (!WCacheSectorAddr(block_array, i)) ) {
                 n++;
                 BCount--;
            }
            BCount += n;
            n &= ~PacketMask;
            if (n>PS) {
                if (!NT_SUCCESS(status = Cache->ReadProc(IrpContext, Context, Buffer, BS*n, Lba+saved_BC-BCount, &_ReadBytes, 0))) {
                    status = WCacheRaiseIoError(Cache, Context, status, Lba+saved_BC-BCount, n, Buffer, WCACHE_R_OP, NULL);
                    if (!NT_SUCCESS(status)) {
                        goto EO_WCache_R;
                    }
                }
                BCount -= n;
                Lba += saved_BC - BCount;
                // If reading non-cached packet-size-aligned data, it is not added to the cache.
                // Therefore, we reset the saved_BC variable to zero in this case.
                saved_BC = BCount;
                i += n;
                Buffer += BS*n;
                *ReadBytes += BS*n;
            }
        }
        // read non-cached extent (if any)
        // firstable, we'll get total number of sectors to read
        to_read = 0;
        saved_i = i;
        d = BCount;
        while(d &&
              (i < Cache->BlocksPerFrame) &&
              (!WCacheSectorAddr(block_array, i)) ) {
            i++;
            to_read += BS;
            d--;
        }
        // read some not cached sectors
        if (to_read) {
            i = saved_i;
            saved_to_read = to_read;
            d = BCount - d;
            // split request if necessary
            if (saved_to_read > MaxR) {
                WCacheInsertRangeToList(Cache->CachedBlocksList, &(Cache->BlockCount), Lba, saved_BC - BCount);
                ASSERT(ValidateFrameBlocksList(Cache, Lba));
                n = MaxR >> BSh;
                do {
                    status = Cache->ReadProc(IrpContext, Context, Buffer, MaxR, i + (frame << Cache->BlocksPerFrameSh), &_ReadBytes, 0);
                    *ReadBytes += _ReadBytes;
                    if (!NT_SUCCESS(status)) {
                        _ReadBytes &= ~(BS-1);
                        BCount -= _ReadBytes >> BSh;
                        saved_to_read -= _ReadBytes;
                        Buffer += _ReadBytes;
                        // Can the variable saved_BC be modified here? Most likely not. This requires debugging.
                        ASSERT(FALSE);
                        saved_BC = BCount;
                        goto store_read_data_1;
                    }
                    Buffer += MaxR;
                    saved_to_read -= MaxR;
                    i += n;
                    BCount -= n;
                    d -= n;
                } while(saved_to_read > MaxR);
                // The variable saved_BC should not be modified, as it holds the original value of BCount
                // and is used by WCacheInsertRangeToList below. Modifying it has led to memory leaks,
                // causing WCacheFlushBlocksRAM to not release all sectors and to delete a block without freeing the memory.
                //saved_BC = BCount; 
            }
            if (saved_to_read) {
                status = Cache->ReadProc(IrpContext, Context, Buffer, saved_to_read, i + (frame << Cache->BlocksPerFrameSh), &_ReadBytes, 0);
                *ReadBytes += _ReadBytes;
                if (!NT_SUCCESS(status)) {
                    _ReadBytes &= ~(BS-1);
                    BCount -= _ReadBytes >> BSh;
                    saved_to_read -= _ReadBytes;
                    Buffer += _ReadBytes;
                    goto store_read_data_1;
                }
                Buffer += saved_to_read;
                saved_to_read = 0;
                BCount -= d;
            }

store_read_data_1:
            // and now we'll copy them to cache

            //
            Buffer -= (to_read - saved_to_read);
            i = saved_i;
            while(to_read - saved_to_read) {
                ASSERT(block_array[i].Sector == NULL);
                block_array[i].Sector = (PCHAR)DbgAllocatePoolWithTag(CACHED_BLOCK_MEMORY_TYPE, BS, MEM_WCBUF_TAG);
                if (!block_array[i].Sector) {
                    BCount += to_read >> BSh;
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    goto EO_WCache_R;
                }
                DbgCopyMemory(block_array[i].Sector, Buffer, BS);
                Cache->FrameList[frame].BlockCount++;
                i++;
                Buffer += BS;
                to_read -= BS;
            }
            if (!NT_SUCCESS(status))
                goto EO_WCache_R;
            to_read = 0;
        }
    }

EO_WCache_R:

    // we know the number of unread sectors if an error occured
    // so we can need to update BlockCount
    // return number of read bytes
    WCacheInsertRangeToList(Cache->CachedBlocksList, &(Cache->BlockCount), Lba, saved_BC - BCount);
    ASSERT(ValidateFrameBlocksList(Cache, Lba));
EO_WCache_R2:
    if (!CachedOnly) {
        ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
    }

    return status;
} // end WCacheReadBlocks__()

/*
  WCacheWriteBlocks__() writes data to cache.
  Data is written directly to media if:
  1) requested block is Packet-aligned
  2) requested Lba(s) lays beyond cached area
  Public routine
 */
NTSTATUS
WCacheWriteBlocks__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    IN PCHAR Buffer,          // user-supplied buffer containing data to be written
    IN lba_t Lba,             // LBA to start write from
    IN ULONG BCount,          // number of blocks to be written
    OUT PSIZE_T WrittenBytes,  // user-supplied pointer to ULONG that will
                              //   recieve number of actually written bytes
    IN BOOLEAN CachedOnly     // specifies that cache is already locked
    )
{
    ULONG frame;
    ULONG i, saved_BC = BCount, n, d;
    NTSTATUS status = STATUS_SUCCESS;
    PW_CACHE_ENTRY block_array;
    ULONG BSh = Cache->BlockSizeSh;
    ULONG BS = Cache->BlockSize;
    PCHAR addr;
    SIZE_T _WrittenBytes;
    ULONG PS = Cache->PacketSize;
    ULONG PacketMask = PS-1; // here we assume that Packet Size value is 2^n
    ULONG block_type;

    BOOLEAN WriteThrough = FALSE;
    lba_t   WTh_Lba;
    ULONG   WTh_BCount;

    WcPrint(("WC:W %x (%x)\n", Lba, BCount));

    *WrittenBytes = 0;
    // check if we try to read too much data
    if (BCount >= Cache->MaxBlocks) {
        i = 0;
        if (CachedOnly) {
            status = STATUS_INVALID_PARAMETER;
            goto EO_WCache_W2;
        }
        while(TRUE) {
            status = WCacheWriteBlocks__(IrpContext, Cache, Context, Buffer + (i<<BSh), Lba, min(PS,BCount), &_WrittenBytes, FALSE);
            (*WrittenBytes) += _WrittenBytes;
            BCount -= PS;
            Lba += PS;
            i += PS;
            if (!NT_SUCCESS(status) || (BCount < PS))
                return status;
        }
    }
    // check if we try to access beyond cached area
    if ((Lba < Cache->FirstLba) ||
       (Lba + BCount - 1 > Cache->LastLba)) {
        return STATUS_INVALID_PARAMETER;
    }
    if (!CachedOnly) {
        ExAcquireResourceExclusiveLite(&(Cache->WCacheLock), TRUE);
    }

    frame = Lba >> Cache->BlocksPerFrameSh;
    i = Lba - (frame << Cache->BlocksPerFrameSh);

    if (!CachedOnly &&
       !NT_SUCCESS(status = WCacheCheckLimits(IrpContext, Cache, Context, Lba, BCount))) {
        ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
        return status;
    }

    // assume successful operation
    block_array = Cache->FrameList[frame].Frame;
    if (!block_array) {

        if (BCount && !(BCount & (PS-1)) && !(Lba & (PS-1)) &&
           (Cache->Mode != WCACHE_MODE_R) &&
           (i+BCount <= Cache->BlocksPerFrame) &&
            !Cache->NoWriteThrough) {
            status = Cache->WriteProc(IrpContext, Context, Buffer, BCount<<BSh, Lba, WrittenBytes, 0);
            if (!NT_SUCCESS(status)) {
                status = WCacheRaiseIoError(Cache, Context, status, Lba, BCount, Buffer, WCACHE_W_OP, NULL);
            }
            goto EO_WCache_W2;
        }

        ASSERT(!CachedOnly);
        ASSERT(Cache->FrameCount < Cache->MaxFrames);
        block_array = WCacheInitFrame(IrpContext, Cache, Context, frame);
        if (!block_array) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto EO_WCache_W;
        }
    }

    if (Cache->Mode == WCACHE_MODE_RAM &&
       BCount &&
       (!(BCount & (PS-1)) || (BCount > PS)) ) {
        WriteThrough = TRUE;
        WTh_Lba = Lba;
        WTh_BCount = BCount;
    } else
    if (Cache->Mode == WCACHE_MODE_RAM &&
       ((Lba & ~PacketMask) != ((Lba+BCount-1) & ~PacketMask))
      ) {
        WriteThrough = TRUE;
        WTh_Lba = Lba & ~PacketMask;
        WTh_BCount = PS;
    }

    Cache->FrameList[frame].UpdateCount++;
    while(BCount) {
        if (i >= Cache->BlocksPerFrame) {
            frame++;
            block_array = Cache->FrameList[frame].Frame;
            i -= Cache->BlocksPerFrame;
        }
        if (!block_array) {
            ASSERT(Cache->FrameCount < Cache->MaxFrames);
            block_array = WCacheInitFrame(IrpContext, Cache, Context, frame);
            if (!block_array) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                goto EO_WCache_W;
            }
        }
        // 'write' cached extent (if any)
        // it is just copying
        while(BCount &&
              (i < Cache->BlocksPerFrame) &&
              (addr = (PCHAR)WCacheSectorAddr(block_array, i)) ) {
            block_type = Cache->CheckUsedProc(Context, Lba+saved_BC-BCount);
            if (Cache->NoWriteBB &&
               /*WCacheGetBadFlag(block_array,i)*/
               (block_type & WCACHE_BLOCK_BAD)) {
                // bad packet. no cached write
                status = STATUS_DEVICE_DATA_ERROR;
                goto EO_WCache_W;
            }
            DbgCopyMemory(addr, Buffer, BS);
            WCacheSetModFlag(block_array, i);
            Buffer += BS;
            *WrittenBytes += BS;
            i++;
            BCount--;
        }
        // write non-cached not-aligned extent (if any) till aligned one
        while(BCount &&
              (i & PacketMask) &&
              (Cache->Mode != WCACHE_MODE_R) &&
              (i < Cache->BlocksPerFrame) &&
              (!WCacheSectorAddr(block_array, i)) ) {
            ASSERT(block_array[i].Sector == NULL);
            block_array[i].Sector = (PCHAR)DbgAllocatePoolWithTag(CACHED_BLOCK_MEMORY_TYPE, BS, MEM_WCBUF_TAG);
            if (!block_array[i].Sector) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                goto EO_WCache_W;
            }
            DbgCopyMemory(block_array[i].Sector, Buffer, BS);
            WCacheSetModFlag(block_array, i);
            i++;
            Buffer += BS;
            *WrittenBytes += BS;
            BCount--;
            Cache->FrameList[frame].BlockCount ++;
        }
        // write non-cached packet-size-aligned extent (if any)
        // now we'll calculate total length & decide if has enough size
        if (!Cache->NoWriteThrough
                     &&
           ( !(i & PacketMask) ||
             ((Cache->Mode == WCACHE_MODE_R) && (BCount >= PS)) )) {
            n = 0;
            while(BCount &&
                  (i < Cache->BlocksPerFrame) &&
                  (!WCacheSectorAddr(block_array, i)) ) {
                 n++;
                 BCount--;
            }
            BCount += n;
            n &= ~PacketMask;
            if (n) {
                // add previously written data to list
                d = saved_BC - BCount;
                WCacheInsertRangeToList(Cache->CachedBlocksList, &(Cache->BlockCount), Lba, d);
                ASSERT(ValidateFrameBlocksList(Cache, Lba));
                WCacheInsertRangeToList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba, d);
                Lba += d;
                saved_BC = BCount;

                while(n) {
                    if (Cache->Mode == WCACHE_MODE_R)
                        Cache->UpdateRelocProc(Context, Lba, NULL, PS);
                    if (!NT_SUCCESS(status = Cache->WriteProc(IrpContext, Context, Buffer, PS<<BSh, Lba, &_WrittenBytes, 0))) {
                        status = WCacheRaiseIoError(Cache, Context, status, Lba, PS, Buffer, WCACHE_W_OP, NULL);
                        if (!NT_SUCCESS(status)) {
                            goto EO_WCache_W;
                        }
                    }
                    BCount -= PS;
                    Lba += PS;
                    saved_BC = BCount;
                    i += PS;
                    Buffer += PS<<BSh;
                    *WrittenBytes += PS<<BSh;
                    n-=PS;
                }
            }
        }
        // write non-cached not-aligned extent (if any)
        while(BCount &&
              (i < Cache->BlocksPerFrame) &&
              (!WCacheSectorAddr(block_array, i)) ) {
            ASSERT(block_array[i].Sector == NULL);
            block_array[i].Sector = (PCHAR)DbgAllocatePoolWithTag(CACHED_BLOCK_MEMORY_TYPE, BS, MEM_WCBUF_TAG);
            if (!block_array[i].Sector) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                goto EO_WCache_W;
            }
            DbgCopyMemory(block_array[i].Sector, Buffer, BS);
            WCacheSetModFlag(block_array, i);
            i++;
            Buffer += BS;
            *WrittenBytes += BS;
            BCount--;
            Cache->FrameList[frame].BlockCount ++;
        }
    }

EO_WCache_W:

    // we know the number of unread sectors if an error occured
    // so we can need to update BlockCount
    // return number of read bytes
    WCacheInsertRangeToList(Cache->CachedBlocksList, &(Cache->BlockCount), Lba, saved_BC - BCount);
    ASSERT(ValidateFrameBlocksList(Cache, Lba));
    WCacheInsertRangeToList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba, saved_BC - BCount);

    if (WriteThrough && !BCount) {
        ULONG d;
        ULONG firstPos;
        ULONG lastPos;

        BCount = WTh_BCount;
        Lba = WTh_Lba;
        while(BCount) {
            frame = Lba >> Cache->BlocksPerFrameSh;
            firstPos = WCacheGetSortedListIndex(Cache->BlockCount, Cache->CachedBlocksList, Lba);
            d = min(Lba+BCount, (frame+1) << Cache->BlocksPerFrameSh) - Lba;
            lastPos = WCacheGetSortedListIndex(Cache->BlockCount, Cache->CachedBlocksList, Lba+d);
            block_array = Cache->FrameList[frame].Frame;
            if (!block_array) {
                // write was non-cached, so skip this cache frame without asserting
                // ASSERT(FALSE); 
                BCount -= d;
                Lba += d;
                continue;
            }
            status = WCacheFlushBlocksRAM(IrpContext, Cache, Context, block_array, Cache->CachedBlocksList, firstPos, lastPos, FALSE);
            WCacheRemoveRangeFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba, d);
            BCount -= d;
            Lba += d;
        }
    }

EO_WCache_W2:

    if (!CachedOnly) {
        ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
    }
    return status;
} // end WCacheWriteBlocks__()

/*
  WCacheFlushAll__() copies all data stored in cache to media.
  Flushed blocks are kept in cache.
  Public routine
 */
VOID
WCacheFlushAll__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context)         // user-supplied context for IO callbacks
{
    if (!(Cache->ReadProc)) return;
    ExAcquireResourceExclusiveLite(&(Cache->WCacheLock), TRUE);

    switch(Cache->Mode) {
    case WCACHE_MODE_RAM:
        WCacheFlushAllRAM(IrpContext, Cache, Context);
        break;
    case WCACHE_MODE_ROM:
    case WCACHE_MODE_RW:
        WCacheFlushAllRW(IrpContext, Cache, Context);
        break;
    case WCACHE_MODE_R:
        WCachePurgeAllR(IrpContext, Cache, Context);
        break;
    }

    ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
    return;
} // end WCacheFlushAll__()

/*
  WCachePurgeAll__() copies all data stored in cache to media.
  Flushed blocks are removed cache.
  Public routine
 */
VOID
WCachePurgeAll__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context)         // user-supplied context for IO callbacks
{
    if (!(Cache->ReadProc)) return;
    ExAcquireResourceExclusiveLite(&(Cache->WCacheLock), TRUE);

    switch(Cache->Mode) {
    case WCACHE_MODE_RAM:
        WCachePurgeAllRAM(IrpContext, Cache, Context);
        break;
    case WCACHE_MODE_ROM:
    case WCACHE_MODE_RW:
        WCachePurgeAllRW(IrpContext, Cache, Context);
        break;
    case WCACHE_MODE_R:
        WCachePurgeAllR(IrpContext, Cache, Context);
        break;
    }

    ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
    return;
} // end WCachePurgeAll__()
/*
  WCachePurgeAllRW() copies modified blocks from cache to media
  and removes them from cache
  This routine can be used for RAM, RW and ROM media.
  For ROM media blocks are just removed.
  Internal routine
 */
VOID
WCachePurgeAllRW(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context)         // user-supplied context for IO callbacks
{
    ULONG frame;
    lba_t firstLba;
    lba_t* List = Cache->CachedBlocksList;
    lba_t Lba;
    ULONG BSh = Cache->BlockSizeSh;
    ULONG BS = Cache->BlockSize;
    ULONG PS = BS << Cache->PacketSizeSh; // packet size (bytes)
    ULONG PSs = Cache->PacketSize;
    PW_CACHE_ENTRY block_array;
    SIZE_T ReadBytes;

    if (!(Cache->ReadProc)) return;

    while(Cache->BlockCount) {
        Lba = List[0] & ~(PSs-1);
        frame = Lba >> Cache->BlocksPerFrameSh;
        firstLba = frame << Cache->BlocksPerFrameSh;
        block_array = Cache->FrameList[frame].Frame;
        if (!block_array) {
            return;
        }

        WCacheUpdatePacket(IrpContext, Cache, Context, &FirstWContext, &PrevWContext, block_array, firstLba,
            Lba, BSh, BS, PS, PSs, &ReadBytes, TRUE, ASYNC_STATE_NONE);

        // free memory
        WCacheFreePacket(Cache, frame, block_array, Lba-firstLba, PSs);

        WCacheRemoveRangeFromList(List, &(Cache->BlockCount), Lba, PSs);
        ASSERT(ValidateFrameBlocksList(Cache, Lba));
        WCacheRemoveRangeFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba, PSs);
        // check if frame is empty
        if (!(Cache->FrameList[frame].BlockCount)) {
            WCacheRemoveFrame(Cache, Context, frame);
        } else {
            ASSERT(Cache->FrameList[frame].Frame);
        }
        if (chain_count >= WCACHE_MAX_CHAIN) {
        }
    }
    return;
} // end WCachePurgeAllRW()

/*
  WCacheFlushAllRW() copies modified blocks from cache to media.
  All blocks are not removed from cache.
  This routine can be used for RAM, RW and ROM media.
  Internal routine
 */
VOID
WCacheFlushAllRW(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context)         // user-supplied context for IO callbacks
{
    ULONG frame;
    lba_t firstLba;
    lba_t* List = Cache->CachedModifiedBlocksList;
    lba_t Lba;
    ULONG BSh = Cache->BlockSizeSh;
    ULONG BS = Cache->BlockSize;
    ULONG PS = BS << Cache->PacketSizeSh; // packet size (bytes)
    ULONG PSs = Cache->PacketSize;
    ULONG BFs = Cache->BlocksPerFrameSh;
    PW_CACHE_ENTRY block_array;
    SIZE_T ReadBytes;
    ULONG i;

    if (!(Cache->ReadProc)) return;

    // walk through modified blocks
    while(Cache->WriteCount) {
        Lba = List[0] & ~(PSs-1);
        frame = Lba >> BFs;
        firstLba = frame << BFs;
        block_array = Cache->FrameList[frame].Frame;
        if (!block_array) {
            continue;;
        }
        // queue modify request
        WCacheUpdatePacket(IrpContext, Cache, Context, &FirstWContext, &PrevWContext, block_array, firstLba,
            Lba, BSh, BS, PS, PSs, &ReadBytes, TRUE, ASYNC_STATE_NONE);
        // clear MODIFIED flag for queued blocks
        WCacheRemoveRangeFromList(List, &(Cache->WriteCount), Lba, PSs);
        Lba -= firstLba;
        for(i=0; i<PSs; i++) {
            WCacheClrModFlag(block_array, Lba+i);
        }
        // check queue size
        if (chain_count >= WCACHE_MAX_CHAIN) {
        }
    }
#ifdef DBG
#if 1
    // check consistency
    List = Cache->CachedBlocksList;
    for(i=0; i<Cache->BlockCount; i++) {
        Lba = List[i] /*& ~(PSs-1)*/;
        frame = Lba >> Cache->BlocksPerFrameSh;
        firstLba = frame << Cache->BlocksPerFrameSh;
        block_array = Cache->FrameList[frame].Frame;
        if (!block_array) {
        }
        ASSERT(!WCacheGetModFlag(block_array, Lba-firstLba));
    }
#endif // 1
#endif // DBG
    return;
} // end WCacheFlushAllRW()

/*
  WCacheRelease__() frees all allocated memory blocks and
  deletes synchronization resources
  Public routine
 */
VOID
WCacheRelease__(
    IN PW_CACHE Cache         // pointer to the Cache Control structure
    )
{
    ULONG i, j, k;
    PW_CACHE_ENTRY block_array;

    Cache->Tag = 0xDEADCACE;
    if (!(Cache->ReadProc)) return;
    ExAcquireResourceExclusiveLite(&(Cache->WCacheLock), TRUE);
    for(i=0; i<Cache->FrameCount; i++) {
        j = Cache->CachedFramesList[i];
        block_array = Cache->FrameList[j].Frame;
        if (block_array) {
            for(k=0; k<Cache->BlocksPerFrame; k++) {
                if (WCacheSectorAddr(block_array, k)) {
                    WCacheFreeSector(j, k);
                }
            }
            MyFreePool__(block_array);
        }
    }
    if (Cache->FrameList)
        MyFreePool__(Cache->FrameList);
    if (Cache->CachedBlocksList)
        MyFreePool__(Cache->CachedBlocksList);
    if (Cache->CachedModifiedBlocksList)
        MyFreePool__(Cache->CachedModifiedBlocksList);
    if (Cache->CachedFramesList)
        MyFreePool__(Cache->CachedFramesList);
    if (Cache->tmp_buff_r)
        MyFreePool__(Cache->tmp_buff_r);
    if (Cache->CachedFramesList)
        MyFreePool__(Cache->tmp_buff);
    if (Cache->CachedFramesList)
        MyFreePool__(Cache->reloc_tab);
    ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
    ExDeleteResourceLite(&(Cache->WCacheLock));
    RtlZeroMemory(Cache, sizeof(W_CACHE));
    return;
} // end WCacheRelease__()

/*
  WCacheIsInitialized__() checks if the pointer supplied points
  to initialized cache structure.
  Public routine
 */
BOOLEAN
WCacheIsInitialized__(
    IN PW_CACHE Cache
    )
{
    return (Cache->ReadProc != NULL);
} // end WCacheIsInitialized__()

NTSTATUS
WCacheFlushBlocksRW(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    IN lba_t _Lba,             // LBA to start flush from
    IN ULONG BCount           // number of blocks to be flushed
    )
{
    ULONG frame;
    lba_t firstLba;
    lba_t* List = Cache->CachedModifiedBlocksList;
    lba_t Lba;
    ULONG BSh = Cache->BlockSizeSh;
    ULONG BS = Cache->BlockSize;
    ULONG PS = BS << Cache->PacketSizeSh; // packet size (bytes)
    ULONG PSs = Cache->PacketSize;
    ULONG BFs = Cache->BlocksPerFrameSh;
    PW_CACHE_ENTRY block_array;
    SIZE_T ReadBytes;
    ULONG i;
    lba_t lim;

    if (!(Cache->ReadProc)) return STATUS_INVALID_PARAMETER;

    // walk through modified blocks
    lim = (_Lba+BCount+PSs-1) & ~(PSs-1);
    for(Lba = _Lba & ~(PSs-1);Lba < lim ; Lba += PSs) {
        frame = Lba >> BFs;
        firstLba = frame << BFs;
        block_array = Cache->FrameList[frame].Frame;
        if (!block_array) {
            // not cached block may be requested for flush
            Lba += (1 << BFs) - PSs;
            continue;
        }
        // queue modify request
        WCacheUpdatePacket(IrpContext, Cache, Context, &FirstWContext, &PrevWContext, block_array, firstLba,
            Lba, BSh, BS, PS, PSs, &ReadBytes, TRUE, ASYNC_STATE_NONE);
        // clear MODIFIED flag for queued blocks
        WCacheRemoveRangeFromList(List, &(Cache->WriteCount), Lba, PSs);
        Lba -= firstLba;
        for(i=0; i<PSs; i++) {
            WCacheClrModFlag(block_array, Lba+i);
        }
        Lba += firstLba;
        // check queue size
        if (chain_count >= WCACHE_MAX_CHAIN) {
        }
    }
/*
    if (Cache->Mode != WCACHE_MODE_RAM)
        return STATUS_SUCCESS;
*/

    return STATUS_SUCCESS;
} // end WCacheFlushBlocksRW()

/*
  WCacheFlushBlocks__() copies specified blocks stored in cache to media.
  Flushed blocks are kept in cache.
  Public routine
 */
NTSTATUS
WCacheFlushBlocks__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    IN lba_t Lba,             // LBA to start flush from
    IN ULONG BCount           // number of blocks to be flushed
    )
{
    NTSTATUS status = STATUS_INVALID_PARAMETER;

    if (!(Cache->ReadProc)) return STATUS_INVALID_PARAMETER;
    ExAcquireResourceExclusiveLite(&(Cache->WCacheLock), TRUE);

    // check if we try to access beyond cached area
    if ((Lba < Cache->FirstLba) ||
       (Lba+BCount-1 > Cache->LastLba)) {
        UDFPrint(("LBA %#x (%x) is beyond cacheable area\n", Lba, BCount));
        status = STATUS_INVALID_PARAMETER;
        goto EO_WCache_F;
    }

    switch(Cache->Mode) {
    case WCACHE_MODE_RAM:
    case WCACHE_MODE_ROM:
    case WCACHE_MODE_RW:
        status = WCacheFlushBlocksRW(IrpContext, Cache, Context, Lba, BCount);
        break;
    case WCACHE_MODE_R:
        status = STATUS_SUCCESS;
        break;
    }
EO_WCache_F:
    ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
    return status;
} // end WCacheFlushBlocks__()

/*
  WCacheDirect__() returns pointer to memory block where
  requested block is stored in.
  If no #CachedOnly flag specified this routine locks cache,
  otherwise it assumes that cache is already locked by previous call
  to WCacheStartDirect__().
  Cache can be unlocked by WCacheEODirect__().
  Using this routine caller can access cached block directly in memory
  without Read_to_Tmp and Modify/Write steps.
  Public routine
 */
NTSTATUS
WCacheDirect__(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    IN lba_t Lba,             // LBA of block to get pointer to
    IN BOOLEAN Modified,      // indicates that block will be modified
    OUT PCHAR* CachedBlock,   // address for pointer to cached block to be stored in
    IN BOOLEAN CachedOnly     // specifies that cache is already locked
    )
{
    ULONG frame;
    ULONG i;
    NTSTATUS status = STATUS_SUCCESS;
    PW_CACHE_ENTRY block_array;
    ULONG BS = Cache->BlockSize;
    PCHAR addr;
    SIZE_T _ReadBytes;
    ULONG block_type;

    WcPrint(("WC:%sD %x (1)\n", Modified ? "W" : "R", Lba));

    // lock cache if nececcary
    if (!CachedOnly) {
        ExAcquireResourceExclusiveLite(&(Cache->WCacheLock), TRUE);
    }
    // check if we try to access beyond cached area
    if ((Lba < Cache->FirstLba) ||
       (Lba > Cache->LastLba)) {
        UDFPrint(("LBA %#x is beyond cacheable area\n", Lba));
        status = STATUS_INVALID_PARAMETER;
        goto EO_WCache_D;
    }

    frame = Lba >> Cache->BlocksPerFrameSh;
    i = Lba - (frame << Cache->BlocksPerFrameSh);
    // check if we have enough space to store requested block
    if (!CachedOnly &&
       !NT_SUCCESS(status = WCacheCheckLimits(IrpContext, Cache, Context, Lba, 1))) {
        goto EO_WCache_D;
    }

    // small updates are more important
    block_array = Cache->FrameList[frame].Frame;
    if (Modified) {
        Cache->FrameList[frame].UpdateCount+=8;
    } else {
        Cache->FrameList[frame].AccessCount+=8;
    }
    if (!block_array) {
        ASSERT(Cache->FrameCount < Cache->MaxFrames);
        block_array = WCacheInitFrame(IrpContext, Cache, Context, frame);
        if (!block_array) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto EO_WCache_D;
        }
    }
    // check if requested block is already cached
    if ( !(addr = (PCHAR)WCacheSectorAddr(block_array, i)) ) {
        // block is not cached
        // allocate memory and read block from media
        // do not set block_array[i].Sector here, because if media access fails and recursive access to cache
        // comes, this block should not be marked as 'cached'
        addr = (PCHAR)DbgAllocatePoolWithTag(CACHED_BLOCK_MEMORY_TYPE, BS, MEM_WCBUF_TAG);
        if (!addr) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto EO_WCache_D;
        }
        block_type = Cache->CheckUsedProc(Context, Lba);
        if (block_type == WCACHE_BLOCK_USED) {
            status = Cache->ReadProc(IrpContext, Context, addr, BS, Lba, &_ReadBytes, PH_TMP_BUFFER);
            if (Cache->RememberBB) {
                if (!NT_SUCCESS(status)) {
                    RtlZeroMemory(addr, BS);
                    //WCacheSetBadFlag(block_array,i);
                }
            }
        } else {
            if (block_type & WCACHE_BLOCK_BAD) {
                DbgFreePool(addr);
                addr = NULL;
                status = STATUS_DEVICE_DATA_ERROR;
                goto EO_WCache_D;
            }
            if (!(block_type & WCACHE_BLOCK_ZERO)) {
            }
            status = STATUS_SUCCESS;
            RtlZeroMemory(addr, BS);
        }
        // now add pointer to buffer to common storage
        ASSERT(block_array[i].Sector == NULL);
        block_array[i].Sector = addr;
        WCacheInsertItemToList(Cache->CachedBlocksList, &(Cache->BlockCount), Lba);
        if (Modified) {
            WCacheInsertItemToList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba);
            WCacheSetModFlag(block_array, i);
        }
        Cache->FrameList[frame].BlockCount ++;
        ASSERT(ValidateFrameBlocksList(Cache, Lba));
    } else {
        // block is not cached
        // just return pointer
        block_type = Cache->CheckUsedProc(Context, Lba);
        if (block_type & WCACHE_BLOCK_BAD) {
        //if (WCacheGetBadFlag(block_array,i)) {
            // bad packet. no pre-read
            status = STATUS_DEVICE_DATA_ERROR;
            goto EO_WCache_D;
        }
#ifndef UDF_CHECK_UTIL
        ASSERT(block_type & WCACHE_BLOCK_USED);
#else
        if (!(block_type & WCACHE_BLOCK_USED)) {
            UDFPrint(("LBA %#x is not marked as used\n", Lba));
        }
#endif
        if (Modified &&
           !WCacheGetModFlag(block_array, i)) {
            WCacheInsertItemToList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba);
            WCacheSetModFlag(block_array, i);
        }
    }
    (*CachedBlock) = addr;

EO_WCache_D:

    return status;
} // end WCacheDirect__()

/*
  WCacheEODirect__() must be used to unlock cache after calls to
  to WCacheStartDirect__().
  Public routine
 */
NTSTATUS
WCacheEODirect__(
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context          // user-supplied context for IO callbacks
    )
{
    ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
    return STATUS_SUCCESS;
} // end WCacheEODirect__()

/*
  WCacheStartDirect__() locks cache for exclusive use.
  Using this routine caller can access cached block directly in memory
  without Read_to_Tmp and Modify/Write steps.
  See also WCacheDirect__()
  Cache can be unlocked by WCacheEODirect__().
  Public routine
 */
NTSTATUS
WCacheStartDirect__(
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    IN BOOLEAN Exclusive      // lock cache for exclusive use,
                              //   currently must be TRUE.
    )
{
    if (Exclusive) {
        ExAcquireResourceExclusiveLite(&(Cache->WCacheLock), TRUE);
    } else {
        ExAcquireResourceSharedLite(&(Cache->WCacheLock), TRUE);
    }
    return STATUS_SUCCESS;
} // end WCacheStartDirect__()

/*
  WCacheIsCached__() checks if requested blocks are immediately available.
  Cache must be previously locked for exclusive use with WCacheStartDirect__().
  Using this routine caller can access cached block directly in memory
  without Read_to_Tmp and Modify/Write steps.
  See also WCacheDirect__().
  Cache can be unlocked by WCacheEODirect__().
  Public routine
 */
BOOLEAN
WCacheIsCached__(
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN lba_t Lba,             // LBA to start check from
    IN ULONG BCount           // number of blocks to be checked
    )
{
    ULONG frame;
    ULONG i;
    PW_CACHE_ENTRY block_array;

    // check if we try to access beyond cached area
    if ((Lba < Cache->FirstLba) ||
       (Lba + BCount - 1 > Cache->LastLba)) {
        return FALSE;
    }

    frame = Lba >> Cache->BlocksPerFrameSh;
    i = Lba - (frame << Cache->BlocksPerFrameSh);

    block_array = Cache->FrameList[frame].Frame;
    if (!block_array) {
        return FALSE;
    }

    while(BCount) {
        if (i >= Cache->BlocksPerFrame) {
            frame++;
            block_array = Cache->FrameList[frame].Frame;
            i -= Cache->BlocksPerFrame;
        }
        if (!block_array) {
            return FALSE;
        }
        // 'read' cached extent (if any)
        while(BCount &&
              (i < Cache->BlocksPerFrame) &&
              WCacheSectorAddr(block_array, i) &&
              /*!WCacheGetBadFlag(block_array, i)*/
              /*!(Cache->CheckUsedProc(Context, Lba) & WCACHE_BLOCK_BAD)*/
              TRUE ) {
            i++;
            BCount--;
            Lba++;
        }
        if (BCount &&
              (i < Cache->BlocksPerFrame) /*&&
              (!WCacheSectorAddr(block_array, i))*/ ) {
            return FALSE;
        }
    }
    return TRUE;
} // end WCacheIsCached__()

/*
  WCacheCheckLimitsR() implements automatic flush and purge of
  unused blocks to keep enough free cache entries for newly
  read/written blocks for WORM media.
  See also WCacheCheckLimits()
  Internal routine
 */
NTSTATUS
WCacheCheckLimitsR(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context,         // user-supplied context for IO callbacks
    IN lba_t ReqLba,          // first LBA to access/cache
    IN ULONG BCount           // number of Blocks to access/cache
    )
{
    ULONG frame;
    lba_t firstLba;
    lba_t* List = Cache->CachedBlocksList;
    lba_t Lba;
    PCHAR tmp_buff = Cache->tmp_buff;
    ULONG firstPos;
    ULONG BSh = Cache->BlockSizeSh;
    ULONG BS = Cache->BlockSize;
    ULONG PS = BS << Cache->PacketSizeSh; // packet size (bytes)
    ULONG PSs = Cache->PacketSize;
    ULONG i;
    PW_CACHE_ENTRY block_array;
    BOOLEAN mod;
    NTSTATUS status;
    SIZE_T ReadBytes;
    ULONG MaxReloc = Cache->PacketSize;
    PULONG reloc_tab = Cache->reloc_tab;

    // check if we try to read too much data
    if (BCount > Cache->MaxBlocks) {
        return STATUS_INVALID_PARAMETER;
    }

    // remove(flush) packets from entire frame(s)
    while( ((Cache->BlockCount + WCacheGetSortedListIndex(Cache->BlockCount, List, ReqLba) +
             BCount - WCacheGetSortedListIndex(Cache->BlockCount, List, ReqLba+BCount)) > Cache->MaxBlocks) ||
           (Cache->FrameCount >= Cache->MaxFrames) ) {

WCCL_retry_1:

        Lba = WCacheFindLbaToRelease(Cache);
        if (Lba == WCACHE_INVALID_LBA) {
            ASSERT(!Cache->FrameCount);
            ASSERT(!Cache->BlockCount);
            break;
        }
        frame = Lba >> Cache->BlocksPerFrameSh;
        firstLba = frame << Cache->BlocksPerFrameSh;
        firstPos = WCacheGetSortedListIndex(Cache->BlockCount, List, Lba);
        block_array = Cache->FrameList[frame].Frame;
        if (!block_array) {
            return STATUS_DRIVER_INTERNAL_ERROR;
        }
        // check if modified
        mod = WCacheGetModFlag(block_array, Lba - firstLba);
        // read/modify/write
        if (mod && (Cache->CheckUsedProc(Context, Lba) & WCACHE_BLOCK_USED)) {
            if (Cache->WriteCount < MaxReloc) goto WCCL_retry_1;
            firstPos = WCacheGetSortedListIndex(Cache->WriteCount, Cache->CachedModifiedBlocksList, Lba);
            if (!block_array) {
                return STATUS_DRIVER_INTERNAL_ERROR;
            }
            // prepare packet & reloc table
            for(i=0; i<MaxReloc; i++) {
                Lba = Cache->CachedModifiedBlocksList[firstPos];
                frame = Lba >> Cache->BlocksPerFrameSh;
                firstLba = frame << Cache->BlocksPerFrameSh;
                block_array = Cache->FrameList[frame].Frame;
                DbgCopyMemory(tmp_buff + (i << BSh),
                              (PVOID)WCacheSectorAddr(block_array, Lba-firstLba),
                              BS);
                reloc_tab[i] = Lba;
                WCacheRemoveItemFromList(List, &(Cache->BlockCount), Lba);
                WCacheRemoveItemFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba);
                // mark as non-cached & free pool
                WCacheFreeSector(frame, Lba-firstLba);
                ASSERT(ValidateFrameBlocksList(Cache, Lba));
                // check if frame is empty
                if (!Cache->FrameList[frame].BlockCount) {
                    WCacheRemoveFrame(Cache, Context, frame);
                }
                if (firstPos >= Cache->WriteCount) firstPos=0;
            }
            // write packet
            Cache->UpdateRelocProc(Context, NULL, reloc_tab, MaxReloc);
            status = Cache->WriteProc(IrpContext, Context, tmp_buff, PS, NULL, &ReadBytes, 0);
            if (!NT_SUCCESS(status)) {
                status = WCacheRaiseIoError(Cache, Context, status, NULL, PSs, tmp_buff, WCACHE_W_OP, NULL);
            }
        } else {

            if ((i = Cache->BlockCount - Cache->WriteCount) > MaxReloc) i = MaxReloc;
            // discard blocks
            for(; i; i--) {
                Lba = List[firstPos];
                frame = Lba >> Cache->BlocksPerFrameSh;
                firstLba = frame << Cache->BlocksPerFrameSh;
                block_array = Cache->FrameList[frame].Frame;

                if ( (mod = WCacheGetModFlag(block_array, Lba - firstLba)) &&
                    (Cache->CheckUsedProc(Context, Lba) & WCACHE_BLOCK_USED) )
                    continue;
                WCacheRemoveItemFromList(List, &(Cache->BlockCount), Lba);
                if (mod)
                    WCacheRemoveItemFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba);
                // mark as non-cached & free pool
                WCacheFreeSector(frame, Lba-firstLba);
                ASSERT(ValidateFrameBlocksList(Cache, Lba));
                // check if frame is empty
                if (!Cache->FrameList[frame].BlockCount) {
                    WCacheRemoveFrame(Cache, Context, frame);
                }
                if (firstPos >= Cache->WriteCount) firstPos=0;
            }
        }
    }
    return STATUS_SUCCESS;
} // end WCacheCheckLimitsR()

/*
  WCachePurgeAllR() copies modified blocks from cache to media
  and removes them from cache
  This routine can be used for R media only.
  Internal routine
 */
VOID
WCachePurgeAllR(
    IN PIRP_CONTEXT IrpContext,
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN PVOID Context)         // user-supplied context for IO callbacks
{
    ULONG frame;
    lba_t firstLba;
    lba_t* List = Cache->CachedBlocksList;
    lba_t Lba;
    PCHAR tmp_buff = Cache->tmp_buff;
    ULONG BSh = Cache->BlockSizeSh;
    ULONG BS = Cache->BlockSize;
    PW_CACHE_ENTRY block_array;
    BOOLEAN mod;
    NTSTATUS status;
    SIZE_T ReadBytes;
    ULONG MaxReloc = Cache->PacketSize;
    PULONG reloc_tab = Cache->reloc_tab;
    ULONG RelocCount = 0;
    BOOLEAN IncompletePacket;
    ULONG i=0;
    ULONG PacketTail;

    while(Cache->WriteCount < Cache->BlockCount) {

        Lba = List[i];
        frame = Lba >> Cache->BlocksPerFrameSh;
        firstLba = frame << Cache->BlocksPerFrameSh;
        block_array = Cache->FrameList[frame].Frame;
        if (!block_array) {
            return;
        }
        // check if modified
        mod = WCacheGetModFlag(block_array, Lba - firstLba);
        // just discard
        if (!mod || !(Cache->CheckUsedProc(Context, Lba) & WCACHE_BLOCK_USED)) {
            // mark as non-cached & free pool
            if (WCacheSectorAddr(block_array,Lba-firstLba)) {
                WCacheRemoveItemFromList(List, &(Cache->BlockCount), Lba);
                if (mod)
                    WCacheRemoveItemFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba);
                // mark as non-cached & free pool
                WCacheFreeSector(frame, Lba-firstLba);
                ASSERT(ValidateFrameBlocksList(Cache, Lba));
                // check if frame is empty
                if (!Cache->FrameList[frame].BlockCount) {
                    WCacheRemoveFrame(Cache, Context, frame);
                }
            } else {
            }
        } else {
            i++;
        }
    }

    PacketTail = Cache->WriteCount & (MaxReloc-1);
    IncompletePacket = (Cache->WriteCount >= MaxReloc) ? FALSE : TRUE;

    // remove(flush) packet
    while((Cache->WriteCount > PacketTail) || (Cache->WriteCount && IncompletePacket)) {

        Lba = List[0];
        frame = Lba >> Cache->BlocksPerFrameSh;
        firstLba = frame << Cache->BlocksPerFrameSh;
        block_array = Cache->FrameList[frame].Frame;
        if (!block_array) {
            return;
        }
        // check if modified
        mod = WCacheGetModFlag(block_array, Lba - firstLba);
        // pack/reloc/write
        if (mod) {
            DbgCopyMemory(tmp_buff + (RelocCount << BSh),
                          (PVOID)WCacheSectorAddr(block_array, Lba-firstLba),
                          BS);
            reloc_tab[RelocCount] = Lba;
            RelocCount++;
            // write packet
            if ((RelocCount >= MaxReloc) || (Cache->BlockCount == 1)) {
                Cache->UpdateRelocProc(Context, NULL, reloc_tab, RelocCount);
                status = Cache->WriteProc(IrpContext, Context, tmp_buff, RelocCount<<BSh, NULL, &ReadBytes, 0);
                if (!NT_SUCCESS(status)) {
                    status = WCacheRaiseIoError(Cache, Context, status, NULL, RelocCount, tmp_buff, WCACHE_W_OP, NULL);
                }
                RelocCount = 0;
            }
            WCacheRemoveItemFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba);
        } else {
        }
        // mark as non-cached & free pool
        if (WCacheSectorAddr(block_array,Lba-firstLba)) {
            WCacheRemoveItemFromList(List, &(Cache->BlockCount), Lba);
            // mark as non-cached & free pool
            WCacheFreeSector(frame, Lba-firstLba);
            ASSERT(ValidateFrameBlocksList(Cache, Lba));
            // check if frame is empty
            if (!Cache->FrameList[frame].BlockCount) {
                WCacheRemoveFrame(Cache, Context, frame);
            }
        } else {
        }
    }
} // end WCachePurgeAllR()

/*
  WCacheSetMode__() changes cache operating mode (ROM/R/RW/RAM).
  Public routine
 */
NTSTATUS
WCacheSetMode__(
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN ULONG Mode             // cache mode/media type to be used
    )
{
    if (Mode > WCACHE_MODE_MAX) return STATUS_INVALID_PARAMETER;
    Cache->Mode = Mode;
    return STATUS_SUCCESS;
} // end WCacheSetMode__()

/*
  WCacheGetMode__() returns cache operating mode (ROM/R/RW/RAM).
  Public routine
 */
ULONG
WCacheGetMode__(
    IN PW_CACHE Cache
    )
{
    return Cache->Mode;
} // end WCacheGetMode__()

/*
  WCacheGetWriteBlockCount__() returns number of modified blocks, those are
  not flushed to media. Is usually used to preallocate blocks for
  relocation table on WORM (R) media.
  Public routine
 */
ULONG
WCacheGetWriteBlockCount__(
    IN PW_CACHE Cache
    )
{
    return Cache->WriteCount;
} // end WCacheGetWriteBlockCount__()

/*
  WCacheSyncReloc__() builds list of all modified blocks, currently
  stored in cache. For each modified block WCacheSyncReloc__() calls
  user-supplied callback routine in order to update relocation table
  on WORM (R) media.
  Public routine
 */
VOID
WCacheSyncReloc__(
    IN PW_CACHE Cache,
    IN PVOID Context)
{
    ULONG frame;
    lba_t firstLba;
    lba_t* List = Cache->CachedBlocksList;
    lba_t Lba;
    PW_CACHE_ENTRY block_array;
    BOOLEAN mod;
    ULONG MaxReloc = Cache->PacketSize;
    PULONG reloc_tab = Cache->reloc_tab;
    ULONG RelocCount = 0;
    BOOLEAN IncompletePacket;

    IncompletePacket = (Cache->WriteCount >= MaxReloc) ? FALSE : TRUE;
    // enumerate modified blocks
    for(ULONG i=0; IncompletePacket && (i<Cache->BlockCount); i++) {

        Lba = List[i];
        frame = Lba >> Cache->BlocksPerFrameSh;
        firstLba = frame << Cache->BlocksPerFrameSh;
        block_array = Cache->FrameList[frame].Frame;
        if (!block_array) {
            return;
        }
        // check if modified
        mod = WCacheGetModFlag(block_array, Lba - firstLba);
        // update relocation table for modified sectors
        if (mod && (Cache->CheckUsedProc(Context, Lba) & WCACHE_BLOCK_USED)) {
            reloc_tab[RelocCount] = Lba;
            RelocCount++;
            if (RelocCount >= Cache->WriteCount) {
                Cache->UpdateRelocProc(Context, NULL, reloc_tab, RelocCount);
                break;
            }
        }
    }
} // end WCacheSyncReloc__()

/*
  WCacheDiscardBlocks__() removes specified blocks from cache.
  Blocks are not flushed to media.
  Public routine
 */
VOID
WCacheDiscardBlocks__(
    IN PW_CACHE Cache,
    IN PVOID Context,
    IN lba_t ReqLba,
    IN ULONG BCount
    )
{
    ULONG frame;
    lba_t firstLba;
    lba_t* List;
    lba_t Lba;
    PW_CACHE_ENTRY block_array;
    BOOLEAN mod;
    ULONG i;

    ExAcquireResourceExclusiveLite(&(Cache->WCacheLock), TRUE);

    UDFPrint(("  Discard req: %x@%x\n",BCount, ReqLba));

    List = Cache->CachedBlocksList;
    if (!List) {
        ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
        return;
    }
    i = WCacheGetSortedListIndex(Cache->BlockCount, List, ReqLba);

    // enumerate requested blocks
    while((List[i] < (ReqLba+BCount)) && (i < Cache->BlockCount)) {

        Lba = List[i];
        frame = Lba >> Cache->BlocksPerFrameSh;
        firstLba = frame << Cache->BlocksPerFrameSh;
        block_array = Cache->FrameList[frame].Frame;
        if (!block_array) {
            ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
            return;
        }
        // check if modified
        mod = WCacheGetModFlag(block_array, Lba - firstLba);
        // just discard

        // mark as non-cached & free pool
        if (WCacheSectorAddr(block_array,Lba-firstLba)) {
            WCacheRemoveItemFromList(List, &(Cache->BlockCount), Lba);
            if (mod)
                WCacheRemoveItemFromList(Cache->CachedModifiedBlocksList, &(Cache->WriteCount), Lba);
            // mark as non-cached & free pool
            WCacheFreeSector(frame, Lba-firstLba);
            ASSERT(ValidateFrameBlocksList(Cache, Lba));
            // check if frame is empty
            if (!Cache->FrameList[frame].BlockCount) {
                WCacheRemoveFrame(Cache, Context, frame);
            } else {
                ASSERT(Cache->FrameList[frame].Frame);
            }
        } else {
            // we should never get here !!!
            // getting this part of code means that we have
            // placed non-cached block in CachedBlocksList
        }
    }
    ExReleaseResourceForThreadLite(&(Cache->WCacheLock), ExGetCurrentResourceThread());
} // end WCacheDiscardBlocks__()

NTSTATUS
WCacheCompleteAsync__(
    IN PVOID WContext,
    IN NTSTATUS Status
    )
{
    PW_CACHE_ASYNC AsyncCtx = (PW_CACHE_ASYNC)WContext;

    AsyncCtx->PhContext.IosbToUse.Status = Status;
    KeSetEvent(&(AsyncCtx->PhContext.event), 0, FALSE);

    return STATUS_SUCCESS;
} // end WCacheSetMode__()

/*
  WCacheDecodeFlags() updates internal BOOLEANs according to Flags
  Internal routine
 */
NTSTATUS
WCacheDecodeFlags(
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN ULONG Flags            // cache mode flags
    )
{
    //ULONG OldFlags;
    if (Flags & ~WCACHE_VALID_FLAGS) {
        UDFPrint(("Invalid flags: %x\n", Flags & ~WCACHE_VALID_FLAGS));
        return STATUS_INVALID_PARAMETER;
    }
    Cache->CacheWholePacket = (Flags & WCACHE_CACHE_WHOLE_PACKET) ? TRUE : FALSE;
    Cache->DoNotCompare = (Flags & WCACHE_DO_NOT_COMPARE) ? TRUE : FALSE;
    Cache->Chained = (Flags & WCACHE_CHAINED_IO) ? TRUE : FALSE;
    Cache->RememberBB = (Flags & WCACHE_MARK_BAD_BLOCKS) ? TRUE : FALSE;
    if (Cache->RememberBB) {
        Cache->NoWriteBB = (Flags & WCACHE_RO_BAD_BLOCKS) ? TRUE : FALSE;
    }
    Cache->NoWriteThrough = (Flags & WCACHE_NO_WRITE_THROUGH) ? TRUE : FALSE;

    Cache->Flags = Flags;

    return STATUS_SUCCESS;
}

/*
  WCacheChFlags__() changes cache flags.
  Public routine
 */
ULONG
WCacheChFlags__(
    IN PW_CACHE Cache,        // pointer to the Cache Control structure
    IN ULONG SetFlags,        // cache mode/media type to be set
    IN ULONG ClrFlags         // cache mode/media type to be cleared
    )
{
    ULONG Flags;

    if (SetFlags || ClrFlags) {
        Flags = (Cache->Flags & ~ClrFlags) | SetFlags;

        if (!NT_SUCCESS(WCacheDecodeFlags(Cache, Flags))) {
            return -1;
        }
    } else {
        return Cache->Flags;
    }
    return Flags;
} // end WCacheSetMode__()

BOOLEAN 
ValidateFrameBlocksList(
    IN PW_CACHE Cache,
    IN lba_t Lba)
{
    ULONG Frame = Lba >> Cache->BlocksPerFrameSh;
    lba_t FirstLba = Frame << Cache->BlocksPerFrameSh;
    lba_t LastLba = FirstLba + Cache->BlocksPerFrame;
    ULONG FirstPos = WCacheGetSortedListIndex(Cache->BlockCount, Cache->CachedBlocksList, FirstLba);
    ULONG LastPos = WCacheGetSortedListIndex(Cache->BlockCount, Cache->CachedBlocksList, LastLba);

    ULONG BlockCount = Cache->FrameList[Frame].BlockCount;
    ULONG RangeSize = LastPos - FirstPos;

    return (BlockCount == RangeSize);
}
