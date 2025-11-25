#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <string>
#include <vector>
#include <algorithm> // Required for std::min

/**
 * @brief Calculates the Levenshtein distance between two strings.
 */
inline int levenshtein_distance(const std::string& s1, const std::string& s2) {
    const size_t len1 = s1.size();
    const size_t len2 = s2.size();
    std::vector<std::vector<int>> d(len1 + 1, std::vector<int>(len2 + 1));

    // Initialize the first row and column of the matrix
    // Casting to int to resolve the C4267 warning. This is safe because
    // the string lengths will never exceed the capacity of an int.
    for (size_t i = 0; i <= len1; ++i) {
        d[i][0] = static_cast<int>(i);
    }
    for (size_t j = 0; j <= len2; ++j) {
        d[0][j] = static_cast<int>(j);
    }

    // Fill in the rest of the matrix
    for (size_t i = 1; i <= len1; ++i) {
        for (size_t j = 1; j <= len2; ++j) {
            int cost = (s2[j - 1] == s1[i - 1]) ? 0 : 1;
            d[i][j] = std::min({ d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost });
        }
    }
    return d[len1][len2];
}

#endif // STRING_UTILS_H