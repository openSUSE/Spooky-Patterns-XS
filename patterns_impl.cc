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

struct Token {
    int linenumber;
    uint64_t hash;
    std::string text;
};

typedef std::vector<Token> TokenList;

const int MAX_TOKEN_LENGTH = 100;
const int MAX_LINE_SIZE = 8000;

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
            "/", "//", "%", "%%", "dnl",
            "#~", ";;", "\"\"", "--", "#:",
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
        if (!index && it->hash <= MAX_SKIP)
            continue;
        av_store(ret, index, newSVuv(it->hash));
        ++index;
    }
    return ret;
}

TokenTree* check_or_insert_skip(TokenTree* current, unsigned char uv)
{
    SkipList::const_iterator last;

    if (current->skips) {
        SkipList& sl = *current->skips;
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
        current->skips = new SkipList;
        last = current->skips->before_begin();
    }
    return current->skips->insert_after(last, std::make_pair(uv, new TokenTree))->second;
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

        if (uv <= MAX_SKIP) {
            current = check_or_insert_skip(current, uv);
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

        if (patterns->skips) {
            for (SkipList::const_iterator it = patterns->skips->begin(); it != patterns->skips->end(); ++it) {
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

    m->pattern_tree.mark_elements(si);
    fwrite(&si.tree_count, sizeof(si.tree_count), 1, file);
    fwrite(&si.node_count, sizeof(si.tree_count), 1, file);
    fwrite(&si.element_count, sizeof(si.tree_count), 1, file);

    // elements are quick
    uint64_t* elements = new uint64_t[si.element_count];
    for (std::map<uint64_t, int>::const_iterator it = si.elements.begin(); it != si.elements.end(); it++) {
        elements[it->second] = it->first;
    }
    fwrite(elements, sizeof(uint64_t), si.element_count, file);
    delete[] elements;

    // trees reference nodes and are recursive
    const TokenTree** trees = new const TokenTree*[si.tree_count];
    memset(trees, 0, si.tree_count * sizeof(TokenTree*));
    for (std::map<const TokenTree*, int>::const_iterator it = si.trees.begin();
         it != si.trees.end(); it++) {
        trees[it->second] = it->first;
    }

    for (int i = 0; i < si.tree_count; i++) {
        const TokenTree* t = trees[i];
        fwrite(&t->pid, sizeof(t->pid), 1, file);
        char skip_count = 0;
        // forward_list has no length - but is cheap
        for (SkipList::const_iterator it = t->skips->begin(); it != t->skips->end(); ++it)
            skip_count++;
        fwrite(&skip_count, 1, 1, file);
        for (SkipList::const_iterator it = t->skips->begin(); it != t->skips->end(); ++it) {
            fwrite(&it->first, 1, 1, file);
            int32_t index = si.trees[it->second];
            fwrite(&index, sizeof(int32_t), 1, file);
        }
        int32_t index = si.nodes[t->root];
        fwrite(&index, sizeof(int32_t), 1, file);
    }
    delete[] trees;

    // trees reference nodes and are recursive
    const AANode** nodes = new const AANode*[si.node_count];
    memset(nodes, 0, si.node_count * sizeof(AANode*));
    for (std::map<const AANode*, int>::const_iterator it = si.nodes.begin();
         it != si.nodes.end(); it++) {
        nodes[it->second] = it->first;
    }
    for (int i = 0; i < si.node_count; i++) {
        const AANode* n = nodes[i];
        int32_t index = si.nodes[n->left];
        fwrite(&index, sizeof(int32_t), 1, file);
        index = si.nodes[n->right];
        fwrite(&index, sizeof(int32_t), 1, file);
        fwrite(&n->level, sizeof(n->level), 1, file);
        index = si.trees[n->next_token];
        fwrite(&index, sizeof(int32_t), 1, file);
    }

    delete[] nodes;

    std::cout << "count " << si.element_count << " " << si.node_count << std::endl;
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

    si.tree_count = *reinterpret_cast<int32_t*>(dump);
    dump += sizeof(int32_t);
    si.node_count = *reinterpret_cast<int32_t*>(dump);
    dump += sizeof(int32_t);
    si.element_count = *reinterpret_cast<int32_t*>(dump);
    dump += sizeof(int32_t);

    uint64_t* elements = reinterpret_cast<uint64_t*>(dump);
    dump += sizeof(uint64_t) * si.element_count;

#if 1
    TokenTree** trees = new TokenTree*[si.tree_count];
    for (int i = 0; i < si.tree_count; i++)
        trees[i] = new TokenTree(m->pattern_tree.nullNode);
#endif

    std::cout << "count " << si.element_count << " " << si.node_count << " " << si.tree_count << std::endl;
#if 0
  for (int i = 0; i < si.tree_count; i++) {
    TokenTree *t = trees[i];
    fread(&t->pid, sizeof(t->pid), 1, file);
    //std::cout << "PID " << t->pid << std::endl;
    char skip_count = 0;
    fread(&skip_count, 1, 1, file);
    //std::cout << "SC " << int(skip_count) << std::endl;
    SkipList::const_iterator last = t->skips->before_begin();
    for (int s = 0; s < skip_count; s++) {
      unsigned char skip;
      fread(&skip, 1, 1, file);
      int32_t index;
      fread(&index, sizeof(index), 1, file);
      //std::cout << "Index " << index << std::endl;
      last = t->skips->emplace_after(last, skip, trees[index]);
    }
    int32_t index;
    fread(&index, sizeof(index), 1, file);
    //std::cout << "Index2 " << index << std::endl;
    t->root = reinterpret_cast<AANode*>(index);
  }

  AANode **nodes = new AANode*[si.node_count];
  
  // trees reference nodes and are recursive
  for (int i = 0; i < si.node_count; i++) {
    int32_t e, left, right, nt;
    uint16_t level;
    e = 0;
    fread(&left, sizeof(left), 1, file);
    fread(&right, sizeof(right), 1, file);
    fread(&level, sizeof(level), 1, file);
    fread(&nt, sizeof(nt), 1, file);
    nodes[i] = new AANode(elements[e], trees[nt], nodes[left], nodes[right], level);
  }

  for (int i = 0; i < si.tree_count; i++) {
    TokenTree *t = trees[i];
    t->root = nodes[reinterpret_cast<long>(t->root)];
  }

  delete [] nodes;
  delete [] trees;
#endif
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
