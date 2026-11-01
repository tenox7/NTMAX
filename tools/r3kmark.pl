#!/usr/bin/perl
#
#  r3kmark.pl -- mark PE images as R3000 images.
#
#    perl r3kmark.pl IMAGE...
#
#  C900 with /QMR3000 makes R3000 objects (machine 0x162), but the linker of
#  the tools (LINK 3.10) writes every MIPS image as an R4000 image (0x166),
#  and an R3000 kernel maps no other image than an R3000 image into a process
#  (MM\MAPVIEW.C).  This tool sets the machine of the file header to the
#  R3000 (IMAGE_FILE_MACHINE_R3000) and computes the checksum of the optional
#  header again if the image has one, as the linker computes it: the sum of
#  the 16-bit words of the file without the checksum, the carries added back
#  in, plus the length of the file.

use strict;
use warnings;

die "usage: r3kmark.pl IMAGE...\n" unless @ARGV;
for my $name (@ARGV) {
	open(my $f, '<:raw', $name) or die "r3kmark: $name: $!\n";
	my $d = do { local $/; <$f> };
	close($f);
	die "r3kmark: $name: not a PE image\n" unless substr($d, 0, 2) eq 'MZ';
	my $pe = unpack('V', substr($d, 0x3c, 4));
	die "r3kmark: $name: not a PE image\n" unless substr($d, $pe, 4) eq "PE\0\0";
	my $machine = unpack('v', substr($d, $pe + 4, 2));
	die sprintf("r3kmark: %s: machine 0x%x is not MIPS\n", $name, $machine)
	    unless $machine == 0x162 || $machine == 0x166;
	substr($d, $pe + 4, 2) = pack('v', 0x162);

	# The checksum of the optional header (offset 64 in it).
	my $ck = $pe + 24 + 64;
	if (unpack('V', substr($d, $ck, 4)) != 0) {
		substr($d, $ck, 4) = pack('V', 0);
		my $sum = 0;
		my $n = length($d);
		my $words = $d;
		$words .= "\0" if $n & 1;
		for my $w (unpack('v*', $words)) {
			$sum += $w;
			$sum = ($sum & 0xffff) + ($sum >> 16);
		}
		substr($d, $ck, 4) = pack('V', $sum + $n);
	}

	open(my $o, '>:raw', $name) or die "r3kmark: $name: $!\n";
	print $o $d;
	close($o) or die "r3kmark: $name: $!\n";
	printf "r3kmark: %s: R3000 (was 0x%x)\n", $name, $machine;
}
