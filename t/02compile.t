use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;

Spooky::Patterns::XS::init_matcher();
my $ret = Spooky::Patterns::XS::parse_tokens("Hello World");
cmp_deeply( $ret, [ 11695443286496022098, 14227499413149678217 ],
    "got 2 tokens" );

done_testing();
