#!/usr/bin/perl
#
#  r3kimplib.pl -- make the import thunks of a MIPS import library R3000
#  code.
#
#    perl r3kimplib.pl IN.lib OUT.lib
#
#  An import library (lib -def) has a member for each export, and the member
#  of a function has the thunk that a call of the function goes to when its
#  caller does not call through the import address (__imp_NAME) itself:
#
#	lui	t0,ThunkData
#	lw	t0,ThunkData(t0)
#	jr	t0
#	nop
#
#  The R4000 waits for the lw before the jr; the R3000 does not, and its jr
#  would use the old t0.  This tool puts a nop between the lw and the jr in
#  each thunk (the section grows from 16 to 20 bytes; the relocations of the
#  lui and the lw stay where they are) and moves the other parts of the
#  member and the offsets of the archive (the two linker members) after it.
#  Members that are not such thunks are copied as they are.

use strict;
use warnings;

die "usage: r3kimplib.pl IN.lib OUT.lib\n" unless @ARGV == 2;
my ($in, $outname) = @ARGV;

open(my $f, '<:raw', $in) or die "r3kimplib: $in: $!\n";
my $lib = do { local $/; <$f> };
close($f);
die "r3kimplib: $in: not an archive\n" unless substr($lib, 0, 8) eq "!<arch>\n";

# The thunk as LIB writes it (DEFLIB.C MipsEntryCode), and the R3000 thunk.
my $THUNK = pack('V4', 0x3c080000, 0x8d080000, 0x01000008, 0);
my $R3000 = pack('V5', 0x3c080000, 0x8d080000, 0, 0x01000008, 0);
my ($IMAGE_REL_MIPS_REFHI, $IMAGE_REL_MIPS_REFLO, $IMAGE_REL_MIPS_PAIR) = (4, 5, 0x25);

# Read the members: header fields and data, with the offset of each.
my @members;
my $pos = 8;
while ($pos < length($lib)) {
	my $hdr = substr($lib, $pos, 60);
	die "r3kimplib: $in: bad member header at $pos\n"
		unless length($hdr) == 60 && substr($hdr, 58, 2) eq "`\n";
	my $size = substr($hdr, 48, 10) + 0;
	push @members, { offset => $pos, name => substr($hdr, 0, 16),
	    hdr => $hdr, data => substr($lib, $pos + 60, $size) };
	$pos += 60 + $size + ($size & 1);
}

# fixthunk DATA: DATA with the thunk made R3000 code, or undef if DATA is not
# a COFF object with a MIPS thunk.
sub fixthunk {
	my ($d) = @_;
	return undef if length($d) < 20;
	my ($machine, $nsect, undef, $symptr, $nsym, $opthdr) = unpack('vvVVVv', $d);
	return undef unless ($machine == 0x162 || $machine == 0x166) && $opthdr == 0;
	my $base = 20;
	for my $s (0 .. $nsect - 1) {
		my $sh = $base + 40 * $s;
		my ($vsize, $va, $rawsize, $rawptr, $relptr, $lineptr, $nrel, $nline, $chars) =
		    unpack('VVVVVVvvV', substr($d, $sh + 8, 32));
		next unless $rawsize == 16 && substr($d, $rawptr, 16) eq $THUNK && $nrel == 3;
		# the relocations: REFHI and PAIR at 0, REFLO at 4
		my @r = map { [unpack('VVv', substr($d, $relptr + 10 * $_, 10))] } 0 .. 2;
		next unless $r[0][0] == 0 && $r[0][2] == $IMAGE_REL_MIPS_REFHI &&
		    $r[1][0] == 0 && $r[1][2] == $IMAGE_REL_MIPS_PAIR &&
		    $r[2][0] == 4 && $r[2][2] == $IMAGE_REL_MIPS_REFLO;

		# Everything at or after the end of the old thunk moves by 4.
		my $end = $rawptr + 16;
		my $move = sub { my ($p) = @_; return ($p != 0 && $p >= $end) ? $p + 4 : $p };
		my $n = substr($d, 0, $rawptr) . $R3000 . substr($d, $end);
		substr($n, 8, 4) = pack('V', $move->($symptr));
		for my $t (0 .. $nsect - 1) {
			my $th = $base + 40 * $t;
			my ($tvs, $tva, $trs, $trp, $trl, $tln) = unpack('VVVVVV', substr($n, $th + 8, 24));
			$trs = 20 if $t == $s;
			substr($n, $th + 8, 24) = pack('VVVVVV', $tvs, $tva, $trs,
			    $t == $s ? $trp : $move->($trp), $move->($trl), $move->($tln));
		}
		return $n;
	}
	return undef;
}

# Make the new members and the map from old to new offsets.
my %newoff;
my $npos = 8;
my $fixed = 0;
for my $m (@members) {
	if ($m->{name} !~ m{^/ } && $m->{name} !~ m{^// }) {
		my $n = fixthunk($m->{data});
		if (defined $n) {
			$m->{data} = $n;
			substr($m->{hdr}, 48, 10) = sprintf('%-10d', length($n));
			$fixed++;
		}
	}
	$newoff{$m->{offset}} = $npos;
	$npos += 60 + length($m->{data}) + (length($m->{data}) & 1);
}

# The linker members: the first has big-endian offsets of the member of each
# symbol, the second little-endian offsets of the members.
my $linker = 0;
for my $m (@members) {
	next unless $m->{name} =~ m{^/ };
	my $d = $m->{data};
	if ($linker++ == 0) {
		my $count = unpack('N', $d);
		for my $i (0 .. $count - 1) {
			my $o = unpack('N', substr($d, 4 + 4 * $i, 4));
			die "r3kimplib: $in: first linker member: no member at $o\n" unless exists $newoff{$o};
			substr($d, 4 + 4 * $i, 4) = pack('N', $newoff{$o});
		}
	} else {
		my $count = unpack('V', $d);
		for my $i (0 .. $count - 1) {
			my $o = unpack('V', substr($d, 4 + 4 * $i, 4));
			die "r3kimplib: $in: second linker member: no member at $o\n" unless exists $newoff{$o};
			substr($d, 4 + 4 * $i, 4) = pack('V', $newoff{$o});
		}
	}
	$m->{data} = $d;
}

open(my $o, '>:raw', $outname) or die "r3kimplib: $outname: $!\n";
print $o "!<arch>\n";
for my $m (@members) {
	print $o $m->{hdr}, $m->{data};
	print $o "\n" if length($m->{data}) & 1;
}
close($o) or die "r3kimplib: $outname: $!\n";
print "r3kimplib: $in: $fixed thunks\n";
