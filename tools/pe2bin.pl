#!/usr/bin/perl
#
#  pe2bin.pl -- lay a fixed-base PE image out as it sits in memory, from its
#  image base, for the DEC boot block (mkdecboot.pl) to load in one piece.
#
#    perl pe2bin.pl IMAGE.EXE OUT.BIN
#
#  Prints the image base and the entry point address (both hex), the load and
#  start addresses for mkdecboot.pl.  The image must not need relocation
#  (link -fixed at the address it will run at).

use strict;
use warnings;

die "usage: pe2bin.pl IMAGE.EXE OUT.BIN\n" unless @ARGV == 2;
my ($image, $out) = @ARGV;

open(my $in, '<:raw', $image) or die "$image: $!\n";
my $d = do { local $/; <$in> };
close($in);

my $pe = unpack('V', substr($d, 0x3c, 4));
die "$image: not a PE image\n" unless substr($d, $pe, 4) eq "PE\0\0";
my ($nsec, $optsz) = unpack('x2 v x12 v', substr($d, $pe + 4, 20));
my $opt = $pe + 24;
my ($entry, $base, $hdrsize) = (unpack('V', substr($d, $opt + 16, 4)),
    unpack('V', substr($d, $opt + 28, 4)), unpack('V', substr($d, $opt + 60, 4)));

my $mem = substr($d, 0, $hdrsize);
for my $s (0 .. $nsec - 1) {
	my $o = $opt + $optsz + 40 * $s;
	my ($vsize, $va, $rawsize, $rawptr) = unpack('V4', substr($d, $o + 8, 16));
	my $size = $vsize > $rawsize ? $vsize : $rawsize;
	$mem .= "\0" x ($va + $size - length($mem)) if length($mem) < $va + $size;
	substr($mem, $va, $rawsize) = substr($d, $rawptr, $rawsize) if $rawsize;
}

open(my $fh, '>:raw', $out) or die "$out: $!\n";
print $fh $mem;
close($fh) or die "$out: $!\n";
printf "%08x %08x\n", $base, $base + $entry;
