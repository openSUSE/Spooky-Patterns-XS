
#! /usr/bin/perl

use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;

my $m = Spooky::Patterns::XS::init_matcher();

for my $fn ( glob("t/04license.*.pattern") ) {
    $fn =~ m/\.(.*)\.pattern/;
    my $num = $1;
    open( my $fh, '<', $fn );
    my $str = join( '', <$fh> );
    close($fh);

    $m->add_pattern( $num, Spooky::Patterns::XS::parse_tokens($str) );
    ok( 1, "Loaded $fn" );
}

my %exp = (
    1 => [ [ 1,  1,  165 ] ],
    2 => [ [ 2,  2,  23 ] ],
    3 => [ [ 7,  6,  7 ], [ 4, 10, 10 ] ],
    4 => [ [ 11, 35, 60 ], [ 12, 6, 18 ], [ 4, 2, 2 ], [ 4, 27, 27 ] ],
    5 => [ [ 13, 1,  502 ] ],
    6 => [ [ 15, 5, 17 ], [ 4, 2, 2 ] ],
    7 => [ [ 16, 1, 1 ] ],
    8 => [ [ 19, 7, 19 ], [ 18, 5, 5 ], [ 4, 3, 3 ] ]
);

for my $fn ( glob("t/04license.*.txt") ) {
    $fn =~ m/\.(.*)\.txt/;
    my $num = $1;

    my $best = $m->find_matches($fn);
    #use Data::Dumper;
    #print STDERR Dumper($best);
    cmp_deeply( $exp{$num}, $best, "Structure for $num fits" );
}

done_testing();
