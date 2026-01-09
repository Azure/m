// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <string>
#include <string_view>

#include <m/errors/errors.h>
#include <m/exception/exception.h>
#include <m/utility/exception.h>

using namespace std::string_literals;
using namespace std::string_view_literals;

#undef NOMINMAX
#define NOMINMAX
#include <Windows.h>

TEST(TestErrors, InstantiateHresultCategory)
{
    auto const& cat = m::hresult_category();
    EXPECT_STREQ(cat.name(), "hresult");
}

TEST(TestErrors, TestThrowHresult)
{
    //
    EXPECT_THROW(m::throw_hresult(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)), m::not_found);
    EXPECT_THROW(m::throw_hresult(HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)),
                 m::sharing_violation);
    EXPECT_THROW(m::throw_hresult(E_FAIL), std::system_error);

    EXPECT_THROW(m::throw_hresult(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)), m::not_found);
    EXPECT_THROW(m::throw_hresult(HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)),
                 m::sharing_violation);
    EXPECT_THROW(m::throw_hresult(E_FAIL), std::system_error);
}

TEST(TestErrors, TestThrowWin32ErrorCode)
{
    //
    EXPECT_THROW(m::throw_win32_error_code(ERROR_FILE_NOT_FOUND), m::not_found);
    EXPECT_THROW(m::throw_win32_error_code(ERROR_SHARING_VIOLATION), m::sharing_violation);
    EXPECT_THROW(m::throw_win32_error_code(ERROR_ABANDON_HIBERFILE), std::system_error);

    EXPECT_THROW(m::throw_win32_error_code(ERROR_FILE_NOT_FOUND), m::not_found);
    EXPECT_THROW(m::throw_win32_error_code(ERROR_SHARING_VIOLATION), m::sharing_violation);
    EXPECT_THROW(m::throw_win32_error_code(ERROR_ABANDON_HIBERFILE), std::system_error);
}

TEST(TestErrors, TestFailed_win32_error_code)
{
    EXPECT_TRUE(m::failed(static_cast<m::windows::win32_error_code>(ERROR_FILE_NOT_FOUND)));
    EXPECT_FALSE(m::failed(static_cast<m::windows::win32_error_code>(ERROR_SUCCESS)));
}

TEST(TestErrors, TestFailed_error_code)
{
    std::error_code ec;

    EXPECT_FALSE(m::failed(ec));
    EXPECT_FALSE(static_cast<bool>(ec));

    ec = m::make_hresult_error_code(E_FAIL);

    EXPECT_TRUE(m::failed(ec));
    EXPECT_TRUE(static_cast<bool>(ec));

    ec.clear();

    EXPECT_FALSE(m::failed(ec));
    EXPECT_FALSE(static_cast<bool>(ec));

    ec = m::make_hresult_error_code(E_ACCESSDENIED);

    EXPECT_TRUE(m::failed(ec));
    EXPECT_TRUE(static_cast<bool>(ec));

    ec = m::make_hresult_error_code(S_OK);
    EXPECT_FALSE(m::failed(ec));
    EXPECT_FALSE(static_cast<bool>(ec));
}

TEST(TestErrors, TestThrowError_win32_error_code)
{
    //
    EXPECT_THROW(m::throw_error(static_cast<m::windows::win32_error_code>(ERROR_FILE_NOT_FOUND)),
                 m::not_found);
    EXPECT_THROW(m::throw_error(static_cast<m::windows::win32_error_code>(ERROR_SHARING_VIOLATION)),
                 m::sharing_violation);
    EXPECT_THROW(m::throw_error(static_cast<m::windows::win32_error_code>(ERROR_ABANDON_HIBERFILE)),
                 std::system_error);

    EXPECT_THROW(m::throw_error(static_cast<m::windows::win32_error_code>(ERROR_FILE_NOT_FOUND)),
                 m::not_found);
    EXPECT_THROW(m::throw_error(static_cast<m::windows::win32_error_code>(ERROR_SHARING_VIOLATION)),
                 m::sharing_violation);
    EXPECT_THROW(m::throw_error(static_cast<m::windows::win32_error_code>(ERROR_ABANDON_HIBERFILE)),
                 std::system_error);
}
