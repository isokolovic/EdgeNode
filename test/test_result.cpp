#include "core/result.h"

#include <gtest/gtest.h>
#include <string>

namespace edgenode::tests {

using core::Result;

enum class IoError { TIMEOUT, CLOSED };

/// @brief  Test checks that a Result in the success state correctly holds and returns the value, and that the is_ok() and is_err() methods reflect the correct state.
TEST(Result, OkHoldsValue)
{
	auto r = Result<int, IoError>::ok(7);

	EXPECT_TRUE(r.is_ok());
	EXPECT_FALSE(r.is_err());
	EXPECT_EQ(r.value(), 7);
}

/// @brief Test checks that a Result in the error state correctly holds and returns the error, and that the is_ok() and is_err() methods reflect the correct state.
TEST(Result, ErrHoldsError)
{
	auto r = Result<int, IoError>::err(IoError::TIMEOUT);

	EXPECT_TRUE(r.is_err());
	EXPECT_FALSE(r.is_ok());
	EXPECT_EQ(r.error(), IoError::TIMEOUT);
}

/// @brief Test checks that the value_or() method returns the contained value when the Result is in the success state, and returns the provided fallback value when the Result is in the error state.
TEST(Result, ValueOrReturnsFallbackOnError)
{
	auto ok = Result<int, IoError>::ok(5);
	auto err = Result<int, IoError>::err(IoError::CLOSED);

	EXPECT_EQ(ok.value_or(99), 5);
	EXPECT_EQ(err.value_or(99), 99);
}

/// @brief Test checks that a Result can hold and return move-only types (like std::string), and that the move constructor of the value is properly utilized when constructing a Result in the success state with an rvalue.
TEST(Result, SupportsMoveOnlyValueTypes)
{
	auto r = Result<std::string, IoError>::ok(std::string("payload"));

	ASSERT_TRUE(r.is_ok());
	EXPECT_EQ(r.value(), "payload");
}

/// @brief Test checks that the non-const value() accessor allows modifying the contained value when the Result is in the success state, and that the changes are reflected when accessing the value again.
TEST(Result, MutableValueAccessor)
{
	auto r = Result<int, IoError>::ok(1);
	r.value() = 42;

	EXPECT_EQ(r.value(), 42);
}

} // namespace edgenode::tests
