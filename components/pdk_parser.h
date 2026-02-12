#pragma once
#include "circuit.h"
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdlib>

/**
 * @file pdk_parser.h
 * @brief SPICE/PDK Parser - Phase 6: Industry Compatibility.
 *
 * ============================================================================
 * IMPLEMENTATION GUIDE: PDK & NETLIST PARSING
 * ============================================================================
 *
 * To support SkyWater 130nm or any standard fab PDK, the simulator must
 * understand the SPICE language.
 *
 * KEY FEATURES TO IMPLEMENT:
 *   1. Hierarchical Parsing: .subckt / .ends (Maps directly to TensorBlock).
 *   2. Library Support: .lib / .include (For process corners).
 *   3. Model Cards: .model (For BSIM/MOSFET parameters).
 *   4. Parameter Sweeps: .param (For PVT corner batching).
 */

namespace acutesim {

// ============================================================================
// SPICE Engineering Suffix Parser
// ============================================================================

// Parse a SPICE numeric value with optional engineering suffix.
// E.g. "200U" -> 200e-6, "1.5MEG" -> 1.5e6, "3.3" -> 3.3
inline double parseSpiceValue(const std::string& token) {
    if (token.empty()) return 0.0;

    // Find where digits/decimal/sign end and suffix begins
    size_t suffixStart = token.size();
    for (size_t i = 0; i < token.size(); ++i) {
        char c = token[i];
        if (!std::isdigit(c) && c != '.' && c != '-' && c != '+' && c != 'e' && c != 'E') {
            // Check if 'e'/'E' is part of scientific notation (e.g. 1e-14)
            if ((c == 'e' || c == 'E') && i > 0 && i + 1 < token.size()) {
                char next = token[i + 1];
                if (std::isdigit(next) || next == '-' || next == '+') continue;
            }
            suffixStart = i;
            break;
        }
    }

    std::string numPart = token.substr(0, suffixStart);
    std::string suffix = token.substr(suffixStart);

    char* end = nullptr;
    double value = std::strtod(numPart.c_str(), &end);
    if (end == numPart.c_str()) return 0.0;

    if (suffix.empty()) return value;

    // Uppercase for comparison
    std::string upper;
    upper.reserve(suffix.size());
    for (char c : suffix) upper.push_back(static_cast<char>(std::toupper(c)));

    // SPICE engineering suffixes (MEG must come before M)
    if (upper == "T")        return value * 1e12;
    if (upper == "G")        return value * 1e9;
    if (upper == "MEG")      return value * 1e6;
    if (upper == "X")        return value * 1e6;   // Alternate for MEG
    if (upper == "K")        return value * 1e3;
    if (upper == "M" || upper == "MIL") return value * 1e-3;
    if (upper == "U")        return value * 1e-6;
    if (upper == "N")        return value * 1e-9;
    if (upper == "P")        return value * 1e-12;
    if (upper == "F")        return value * 1e-15;
    if (upper == "A")        return value * 1e-18;

    return value; // Unknown suffix, return raw
}

// ============================================================================
// .MODEL Line Parser
// ============================================================================

// Tokenize a string by whitespace, stripping parentheses
inline std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string cleaned;
    cleaned.reserve(line.size());
    for (char c : line) {
        if (c == '(' || c == ')') {
            cleaned.push_back(' ');
        } else {
            cleaned.push_back(c);
        }
    }
    std::istringstream iss(cleaned);
    std::string tok;
    while (iss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

inline std::string toUpper(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) result.push_back(static_cast<char>(std::toupper(c)));
    return result;
}

// Parse a single SPICE .MODEL line/block:
//   .MODEL name TYPE(param=val param=val ...)
//   .MODEL name TYPE param=val param=val ...
inline ModelCard parseModelCard(const std::string& line) {
    ModelCard card;

    auto tokens = tokenize(line);
    if (tokens.size() < 3) return card;

    // tokens[0] should be ".MODEL" (case-insensitive)
    // tokens[1] = model name
    // tokens[2] = device type
    card.name = tokens[1];
    card.type = toUpper(tokens[2]);

    // Parse key=value pairs from remaining tokens
    for (size_t i = 3; i < tokens.size(); ++i) {
        const auto& tok = tokens[i];
        auto eqPos = tok.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = toUpper(tok.substr(0, eqPos));
        std::string valStr = tok.substr(eqPos + 1);

        if (key.empty() || valStr.empty()) continue;

        double val = parseSpiceValue(valStr);

        if (key == "LEVEL") {
            card.level = static_cast<int>(val);
        } else {
            card.params[key] = val;
        }
    }

    return card;
}

// Parse all .MODEL cards from a multi-line netlist string.
// Handles SPICE continuation lines (lines starting with '+').
inline std::vector<ModelCard> parseModelCards(const std::string& netlistText) {
    std::vector<ModelCard> cards;

    // Split into lines
    std::vector<std::string> lines;
    std::istringstream stream(netlistText);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        // Skip comment lines
        if (!lines[i].empty() && lines[i][0] == '*') continue;

        // Check for .MODEL directive (case-insensitive)
        std::string upper = toUpper(lines[i]);
        auto pos = upper.find(".MODEL");
        if (pos == std::string::npos) continue;

        // Accumulate continuation lines
        std::string fullLine = lines[i];
        while (i + 1 < lines.size()) {
            // Trim leading whitespace to check for '+'
            const auto& nextLine = lines[i + 1];
            size_t firstNonSpace = nextLine.find_first_not_of(" \t");
            if (firstNonSpace != std::string::npos && nextLine[firstNonSpace] == '+') {
                fullLine += " " + nextLine.substr(firstNonSpace + 1);
                ++i;
            } else {
                break;
            }
        }

        ModelCard card = parseModelCard(fullLine);
        if (!card.name.empty() && !card.type.empty()) {
            cards.push_back(card);
        }
    }

    return cards;
}

} // namespace acutesim

// ============================================================================
// TODO: IMPLEMENT BSIM4 Device Model (Phase 6 Core)
// ============================================================================
/**
 * WHY BSIM4?
 * It is the standard for 130nm (SkyWater). It models complex effects like:
 *   - Channel Length Modulation (CLM)
 *   - Velocity Saturation
 *   - Gate Leakage
 *
 * PSEUDO-CODE for MOSFET Stamping:
 *
 * void stampMosfetBSIM4(int D, int G, int S, int B, const BSIM4Params& params, V_guess) {
 *     // 1. Calculate Drain Current (Id) using BSIM4 equations (very complex!)
 *     // 2. Calculate small-signal conductances (gm, gds, gmbs) as derivatives
 *     // 3. Create Linearized Model:
 *     //    - Equivalent current source (Id - gm*Vgs - gds*Vds - gmbs*Vbs)
 *     //    - Voltage-controlled current sources (VCCS) into the MNA matrix
 * }
 */
