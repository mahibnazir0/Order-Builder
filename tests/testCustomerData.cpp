#include "doctest.h"
#include "customerData.hpp"

#include <cstdlib>
#include <optional>
#include <string>

using namespace std;

namespace {

// Sets OB_CUSTOMER_DATA for one test and puts back what was there.
class CustomerDataSetting {
public:
    explicit CustomerDataSetting(const optional<string>& value) : previous_(customerData::setting()) { assign(value); }
    ~CustomerDataSetting() { assign(previous_); }
    CustomerDataSetting(const CustomerDataSetting&) = delete;
    CustomerDataSetting& operator=(const CustomerDataSetting&) = delete;

private:
    static void assign(const optional<string>& value) {
#ifdef _WIN32
        _putenv_s("OB_CUSTOMER_DATA", value ? value->c_str() : "");
#else
        if (value) setenv("OB_CUSTOMER_DATA", value->c_str(), 1);
        else unsetenv("OB_CUSTOMER_DATA");
#endif
    }
    optional<string> previous_;
};

} // namespace

TEST_CASE("customerData: unset, missing customer data is required") {
    const CustomerDataSetting setting(nullopt);
    CHECK(customerData::requirement() == customerData::Requirement::Required);
}

TEST_CASE("customerData: \"required\" requires it") {
    const CustomerDataSetting setting(string("required"));
    CHECK(customerData::requirement() == customerData::Requirement::Required);
}

TEST_CASE("customerData: \"optional\" lets the guards report it skipped") {
    const CustomerDataSetting setting(string("optional"));
    CHECK(customerData::requirement() == customerData::Requirement::Optional);
}

TEST_CASE("customerData: any other value is unreadable, so the guards fail") {
    for (const string value : {"Optional", "yes", "0", " optional"}) {
        CAPTURE(value);
        const CustomerDataSetting setting(value);
        CHECK(customerData::requirement() == customerData::Requirement::Unreadable);
    }
}
