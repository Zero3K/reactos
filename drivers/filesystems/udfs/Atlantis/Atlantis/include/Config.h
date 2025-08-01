#pragma once

#ifndef CONFIG_H_
#define CONFIG_H_

//**************************************************************************************
//*
//*		Manager and configuration file storage 
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

#include "Archive.h"



namespace Atlantis
{
//**************************************************************************************
//*
//*		Interface IConfig
//*
//*************************************************************************************

	class IConfig
	{
	public:
		using Pointer = std::unique_ptr<IConfig>;
		virtual ~IConfig() = default;


	};
}
namespace Atlantis
{
//**************************************************************************************
//*
//*		template<typename KEY, typename VALUE>
//*		class Config
//*
//*************************************************************************************
	template<typename KEY, typename VALUE>
	class Config
		: public IConfig
	{
	public:
		using Pointer = std::shared_ptr<Config>;

		template<typename ...Args>
		Pointer static create(Args &&...arg)
		{
			struct EnaleMakeShared
				: public Config
			{
				EnaleMakeShared(Args &&...arg)
					: Config(std::forward<Args>(arg)...)
				{}
			};
			Pointer result(std::make_shared<EnaleMakeShared>(std::forward<Args>(arg)...));
			return(std::move(result));
		}
		virtual ~Config() = default;
	
		Config(const Config &) = delete;
		Config(Config &&) = delete;
		const Config& operator=(const Config&) = delete;

		void load()
		{
			size_t fileSize = archive_->getFileSize();


			char *buffer = new char[fileSize + 1];

			archive_->readFile(0, buffer, fileSize);

			std::regex self_regex("REGULAR EXPRESSIONS",
				std::regex_constants::ECMAScript | std::regex_constants::icase);
			if (std::regex_match(buffer, self_regex)) {
				std::cout << "Text contains the phrase 'regular expressions'\n";
			}

			std::regex word_regex("(.*)=(.*)");
			
			
			auto words_begin =
				std::cregex_iterator(buffer, buffer+fileSize, word_regex);
			auto words_end = std::cregex_iterator();


			
			for (std::cregex_iterator iRegex = words_begin; iRegex != words_end; ++iRegex) {
				std::cmatch match = *iRegex;
				std::string match_str = match.str();
				std::vector<std::string> out;
				split(match_str, '=', out);
				mapConfig[out[0]] = out[1];
			}


			delete[] buffer;
		}

		VALUE & getValue(KEY key)
		{
			return (mapConfig[key]);
		}

		int getValueInt(KEY key)
		{
			return (atoi(mapConfig[key].c_str()));
		}

		bool getValueBool(KEY key)
		{
			return (mapConfig[key] == "true" ? true : false);
		}
	protected:
		explicit Config(const std::string &fileName)
			
		{
			archive_ = Archive::create(fileName);
		}

	private:
		void split(std::string &str, char delim, std::vector<std::string> &out)
		{
			size_t start;
			size_t end = 0;

			while ((start = str.find_first_not_of(delim, end)) != std::string::npos) {
				end = str.find(delim, start);
				out.push_back(str.substr(start, end - start));
			}
		}

	private:
		Archive::Pointer archive_;
		std::unordered_map<KEY, VALUE> mapConfig;

	};
}


namespace Atlantis
{
//**************************************************************************************
//*
//*		class ConfigCache (used as interface)
//*
//*************************************************************************************
	
	class ConfigCache
		: public Config<std::string, std::string>
	{
	public:
		virtual ~ConfigCache() = default;
		ConfigCache(const ConfigCache &) = delete;
		ConfigCache(ConfigCache &&) = delete;
		const ConfigCache& operator=(const ConfigCache&) = delete;

	private:
		explicit ConfigCache(const std::string &fileName)
			: Config(fileName)
		{
		}


	};
}


#endif // !ARCHIVE_H_

