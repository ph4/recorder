//
// Created by pavel on 09.12.2024.
//

#include <gtest/gtest.h>

#include "src/ChunkedRingBuffer.hpp"

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
class ChunkedBufferTest : public ::testing::Test {
};

TEST_F(ChunkedBufferTest, ChunkedBuffer) {
  ChunkedBuffer<int, 3, 3> buffer;
  ASSERT_TRUE(buffer.IsEmpty());
  ASSERT_FALSE(buffer.HasChunks());
  auto v123 = std::vector{1, 2, 3};
  buffer.Push(std::vector{1, 2, 3});
  ASSERT_FALSE(buffer.IsEmpty());
  ASSERT_TRUE(buffer.HasChunks());
  auto d = buffer.Retrieve();
  auto rv = std::vector(d.begin(), d.end());
  ASSERT_EQ(rv, v123);
  ASSERT_ANY_THROW(buffer.Retrieve());
};

TEST_F(ChunkedBufferTest, WriteFull) {
  #define COMMA ,
  ChunkedBuffer<int, 3, 3> buffer;
  auto in = std::vector{1, 2, 3, 4, 5, 6, 7, 8, 9};
  auto v10 =  std::vector{10};
  buffer.Push(in);
  ASSERT_ANY_THROW(buffer.Push(v10));
  auto r = buffer.Retrieve();
  auto rv = std::vector(r.begin(), r.end());
  auto v123 = std::vector{1, 2, 3};
  ASSERT_EQ(rv, v123);
  buffer.Push(v123);
  ASSERT_ANY_THROW(buffer.Push(v10));
};
