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

#include <EXTERN.h>
#include <perl.h>
#include <string>
#include <vector>

// map string into token array
AV* pattern_parse(const char* str);
AV* pattern_normalize(const char* str);
int pattern_distance(AV* a1, AV* a2);
AV* pattern_read_lines(const char* filename, HV* needed);

struct Matcher;
Matcher* pattern_init_matcher();
void pattern_add(Matcher* m, unsigned id, AV* tokens);
AV* pattern_find_matches(Matcher* m, const char* filename);
void pattern_dump(Matcher* m, const char* filename);
void pattern_load(Matcher* m, const char* filename);
void destroy_matcher(Matcher* m);

class SpookyHash;
SpookyHash* pattern_init_hash(UV seed1, UV seed2);
void pattern_add_to_hash(SpookyHash* s, SV* sv);
void destroy_hash(SpookyHash* s);
AV* pattern_hash128(SpookyHash* s);

class BagOfPatterns;
BagOfPatterns* pattern_init_bag_of_patterns();
void destroy_bag_of_patterns(BagOfPatterns *b);
void pattern_bag_set_patterns(BagOfPatterns *b, HV *patterns);
AV *pattern_bag_best_for(BagOfPatterns *b, const char *str, int count);
void pattern_bag_dump(BagOfPatterns* b, const char* filename);
bool pattern_bag_load(BagOfPatterns* b, const char* filename);
