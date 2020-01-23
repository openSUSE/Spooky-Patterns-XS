#! /usr/bin/perl

use 5.012;
use strict;
use warnings;
use Test::More;
use Spooky::Patterns::XS;

my %patterns;
$patterns{42} = "This is the great GPL";
$patterns{17} = "Artistic is great too";

my $bag    = Spooky::Patterns::XS::init_bag_of_patterns;
$bag->set_patterns( \%patterns );
$bag->dump('t/08bag.dump');
$bag->load('t/08bag.dump');
my $result = $bag->best_for('GPL is great', 2);

is(scalar @$result, 2, 'right number');
is_deeply( $result->[0], { pattern => 42, match => 0.5773 }, 'fits GPL' );

if (0) {
    use Mojo::File;
    use Mojo::JSON 'decode_json';
    # https://stephan.kulow.org/test.json.xz
    my $json = Mojo::File->new('test.json')->slurp;
    $json   = decode_json($json);
    $bag    = Spooky::Patterns::XS::init_bag_of_patterns;
    $bag->set_patterns($json->{patterns});
    #$bag->dump('test.dump');
    #$bag->load('test.dump');
    $result = $bag->best_for( $json->{snippets}{2061026}, 10);
    is_deeply( $result->[0], { pattern => 2430, match => 0.9051 }, 'fits' );

    diag $json->{patterns}{17245};
    diag $json->{patterns}{10177};

    my $stime = time;
    my $count = 0;
    for my $snippet ( keys %{ $json->{snippets} } ) {
        $result = $bag->best_for( $json->{snippets}{$snippet}, 1 )->[0];
        $count++;
        my $delta = time - $stime;
        diag "$snippet: $count/$delta $result->{pattern}/$result->{match}";
        last if ( $delta > 10 || $count > 1000 );
    }
}
done_testing();
