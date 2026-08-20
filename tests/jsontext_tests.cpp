// Contract tests for the Qt-free JSON text helpers.
//
// These assemble the transport envelope by concatenation, which is what makes the
// text data path cheap - and also what makes it worth pinning: a mistake here is a
// malformed frame on the wire rather than a compile error.
#include "phi/transport/api/jsontext.h"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const char *what, const std::string &detail = {})
{
    if (condition)
        return;
    ++g_failures;
    std::printf("FAIL %s%s%s\n", what, detail.empty() ? "" : ": ", detail.c_str());
}

void checkEqual(const std::string &actual, const std::string &expected, const char *what)
{
    check(actual == expected, what, "expected " + expected + ", got " + actual);
}

using namespace phicore::transport;

void testJsonQuoted()
{
    checkEqual(jsonQuoted("plain"), "\"plain\"", "plain text");
    checkEqual(jsonQuoted(""), "\"\"", "empty text");
    checkEqual(jsonQuoted("a\"b"), "\"a\\\"b\"", "quote is escaped");
    checkEqual(jsonQuoted("a\\b"), "\"a\\\\b\"", "backslash is escaped");
    checkEqual(jsonQuoted("line\nbreak"), "\"line\\nbreak\"", "newline is escaped");
    checkEqual(jsonQuoted("tab\there"), "\"tab\\there\"", "tab is escaped");
    // Control characters must not reach the wire raw.
    checkEqual(jsonQuoted(std::string("x\x01y")), "\"x\\u0001y\"", "control char is escaped");
    // UTF-8 passes through as bytes: a topic or id may carry non-ASCII.
    checkEqual(jsonQuoted("Küche"), "\"Küche\"", "utf-8 passes through");
}

void testEmptiness()
{
    check(isEmptyJsonObject(""), "empty string counts as empty");
    check(isEmptyJsonObject("{}"), "{} counts as empty");
    check(isEmptyJsonObject("  { }  "), "blank object counts as empty");
    check(!isEmptyJsonObject("{\"a\":1}"), "populated object is not empty");
    checkEqual(emptyJsonObject(), "{}", "empty object literal");
}

void testJsonField()
{
    checkEqual(jsonField("payload", "{\"a\":1}"), "\"payload\":{\"a\":1}", "nesting an object");
    checkEqual(jsonField("cid", "42"), "\"cid\":42", "numeric value stays unquoted");
    // A missing payload must still produce valid JSON, not an empty value slot.
    checkEqual(jsonField("payload", ""), "\"payload\":{}", "empty value becomes {}");
    checkEqual(jsonField("a\"b", "1"), "\"a\\\"b\":1", "key is escaped");
}

void testWithJsonField()
{
    checkEqual(withJsonField("{\"a\":1}", "sync", "\"t\""),
               "{\"sync\":\"t\",\"a\":1}",
               "field is prepended, members kept");
    checkEqual(withJsonField("{}", "sync", "\"t\""), "{\"sync\":\"t\"}", "into an empty object");
    checkEqual(withJsonField("", "sync", "\"t\""), "{\"sync\":\"t\"}", "into no object at all");
    checkEqual(withJsonField("  {  }  ", "sync", "\"t\""), "{\"sync\":\"t\"}", "into a blank object");
    // Nested braces must survive: the splice may not cut at the first '}'.
    checkEqual(withJsonField("{\"a\":{\"b\":2},\"c\":3}", "sync", "\"t\""),
               "{\"sync\":\"t\",\"a\":{\"b\":2},\"c\":3}",
               "nested objects survive");
    // A brace inside a string value must not confuse the splice either.
    checkEqual(withJsonField("{\"a\":\"}\"}", "sync", "\"t\""),
               "{\"sync\":\"t\",\"a\":\"}\"}",
               "brace inside a string value survives");
}

void testJsonObject()
{
    checkEqual(jsonObject({}), "{}", "no members");
    checkEqual(jsonObject({{"a", "1"}}), "{\"a\":1}", "one member");
    checkEqual(jsonObject({{"host", jsonQuoted("::1")}, {"port", "5040"}}),
               "{\"host\":\"::1\",\"port\":5040}", "two members");
    // A key is escaped like any other string.
    checkEqual(jsonObject({{"a\"b", "1"}}), "{\"a\\\"b\":1}", "key is escaped");
    // An empty value text becomes {}, matching jsonField.
    checkEqual(jsonObject({{"a", ""}}), "{\"a\":{}}", "empty value is an empty object");
    // A nested object is spliced, not re-encoded.
    checkEqual(jsonObject({{"outer", jsonObject({{"inner", "true"}})}}),
               "{\"outer\":{\"inner\":true}}", "nested object");
}

} // namespace

int main()
{
    testJsonObject();
    testJsonQuoted();
    testEmptiness();
    testJsonField();
    testWithJsonField();

    if (g_failures == 0) {
        std::printf("jsontext_tests: all passed\n");
        return 0;
    }
    std::printf("jsontext_tests: %d failure(s)\n", g_failures);
    return 1;
}
