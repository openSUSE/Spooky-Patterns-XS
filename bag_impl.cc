// Copyright © 2020 SUSE LLC
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

#include "Matcher.h"
#include "patterns_impl.h"
#include <EXTERN.h>
#include <XSUB.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <perl.h>
#include <set>

#define DEBUG 0

using namespace std;

typedef map<uint64_t, uint64_t> wordmap;

// https://en.wikipedia.org/wiki/Tf%E2%80%93idf
struct TfIdf {
    uint64_t hash;
    double value;
    TfIdf(uint64_t _hash, double _value)
    {
        hash = _hash;
        value = _value;
    }
    bool operator<(const TfIdf& str) const
    {
        return (hash < str.hash);
    }
};

struct Pattern {
    uint64_t index;
    double square_sum;
    vector<TfIdf> tf_idfs;
};

class BagOfPatterns {
public:
    BagOfPatterns() {}
    void set_patterns(HV* patterns);
    AV* best_for(const string& snippet, unsigned int count);
    void dump(const char* filename) const;
    bool load(const char* filename);

private:
    void tokenize(const char* str, wordmap& localwords);
    double compare2(const vector<TfIdf>& tfdif1, const Pattern& pattern) const;
    double tf_idf(const wordmap& l1, vector<TfIdf>& tfdif1);

    map<uint64_t, double> idfs;
    vector<Pattern> patterns;
};

BagOfPatterns* pattern_init_bag_of_patterns()
{
    return new BagOfPatterns();
}

void pattern_bag_set_patterns(BagOfPatterns* b, HV* patterns)
{
    b->set_patterns(patterns);
}

void destroy_bag_of_patterns(BagOfPatterns* b)
{
    delete b;
}

AV* pattern_bag_best_for(BagOfPatterns* b, const char* str, int count)
{
    return b->best_for(str, count);
}

void pattern_bag_dump(BagOfPatterns* b, const char* filename)
{
    b->dump(filename);
}

void pattern_bag_load(BagOfPatterns* b, const char* filename)
{
    b->load(filename);
}

void BagOfPatterns::set_patterns(HV* hv_patterns)
{
    idfs.clear();
    patterns.clear();

    wordmap words;
    vector<wordmap> wordcounts;
    vector<uint64_t> indexes;

    hv_iterinit(hv_patterns);
    HE* he;
    while ((he = hv_iternext(hv_patterns)) != 0) {
        I32 len;
        char* key = hv_iterkey(he, &len);
        unsigned int index = strtoul(key, 0, 10);

        SV* svp = hv_iterval(hv_patterns, he);
        if (!svp)
            continue;

        wordmap localwords;
        tokenize(SvPV_nolen(svp), localwords);
        indexes.push_back(index);
        wordcounts.push_back(localwords);

        for (wordmap::const_iterator it = localwords.begin(); it != localwords.end(); ++it) {
            wordmap::iterator word_it = words.find(it->first);
            if (word_it == words.end())
                words[it->first] = 1;
            else
                word_it->second++;
        }
    }
    for (wordmap::const_iterator it = words.begin(); it != words.end(); ++it) {
        idfs[it->first] = log(double(indexes.size()) / it->second);
    }

    vector<uint64_t>::const_iterator index_it = indexes.begin();
    vector<wordmap>::const_iterator words_it = wordcounts.begin();
    for (; words_it != wordcounts.end(); ++words_it, ++index_it) {
        Pattern p;
        p.index = *index_it;
        p.square_sum = tf_idf(*words_it, p.tf_idfs);
        patterns.push_back(p);
    }
}

void BagOfPatterns::tokenize(const char* str, wordmap& localwords)
{
    char* copy = strdup(str);
    TokenList t;
    Matcher::self()->tokenize(t, copy, 1);
    free(copy);

    // avoid '=======' dominating matches
    uint64_t last_hash = 0;
    for (TokenList::const_iterator it = t.begin(); it != t.end(); ++it) {
        if (it->hash == last_hash)
            continue;
        last_hash = it->hash;
        wordmap::iterator word_it = localwords.find(it->hash);
        if (word_it == localwords.end()) {
            localwords[it->hash] = 1;
        } else {
            word_it->second++;
        }
    }
}

double BagOfPatterns::tf_idf(const wordmap& words, vector<TfIdf>& tf_idfs)
{
    double square_sum = 0;
    for (wordmap::const_iterator it = words.begin(); it != words.end(); ++it) {
        double value = it->second * idfs[it->first];
        square_sum += value * value;
        tf_idfs.emplace_back(it->first, value);
    }
    sort(tf_idfs.begin(), tf_idfs.end());
    return sqrt(square_sum);
}

double BagOfPatterns::compare2(const vector<TfIdf>& tf_idfs1, const Pattern& pattern) const
{
    // both vectors are assumed to be sorted by hash
    double sum = 0;
    vector<TfIdf>::const_iterator it1 = pattern.tf_idfs.begin();
    vector<TfIdf>::const_iterator it2 = tf_idfs1.begin();
    while (it1 != pattern.tf_idfs.end() && it2 != tf_idfs1.end()) {
        if (it1->hash == it2->hash) {
            sum += it1->value * it2->value;
            ++it1;
            ++it2;
        } else if (it1->hash > it2->hash) {
            ++it2;
        } else {
            ++it1;
        }
    }
    return sum / pattern.square_sum;
}

AV* BagOfPatterns::best_for(const string& snippet, unsigned int count)
{
    AV* result = newAV();

    wordmap localwords;
    tokenize(snippet.c_str(), localwords);

    double highscore = -1;

    vector<TfIdf> tfidf;
    double square_sum = tf_idf(localwords, tfidf);

    struct BagHit {
        BagHit() {} // not used
        BagHit(double _match, uint64_t _index)
        {
            match = _match;
            index = _index;
        }
        bool operator<(const BagHit& rhs)
        {
            return match < rhs.match;
        }
        double match;
        uint64_t index;
    };
    vector<BagHit> hits;
    vector<Pattern>::const_iterator it = patterns.begin();
    for (; it != patterns.end(); ++it) {
        double match = compare2(tfidf, *it);
        if (match > highscore) {
            hits.emplace_back(match, it->index);
            sort(hits.rbegin(), hits.rend());
            if (hits.size() > count) {
                hits.resize(count);
                highscore = hits.back().match;
            }
        }
    }
    for (const auto& i : hits) {
        HV* hv = (HV*)sv_2mortal((SV*)newHV());
        hv_store(hv, "pattern", 7, newSVuv(i.index), 0);
        hv_store(hv, "match", 5, newSVnv(int(i.match * 10000 / square_sum) / 10000.), 0);
        av_push(result, newRV_inc((SV*)hv));
    }

    return result;
}

void BagOfPatterns::dump(const char* filename) const
{
    FILE* file = fopen(filename, "wb");

    uint64_t count = idfs.size();
    fwrite(&count, sizeof(count), 1, file);

    map<uint64_t, double>::const_iterator it = idfs.begin();
    for (; it != idfs.end(); ++it) {
        uint64_t f1 = it->first;
        double f2 = it->second;
        fwrite(&f1, sizeof(f1), 1, file);
        fwrite(&f2, sizeof(f2), 1, file);
    }

    count = patterns.size();
    fwrite(&count, sizeof(count), 1, file);

    for (const auto& i : patterns) {
        uint64_t f1 = i.index;
        fwrite(&f1, sizeof(f1), 1, file);
        double f2 = i.square_sum;
        fwrite(&f2, sizeof(f2), 1, file);

        count = i.tf_idfs.size();
        fwrite(&count, sizeof(count), 1, file);
        for (const auto& t : i.tf_idfs) {
            uint64_t f1 = t.hash;
            double f2 = t.value;
            fwrite(&f1, sizeof(f1), 1, file);
            fwrite(&f2, sizeof(f2), 1, file);
        }
    }
}

bool BagOfPatterns::load(const char* filename)
{
    FILE* file = fopen(filename, "rb");
    if (!file)
        return false;

    uint64_t count = 0;
    if (fread(&count, sizeof(count), 1, file) != 1) {
        fclose(file);
        return false;
    }

    idfs.clear();
    while (count--) {
        uint64_t f1 = 0;
        double f2 = 0;
        fread(&f1, sizeof(f1), 1, file);
        fread(&f2, sizeof(f2), 1, file);
        idfs[f1] = f2;
    }

    patterns.clear();
    count = 0;
    fread(&count, sizeof(count), 1, file);

    while (count--) {
        Pattern p;
        uint64_t f1 = 0;
        fread(&f1, sizeof(f1), 1, file);
        p.index = f1;
        double f2 = 0;
        fread(&f2, sizeof(f2), 1, file);
        p.square_sum = f2;

        uint64_t f3 = 0;
        fread(&f3, sizeof(f3), 1, file);

        while (f3--) {
            uint64_t f1;
            double f2;
            fread(&f1, sizeof(f1), 1, file);
            fread(&f2, sizeof(f2), 1, file);
            p.tf_idfs.emplace_back(f1, f2);
        }
        patterns.push_back(p);
    }

    return true;
}
