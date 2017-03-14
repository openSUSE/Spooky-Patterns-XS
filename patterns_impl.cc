// Copyright © 2017 SUSE LLC
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, see <http://www.gnu.org/licenses/>.

#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "SpookyV2.h"
#include "patterns_impl.h"
#include <boost/tokenizer.hpp>
#include <cstring>
#include <iostream>
#include <list>
#include <map>

#define DEBUG 0

// typical comment and markup - have to be single tokens!
const char* ignored_tokens[] = { "/*", "*/", "//", "%", "%%", "dnl",
    "//**", "/**", "**", "#~", ";;", "\"\"", "--", "#:",
    "{", "\\", ">", "==", "::", " ",
    "##", "|", 0 };

bool to_ignore(const char* token)
{
    int index = 0;
    while (ignored_tokens[index]) {
        if (!strcmp(token, ignored_tokens[index]))
            return true;
        index++;
    }
    return false;
}

struct Token {
    int linenumber;
    uint64_t hash;
#if DEBUG
    std::string text;
#endif
};

typedef std::vector<Token> TokenList;

const int MAX_TOKEN_LENGTH = 100;

void tokenize(TokenList& result, const std::string& str, int linenumber = 0)
{
    char copy[MAX_TOKEN_LENGTH];

    typedef boost::tokenizer<boost::char_separator<char> >
        tokenizer;
    // drop whitespace, but keep punctation in the token flow - mostly to be ignored
    boost::char_separator<char> sep(" \r\n\t*;,:!#", "-.+?\"\'=");
    tokenizer tokens(str, sep);
    for (tokenizer::iterator tok_iter = tokens.begin();
         tok_iter != tokens.end(); ++tok_iter) {
        size_t len = tok_iter->copy(copy, MAX_TOKEN_LENGTH - 1);
        copy[len] = 0;
        for (unsigned int i = 0; i < len; i++) {
            // snipe out escape sequences
            if (copy[i] < ' ')
                copy[i] = ' ';
            copy[i] = tolower(copy[i]);
        }
        if (to_ignore(copy))
            continue;
        Token t;
#if DEBUG
        t.text = copy;
#endif
        t.linenumber = linenumber;
        t.hash = 0;
        if (!linenumber && !strncmp(copy, "$skip", 5)) {
	  const char *number = copy + 5;
	  char *endptr;
	  t.hash = strtol(number, &endptr, 10);
	  if (*endptr || t.hash > 20) { // more than just a number
	    t.hash = 0;
	  }
	}
        if (!t.hash) {
            // hash64 has no collisions on our patterns and is very fast
            // *and* 0-20 are "free"
            t.hash = SpookyHash::Hash64(copy, len, 1);
            assert(t.hash > 20);
        }
        result.push_back(t);
    }
}

struct Pattern {
    TokenList tokens;
    int id;
};

typedef std::vector<Pattern> PatternList;

AV* pattern_parse(const char* str)
{
    TokenList t;
    tokenize(t, str);
    AV* ret = newAV();
    av_extend(ret, t.size());
    int index = 0;
    for (TokenList::const_iterator it = t.begin(); it != t.end(); ++it) {
        // do not start with an expansion variable
        if (!index && it->hash < 20)
            continue;
        av_store(ret, index, newSVuv(it->hash));
        ++index;
    }
    return ret;
}

typedef std::map<uint64_t, PatternList> PatternHash;

struct Matcher {
    PatternHash patterns;
};

Matcher* pattern_init_matcher()
{
    return new Matcher;
}

void destroy_matcher(Matcher* m)
{
    delete m;
}

void pattern_add(Matcher* m, unsigned int id, av* tokens)
{
    Pattern p;
    p.id = id;

    SSize_t len = av_top_index(tokens) + 1;
    if (!len) {
        std::cerr << "add failed for id " << id << std::endl;
        return;
    }
    p.tokens.reserve(len);

    uint64_t prime = SvUV(*av_fetch(tokens, 0, 0));
    Token t;
    for (SSize_t i = 1; i < len; ++i) {
        SV* sv = *av_fetch(tokens, i, 0);
        UV uv = SvUV(sv);
        t.hash = uv;
        p.tokens.push_back(t);
    }

    m->patterns[prime].push_back(p);
}

int check_token_matches(const TokenList& tokens, unsigned int offset, TokenList::const_iterator pat_iter, const TokenList::const_iterator& pat_end)
{
    while (pat_iter != pat_end) {
        // pattern longer than text -> fail
        if (offset >= tokens.size())
            return 0;

#if DEBUG
        fprintf(stderr, "MP %d %d:%s %lx-%lx\n", offset, tokens[offset].hash == pat_iter->hash, tokens[offset].text.c_str(), pat_iter->hash, tokens[offset].hash);
#endif

        if (pat_iter->hash < 20) {
            int max_gap = pat_iter->hash;
            for (int i = 0; i <= max_gap; ++i) {
                int matched = check_token_matches(tokens, offset + i, pat_iter + 1, pat_end);
                if (matched)
                    return matched;
            }
            return 0;
        } else {
            if (tokens[offset].hash != pat_iter->hash) {
                return 0;
            }
        }
        offset++;
        pat_iter++;
    }
    return offset;
}

int match_pattern(const TokenList& tokens, unsigned int offset, const Pattern& p)
{
    unsigned int index = 1; // the prime was already checked
    TokenList::const_iterator pat_iter = p.tokens.begin();

    index = check_token_matches(tokens, offset + index, pat_iter, p.tokens.end());
    if (index)
        return index - offset;
    else
        return 0;
}

struct Match {
    int start;
    int matched;
    int pattern;
};

// if either the start or the end of one region is within the other
bool match_overlap(int s1, int e1, int s2, int e2)
{
    if (s1 >= s2 && s1 <= e2)
        return true;
    if (e1 >= s2 && e1 <= e2)
        return true;
    return false;
}

typedef std::list<Match> Matches;

AV* pattern_find_matches(Matcher* m, const char* filename)
{
    AV* ret = newAV();

    FILE* input = fopen(filename, "r");
    if (!input) {
        std::cerr << "Failed to open " << filename << std::endl;
        return ret;
    }
    char line[1000];
    int linenumber = 1;
    TokenList ts;
    while (fgets(line, sizeof(line) - 1, input)) {
        tokenize(ts, line, linenumber++);
    }
    fclose(input);

    Matches ms;
    for (unsigned int i = 0; i < ts.size(); i++) {
        PatternHash::const_iterator it = m->patterns.find(ts[i].hash);
        //std::cerr << ts[i].text << " " << (it == m->patterns.end()) << std::endl;
        if (it == m->patterns.end())
            continue;
        PatternList::const_iterator it2 = it->second.begin();
        //printf("T %s:%d:%lx %d %s\n", filename, ts[i].linenumber, ts[i].hash, it->second.size(), ts[i].text.c_str());
        for (; it2 != it->second.end(); ++it2) {
            int matched = match_pattern(ts, i, *it2);
            if (matched) {
                Match m;
                m.start = i;
                m.matched = matched;
                m.pattern = it2->id;
#if DEBUG
                fprintf(stderr, "L %s:%d(%d)-%d(%d) id:%d\n", filename, ts[i].linenumber, i, ts[i+matched-1].linenumber, m.matched, it2->id);
#endif
                ms.push_back(m);
            }
        }
    }
    Matches bests;
    while (ms.size()) {
        Matches::const_iterator it = ms.begin();
        Match best = *(it++);
        for (; it != ms.end(); ++it) {
            // the bigger IDs win on same matches - we expect newer patterns to be used
            if (best.matched < it->matched || (best.matched == it->matched && best.pattern < it->pattern)) {
                best = *it;
            }
        }
#if DEBUG
        std::cerr << filename  << "(" << best.pattern  << ") " << best.start << ":" << best.matched << std::endl;
#endif
        bests.push_back(best);
        for (Matches::iterator it2 = ms.begin(); it2 != ms.end();) {
	  if (match_overlap(it2->start, it2->start + it2->matched - 1, best.start, best.start + best.matched - 1)) {
#if DEBUG
	        std::cerr << filename  << "( erase " << it2->pattern  << ") " << it2->start << ":" << it2->matched << std::endl;
#endif
                it2 = ms.erase(it2);
	  } else
                it2++;
        }
    }

    int index = 0;
    for (Matches::const_iterator it = bests.begin(); it != bests.end(); ++it, ++index) {
        AV* line = newAV();
        av_push(line, newSVuv(it->pattern));
        av_push(line, newSVuv(ts[it->start].linenumber));
        av_push(line, newSVuv(ts[it->start + it->matched - 1].linenumber));
        av_push(ret, newRV_noinc((SV*)line));
    }
    return ret;
}

AV* pattern_read_lines(const char* filename, HV* needed_lines)
{
    AV* ret = newAV();

    FILE* input = fopen(filename, "r");
    if (!input) {
        std::cerr << "Failed to open " << filename << std::endl;
        return ret;
    }
    // really long file :)
    char buffer[200];
    char line[1000];
    int linenumber = 1;
    TokenList ts;
    while (fgets(line, sizeof(line) - 1, input)) {
        sprintf(buffer, "%d", linenumber);
        SV* val = hv_delete(needed_lines, buffer, strlen(buffer), 0);
        if (val) {
            // fgets makes sure we have a 0 at the end
            size_t len = strlen(line);
            if (len) {
                // remove one char (most likely newline)
                line[--len] = 0;
            }
            AV* row = newAV();
            av_push(row, newSVuv(linenumber));
            // better create a new one - I'm scared of mortals
            av_push(row, newSVuv(SvUV(val)));
            SV *str = newSVpv(line, len);
            // blindly assume that
            SvUTF8_on(str);
            av_push(row, str);
            av_push(ret, newRV_noinc((SV*)row));
        }
        if (av_len((AV*)needed_lines) == 0)
            break;
        ++linenumber;
    }
    fclose(input);
    return ret;
}
