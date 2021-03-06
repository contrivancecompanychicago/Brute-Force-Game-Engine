#include <Core/String.h>

#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_SUITE(StringTest)

using namespace BFG;

BOOST_AUTO_TEST_CASE(testSplit)
{
	std::string token;
	std::string rest;
	std::string source = "first second third fourth";

	bool res = split(source, " ", token, rest);
	
	BOOST_CHECK(!token.empty());
	BOOST_CHECK_EQUAL(res, true);
	BOOST_CHECK_EQUAL(token, "first");
}

BOOST_AUTO_TEST_CASE(testSplit2)
{
	std::string token;
	std::string rest;
	std::string source = "first-*-second-*-third-*-fourth";

	bool res = split(source, "-*-", token, rest);
	
	BOOST_CHECK(!token.empty());
	BOOST_CHECK_EQUAL(res, true);
	BOOST_CHECK_EQUAL(token, "first");
}

BOOST_AUTO_TEST_CASE(testSplitNegative)
{
	std::string token;
	std::string rest;
	std::string source = "firstSecond";

	bool res = split(source, " ", token, rest);

	BOOST_CHECK(token.empty());
	BOOST_CHECK(rest.empty());
	BOOST_CHECK(!res);
}

BOOST_AUTO_TEST_CASE(testSplitAll)
{
	std::string source = "first second third fourth";
	std::vector<std::string> tokens;
	
	tokenize(source, " ", tokens);
	
	BOOST_CHECK(!tokens.empty());
	BOOST_REQUIRE_EQUAL(tokens.size(), 4);
	BOOST_CHECK_EQUAL(tokens[0], "first");
	BOOST_CHECK_EQUAL(tokens[1], "second");
	BOOST_CHECK_EQUAL(tokens[2], "third");
	BOOST_CHECK_EQUAL(tokens[3], "fourth");
}

BOOST_AUTO_TEST_CASE(testSplitAll2)
{
	std::string source = "first-*-second-*-third-*-fourth";
	std::vector<std::string> tokens;
	
	tokenize(source, "-*-", tokens);
	
	BOOST_CHECK(!tokens.empty());
	BOOST_REQUIRE_EQUAL(tokens.size(), 4);
	BOOST_CHECK_EQUAL(tokens[0], "first");
	BOOST_CHECK_EQUAL(tokens[1], "second");
	BOOST_CHECK_EQUAL(tokens[2], "third");
	BOOST_CHECK_EQUAL(tokens[3], "fourth");
}

BOOST_AUTO_TEST_SUITE_END()