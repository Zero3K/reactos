#include "gtest/gtest.h"
#include "Archive.h"
#include "ArchiveTest.h"

namespace AtlantisTest
{

	void ArchiveTest::SetUp()
	{

	}
	void ArchiveTest::TearDown()
	{
	}


	TEST(ArchiveTest, getFileSize)
	{
		Atlantis::Archive::Pointer archive = 
			Atlantis::Archive::create("frutas.txt");

		EXPECT_EQ(134, archive->getFileSize());
	}


	TEST(ArchiveTest, readFile)	
	{

		Atlantis::Archive::Pointer archive;
		EXPECT_NO_THROW(archive = Atlantis::Archive::create("frutas.txt"));

		char *buffer = new char[128];
		EXPECT_EQ(true, archive->readFile(63, buffer, 7));

		std::string sbuf(buffer, 7);
		EXPECT_EQ("morango", sbuf);
	}
}


