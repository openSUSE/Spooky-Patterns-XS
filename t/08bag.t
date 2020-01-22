#! /usr/bin/perl

use 5.012;
use strict;
use warnings;
use Test::More;
use Test::Deep;
use Spooky::Patterns::XS;

my %patterns;
$patterns{42} = "This is the great GPL";
$patterns{17} = "Artistic is great too";

my $bag    = Spooky::Patterns::XS::init_patterns( \%patterns );
my $result = $bag->best_for('GPL is great');

is_deeply( $result, [ 42, 0.5773 ], 'fits GPL' );

if (0) {
    use Mojo::File;
    use Mojo::JSON 'decode_json';
    # https://stephan.kulow.org/test.json.xz
    my $json = Mojo::File->new('test.json')->slurp;
    $json   = decode_json($json);
    $bag    = Spooky::Patterns::XS::init_patterns( $json->{patterns} );
    $result = $bag->best_for( $json->{snippets}{2061026} );
    is_deeply( $result, [ 2430, 0.9051 ], 'fits' );

    diag $json->{patterns}{17245};
    diag $json->{patterns}{10177};

    my $stime = time;
    my $count = 0;
    for my $snippet ( keys %{ $json->{snippets} } ) {
        $result = $bag->best_for( $json->{snippets}{$snippet} );
        $count++;
        my $delta = time - $stime;
        diag "$snippet: $count/$delta $result->[1]/$result->[0]";
        last if ( $delta > 10 || $count > 1000 );
    }
}
done_testing();
