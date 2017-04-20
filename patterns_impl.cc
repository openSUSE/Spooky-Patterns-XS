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
#include <cstring>
#include <iostream>
#include <list>
#include <map>
#include <perl.h>

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

struct Matcher {
    TokenTree ignore_tree;
    TokenTree pattern_tree;

    SSize_t longest_pattern;

    bool to_ignore(uint64_t t)
    {
        return ignore_tree.find(t);
    }

    Matcher()
    {
        // typical comment and markup - have to be single tokens!
        static const char* ignored_tokens[] = {
            "/*", "*/", "//", "%", "%%", "dnl", "//**",
            "/**", "**", "#~", ";;", "\"\"", "--", "#:",
            "\\", ">", "==", "::", "##", 0
        };

        static TokenTree dummy_next;

        int index = 0;
        while (ignored_tokens[index]) {
            int len = strlen(ignored_tokens[index]);
            uint64_t h = SpookyHash::Hash64(ignored_tokens[index], len, 1);
            ignore_tree.insert(h, &dummy_next);
            index++;
        }

        longest_pattern = 0;
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
    // for the ignore tree
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
        if (!index && it->hash <= 20)
            continue;
        av_store(ret, index, newSVuv(it->hash));
        ++index;
    }
    return ret;
}

TokenTree* check_or_insert_skip(SkipList& sl, unsigned char uv)
{
    SkipList::const_iterator last = sl.before_begin();

    if (!sl.empty()) {
        SkipList::const_iterator sli = sl.begin();
        for (; sli != sl.end(); ++sli) {
            if (sli->first == uv)
                return sli->second;
            if (sli->first > uv)
                break;
            last = sli;
        }
    }
    return sl.insert_after(last, std::make_pair(uv, new TokenTree))->second;
}

void pattern_add(Matcher* m, unsigned int id, av* tokens)
{
    SSize_t len = av_top_index(tokens) + 1;
    if (!len) {
        std::cerr << "add failed for id " << id << std::endl;
        return;
    }

    TokenTree* current = &m->pattern_tree;

    for (SSize_t i = 0; i < len; ++i) {
        SV* sv = *av_fetch(tokens, i, 0);
        UV uv = SvUV(sv);

        if (uv <= 20) {
            current = check_or_insert_skip(current->skips, uv);
        } else {
            TokenTree* next = current->find(uv);
            if (!next) {
                next = new TokenTree;
                current->insert(uv, next);
            }
            current = next;
        }
    }
    if (current->pid) {
        std::cerr << "Problem: ID " << id << " overwrites " << current->pid << std::endl;
    }
    current->pid = id;
    if (len > m->longest_pattern)
        m->longest_pattern = len;
}

unsigned int check_token_matches(const TokenList& tokens, unsigned int offset, const TokenTree* patterns, int* pid)
{
    unsigned int last_match = 0;
    while (patterns) {
        if (offset >= tokens.size()) {
            // end of text, check if pattern ends too
            if (patterns->pid && last_match < offset) {
                *pid = patterns->pid;
                last_match = offset;
            }
            return last_match;
        }

#if DEBUG
        fprintf(stderr, "MP %d %d:%s\n", offset,
            patterns->find(tokens[offset].hash) ? 1 : 0,
            tokens[offset].text.c_str());
#endif

        for (SkipList::const_iterator it = patterns->skips.begin(); it != patterns->skips.end(); ++it) {
            for (int i = 1; i <= it->first; ++i) {
                int cpid = 0;
                unsigned int matched = check_token_matches(tokens, offset + i, it->second, &cpid);
#if DEBUG
                fprintf(stderr, "MP2 %d SKIP %d:%d = %d %d\n", offset, it->first, i, matched, cpid);
#endif

                if (last_match < matched) {
                    last_match = matched;
                    *pid = cpid;
                }
            }
        }
        if (patterns->pid && last_match < offset) {
            *pid = patterns->pid;
            last_match = offset;
        }
        patterns = patterns->find(tokens[offset].hash);
        offset++;
    }
    return last_match;
}

struct Match {
    int start;
    int matched;
    int pattern;
    int sline;
    int eline;
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

void find_tokens(Matcher* m, TokenList& ts, Matches& ms, int tokenlist_offset, int tokenlist_index)
{
    TokenTree* patterns = m->pattern_tree.find(ts[tokenlist_index].hash);
    if (!patterns)
        return;
    int pid = 0;
    int matched = check_token_matches(ts, tokenlist_index + 1, patterns, &pid);
    if (pid) {
        Match m;
        m.start = tokenlist_offset + tokenlist_index;
        m.matched = matched - tokenlist_index;

        m.sline = ts[tokenlist_index].linenumber;
        m.eline = ts[matched - 1].linenumber;

        m.pattern = pid;
#if DEBUG
        fprintf(stderr, "L %d(%d)-%d(%d) id:%d\n", ts[tokenlist_index].linenumber,
            tokenlist_offset + tokenlist_index, ts[tokenlist_index + matched - 1].linenumber, m.matched, m.pattern);
#endif
        ms.push_back(m);
    }
}

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
    Matches ms;
    int token_offset = 0;
    while (fgets(line, sizeof(line) - 1, input)) {
        tokenize(m, ts, line, linenumber++);
        // preserve memory
        if (SSize_t(ts.size()) > m->longest_pattern * 100) {
            unsigned int erasing = ts.size() - m->longest_pattern - 1;
            for (unsigned int i = 0; i < erasing; i++)
                find_tokens(m, ts, ms, token_offset, i);
            ts.erase(ts.begin(), ts.begin() + erasing);
            token_offset += erasing;
        }
    }
    fclose(input);
    for (unsigned int i = 0; i < ts.size(); i++)
        find_tokens(m, ts, ms, token_offset, i);

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
        av_push(line, newSVuv(it->sline));
        av_push(line, newSVuv(it->eline));
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
