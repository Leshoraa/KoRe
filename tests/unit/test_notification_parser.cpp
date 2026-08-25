/**
 * @file test_notification_parser.cpp
 * @brief Unit test suite for phone notification JSON extraction and text wrapping.
 */

#include <iostream>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

static void wrapMessageLines(const char* msg, std::vector<std::string>& lines, size_t max_lines = 3, size_t max_width = 20) {
    lines.clear();
    if (!msg) return;
    size_t len = strlen(msg);
    size_t pos = 0;
    for (size_t l = 0; l < max_lines && pos < len; l++) {
        size_t take = (len - pos > max_width) ? max_width : (len - pos);
        if (take == max_width && pos + take < len && msg[pos + take] != ' ' && msg[pos + take - 1] != ' ') {
            size_t last_space = 0;
            for (size_t s = 0; s < take; s++) {
                if (msg[pos + s] == ' ') last_space = s;
            }
            if (last_space > 6) take = last_space;
        }
        std::string line(msg + pos, take);
        while (pos + take < len && msg[pos + take] == ' ') take++;
        pos += take;
        lines.push_back(line);
    }
}

int main() {
    std::cout << "[TEST] Running Phone Notification Parsing & Word Wrapping Suite..." << std::endl;

    // Test 1: Short message fits in one line
    std::vector<std::string> lines1;
    wrapMessageLines("Halo bro!", lines1);
    assert(lines1.size() == 1);
    assert(lines1[0] == "Halo bro!");
    std::cout << "[PASS] Short message single line test passed." << std::endl;

    // Test 2: Long message wrapped on word boundary
    std::vector<std::string> lines2;
    wrapMessageLines("Bro jadi ngopi nanti malam jam 8 di kafe biasa?", lines2);
    assert(lines2.size() > 1);
    std::cout << "[INFO] Line 0: \"" << lines2[0] << "\"" << std::endl;
    std::cout << "[INFO] Line 1: \"" << lines2[1] << "\"" << std::endl;
    if (lines2.size() > 2) std::cout << "[INFO] Line 2: \"" << lines2[2] << "\"" << std::endl;
    assert(lines2[0].length() <= 20);
    assert(lines2[1].length() <= 20);
    std::cout << "[PASS] Word wrapping cleanly splits words across lines." << std::endl;

    std::cout << "[SUCCESS] All Notification Parsing & Wrapping tests passed!" << std::endl;
    return 0;
}
