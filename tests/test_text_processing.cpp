#include "../text_processing.h"
#include <cassert>
#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  " << #name << "... "; \
    try { test_##name(); tests_passed++; std::cout << "PASS\n"; } \
    catch (const std::exception& e) { tests_failed++; std::cout << "FAIL: " << e.what() << "\n"; }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { throw std::runtime_error(std::string("expected '") + std::string(b) + "' got '" + std::string(a) + "'"); }

#define ASSERT_TRUE(x) \
    if (!(x)) { throw std::runtime_error("assertion failed: " #x); }

// --- trim_whitespace tests ---

void test_trim_basic() {
    ASSERT_EQ(trim_whitespace("  hello  "), "hello");
}

void test_trim_empty() {
    ASSERT_EQ(trim_whitespace(""), "");
}

void test_trim_all_spaces() {
    ASSERT_EQ(trim_whitespace("   "), "");
}

void test_trim_tabs_newlines() {
    ASSERT_EQ(trim_whitespace("\t\nhello\r\n"), "hello");
}

// --- filter_hallucinations tests ---

void test_filter_thank_you() {
    ASSERT_EQ(filter_hallucinations("Thank you for watching"), "");
}

void test_filter_case_insensitive() {
    ASSERT_EQ(filter_hallucinations("  THANK YOU FOR WATCHING  "), "");
}

void test_filter_blank_audio() {
    ASSERT_EQ(filter_hallucinations("[BLANK_AUDIO]"), "");
}

void test_filter_silence() {
    ASSERT_EQ(filter_hallucinations("[silence]"), "");
    ASSERT_EQ(filter_hallucinations("[ Silence ]"), "");
    ASSERT_EQ(filter_hallucinations("[Silence]"), "");
}

void test_filter_typing() {
    ASSERT_EQ(filter_hallucinations("[typing sounds]"), "");
    ASSERT_EQ(filter_hallucinations("[keyboard sounds]"), "");
    ASSERT_EQ(filter_hallucinations("[typing]"), "");
}

void test_filter_url() {
    ASSERT_EQ(filter_hallucinations("www.example.com"), "");
    ASSERT_EQ(filter_hallucinations("https://example.com"), "");
}

void test_filter_real_speech() {
    std::string real = "Hello, this is a real sentence.";
    ASSERT_EQ(filter_hallucinations(real), real);
}

void test_filter_empty() {
    ASSERT_EQ(filter_hallucinations(""), "");
}

void test_filter_whitespace_only() {
    ASSERT_EQ(filter_hallucinations("   "), "");
}

void test_filter_dedup() {
    std::string input = "Hello. Hello. Hello. World.";
    std::string result = filter_hallucinations(input);
    ASSERT_TRUE(result.find("Hello. Hello.") == std::string::npos || result.size() < input.size());
}

// --- count_words tests ---

void test_count_empty() {
    ASSERT_TRUE(count_words("") == 0);
}

void test_count_single() {
    ASSERT_TRUE(count_words("hello") == 1);
}

void test_count_multiple() {
    ASSERT_TRUE(count_words("hello world foo bar") == 4);
}

void test_count_extra_spaces() {
    ASSERT_TRUE(count_words("  hello   world  ") == 2);
}

// --- split_into_chunks tests ---

void test_split_short() {
    auto chunks = split_into_chunks("Hello world.", 100);
    ASSERT_TRUE(chunks.size() == 1);
    ASSERT_EQ(chunks[0], "Hello world.");
}

void test_split_boundary() {
    std::string text = "One two three. Four five six. Seven eight nine.";
    auto chunks = split_into_chunks(text, 3);
    ASSERT_TRUE(chunks.size() >= 2);
}

void test_split_empty() {
    auto chunks = split_into_chunks("", 10);
    ASSERT_TRUE(chunks.empty());
}

int main() {
    std::cout << "text_processing tests:\n";

    TEST(trim_basic);
    TEST(trim_empty);
    TEST(trim_all_spaces);
    TEST(trim_tabs_newlines);

    TEST(filter_thank_you);
    TEST(filter_case_insensitive);
    TEST(filter_blank_audio);
    TEST(filter_silence);
    TEST(filter_typing);
    TEST(filter_url);
    TEST(filter_real_speech);
    TEST(filter_empty);
    TEST(filter_whitespace_only);
    TEST(filter_dedup);

    TEST(count_empty);
    TEST(count_single);
    TEST(count_multiple);
    TEST(count_extra_spaces);

    TEST(split_short);
    TEST(split_boundary);
    TEST(split_empty);

    std::cout << "\n" << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed > 0 ? 1 : 0;
}
