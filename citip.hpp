#ifndef __CITIP_HPP__INCLUDED__
#define __CITIP_HPP__INCLUDED__

# include <map>
# include <string>
# include <vector>

# include "parser.hxx"


struct glp_prob;                    // defined in <glpk.h>


struct SparseVector
{
    std::map<int, double> entries;
    bool is_equality;

    double get(int i) const;        // get component i
    void inc(int i, double v);      // increase/decrease component
};


typedef std::vector<SparseVector> Matrix;


// What a row of the LP stands for, so that a proof can name it. The rows
// are the elemental inequalities followed by the user's constraints.
struct RowInfo
{
    enum Kind { ENTROPY, MUTINF, CONSTRAINT };
    Kind kind;
    int i;              // ENTROPY: the variable that is conditioned
    int a, b, K;        // MUTINF: I(X_a ; X_b | X_K)
    int index;          // CONSTRAINT: which one, counting from 1
};


// One non-negative quantity of a proof, with the multiplier it carries.
struct ProofTerm
{
    double coefficient;
    std::string name;       // "I(X;Y|Z)", or "C2" for a constraint
    std::string expansion;  // the same written with joint entropies
    std::string source;     // for a constraint, where it came from
    SparseVector vec;       // the quantity itself, for the running total
};


// A proof that an expression is non-negative: the expression is the sum of
// these terms, every one of which is non-negative, plus a constant that is
// non-negative too.
struct Proof
{
    std::string expression;             // E, written with joint entropies
    std::string expression_name;        // E as one quantity, if it is one
    SparseVector expr_vec;              // E itself
    std::vector<std::string> vars;      // the variable names, for printing
    std::vector<ProofTerm> terms;
    double constant;
};


std::string set_name(int set, const std::vector<std::string>& vars);
std::string format_vector(const SparseVector& v,
                          const std::vector<std::string>& vars);
std::string name_quantity(const SparseVector& v,
                          const std::vector<std::string>& vars);
void print_proof(std::ostream& out, const Proof& proof);


// Lightweight C++ wrapper for a GLPK problem (glp_prob*). This manages a
// problem of the form "Is I>=0 valid, subject to the constraints C>=0, and
// X>=0 for all column variables X".
class LinearProblem
{
public:
    LinearProblem();
    explicit LinearProblem(int num_cols);
    ~LinearProblem();

    void add_columns(int num_cols);

    LinearProblem(const LinearProblem&) = delete;
    LinearProblem& operator = (const LinearProblem&) = delete;

    void add(const SparseVector&);      // add a constraint C>=0
    bool check(const SparseVector&);    // check if I>=0 is redundant

    // As check(), but on success also reads the proof out of the dual
    // solution: the multipliers that write I as a non-negative combination
    // of the rows and the column non-negativities.
    bool check(const SparseVector&, Proof* proof,
               const std::vector<std::string>* vars);

    // The elemental inequalities are the first rows. Forcing one to zero
    // turns it from a basic inequality into an assumption, which is how the
    // search for sufficient conditions tries them without rebuilding.
    int num_elemental() const;
    const RowInfo& row(int k) const { return row_info[k]; }   // 0-based
    void force_zero(int k, bool on);

    // A point where I < 0 (for an inequality I >= 0 that does not hold),
    // as the value of every elemental quantity there. False if none.
    bool counterexample(const SparseVector&, std::vector<double>* elemental);

protected:
    glp_prob* lp;
    std::vector<RowInfo> row_info;      // one per row, in the order added
    std::vector<std::string> constraint_text;
};


// Manage a linear programming problem in the context of random variables.
// The system has 2**num_vars-1 random variables which correspond to joint
// entropies of the non-empty subsets of random variables. These quantities
// are indexed in a canonical way, such that the bit-representation of the
// index is in one-to-one correspondence with the subset.
class ShannonTypeProblem
    : public LinearProblem
{
public:
    explicit ShannonTypeProblem(int num_vars);

    // add a user constraint, remembering what it was for the proof
    void add_constraint(const SparseVector&, const std::string& text);
};

// This is used automatically for a ShannonTypeProblem.
void add_elemental_inequalities(glp_prob* lp, int num_vars,
                                std::vector<RowInfo>* info = nullptr);


class ParserOutput : public ParserCallback
{
    int get_var_index(const std::string&);
    int get_set_index(const ast::VarList&);     // as in 'set of variables'
    void add_quant_vec(SparseVector&, const ast::Quantity&);
    void add_term(SparseVector&, const ast::Term&, double scale=1);

    std::map<std::string, int> vars;

    void add_relation(SparseVector, bool is_inquiry);
public:
    // consider this read-only
    std::vector<std::string> var_names;

    Matrix inquiries;
    Matrix constraints;

    // where each constraint came from, so a proof can quote it
    std::vector<int> constraint_line;
    std::vector<std::string> source_lines;
    int current_line = 0;

    // parser callback
    void relation(ast::Relation);
    void markov_chain(ast::MarkovChain);
    void mutual_independence(ast::MutualIndependence);
    void function_of(ast::FunctionOf);
};


ParserOutput parse(const std::vector<std::string>&);

bool check(const ParserOutput&);

// Prove every inquiry, writing the proofs to the stream. Returns false as
// soon as one of them does not hold.
bool prove(const ParserOutput&, std::ostream& out, bool show_proof);


// One set of assumptions under which a statement becomes provable: each is
// an elemental quantity forced to zero, as constraint text that can be fed
// back to the prover, and as a sentence.
struct SufficientCondition
{
    std::vector<std::string> constraints;
    std::vector<std::string> meanings;
};

struct Conditions
{
    std::vector<SufficientCondition> found;
    int candidates = 0;     // elemental quantities that could be assumed zero
    int tested = 0;         // sets of them actually tried
    int maxsize = 0;
    bool exhausted = true;  // false if the search stopped at limit or budget
};

// For a statement that is not provable: the smallest sets of at most
// `maxsize` assumptions that make it provable, at most `limit` of them,
// trying at most `budget` sets. As sufficient_conditions in Xitip.jl.
Conditions sufficient_conditions(const ParserOutput&, int maxsize = 2,
                                 int limit = 6, int budget = 4000);
void print_conditions(std::ostream& out, const Conditions&);


#endif // include guard
