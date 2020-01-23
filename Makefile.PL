use strict;

use ExtUtils::MakeMaker qw(WriteMakefile);
use File::Spec::Functions;

my (@INC, @LIBPATH, @LIBS);

my $DEFINES = '-O2';
$DEFINES .= ' -Wall -Wno-unused-value -Wno-format-security -std=c++11';

unshift @INC, '-I. -I.. -Isrc';

WriteMakefile(
    NAME              => 'Spooky::Patterns::XS',
    VERSION_FROM      => 'XS.pm',
    CC => 'g++',
    depend => {
       'patterns_impl.o' => 'TokenTree.h'
    },
    LD => 'g++',
    XSOPT => '-C++',
    LICENSE       => 'GPL_2',
    AUTHOR            => 'Stephan Kulow <coolo@suse.de>',
    INC               => join(' ', @INC),
    LIBS              => [ join(' ', @LIBPATH, @LIBS) ],
    DEFINE            => $DEFINES,
    OBJECT            => '$(O_FILES)',
    PREREQ_PM         => { 'Test::Deep' => 0 },
    META_MERGE        => {
        resources => {
            license    => 'https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt',
            repository => 'https://github.com/coolo/spooky-pattern-xs',
        },
    },
);
