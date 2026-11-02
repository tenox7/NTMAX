#!/usr/bin/perl
#
#  makeinc.pl TREE OVERLAY DIR -- the directories of the makefiles that the
#  MAKEFILE of DIR includes (!include), and of the ones those include, as
#  nmake finds them: the macros set before an include expanded in its name
#  ($(UI), the top of NET\UI, $(BASEDIR) the top of the tree), the name
#  relative to DIR (the current directory of nmake) or else to the directory
#  of the makefile that includes it.  Prints each directory once (not DIR),
#  relative to the top of the tree and spelled as TREE (the NT tree) or
#  OVERLAY (3MAX/nt) spells it, for ntcopytop (vm.sh).  Every branch of the
#  conditionals counts; an include that names a macro not set here
#  ($(NTMAKEENV), makefile.def) or a file that is not there is passed over.

use strict;
use warnings;

@ARGV == 3 or die "usage: makeinc.pl TREE OVERLAY DIR\n";
my ($tree, $overlay, $dir) = @ARGV;
my %mac = (BASEDIR => '');
my (%seen, %dirs);

# the spelling of the path P (relative to the top, any case) in the trees,
# or undef
sub spell {
	my @p = grep { $_ ne '' } split m{/}, shift;
	my $r = '';
	for my $c (@p) {
		my $m;
		for my $top ($tree, $overlay) {
			opendir(my $h, "$top/$r") or next;
			($m) = grep { lc($_) eq lc($c) } readdir $h;
			closedir $h;
			last if defined $m;
		}
		return undef unless defined $m;
		$r = $r eq '' ? $m : "$r/$m";
	}
	return $r;
}

# the path of NAME (a name of an include) relative to the directory D, with
# the . and .. components folded
sub fold {
	my ($d, $name) = @_;
	my @w;
	for my $c (split m{/}, ($name =~ m{^/} ? $name : "$d/$name")) {
		if ($c eq '..') { pop @w } elsif ($c ne '.' && $c ne '') { push @w, $c }
	}
	return join '/', @w;
}

# S with its macros expanded: an include's name (undef when it names a macro
# not set here), or with EMPTY a macro's value, where a macro not set is empty
# as in nmake (the value is stored expanded: A=$(A) x refers to the old A)
sub expand {
	my ($s, $empty) = @_;
	for (1 .. 64) {
		last unless $s =~ /\$\((\w+)\)/;
		my $n = $1;
		my $v = $mac{$n};
		unless (defined $v) {
			return undef unless $empty;
			$v = '';
		}
		$s =~ s/\$\(\Q$n\E\)/$v/g;
	}
	return $s =~ /\$\(/ && !$empty ? undef : $s;
}

sub readmk {
	my $f = shift;	# spelled, relative to the top
	return if $seen{$f}++;
	my ($fd) = $f =~ m{^(.*)/[^/]*$};
	my $path = -f "$overlay/$f" ? "$overlay/$f" : "$tree/$f";
	open(my $h, '<', $path) or return;
	my @lines = <$h>;
	close $h;
	for (@lines) {
		s/\r?\n$//;
		s/\x1a.*//;
		if (/^\s*!\s*include\s+(.*?)\s*$/i) {
			my $name = $1;
			$name =~ s/^["<]//;
			$name =~ s/[">]$//;
			$name = expand($name);
			next unless defined $name;
			$name =~ tr{\\}{/};
			next if $name =~ /^[A-Za-z]:/;
			for my $d ($dir, $fd) {
				my $g = spell(fold($d, $name));
				next unless defined $g && -f (-e "$overlay/$g" ? "$overlay/$g" : "$tree/$g");
				my ($gd) = $g =~ m{^(.*)/[^/]*$};
				$dirs{$gd} = 1;
				readmk($g);
				last;
			}
		} elsif (/^([A-Za-z_]\w*)\s*=\s*(.*?)\s*(#.*)?$/) {
			my ($n, $s) = ($1, $2);
			$mac{$n} = expand($s, 1);
			$mac{$n} =~ tr{\\}{/};
		}
	}
}

my $d = spell($dir);
defined $d or die "makeinc.pl: no directory $dir\n";
$dir = $d;
my $m = spell("$dir/makefile");
readmk($m) if defined $m;
delete $dirs{$dir};
print "$_\n" for sort keys %dirs;
