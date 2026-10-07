#include "doctest.h"
#include "isoDate.hpp"

#include <string>

using namespace std;
using namespace ob;

TEST_CASE("isoDate: a real YYYY-MM-DD date is accepted") {
    CHECK(isIsoDate("2026-08-17"));
    CHECK(isIsoDate("2026-12-31"));
}

TEST_CASE("isoDate: another format, a blank or a non-numeric string is rejected") {
    for (const string text : {"", "20260817", "2026/08/17", "2026-8-17", "2026-08-1x",
                              " 2026-08-17", "2026-08-17 "}) {
        CAPTURE(text);
        CHECK_FALSE(isIsoDate(text));
    }
}

TEST_CASE("isoDate: a month or day that does not exist is rejected") {
    for (const string text : {"2026-13-01", "2026-00-10", "2026-08-00", "2026-08-32",
                              "2026-04-31", "2026-02-30"}) {
        CAPTURE(text);
        CHECK_FALSE(isIsoDate(text));
    }
}

TEST_CASE("isoDate: 29 February exists only in a leap year") {
    CHECK(isIsoDate("2028-02-29"));
    CHECK(isIsoDate("2000-02-29"));
    CHECK_FALSE(isIsoDate("2027-02-29"));
    CHECK_FALSE(isIsoDate("1900-02-29"));
}
