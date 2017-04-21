#! /usr/bin/perl

use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;
use File::Slurp;
use Data::Dumper;
use Algorithm::Diff 'sdiff';

my @ps = glob('/tmp/pattern-*');

my $p1 = Spooky::Patterns::XS::normalize( read_file('t/07close.p1') );
my $p2 = Spooky::Patterns::XS::normalize( read_file('t/07close.p2') );
is( Spooky::Patterns::XS::distance( $p1, $p2 ), 2, "Distance is 2" );

my @words1 = map { $_->[1] } @$p1;
my @words2 = map { $_->[1] } @$p2;
my @diff = sdiff( \@words1, \@words2 );
my $seen_cs = 0;
for my $row (@diff) {
    if ( $row->[0] eq 'c' ) {
        is_deeply( $row, [ 'c', 'holder(s)', 'holders' ], "The one change" );
        $seen_cs++;
    }
    else {
        is( $row->[0], 'u' );
    }
}
is( $seen_cs, 2, "Seen two changes" );

done_testing();
