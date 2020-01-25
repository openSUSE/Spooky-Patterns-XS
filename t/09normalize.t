#! /usr/bin/perl

use 5.012;
use strict;
use warnings;
use Test::More;
use File::Slurp;
use Spooky::Patterns::XS;

sub compare_case {
    my $case = shift;
    my $p1 =
      Spooky::Patterns::XS::normalize( read_file("t/09normalize.$case.in") );
    my @outlines;

    my $txt;
    my $prevline = 1;
    for my $token (@$p1) {
        while ( $prevline < $token->[0] ) {
            $txt .= "\n";
            $prevline++;
        }
        $txt .= $token->[1] . " ";
    }
    chomp $txt;
    my $exp = read_file("t/09normalize.$case.out");
    chomp $exp;

    is( $txt, $exp );
}

compare_case(1);
compare_case(2);

done_testing();
