# Copyright © 2017 SUSE LLC
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, see <http://www.gnu.org/licenses/>.

package Spooky::Patterns::XS;

use strict;
use warnings;

require Exporter;

our @ISA    = qw(Exporter);
our @EXPORT_OK = qw();

our $VERSION = '1.40';

require XSLoader;
XSLoader::load('Spooky::Patterns::XS', $VERSION);

package Spooky::Patterns::XS::Hash;

sub hex {
    my $self = shift;
    my $hash = $self->hash128;
    return sprintf("%016x%016x", $hash->[0], $hash->[1]);
}

sub hash64 {
    my $self = shift;
    return $self->hash128->[0];
}

1;

# vim: set sw=4 et:
