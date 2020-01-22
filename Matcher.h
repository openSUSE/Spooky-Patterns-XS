#include <cstdint>
#include <list>
#include <vector>
#include <string>

struct Match {
    int start;
    int matched;
    int pattern;
    int sline;
    int eline;
};

typedef std::list<Match> Matches;

struct Token {
    int linenumber;
    uint64_t hash;
    std::string text;
};

typedef std::vector<Token> TokenList;

class TokenTree;

struct Matcher {
    TokenTree *ignore_tree;
    TokenTree *pattern_tree;

    ssize_t longest_pattern;

    static Matcher* _self;
    static Matcher* self() {
      if (!_self) {
        _self = new Matcher();
      }

      return _self;
    }

    Matcher();
    bool to_ignore(uint64_t t) const;
    void init();
    void add_token(TokenList& result, const char* start, size_t len, int line) const;
    void tokenize(TokenList& result, char* str, int linenumber = 0);
};
