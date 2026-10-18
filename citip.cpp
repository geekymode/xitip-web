#include <cmath>        // fabs, floor
#include <math.h>       // NAN, fabs
#include <algorithm>    // max, sort
#include <array>        // array
#include <set>          // set
#include <utility>      // move
#include <sstream>      // istringstream
#include <stdexcept>    // runtime_error

#include <glpk.h>

#include "citip.hpp"
#include "parser.hxx"
#include "scanner.hpp"
#include "common.hpp"
#include "known_inequalities.hpp"

using std::move;
using util::sprint_all;


void check_num_vars(int num_vars)
{
    // The index type (int) must allow to represent column numbers up to
    // 2**num_vars. For signed int MAXINT = 2**(8*sizeof(int)-1)-1,
    // therefore the following is the best we can do (and 30 or so random
    // variables are probably already too much to handle anyway):
    int max_vars = 8*sizeof(int) - 2;
    if (num_vars > max_vars) {
        // Note that the base class destructor ~LinearProblem will still be
        // executed, thus freeing the allocated resource.
        throw std::runtime_error(sprint_all(
                    "Too many variables! At most ", max_vars,
                    " are allowed."));
    }
}


// Shift bits such that the given bit is free.
int skip_bit(int pool, int bit_index)
{
    int bit = 1 << bit_index;
    int left = (pool & ~(bit-1)) << 1;
    int right = pool & (bit-1);
    return left | right;
}


void add_elemental_inequalities(glp_prob* lp, int num_vars,
                                std::vector<RowInfo>* info)
{
    auto note = [info](RowInfo r) { if (info) info->push_back(r); };
    // NOTE: GLPK uses 1-based indices and never uses the 0th element.
//printf("Rethna:glp_get_num_cols(lp)=%d\n",glp_get_num_cols(lp));
    int indices[5];
    double values[5];
    int i, a, b;

    if (num_vars == 1) {
        indices[1] = 1;
        values[1] = 1;
        int row = glp_add_rows(lp, 1);
        glp_set_row_bnds(lp, row, GLP_LO, 0.0, NAN);
        glp_set_mat_row(lp, row, 1, indices, values);
        note({RowInfo::ENTROPY, 0, 0, 0, 0, 0});
        return;
    }

    // Identify each variable with its index i from I = {0, 1, ..., N-1}.
    // Then entropy is a real valued set function from the power set of
    // indices P = 2**I. The value for the empty set can be defined to be
    // zero and is irrelevant. Therefore the dimensionality of the problem
    // is 2**N-1.
    int dim = (1<<num_vars) - 1;

    // After choosing 2 variables there are 2**(N-2) possible subsets of
    // the remaining N-2 variables.
    int sub_dim = 1 << (num_vars-2);

    // index of the entropy component corresponding to the joint entropy of
    // all variables. NOTE: since the left-most column is not used, the
    // variables involved in a joint entropy correspond exactly to the bit
    // representation of its index.
    size_t all = dim;

    // Add all elemental conditional entropy positivities, i.e. those of
    // the form H(X_i|X_c)>=0 where c = ~ {i}:
    for (i = 0; i < num_vars; ++i) {
        int c = all ^ (1 << i);
        indices[1] = all;
        indices[2] = c;
        values[1] = +1;
        values[2] = -1;
        int row = glp_add_rows(lp, 1);
        glp_set_row_bnds(lp, row, GLP_LO, 0.0, NAN);
        glp_set_mat_row(lp, row, 2, indices, values);
        note({RowInfo::ENTROPY, i, 0, 0, 0, 0});
    }

    // Add all elemental conditional mutual information positivities, i.e.
    // those of the form I(X_a:X_b|X_K)>=0 where a,b not in K
    for (a = 0; a < num_vars-1; ++a) {
        for (b = a+1; b < num_vars; ++b) {
            int A = 1 << a;
            int B = 1 << b;
            for (i = 0; i < sub_dim; ++i) {
                int K = skip_bit(skip_bit(i, a), b);
                indices[1] = A|K;
                indices[2] = B|K;
                indices[3] = A|B|K;
                indices[4] = K;
                values[1] = +1;
                values[2] = +1;
                values[3] = -1;
                values[4] = -1;
                int row = glp_add_rows(lp, 1);
                glp_set_row_bnds(lp, row, GLP_LO, 0.0, NAN);
                glp_set_mat_row(lp, row, K ? 4 : 3, indices, values);
                note({RowInfo::MUTINF, 0, a, b, K, 0});
            }
        }
    }
}


//----------------------------------------
// ParserOutput
//----------------------------------------

double SparseVector::get(int i) const
{
    auto&& it = entries.find(i);
    if (it != entries.end())
        return it->second;
    return 0;
}

void SparseVector::inc(int i, double v)
{
    entries[i] += v;
}

void ParserOutput::add_term(SparseVector& v, const ast::Term& t, double scale)
{
    const ast::Quantity& q = t.quantity;
    double coef = scale * t.coefficient;
    int num_parts = q.parts.size();
    if (num_parts == 0) {   // constant
        v.inc(0, coef);
        return;
    }

    // Need to index 2**num_parts subsets. For more detailed reasoning see
    // the check_num_vars() function.
    int max_parts = 8*sizeof(int) - 2;
    if (num_parts > max_parts) {
        throw std::runtime_error(sprint_all(
                    "Too many parts in multivariate mutual information! ",
                    "At most ", max_parts, " are allowed."));
    }

    // Multivariate mutual information is recursively defined by
    //
    //          I(a:…:y:z) = I(a:…:y) - I(a:…:y|z)
    //
    // Here, it is calculated as the alternating sum of (conditional)
    // entropies of all subsets of the parts [Jakulin & Bratko (2003)].
    //
    //      I(X₁:…:Xₙ|Y) = - Σ (-1)^|T| H(T|Y)
    //
    // where the sum is over all T ⊆ {X₁, …, Xₙ}.
    //
    // See: http://en.wikipedia.org/wiki/Multivariate_mutual_information

    std::vector<int> set_indices(num_parts);
    for (int i = 0; i < num_parts; ++i)
        set_indices[i] = get_set_index(q.parts[i]);

    int num_subsets = 1 << num_parts;
    int c = get_set_index(q.cond);
    // Start at i=1 because i=0 which corresponds to H(empty set) gives no
    // contribution to the sum. Furthermore, the i=0 is already reserved
    // for the constant term for our purposes.
    for (int set = 1; set < num_subsets; ++set) {
        int a = 0;
        int s = -1;
        for (int i = 0; i < num_parts; ++i) {
            if (set & 1<<i) {
                a |= set_indices[i];
                s = -s;
            }
        }
        v.inc(a|c, s*coef);
    }
    if (c)
        v.inc(c, -coef);
}

int ParserOutput::get_var_index(const std::string& s)
{
    auto&& it = vars.find(s);
    if (it != vars.end())
        return it->second;
    int next_index = var_names.size();
    check_num_vars(next_index + 1);
    vars[s] = next_index;
    var_names.push_back(s);
    return next_index;
}

int ParserOutput::get_set_index(const ast::VarList& l)
{
    int idx = 0;
    for (auto&& v : l)
        idx |= 1 << get_var_index(v);
    return idx;
}

void ParserOutput::add_relation(SparseVector v, bool is_inquiry)
{
    if (is_inquiry) {
        inquiries.push_back(move(v));
    } else {
        constraints.push_back(move(v));
        constraint_line.push_back(current_line);
    }
}

void ParserOutput::relation(ast::Relation re)
{
    bool is_inquiry = inquiries.empty();
    // create a SparseVector of standard '>=' form. For that the relation
    // needs to be transformed such that:
    //
    //      l <= r      =>      -l + r >= 0
    //      l >= r      =>       l - r >= 0
    //      l  = r      =>       l - r  = 0
    int l_sign = re.relation == ast::REL_LE ? -1 : 1;
    int r_sign = -l_sign;
    SparseVector v;
    v.is_equality = re.relation == ast::REL_EQ;
    for (auto&& term : re.left)
        add_term(v, term, l_sign);
    for (auto&& term : re.right)
        add_term(v, term, r_sign);
    add_relation(v, is_inquiry);
}

void ParserOutput::mutual_independence(ast::MutualIndependence mi)
{
    bool is_inquiry = inquiries.empty();
    // 0 = H(a) + H(b) + H(c) + … - H(a,b,c,…)
    int all = 0;
    SparseVector v;
    v.is_equality = true;
    for (auto&& vl : mi) {
        int idx = get_set_index(vl);
        all |= idx;
        v.inc(idx, 1);
    }
    v.inc(all, -1);
    add_relation(move(v), is_inquiry);
}

void ParserOutput::markov_chain(ast::MarkovChain mc)
{
    bool is_inquiry = inquiries.empty();
    int a = 0;
    for (int i = 0; i+2 < mc.size(); ++i) {
        int b, c;
        a |= get_set_index(mc[i+0]);
        b = get_set_index(mc[i+1]);
        c = get_set_index(mc[i+2]);
        // 0 = I(a:c|b) = H(a|b) + H(c|b) - H(a,c|b)
        SparseVector v;
        v.is_equality = true;
        v.inc(a|b, 1);
        v.inc(c|b, 1);
        v.inc(b, -1);
        v.inc(a|b|c, -1);
        add_relation(move(v), is_inquiry);
    }
}

void ParserOutput::function_of(ast::FunctionOf fo)
{
    bool is_inquiry = inquiries.empty();
    int func = get_set_index(fo.function);
    int of = get_set_index(fo.of);
    // 0 = H(func|of) = H(func,of) - H(of)
    SparseVector v;
    v.is_equality = true;
    v.inc(func|of, 1);
    v.inc(of, -1);
    add_relation(move(v), is_inquiry);
}


//----------------------------------------
// LinearProblem
//----------------------------------------

LinearProblem::LinearProblem()
{
    lp = glp_create_prob();
    glp_set_obj_dir(lp, GLP_MIN);
//printf("Rethna2:glp_get_num_cols(lp)=%d\n",glp_get_num_cols(lp));
}

LinearProblem::LinearProblem(int num_cols)
    : LinearProblem()
{
    add_columns(num_cols);
}

LinearProblem::~LinearProblem()
{
    glp_delete_prob(lp);
}

void LinearProblem::add_columns(int num_cols)
{
    glp_add_cols(lp, num_cols);
    for (int i = 1; i <= num_cols; ++i) {
        glp_set_col_bnds(lp, i, GLP_LO, 0, NAN);
    }
}

void LinearProblem::add(const SparseVector& v)
{
    std::vector<int> indices;
    std::vector<double> values;
    indices.reserve(v.entries.size());
    values.reserve(v.entries.size());
    for (auto&& ent : v.entries) {
        if (ent.first == 0)
            continue;
        indices.push_back(ent.first);
        values.push_back(ent.second);
    }

    int kind = v.is_equality ? GLP_FX : GLP_LO;
    int row = glp_add_rows(lp, 1);
    glp_set_row_bnds(lp, row, kind, -v.get(0), NAN);
    glp_set_mat_row(
            lp, row, indices.size(),
            indices.data()-1, values.data()-1);
}

// Tolerance for comparing values that are mathematically zero but come out
// of floating point arithmetic, e.g. an LP optimum of -1e-17. GLPK itself
// uses a relative feasibility tolerance of 1e-7 (glp_smcp::tol_bnd). The
// tolerance is scaled by the largest coefficient so that the outcome does
// not depend on how the relation is scaled.
static double tolerance(const SparseVector& v)
{
    double scale = 1;
    for (auto&& ent : v.entries)
        scale = std::max(scale, fabs(ent.second));
    return 1e-7 * scale;
}

// Evaluate a relation that consists of only a constant term, i.e. 'c>=0'
// or 'c=0'.
static bool holds_constant(const SparseVector& v)
{
    double c = v.get(0);
    if (v.is_equality)
        return fabs(c) <= tolerance(v);
    return c >= -tolerance(v);
}

void ShannonTypeProblem::add_constraint(const SparseVector& v,
                                        const std::string& text)
{
    add(v);
    row_info.push_back({RowInfo::CONSTRAINT, 0, 0, 0, 0,
                        (int) constraint_text.size() + 1});
    constraint_text.push_back(text);
}


// "X,Y" for the subset whose bits are set in `set`
std::string set_name(int set, const std::vector<std::string>& vars)
{
    std::string out;
    for (size_t i = 0; i < vars.size(); ++i) {
        if (set & (1 << i)) {
            if (!out.empty())
                out += ",";
            out += vars[i];
        }
    }
    return out;
}


// a number as it should read in a formula: "2", "0.5", and "" for one
static std::string format_number(double x)
{
    std::ostringstream out;
    if (x == (long long) x)
        out << (long long) x;
    else
        out << x;
    return out.str();
}


// a linear combination of joint entropies, e.g. "H(X) + H(Y) - H(X,Y)"
std::string format_vector(const SparseVector& v,
                          const std::vector<std::string>& vars)
{
    std::string out;
    for (auto&& ent : v.entries) {
        double c = ent.second;
        if (c == 0)
            continue;
        std::string term = ent.first == 0
            ? format_number(std::abs(c))
            : (std::abs(c) == 1 ? "" : format_number(std::abs(c)) + " ")
              + "H(" + set_name(ent.first, vars) + ")";
        if (out.empty())
            out = (c < 0 ? "-" : "") + term;
        else
            out += (c < 0 ? " - " : " + ") + term;
    }
    return out.empty() ? "0" : out;
}


// Recognise a multiple of a single information quantity, so that a sum of
// entropies can be read as the thing it is: H(A), H(A|B), I(A;B), I(A;B|K).
// The sets are read off the terms, so there is nothing to search.
std::string name_quantity(const SparseVector& v,
                          const std::vector<std::string>& vars)
{
    std::vector<int> plus, minus;
    double scale = 0;
    for (auto&& ent : v.entries) {
        if (ent.second == 0)
            continue;
        if (ent.first == 0)             // a constant term is not a quantity
            return "";
        double mag = std::fabs(ent.second);
        if (scale == 0)
            scale = mag;
        else if (std::fabs(mag - scale) > 1e-9)
            return "";                  // not all the same size
        (ent.second > 0 ? plus : minus).push_back(ent.first);
    }
    if (scale == 0 || plus.size() + minus.size() > 4)
        return "";
    std::sort(plus.begin(), plus.end());
    std::sort(minus.begin(), minus.end());

    std::string name;
    auto S = [&vars](int m) { return set_name(m, vars); };
    if (plus.size() == 1 && minus.empty()) {
        name = "H(" + S(plus[0]) + ")";
    } else if (plus.size() == 1 && minus.size() == 1) {
        int A = plus[0], B = minus[0];
        if ((B & ~A) != 0 || B == A)            // need B a subset of A
            return "";
        name = "H(" + S(A & ~B) + "|" + S(B) + ")";
    } else if (plus.size() == 2 && minus.size() == 1) {
        int A = plus[0], B = plus[1];
        if ((A & B) != 0 || minus[0] != (A | B))
            return "";
        name = "I(" + S(A) + ";" + S(B) + ")";
    } else if (plus.size() == 2 && minus.size() == 2) {
        int A = plus[0], B = plus[1], K = A & B;
        if (K == 0 || minus[0] != std::min(A | B, K)
                   || minus[1] != std::max(A | B, K))
            return "";
        name = "I(" + S(A & ~K) + ";" + S(B & ~K) + "|" + S(K) + ")";
    } else {
        return "";
    }
    return scale == 1 ? name : format_number(scale) + " " + name;
}


void print_proof(std::ostream& out, const Proof& proof)
{
    out << "Proof of  E >= 0  where  E = " << proof.expression;
    if (!proof.expression_name.empty())
        out << "  =  " << proof.expression_name;
    out << "\n\n";
    if (proof.terms.empty() && proof.constant == 0) {
        out << "  E is identically 0, hence E >= 0.\n";
        return;
    }
    // E = <terms so far> + [ what is left ], one line per term, until
    // there is nothing left over
    out << "  E  =  " << proof.expression << "\n";
    SparseVector rest = proof.expr_vec;
    std::vector<std::string> parts;
    for (auto&& t : proof.terms) {
        for (auto&& ent : t.vec.entries)
            rest.inc(ent.first, -t.coefficient * ent.second);
        parts.push_back((t.coefficient == 1 ? std::string()
                         : format_number(t.coefficient) + " ") + t.name);
        out << "     =  ";
        for (size_t k = 0; k < parts.size(); ++k)
            out << (k ? "  +  " : "") << parts[k];
        std::string left = format_vector(rest, proof.vars);
        if (left != "0") {
            std::string named = name_quantity(rest, proof.vars);
            out << "  +  [ " << (named.empty() ? left : named) << " ]";
        }
        out << "\n";
    }
    if (proof.constant != 0)
        out << "     (and a non-negative constant "
            << format_number(proof.constant) << ")\n";
    out << "\n  where every term is non-negative:\n";
    for (auto&& t : proof.terms) {
        out << "    " << t.name << " = " << t.expansion;
        if (!t.source.empty())
            out << "        [" << t.source << "]";
        out << "\n";
    }
    if (proof.constant != 0)
        out << "    " << format_number(proof.constant)
            << " is a non-negative constant\n";
}


// A dual value that is a hair off a simple number is that number: the
// simplex works in floating point, but these multipliers are rationals with
// small denominators.
static double tidy(double x)
{
    double r = std::floor(x * 2520.0 + 0.5) / 2520.0;
    return std::fabs(x - r) < 1e-7 ? r : x;
}


// Read the proof out of the dual solution. By LP duality the objective row
// is the non-negative combination  v = sum_j y_j a_j + d  of the rows (with
// multipliers y from the row duals) and the column non-negativities (with
// multipliers d from the reduced costs). Every multiplier is >= 0, so that
// combination is exactly a proof of  E >= 0.
//
// It is recomputed and compared against v before being believed: the duals
// come out of a floating point simplex, and a proof that does not add up is
// worse than no proof.
static bool extract_proof(glp_prob* lp, const SparseVector& v,
                          const std::vector<std::string>& vars,
                          const std::vector<RowInfo>& info,
                          const std::vector<std::string>& ctext,
                          const std::vector<std::string>& ktext,
                          Proof& proof)
{
    const double eps = 1e-9;
    int num_rows = glp_get_num_rows(lp);
    int num_cols = glp_get_num_cols(lp);
    int all = (1 << vars.size()) - 1;

    proof.terms.clear();
    proof.expression = format_vector(v, vars);
    proof.expression_name = name_quantity(v, vars);
    proof.expr_vec = v;
    proof.vars = vars;

    SparseVector total;                 // sum of multiplier * row, rebuilt
    total.is_equality = false;
    double constant = v.get(0);

    std::vector<int> ind(num_cols + 1);
    std::vector<double> val(num_cols + 1);

    for (int r = 1; r <= num_rows; ++r) {
        double y = tidy(glp_get_row_dual(lp, r));
        if (std::fabs(y) < eps)
            continue;
        // an inequality row may only be used one way round; an equality may
        // be used either way, which is what a negative multiplier means
        bool equality = glp_get_row_type(lp, r) == GLP_FX;
        if (y < 0 && !equality)
            return false;
        double sign = y < 0 ? -1.0 : 1.0;

        int len = glp_get_mat_row(lp, r, ind.data(), val.data());
        SparseVector row;
        row.is_equality = false;
        for (int k = 1; k <= len; ++k) {
            row.inc(ind[k], sign * val[k]);
            total.inc(ind[k], y * val[k]);
        }
        double lb = glp_get_row_lb(lp, r);
        if (lb != 0)                    // the row is  a.h - lb >= 0
            row.inc(0, -sign * lb);
        constant += y * lb;

        ProofTerm term;
        term.vec = row;
        term.coefficient = std::fabs(y);
        term.expansion = format_vector(row, vars);
        const RowInfo& what = (size_t) r <= info.size()
            ? info[r - 1] : RowInfo{RowInfo::CONSTRAINT, 0, 0, 0, 0, 0};
        if (what.kind == RowInfo::ENTROPY) {
            int rest = all ^ (1 << what.i);
            term.name = rest
                ? "H(" + vars[what.i] + "|" + set_name(rest, vars) + ")"
                : "H(" + vars[what.i] + ")";
        } else if (what.kind == RowInfo::MUTINF) {
            term.name = "I(" + vars[what.a] + ";" + vars[what.b]
                      + (what.K ? "|" + set_name(what.K, vars) : "") + ")";
        } else if (what.kind == RowInfo::KNOWN) {
            // an instance of a known non-Shannon-type inequality, which is
            // clearer written with the information quantities it was
            // stated in than as joint entropies
            term.name = "N" + std::to_string(what.index);
            if (what.index >= 1 && (size_t) what.index <= ktext.size())
                term.source = ktext[what.index - 1];
        } else {
            term.name = "C" + std::to_string(what.index);
            term.source = "from constraint " + std::to_string(what.index);
            if (y < 0)
                term.source += " (reversed)";
            if (what.index >= 1 && (size_t) what.index <= ctext.size()
                    && !ctext[what.index - 1].empty())
                term.source += ": " + ctext[what.index - 1];
        }
        proof.terms.push_back(term);
    }

    // the columns carry H(S) >= 0, which the reduced costs may also use
    for (int c = 1; c <= num_cols; ++c) {
        double d = tidy(glp_get_col_dual(lp, c));
        if (std::fabs(d) < eps)
            continue;
        if (d < 0)
            return false;
        total.inc(c, d);
        ProofTerm term;
        term.coefficient = d;
        term.vec.is_equality = false;
        term.vec.inc(c, 1);
        term.name = "H(" + set_name(c, vars) + ")";
        term.expansion = term.name;
        proof.terms.push_back(term);
    }

    proof.constant = tidy(constant);
    if (proof.constant < -eps)
        return false;

    // does it add up?  E must equal the terms plus the constant
    for (int c = 1; c <= num_cols; ++c) {
        if (std::fabs(total.get(c) - v.get(c)) > 1e-6)
            return false;
    }
    return true;
}


bool LinearProblem::check(const SparseVector& v)
{
    return check(v, nullptr, nullptr);
}


bool LinearProblem::check(const SparseVector& v, Proof* proof,
                          const std::vector<std::string>* vars)
{
    // check for equalities as I>=0 and -I>=0
    if (v.is_equality) {
        SparseVector v2(v);
        v2.is_equality = false;
        if (!check(v2, proof, vars))
            return false;
        for (auto&& ent : v2.entries)
            ent.second = -ent.second;
        return check(v2, proof, vars);
    }

    glp_smcp parm;
    glp_init_smcp(&parm);
    parm.msg_lev = GLP_MSG_ERR;

    int num_cols = glp_get_num_cols(lp);
//printf("Rethna3:glp_get_num_cols(lp)=%d\n",glp_get_num_cols(lp));
    for (int i = 1; i <= num_cols; ++i)
        glp_set_obj_coef(lp, i, v.get(i));

    int outcome = glp_simplex(lp, &parm);
    if (outcome != 0) {
        throw std::runtime_error(sprint_all(
                    "Error in glp_simplex: ", outcome));
    }

    int status = glp_get_status(lp);
    if (status == GLP_OPT) {
        // the original check was for the solution (primal variable values)
        // rather than objective value, but let's do it simpler for now (if
        // an optimum is found, it should be zero anyway):
        bool valid = glp_get_obj_val(lp) + v.get(0) >= -tolerance(v);
        if (valid && proof && vars) {
            if (!extract_proof(lp, v, *vars, row_info, constraint_text,
                               known_text, *proof))
                proof->expression.clear();      // no proof we can stand by
        }
        return valid;
    }

    if (status == GLP_UNBND) {
        return false;
    }

    // I am not sure about the exact distinction of GLP_NOFEAS, GLP_INFEAS,
    // GLP_UNDEF, so here is a generic error message:
    throw std::runtime_error(sprint_all(
                "no feasible solution (status code ", status, ")"
                ));
}


int LinearProblem::num_elemental() const
{
    int n = 0;
    while (n < (int) row_info.size() && row_info[n].kind != RowInfo::CONSTRAINT)
        ++n;
    return n;
}


void LinearProblem::force_zero(int k, bool on)
{
    glp_set_row_bnds(lp, k + 1, on ? GLP_FX : GLP_LO, 0.0, 0.0);
}


// The LP for "is I >= 0" minimises I over a cone (or, with constant terms in
// the constraints, a polyhedron), so when I can go negative it usually can
// go to minus infinity and the solver reports no point. Bounding I from
// below by -(|c|+1), c being I's constant, keeps the minimum finite while
// still below -c, so the optimum is a point where I < 0.
bool LinearProblem::counterexample(const SparseVector& v,
                                   std::vector<double>* elemental)
{
    SparseVector bound(v);
    bound.is_equality = false;
    bound.entries.erase(0);
    double lowest = -(fabs(v.get(0)) + 1);
    bound.inc(0, -lowest);                      // I_linear - lowest >= 0
    add(bound);
    int extra = glp_get_num_rows(lp);

    glp_smcp parm;
    glp_init_smcp(&parm);
    parm.msg_lev = GLP_MSG_OFF;
    int num_cols = glp_get_num_cols(lp);
    for (int i = 1; i <= num_cols; ++i)
        glp_set_obj_coef(lp, i, v.get(i));

    bool found = glp_simplex(lp, &parm) == 0
              && glp_get_status(lp) == GLP_OPT
              && glp_get_obj_val(lp) + v.get(0) < -tolerance(v);
    if (found && elemental) {
        elemental->assign(num_elemental(), 0.0);
        for (int k = 0; k < (int) elemental->size(); ++k)
            (*elemental)[k] = glp_get_row_prim(lp, k + 1);
    }
    int rows[2] = {0, extra};
    glp_del_rows(lp, 1, rows);
    glp_std_basis(lp);          // the old basis may refer to the deleted row
    return found;
}


// As counterexample(), but reading off the columns: the joint entropies.
bool LinearProblem::point_below(const SparseVector& v, std::vector<double>* h)
{
    SparseVector bound(v);
    bound.is_equality = false;
    bound.entries.erase(0);
    double lowest = -(fabs(v.get(0)) + 1);
    bound.inc(0, -lowest);
    add(bound);
    int extra = glp_get_num_rows(lp);

    glp_smcp parm;
    glp_init_smcp(&parm);
    parm.msg_lev = GLP_MSG_OFF;
    int num_cols = glp_get_num_cols(lp);
    for (int i = 1; i <= num_cols; ++i)
        glp_set_obj_coef(lp, i, v.get(i));

    bool found = glp_simplex(lp, &parm) == 0
              && glp_get_status(lp) == GLP_OPT
              && glp_get_obj_val(lp) + v.get(0) < -tolerance(v);
    if (found && h) {
        h->assign(num_cols + 1, 0.0);
        for (int c = 1; c <= num_cols; ++c)
            (*h)[c] = glp_get_col_prim(lp, c);
    }
    int rows[2] = {0, extra};
    glp_del_rows(lp, 1, rows);
    glp_std_basis(lp);
    return found;
}


void LinearProblem::add_known(const SparseVector& v, const std::string& text,
                              int family)
{
    add(v);
    known_text.push_back(text);
    known_family.push_back(family);
    row_info.push_back({RowInfo::KNOWN, 0, 0, 0, 0, (int) known_text.size()});
}


std::vector<int> LinearProblem::known_families_used()
{
    std::set<int> used;
    for (size_t r = 0; r < row_info.size(); ++r) {
        if (row_info[r].kind != RowInfo::KNOWN)
            continue;
        if (std::fabs(glp_get_row_dual(lp, (int) r + 1)) > 1e-9)
            used.insert(known_family[row_info[r].index - 1]);
    }
    return std::vector<int>(used.begin(), used.end());
}


ShannonTypeProblem::ShannonTypeProblem(int num_vars)
    : LinearProblem()
{
    // GLPK aborts the process when asked for zero columns.
    if (num_vars < 1) {
        throw std::invalid_argument(
                "ShannonTypeProblem needs at least one variable");
    }
    check_num_vars(num_vars);
    add_columns((1<<num_vars) - 1);
    add_elemental_inequalities(lp, num_vars, &row_info);
}


//----------------------------------------
// Known non-Shannon-type inequalities
//----------------------------------------
//
// A statement that does not follow from the basic inequalities may still be
// a theorem, because it follows from inequalities that are not Shannon-type.
// The ones below are all on four variables A, B, C, D. Each family is
// applied to the problem's variables in every order (A, B, C, D standing for
// four distinct single variables), and because that is a lot of rows, they
// are added only as needed: find a point of the current region where the
// statement fails, add the instances that point violates most, and solve
// again, until either the statement is proved or the point violates none of
// them, in which case these inequalities do not prove it either.

namespace {

// c * I(X;Y|Z), with X, Y, Z sets of the formal variables A=1, B=2, C=4, D=8
struct Quantity { double c; int X, Y, Z; };

const int A_ = 1, B_ = 2, C_ = 4, D_ = 8;

struct Family
{
    std::string name, reference, url, anchor;
    std::vector<Quantity> terms;        // sum of the terms >= 0
    std::vector<Quantity> conditions;   // must each be implied = 0, if any
};

// a I(A;B) <= b I(A;B|C) + c I(A;C|B) + d I(B;C|A) + e I(A;B|D)
//           + f I(A;D|B) + g I(B;D|A) + h I(C;D) + i I(C;D|A)
std::vector<Quantity> from_template(const int k[9])
{
    std::vector<Quantity> t = {
        {-(double) k[0], A_, B_, 0},
        {(double) k[1], A_, B_, C_}, {(double) k[2], A_, C_, B_},
        {(double) k[3], B_, C_, A_}, {(double) k[4], A_, B_, D_},
        {(double) k[5], A_, D_, B_}, {(double) k[6], B_, D_, A_},
        {(double) k[7], C_, D_, 0},  {(double) k[8], C_, D_, A_},
    };
    std::vector<Quantity> out;
    for (auto&& q : t)
        if (q.c != 0)
            out.push_back(q);
    return out;
}

const std::vector<Family>& known_families()
{
    static std::vector<Family> all;
    if (!all.empty())
        return all;

    all.push_back({"Zhang–Yeung conditional inequality (1997)",
        "Z. Zhang and R. W. Yeung, “A non-Shannon-type conditional "
        "inequality of information quantities,” IEEE Transactions on "
        "Information Theory, vol. 43, no. 6, 1997",
        "https://doi.org/10.1109/18.641561", "ref-10",
        {{1, C_, D_, A_}, {1, C_, D_, B_}, {-1, C_, D_, 0}},
        {{1, A_, B_, 0}, {1, A_, B_, C_}}});

    all.push_back({"Zhang–Yeung inequality (1998)",
        "Z. Zhang and R. W. Yeung, “On characterization of entropy "
        "function via information inequalities,” IEEE Transactions on "
        "Information Theory, vol. 44, no. 4, 1998",
        "https://doi.org/10.1109/18.681320", "ref-4",
        {{1, A_, B_, 0}, {1, A_, C_|D_, 0}, {3, C_, D_, A_},
         {1, C_, D_, B_}, {-2, C_, D_, 0}}, {}});

    // Matus's first family; s = 1 is Zhang-Yeung again
    for (int s = 2; s <= 8; ++s) {
        int k[9] = {s, s*(s+3)/2, s*(s+1)/2, 1, s, 0, 0, s, 0};
        all.push_back({"Matúš’s inequality with s = "
                + std::to_string(s) + " (2007)",
            "F. Matúš, “Infinitely many information "
            "inequalities,” Proc. IEEE ISIT 2007, pp. 41–44",
            "https://intranet.utia.cas.cz/biblio/pub/0085052", "ref-14",
            from_template(k), {}});
    }

    for (int n = 0; n < NUM_DFZ_INEQUALITIES; ++n) {
        all.push_back({"Dougherty–Freiling–Zeger inequality no. "
                + std::to_string(n + 1) + " (2011)",
            "R. Dougherty, C. Freiling and K. Zeger, “Non-Shannon "
            "information inequalities in four random variables,” "
            "arXiv:1104.3602, 2011",
            "https://arxiv.org/abs/1104.3602", "ref-16",
            from_template(DFZ_INEQUALITIES[n]), {}});
    }
    return all;
}

// the actual variables standing for the formal set `formal`
int place(int formal, const int perm[4])
{
    int set = 0;
    for (int k = 0; k < 4; ++k)
        if (formal & (1 << k))
            set |= 1 << perm[k];
    return set;
}

void add_mutinf(SparseVector& v, double c, int X, int Y, int Z)
{
    v.inc(X | Z, c);
    v.inc(Y | Z, c);
    v.inc(X | Y | Z, -c);
    if (Z)
        v.inc(Z, -c);
}

SparseVector instance(const std::vector<Quantity>& terms, const int perm[4])
{
    SparseVector v;
    v.is_equality = false;
    for (auto&& q : terms)
        add_mutinf(v, q.c, place(q.X, perm), place(q.Y, perm),
                   place(q.Z, perm));
    return v;
}

std::string quantity_text(const Quantity& q, const int perm[4],
                          const std::vector<std::string>& vars)
{
    double m = std::fabs(q.c);
    return (m == 1 ? std::string() : format_number(m) + " ")
         + "I(" + set_name(place(q.X, perm), vars) + ";"
         + set_name(place(q.Y, perm), vars)
         + (q.Z ? "|" + set_name(place(q.Z, perm), vars) : "") + ")";
}

// "Zhang-Yeung inequality (1998), A=X, B=Y, C=Z, D=W:  2 I(Z;W) <= ..."
std::string instance_text(const Family& f, const int perm[4],
                          const std::vector<std::string>& vars)
{
    std::string lhs, rhs;
    for (auto&& q : f.terms) {
        std::string& side = q.c < 0 ? lhs : rhs;
        side += (side.empty() ? "" : " + ") + quantity_text(q, perm, vars);
    }
    std::string out = f.name + " with A=" + vars[perm[0]] + ", B="
        + vars[perm[1]] + ", C=" + vars[perm[2]] + ", D=" + vars[perm[3]]
        + ":  " + (lhs.empty() ? "0" : lhs) + " <= " + rhs;
    if (!f.conditions.empty()) {
        out += ",  given ";
        for (size_t k = 0; k < f.conditions.size(); ++k)
            out += (k ? " and " : "") + quantity_text(f.conditions[k], perm, vars)
                 + " = 0";
    }
    return out;
}

double value_at(const SparseVector& v, const std::vector<double>& h)
{
    double x = v.get(0);
    for (auto&& ent : v.entries)
        if (ent.first > 0 && (size_t) ent.first < h.size())
            x += ent.second * h[ent.first];
    return x;
}

// u = t v for some t > 0 (ignoring zero entries)?
bool proportional(const SparseVector& u, const SparseVector& v)
{
    double t = 0;
    std::set<int> keys;
    for (auto&& e : u.entries) if (std::fabs(e.second) > 1e-12) keys.insert(e.first);
    for (auto&& e : v.entries) if (std::fabs(e.second) > 1e-12) keys.insert(e.first);
    for (int k : keys) {
        double a = u.get(k), b = v.get(k);
        if (std::fabs(b) < 1e-12 || std::fabs(a) < 1e-12)
            return false;
        if (t == 0)
            t = a / b;
        if (t <= 0 || std::fabs(a - t * b) > 1e-9 * std::max(1.0, std::fabs(a)))
            return false;
    }
    return t > 0;
}

// Prove `dir` >= 0 with the known inequalities added to `prob` as needed.
// Rows added stay in `prob`, so the next direction starts from them.
struct KnownSearch
{
    std::vector<std::array<int, 4>> perms;
    // indices into known_families(), in tiers: the classic inequalities
    // first, so that a statement is credited to the simplest known
    // inequalities that prove it, not to later ones that imply them
    std::vector<std::vector<int>> tiers;
    std::set<std::pair<int, int>> added;        // (family, perm) already rows
    std::map<std::pair<int, int>, bool> holds;  // conditions implied?

    // every instance as (column, coefficient) pairs, built on first use and
    // scaled to unit size, which is all that finding violated ones needs
    std::map<int, std::vector<std::vector<std::pair<int, double>>>> compact;
    const std::vector<std::pair<int, double>>& row(int family, int perm)
    {
        auto& rows = compact[family];
        if (rows.empty()) {
            const Family& f = known_families()[family];
            double norm = 0;
            for (auto&& q : f.terms)
                norm += std::fabs(q.c);
            rows.resize(perms.size());
            for (size_t pi = 0; pi < perms.size(); ++pi) {
                SparseVector v = instance(f.terms, perms[pi].data());
                for (auto&& e : v.entries)
                    if (e.first > 0 && e.second != 0)
                        rows[pi].push_back({e.first, e.second / norm});
            }
        }
        return rows[perm];
    }
};

bool prove_with_known(ShannonTypeProblem& prob, const SparseVector& dir,
                      const std::vector<std::string>& vars, Proof* proof,
                      KnownSearch& ks, std::vector<int>* used)
{
    const auto& fams = known_families();
    const std::vector<std::string>* pvars = proof ? &vars : nullptr;
    const int per_round = 24, max_rounds = 60;

    // A statement that is itself a known inequality (up to a positive
    // factor, on some ordering of its variables) is credited to that one.
    for (auto&& tier : ks.tiers) {
        for (int fi : tier) {
            const Family& f = fams[fi];
            if (!f.conditions.empty())
                continue;
            for (size_t pi = 0; pi < ks.perms.size(); ++pi) {
                SparseVector row = instance(f.terms, ks.perms[pi].data());
                if (!proportional(dir, row))
                    continue;
                std::pair<int, int> key(fi, (int) pi);
                if (!ks.added.count(key)) {
                    prob.add_known(row, instance_text(f, ks.perms[pi].data(), vars), fi);
                    ks.added.insert(key);
                }
                if (prob.check(dir, proof, pvars)) {
                    if (used)
                        *used = prob.known_families_used();
                    return true;
                }
            }
        }
    }

    std::vector<int> families;
    for (auto&& tier : ks.tiers) {
    families.insert(families.end(), tier.begin(), tier.end());
    for (int round = 0; round < max_rounds; ++round) {
        std::vector<double> h;
        if (!prob.point_below(dir, &h))
            break;                  // nothing left to cut off: check it

        double scale = 1;
        for (double x : h)
            scale = std::max(scale, std::fabs(x));

        // the instances this point violates, worst first
        std::vector<std::pair<double, std::pair<int, int>>> cut;
        for (int fi : families) {
            for (size_t pi = 0; pi < ks.perms.size(); ++pi) {
                double x = 0;
                for (auto&& e : ks.row(fi, (int) pi))
                    x += e.second * h[e.first];
                if (x < -1e-7 * scale) {
                    std::pair<int, int> key(fi, (int) pi);
                    if (!ks.added.count(key))
                        cut.push_back({x, key});
                }
            }
        }
        std::sort(cut.begin(), cut.end());

        int taken = 0;
        for (auto&& c : cut) {
            if (taken >= per_round)
                break;
            const Family& f = fams[c.second.first];
            const int* perm = ks.perms[c.second.second].data();
            if (!f.conditions.empty()) {
                // a conditional inequality holds only where its conditions
                // do, so it may be used only if the constraints imply them
                auto it = ks.holds.find(c.second);
                if (it == ks.holds.end()) {
                    bool all = true;
                    for (auto&& q : f.conditions) {
                        SparseVector neg = instance({{-q.c, q.X, q.Y, q.Z}}, perm);
                        if (!prob.check(neg)) { all = false; break; }
                    }
                    it = ks.holds.insert({c.second, all}).first;
                }
                if (!it->second)
                    continue;
            }
            prob.add_known(instance(f.terms, perm),
                           instance_text(f, perm, vars), c.second.first);
            ks.added.insert(c.second);
            ++taken;
        }
        if (taken == 0)
            break;                  // this tier all holds here: try the next

        if (prob.check(dir, proof, pvars)) {
            if (used)
                *used = prob.known_families_used();
            return true;
        }
    }
    }
    if (prob.check(dir, proof, pvars)) {
        if (used)
            *used = prob.known_families_used();
        return true;
    }
    return false;
}

KnownSearch make_known_search(int num_vars)
{
    KnownSearch ks;
    for (int a = 0; a < num_vars; ++a)
        for (int b = 0; b < num_vars; ++b)
            for (int c = 0; c < num_vars; ++c)
                for (int d = 0; d < num_vars; ++d)
                    if (a != b && a != c && a != d && b != c && b != d && c != d)
                        ks.perms.push_back(std::array<int, 4>{{a, b, c, d}});
    // Zhang-Yeung (conditional and not), then Matus's family, then the
    // Dougherty-Freiling-Zeger list. With many variables the last is too
    // much work per round on every ordering, so it is left out.
    int count = (int) known_families().size();
    ks.tiers = {{0, 1}, {}, {}};
    for (int f = 2; f < 2 + 7; ++f)
        ks.tiers[1].push_back(f);
    if (num_vars <= 5)
        for (int f = 2 + 7; f < count; ++f)
            ks.tiers[2].push_back(f);
    return ks;
}

}   // namespace



//----------------------------------------
// globals
//----------------------------------------

ParserOutput parse(const std::vector<std::string>& exprs)
{
    ParserOutput out;
    out.source_lines = exprs;
    for (int row = 0; row < exprs.size(); ++row) {
        const std::string& line = exprs[row];
        out.current_line = row;
        std::istringstream in(line);
        yy::scanner scanner(&in);
        yy::parser parser(&scanner, &out);
        try {
            int result = parser.parse();
            if (result != 0) {
                // Not sure if this can even happen
                throw std::runtime_error("Unknown parsing error");
            }
        }
        catch (yy::parser::syntax_error& e) {
            // For undefined tokens, bison currently just tells us something
            // like 'unexpected $undefined' without printing the offending
            // character. This is much more useful:
            // Columns are 1-based and the end column is exclusive. At the
            // end of the input begin==end, so mark at least one character.
            int col = e.location.begin.column;
            int len = std::max(1, int(e.location.end.column) - col);
            std::string new_message = sprint_all(
                    e.what(), "\n",
                    "in row ", row+1, " col ", col, ":\n\n"
                    "    ", line, "\n",
                    "    ", std::string(col-1, ' '), std::string(len, '^'));
            throw yy::parser::syntax_error(e.location, new_message);
        }
    }
    if (out.inquiries.empty()) {
        throw std::runtime_error("This information theoretic expression is invalid or not defined");
    }
    return move(out);
}


// TODO: implement optimization as in Xitip: collapse variables that only
// appear together

// Prove each inquiry, printing the proof when asked for. A Markov chain or
// an independence turns into several constraints, so a term says which line
// it came from rather than which constraint.
bool prove(const ParserOutput& out, std::ostream& os, bool show_proof,
           std::vector<KnownUse>* known)
{
    if (known)
        known->clear();
    if (out.var_names.empty()) {
        for (auto&& constraint : out.constraints) {
            if (!holds_constant(constraint))
                throw std::runtime_error(
                        "no feasible solution (contradictory constraints)");
        }
        for (auto&& inquiry : out.inquiries) {
            if (!holds_constant(inquiry))
                return false;
        }
        return true;
    }

    ShannonTypeProblem prob(out.var_names.size());
    KnownSearch ks;
    bool searched = false;
    std::vector<int> families;      // known ones used, in order of first use
    for (size_t i = 0; i < out.constraints.size(); ++i) {
        int row = i < out.constraint_line.size() ? out.constraint_line[i] : -1;
        std::string text = (row >= 0 && (size_t) row < out.source_lines.size())
            ? out.source_lines[row] : std::string();
        prob.add_constraint(out.constraints[i], text);
    }

    for (auto&& inquiry : out.inquiries) {
        // an equality is two inequalities, and so has two proofs
        std::vector<SparseVector> directions;
        if (inquiry.is_equality) {
            SparseVector a(inquiry);
            a.is_equality = false;
            SparseVector b(a);
            for (auto&& ent : b.entries)
                ent.second = -ent.second;
            directions.push_back(move(a));
            directions.push_back(move(b));
        } else {
            directions.push_back(inquiry);
        }
        for (auto&& dir : directions) {
            Proof proof;
            bool ok = prob.check(dir, show_proof ? &proof : nullptr,
                                 show_proof ? &out.var_names : nullptr);
            if (ok && searched) {
                // rows of known inequalities are already in; note any used
                for (int f : prob.known_families_used())
                    if (std::find(families.begin(), families.end(), f) == families.end())
                        families.push_back(f);
            }
            if (!ok && known && out.var_names.size() >= 4) {
                if (!searched) {
                    ks = make_known_search((int) out.var_names.size());
                    searched = true;
                }
                std::vector<int> used;
                ok = prove_with_known(prob, dir, out.var_names,
                                      show_proof ? &proof : nullptr, ks, &used);
                for (int f : used)
                    if (std::find(families.begin(), families.end(), f) == families.end())
                        families.push_back(f);
            }
            if (!ok)
                return false;
            if (show_proof) {
                if (proof.expression.empty())
                    os << "(the solver found no proof it could stand by)\n\n";
                else {
                    print_proof(os, proof);
                    os << "\n";
                }
            }
        }
    }
    if (known) {
        const auto& fams = known_families();
        for (int f : families)
            known->push_back({fams[f].name, fams[f].reference, fams[f].url,
                              fams[f].anchor});
    }
    return true;
}


bool check(const ParserOutput& out)
{
//  printf("check:out.var_names.size()=%d\n",(int) out.var_names.size());
    // Without any random variables every relation is a plain comparison of
    // constants (e.g. '1 >= 0'), so there is no LP to solve.
    if (out.var_names.empty()) {
        for (auto&& constraint : out.constraints) {
            if (!holds_constant(constraint))
                throw std::runtime_error(
                        "no feasible solution (contradictory constraints)");
        }
        for (auto&& inquiry : out.inquiries) {
            if (!holds_constant(inquiry))
                return false;
        }
        return true;
    }

    ShannonTypeProblem prob(out.var_names.size());
    for (auto&& constraint : out.constraints)
        prob.add(constraint);
    for (auto&& inquiry : out.inquiries) {
        if (!prob.check(inquiry))
            return false;
    }
    return true;
}


//----------------------------------------
// What would make a statement true
//----------------------------------------
//
// A statement that is not provable may become provable once something is
// assumed about the variables. The assumptions tried are the elemental
// quantities, each forced to zero: every one of them reads as a condition
// anybody would state -- an independence, a conditional independence, or a
// functional dependence -- and they are already rows of the LP, so trying
// one is changing a row's bounds and solving again from the last basis.
//
// The counterexample prunes the search. If a quantity is already zero at
// the counterexample, assuming it is zero leaves that counterexample in
// place, so a set of assumptions is worth testing only when at least one of
// its members is strictly positive there.

namespace {

std::string join(const std::vector<std::string>& parts, const char* sep)
{
    std::string out;
    for (size_t k = 0; k < parts.size(); ++k)
        out += (k ? sep : "") + parts[k];
    return out;
}

std::vector<std::string> members(int set, const std::vector<std::string>& vars)
{
    std::vector<std::string> out;
    for (int v = 0; v < (int) vars.size(); ++v)
        if (set & (1 << v))
            out.push_back(vars[v]);
    return out;
}

// the elemental quantity, as constraint text the parser accepts
std::string condition_text(const RowInfo& r, const std::vector<std::string>& vars)
{
    int all = (1 << vars.size()) - 1;
    if (r.kind == RowInfo::ENTROPY) {
        int rest = all & ~(1 << r.i);
        return "H(" + vars[r.i] + (rest ? "|" + set_name(rest, vars) : "")
             + ") = 0";
    }
    return "I(" + vars[r.a] + ";" + vars[r.b]
         + (r.K ? "|" + set_name(r.K, vars) : "") + ") = 0";
}

// what forcing it to zero says about the variables
std::string condition_meaning(const RowInfo& r, const std::vector<std::string>& vars)
{
    int all = (1 << vars.size()) - 1;
    if (r.kind == RowInfo::ENTROPY) {
        std::vector<std::string> others = members(all & ~(1 << r.i), vars);
        if (others.empty())
            return vars[r.i] + " is a constant";
        return vars[r.i] + " is a function of " + join(others, ", ")
             + ", written " + vars[r.i] + ":" + join(others, ",");
    }
    const std::string& a = vars[r.a];
    const std::string& b = vars[r.b];
    std::vector<std::string> given = members(r.K, vars);
    if (given.empty())
        return a + " and " + b + " are independent, written " + a + "." + b;
    // with everything else given, this is exactly a three part Markov chain
    std::string out = a + " and " + b + " are independent given " + join(given, ", ");
    if (given.size() + 2 == vars.size())
        out += ", the Markov chain " + a + "/" + join(given, ",") + "/" + b;
    return out;
}

// Every inquiry, both ways round for an equality, as inequalities I >= 0.
std::vector<SparseVector> directions(const Matrix& inquiries)
{
    std::vector<SparseVector> out;
    for (auto&& inquiry : inquiries) {
        SparseVector a(inquiry);
        a.is_equality = false;
        out.push_back(a);
        if (inquiry.is_equality) {
            for (auto&& ent : a.entries)
                ent.second = -ent.second;
            out.push_back(a);
        }
    }
    return out;
}

// whether every direction holds; an assumption that leaves no feasible
// point proves everything vacuously, which is no help, so that is a no
bool holds_all(LinearProblem& prob, const std::vector<SparseVector>& dirs)
{
    try {
        for (auto&& d : dirs)
            if (!prob.check(d))
                return false;
        return true;
    }
    catch (std::runtime_error&) {
        return false;
    }
}

}


Conditions sufficient_conditions(const ParserOutput& out, int maxsize,
                                 int limit, int budget)
{
    if (maxsize < 1)
        throw std::runtime_error("maxsize must be at least one");
    if (out.var_names.empty())
        throw std::runtime_error("there are no random variables to make "
                                 "assumptions about");

    ShannonTypeProblem prob(out.var_names.size());
    for (auto&& constraint : out.constraints)
        prob.add(constraint);
    std::vector<SparseVector> dirs = directions(out.inquiries);
    if (holds_all(prob, dirs))
        throw std::runtime_error("the statement is already provable, so "
                                 "there is nothing to assume");

    // the candidates, in the order Xitip.jl lists them: the entropies, then
    // for each pair of variables the conditioning sets from largest down
    std::vector<int> order(prob.num_elemental());
    for (int k = 0; k < (int) order.size(); ++k)
        order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&prob](int x, int y) {
        const RowInfo& p = prob.row(x);
        const RowInfo& q = prob.row(y);
        if (p.kind != q.kind) return p.kind == RowInfo::ENTROPY;
        if (p.kind == RowInfo::ENTROPY) return p.i < q.i;
        if (p.a != q.a) return p.a < q.a;
        if (p.b != q.b) return p.b < q.b;
        return p.K > q.K;
    });

    Conditions result;
    result.candidates = (int) order.size();
    result.maxsize = maxsize;

    // a quantity already zero at the counterexample cannot rule it out
    std::vector<bool> useful(order.size(), true);
    for (auto&& d : dirs) {
        std::vector<double> value;
        if (prob.counterexample(d, &value)) {
            double scale = 1;
            for (double x : value)
                scale = std::max(scale, fabs(x));
            for (size_t c = 0; c < order.size(); ++c)
                useful[c] = value[order[c]] > 1e-7 * scale;
            break;
        }
    }

    std::vector<std::vector<int>> found;    // as positions in `order`
    auto covered = [&found](const std::vector<int>& set) {
        for (auto&& f : found)
            if (std::includes(set.begin(), set.end(), f.begin(), f.end()))
                return true;
        return false;
    };

    int n = (int) order.size();
    bool stop = false;
    for (int size = 1; size <= std::min(maxsize, n) && !stop; ++size) {
        std::vector<int> set(size);
        for (int k = 0; k < size; ++k)
            set[k] = k;
        while (!stop) {
            bool any_useful = false;
            for (int c : set)
                any_useful = any_useful || useful[c];
            if (any_useful && !covered(set)) {
                if (result.tested >= budget) {
                    result.exhausted = false;
                    stop = true;
                    break;
                }
                ++result.tested;
                for (int c : set) prob.force_zero(order[c], true);
                bool works = holds_all(prob, dirs);
                for (int c : set) prob.force_zero(order[c], false);
                if (works) {
                    found.push_back(set);
                    SufficientCondition cond;
                    for (int c : set) {
                        const RowInfo& r = prob.row(order[c]);
                        cond.constraints.push_back(condition_text(r, out.var_names));
                        cond.meanings.push_back(condition_meaning(r, out.var_names));
                    }
                    result.found.push_back(cond);
                    if ((int) result.found.size() >= limit) {
                        result.exhausted = false;
                        stop = true;
                        break;
                    }
                }
            }
            // next subset of this size, in lexicographic order
            int k = size - 1;
            while (k >= 0 && set[k] == n - size + k)
                --k;
            if (k < 0)
                break;
            ++set[k];
            for (int m = k + 1; m < size; ++m)
                set[m] = set[m - 1] + 1;
        }
    }
    return result;
}


void print_conditions(std::ostream& os, const Conditions& c)
{
    if (c.found.empty()) {
        os << "No set of at most " << c.maxsize
           << (c.maxsize == 1 ? " assumption" : " assumptions")
           << " out of " << c.candidates << " candidates makes it provable"
           << (c.exhausted ? "" : " within the budget") << ".\n";
        return;
    }
    os << "Provable if you assume any one of these:\n";
    for (size_t k = 0; k < c.found.size(); ++k) {
        const SufficientCondition& cond = c.found[k];
        os << "\n  " << k + 1 << ". " << join(cond.constraints, "  and  ") << "\n";
        for (auto&& meaning : cond.meanings)
            os << "       " << meaning << "\n";
    }
    if (!c.exhausted)
        os << "\n(search stopped early; there may be more)\n";
}
