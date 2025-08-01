#pragma once

#ifndef CACHEMODEL_H_
#define CACHEMODEL_H_

//**************************************************************************************
//*
//*		Implement RLU container
//*
//*		2021/03/26
//*		Rogerio Regis
//*
//*************************************************************************************

#include <string>
#include <sstream>
#include <memory>
#include <list>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <map>




namespace Atlantis
{
//**************************************************************************************
//*
//*		template<typename CACHEKEY, typename CACHEVALUE>
//*		Interface ICacheModel
//*
//*************************************************************************************
	template<typename CACHEKEY, typename CACHEVALUE>
	class ICacheModel
	{
	public:
		using Pointer = std::shared_ptr<ICacheModel<CACHEKEY, CACHEVALUE>>;
		virtual ~ICacheModel() = default;

		virtual std::pair<bool, CACHEVALUE> getValue(CACHEKEY key) = 0;
		virtual std::pair<CACHEKEY, CACHEVALUE> setValue(CACHEKEY key, CACHEVALUE value) = 0;

	};
}

namespace Atlantis
{
//**************************************************************************************
//*
//*		template<typename CACHEKEY, typename CACHEVALUE>
//*		class CacheModelLRU
//*
//*************************************************************************************
	template<typename CACHEKEY, typename CACHEVALUE>
	class CacheModelLRU
		: public ICacheModel<CACHEKEY, CACHEVALUE>

	{
	public:
		using Pointer = std::shared_ptr<CacheModelLRU<CACHEKEY, CACHEVALUE>>;
		using PairCache = std::pair<CACHEKEY, CACHEVALUE>;
		using ListCache = std::list<PairCache>;
		using MapCache = std::unordered_map<CACHEKEY, typename ListCache::iterator>;

		template<typename ...Args>
		Pointer static create(Args &&...arg)
		{
			struct EnaleMakeShared
				: public CacheModelLRU
			{
				EnaleMakeShared(Args &&...arg)
					: CacheModelLRU(std::forward<Args>(arg)...)
				{}
			};
			Pointer result(std::make_shared<EnaleMakeShared>(std::forward<Args>(arg)...));
			return(std::move(result));
		}
		virtual ~CacheModelLRU() = default;

		CacheModelLRU(const CacheModelLRU &) = delete;
		CacheModelLRU(CacheModelLRU &&) = delete;
		const CacheModelLRU& operator=(const CacheModelLRU&) = delete;



		std::pair<bool, CACHEVALUE> getValue(CACHEKEY key) override
		{
			
			if (mChache_.find(key) == mChache_.end()) {		// not found
				CACHEVALUE value;
				return(std::make_pair(false, value));
			}
	
			CACHEVALUE value = mChache_[key]->second;
			lCache_.erase(mChache_[key]);


			lCache_.push_front(std::make_pair(key, value));
			mChache_[key] = lCache_.begin();

			return(std::make_pair(true, value));
		}

		PairCache setValue(CACHEKEY key, CACHEVALUE value) override
		{
			
			PairCache pairLast;

			if (mChache_.find(key) != mChache_.end()) {  //found
				mChache_[key]->second;
				lCache_.erase(mChache_[key]);
			}
			else {
				if (lCache_.size() == cacheSize_) {
					pairLast = lCache_.back();
					lCache_.pop_back();
					mChache_.erase(pairLast.first);
				}
			}


			lCache_.push_front(std::make_pair(key, value));
			mChache_[key] = lCache_.begin();
		
			
			return(pairLast);
		}
		const std::string print()
		{
			std::stringstream ss;
			for (auto iCache = lCache_.begin(); iCache != lCache_.end(); ++iCache) {
				ss << (*iCache.first) << " ";
			}

			return(ss.str());
		}

	protected:
		explicit CacheModelLRU(size_t cacheSize)
			: cacheSize_(cacheSize)
		{
		}


	private:
		ListCache lCache_;
		MapCache mChache_;
		int cacheSize_;
	};
}


namespace Atlantis
{
//**************************************************************************************
//*
//*		template<typename CACHEKEY, typename CACHEVALUE>
//*		class LRUCache
//*
//*************************************************************************************
	
	template <class CACHEKEY, class CACHEVALUE>
	class LRUCache
		: public ICacheModel<CACHEKEY, CACHEVALUE>
	{
	public:
		using Pointer = std::shared_ptr<LRUCache<CACHEKEY, CACHEVALUE>>;
		using PairCache = std::pair<CACHEKEY, CACHEVALUE>;
		using ListCache = std::list<PairCache>;
		
		template<typename ...Args>
		Pointer static create(Args &&...arg)
		{
			struct EnaleMakeShared
				: public LRUCache
			{
				EnaleMakeShared(Args &&...arg)
					: LRUCache(std::forward<Args>(arg)...)
				{}
			};
			Pointer result(std::make_shared<EnaleMakeShared>(std::forward<Args>(arg)...));
			return(std::move(result));
		}

		virtual ~LRUCache() = default;

		LRUCache(const LRUCache &) = delete;
		LRUCache(LRUCache &&) = delete;
		const LRUCache& operator=(const LRUCache&) = delete;

		PairCache setValue( CACHEKEY key,  CACHEVALUE val)  override
		{
			auto iItemMap = itemMap_.find(key);

			if (iItemMap != itemMap_.end()) {
				itemList_.erase(iItemMap->second);
				itemMap_.erase(iItemMap);
			}

			itemList_.push_front(std::make_pair(key, val));
			itemMap_.insert(std::make_pair(key, itemList_.begin()));

			return(clean());
		};



		std::pair<bool, CACHEVALUE> getValue( CACHEKEY key) override
		{
			if (itemMap_.count(key) == 0) {
				return(std::make_pair(false, ""));
			}

			auto iItemMap = itemMap_.find(key);
			itemList_.splice(itemList_.begin(), itemList_, iItemMap->second);
	
			return(std::make_pair(true, iItemMap->second->second));
		};

	protected:
		explicit LRUCache(size_t cacheSize)
			: cacheSize_(cacheSize)
		{
		};

	private:
		inline PairCache clean(void)
		{
			PairCache pairClean;

			while (itemMap_.size() > cacheSize_) {
				auto iItemMapLast = itemList_.end(); 
				iItemMapLast--;
				pairClean = std::make_pair(iItemMapLast->first, iItemMapLast->second);
				itemMap_.erase(iItemMapLast->first);
				itemList_.pop_back();
			}

			return (pairClean);
		};

	private:
		ListCache itemList_;
		using MapCache = std::unordered_map<CACHEKEY, decltype(itemList_.begin())>;
		MapCache itemMap_;
		size_t cacheSize_;
	};
}
	


//#define CACHEMODEL LRUCache
//#define CACHEMODEL CacheModelLRU
namespace Atlantis
{
//**************************************************************************************
//*
//*		class CacheRecord (used as interface)
//*
//*************************************************************************************
	class CacheRecord
		: public CacheModelLRU<int, std::string>
	{
	public:
		virtual ~CacheRecord() = default;
		CacheRecord(const CacheRecord &) = delete;
		CacheRecord(CacheRecord &&) = delete;
		const CacheRecord& operator=(const CacheRecord&) = delete;

	private:
		explicit CacheRecord(size_t cacheSize)
			: CacheModelLRU(cacheSize)

		{
		}


	};

}

#endif // !CACHEMODEL_H_


