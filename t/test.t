#! /usr/bin/perl

use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use File::Basename;
use File::Find 'find';
use Spooky::Patterns::XS;
use Storable;
use Time::HiRes 'time';

plan skip_all => 'set DATA_ROOT to enable this test' unless $ENV{DATA_ROOT};

my $m = Spooky::Patterns::XS::init_matcher();
my $dataroot = $ENV{DATA_ROOT};

for my $fn ( glob("$dataroot/patterns/*") ) {
    my $tok = retrieve($fn);
    my $num = basename($fn);
    $m->add_pattern( $num, $tok);
}
ok(1, "Loaded patterns");

sub find_match {
    #return unless $_ eq 'LICENSE';
    return unless -f $_;
    my $best = $m->find_matches($_);
    #print STDERR "FOUND $_: " . scalar(@$best) . "\n";
}

my $start = time;

find(\&find_match, "$dataroot/unpacked/");

print STDERR "Matches took " . (time - $start) . "\n";

done_testing();
