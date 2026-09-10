
#include <gtest/gtest.h>

#include "edge_protocol_core.h"

namespace tests {

// @brief Test checks that the SequenceCounter starts at zero and increments correctly. 
// Also verifies that the peek function returns the expected value without advancing the counter.
TEST(SequenceCounter, StartsAtZeroAndIncrements)
{
	protocol::SequenceCounter seq;
	EXPECT_EQ(seq.peek(), 0);
	EXPECT_EQ(seq.next(), 0);
	EXPECT_EQ(seq.next(), 1);
	EXPECT_EQ(seq.peek(), 2);
}

// @brief Verify that SequenceCounter wraps to zero after reaching 255.
TEST(SequenceCounter, WrapsAfter255)
{
	protocol::SequenceCounter seq;
	for (int i = 0; i < 255; ++i)
		seq.next();

	EXPECT_EQ(seq.next(), 255);
	EXPECT_EQ(seq.next(), 0);
}

// @brief Verify that reset() restores SequenceCounter value to zero.
TEST(SequenceCounter, ResetReturnsToZero)
{
	protocol::SequenceCounter seq;
	seq.next();
	seq.next();
	seq.reset();
	EXPECT_EQ(seq.next(), 0);
}

// @brief Verify that seq_gap returns zero when sequence numbers match.
TEST(SequenceCounter, GapDetectsExpectedFrame)
{
	EXPECT_EQ(protocol::seq_gap(7, 7), 0);
}

// @brief Verify that seq_gap correctly calculates dropped frames count.
TEST(SequenceCounter, GapCountsDroppedFrames)
{
	EXPECT_EQ(protocol::seq_gap(10, 13), 3);
}

// @brief Verify that seq_gap correctly handles wrapping across zero.
TEST(SequenceCounter, GapWrapsAcrossZero)
{
	// Expected 254, received 1 -> three frames were lost across the wrap.
	EXPECT_EQ(protocol::seq_gap(254, 1), 3);
}

// @brief Verify that an old sequence number appears as a large gap (replay).
TEST(SequenceCounter, ReplayShowsAsLargeGap)
{
	// An older frame appears as a distance above 127, which callers treat as a replay.
	EXPECT_GT(protocol::seq_gap(10, 5), 127);
}

} // namespace tests
