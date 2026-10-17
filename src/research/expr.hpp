// expr.hpp — the small rule language edge_lab strategy specs are written in.
//
//   abs(px - open) >= k * atr && !fomc        sign(px - vwap)        ema(5, 9) > ema(5, 21) && lag(5, ema(5, 9) <= ema(5, 21))
//
// Numbers, identifiers, unary - and !, * / + -, < <= > >= == !=, && ||, parentheses and calls name(arg, ...).
// Parameters are substituted at parse time (one parse per variant), so they may appear anywhere, including the
// constant arguments of indicator calls (ema(5, n)). Names are bound by the caller (edge_lab.hpp); this file only
// builds the tree and folds constants. A comparison or logic result is 1 or 0; NaN compares false.
#pragma once
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace lab {

struct Node {
    enum Kind { Num, Var, Neg, Not, Bin, Call } kind = Num;
    double v = 0;                 // Num
    std::string name;             // Var / Call
    std::string op;               // Bin
    std::vector<std::unique_ptr<Node>> a;
    int slot = -1;                // bound by the caller (variable id or indicator id)
};
using NodeP = std::unique_ptr<Node>;

class Parser {
public:
    Parser(const std::string& s, const std::map<std::string, double>& consts) : s_(s), c_(consts) {}
    NodeP parse() {
        NodeP n = expr(0);
        skip();
        if (p_ != s_.size()) fail("unexpected '" + s_.substr(p_, 12) + "'");
        return n;
    }
private:
    const std::string& s_;
    const std::map<std::string, double>& c_;
    size_t p_ = 0;

    [[noreturn]] void fail(const std::string& m) const { throw std::runtime_error("expr \"" + s_ + "\": " + m); }
    void skip() { while (p_ < s_.size() && std::isspace((unsigned char)s_[p_])) ++p_; }
    bool eat(const char* t) {
        skip();
        size_t n = std::char_traits<char>::length(t);
        if (s_.compare(p_, n, t) == 0) { p_ += n; return true; }
        return false;
    }
    static int prec(const std::string& op) {
        if (op == "||") return 1;
        if (op == "&&") return 2;
        if (op == "==" || op == "!=") return 3;
        if (op == "<" || op == "<=" || op == ">" || op == ">=") return 4;
        if (op == "+" || op == "-") return 5;
        if (op == "*" || op == "/") return 6;
        return -1;
    }
    std::string peek_op() {
        skip();
        static const char* ops[] = {"||", "&&", "==", "!=", "<=", ">=", "<", ">", "+", "-", "*", "/"};
        for (const char* o : ops)
            if (s_.compare(p_, std::char_traits<char>::length(o), o) == 0) return o;
        return "";
    }
    NodeP expr(int min_prec) {
        NodeP lhs = unary();
        for (;;) {
            std::string op = peek_op();
            int pr = op.empty() ? -1 : prec(op);
            if (pr < 0 || pr < min_prec) break;
            p_ += op.size();
            NodeP rhs = expr(pr + 1);
            lhs = fold(bin(op, std::move(lhs), std::move(rhs)));
        }
        return lhs;
    }
    NodeP unary() {
        if (eat("-")) { auto n = std::make_unique<Node>(); n->kind = Node::Neg; n->a.push_back(unary()); return fold(std::move(n)); }
        if (eat("!")) { auto n = std::make_unique<Node>(); n->kind = Node::Not; n->a.push_back(unary()); return fold(std::move(n)); }
        return primary();
    }
    NodeP primary() {
        skip();
        if (p_ >= s_.size()) fail("unexpected end");
        if (eat("(")) { NodeP n = expr(0); if (!eat(")")) fail("missing ')'"); return n; }
        char ch = s_[p_];
        if (std::isdigit((unsigned char)ch) || ch == '.') {
            size_t used = 0; double v = std::stod(s_.substr(p_), &used); p_ += used;
            auto n = std::make_unique<Node>(); n->v = v; return n;
        }
        if (std::isalpha((unsigned char)ch) || ch == '_') {
            size_t b = p_;
            while (p_ < s_.size() && (std::isalnum((unsigned char)s_[p_]) || s_[p_] == '_')) ++p_;
            std::string id = s_.substr(b, p_ - b);
            if (eat("(")) {
                auto n = std::make_unique<Node>(); n->kind = Node::Call; n->name = id;
                if (!eat(")")) {
                    do n->a.push_back(expr(0)); while (eat(","));
                    if (!eat(")")) fail("missing ')' after " + id + "(");
                }
                return fold(std::move(n));
            }
            auto it = c_.find(id);
            auto n = std::make_unique<Node>();
            if (it != c_.end()) { n->v = it->second; return n; }
            n->kind = Node::Var; n->name = id; return n;
        }
        fail(std::string("unexpected '") + ch + "'");
    }
    static NodeP bin(const std::string& op, NodeP l, NodeP r) {
        auto n = std::make_unique<Node>(); n->kind = Node::Bin; n->op = op;
        n->a.push_back(std::move(l)); n->a.push_back(std::move(r)); return n;
    }
    static bool all_num(const Node& n) { for (auto& x : n.a) if (x->kind != Node::Num) return false; return true; }
    static NodeP num(double v) { auto n = std::make_unique<Node>(); n->v = v; return n; }
    static NodeP fold(NodeP n) {
        if (!all_num(*n)) return n;
        if (n->kind == Node::Neg) return num(-n->a[0]->v);
        if (n->kind == Node::Not) return num(n->a[0]->v == 0 ? 1 : 0);
        if (n->kind == Node::Bin) return num(apply(n->op, n->a[0]->v, n->a[1]->v));
        if (n->kind == Node::Call) {
            const auto& f = n->name; const auto& a = n->a;
            if (f == "abs" && a.size() == 1) return num(std::fabs(a[0]->v));
            if (f == "sign" && a.size() == 1) return num(sgn(a[0]->v));
            if (f == "min" && a.size() == 2) return num(std::fmin(a[0]->v, a[1]->v));
            if (f == "max" && a.size() == 2) return num(std::fmax(a[0]->v, a[1]->v));
            if (f == "hm" && a.size() == 2) return num(a[0]->v * 60 + a[1]->v);
        }
        return n;
    }
public:
    static double sgn(double x) { return std::isnan(x) ? NAN : (x > 0) - (x < 0); }
    static double apply(const std::string& op, double x, double y) {
        switch (op[0]) {
            case '+': return x + y;
            case '-': return x - y;
            case '*': return x * y;
            case '/': return y == 0 ? NAN : x / y;
            case '<': return op.size() == 1 ? x < y : x <= y;
            case '>': return op.size() == 1 ? x > y : x >= y;
            case '=': return x == y;
            case '!': return x != y && !std::isnan(x) && !std::isnan(y);
            case '&': return (x != 0 && !std::isnan(x)) && (y != 0 && !std::isnan(y));
            case '|': return (x != 0 && !std::isnan(x)) || (y != 0 && !std::isnan(y));
        }
        return NAN;
    }
};

inline NodeP parse(const std::string& s, const std::map<std::string, double>& consts = {}) {
    return Parser(s, consts).parse();
}

}  // namespace lab
