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

} // namespace ob
