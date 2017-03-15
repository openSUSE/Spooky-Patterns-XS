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

#include "patterns_impl.h"
#include "EXTERN.h"
#include "SpookyV2.h"
#include "TokenTree.h"
#include "XSUB.h"
#include "perl.h"
#include <cstring>
#include <iostream>
#include <list>
#include <map>

#define DEBUG 0

struct Token {
    int linenumber;
    uint64_t hash;
#if DEBUG
    std::string text;
#endif
};

typedef std::vector<Token> TokenList;

const int MAX_TOKEN_LENGTH = 100;

struct Pattern {
    TokenList tokens;
    int id;
};

typedef std::vector<Pattern> PatternList;
typedef std::map<uint64_t, PatternList> PatternHash;

struct Matcher {
    TokenTree ignore_tree;
    PatternHash patterns;

    bool to_ignore(uint64_t t)
    {
        return ignore_tree.find(t) != 0;
    }

    Matcher()
    {
        int index = 0;
        // typical comment and markup - have to be single tokens!
        static const char* ignored_tokens[] = { "/*", "*/", "//", "%", "%%", "dnl"
                                                                             "//**",
            "/**", "**", "#~", ";;", "\"\"",
            "--", "#:", "\\", ">", "==", "::",
            "##", 0 };

        while (ignored_tokens[index]) {
            int len = strlen(ignored_tokens[index]);
            uint64_t h = SpookyHash::Hash64(ignored_tokens[index], len, 1);
            ignore_tree.insert(h);
            index++;
        }
    }
};

Matcher* pattern_init_matcher()
{
    return new Matcher;
}

void destroy_matcher(Matcher* m)
{
    delete m;
}

static void add_token(Matcher* m, TokenList& result, const char* start, size_t len, int line)
{
    if (!len)
        return;

    Token t;
#if DEBUG
    t.text = std::string(start, len);
#endif
    t.linenumber = line;
    t.hash = 0;
    if (!line && len > 5 && len < 9 && !strncmp(start, "$skip", 5)) {
        char number[10];
        strncpy(number, start + 5, len - 5);
        number[len - 5] = 0;
        char* endptr;
        t.hash = strtol(number, &endptr, 10);
        if (*endptr || t.hash > 20) // more than just a number
            t.hash = 0;
    }
    if (!t.hash) {
        // hash64 has no collisions on our patterns and is very fast
        // *and* 0-20 are "free"
        t.hash = SpookyHash::Hash64(start, len, 1);
        assert(t.hash > 20);
        if (m->to_ignore(t.hash))
            return;
    }
    result.push_back(t);
}

void tokenize(Matcher* m, TokenList& result, char* str, int linenumber = 0)
{
    static const char* ignore_seps = " \r\n\t*;,:!#{}|";
    static const char* single_seps = "-.+?\"\'=";

    const char* start = str;

    for (; *str; ++str) {
        // snipe out escape sequences
        if (*str < ' ')
            *str = ' ';
        *str = tolower(*str);
        bool ignored = (strchr(ignore_seps, *str) != NULL);
        if (ignored || strchr(single_seps, *str)) {
            add_token(m, result, start, str - start, linenumber);
            //fprintf(stderr, "TO %d:'%s'\n", ignored, str);
            if (!ignored)
                add_token(m, result, str, 1, linenumber);
            start = str + 1;
        }
    }
    add_token(m, result, start, str - start, linenumber);
}

AV* pattern_parse(const char* str)
{
    Matcher m;

    TokenList t;
    char* copy = strdup(str);
    tokenize(&m, t, copy);
    free(copy);
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
        tokenize(m, ts, line, linenumber++);
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
                fprintf(stderr, "L %s:%d(%d)-%d(%d) id:%d\n", filename, ts[i].linenumber, i, ts[i + matched - 1].linenumber, m.matched, it2->id);
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
        std::cerr << filename << "(" << best.pattern << ") " << best.start << ":" << best.matched << std::endl;
#endif
        bests.push_back(best);
        for (Matches::iterator it2 = ms.begin(); it2 != ms.end();) {
            if (match_overlap(it2->start, it2->start + it2->matched - 1, best.start, best.start + best.matched - 1)) {
#if DEBUG
                std::cerr << filename << "( erase " << it2->pattern << ") " << it2->start << ":" << it2->matched << std::endl;
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
            SV* str = newSVpv(line, len);
            if (is_utf8_string((U8*)line, len))
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

SpookyHash* pattern_init_hash(UV seed1, UV seed2)
{
    SpookyHash* s = new SpookyHash;
    s->Init(seed1, seed2);
    return s;
}

void pattern_add_to_hash(SpookyHash* s, SV* sv)
{
    size_t len;
    char* data = SvPV(sv, len);
    s->Update(data, len);
}

AV* pattern_hash128(SpookyHash* s)
{
    uint64 h1, h2;
    s->Final(&h1, &h2);
    AV* ret = newAV();
    av_push(ret, newSVuv(h1));
    av_push(ret, newSVuv(h2));
    return ret;
}

void destroy_hash(SpookyHash* s)
{
    delete s;
}
