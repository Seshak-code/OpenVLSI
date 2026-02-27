// ============================================================================
// terminal_parser.cpp — Implementation
// ============================================================================
#include "terminal_parser.h"

#include <sstream>
#include <cctype>
#include <algorithm>

namespace acutesim::connectivity::terminal {

std::vector<std::string> tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::string token;
    bool inQuote = false;
    for (char c : input) {
        if (c == '"') {
            inQuote = !inQuote;
        } else if (std::isspace(static_cast<unsigned char>(c)) && !inQuote) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

ParsedTerminalCommand parseTerminalCommand(const std::string& raw) {
    ParsedTerminalCommand cmd;

    auto tokens = tokenize(raw);
    if (tokens.empty()) {
        cmd.errorMsg = "Empty command";
        return cmd;
    }

    // Lowercase the verb
    std::string verb = tokens[0];
    std::transform(verb.begin(), verb.end(), verb.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    cmd.verb = verb;

    // Parse --key=value flags and positional args
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        const auto& tok = tokens[i];
        if (tok.size() > 2 && tok[0] == '-' && tok[1] == '-') {
            auto eq = tok.find('=');
            if (eq != std::string::npos) {
                cmd.flags[tok.substr(2, eq - 2)] = tok.substr(eq + 1);
            } else {
                cmd.flags[tok.substr(2)] = "true";
            }
        } else {
            cmd.args.push_back(tok);
        }
    }

    // Classify by verb
    if (verb == "run" || verb == "simulate") {
        cmd.cls   = TerminalCommandClass::Simulation;
        cmd.valid = !cmd.args.empty();
        if (!cmd.valid) cmd.errorMsg = "'run' requires an analysis type (dc|ac|tran|mc)";
    } else if (verb == "plot" || verb == "wave") {
        cmd.cls   = TerminalCommandClass::Plot;
        cmd.valid = !cmd.args.empty();
        if (!cmd.valid) cmd.errorMsg = "'plot' requires at least one signal name";
    } else if (verb == "measure") {
        cmd.cls   = TerminalCommandClass::Measure;
        cmd.valid = !cmd.args.empty();
        if (!cmd.valid) cmd.errorMsg = "'measure' requires a measurement type";
    } else if (verb == "netlist") {
        cmd.cls   = TerminalCommandClass::Netlist;
        cmd.valid = true;
    } else if (verb == "help" || verb == "clear" || verb == "cancel") {
        cmd.cls   = TerminalCommandClass::Control;
        cmd.valid = true;
    } else {
        cmd.cls      = TerminalCommandClass::Unknown;
        cmd.errorMsg = "Unknown command: " + verb;
    }

    return cmd;
}

} // namespace acutesim::connectivity::terminal
