use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;

my $m = Spooky::Patterns::XS::init_matcher();
$m->add_pattern( 1, Spooky::Patterns::XS::parse_tokens('Hello World') );
cmp_deeply(
    $m->find_matches('t/03match.txt'),
    [ [ 1, 1, 2 ], [ 1, 4, 4 ] ],
    "Find hello twice"
);

$m = Spooky::Patterns::XS::init_matcher();
$m->add_pattern( 1, Spooky::Patterns::XS::parse_tokens('this is a $SKIP20') );
use Data::Dumper;
print Dumper($m->find_matches('t/03match.txt'));
cmp_deeply(
    $m->find_matches('t/03match.txt'),
    [ [ 1, 4, 4 ] ],
    "skip at end does not confuse it"
);

done_testing();
