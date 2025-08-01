#include "gtest/gtest.h"
#include "BigFile.h"
#include "BigFileTest.h"

namespace AtlantisTest
{

	void BigFileTest::SetUp()
	{

	}
	void BigFileTest::TearDown()
	{
	}


	TEST(BigFileTest, get)
	{
		Atlantis::BigFile::Pointer bigFile;
		EXPECT_NO_THROW(bigFile = Atlantis::BigFile::create("Frutas.txt", 16));

		bigFile->initialize(1024, 500, 5);

		EXPECT_EQ("uva", bigFile->get(6));
		EXPECT_EQ("abacaxi", bigFile->get(1));
		
		EXPECT_EQ("", bigFile->get(14));

		EXPECT_EQ("laranja", bigFile->get(13));


	}

	TEST(BigFileTest, getAll)
	{
		Atlantis::BigFile::Pointer bigFile;
		EXPECT_NO_THROW(bigFile = Atlantis::BigFile::create("Frutas.txt", 16));

		bigFile->initialize(1024, 500, 5);


		for (size_t idx = 1; idx < 13; ++idx) {
			EXPECT_NE("", bigFile->get(idx));
		}
	

	}

}


