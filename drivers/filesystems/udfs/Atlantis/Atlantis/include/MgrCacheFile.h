#pragma once
#ifndef MANAGERCACHEFILE_H_
#define MANAGERCACHEFILE_H_

//**************************************************************************************
//*
//*		Manager mecory cacho getting  record from file
//*
//*		2021/03/26
//*		Rogerio Regis
//*
//*************************************************************************************

#include <string>
#include <sstream>
#include <memory>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <vector>

#include "Archive.h"
#include "CacheModel.h"
#include "Logger.h"

#define MAXINDEX 15

namespace Atlantis
{
//**************************************************************************************
//*
//*		Interface IMgrCacheFile
//*
//*************************************************************************************
	
	class IMgrCacheFile
	{
	public:
		using Pointer = std::unique_ptr<IMgrCacheFile>;
		virtual ~IMgrCacheFile() = default;

		virtual void initialize(size_t maxReadBufferSize, size_t recordSize) = 0;
		virtual std::pair<bool, const std::string> getValue(size_t key) = 0;

	};
}

namespace Atlantis
{
//**************************************************************************************
//*
//*		class MgrCacheFile
//*
//*************************************************************************************
	class MgrCacheFile
		: IMgrCacheFile
	{
	public:
		using Pointer = std::shared_ptr<MgrCacheFile>;
		struct FileCacheInfo
		{
			int index;
			size_t key;
			size_t startBlock;
			size_t blockSize;
		};
		using VecFileCacheInfo = std::vector<FileCacheInfo>;
		using PairForCache = std::pair<size_t, char *>;

		template<typename ...Args>
		Pointer static create(Args &&...arg)
		{
			struct EnaleMakeShared
				: public MgrCacheFile
			{
				EnaleMakeShared(Args &&...arg)
					: MgrCacheFile(std::forward<Args>(arg)...)
				{}
			};
			Pointer result(std::make_shared<EnaleMakeShared>(std::forward<Args>(arg)...));
			return(std::move(result));
		}
		virtual ~MgrCacheFile()
		{
			ALOGINFO("Blocks found in cache: " << blockFoundInCache_ <<
				", Blocks inserted in cache: " << blockInsertInCache_);

			for (size_t idx = 0; idx < cacheFileSize_; ++idx) {
				if (bufferCacheFile_[idx] != nullptr) {
					delete[] bufferCacheFile_[idx];
				}
			}
			delete[] bufferCacheFile_;
			delete[] bufferCacheFileSwapSav_;
		}

		MgrCacheFile(const MgrCacheFile &) = delete;
		MgrCacheFile(MgrCacheFile &&) = delete;
		const MgrCacheFile& operator=(const MgrCacheFile&) = delete;

		// maxReadBufferSize : max size of block file to read in bytes
		// recordSize : max size of records in bytes
		void initialize(size_t maxReadBufferSize, size_t recordSize)
		{
			recordSize_ = recordSize;

			size_t eofOffset = archive_->getFileSize();


			std::pair<size_t, size_t> pairRet = calculateBlockSize(eofOffset, maxReadBufferSize);
			size_t blockAmount = pairRet.first;
			size_t blockSize = pairRet.second;

			size_t byteToRead = std::min<int>(blockSize, (recordSize * 2) + 2);
			byteToRead = byteToRead == blockSize ? byteToRead - 1 : byteToRead;
			char * buffer = new char[byteToRead];
			char * buffKey = new char[byteToRead];

			// divide the file in blocks and get initial key of this block and save into vector
			for (size_t idx = 0; idx < blockAmount; ++idx) {
				int posToRead = (idx * blockSize) - byteToRead;
				posToRead = posToRead < 0 ? 0 : posToRead;
				char * posBuff = calculateRecordKeyPosition(posToRead, buffer, byteToRead);

				size_t startBlock = posToRead + (posBuff - buffer);
				getRecordKey(posBuff, buffKey);
				size_t key = atoi(buffKey);
				size_t realBlockSize = ((blockSize * idx) - startBlock) + blockSize;
			
				populateCacheIndex(idx, key, startBlock, realBlockSize);
			}

			delete [] buffer;
			delete [] buffKey;

			size_t maxFileBlockSize_ = blockSize;

			// recalcule block size using difference between start block
			for (size_t idx = blockAmount - 1; idx != 0; idx--) {
				FileCacheInfo fileCacheInfoPrevious = vFileCacheInfo_[idx - 1];
				fileCacheInfoPrevious.blockSize = vFileCacheInfo_[idx].startBlock - fileCacheInfoPrevious.startBlock;;
				vFileCacheInfo_[idx - 1] = fileCacheInfoPrevious;
			}

			// recalculate to last block
			FileCacheInfo fileCacheInfo = vFileCacheInfo_[blockAmount - 1];
			fileCacheInfo.blockSize = eofOffset - fileCacheInfo.startBlock;
			vFileCacheInfo_[blockAmount - 1] = fileCacheInfo;


			// calculate greater block size to allocate to cache
			for (size_t idx = 0; idx < blockAmount; ++idx) {
				FileCacheInfo fileCacheInfo = vFileCacheInfo_[idx];
				if (fileCacheInfo.blockSize > maxFileBlockSize_) {
					maxFileBlockSize_ = fileCacheInfo.blockSize;
				}
			}

			// allocate space for buffer to read from file
			for (size_t idx = 0; idx < cacheFileSize_; ++idx) {
				bufferCacheFile_[idx] = new char[maxFileBlockSize_ + 1];
				cacheModelFile_->setValue(99999 + idx, bufferCacheFile_[idx]);  // inserted in cached fake space 
			}

			bufferCacheFileSwap_ = new char[maxFileBlockSize_ + 1];
			bufferCacheFileSwapSav_ = bufferCacheFileSwap_;		// used to delete pointer

			ALOGINFO(
				"File size:" << eofOffset <<
				", Amount Blocks files: "<<  blockAmount <<
				", Max block length: " << maxFileBlockSize_ + 1);
			ALOGINFO("FileCacheInfo:");

			for (size_t idx = 0; idx < blockAmount; ++idx) {
				FileCacheInfo fileCacheInfo = vFileCacheInfo_[idx];
				ALOGINFO("Index: " << fileCacheInfo.index <<
					", Initial Key: " << fileCacheInfo.key <<
					", Block Position:" << fileCacheInfo.startBlock <<
					", Block size: " << fileCacheInfo.blockSize);
			}

		}

		std::pair<bool, const std::string> getValue(size_t key)
		{
			
			// locate block file that contains the record
			std::pair<size_t, char *> pairFileBuffer = getFileBuffer(key);

			// read block from file an search key 
			std::pair<size_t, size_t> pairFileIndexInBuffer =
				getFileIndexInBuffer(pairFileBuffer.second, pairFileBuffer.first, 0, pairFileBuffer.first - 1, key);
			
			if ((int)pairFileIndexInBuffer.first > -1) {
				size_t key = pairFileIndexInBuffer.first;
				size_t bufferPos = pairFileIndexInBuffer.second;
				size_t bufferSize = pairFileBuffer.first;
				char *buffer = pairFileBuffer.second;

				const std::string value = getRecordValue(key, bufferPos, buffer, bufferSize);
				return(std::make_pair(true, value));
			}

			// todo verify se realmente nao existe
			return(std::make_pair(false,""));
		}

	private:
		explicit MgrCacheFile(Archive::Pointer archive, size_t cacheFileSize)
			: archive_(archive), cacheFileSize_(cacheFileSize)
		{

			cacheModelFile_ = CacheModelLRU<size_t, char *>::create(cacheFileSize);

			bufferCacheFile_ = new char*[cacheFileSize];		
		}

		std::pair<size_t, char*> getFileBuffer(size_t key)
		{
			size_t index = getFileIndex(key);

			// verify if exist buffer in cache 
			std::pair<bool, char *> pairValue = cacheModelFile_->getValue(index);
			if (pairValue.first) {
				++blockFoundInCache_;
				return(std::make_pair(vFileCacheInfo_[index].blockSize, pairValue.second));
			}

#ifdef DEBUG
			ALOGINFO("GetFileBuffer " << key << " " << index << " "
#endif				<< vFileCacheInfo_[index].startBlock << " " << vFileCacheInfo_[index].blockSize);

			// read block from file
			char *fileBuffer = bufferCacheFileSwap_;

			archive_->readFile(index, vFileCacheInfo_[index].startBlock, bufferCacheFileSwap_, vFileCacheInfo_[index].blockSize);

			// save block on cache
			std::pair<size_t, char *> pairRetSet = cacheModelFile_->setValue(index, bufferCacheFileSwap_);
			bufferCacheFileSwap_ = pairRetSet.second;							// perform swap

			++blockInsertInCache_;


			return(std::make_pair(vFileCacheInfo_[index].blockSize, fileBuffer));
		}


		int getFileIndexInCache(VecFileCacheInfo &vFileCacheInfo, int left, int right, size_t key)
		{
			if (right < left) {  // not found return index that contains key
				return (left - 1);
			}

			int middle = left + (right - left) / 2;

			if (vFileCacheInfo[middle].key == key) {
				return (middle);
			}
			if (vFileCacheInfo[middle].key > key) {
				return (getFileIndexInCache(vFileCacheInfo, left, middle - 1, key));
			}

			return (getFileIndexInCache(vFileCacheInfo, middle + 1, right, key));
		}

		std::pair<size_t, size_t> getFileIndexInBuffer(char *buffer, size_t BuffeSize, int left, int right, size_t key)
		{
			if (right < left) {
				return(std::make_pair(-1, 0));
			}

			int middle = left + (right - left) / 2;

			char *newBuffer = &buffer[middle];
			std::pair<size_t, size_t> pairRet = getRecordKey(newBuffer, BuffeSize,  middle,  right - left);

			if (pairRet.first == -1) {
				// return (getFileIndexInBuffer(buffer, left, recordSize_, key));	// get record in small space
				return(pairRet);
			}

			size_t keyPos = pairRet.first;
			if (keyPos == key) {
				pairRet.second += middle;
				return (pairRet);
			}
			if (keyPos > key) {
				return (getFileIndexInBuffer(buffer, BuffeSize, left, middle - 1, key));
			}

			return (getFileIndexInBuffer(buffer, BuffeSize, middle + 1, right, key));
		}


		inline const std::string  getRecordValue(size_t key, size_t bufferPos, char *buffer, size_t bufferSize)
		{

			char *bufferRec = &buffer[bufferPos];
			
			// verify if record is the same key
			size_t keyRet = getRecordKey(bufferRec);
			if (keyRet != key) {
				throw("Error. Invalid key received");
			}

			size_t limitSearch = bufferSize - bufferPos - 1;
			for (; *bufferRec != ' '; ++bufferRec, --limitSearch) {
			}

			// get vlue of record
			std::string sValue;
			for (++bufferRec; limitSearch > 0 ; ++bufferRec, --limitSearch) {
				if ((*bufferRec == '\n') ||
					(*bufferRec == '\r')) {
					break;
				}
				sValue.append(bufferRec, 1);
				if (sValue.size() > 32) {
					throw ("Invalid value");
				}
			}

			return(sValue);
		}

		inline size_t getRecordKey(char *posBuff)
		{
			std::string skey;
			for (; *posBuff != ' '; ++posBuff) {
				skey.append(posBuff, 1);
				consistKeySize(skey);
			}

			return(atoi(skey.c_str()));
		}


		inline std::pair<size_t, size_t> getRecordKey(char *buffer, size_t bufferSize, size_t posBuff, size_t sizeBuff)
		{
			sizeBuff = std::max<size_t>(sizeBuff, recordSize_ * 2);
			size_t sizeBuffPos = sizeBuff;


			size_t sizeBuffPos_D = std::max<size_t>(sizeBuff, recordSize_ * 2);;	// to down
			size_t sizeBuffPos_U = bufferSize - posBuff;							// to upper


			do {
				if ((*buffer == '\n') ||	// look for end of line or start of block
					(posBuff == 0)) {
					std::string skey;
					for (; sizeBuffPos > 0; --sizeBuffPos) {
						--sizeBuffPos_D;
						--sizeBuffPos_U;
						if (*buffer != ' ') {		// delimite rfor key
							skey.append(buffer, 1);
							if (sizeBuffPos_U == 0) {
								return(std::make_pair(-1, 0));
							}
							consistKeySize(skey);
							++buffer;

						}
						else {
							size_t offset = (sizeBuff - sizeBuffPos) - skey.size();
							offset = posBuff == 0 ? offset : offset + 1;		// increment because EOL
							return(std::make_pair(atoi(skey.c_str()), offset));
						}
					}
					return(std::make_pair(-1, 0));
				}
				++buffer;
				--sizeBuffPos;

				--sizeBuffPos_D;
				--sizeBuffPos_U;

			} while (sizeBuffPos > 0);

			return(std::make_pair(-1, 0));

		}


		inline size_t getFileIndex(size_t key)
		{
			return(getFileIndexInCache(vFileCacheInfo_, 0, vFileCacheInfo_.size() - 1, key));
		}

		inline void populateCacheIndex(size_t index, size_t key, size_t startBlock, size_t blockSize)
		{

			FileCacheInfo fileCacheInfo;

			fileCacheInfo.index = index;
			fileCacheInfo.key = key == 1 ? 0 : key;
			fileCacheInfo.startBlock = startBlock;
			fileCacheInfo.blockSize = blockSize;

			vFileCacheInfo_.push_back(fileCacheInfo);
		}

		inline std::pair<size_t, size_t> calculateBlockSize(size_t fileSize, size_t &maxReadBuffer)
		{
			size_t maxBlock = fileSize / maxReadBuffer;

			int index = 0;
			for (index = 0; index < MAXINDEX; ++index) {
				if (std::pow(2, index) > maxBlock) {			// calculate nro block using pow of 2
					break;

				}
			}

			size_t blockAmount = (size_t)pow(2, index);
			size_t blockSize = (size_t)(fileSize / blockAmount) + 1;

			std::pair<size_t, size_t> pairRet = std::make_pair(blockAmount, blockSize);
			return(pairRet);

		}

		inline void getRecordKey(char *posBuff, char *key)
		{
			char *posKey = key;
			for (; *posBuff != ' '; ++posBuff) {
				*posKey++ = *posBuff;

				if (posKey - key > 10) {
					throw ("Invalid key"); 
				}
			}
			*posKey = '\0';

		}

		inline char * calculateRecordKeyPosition(size_t posToRead, char *buffer, size_t byteToRead)
		{

			archive_->readFile(posToRead, buffer, byteToRead);

			char *posBuff = buffer;

			if (posToRead == 0) {
				return(posBuff);
			}

			do {						// look for start of record (after EOL)
				if (*posBuff == '\n') {
					return(posBuff + 1);
				}
				++posBuff;
			} while (posBuff < buffer + byteToRead);

			return(0);

		}
		inline void consistKeySize(std::string & key)
		{
			if (key.size() > 10) {
				std::stringstream ss;
				ss << "Invalid key size: " << key;
				throw(ss.str().c_str());
			}
		}
	private:
		Archive::Pointer archive_;
		size_t cacheFileSize_;

		VecFileCacheInfo vFileCacheInfo_;
		ICacheModel<size_t, char *>::Pointer cacheModelFile_;
		char **bufferCacheFile_;
		char *bufferCacheFileSwap_ = nullptr;
		char *bufferCacheFileSwapSav_ = nullptr;

		size_t recordSize_ = 32;
		
		size_t blockFoundInCache_ = 0;
		size_t blockInsertInCache_ = 0;

	};
}


#endif

