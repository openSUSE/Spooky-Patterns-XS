use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;

open(my $fh, '<', 't/04license.pattern');
my $str = join('', <$fh>);
close($fh);

my $m = Spooky::Patterns::XS::init_matcher();
$m->add_pattern(1, Spooky::Patterns::XS::parse_tokens($str));
my $best = $m->find_matches('t/04license.txt');
cmp_deeply([[1,2,23]], $best);

done_testing();
