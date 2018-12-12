/*
 * Copyright (C) 2017  Andrew Gunnerson <andrewgunnerson@gmail.com>
 *
 * This file is part of DualBootPatcher
 *
 * DualBootPatcher is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * DualBootPatcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with DualBootPatcher.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gmock/gmock.h>

#include <algorithm>
#include <memory>
#include <vector>

#include <cinttypes>

#include "mbcommon/file/memory.h"
#include "mbcommon/file_error.h"
#include "mbcommon/file_util.h"
#include "mbcommon/string.h"

#include "file/mock_test_file.h"

using namespace mb;
using namespace testing;

struct FileUtilTest : Test
{
    NiceMock<MockFile> _file;

    void SetUp() override
    {
        EXPECT_CALL(_file, is_open())
                .WillRepeatedly(Return(true));
    }
};

TEST_F(FileUtilTest, ReadRetryNormal)
{
    EXPECT_CALL(_file, read(_))
            .Times(5)
            .WillRepeatedly(Return(2u));

    std::byte buf[10];
    ASSERT_EQ(file_read_retry(_file, span(buf)), oc::success(10u));
}

TEST_F(FileUtilTest, ReadRetryEOF)
{
    EXPECT_CALL(_file, read(_))
            .Times(5)
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(0u));

    std::byte buf[10];
    ASSERT_EQ(file_read_retry(_file, span(buf)), oc::success(8u));
}

TEST_F(FileUtilTest, ReadRetryInterrupted)
{
    auto eintr = std::make_error_code(std::errc::interrupted);

    EXPECT_CALL(_file, read(_))
            .Times(5)
            .WillOnce(Return(eintr))
            .WillOnce(Return(eintr))
            .WillOnce(Return(eintr))
            .WillOnce(Return(eintr))
            .WillOnce(Return(10u));

    std::byte buf[10];
    ASSERT_EQ(file_read_retry(_file, span(buf)), oc::success(10u));
}

TEST_F(FileUtilTest, ReadRetryFailure)
{
    EXPECT_CALL(_file, read(_))
            .Times(1)
            .WillOnce(Return(std::error_code()));

    std::byte buf[10];
    ASSERT_EQ(file_read_retry(_file, span(buf)),
              oc::failure(std::error_code()));
}

TEST_F(FileUtilTest, WriteRetryNormal)
{
    EXPECT_CALL(_file, write(_))
            .Times(5)
            .WillRepeatedly(Return(2u));

    ASSERT_EQ(file_write_retry(_file, as_bytes(span("xxxxxxxxxx", 10u))),
              oc::success(10u));
}

TEST_F(FileUtilTest, WriteRetryEOF)
{
    EXPECT_CALL(_file, write(_))
            .Times(5)
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(0u));

    ASSERT_EQ(file_write_retry(_file, as_bytes(span("xxxxxxxxxx", 10u))),
              oc::success(8u));
}

TEST_F(FileUtilTest, WriteRetryInterrupted)
{
    auto eintr = std::make_error_code(std::errc::interrupted);

    EXPECT_CALL(_file, write(_))
            .Times(5)
            .WillOnce(Return(eintr))
            .WillOnce(Return(eintr))
            .WillOnce(Return(eintr))
            .WillOnce(Return(eintr))
            .WillOnce(Return(10u));

    ASSERT_EQ(file_write_retry(_file, as_bytes(span("xxxxxxxxxx", 10u))),
              oc::success(10u));
}

TEST_F(FileUtilTest, WriteRetryFailure)
{
    EXPECT_CALL(_file, write(_))
            .Times(1)
            .WillOnce(Return(std::error_code()));

    ASSERT_EQ(file_write_retry(_file, as_bytes(span("xxxxxxxxxx", 10u))),
              oc::failure(std::error_code()));
}

TEST_F(FileUtilTest, ReadExactNormal)
{
    EXPECT_CALL(_file, read(_))
            .Times(5)
            .WillRepeatedly(Return(2u));

    std::byte buf[10];
    ASSERT_TRUE(file_read_exact(_file, span(buf)));
}

TEST_F(FileUtilTest, ReadExactEOF)
{
    EXPECT_CALL(_file, read(_))
            .Times(5)
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(0u));

    std::byte buf[10];
    ASSERT_EQ(file_read_exact(_file, span(buf)),
              oc::failure(FileError::UnexpectedEof));
}

TEST_F(FileUtilTest, ReadExactPartialFail)
{
    EXPECT_CALL(_file, read(_))
            .Times(5)
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(std::error_code()));

    std::byte buf[10];
    ASSERT_EQ(file_read_exact(_file, span(buf)),
              oc::failure(std::error_code()));
}

TEST_F(FileUtilTest, WriteExactNormal)
{
    EXPECT_CALL(_file, write(_))
            .Times(5)
            .WillRepeatedly(Return(2u));

    ASSERT_TRUE(file_write_exact(_file, as_bytes(span("xxxxxxxxxx", 10))));
}

TEST_F(FileUtilTest, WriteExactEOF)
{
    EXPECT_CALL(_file, write(_))
            .Times(5)
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(0u));

    ASSERT_EQ(file_write_exact(_file, as_bytes(span("xxxxxxxxxx", 10))),
              oc::failure(FileError::UnexpectedEof));
}

TEST_F(FileUtilTest, WriteExactPartialFail)
{
    EXPECT_CALL(_file, write(_))
            .Times(5)
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(std::error_code()));

    ASSERT_EQ(file_write_exact(_file, as_bytes(span("xxxxxxxxxx", 10))),
              oc::failure(std::error_code()));
}

TEST_F(FileUtilTest, ReadDiscardNormal)
{
    EXPECT_CALL(_file, read(_))
            .Times(5)
            .WillRepeatedly(Return(2u));

    ASSERT_EQ(file_read_discard(_file, 10), oc::success(10u));
}

TEST_F(FileUtilTest, ReadDiscardEOF)
{
    EXPECT_CALL(_file, read(_))
            .Times(5)
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(0u));

    ASSERT_EQ(file_read_discard(_file, 10), oc::success(8u));
}

TEST_F(FileUtilTest, ReadDiscardPartialFail)
{
    EXPECT_CALL(_file, read(_))
            .Times(5)
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(2u))
            .WillOnce(Return(std::error_code()));

    ASSERT_EQ(file_read_discard(_file, 10), oc::failure(std::error_code()));
}

struct FileSearchTest : Test
{
    // Callback counters
    int _n_result = 0;
    FileSearchResultCallback _cb;

    FileSearchTest()
        : _cb(std::bind(&FileSearchTest::_result_cb, this,
                        std::placeholders::_1, std::placeholders::_2))
    {
    }

    oc::result<FileSearchAction> _result_cb(File &file, uint64_t offset)
    {
        (void) file;
        (void) offset;

        ++_n_result;

        return FileSearchAction::Continue;
    }
};

TEST_F(FileSearchTest, CheckInvalidBoundariesFail)
{
    MemoryFile file(span<std::byte>{});
    ASSERT_TRUE(file.is_open());

    ASSERT_EQ(file_search(file, 20, 10, 0, as_bytes(span("x", 1)), {}, _cb),
              oc::failure(FileError::ArgumentOutOfRange));
}

TEST_F(FileSearchTest, CheckZeroMaxMatches)
{
    MemoryFile file(span<std::byte>{});
    ASSERT_TRUE(file.is_open());

    ASSERT_TRUE(file_search(file, {}, {}, 0, as_bytes(span("x", 1)), 0, _cb));
}

TEST_F(FileSearchTest, CheckZeroPatternSize)
{
    MemoryFile file(span<std::byte>{});
    ASSERT_TRUE(file.is_open());

    ASSERT_TRUE(file_search(file, {}, {}, 0, {}, {}, _cb));
}

TEST_F(FileSearchTest, CheckBufferSize)
{
    MemoryFile file(span<std::byte>{});
    ASSERT_TRUE(file.is_open());

    // Auto buffer size
    ASSERT_TRUE(file_search(file, {}, {}, 0, as_bytes(span("x", 1)), {}, _cb));

    // Too small
    ASSERT_EQ(file_search(file, {}, {}, 1, as_bytes(span("xxx", 3)), {}, _cb),
              oc::failure(FileError::ArgumentOutOfRange));

    // Equal to pattern size
    ASSERT_TRUE(file_search(file, {}, {}, 1, as_bytes(span("x", 1)), {}, _cb));
}

TEST_F(FileSearchTest, FindNormal)
{
    char buf[] = "abc";

    MemoryFile file(as_writable_bytes(span(buf, sizeof(buf) - 1)));
    ASSERT_TRUE(file.is_open());

    ASSERT_TRUE(file_search(file, {}, {}, 0, as_bytes(span("a", 1)), {}, _cb));
}

TEST(FileMoveTest, DegenerateCasesShouldSucceed)
{
    char buf[] = "abcdef";

    MemoryFile file(as_writable_bytes(span(buf, sizeof(buf) - 1)));
    ASSERT_TRUE(file.is_open());

    // src == dest
    ASSERT_TRUE(file_move(file, 0, 0, 3));

    // size == 0
    ASSERT_TRUE(file_move(file, 3, 0, 0));
}

TEST(FileMoveTest, NormalForwardsCopyShouldSucceed)
{
    char buf[] = "abcdef";

    MemoryFile file(as_writable_bytes(span(buf, sizeof(buf) - 1)));
    ASSERT_TRUE(file.is_open());

    ASSERT_EQ(file_move(file, 2, 0, 3), oc::success(3u));
    ASSERT_STREQ(buf, "cdedef");
}

TEST(FileMoveTest, NormalBackwardsCopyShouldSucceed)
{
    char buf[] = "abcdef";

    MemoryFile file(as_writable_bytes(span(buf, sizeof(buf) - 1)));
    ASSERT_TRUE(file.is_open());

    ASSERT_EQ(file_move(file, 0, 2, 3), oc::success(3u));
    ASSERT_STREQ(buf, "ababcf");
}

TEST(FileMoveTest, OutOfBoundsForwardsCopyShouldCopyPartially)
{
    char buf[] = "abcdef";

    MemoryFile file(as_writable_bytes(span(buf, sizeof(buf) - 1)));
    ASSERT_TRUE(file.is_open());

    ASSERT_EQ(file_move(file, 2, 0, 5), oc::success(4u));
    ASSERT_STREQ(buf, "cdefef");
}

TEST(FileMoveTest, OutOfBoundsBackwardsCopyShouldCopyPartially)
{
    char buf[] = "abcdef";

    MemoryFile file(as_writable_bytes(span(buf, sizeof(buf) - 1)));
    ASSERT_TRUE(file.is_open());

    ASSERT_EQ(file_move(file, 0, 2, 5), oc::success(4u));
    ASSERT_STREQ(buf, "ababcd");
}

TEST(FileMoveTest, LargeForwardsCopyShouldSucceed)
{
    std::vector<std::byte> buf(100000);

    auto middle = buf.begin() + (buf.end() - buf.begin()) / 2;
    std::fill(buf.begin(), middle, std::byte('a'));
    std::fill(middle, buf.end(), std::byte('b'));

    MemoryFile file(span<std::byte>{buf});
    ASSERT_TRUE(file.is_open());

    ASSERT_EQ(file_move(file, buf.size() / 2, 0, buf.size() / 2),
              oc::success(buf.size() / 2));

    for (size_t i = 0; i < buf.size(); ++i) {
        ASSERT_EQ(buf[i], std::byte('b'));
    }
}

TEST(FileMoveTest, LargeBackwardsCopyShouldSucceed)
{
    std::vector<std::byte> buf(100000);

    auto middle = buf.begin() + (buf.end() - buf.begin()) / 2;
    std::fill(buf.begin(), middle, std::byte('a'));
    std::fill(middle, buf.end(), std::byte('b'));

    MemoryFile file(span<std::byte>{buf});
    ASSERT_TRUE(file.is_open());

    ASSERT_EQ(file_move(file, 0, buf.size() / 2, buf.size() / 2),
              oc::success(buf.size() / 2));

    for (size_t i = 0; i < buf.size(); ++i) {
        ASSERT_EQ(buf[i], std::byte('a'));
    }
}

// TODO: Add more tests after integrating gmock
