#include "isoDate.hpp"

using namespace std;

namespace ob {

namespace {

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int digitsValue(const string& text, size_t position, size_t count) {
    int value = 0;
    for (size_t offset = 0; offset < count; ++offset) {
        value = value * 10 + (text[position + offset] - '0');
    }
    return value;
}

// Days since 1970-01-01 in the proleptic Gregorian calendar (Howard Hinnant's
// days_from_civil): shifting the year to start in March puts the leap day last.
long long daysFromCivil(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const int yearOfEra = year - era * 400;
    const int dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return static_cast<long long>(era) * 146097 + dayOfEra - 719468;
}

long long daysFromIsoDate(const string& isoDate) {
    return daysFromCivil(digitsValue(isoDate, 0, 4), digitsValue(isoDate, 5, 2),
                         digitsValue(isoDate, 8, 2));
}

string twoDigits(int value) {
    return string(1, static_cast<char>('0' + value / 10)) + static_cast<char>('0' + value % 10);
}

} // anonymous namespace

bool isIsoDate(const string& text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
    for (const size_t position : {0, 1, 2, 3, 5, 6, 8, 9}) {
        if (text[position] < '0' || text[position] > '9') return false;
    }
    const int year = digitsValue(text, 0, 4);
    const int month = digitsValue(text, 5, 2);
    const int day = digitsValue(text, 8, 2);
    if (month < 1 || month > 12 || day < 1) return false;
    constexpr int kDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int lastDay = month == 2 && isLeapYear(year) ? 29 : kDaysInMonth[month - 1];
    return day <= lastDay;
}

int weekdayIndex(const string& isoDate) {
    // 1970-01-01 was a Thursday, index 3.
    const long long days = daysFromIsoDate(isoDate) + 3;
    return static_cast<int>(((days % 7) + 7) % 7);
}

const char* weekdayName(int weekday) {
    constexpr const char* kNames[] = {"Monday", "Tuesday", "Wednesday", "Thursday",
                                      "Friday", "Saturday", "Sunday"};
    return weekday >= 0 && weekday < 7 ? kNames[weekday] : "unknown";
}

// Hinnant's civil_from_days, the inverse of daysFromCivil.
string addDays(const string& isoDate, int days) {
    const long long shifted = daysFromIsoDate(isoDate) + days + 719468;
    const long long era = (shifted >= 0 ? shifted : shifted - 146096) / 146097;
    const long long dayOfEra = shifted - era * 146097;
    const long long yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524
                                 - dayOfEra / 146096) / 365;
    const long long dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const long long monthShifted = (5 * dayOfYear + 2) / 153;
    const int day = static_cast<int>(dayOfYear - (153 * monthShifted + 2) / 5 + 1);
    const int month = static_cast<int>(monthShifted < 10 ? monthShifted + 3 : monthShifted - 9);
    const int year = static_cast<int>(yearOfEra + era * 400 + (month <= 2 ? 1 : 0));
    return to_string(year) + "-" + twoDigits(month) + "-" + twoDigits(day);
}

} // namespace ob
