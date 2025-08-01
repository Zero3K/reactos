#include "gtest/gtest.h"
#include "Config.h"
#include "ConfigTest.h"

namespace AtlantisTest
{

	void ConfigTest::SetUp()
	{

	}
	void ConfigTest::TearDown()
	{
	}


	TEST(ConfigTest, getValue)
	{
		Atlantis::ConfigCache::Pointer config;
		EXPECT_NO_THROW(config = Atlantis::ConfigCache::create("Config.txt"));

		config->load();

		EXPECT_EQ(".\\frutas.txt", config->getValue("CacheFileName"));
		EXPECT_EQ(500, config->getValueInt("CacheRecordSize"));

		EXPECT_EQ("", config->getValue("NotValid"));

		

	}

	
}


