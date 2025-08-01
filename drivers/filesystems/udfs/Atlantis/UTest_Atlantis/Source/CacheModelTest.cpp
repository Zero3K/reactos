#include "gtest/gtest.h"
#include "CacheModel.h"
#include "CacheModelTest.h"

namespace AtlantisTest
{

	void CacheModelTest::SetUp()
	{

	}
	void CacheModelTest::TearDown()
	{
	}


	TEST(CacheModelTest, getAndSetValue)
	{

		Atlantis::CacheRecord::Pointer cacheModel = Atlantis::CacheRecord::create(150);


		std::pair<bool, std::string> pairValue = cacheModel->getValue(10);
		EXPECT_EQ(false, pairValue.first);

		cacheModel->setValue(10, "Atlantis10");
		cacheModel->setValue(11, "Atlantis11");
		cacheModel->setValue(12, "Atlantis12");

		pairValue = cacheModel->getValue(10);
		EXPECT_EQ(true, pairValue.first);

		EXPECT_EQ("Atlantis10", pairValue.second);

	}

	TEST(CacheModelTest, getValueLRU)
	{

		Atlantis::CacheRecord::Pointer cacheModel = Atlantis::CacheRecord::create(3);


		
		cacheModel->setValue(10, "Atlantis10");
		cacheModel->setValue(11, "Atlantis11");
		cacheModel->setValue(12, "Atlantis12");

		std::pair<int, std::string> pairValue;

		pairValue = cacheModel->setValue(13, "Atlantis13");
		EXPECT_EQ(10, pairValue.first);		// [10], 11, 12, 13

		pairValue = cacheModel->setValue(14, "Atlantis13");
		EXPECT_EQ(11, pairValue.first);		// [11], 12, 13, 14

		cacheModel->getValue(12);			// 13, 14, 12

		pairValue = cacheModel->setValue(15, "Atlantis15");
		EXPECT_EQ(13, pairValue.first);		// [13], 14, 12, 15

		pairValue = cacheModel->setValue(16, "Atlantis16");
		EXPECT_EQ(14, pairValue.first);		// [14], 12, 15, 16

		pairValue = cacheModel->setValue(17, "Atlantis17");
		EXPECT_EQ(12, pairValue.first);		// [12], 15, 16, 17

	}

	TEST(CacheModelTest, duplicateSetValue)
	{

		Atlantis::CacheRecord::Pointer cacheModel = Atlantis::CacheRecord::create(3);


		std::pair<bool, std::string> pairValue;
		std::pair<int, std::string> pairSetValue;

		cacheModel->setValue(10, "Atlantis10");
		cacheModel->setValue(11, "Atlantis11");
		cacheModel->setValue(12, "Atlantis12");


		pairValue = cacheModel->getValue(10);
		EXPECT_EQ(true, pairValue.first);
		EXPECT_EQ("Atlantis10", pairValue.second);

		pairSetValue = cacheModel->setValue(17, "Atlantis17");
		EXPECT_EQ(11, pairSetValue.first);		// [11], 12, 10, 17

		pairValue = cacheModel->getValue(12);	// 10, 17, 12

		cacheModel->setValue(10, "ValueReplaced");
		pairValue = cacheModel->getValue(10);	// 17, 12, 10	
		EXPECT_EQ(true, pairValue.first);
		EXPECT_EQ("ValueReplaced", pairValue.second);

		pairSetValue = cacheModel->setValue(18, "Atlantis18");
		EXPECT_EQ(17, pairSetValue.first);	// [17], 12, 10, 18

	}
	
}


