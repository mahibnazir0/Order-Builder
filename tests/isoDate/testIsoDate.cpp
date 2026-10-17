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

TEST_CASE("isoDate: weekdayIndex counts from Monday") {
    CHECK(weekdayIndex("2026-10-05") == 0);
    CHECK(weekdayIndex("2026-10-09") == 4);
    CHECK(weekdayIndex("2026-10-11") == 6);
    CHECK(weekdayIndex("1970-01-01") == 3);
    CHECK(string(weekdayName(weekdayIndex("2026-10-07"))) == "Wednesday");
}

TEST_CASE("isoDate: addDays crosses month, year and leap-day boundaries") {
    CHECK(addDays("2026-10-09", 3) == "2026-10-12");
    CHECK(addDays("2026-09-29", 2) == "2026-10-01");
    CHECK(addDays("2026-12-31", 2) == "2027-01-02");
    CHECK(addDays("2028-02-28", 1) == "2028-02-29");
    CHECK(addDays("2026-02-28", 1) == "2026-03-01");
    CHECK(addDays("2026-03-01", -1) == "2026-02-28");
}
