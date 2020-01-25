#! /usr/bin/perl

use 5.012;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;
use utf8;

my $ret = Spooky::Patterns::XS::read_lines( 't/03match.txt', { 4 => 1 } );
cmp_deeply(
    $ret,
    [ [ 4, 1, 'Hello world, this is a test' ] ],
    'read_lines returns line 4'
);

$ret = Spooky::Patterns::XS::read_lines( 't/05readlines.1.txt', { 1 => 1 } );

# read_lines only returns chars, so we need to check utf-8 here
my $str = $ret->[0]->[2];
is( utf8::is_utf8($str) ? 1 : 0, 0, 'not returned as utf-8' );
utf8::decode($str);
is( $str, "la araña is a böses Tier", 'unicode string' );
$ret->[0]->[2] = 'UTF-8';
cmp_deeply( $ret, [ [ 1, 1, 'UTF-8' ] ], 'read_lines returns line 4' );

# I'm unable to find a good way to find out the returned string doesn't produce errors
# when printed - just don't crash
$ret = Spooky::Patterns::XS::read_lines( 't/05readlines.2.raw', { 1 => 1 } );

$ret = Spooky::Patterns::XS::read_lines( 't/04license.12.txt', { 115 => 1 } );
cmp_deeply(
    $ret,
    [ [ 115, 1, 'END OF TERMS AND CONDITIONS' ] ],
    "end of file returned"
);

done_testing();
