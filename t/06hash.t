#! /usr/bin/perl

use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;
use utf8;

my $h = Spooky::Patterns::XS::init_hash(0, 0);
$h->add("Hállöchen\n");
$h->add("abc\x{300}");
is($h->hex, 'd6d58320114a2d3c1d6dd671ab0383ec', "Hhex correct");
is($h->hash64, 15480423467908214076, "Hash64 correct");

done_testing();
