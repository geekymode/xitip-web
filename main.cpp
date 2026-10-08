// This is the main C++ program file of the ITIP CLI frontend.

#include <iostream>     // cin/cerr etc ...
#include <string>       // getline
#include <vector>       // vector
#include <iterator>     // back_inserter

#include "citip.hpp"
#include "common.hpp"

using util::quoted;
using util::line_iterator;


int main (int argc, char *argv[])
try
{
    using namespace std;

    vector<string> expr;
    bool prove_it = false;
    bool show_proof = false;
    bool conditions = false;

    // Options come first; everything after them is the expression and its
    // constraints. With none of them the tool counts variables, which is
    // what it has always done.
    int first = 1;
    for (; first < argc; ++first) {
        string arg = argv[first];
        if (arg == "--prove") {
            prove_it = true;
        } else if (arg == "--proof" || arg == "--steps") {
            prove_it = show_proof = true;
        } else if (arg == "--conditions") {
            prove_it = conditions = true;
        } else if (arg == "--") {
            // everything after this is input, even if it looks like a flag;
            // a caller passing untrusted text should always use it
            ++first;
            break;
        } else if (arg == "--help" || arg == "-h") {
            cout <<
                "usage: oXitipLen [--prove|--proof] [--conditions] EXPRESSION [CONSTRAINT...]\n"
                "\n"
                "  (no option)   print the number of distinct random variables\n"
                "  --prove       say whether the expression is true\n"
                "  --proof       say so, and print why\n"
                "  --steps       the same as --proof\n"
                "  --conditions  if it is not provable, find assumptions that\n"
                "                would make it so\n"
                "  --           end of options; everything after is input\n"
                "\n"
                "With no expression, or with '-' last, they are read from\n"
                "standard input, one per line.\n";
            return 0;
        } else {
            break;
        }
    }

    bool use_stdin = first == argc;

    if (argc > first && string(argv[argc-1]) == "-") {
        --argc;
        use_stdin = true;
    }

    copy(argv+first, argv+argc, back_inserter(expr));

    if (use_stdin) {
        copy(line_iterator(cin), line_iterator(), back_inserter(expr));
    }

    ParserOutput out = parse(expr);

    if (!prove_it) {
        // Print the number of distinct random variables to stdout. The exit
        // code only signals success (0) or failure (2, 3), so a count can
        // never be confused with an error.
        cout << out.var_names.size() << endl;
        return 0;
    }

    bool holds = prove(out, cout, show_proof);
    cout << "The information expression is "
         << (holds ? "TRUE" : "FALSE") << "." << endl;
    if (!holds && conditions) {
        cout << endl;
        print_conditions(cout, sufficient_conditions(out));
    }
    return 0;
}
catch (std::exception& e)
{
    std::cerr << "ERROR: " << e.what() << std::endl;
    return 2;
}
// force stack unwinding
catch (...)
{
    std::cerr << "UNKNOWN ERROR - aborting." << std::endl;
    return 3;
}
