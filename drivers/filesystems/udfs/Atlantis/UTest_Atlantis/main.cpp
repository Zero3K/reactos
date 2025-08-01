#include "gtest/gtest.h"

GTEST_API_ int main(int argc, char * argv[])
{
	//GTest Initialize

	::testing::InitGoogleTest(&argc, argv);


	std::streambuf *old = std::cout.rdbuf(); 
	std::stringstream ss;

	std::cout.rdbuf(ss.rdbuf());       // redirect

	

	int retCode = 0;
	//GTest Run all tests cases
	try {
		retCode = RUN_ALL_TESTS();
	}
	catch (...) {
		std::cout << "erro" << std::endl;
		return(1);
	}

	return(retCode);
}