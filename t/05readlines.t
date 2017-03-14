#! /usr/bin/perl

use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;
use utf8;

my $ret = Spooky::Patterns::XS::read_lines('t/03match.txt', { 4 => 1 });
cmp_deeply($ret, [[4,1,'Hello world, this is a test']], 'read_lines returns line 4');

$ret = Spooky::Patterns::XS::read_lines('t/05readlines.1.txt', { 1 => 1 });
cmp_deeply($ret, [[1,1,"la araña is a böses Tier"]], 'read_lines returns line 4');

# I'm unable to find a good way to find out the returned string doesn't produce errors
# when printed - just don't crash
$ret = Spooky::Patterns::XS::read_lines('t/05readlines.2.raw', { 1 => 1 });

done_testing();
