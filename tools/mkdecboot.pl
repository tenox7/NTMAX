#!/usr/bin/perl
#
#  mkdecboot.pl -- make a DECstation boot disk image from a raw program image.
#
#    perl mkdecboot.pl PROGRAM.BIN LOAD_ADDR EXEC_ADDR DISK.IMG [DISK_MB]
#
#  Sector 0 gets the DEC boot block the PROM reads ("boot 5/rz0"):
#
#	0x08	magic 0x0002757a
#	0x0c	mode 0 = the program is contiguous on the disk
#	0x10	load address
#	0x14	execution address
#	0x18	map[0]: number of 512-byte blocks, first block
#	0x20	map[1]: 0 = end of the map
#
#  and an MBR partition table at 0x1be (the PROM never looks at it): the
#  program lives in blocks 1..2015, blocks 2016..2047 keep the firmware's
#  non-volatile configuration (arcfw/dxnvram.c; zero = not set yet), and
#  partition 1 (type 6, FAT16) starts at 1 MB for the ARC system partition.
#  All words are little-endian.
#
#  If DISK.IMG exists, only the boot block words and the program blocks are
#  written: the partition table, the non-volatile configuration and the
#  partitions stay as they are.  Otherwise a new DISK_MB (default 64) image
#  is made, its partition 1 not formatted yet.

use strict;
use warnings;

die "usage: mkdecboot.pl PROGRAM.BIN LOAD_ADDR EXEC_ADDR DISK.IMG [DISK_MB]\n"
    if @ARGV < 4;
my ($prog, $load, $exec, $disk, $mb) = @ARGV;
$load = hex($load);
$exec = hex($exec);
$mb ||= 64;

my $FIRST_BLOCK = 1;		# the program, right after the boot block
my $NVRAM_BLOCK = 2016;		# FW_NVRAM_BLOCK of arcfw/decfw.h
my $PART_START = 2048;		# partition 1 at 1 MB
my $DISK_BLOCKS = $mb * 2048;

open(my $in, '<:raw', $prog) or die "$prog: $!\n";
local $/;
my $code = <$in>;
close($in);

my $blocks = int((length($code) + 511) / 512);
die "$prog: " . length($code) . " bytes, more than fits before the NVRAM blocks\n"
    if $FIRST_BLOCK + $blocks > $NVRAM_BLOCK;
$code .= "\0" x ($blocks * 512 - length($code));

my $words = pack('V6', 0x0002757a, 0, $load, $exec, $blocks, $FIRST_BLOCK);

if (-e $disk) {
	open(my $out, '+<:raw', $disk) or die "$disk: $!\n";
	my $bb;
	sysread($out, $bb, 512) == 512 or die "$disk: cannot read the boot block\n";
	substr($bb, 0x08, 24) = $words;
	sysseek($out, 0, 0) or die "$disk: $!\n";
	syswrite($out, $bb . $code) == 512 + length($code) or die "$disk: $!\n";
	close($out) or die "$disk: $!\n";
	$mb = int((-s $disk) / (1024 * 1024));
} else {
	my $bb = "\0" x 512;
	substr($bb, 0x08, 24) = $words;

	# MBR partition 1: not active, type 6, LBA start + size; CHS left at
	# the LBA markers (0xfe 0xff 0xff), the way large disks are written.
	my $part = pack('C C3 C C3 V V', 0x00, 0xfe, 0xff, 0xff, 0x06, 0xfe, 0xff, 0xff,
	    $PART_START, $DISK_BLOCKS - $PART_START);
	substr($bb, 0x1be, 16) = $part;
	substr($bb, 0x1fe, 2) = "\x55\xaa";

	open(my $out, '>:raw', $disk) or die "$disk: $!\n";
	print $out $bb, $code;
	truncate($out, $DISK_BLOCKS * 512) or die "$disk: $!\n";
	close($out) or die "$disk: $!\n";
}

printf "%s: boot block load 0x%08x exec 0x%08x, %d blocks at block %d; %d MB\n",
    $disk, $load, $exec, $blocks, $FIRST_BLOCK, $mb;
