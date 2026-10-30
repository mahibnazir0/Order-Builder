#pragma once
// Text formatting shared by the reporters. Presentation only.

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

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

// Text from the input files with every byte outside printable ASCII replaced by '?'. An em dash
// in a printed report once broke the client's console.
inline std::string printable(const std::string& text) {
    std::string result = text;
    for (char& character : result) {
        const auto code = static_cast<unsigned char>(character);
        if (code < 0x20 || code > 0x7e) character = '?';
    }
    return result;
}

inline void pad_left(std::ostream& out, const std::string& s, size_t width) {
    if (s.size() < width) out << std::string(width - s.size(), ' ');
    out << s;
}
inline void pad_right(std::ostream& out, const std::string& s, size_t width) {
    out << s;
    if (s.size() < width) out << std::string(width - s.size(), ' ');
}

// A table whose every column is as wide as its widest entry, so nothing is ever cut short.
// rightAligned is parallel to the header; the last column is never padded.
inline void printColumns(std::ostream& out, const std::vector<std::string>& header,
                         const std::vector<std::vector<std::string>>& rows,
                         const std::vector<bool>& rightAligned) {
    std::vector<size_t> widths(header.size());
    for (size_t column = 0; column < header.size(); ++column) widths[column] = header[column].size();
    for (const auto& row : rows) {
        for (size_t column = 0; column < row.size(); ++column) {
            widths[column] = std::max(widths[column], row[column].size());
        }
    }
    auto printRow = [&](const std::vector<std::string>& row) {
        out << " ";
        for (size_t column = 0; column < row.size(); ++column) {
            out << " ";
            if (rightAligned[column]) {
                pad_left(out, row[column], widths[column]);
            } else if (column + 1 == row.size()) {
                out << row[column];
            } else {
                pad_right(out, row[column], widths[column]);
            }
        }
        out << "\n";
    };
    printRow(header);
    std::vector<std::string> dashes;
    for (const size_t width : widths) dashes.push_back(std::string(width, '-'));
    printRow(dashes);
    for (const auto& row : rows) printRow(row);
}

} // namespace ob
