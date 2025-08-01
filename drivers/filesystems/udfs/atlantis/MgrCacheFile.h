////////////////////////////////////////////////////////////////////
// Atlantis Cache System - MgrCacheFile - Kernel Mode Port
// Copyright (C) Rogerio Regis, adapted for ReactOS
// This file was adapted from the original Atlantis project
////////////////////////////////////////////////////////////////////

#ifndef ATLANTIS_MGRCACHEFILE_H
#define ATLANTIS_MGRCACHEFILE_H

#include "atlantis_types.h"
#include "Archive.h"
#include "CacheModel.h"

namespace Atlantis {

//**************************************************************************************
//*		Interface IMgrCacheFile
//*************************************************************************************
class IMgrCacheFile {
public:
    using Pointer = unique_ptr<IMgrCacheFile>;
    virtual ~IMgrCacheFile() = default;

    virtual void initialize(size_t maxReadBufferSize, size_t recordSize) = 0;
    virtual pair<bool, const string> getValue(size_t key) = 0;
};

//**************************************************************************************
//*		class MgrCacheFile
//*************************************************************************************
class MgrCacheFile : public IMgrCacheFile {
public:
    using Pointer = shared_ptr<MgrCacheFile>;
    
    struct FileCacheInfo {
        int index;
        size_t key;
        size_t startBlock;
        size_t blockSize;
    };
    
    using VecFileCacheInfo = vector<FileCacheInfo>;
    using PairForCache = pair<size_t, char*>;

    template<typename ...Args>
    static Pointer create(Args &&...arg) {
        struct EnableMakeShared : public MgrCacheFile {
            EnableMakeShared(Args &&...arg) : MgrCacheFile(std::forward<Args>(arg)...) {}
        };
        Pointer result(new EnableMakeShared(std::forward<Args>(arg)...));
        return result;
    }
    
    virtual ~MgrCacheFile() {
        // Clean up allocated buffers
        for (size_t idx = 0; idx < cacheFileSize_; ++idx) {
            if (bufferCacheFile_[idx] != nullptr) {
                ExFreePool(bufferCacheFile_[idx]);
            }
        }
        if (bufferCacheFile_) {
            ExFreePool(bufferCacheFile_);
        }
        if (bufferCacheFileSwapSav_) {
            ExFreePool(bufferCacheFileSwapSav_);
        }
    }

    MgrCacheFile(const MgrCacheFile &) = delete;
    MgrCacheFile(MgrCacheFile &&) = delete;
    const MgrCacheFile& operator=(const MgrCacheFile&) = delete;

    void initialize(size_t maxReadBufferSize, size_t recordSize) override {
        recordSize_ = recordSize;
        size_t eofOffset = archive_->getFileSize();

        pair<size_t, size_t> pairRet = calculateBlockSize(eofOffset, maxReadBufferSize);
        size_t blockAmount = pairRet.first;
        size_t blockSize = pairRet.second;

        size_t byteToRead = min(blockSize, (recordSize * 2) + 2);
        byteToRead = byteToRead == blockSize ? byteToRead - 1 : byteToRead;
        
        char* buffer = (char*)ExAllocatePool(PagedPool, byteToRead);
        char* buffKey = (char*)ExAllocatePool(PagedPool, byteToRead);
        
        if (!buffer || !buffKey) {
            if (buffer) ExFreePool(buffer);
            if (buffKey) ExFreePool(buffKey);
            return;
        }

        // Divide the file in blocks and get initial key of each block
        for (size_t idx = 0; idx < blockAmount; ++idx) {
            int posToRead = (int)((idx * blockSize) - byteToRead);
            posToRead = posToRead < 0 ? 0 : posToRead;
            
            char* posBuff = calculateRecordKeyPosition(posToRead, buffer, byteToRead);
            if (!posBuff) continue;

            size_t startBlock = posToRead + (posBuff - buffer);
            getRecordKey(posBuff, buffKey);
            size_t key = atoi(buffKey);
            size_t realBlockSize = ((blockSize * idx) - startBlock) + blockSize;

            populateCacheIndex((int)idx, key, startBlock, realBlockSize);
        }

        ExFreePool(buffer);
        ExFreePool(buffKey);

        size_t maxFileBlockSize_ = blockSize;

        // Recalculate block size using difference between start blocks
        for (size_t idx = blockAmount - 1; idx != 0; idx--) {
            if (idx < vFileCacheInfo_.size() && (idx - 1) < vFileCacheInfo_.size()) {
                vFileCacheInfo_[idx - 1].blockSize = vFileCacheInfo_[idx].startBlock - vFileCacheInfo_[idx - 1].startBlock;
            }
        }

        // Recalculate last block
        if (!vFileCacheInfo_.empty()) {
            vFileCacheInfo_[blockAmount - 1].blockSize = eofOffset - vFileCacheInfo_[blockAmount - 1].startBlock;
        }

        // Calculate greater block size to allocate for cache
        for (size_t idx = 0; idx < blockAmount; ++idx) {
            if (idx < vFileCacheInfo_.size() && vFileCacheInfo_[idx].blockSize > maxFileBlockSize_) {
                maxFileBlockSize_ = vFileCacheInfo_[idx].blockSize;
            }
        }

        // Allocate space for buffer cache
        bufferCacheFile_ = (char**)ExAllocatePool(PagedPool, cacheFileSize_ * sizeof(char*));
        if (bufferCacheFile_) {
            for (size_t idx = 0; idx < cacheFileSize_; ++idx) {
                bufferCacheFile_[idx] = (char*)ExAllocatePool(PagedPool, maxFileBlockSize_ + 1);
                if (bufferCacheFile_[idx]) {
                    cacheModelFile_->setValue(99999 + idx, bufferCacheFile_[idx]);
                }
            }
        }

        bufferCacheFileSwap_ = (char*)ExAllocatePool(PagedPool, maxFileBlockSize_ + 1);
        bufferCacheFileSwapSav_ = bufferCacheFileSwap_;
    }

    pair<bool, const string> getValue(size_t key) override {
        // Locate block file that contains the record
        pair<size_t, char*> pairFileBuffer = getFileBuffer(key);

        // Read block from file and search key
        pair<size_t, size_t> pairFileIndexInBuffer = 
            getFileIndexInBuffer(pairFileBuffer.second, pairFileBuffer.first, 0, 
                               pairFileBuffer.first - 1, key);

        if ((int)pairFileIndexInBuffer.first > -1) {
            size_t foundKey = pairFileIndexInBuffer.first;
            size_t bufferPos = pairFileIndexInBuffer.second;
            size_t bufferSize = pairFileBuffer.first;
            char* buffer = pairFileBuffer.second;

            string value = getRecordValue(foundKey, bufferPos, buffer, bufferSize);
            return make_pair(true, value);
        }

        return make_pair(false, string(""));
    }

private:
    explicit MgrCacheFile(Archive::Pointer archive, size_t cacheFileSize)
        : archive_(archive), cacheFileSize_(cacheFileSize), recordSize_(32),
          bufferCacheFile_(nullptr), bufferCacheFileSwap_(nullptr), 
          bufferCacheFileSwapSav_(nullptr) {
        
        cacheModelFile_ = LRUCache<size_t, char*>::create(cacheFileSize);
    }

    pair<size_t, char*> getFileBuffer(size_t key) {
        size_t index = getFileIndex(key);

        // Verify if buffer exists in cache
        pair<bool, char*> pairValue = cacheModelFile_->getValue(index);
        if (pairValue.first && index < vFileCacheInfo_.size()) {
            return make_pair(vFileCacheInfo_[index].blockSize, pairValue.second);
        }

        if (index >= vFileCacheInfo_.size()) {
            return make_pair(0, nullptr);
        }

        // Read block from file
        char* fileBuffer = bufferCacheFileSwap_;
        if (fileBuffer) {
            archive_->readFile(vFileCacheInfo_[index].startBlock, fileBuffer, 
                             vFileCacheInfo_[index].blockSize);

            // Save block in cache
            pair<size_t, char*> pairRetSet = cacheModelFile_->setValue(index, bufferCacheFileSwap_);
            bufferCacheFileSwap_ = pairRetSet.second;
        }

        return make_pair(vFileCacheInfo_[index].blockSize, fileBuffer);
    }

    int getFileIndexInCache(const VecFileCacheInfo& vFileCacheInfo, int left, int right, size_t key) {
        if (right < left) {
            return left - 1;  // Return index that contains key
        }

        int middle = left + (right - left) / 2;

        if (middle < (int)vFileCacheInfo.size()) {
            if (vFileCacheInfo[middle].key == key) {
                return middle;
            }
            if (vFileCacheInfo[middle].key > key) {
                return getFileIndexInCache(vFileCacheInfo, left, middle - 1, key);
            }
        }

        return getFileIndexInCache(vFileCacheInfo, middle + 1, right, key);
    }

    pair<size_t, size_t> getFileIndexInBuffer(char* buffer, size_t bufferSize, 
                                            int left, int right, size_t key) {
        if (right < left) {
            return make_pair((size_t)-1, 0);
        }

        int middle = left + (right - left) / 2;
        char* newBuffer = &buffer[middle];
        
        pair<size_t, size_t> pairRet = getRecordKey(newBuffer, bufferSize, middle, right - left);

        if (pairRet.first == (size_t)-1) {
            return pairRet;
        }

        size_t keyPos = pairRet.first;
        if (keyPos == key) {
            pairRet.second += middle;
            return pairRet;
        }
        if (keyPos > key) {
            return getFileIndexInBuffer(buffer, bufferSize, left, middle - 1, key);
        }

        return getFileIndexInBuffer(buffer, bufferSize, middle + 1, right, key);
    }

    string getRecordValue(size_t key, size_t bufferPos, char* buffer, size_t bufferSize) {
        char* bufferRec = &buffer[bufferPos];

        // Verify if record has the same key
        size_t keyRet = getRecordKey(bufferRec);
        if (keyRet != key) {
            return string("");
        }

        size_t limitSearch = bufferSize - bufferPos - 1;
        // Skip to space separator
        while (limitSearch > 0 && *bufferRec != ' ') {
            ++bufferRec;
            --limitSearch;
        }

        // Get value of record
        string sValue;
        if (limitSearch > 0) {
            ++bufferRec; // Skip space
            --limitSearch;
            
            while (limitSearch > 0) {
                if (*bufferRec == '\n' || *bufferRec == '\r') {
                    break;
                }
                sValue.append(bufferRec, 1);
                ++bufferRec;
                --limitSearch;
                
                if (sValue.size() > 1024) { // Prevent runaway
                    break;
                }
            }
        }

        return sValue;
    }

    size_t getRecordKey(char* posBuff) {
        string skey;
        while (*posBuff && *posBuff != ' ') {
            skey.append(posBuff, 1);
            ++posBuff;
            if (skey.size() > 10) {
                break;
            }
        }
        return atoi(skey.c_str());
    }

    pair<size_t, size_t> getRecordKey(char* buffer, size_t bufferSize, 
                                    size_t posBuff, size_t sizeBuff) {
        sizeBuff = max(sizeBuff, recordSize_ * 2);
        size_t sizeBuffPos = sizeBuff;
        size_t sizeBuffPos_U = bufferSize - posBuff;

        do {
            if ((*buffer == '\n') || (posBuff == 0)) {
                string skey;
                for (; sizeBuffPos > 0; --sizeBuffPos) {
                    --sizeBuffPos_U;
                    if (*buffer != ' ') {
                        skey.append(buffer, 1);
                        if (sizeBuffPos_U == 0) {
                            return make_pair((size_t)-1, 0);
                        }
                        if (skey.size() > 10) {
                            break;
                        }
                        ++buffer;
                    } else {
                        size_t offset = (sizeBuff - sizeBuffPos) - skey.size();
                        offset = posBuff == 0 ? offset : offset + 1;
                        return make_pair(atoi(skey.c_str()), offset);
                    }
                }
                return make_pair((size_t)-1, 0);
            }
            ++buffer;
            --sizeBuffPos;
            --sizeBuffPos_U;
        } while (sizeBuffPos > 0);

        return make_pair((size_t)-1, 0);
    }

    size_t getFileIndex(size_t key) {
        return getFileIndexInCache(vFileCacheInfo_, 0, (int)vFileCacheInfo_.size() - 1, key);
    }

    void populateCacheIndex(int index, size_t key, size_t startBlock, size_t blockSize) {
        FileCacheInfo fileCacheInfo;
        fileCacheInfo.index = index;
        fileCacheInfo.key = key == 1 ? 0 : key;
        fileCacheInfo.startBlock = startBlock;
        fileCacheInfo.blockSize = blockSize;
        vFileCacheInfo_.push_back(fileCacheInfo);
    }

    pair<size_t, size_t> calculateBlockSize(size_t fileSize, size_t maxReadBuffer) {
        size_t maxBlock = fileSize / maxReadBuffer;
        
        int index = 0;
        for (index = 0; index < 15; ++index) {
            if ((1ULL << index) > maxBlock) {
                break;
            }
        }

        size_t blockAmount = 1ULL << index;
        size_t blockSize = (fileSize / blockAmount) + 1;

        return make_pair(blockAmount, blockSize);
    }

    void getRecordKey(char* posBuff, char* key) {
        char* posKey = key;
        while (*posBuff && *posBuff != ' ') {
            *posKey++ = *posBuff++;
            if (posKey - key > 10) {
                break;
            }
        }
        *posKey = '\0';
    }

    char* calculateRecordKeyPosition(size_t posToRead, char* buffer, size_t byteToRead) {
        archive_->readFile(posToRead, buffer, byteToRead);

        char* posBuff = buffer;
        if (posToRead == 0) {
            return posBuff;
        }

        // Look for start of record (after EOL)
        for (size_t i = 0; i < byteToRead; ++i) {
            if (*posBuff == '\n') {
                return posBuff + 1;
            }
            ++posBuff;
        }

        return nullptr;
    }

private:
    Archive::Pointer archive_;
    size_t cacheFileSize_;
    VecFileCacheInfo vFileCacheInfo_;
    ICacheModel<size_t, char*>::Pointer cacheModelFile_;
    char** bufferCacheFile_;
    char* bufferCacheFileSwap_;
    char* bufferCacheFileSwapSav_;
    size_t recordSize_;
};

} // namespace Atlantis

#endif // ATLANTIS_MGRCACHEFILE_H