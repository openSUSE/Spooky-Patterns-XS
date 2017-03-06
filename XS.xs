#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "spooky_patterns.h"

typedef Matcher *Spooky__Patterns__XS__Matcher;

MODULE = Spooky::Patterns::XS  PACKAGE = Spooky::Patterns::XS

PROTOTYPES: ENABLE

AV *parse_tokens(const char *str)
  CODE:
    RETVAL = pattern_parse(str);

  OUTPUT:
    RETVAL

Spooky::Patterns::XS::Matcher init_matcher()
  CODE:
   RETVAL = pattern_init_matcher();

  OUTPUT:
    RETVAL

AV *read_lines(const char *filename, int from, int to)
  CODE:
    RETVAL = pattern_read_lines(filename, from, to);

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

void DESTROY(Spooky::Patterns::XS::Matcher self)
 CODE:
  destroy_matcher(self);

