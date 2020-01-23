#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "patterns_impl.h"

typedef Matcher *Spooky__Patterns__XS__Matcher;
typedef SpookyHash *Spooky__Patterns__XS__Hash;
typedef BagOfPatterns *Spooky__Patterns__XS__BagOfPatterns;

MODULE = Spooky::Patterns::XS  PACKAGE = Spooky::Patterns::XS

PROTOTYPES: ENABLE

Spooky::Patterns::XS::Hash init_hash(UV seed1, UV seed2)
  CODE:
    RETVAL = pattern_init_hash(seed1, seed2);

  OUTPUT:
    RETVAL

# pass a hash of integer index to string here
Spooky::Patterns::XS::BagOfPatterns init_patterns(HV *patterns)
  CODE:
    RETVAL = pattern_init_bag_of_patterns(patterns);

  OUTPUT:
    RETVAL

AV *parse_tokens(const char *str)
  CODE:
    RETVAL = pattern_parse(str);

  OUTPUT:
    RETVAL

AV *normalize(const char *str)
  CODE:
    RETVAL = pattern_normalize(str);

  OUTPUT:
    RETVAL

int distance(AV *a1, AV *a2)
  CODE:
    RETVAL = pattern_distance(a1, a2);

  OUTPUT:
    RETVAL

Spooky::Patterns::XS::Matcher init_matcher()
  CODE:
   RETVAL = pattern_init_matcher();

  OUTPUT:
    RETVAL

AV *read_lines(const char *filename, HV *needed)
  CODE:
    RETVAL = pattern_read_lines(filename, needed);

  OUTPUT:
    RETVAL

MODULE = Spooky::Patterns::XS  PACKAGE = Spooky::Patterns::XS::Matcher PREFIX = Matcher

void add_pattern(Spooky::Patterns::XS::Matcher self, unsigned int id, AV *tokens)
  CODE:
    pattern_add(self, id, tokens);

AV *find_matches(Spooky::Patterns::XS::Matcher self, const char *filename)
  CODE:
    RETVAL = pattern_find_matches(self, filename);

  OUTPUT:
    RETVAL

void dump(Spooky::Patterns::XS::Matcher self, const char *filename)
  CODE:
    pattern_dump(self, filename);

void load(Spooky::Patterns::XS::Matcher self, const char *filename)
  CODE:
    pattern_load(self, filename);

void DESTROY(Spooky::Patterns::XS::Matcher self)
  CODE:
   destroy_matcher(self);

MODULE = Spooky::Patterns::XS  PACKAGE = Spooky::Patterns::XS::Hash PREFIX = Hash

void DESTROY(Spooky::Patterns::XS::Hash self)
  CODE:
    destroy_hash(self);

void add(Spooky::Patterns::XS::Hash self, SV *s)
  CODE:
    pattern_add_to_hash(self, s);

AV *hash128(Spooky::Patterns::XS::Hash self)
  CODE:
    RETVAL = pattern_hash128(self);

  OUTPUT:
    RETVAL

MODULE = Spooky::Patterns::XS PACKAGE = Spooky::Patterns::XS::BagOfPatterns PREFIX = BagOfPatterns

void DESTROY(Spooky::Patterns::XS::BagOfPatterns self)
  CODE:
    destroy_bag_of_patterns(self);

AV *best_for(Spooky::Patterns::XS::BagOfPatterns self, const char *str, int count)
  CODE:
    RETVAL = pattern_bag_best_for(self, str, count);

  OUTPUT:
    RETVAL
