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
#include "SpookyV2.h"
#include "TokenTree.h"
#include <EXTERN.h>
#include <XSUB.h>
#include <cstring>
#include <iostream>
#include <list>
#include <map>
#include <perl.h>
#include <sys/mman.h>

#define DEBUG 0
#define MAX_SKIP 99

using namespace std;

struct Token {
    int linenumber;
    uint64_t hash;
    std::string text;
};

typedef std::vector<Token> TokenList;

std::vector<AANode> TokenTree::nodes;
std::vector<TokenTree> TokenTree::trees;

const int MAX_TOKEN_LENGTH = 100;
const int MAX_LINE_SIZE = 8000;

struct Matcher {
    TokenTreeIndex ignore_tree;
    TokenTreeIndex pattern_tree;

    SSize_t longest_pattern;

    static Matcher* _self;
    static Matcher* self() { return _self; }

    bool to_ignore(uint64_t t)
    {
        return TokenTree::trees[ignore_tree].find(t);
    }

    Matcher()
    {
        if (_self) {
            fprintf(stderr, "Matcher::self already initialized\n");
        }
        init();
    }

    void init()
    {
        TokenTree::nodes.clear();
        TokenTree::trees.clear();
        TokenTree::trees.emplace_back();
        ignore_tree = 0;
        TokenTree::trees.emplace_back();
        pattern_tree = 1;

        // typical comment and markup - have to be single tokens!
        static const char* ignored_tokens[] = {
            "/", "//", "%", "%%", "dnl",
            "#~", ";;", "\"\"", "--", "#:",
            "\\", ">", "==", "::", "##", 0
        };

        TokenTree& it = TokenTree::trees[0];

        int index = 0;
        while (ignored_tokens[index]) {
            int len = strlen(ignored_tokens[index]);
            uint64_t h = SpookyHash::Hash64(ignored_tokens[index], len, 1);
            TokenTreeIndex dummy = 42; // != 0
            it.insert(h, dummy);
            index++;
        }
        longest_pattern = 0;
    }
};

Matcher* Matcher::_self = 0;

Matcher* pattern_init_matcher()
{
    if (!Matcher::_self)
        Matcher::_self = new Matcher;

    Matcher::_self->init();

    return Matcher::_self;
}

void destroy_matcher(Matcher* m)
{
    // do nothing, we reuse the self
}

static void add_token(Matcher* m, TokenList& result, const char* start, size_t len, int line)
{
    if (!len)
        return;

    Token t;
    t.text = std::string(start, len);
    t.linenumber = line;
    t.hash = 0;
    if (!line && len > 5 && len < 9 && !strncmp(start, "$skip", 5)) {
        char number[10];
        strncpy(number, start + 5, len - 5);
        number[len - 5] = 0;
        char* endptr;
        t.hash = strtol(number, &endptr, 10);
        if (*endptr || t.hash > MAX_SKIP) // more than just a number
            t.hash = 0;
    }
    if (!t.hash) {
        // hash64 has no collisions on our patterns and is very fast
        // *and* 0-3000 (at least) are "free"
        t.hash = SpookyHash::Hash64(start, len, 1);
        assert(t.hash > MAX_SKIP);
        if (m->to_ignore(t.hash))
            return;
    }
    result.push_back(t);
}

void tokenize(Matcher* m, TokenList& result, char* str, int linenumber = 0)
{
    static const char* ignore_seps = " \r\n\t*;,:!#{}()[]|";
    static const char* single_seps = "-.+?\"\'`=<>";

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
    TokenList t;
    char* copy = strdup(str);
    Matcher* m = Matcher::self();
    AV* ret = newAV();
    if (!m) {
        fprintf(stderr, "Need a Matcher - call init_matcher\n");
        return ret;
    }
    tokenize(m, t, copy);
    free(copy);
    av_extend(ret, t.size());
    int index = 0;
    for (TokenList::const_iterator it = t.begin(); it != t.end(); ++it) {

        // do not start with an expansion variable
        if (!index && it->hash <= MAX_SKIP)
            continue;
        av_store(ret, index, newSVuv(it->hash));
        ++index;
    }
    return ret;
}

TokenTreeIndex check_or_insert_skip(TokenTree& current, unsigned char uv)
{
    SkipList::const_iterator last;

    if (current.skips) {
        SkipList& sl = *current.skips;
        last = sl.before_begin();

        SkipList::const_iterator sli = sl.begin();
        for (; sli != sl.end(); ++sli) {
            if (sli->first == uv)
                return sli->second;
            if (sli->first > uv)
                break;
            last = sli;
        }
    } else {
        current.skips = new SkipList;
        last = current.skips->before_begin();
    }
    TokenTree::trees.emplace_back();
    int next = TokenTree::trees.size() - 1;
    current.skips->insert_after(last, std::make_pair(uv, next));
    return next;
}

void pattern_add(Matcher* m, unsigned int id, av* tokens)
{
    SSize_t len = av_top_index(tokens) + 1;
    if (!len) {
        std::cerr << "add failed for id " << id << std::endl;
        return;
    }

    TokenTreeIndex current = m->pattern_tree;

    for (SSize_t i = 0; i < len; ++i) {
        SV* sv = *av_fetch(tokens, i, 0);
        UV uv = SvUV(sv);

        if (uv <= MAX_SKIP) {
            current = check_or_insert_skip(TokenTree::trees[current], uv);
        } else {
            TokenTreeIndex next = TokenTree::trees[current].find(uv);
            if (next < 0) {
                TokenTree::trees.emplace_back();
                next = TokenTree::trees.size() - 1;
                TokenTree::trees[current].insert(uv, next);
            }
            current = next;
        }
    }
    if (TokenTree::trees[current].pid) {
        std::cerr << "Problem: ID " << id << " overwrites "
                  << TokenTree::trees[current].pid << std::endl;
    }
    TokenTree::trees[current].pid = id;
    if (len > m->longest_pattern)
        m->longest_pattern = len;
}

unsigned int check_token_matches(const TokenList& tokens,
    unsigned int offset, TokenTreeIndex patterns, int* pid)
{
    unsigned int last_match = 0;
    while (patterns) {
        TokenTree& pt = TokenTree::trees[patterns];

        if (offset >= tokens.size()) {
            // end of text, check if pattern ends too
            if (pt.pid && last_match < offset) {
                *pid = pt.pid;
                last_match = offset;
            }
            return last_match;
        }

#if DEBUG
        fprintf(stderr, "MP %d %d:%s\n", offset,
            pt.find(tokens[offset].hash) ? 1 : 0,
            tokens[offset].text.c_str());
#endif

        if (pt.skips) {
            for (SkipList::const_iterator it = pt.skips->begin(); it != pt.skips->end(); ++it) {
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
        }
        if (pt.pid && last_match < offset) {
            *pid = pt.pid;
            last_match = offset;
        }
        patterns = pt.find(tokens[offset].hash);
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
    TokenTreeIndex patterns = TokenTree::trees[m->pattern_tree].find(ts[tokenlist_index].hash);
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
    char line[MAX_LINE_SIZE];
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

void pattern_dump(Matcher* m, const char* filename)
{
    FILE* file = fopen(filename, "wb");
    fwrite(&m->longest_pattern, sizeof(m->longest_pattern), 1, file);

    SerializeInfo si;

    TokenTree::trees[m->pattern_tree].mark_elements(si);
    uint32_t count = TokenTree::trees.size();
    fwrite(&count, sizeof(count), 1, file);
    count = TokenTree::nodes.size();
    fwrite(&count, sizeof(count), 1, file);

#if 0
    fwrite(&si.element_count, sizeof(si.element_count), 1, file);

    // elements are quick
    uint64_t* elements = new uint64_t[si.element_count];
    for (std::map<uint64_t, int>::const_iterator it = si.elements.begin(); it != si.elements.end(); it++) {
        elements[it->second] = it->first;
    }
    fwrite(elements, sizeof(uint64_t), si.element_count, file);
    delete[] elements;
#endif

    vector<TokenTree>::const_iterator it2 = TokenTree::trees.begin();
    it2++; // skip nullTree
    for (; it2 != TokenTree::trees.end(); ++it2) {
        fwrite(&it2->pid, sizeof(uint32_t), 1, file);
        char skip_count = 0;
        if (it2->skips) {
            // forward_list has no length - but is cheap
            for (SkipList::const_iterator it = it2->skips->begin(); it != it2->skips->end(); ++it)
                skip_count++;
        }
        fwrite(&skip_count, 1, 1, file);
        if (it2->skips) {
            for (SkipList::const_iterator it = it2->skips->begin(); it != it2->skips->end(); ++it) {
                fwrite(&it->first, 1, 1, file);
                int32_t index = it->second;
                fwrite(&index, sizeof(int32_t), 1, file);
            }
        }
        int32_t index = it2->root;
        fwrite(&index, sizeof(int32_t), 1, file);
    }

    vector<AANode>::const_iterator it = TokenTree::nodes.begin();
    it++; // skip nullNode
    for (; it != TokenTree::nodes.end(); ++it) {
        fwrite(&it->element, sizeof(int64_t), 1, file);
        uint32_t index = it->left;
        fwrite(&index, sizeof(int32_t), 1, file);
        index = it->right;
        fwrite(&index, sizeof(int32_t), 1, file);
        fwrite(&it->level, sizeof(it->level), 1, file);
        index = it->next_token;
        fwrite(&index, sizeof(int32_t), 1, file);
    }

    uint32_t index = TokenTree::trees[m->pattern_tree].root;
    fwrite(&index, sizeof(uint32_t), 1, file);

    fclose(file);
}

void pattern_load(Matcher* m, const char* filename)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Couldn't open %s\n", filename);
        return;
    }
    struct stat attr;
    if (fstat(fd, &attr) == -1) {
        fprintf(stderr, "Error accessing %s\n", filename);
        return;
    }
    char* dump = (char*)mmap(NULL, attr.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);

    m->longest_pattern = *reinterpret_cast<SSize_t*>(dump);
    dump += sizeof(SSize_t);

    SerializeInfo si;

    uint32_t tree_count = *reinterpret_cast<uint32_t*>(dump);
    dump += sizeof(uint32_t);
    uint32_t node_count = *reinterpret_cast<uint32_t*>(dump);
    dump += sizeof(uint32_t);

#if 0
    si.element_count = *reinterpret_cast<uint32_t*>(dump);
    dump += sizeof(int32_t);

    uint64_t* elements = reinterpret_cast<uint64_t*>(dump);
    dump += sizeof(uint64_t) * si.element_count;
#endif

    struct timeval tv1, tv2;
    gettimeofday(&tv1, 0);

    TokenTree::trees.reserve(tree_count);

    gettimeofday(&tv2, 0);
    fprintf(stderr, "diff %ld\n", (tv2.tv_sec - tv1.tv_sec) * 1000 + (tv2.tv_usec - tv1.tv_usec) / 1000);
    for (unsigned int i = 1; i < tree_count; i++) {

        uint32_t pid = *reinterpret_cast<uint32_t*>(dump);
        dump += sizeof(uint32_t);
        uint32_t root = *reinterpret_cast<uint32_t*>(dump);
        dump += sizeof(uint32_t);

        TokenTree::trees.emplace_back(pid, root);

        unsigned char skip_count = *reinterpret_cast<unsigned char*>(dump++);
        if (skip_count) {
            TokenTree& t = TokenTree::trees[i];
            t.skips = new SkipList;
            SkipList::const_iterator last = t.skips->before_begin();
            for (int s = 0; s < skip_count; s++) {
                unsigned char skip = *reinterpret_cast<unsigned char*>(dump++);
                int32_t index = *reinterpret_cast<uint32_t*>(dump);
                dump += sizeof(uint32_t);
                last = t.skips->emplace_after(last, skip, index);
            }
        }
    }

    TokenTree::nodes.clear();
    TokenTree::nodes.reserve(node_count);
    TokenTree::trees[m->pattern_tree].initNull();

    for (unsigned int i = 1; i < node_count; i++) {
        uint64_t element = *reinterpret_cast<uint64_t*>(dump);
        dump += sizeof(uint64_t);
        uint32_t left = *reinterpret_cast<uint32_t*>(dump);
        dump += sizeof(uint32_t);
        uint32_t right = *reinterpret_cast<uint32_t*>(dump);
        dump += sizeof(uint32_t);
        uint16_t level = *reinterpret_cast<uint16_t*>(dump);
        dump += sizeof(uint16_t);
        uint32_t nt = *reinterpret_cast<uint32_t*>(dump);
        dump += sizeof(uint32_t);
        TokenTree::nodes.emplace_back(element, nt, left, right, level);
    }

    //delete [] trees;

    TokenTree::trees[m->pattern_tree].root = *reinterpret_cast<uint32_t*>(dump);
    dump += sizeof(uint32_t);

    munmap(dump, attr.st_size);
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
    char line[MAX_LINE_SIZE];
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

static int _LevenshteinDistance(AV* s, int len_s, AV* t, int len_t)
{
    // degenerate cases
    if (len_s == 0)
        return len_t;
    if (len_t == 0)
        return len_s;

    // create two work vectors of integer distances
    int64_t* v0 = new int64_t[len_t + 1];
    int64_t* v1 = new int64_t[len_t + 1];

    // initialize v0 (the previous row of distances)
    // this row is A[0][i]: edit distance for an empty s
    // the distance is just the number of characters to delete from t
    for (int i = 0; i < len_t + 1; i++)
        v0[i] = i;

    for (int i = 0; i < len_s; i++) {
        // calculate v1 (current row distances) from the previous row v0

        // first element of v1 is A[i+1][0]
        //   edit distance is delete (i+1) chars from s to match empty t
        v1[0] = i + 1;
        AV* av1 = (AV*)SvRV(*av_fetch(s, i, 0));
        SV* sv1 = *av_fetch((AV*)av1, 2, 0);
        //fprintf(stderr, "sv %ld\n", SvUV(sv1));

        // use formula to fill in the rest of the row
        for (int j = 0; j < len_t; j++) {
            AV* av2 = (AV*)SvRV(*av_fetch(t, j, 0));
            SV* sv2 = *av_fetch((AV*)av2, 2, 0);

            int cost = (SvUV(sv1) == SvUV(sv2)) ? 0 : 1;
            v1[j + 1] = std::min(std::min(v1[j] + 1, v0[j + 1] + 1), v0[j] + cost);
        }

        // copy v1 (current row) to v0 (previous row) for next iteration
        for (int j = 0; j < len_t + 1; j++)
            v0[j] = v1[j];
    }

    return v1[len_t];
}

int pattern_distance(AV* a1, AV* a2)
{
    return _LevenshteinDistance(a1, av_len(a1), a2, av_len(a2));
}

AV* pattern_normalize(const char* p)
{
    Matcher m;
    TokenList t;
    int line = 1;
    while (true) {
        const char* nl = strchr(p, '\n');
        char* copy;
        if (nl)
            copy = strndup(p, nl - p);
        else
            copy = strdup(p);
        tokenize(&m, t, copy, line++);
        free(copy);
        if (!nl)
            break;
        p = nl + 1;
    }

    AV* ret = newAV();
    for (TokenList::const_iterator it = t.begin(); it != t.end(); ++it) {
        AV* row = newAV();
        av_push(row, newSVuv(it->linenumber));
        SV* str = newSVpv(it->text.data(), it->text.length());
        av_push(row, str);
        av_push(row, newSVuv(it->hash));
        av_push(ret, newRV_noinc((SV*)row));
    }
    return ret;
}
