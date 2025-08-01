#pragma once

#ifndef LOGGER_H_
#define LOGGER_H_


//**************************************************************************************
//*
//*		Implement simple logger file
//*
//*		2021/03/26
//*		Rogerio Regis
//*
//*************************************************************************************

#include <string>
#include <memory>
#include <fstream>
#include <iostream>
#include <thread>
#include <sstream>

#include <chrono>



namespace Atlantis
{
//**************************************************************************************
//*
//*		class Logger
//*
//*************************************************************************************
	class Logger
	{
	public:
		using Pointer = std::unique_ptr<Logger>;

		template<typename ...Args>
		Pointer static create(Args &&...arg)
		{
			struct EnaleMakeShared
				: public Logger
			{
				EnaleMakeShared(Args &&...arg)
					: Logger(std::forward<Args>(arg)...)
				{}
			};
			Pointer result(std::make_unique<EnaleMakeShared>(std::forward<Args>(arg)...));
			return(std::move(result));
		}
		virtual ~Logger() = default;

		Logger(const Logger &) = delete;
		Logger(Logger &&) = delete;
		const Logger& operator=(const Logger&) = delete;


	private:

		explicit Logger(const std::string &logFileName)
		{
			if (logFileName.empty()) {
				return;
			}

			backup_ = std::cout.rdbuf();

			altLogFile_ = new std::ofstream(logFileName);
			std::cout.rdbuf(altLogFile_->rdbuf());
		}

		inline  std::thread::id getThreadId() const
		{
			return (std::this_thread::get_id());
		}
	public:
		static std::string
			sysTimePointToString(const std::chrono::system_clock::time_point &time, const std::string &format)
		{
			auto autoTimePointToString = [](const std::chrono::system_clock::time_point &time,
				const std::string &format)->std::string
			{
				std::time_t tt = std::chrono::system_clock::to_time_t(time);
				std::tm tm = *std::localtime(&tt); //GMT (UTC)
				//std::tm tm = *std::localtime(&tt); //Locale time-zone, usually UTC by default.
				std::stringstream ss;
				ss << std::put_time(&tm, format.c_str());
				return ss.str();
			};

			if (format.find(".") == std::string::npos) {
				return (autoTimePointToString(static_cast<std::chrono::system_clock::time_point>(time), format));
			}

			std::string formatFrac = format.substr(format.find(".") + 1, format.size() - format.find("."));
			std::string strDate = autoTimePointToString(time, format.substr(0, format.find(".")));

			auto ttime_t = std::chrono::system_clock::to_time_t(time);
			auto tp_sec = std::chrono::system_clock::from_time_t(ttime_t);

			long long fractCount = 0;
			int width = 0;
			if (formatFrac == "%m") {
				std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(time - tp_sec);
				fractCount = ms.count();
				width = 3;
			}
			else
				if (formatFrac == "%u") {
					std::chrono::microseconds ms = std::chrono::duration_cast<std::chrono::microseconds>(time - tp_sec);
					fractCount = ms.count();
					width = 7;
				}

			std::stringstream ss;
			ss << strDate << "." << std::left << std::setw(width) << std::setfill('0') << fractCount;

			return (ss.str());

		}


	private:
		std::streambuf *backup_;
		std::ofstream *altLogFile_;
	};
}



namespace Atlantis
{

#define  ALOG_WRITE(LOGTYPE, LOGMSG) \
{ \
    std::thread::id m_threadId = std::this_thread::get_id(); \
    std::stringstream ss; \
    ss  << "[" << Logger::sysTimePointToString(std::chrono::system_clock::now(), "%Y/%m/%d-%H:%M:%S.%m") << "]" \
        << "[" << m_threadId << "]" \
              << __FUNCTION__  << " " << LOGTYPE << " " << LOGMSG; \
    std::cout << ss.str() << std::endl << std::flush; \
    std::flush(std::cout); \
}

#define  ALOGERROR(LOGMSG)  ALOG_WRITE("Error:", LOGMSG)
#define  ALOGINFO(LOGMSG)   ALOG_WRITE("Info:", LOGMSG)
#define  ALOGWARN(LOGMSG)   ALOG_WRITE("Warn:", LOGMSG)
}

#endif //