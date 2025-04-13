//
// Created by pavel on 09.12.2024.
//

#include <gtest/gtest.h>

#include "src/ChunkedRingBuffer.hpp"

#include "src/audio/RingBuffer.hpp"

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
class ChunkedBufferTest : public ::testing::Test {
};
class RingBufferTest : public ::testing::Test {
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

TEST_F(RingBufferTest, ChunkedBuffer) {
  RingBuffer<int, 1, 3, 3> buffer;
  // ASSERT_TRUE(buffer.IsEmpty());
  ASSERT_FALSE(buffer.HasChunks());
  auto v123 = std::vector{1, 2, 3};
  buffer.Push(std::vector{1, 2, 3});
  // ASSERT_FALSE(buffer.IsEmpty());
  ASSERT_TRUE(buffer.HasChunks());
  auto d = buffer.Retrieve();
  auto rv = std::vector(d.begin(), d.end());
  ASSERT_EQ(rv, v123);
  ASSERT_ANY_THROW(buffer.Retrieve());
};

TEST_F(RingBufferTest, WriteFull) {
  #define COMMA ,
  RingBuffer<int, 1, 3, 3> buffer;
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

TEST_F(RingBufferTest, WriteChannels) {
  #define COMMA ,
  RingBuffer<char, 2, 3, 3> buffer;
  auto in0 = std::vector{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  auto in1 = std::vector{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i'};
  auto v10 =  std::vector{'&'};
  buffer.PushChannel<0>(in0);
  ASSERT_ANY_THROW(buffer.PushChannel<0>(v10));
  ASSERT_FALSE(buffer.HasChunks());
  buffer.PushChannel<1>(in1);
  ASSERT_TRUE(buffer.HasChunks());
  auto r = buffer.Retrieve();
  auto rv = std::vector(r.begin(), r.end());

  auto sret = std::string("1a2b3c");
  auto vret = std::vector(sret.begin(), sret.end());
  ASSERT_EQ(rv, vret);

  auto s123 = std::string("123");
  auto v123 = std::vector(s123.begin(), s123.end());
  buffer.PushChannel<1>(v123);
  ASSERT_ANY_THROW(buffer.PushChannel<1>(v10));

  buffer.Retrieve();
  buffer.Retrieve();
  ASSERT_FALSE(buffer.HasChunks());
  buffer.PushChannel<0>(v123);
  ASSERT_TRUE(buffer.HasChunks());
  auto rr = buffer.Retrieve();
  auto rrv = std::vector(rr.begin(), rr.end());
  auto ss = std::string("112233");
  auto ssv = std::vector(ss.begin(), ss.end());
  ASSERT_EQ(rrv, ssv);

  ASSERT_FALSE(buffer.HasChunks());
};
