#pragma once

#ifndef ARCHIVE_H_
#define ARCHIVE_H_

//**************************************************************************************
//*
//*		Implement phisical aceess to file
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


#include "Logger.h"


namespace Atlantis
{
//**************************************************************************************
//*
//*		Interface IArchive
//*
//*************************************************************************************

	class IArchive
	{
	public:
		using Pointer = std::unique_ptr<IArchive>;
		virtual ~IArchive() = default;


	};
}
namespace Atlantis
{

//**************************************************************************************
//*	
//*		class IArchive
//*
//*************************************************************************************
	class Archive
		: public IArchive
	{
	public:
		using Pointer = std::shared_ptr<Archive>;

		template<typename ...Args>
		Pointer static create(Args &&...arg)
		{
			struct EnaleMakeShared
				: public Archive
			{
				EnaleMakeShared(Args &&...arg)
					: Archive(std::forward<Args>(arg)...)
				{}
			};
			Pointer result(std::make_shared<EnaleMakeShared>(std::forward<Args>(arg)...));
			return(std::move(result));
		}
		virtual ~Archive()
		{
			file_.close();
		}

		Archive(const Archive &) = delete;
		Archive(Archive &&) = delete;
		const Archive& operator=(const Archive&) = delete;

		
		size_t getFileSize()
		{
			file_.seekp(0, std::ios::end); // seek to the end of the file
			size_t eofOffset = (size_t)file_.tellp(); // get size of file	

			return(eofOffset);
		}

		bool readFile(size_t index, size_t position, char *buffer, size_t blockSize)
		{
			++readTime_;
#ifdef DEBUG
			ALOGINFO("Read file " << readTime_ << " I-" << index << " P-" << position << " Bs-" << blockSize);
#endif
			return(readFile(position, buffer, blockSize));
		}

		bool readFile(size_t position, char *buffer, size_t blockSize)
		{

			try {
				file_.seekp(position, std::ios::beg);
				if (!file_.read(buffer, blockSize)) {
					std::stringstream ss;
					ss << "Error. Read not entire completed of file: [" << blockSize << "] " << fileName_;
					throw(std::exception(ss.str().c_str()));
				}

			}
			catch (std::exception & ex) {
				std::stringstream ss;
				ss << "Error. Read error of file: " << fileName_ << " " << ex.what();
				throw(std::exception(ss.str().c_str()));
				
			}

			return(true);

		}
	private:
		explicit Archive(const std::string &fileName)
			: fileName_(fileName)
		{

			if (!std::filesystem::exists(fileName_)) {
				std::stringstream ss;
				ss << "Error. file not exist: " << fileName_;
				throw(std::exception(ss.str().c_str()));
			}

			file_.open(fileName_, std::fstream::in | std::fstream::binary);

		}


	private:
		const std::string fileName_;

		std::fstream file_;
		size_t readTime_ = 0;

	};
}


#endif // !ARCHIVE_H_

