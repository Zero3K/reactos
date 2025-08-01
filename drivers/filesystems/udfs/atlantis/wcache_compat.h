////////////////////////////////////////////////////////////////////
// Atlantis Cache System - WCache Compatibility Layer
// Copyright (C) Rogerio Regis, adapted for ReactOS
// This provides WCache API compatibility while using Atlantis backend
////////////////////////////////////////////////////////////////////

#ifndef ATLANTIS_WCACHE_COMPAT_H
#define ATLANTIS_WCACHE_COMPAT_H

#include "atlantis_types.h"
#include "BigFile.h"
#include "Archive.h"

extern "C" {
#include "../Include/platform.h"
}

// Forward declarations from wcache_lib.h
struct IRP_CONTEXT;
typedef struct IRP_CONTEXT *PIRP_CONTEXT;

typedef NTSTATUS (*PWRITE_BLOCK) (IN PIRP_CONTEXT IrpContext,
                                 IN PVOID Context,
                                 IN PVOID Buffer,     // Target buffer
                                 IN SIZE_T Length,
                                 IN lba_t Lba,
                                 OUT PSIZE_T WrittenBytes,
                                 IN uint32 Flags);

typedef NTSTATUS (*PREAD_BLOCK) (IN PIRP_CONTEXT IrpContext,
                                IN PVOID Context,
                                IN PVOID Buffer,     // Target buffer
                                IN SIZE_T Length,
                                IN lba_t Lba,
                                OUT PSIZE_T ReadBytes,
                                IN uint32 Flags);

typedef NTSTATUS (*PWRITE_BLOCK_ASYNC) (IN PVOID Context,
                                       IN PVOID WContext,
                                       IN PVOID Buffer,     // Target buffer
                                       IN SIZE_T Length,
                                       IN lba_t Lba,
                                       OUT PSIZE_T WrittenBytes,
                                       IN BOOLEAN FreeBuffer);

typedef NTSTATUS (*PREAD_BLOCK_ASYNC) (IN PVOID Context,
                                      IN PVOID WContext,
                                      IN PVOID Buffer,     // Source buffer
                                      IN SIZE_T Length,
                                      IN lba_t Lba,
                                      OUT PSIZE_T ReadBytes);

typedef ULONG (*PCHECK_BLOCK) (IN PVOID Context,
                              IN lba_t Lba);

typedef NTSTATUS (*PUPDATE_RELOC) (IN PVOID Context,
                                  IN lba_t Lba,
                                  IN PULONG RelocTab,
                                  IN ULONG BCount);

typedef NTSTATUS (*PWC_ERROR_HANDLER) (IN PVOID Context,
                                      IN PVOID ErrorInfo);

// WCache mode constants
#define WCACHE_MODE_ROM      0x00000000  // read only (CD-ROM)
#define WCACHE_MODE_RW       0x00000001  // rewritable (CD-RW)
#define WCACHE_MODE_R        0x00000002  // WORM (CD-R)
#define WCACHE_MODE_RAM      0x00000003  // random writable device (HDD)
#define WCACHE_MODE_EWR      0x00000004  // ERASE-cycle required (MO)

// WCache flag constants
#define WCACHE_CACHE_WHOLE_PACKET   0x01
#define WCACHE_DO_NOT_COMPARE       0x02
#define WCACHE_CHAINED_IO           0x04
#define WCACHE_MARK_BAD_BLOCKS      0x08
#define WCACHE_RO_BAD_BLOCKS        0x10
#define WCACHE_NO_WRITE_THROUGH     0x20

namespace Atlantis {

//**************************************************************************************
//*		AtlantisWCache - WCache compatibility wrapper
//*************************************************************************************
class AtlantisWCache {
public:
    AtlantisWCache() : 
        initialized_(false),
        tag_(0x41544C41), // 'ATLA'
        maxFrames_(0),
        maxBlocks_(0),
        maxBytesToRead_(0),
        packetSizeSh_(0),
        blockSizeSh_(0),
        blocksPerFrameSh_(0),
        firstLba_(0),
        lastLba_(0),
        mode_(WCACHE_MODE_ROM),
        flags_(0),
        framesToKeepFree_(0),
        writeProc_(nullptr),
        readProc_(nullptr),
        writeProcAsync_(nullptr),
        readProcAsync_(nullptr),
        checkUsedProc_(nullptr),
        updateRelocProc_(nullptr),
        errorHandlerProc_(nullptr),
        context_(nullptr) {
        
        ExInitializeResourceLite(&cacheLock_);
    }

    ~AtlantisWCache() {
        if (initialized_) {
            ExDeleteResourceLite(&cacheLock_);
        }
    }

    NTSTATUS initialize(ULONG maxFrames, ULONG maxBlocks, SIZE_T maxBytesToRead,
                       ULONG packetSizeSh, ULONG blockSizeSh, ULONG blocksPerFrameSh,
                       lba_t firstLba, lba_t lastLba, ULONG mode, ULONG flags,
                       ULONG framesToKeepFree, PWRITE_BLOCK writeProc,
                       PREAD_BLOCK readProc, PWRITE_BLOCK_ASYNC writeProcAsync,
                       PREAD_BLOCK_ASYNC readProcAsync, PCHECK_BLOCK checkUsedProc,
                       PUPDATE_RELOC updateRelocProc, PWC_ERROR_HANDLER errorHandlerProc) {
        
        if (initialized_) {
            return STATUS_SUCCESS;
        }

        maxFrames_ = maxFrames;
        maxBlocks_ = maxBlocks;
        maxBytesToRead_ = maxBytesToRead;
        packetSizeSh_ = packetSizeSh;
        blockSizeSh_ = blockSizeSh;
        blocksPerFrameSh_ = blocksPerFrameSh;
        firstLba_ = firstLba;
        lastLba_ = lastLba;
        mode_ = mode;
        flags_ = flags;
        framesToKeepFree_ = framesToKeepFree;
        writeProc_ = writeProc;
        readProc_ = readProc;
        writeProcAsync_ = writeProcAsync;
        readProcAsync_ = readProcAsync;
        checkUsedProc_ = checkUsedProc;
        updateRelocProc_ = updateRelocProc;
        errorHandlerProc_ = errorHandlerProc;

        // Calculate cache sizes for Atlantis
        size_t cacheRecordSize = maxBlocks / 4;  // 25% for record cache
        size_t cacheFileSize = maxFrames;        // Use frames for file cache
        size_t maxRecordSize = 1024;             // Default record size

        // Create Atlantis BigFile cache
        bigFile_ = BigFile::create(maxRecordSize);
        
        initialized_ = true;
        return STATUS_SUCCESS;
    }

    NTSTATUS initializeWithContext(PIRP_CONTEXT irpContext, PVOID context,
                                 size_t fileSize, ULONG blockSize) {
        if (!initialized_ || !readProc_) {
            return STATUS_INVALID_PARAMETER;
        }

        context_ = context;

        // Calculate cache sizes
        size_t cacheRecordSize = maxBlocks_ / 4;
        size_t cacheFileSize = maxFrames_;
        size_t maxRecordSize = 1024;

        // Initialize BigFile with UDFS context
        bigFile_->initializeWithContext(irpContext, context, readProc_, 
                                      fileSize, blockSize, maxBytesToRead_,
                                      cacheRecordSize, cacheFileSize);

        return STATUS_SUCCESS;
    }

    NTSTATUS readBlocks(PIRP_CONTEXT irpContext, PVOID context, PCHAR buffer,
                       lba_t lba, ULONG bCount, PSIZE_T readBytes, BOOLEAN cachedOnly) {
        
        if (!initialized_ || !bigFile_) {
            return STATUS_INVALID_PARAMETER;
        }

        ExAcquireResourceSharedLite(&cacheLock_, TRUE);

        NTSTATUS status = STATUS_SUCCESS;
        SIZE_T totalRead = 0;
        ULONG blockSize = 1UL << blockSizeSh_;

        _SEH2_TRY {
            for (ULONG i = 0; i < bCount; ++i) {
                // Try to get block from Atlantis cache
                string cachedData = bigFile_->get(lba + i);
                
                if (!cachedData.empty()) {
                    // Copy cached data
                    SIZE_T copySize = min(blockSize, cachedData.size());
                    RtlCopyMemory(buffer + (i * blockSize), cachedData.c_str(), copySize);
                    totalRead += copySize;
                } else if (!cachedOnly && readProc_) {
                    // Read directly from device
                    SIZE_T currentRead = 0;
                    status = readProc_(irpContext, context, buffer + (i * blockSize),
                                     blockSize, lba + i, &currentRead, 0);
                    if (!NT_SUCCESS(status)) {
                        break;
                    }
                    totalRead += currentRead;
                } else {
                    // Not in cache and cached-only requested
                    status = STATUS_NOT_FOUND;
                    break;
                }
            }

            *readBytes = totalRead;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            status = STATUS_INVALID_PARAMETER;
        }
        _SEH2_END;

        ExReleaseResourceLite(&cacheLock_);
        return status;
    }

    NTSTATUS writeBlocks(PIRP_CONTEXT irpContext, PVOID context, PCHAR buffer,
                        lba_t lba, ULONG bCount, PSIZE_T writtenBytes, BOOLEAN cachedOnly) {
        
        if (!initialized_ || mode_ == WCACHE_MODE_ROM) {
            return STATUS_MEDIA_WRITE_PROTECTED;
        }

        ExAcquireResourceExclusiveLite(&cacheLock_, TRUE);

        NTSTATUS status = STATUS_SUCCESS;
        SIZE_T totalWritten = 0;
        ULONG blockSize = 1UL << blockSizeSh_;

        _SEH2_TRY {
            for (ULONG i = 0; i < bCount; ++i) {
                // For write operations, we should update both cache and device
                if (writeProc_ && !cachedOnly) {
                    SIZE_T currentWritten = 0;
                    status = writeProc_(irpContext, context, buffer + (i * blockSize),
                                      blockSize, lba + i, &currentWritten, 0);
                    if (!NT_SUCCESS(status)) {
                        break;
                    }
                    totalWritten += currentWritten;
                }

                // Update cache with written data (if BigFile supported writes)
                // For now, we invalidate the cache entry
                // bigFile_->invalidate(lba + i);
            }

            *writtenBytes = totalWritten;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            status = STATUS_INVALID_PARAMETER;
        }
        _SEH2_END;

        ExReleaseResourceLite(&cacheLock_);
        return status;
    }

    NTSTATUS directAccess(PIRP_CONTEXT irpContext, PVOID context, lba_t lba,
                         BOOLEAN modified, PCHAR* cachedBlock, BOOLEAN cachedOnly) {
        
        if (!initialized_ || !bigFile_) {
            return STATUS_INVALID_PARAMETER;
        }

        ExAcquireResourceSharedLite(&cacheLock_, TRUE);

        // For direct access, we need to return a pointer to cached data
        // This is complex with Atlantis since it returns string objects
        // For now, return not implemented
        ExReleaseResourceLite(&cacheLock_);
        return STATUS_NOT_IMPLEMENTED;
    }

    VOID flushAll(PIRP_CONTEXT irpContext, PVOID context) {
        if (!initialized_) {
            return;
        }

        ExAcquireResourceExclusiveLite(&cacheLock_, TRUE);
        // Atlantis doesn't have explicit flush - it's always consistent
        ExReleaseResourceLite(&cacheLock_);
    }

    VOID purgeAll(PIRP_CONTEXT irpContext, PVOID context) {
        if (!initialized_) {
            return;
        }

        ExAcquireResourceExclusiveLite(&cacheLock_, TRUE);
        // Recreate BigFile to clear cache
        if (bigFile_) {
            size_t maxRecordSize = 1024;
            bigFile_ = BigFile::create(maxRecordSize);
            
            if (context_) {
                size_t cacheRecordSize = maxBlocks_ / 4;
                size_t cacheFileSize = maxFrames_;
                
                // Re-initialize with context if available
                // bigFile_->initializeWithContext(...);
            }
        }
        ExReleaseResourceLite(&cacheLock_);
    }

    VOID release() {
        if (initialized_) {
            ExAcquireResourceExclusiveLite(&cacheLock_, TRUE);
            bigFile_.reset();
            initialized_ = false;
            ExReleaseResourceLite(&cacheLock_);
            ExDeleteResourceLite(&cacheLock_);
        }
    }

    BOOLEAN isInitialized() const {
        return initialized_;
    }

    BOOLEAN isCached(lba_t lba, ULONG bCount) {
        if (!initialized_ || !bigFile_) {
            return FALSE;
        }

        // Check if all requested blocks are cached
        for (ULONG i = 0; i < bCount; ++i) {
            string cachedData = bigFile_->get(lba + i);
            if (cachedData.empty()) {
                return FALSE;
            }
        }
        return TRUE;
    }

    NTSTATUS setMode(ULONG mode) {
        if (!initialized_) {
            return STATUS_INVALID_PARAMETER;
        }
        mode_ = mode;
        return STATUS_SUCCESS;
    }

    ULONG getMode() const {
        return mode_;
    }

    ULONG changeFlags(ULONG setFlags, ULONG clrFlags) {
        if (!initialized_) {
            return 0;
        }
        ULONG oldFlags = flags_;
        flags_ = (flags_ | setFlags) & ~clrFlags;
        return oldFlags;
    }

    // Statistics
    ULONG getWriteBlockCount() {
        return 0; // Not tracked in current implementation
    }

public:
    // Public members for compatibility with W_CACHE structure
    ULONG tag_;
    ERESOURCE cacheLock_;

private:
    bool initialized_;
    ULONG maxFrames_;
    ULONG maxBlocks_;
    SIZE_T maxBytesToRead_;
    ULONG packetSizeSh_;
    ULONG blockSizeSh_;
    ULONG blocksPerFrameSh_;
    lba_t firstLba_;
    lba_t lastLba_;
    ULONG mode_;
    ULONG flags_;
    ULONG framesToKeepFree_;

    // Callback functions
    PWRITE_BLOCK writeProc_;
    PREAD_BLOCK readProc_;
    PWRITE_BLOCK_ASYNC writeProcAsync_;
    PREAD_BLOCK_ASYNC readProcAsync_;
    PCHECK_BLOCK checkUsedProc_;
    PUPDATE_RELOC updateRelocProc_;
    PWC_ERROR_HANDLER errorHandlerProc_;

    PVOID context_;

    // Atlantis backend
    BigFile::Pointer bigFile_;
};

} // namespace Atlantis

// C-style wrapper structure for compatibility
typedef struct _W_CACHE {
    Atlantis::AtlantisWCache* atlantisCache;
    // Keep tag at the beginning for compatibility
    ULONG Tag;
    ERESOURCE WCacheLock;
} W_CACHE, *PW_CACHE;

#endif // ATLANTIS_WCACHE_COMPAT_H