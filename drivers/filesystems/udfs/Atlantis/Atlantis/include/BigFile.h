#pragma once
#ifndef BIGFILE_H_
#define BIGFILE_H_

//**************************************************************************************
//*
//*		Access Interface to file cache 
//*
//*		2021/03/26
//*		Rogerio Regis
//*
//*************************************************************************************

#include <string>
#include <sstream>
#include <memory>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>

#include "Archive.h"
#include "MgrCacheFile.h"
#include "CacheModel.h"
#include "WarmUp.h"
#include "Logger.h"

#define MAXINDEX 15

namespace Atlantis
{
//**************************************************************************************

//*		
//*		Interface IBigFile
//*
//*************************************************************************************

	class IBigFile
	{
	public:
		using Pointer = std::unique_ptr<IBigFile>;
		virtual ~IBigFile() = default;

		virtual void initialize(size_t maxReadBufferLength, size_t cacheRecordSize, size_t cacheFileSize) = 0;
		virtual const std::string  get(size_t key) = 0;

	};
}


namespace Atlantis
{
//**************************************************************************************
//*
//*		class BigFile
//*
//*************************************************************************************

	class BigFile
	{
	public:
		using Pointer = std::unique_ptr<BigFile>;

		template<typename ...Args>
		Pointer static create(Args &&...arg)
		{
			struct EnaleMakeShared
				: public BigFile
			{
				EnaleMakeShared(Args &&...arg)
					: BigFile(std::forward<Args>(arg)...)
				{}
			};
			Pointer result(std::make_unique<EnaleMakeShared>(std::forward<Args>(arg)...));
			return(std::move(result));
		}
		virtual ~BigFile()
		{
			ALOGINFO("GetValue Request: " << requestTotal_ <<
				", Request found in cache: " << requestFromRecordCache_ <<
				", Request Inserted into cache: " << requestInsertRecordCache_ <<
				", RequestNotFound: " << requestNotFound_);
		}

		BigFile(const BigFile &) = delete;
		BigFile(BigFile &&) = delete;
		const BigFile& operator=(const BigFile&) = delete;

		// maxReadBufferLength : max size of block file to read in bytes
		// cacheValueSize : size in nro of  itens for cache of records
		// cacheFileSize: size in nro of blocks for cache of blocks reads fo file
		void initialize(size_t maxReadBufferLength,  size_t cacheRecordSize, size_t cacheFileSize)
		{
			archive_ = Archive::create(fileName_);
			mgrCacheFile_ = MgrCacheFile::create(archive_, cacheFileSize);

			mgrCacheFile_->initialize(maxReadBufferLength, maxRecordSize_);

			cacheRecord_ = CacheRecord::create(cacheRecordSize);

		}

		void warmUp(size_t maxRecord, size_t minKey, size_t maxKey)
		{
			WarmUp::Pointer warmUp = WarmUp::create(cacheRecord_, mgrCacheFile_);
			warmUp->load(maxRecord, minKey, maxKey);

		}

		const std::string  get(size_t key)
		{

			++requestTotal_;

			// verify if record is into memory cache
			std::pair<bool, const std::string> pairRet = cacheRecord_->getValue(key);
			if (pairRet.first) {
				++requestFromRecordCache_;
				return(pairRet.second);
			}

			// get record from file and insert in memory cache if found
			std::pair<bool, const std::string> pairValue = mgrCacheFile_->getValue(key);
			if (pairValue.first) {
				++requestInsertRecordCache_;
				cacheRecord_->setValue(key, pairValue.second);
				return(pairValue.second);
			}
		
			++requestNotFound_;
			return(pairValue.second);
		}

	private:
		explicit BigFile(const std::string &fileName, size_t maxRecordSize)
			: fileName_(fileName), maxRecordSize_(maxRecordSize)
		{

			
		}

	
	private:
		const std::string fileName_;
		size_t maxRecordSize_;

		Archive::Pointer archive_;
		MgrCacheFile::Pointer mgrCacheFile_;
		CacheRecord::Pointer cacheRecord_;

		//statistics
		size_t requestTotal_ = 0;
		size_t requestFromRecordCache_ = 0;
		size_t requestInsertRecordCache_ = 0;
		size_t requestNotFound_ = 0;

	};
}


#endif // !BIGFILE_H_

