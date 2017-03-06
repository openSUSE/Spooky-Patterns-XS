use strict;

use ExtUtils::MakeMaker qw(WriteMakefile);
use File::Spec::Functions;

my (@INC, @LIBPATH, @LIBS);

my $DEFINES = '-O2';
$DEFINES .= ' -Wall -Wno-unused-value -Wno-format-security -std=c++11';

unshift @INC, '-I. -I.. -Isrc';

WriteMakefile(
    NAME              => 'Spooky::Patterns::XS',
    VERSION_FROM      => 'lib/Spooky/Patterns/XS.pm',
    CC => 'g++',
    LD => 'g++',
    XSOPT => '-C++',
    LICENSE       => 'mit',
    AUTHOR            => 'Stephan Kulow <coolo@suse.de>',
    INC               => join(' ', @INC),
    LIBS              => [ join(' ', @LIBPATH, @LIBS) ],
    DEFINE            => $DEFINES,
    OBJECT            => '$(O_FILES)',
    PREREQ_PM         => { 'Test::Deep' => 0 },
    META_MERGE        => {
        resources => {
            license    => 'https://opensource.org/licenses/MIT',
            repository => 'https://github.com/coolo/spooky-pattern-xs',
        },
    },
);
