#! /usr/bin/perl

use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;
use utf8;

my $h = Spooky::Patterns::XS::init_hash( 0, 0 );
$h->add("Hállöchen\n");
$h->add("abc\x{300}");
is( $h->hex, 'd6d58320114a2d3c1d6dd671ab0383ec', "Hhex correct" );
is( $h->hash64, 15480423467908214076, "Hash64 correct" );

$h = Spooky::Patterns::XS::init_hash( 0, 0 );
$h->add("/* \n");
$h->add(
"** Invoke the authorization callback for permission to read column zCol from \n"
);
$h->add(
"** table zTab in database zDb. This function assumes that an authorization\n"
);
is( $h->hex, '06a8354a1a3a0934b5b4c88496411563', "Hhex correct" );
done_testing();
