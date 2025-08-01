#pragma once

#ifndef WARMUP_H_
#define WARMUP_H_

//**************************************************************************************
//*
//*		Warm up random record in cache
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
#include <regex>
#include <random>

#include "Archive.h"


namespace Atlantis
{
//**************************************************************************************
//*	
//*		Interface IWarmUp
//*
//*************************************************************************************

	class IWarmUp
	{
	public:
		using Pointer = std::unique_ptr<IWarmUp>;
		virtual ~IWarmUp() = default;


	};
}
namespace Atlantis
{
//**************************************************************************************
//*
//*		class WarmUp
//*
//*************************************************************************************
class WarmUp
		: public IWarmUp
	{
	public:
		using Pointer = std::unique_ptr<WarmUp>;

		template<typename ...Args>
		Pointer static create(Args &&...arg)
		{
			struct EnaleMakeShared
				: public WarmUp
			{
				EnaleMakeShared(Args &&...arg)
					: WarmUp(std::forward<Args>(arg)...)
				{}
			};
			Pointer result(std::make_unique<EnaleMakeShared>(std::forward<Args>(arg)...));
			return(std::move(result));
		}
		virtual ~WarmUp() = default;

		WarmUp(const WarmUp &) = delete;
		WarmUp(WarmUp &&) = delete;
		const WarmUp& operator=(const WarmUp&) = delete;

		void load(size_t maxRecord, size_t minKey, size_t maxKey)
		{
			std::random_device rd;			//Will be used to obtain a seed for the random number engine
			std::mt19937 gen(rd());			//Standard mersenne_twister_engine seeded with rd()
			std::uniform_int_distribution<> distrib(minKey, maxKey);

			for (size_t idx = 0; idx < maxRecord; ++idx) {

				size_t key = distrib(gen);
				std::pair<bool, const std::string> pairRet = cacheRecord_->getValue(key);
				if (pairRet.first) {
					continue;
				}
				std::pair<bool, const std::string> pairValue = mgrCacheFile_->getValue(key);
				if (pairValue.first) {
					cacheRecord_->setValue(key, pairValue.second);
				}
				
			}
		}
	private:
		explicit WarmUp(CacheRecord::Pointer cacheRecord, MgrCacheFile::Pointer mgrCacheFile)
			: cacheRecord_(cacheRecord), mgrCacheFile_(mgrCacheFile)
		{
		}

	private:
		CacheRecord::Pointer cacheRecord_;
		MgrCacheFile::Pointer mgrCacheFile_;
		
	};
}


#endif // !ARCHIVE_H_

