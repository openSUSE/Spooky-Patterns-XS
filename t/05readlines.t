#! /usr/bin/perl

use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;

my $ret = Spooky::Patterns::XS::read_lines('t/03match.txt', { 4 => 1 });
cmp_deeply([[4,1,'Hello world, this is a test']], $ret, 'read_lines returns line 4');
done_testing();
