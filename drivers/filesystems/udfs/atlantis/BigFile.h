////////////////////////////////////////////////////////////////////
// Atlantis Cache System - BigFile Interface - Kernel Mode Port
// Copyright (C) Rogerio Regis, adapted for ReactOS
// This file was adapted from the original Atlantis project
////////////////////////////////////////////////////////////////////

#ifndef ATLANTIS_BIGFILE_H
#define ATLANTIS_BIGFILE_H

#include "atlantis_types.h"
#include "Archive.h"
#include "MgrCacheFile.h"
#include "CacheModel.h"

namespace Atlantis {

//**************************************************************************************
//*		Interface IBigFile
//*************************************************************************************
class IBigFile {
public:
    using Pointer = unique_ptr<IBigFile>;
    virtual ~IBigFile() = default;

    virtual void initialize(size_t maxReadBufferLength, size_t cacheRecordSize, 
                          size_t cacheFileSize) = 0;
    virtual const string get(size_t key) = 0;
};

//**************************************************************************************
//*		class BigFile - Main Atlantis Cache Interface
//*************************************************************************************
class BigFile : public IBigFile {
public:
    using Pointer = unique_ptr<BigFile>;

    template<typename ...Args>
    static Pointer create(Args &&...arg) {
        struct EnableMakeUnique : public BigFile {
            EnableMakeUnique(Args &&...arg) : BigFile(std::forward<Args>(arg)...) {}
        };
        Pointer result(new EnableMakeUnique(std::forward<Args>(arg)...));
        return result;
    }
    
    virtual ~BigFile() {
        // Statistics could be logged here if needed
    }

    BigFile(const BigFile &) = delete;
    BigFile(BigFile &&) = delete;
    const BigFile& operator=(const BigFile&) = delete;

    void initialize(size_t maxReadBufferLength, size_t cacheRecordSize, 
                   size_t cacheFileSize) override {
        
        // Create archive for file access
        archive_ = Archive::create();
        
        // Create cache manager
        mgrCacheFile_ = MgrCacheFile::create(archive_, cacheFileSize);
        
        // Initialize cache manager
        mgrCacheFile_->initialize(maxReadBufferLength, maxRecordSize_);
        
        // Create record cache
        cacheRecord_ = CacheRecord::Pointer(new CacheRecord(cacheRecordSize));
    }

    void initializeWithContext(PIRP_CONTEXT irpContext, PVOID context, 
                             PREAD_BLOCK readProc, size_t fileSize, 
                             ULONG blockSize, size_t maxReadBufferLength, 
                             size_t cacheRecordSize, size_t cacheFileSize) {
        
        // Create and initialize archive
        archive_ = Archive::create();
        archive_->initialize(irpContext, context, readProc, fileSize, blockSize);
        
        // Create cache manager
        mgrCacheFile_ = MgrCacheFile::create(archive_, cacheFileSize);
        
        // Initialize cache manager
        mgrCacheFile_->initialize(maxReadBufferLength, maxRecordSize_);
        
        // Create record cache
        cacheRecord_ = CacheRecord::Pointer(new CacheRecord(cacheRecordSize));
    }

    const string get(size_t key) override {
        ++requestTotal_;

        // Verify if record is in memory cache
        pair<bool, const string> pairRet = cacheRecord_->getValue((int)key);
        if (pairRet.first) {
            ++requestFromRecordCache_;
            return pairRet.second;
        }

        // Get record from file and insert in memory cache if found
        pair<bool, const string> pairValue = mgrCacheFile_->getValue(key);
        if (pairValue.first) {
            ++requestInsertRecordCache_;
            cacheRecord_->setValue((int)key, pairValue.second);
            return pairValue.second;
        }

        ++requestNotFound_;
        return string("");
    }

    // Statistics access methods
    size_t getRequestTotal() const { return requestTotal_; }
    size_t getRequestFromRecordCache() const { return requestFromRecordCache_; }
    size_t getRequestInsertRecordCache() const { return requestInsertRecordCache_; }
    size_t getRequestNotFound() const { return requestNotFound_; }

private:
    explicit BigFile(size_t maxRecordSize) : maxRecordSize_(maxRecordSize),
        requestTotal_(0), requestFromRecordCache_(0), 
        requestInsertRecordCache_(0), requestNotFound_(0) {
    }

private:
    size_t maxRecordSize_;
    Archive::Pointer archive_;
    MgrCacheFile::Pointer mgrCacheFile_;
    CacheRecord::Pointer cacheRecord_;

    // Statistics
    size_t requestTotal_;
    size_t requestFromRecordCache_;
    size_t requestInsertRecordCache_;
    size_t requestNotFound_;
};

} // namespace Atlantis

#endif // ATLANTIS_BIGFILE_H