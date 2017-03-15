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

#include <string>
#include <vector>
#include "EXTERN.h"
#include "perl.h"

// map string into token array
AV *pattern_parse(const char *str);
struct Matcher;
class SpookyHash;
Matcher *pattern_init_matcher();
void pattern_add(Matcher *m, unsigned id, AV *tokens);
AV *pattern_find_matches(Matcher *m, const char *filename);
void destroy_matcher(Matcher *m);
AV *pattern_read_lines(const char *filename, HV *needed);
SpookyHash *pattern_init_hash(UV seed1, UV seed2);
void pattern_add_to_hash(SpookyHash *s, SV *sv);
void destroy_hash(SpookyHash *s);
AV *pattern_hash128(SpookyHash *s);
