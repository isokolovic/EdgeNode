#include "core/result.h"

#include <gtest/gtest.h>
#include <string>

namespace tests {

using coretypes::Result;

// IoError stands in for a real HAL error enum: Result is meant to carry an error
// code instead of throwing, since exceptions are banned in HAL and protocol code.
enum class IoError { TIMEOUT, CLOSED };

// The ok state must expose the value and report exactly one of is_ok/is_err.
TEST(Result, OkHoldsValue)
{
	auto r = Result<int, IoError>::ok(7);

	EXPECT_TRUE(r.is_ok());
	EXPECT_FALSE(r.is_err());
	EXPECT_EQ(r.value(), 7);
}

// The error must return the same value that was passed in. 
TEST(Result, ErrHoldsError)
{
	auto r = Result<int, IoError>::err(IoError::TIMEOUT);

	EXPECT_TRUE(r.is_err());
	EXPECT_FALSE(r.is_ok());
	EXPECT_EQ(r.error(), IoError::TIMEOUT);
}

// value_or lets a caller supply a default instead of checking is_ok() first. 
// The fallback must only be used in the error case.
TEST(Result, ValueOrReturnsFallbackOnError)
{
	auto ok = Result<int, IoError>::ok(5);
	auto err = Result<int, IoError>::err(IoError::CLOSED);

	EXPECT_EQ(ok.value_or(99), 5);
	EXPECT_EQ(err.value_or(99), 99);
}

// Result must support move-only value types. This test constructs a Result from an rvalue std::string.
TEST(Result, SupportsMoveOnlyValueTypes)
{
	auto r = Result<std::string, IoError>::ok(std::string("payload")); // ::ok() constructs from an rvalue

	ASSERT_TRUE(r.is_ok());
	EXPECT_EQ(r.value(), "payload");
}

// This test also proves that Result can be constructed from an rvalue and still return modified values
TEST(Result, MutableValueAccessor)
{
	auto r = Result<int, IoError>::ok(1); // ::ok() constructs from an rvalue
	r.value() = 42; // assign through the reference

	EXPECT_EQ(r.value(), 42);
}

} // namespace tests
