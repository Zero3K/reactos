#include <string>
#include <iostream>
#include <random>

#include "include/BigFile.h"
#include "include/Config.h"
#include "include/Logger.h"

#define MAXRECORDS	10000
void createFile(const std::string &fineName)
{
	if (std::filesystem::exists(fineName)) {
		return;
	}

	std::fstream file;
	file.open(fineName, std::fstream::out);

	for (int idx = 0; idx < 25000; ++idx) {
		std::stringstream ss;
		ss << idx << " " << idx << "-" << (idx % 3 ? "Triplo" : "Others fruits") << std::endl;
		file.write(ss.str().c_str(), ss.str().size());
	}

	file.close();
}
int main(int argc, const char * argv[])
{
	for (size_t idx = 0; idx < (size_t)argc; ++idx) {
		std::cout << "Parameter " << idx << " = " << argv[idx] << std::endl;
	}

	if (argc != 2) {
		std::cout << "Invalid exec parameters. Use argv[idx] <file config name> " << std::endl;
		return(-1);
	}

	std::string fileConfigName = argv[1];

	

	Atlantis::ConfigCache::Pointer config = Atlantis::ConfigCache::create(fileConfigName);
	config->load();

	Atlantis::Logger::Pointer logger = Atlantis::Logger::create(config->getValue("LogFileName"));
	
	if(config->getValueBool("GenerateFile")) {
		createFile(config->getValue("CacheFileName"));
	}


	Atlantis::BigFile::Pointer bigFile =
		Atlantis::BigFile::create(config->getValue("CacheFileName"), config->getValueInt("MaxRecordSize"));
	

	bigFile->initialize(config->getValueInt("MaxReadBufferLength"),
					config->getValueInt("CacheRecordSize"), config->getValueInt("CacheFileSize"));

	if (config->getValueBool("ExecuteWarmUp")) {
		bigFile->warmUp(config->getValueInt("WarmUpMaxRecord"),
					config->getValueInt("Warm	UpMinKey"), config->getValueInt("WarmUpMaxKey"));
	}
	

	size_t maxLoop = config->getValueInt("WarmUpMaxKey");

	for (size_t idx = maxLoop; idx != 0; --idx) {
		if (bigFile->get(idx) == "") {
			std::cout << idx << " " << "Not found" << std::endl;
			continue;
		}
		//		std::cout << idx << " " << bigFile->get(idx) << std::endl;
	}

	for (size_t idx = 1; idx < maxLoop +10; ++idx) {
		if (bigFile->get(idx) == "") {
			std::cout << idx << " " << "Not found" << std::endl;
			continue;
		}
//		std::cout << idx << " " << bigFile->get(idx) << std::endl;
	}
	
	return(0);
}