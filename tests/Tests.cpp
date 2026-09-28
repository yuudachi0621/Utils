#include "Tests.h"

#include "../src/tool/stringUtil.h"
#include "../src/container/VariableBuffer.h"
#include "timer.h"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_passed = 0;
int g_failed = 0;

void Expect(bool condition, const char* expression, const char* file, int line)
{
    if (condition)
    {
        ++g_passed;
        return;
    }

    ++g_failed;
    std::cerr << "[FAIL] " << file << ':' << line << "  " << expression << "\n";
}

} // namespace

#define EXPECT(expression) Expect((expression), #expression, __FILE__, __LINE__)

int tests::RunTests()
{
    using util::StringUtil;

    g_passed = 0;
    g_failed = 0;

    // Format / case conversion
    EXPECT(StringUtil::Format("%d-%s", 42, "ok") == "42-ok");
    EXPECT(StringUtil::ToLower("AbC-123") == "abc-123");
    EXPECT(StringUtil::ToUpper("AbC-123") == "ABC-123");

    std::string lowerSelf = "AbC";
    StringUtil::ToLower_Self(lowerSelf);
    EXPECT(lowerSelf == "abc");

    std::string upperSelf = "AbC";
    StringUtil::ToUpper_Self(upperSelf);
    EXPECT(upperSelf == "ABC");

    // Prefix / suffix / contains
    EXPECT(StringUtil::StartsWith("hello world", std::string("hello")));
    EXPECT(StringUtil::EndsWith("hello world", std::string("world")));
    EXPECT(StringUtil::Contains("hello world", std::string("lo wo")));
    EXPECT(!StringUtil::StartsWith("hello", std::string("world")));
    EXPECT(!StringUtil::EndsWith("hello", std::string("world")));

    // Find
    const char text[] = "abcabc";
    EXPECT(StringUtil::Find(text, 6, 'b') == text + 1);
    EXPECT(StringUtil::Find(text, 6, "bc") == text + 1);
    EXPECT(StringUtil::Find(text, 6, "") == text);
    EXPECT(StringUtil::Find(text, 6, 'z') == nullptr);
    EXPECT(StringUtil::Find(text, 6, "xyz") == nullptr);

    // Trim view / copy / self
    EXPECT(StringUtil::Trim("  hello\r\n") == "hello");
    EXPECT(StringUtil::TrimStart("  hello") == "hello");
    EXPECT(StringUtil::TrimEnd("hello  ") == "hello");
    EXPECT(StringUtil::TrimCopy(" \t hello \n") == "hello");

    std::string trimSelf = "xxhelloxx";
    StringUtil::Trim_Self(trimSelf, "x");
    EXPECT(trimSelf == "hello");

    std::string trimStartSelf = "xxhello";
    StringUtil::TrimStart_Self(trimStartSelf, "x");
    EXPECT(trimStartSelf == "hello");

    std::string trimEndSelf = "helloxx";
    StringUtil::TrimEnd_Self(trimEndSelf, "x");
    EXPECT(trimEndSelf == "hello");

    // Split / SplitView
    const std::vector<std::string> split = StringUtil::Split("a,,b", ",");
    EXPECT(split.size() == 3);
    EXPECT(split[0] == "a");
    EXPECT(split[1].empty());
    EXPECT(split[2] == "b");

    const std::string_view source                 = "a/b/";
    const std::vector<std::string_view> splitView = StringUtil::SplitView(source, "/");
    EXPECT(splitView.size() == 3);
    EXPECT(splitView[0] == "a");
    EXPECT(splitView[1] == "b");
    EXPECT(splitView[2].empty());

    EXPECT(StringUtil::Split("abc", "").size() == 1);
    EXPECT(StringUtil::Split("", ",").size() == 1);

    // Join
    EXPECT(StringUtil::Join(std::vector<std::string>{"a", "b", "c"}, ",") == "a,b,c");
    EXPECT(StringUtil::Join(std::vector<std::string_view>{"a", "b", "c"}, "/") == "a/b/c");
    EXPECT(StringUtil::Join(std::vector<std::string>{}, ",").empty());

    // Replace
    EXPECT(StringUtil::Replace("a-b-a", "a", "x") == "x-b-x");
    EXPECT(StringUtil::Replace("aaaa", "aa", "b", 1) == "baa");
    EXPECT(StringUtil::Replace("aaa", "a", "") == "");
    EXPECT(StringUtil::Replace("abc", "", "x") == "abc");

    // Remove / RightRemove
    EXPECT(StringUtil::Remove("a-b-c", "-") == "abc");

    std::string removeSelf = "a-b-c";
    StringUtil::Remove_Self(removeSelf, "-");
    EXPECT(removeSelf == "abc");

    VariableBuffer buffer(4);
    buffer.Append("abcd", 4);
    buffer.Consume(2);
    buffer.Append("ef", 2);
    EXPECT(buffer.GetValidDataToStr() == "cdef");

    std::cout << "Years: " << util::Timer::Years() << std::endl;
    std::cout << "Months: " << util::Timer::Months() << std::endl;
    std::cout << "Days: " << util::Timer::Days() << std::endl;
    std::cout << "FormatYMD: " << util::Timer::FormatYMD() << std::endl;
    std::cout << "FormatHMSS: " << util::Timer::FormatHMSS() << std::endl;
    std::cout << "FormatYMDHMS: " << util::Timer::FormatYMDHMS() << std::endl;
    std::cout << "FormatYMDHMSS: " << util::Timer::FormatYMDHMSS() << std::endl;
    std::cout << "GetCurTime: " << util::Timer::GetCurTime() << std::endl;

    std::cout << "Tests: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
