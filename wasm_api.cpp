// Entry point for the WebAssembly build.
//
// The command line binary is driven by argv and prints to stdout; in the
// browser it is easier to hand over a block of text and get a block of text
// back, with no process to start and no stream to capture. This is that
// entry point, and the only file that is not shared with the native build.

#include <exception>
#include <sstream>
#include <string>
#include <vector>

#include "citip.hpp"

namespace {

// The result is handed to JavaScript as a pointer into the wasm heap, so it
// has to outlive the call; one buffer is enough because JavaScript copies
// the string out before calling again.
std::string result;

std::vector<std::string> split_lines(const char* text)
{
    std::vector<std::string> out;
    std::istringstream in(text ? text : "");
    std::string line;
    while (std::getline(in, line)) {
        // tolerate CRLF, and skip blank lines and comments
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos || line[a] == '#')
            continue;
        size_t b = line.find_last_not_of(" \t");
        out.push_back(line.substr(a, b - a + 1));
    }
    return out;
}

}

extern "C" {

// Returns one of
//     TRUE\n<proof, possibly empty>
//     FALSE\n
//     ERROR\n<message>
// which is simpler to pick apart in JavaScript than JSON is to write here.
const char* xitip_solve(const char* text, int want_proof)
{
    try {
        std::vector<std::string> lines = split_lines(text);
        if (lines.empty())
            throw std::runtime_error("nothing to prove");

        ParserOutput out = parse(lines);
        std::ostringstream proof;
        bool holds = prove(out, proof, want_proof != 0);
        result = (holds ? "TRUE\n" : "FALSE\n");
        if (holds && want_proof)
            result += proof.str();
    }
    catch (std::exception& e) {
        result = std::string("ERROR\n") + e.what();
    }
    catch (...) {
        result = "ERROR\nunknown error";
    }
    return result.c_str();
}

// How many distinct random variables the expression mentions, or -1 if it
// does not parse.
int xitip_variables(const char* text)
{
    try {
        return (int) parse(split_lines(text)).var_names.size();
    }
    catch (...) {
        return -1;
    }
}

}
