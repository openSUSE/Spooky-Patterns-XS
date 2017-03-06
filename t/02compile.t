use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;

my $ret = Spooky::Patterns::XS::parse_tokens("Hallo World");
cmp_deeply([176382067367922544, 14227499413149678217], $ret, "got 2 tokens");

done_testing();