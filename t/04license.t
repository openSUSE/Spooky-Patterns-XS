use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;

my %exp = ( 2 => [ [ 1, 2, 23 ] ], 1 => [ [ 1, 1, 165 ] ] );
for my $num ( 1 .. 2 ) {
    open( my $fh, '<', "t/04license.$num.pattern" );
    my $str = join( '', <$fh> );
    close($fh);

    my $m = Spooky::Patterns::XS::init_matcher();
    $m->add_pattern( 1, Spooky::Patterns::XS::parse_tokens($str) );
    my $best = $m->find_matches("t/04license.$num.txt");
    cmp_deeply( $exp{$num}, $best );
}

done_testing();
