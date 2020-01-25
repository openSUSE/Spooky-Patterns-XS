#! /usr/bin/perl

use 5.012;
use strict;
use warnings;
use Test::More;
use File::Slurp;
use Spooky::Patterns::XS;

my $p1 = Spooky::Patterns::XS::normalize( read_file('t/09normalize.1.in') );
my @outlines;

my $txt;
my $prevline = 1;
for my $token (@$p1) {
	while ($prevline < $token->[0]) {
		$txt .= "\n";
		$prevline++
	}
	$txt .= $token->[1] . " ";
}
chomp $txt;
my $exp = read_file('t/09normalize.1.out');
chomp $exp;

is($txt, $exp);

done_testing();
