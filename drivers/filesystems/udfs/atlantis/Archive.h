////////////////////////////////////////////////////////////////////
// Atlantis Cache System - Archive (File Access) - Kernel Mode Port
// Copyright (C) Rogerio Regis, adapted for ReactOS
// This file was adapted from the original Atlantis project
////////////////////////////////////////////////////////////////////

#ifndef ATLANTIS_ARCHIVE_H
#define ATLANTIS_ARCHIVE_H

#include "atlantis_types.h"

// Include UDFS types
extern "C" {
#include "../Include/platform.h"
#include <ntifs.h>
}

struct IRP_CONTEXT;
typedef struct IRP_CONTEXT *PIRP_CONTEXT;

typedef NTSTATUS (*PREAD_BLOCK) (IN PIRP_CONTEXT IrpContext,
                                IN PVOID Context,
                                IN PVOID Buffer,     // Target buffer
                                IN SIZE_T Length,
                                IN lba_t Lba,
                                OUT PSIZE_T ReadBytes,
                                IN uint32 Flags);

namespace Atlantis {

//**************************************************************************************
//*
//*		Interface IArchive
//*
//*************************************************************************************
class IArchive {
public:
    using Pointer = unique_ptr<IArchive>;
    virtual ~IArchive() = default;

    virtual void readFile(size_t position, char* buffer, size_t length) = 0;
    virtual void readFile(size_t index, size_t position, char* buffer, size_t length) = 0;
    virtual size_t getFileSize() = 0;
};

//**************************************************************************************
//*
//*		class Archive - Kernel mode file access for UDFS
//*
//*************************************************************************************
class Archive : public IArchive {
public:
    using Pointer = unique_ptr<Archive>;

    template<typename ...Args>
    static Pointer create(Args &&...arg) {
        struct EnableMakeUnique : public Archive {
            EnableMakeUnique(Args &&...arg) : Archive(std::forward<Args>(arg)...) {}
        };
        Pointer result(new EnableMakeUnique(std::forward<Args>(arg)...));
        return result;
    }

    virtual ~Archive() = default;

    Archive(const Archive &) = delete;
    Archive(Archive &&) = delete;
    const Archive& operator=(const Archive&) = delete;

    // Read data from file at specified position
    void readFile(size_t position, char* buffer, size_t length) override {
        if (!context_ || !readProc_ || !buffer) {
            return;
        }

        // Convert position to LBA (Logical Block Address)
        lba_t lba = static_cast<lba_t>(position / blockSize_);
        ULONG blockOffset = static_cast<ULONG>(position % blockSize_);
        ULONG blocksToRead = static_cast<ULONG>((length + blockOffset + blockSize_ - 1) / blockSize_);
        
        // Allocate temporary buffer for block-aligned read
        SIZE_T tempBufferSize = blocksToRead * blockSize_;
        char* tempBuffer = (char*)ExAllocatePool(PagedPool, tempBufferSize);
        if (!tempBuffer) {
            return;
        }

        SIZE_T readBytes = 0;
        NTSTATUS status = readProc_(irpContext_, context_, tempBuffer, tempBufferSize, lba, &readBytes, 0);
        
        if (NT_SUCCESS(status) && readBytes >= length + blockOffset) {
            // Copy the requested portion from the aligned buffer
            RtlCopyMemory(buffer, tempBuffer + blockOffset, length);
        }

        ExFreePool(tempBuffer);
    }

    // Read data from file using index-based access
    void readFile(size_t index, size_t position, char* buffer, size_t length) override {
        // For kernel mode, we can use the same implementation as direct position access
        readFile(position, buffer, length);
    }

    // Get total file size
    size_t getFileSize() override {
        return fileSize_;
    }

    // Initialize with UDFS-specific parameters
    void initialize(PIRP_CONTEXT irpContext, PVOID context, PREAD_BLOCK readProc, 
                   size_t fileSize, ULONG blockSize) {
        irpContext_ = irpContext;
        context_ = context;
        readProc_ = readProc;
        fileSize_ = fileSize;
        blockSize_ = blockSize;
    }

private:
    explicit Archive() : 
        irpContext_(nullptr), 
        context_(nullptr), 
        readProc_(nullptr),
        fileSize_(0),
        blockSize_(2048) // Default CD/DVD block size
    {
    }

private:
    PIRP_CONTEXT irpContext_;
    PVOID context_;
    PREAD_BLOCK readProc_;
    size_t fileSize_;
    ULONG blockSize_;
};

} // namespace Atlantis

#endif // ATLANTIS_ARCHIVE_H