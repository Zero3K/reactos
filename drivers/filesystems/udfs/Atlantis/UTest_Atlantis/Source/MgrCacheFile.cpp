#include "gtest/gtest.h"
#include "MgrCacheFile.h"
#include "MgrCacheFileTest.h"

namespace AtlantisTest
{

	void MgrCacheFileTest::SetUp()
	{

	}
	void MgrCacheFileTest::TearDown()
	{
	}


	TEST(MgrCacheFileTest, getValue)
	{

		Atlantis::Archive::Pointer archive;
		EXPECT_NO_THROW(archive = Atlantis::Archive::create("frutas.txt"));

		Atlantis::MgrCacheFile::Pointer mgrCacheFile;
		EXPECT_NO_THROW(mgrCacheFile = Atlantis::MgrCacheFile::create(archive, 3));



		mgrCacheFile->initialize(1024, 16);

		std::pair<bool, std::string> pairValue;

		pairValue = mgrCacheFile->getValue(6);
		EXPECT_EQ(true, pairValue.first);
		EXPECT_EQ("uva", pairValue.second);


		pairValue = mgrCacheFile->getValue(1);
		EXPECT_EQ(true, pairValue.first);
		EXPECT_EQ("abacaxi", pairValue.second);

		pairValue = mgrCacheFile->getValue(15);
		EXPECT_EQ(false, pairValue.first);

	}

}


