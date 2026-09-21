#pragma once
// Text formatting shared by the M1 and M2 reporters. Presentation only.

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

namespace ob {

inline std::string fixed(double v, int places) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(places) << v;
    return os.str();
}

// Thousands separators, done manually so no locale needs installing.
inline std::string grouped(double v, int places = 0) {
    std::string s = fixed(v, places);
    std::string intpart = s;
    std::string frac;
    const auto dot = s.find('.');
    if (dot != std::string::npos) {
        intpart = s.substr(0, dot);
        frac    = s.substr(dot);
    }
    bool neg = !intpart.empty() && intpart[0] == '-';
    if (neg) intpart.erase(0, 1);

    std::string out;
    int count = 0;
    for (auto it = intpart.rbegin(); it != intpart.rend(); ++it) {
        if (count && count % 3 == 0) out.push_back(',');
        out.push_back(*it);
        ++count;
    }
    std::reverse(out.begin(), out.end());
    if (neg) out.insert(out.begin(), '-');
    return out + frac;
}

inline void pad_left(std::ostream& out, const std::string& s, size_t width) {
    if (s.size() < width) out << std::string(width - s.size(), ' ');
    out << s;
}
inline void pad_right(std::ostream& out, const std::string& s, size_t width) {
    out << s;
    if (s.size() < width) out << std::string(width - s.size(), ' ');
}

} // namespace ob
