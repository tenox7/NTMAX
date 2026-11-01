#!/usr/bin/perl
#
#  r3kcheck.pl -- check MIPS COFF objects and PE images for code an R3000
#  cannot run.  gxemul runs such code without complaint, so this is the check.
#
#    perl r3kcheck.pl [-v] FILE.obj|FILE.exe ...
#
#  It reports, for the code sections:
#
#	MIPS II+	instructions the R3000 does not have (branch-likely, ll/sc,
#			ldc1/sdc1, traps, sync, sqrt, trunc.w, 64-bit, cache...)
#	load delay	the instruction after a load (lb..lwr, lwc1, mfc0, mfc1,
#			cfc1, mtc1) uses the loaded register: the R3000 does not
#			wait for the load, so it reads the old value
#	FP compare	c.cond directly followed by bc1f/bc1t (the R3000 needs
#			one instruction between them)
#	HI/LO		mult, div, mthi or mtlo within two instructions after
#			mfhi/mflo
#	branch in slot	a branch or jump in a delay slot
#
#  A branch that is always taken (b, bal: beq r,r and bgez/bgezal zero) only
#  goes to its target.
#
#  A load in the delay slot of a jump (j, jal, jr, jalr) is counted as
#  "load in jump slot": the next instruction is at the jump target, which a
#  single object does not show.  -v lists every finding, otherwise the first
#  few of each kind.  Exit status 1 if anything but jump slots was found.

use strict;
use warnings;

my $verbose = 0;
if (@ARGV && $ARGV[0] eq '-v') {
	$verbose = 1;
	shift;
}
die "usage: r3kcheck.pl [-v] FILE...\n" unless @ARGV;

my %MACHINE = (0x162 => 'R3000', 0x166 => 'R4000', 0x168 => 'R10000');
my @ISA = ('not an R3000 instruction', 'MIPS I', 'MIPS II', 'MIPS III', 'MIPS IV');
my @KINDS = ('MIPS II+', 'load delay', 'FP compare', 'HI/LO', 'branch in slot',
    'load in jump slot');
my $SHOW = 5;

my $bad = 0;
$bad += check_file($_) for @ARGV;
exit($bad ? 1 : 0);


sub sext16 {
	my ($w) = @_;
	my $v = $w & 0xffff;
	return $v >= 0x8000 ? $v - 0x10000 : $v;
}

# A double-precision register is an even/odd pair of 32-bit registers.
sub fsrc {
	my ($fmt, $r) = @_;
	return $fmt eq 'd' ? ($r & ~1, ($r & ~1) + 1) : ($r);
}

sub decode {
	my ($w, $pc) = @_;
	my $op = $w >> 26;
	my $rs = ($w >> 21) & 31;
	my $rt = ($w >> 16) & 31;
	my $rd = ($w >> 11) & 31;
	my $fn = $w & 63;
	my %i = (isa => 1, rd => [], frd => []);
	my $target = $pc + 4 + (sext16($w) << 2);

	if ($op == 0) {
		my @n = qw(sll movci srl sra sllv - srlv srav jr jalr movz movn
		    syscall break - sync mfhi mthi mflo mtlo dsllv - dsrlv dsrav
		    mult multu div divu dmult dmultu ddiv ddivu add addu sub subu
		    and or xor nor - - slt sltu dadd daddu dsub dsubu tge tgeu tlt
		    tltu teq - tne - dsll - dsrl dsra dsll32 - dsrl32 dsra32);
		$i{name} = $w == 0 ? 'nop' : $n[$fn];
		if ($fn == 0 || $fn == 2 || $fn == 3) {
			@i{'rd', 'wr'} = ([$rt], $rd);
		} elsif ($fn == 4 || $fn == 6 || $fn == 7) {
			@i{'rd', 'wr'} = ([$rs, $rt], $rd);
		} elsif ($fn == 8 || $fn == 9) {
			@i{'rd', 'br'} = ([$rs], 'jump');
			$i{wr} = $rd if $fn == 9;
		} elsif ($fn == 12 || $fn == 13) {
		} elsif ($fn == 16 || $fn == 18) {
			@i{'wr', 'hilo_r'} = ($rd, 1);
		} elsif ($fn == 17 || $fn == 19) {
			@i{'rd', 'hilo_w'} = ([$rs], 1);
		} elsif ($fn >= 24 && $fn <= 27) {
			@i{'rd', 'hilo_w'} = ([$rs, $rt], 1);
		} elsif (($fn >= 32 && $fn <= 39) || $fn == 42 || $fn == 43) {
			@i{'rd', 'wr'} = ([$rs, $rt], $rd);
		} elsif ($i{name} eq '-') {
			@i{'name', 'isa'} = ('special?', 0);
		} elsif ($fn == 1 || $fn == 10 || $fn == 11) {
			$i{isa} = 4;
		} elsif ($fn == 15 || ($fn >= 48 && $fn <= 54)) {
			$i{isa} = 2;
		} else {
			$i{isa} = 3;
		}
	} elsif ($op == 1) {
		my %n = (0 => 'bltz', 1 => 'bgez', 2 => 'bltzl', 3 => 'bgezl',
		    8 => 'tgei', 9 => 'tgeiu', 10 => 'tlti', 11 => 'tltiu',
		    12 => 'teqi', 14 => 'tnei', 16 => 'bltzal', 17 => 'bgezal',
		    18 => 'bltzall', 19 => 'bgezall');
		$i{name} = $n{$rt} // 'regimm?';
		$i{rd} = [$rs];
		if ($rt == 0 || $rt == 1 || $rt == 16 || $rt == 17) {
			@i{'br', 'target'} = ('cond', $target);
			$i{always} = 1 if $rs == 0 && ($rt == 1 || $rt == 17);
			$i{wr} = 31 if $rt >= 16;
		} else {
			$i{isa} = exists $n{$rt} ? 2 : 0;
		}
	} elsif ($op == 2 || $op == 3) {
		@i{'name', 'br'} = ($op == 2 ? 'j' : 'jal', 'jump');
		$i{wr} = 31 if $op == 3;
	} elsif ($op >= 4 && $op <= 7) {
		$i{name} = (qw(beq bne blez bgtz))[$op - 4];
		$i{rd} = $op < 6 ? [$rs, $rt] : [$rs];
		$i{always} = 1 if $op == 4 && $rs == $rt;
		@i{'br', 'target'} = ('cond', $target);
	} elsif ($op >= 8 && $op <= 15) {
		$i{name} = (qw(addi addiu slti sltiu andi ori xori lui))[$op - 8];
		$i{rd} = $op == 15 ? [] : [$rs];
		$i{wr} = $rt;
	} elsif ($op == 16) {
		if ($rs == 0) {
			@i{'name', 'wr', 'delay'} = ('mfc0', $rt, 1);
		} elsif ($rs == 4) {
			@i{'name', 'rd'} = ('mtc0', [$rt]);
		} elsif ($rs == 1 || $rs == 5) {
			@i{'name', 'isa'} = ($rs == 1 ? 'dmfc0' : 'dmtc0', 3);
		} elsif ($rs == 8) {
			$i{name} = (qw(bc0f bc0t bc0fl bc0tl))[$rt & 3];
			@i{'br', 'target'} = ('cond', $target);
			$i{isa} = 2 if $rt & 2;
		} elsif ($rs >= 16) {
			my %n = (1 => 'tlbr', 2 => 'tlbwi', 6 => 'tlbwr', 8 => 'tlbp',
			    16 => 'rfe', 24 => 'eret');
			$i{name} = $n{$fn} // 'cop0?';
			$i{isa} = $fn == 24 ? 3 : exists $n{$fn} ? 1 : 0;
		} else {
			@i{'name', 'isa'} = ('cop0?', 0);
		}
	} elsif ($op == 17) {
		my ($fs, $ft, $fd) = ($rd, $rt, ($w >> 6) & 31);
		my %fmt = (16 => 's', 17 => 'd', 20 => 'w', 21 => 'l');
		if ($rs == 0) {
			@i{'name', 'wr', 'delay', 'frd'} = ('mfc1', $rt, 1, [$fs]);
		} elsif ($rs == 2) {
			@i{'name', 'wr', 'delay'} = ('cfc1', $rt, 1);
		} elsif ($rs == 4) {
			@i{'name', 'rd', 'fwr', 'delay'} = ('mtc1', [$rt], $fs, 1);
		} elsif ($rs == 6) {
			@i{'name', 'rd'} = ('ctc1', [$rt]);
		} elsif ($rs == 1 || $rs == 5) {
			@i{'name', 'isa'} = ($rs == 1 ? 'dmfc1' : 'dmtc1', 3);
		} elsif ($rs == 8) {
			$i{name} = (qw(bc1f bc1t bc1fl bc1tl))[$rt & 3];
			@i{'br', 'target', 'bc1'} = ('cond', $target, 1);
			$i{isa} = 2 if $rt & 2;
		} elsif (my $f = $fmt{$rs}) {
			my @n = qw(add sub mul div sqrt abs mov neg round.l trunc.l
			    ceil.l floor.l round.w trunc.w ceil.w floor.w);
			$i{isa} = 3 if $f eq 'l';
			if ($fn < 16) {
				$i{name} = "$n[$fn].$f";
				$i{frd} = [fsrc($f, $fs), $fn < 4 ? fsrc($f, $ft) : ()];
				$i{isa} = 2 if $fn == 4 || $fn >= 12;
				$i{isa} = 3 if $fn >= 8 && $fn <= 11;
			} elsif ($fn == 32 || $fn == 33 || $fn == 36) {
				$i{name} = ($fn == 32 ? 'cvt.s' : $fn == 33 ? 'cvt.d' : 'cvt.w') . ".$f";
				$i{frd} = [fsrc($f, $fs)];
			} elsif ($fn == 37) {
				@i{'name', 'isa'} = ("cvt.l.$f", 3);
			} elsif ($fn >= 48) {
				$i{name} = sprintf('c.%d.%s', $fn - 48, $f);
				@i{'frd', 'fcmp'} = ([fsrc($f, $fs), fsrc($f, $ft)], 1);
			} elsif ($fn >= 17 && $fn <= 19) {
				@i{'name', 'isa'} = ("movcf/movz/movn.$f", 4);
			} else {
				@i{'name', 'isa'} = ("cop1 $f?", 0);
			}
		} else {
			@i{'name', 'isa'} = ('cop1?', 0);
		}
	} elsif ($op >= 20 && $op <= 23) {
		$i{name} = (qw(beql bnel blezl bgtzl))[$op - 20];
		$i{rd} = $op < 22 ? [$rs, $rt] : [$rs];
		@i{'br', 'target', 'isa'} = ('cond', $target, 2);
	} elsif ($op >= 32 && $op <= 38) {
		$i{name} = (qw(lb lh lwl lw lbu lhu lwr))[$op - 32];
		# lwl/lwr merge into the old register, so they also read it
		$i{rd} = ($op == 34 || $op == 38) ? [$rs, $rt] : [$rs];
		@i{'wr', 'delay'} = ($rt, 1);
	} elsif ($op >= 40 && $op <= 46 && $op != 44 && $op != 45) {
		$i{name} = (qw(sb sh swl sw - - swr))[$op - 40];
		$i{rd} = [$rs, $rt];
	} elsif ($op == 49) {
		@i{'name', 'rd', 'fwr', 'delay'} = ('lwc1', [$rs], $rt, 1);
	} elsif ($op == 57) {
		@i{'name', 'rd', 'frd'} = ('swc1', [$rs], [$rt]);
	} else {
		my %n = (18 => 'cop2', 19 => 'cop1x', 24 => 'daddi', 25 => 'daddiu',
		    26 => 'ldl', 27 => 'ldr', 39 => 'lwu', 44 => 'sdl', 45 => 'sdr',
		    47 => 'cache', 48 => 'll', 50 => 'lwc2', 51 => 'pref', 52 => 'lld',
		    53 => 'ldc1', 54 => 'ldc2', 55 => 'ld', 56 => 'sc', 58 => 'swc2',
		    60 => 'scd', 61 => 'sdc1', 62 => 'sdc2', 63 => 'sd');
		my %isa = (18 => 0, 19 => 4, 48 => 2, 50 => 0, 51 => 4, 53 => 2,
		    54 => 2, 56 => 2, 58 => 0, 61 => 2, 62 => 2);
		$i{name} = $n{$op} // sprintf('op%02x?', $op);
		$i{isa} = exists $n{$op} ? ($isa{$op} // 3) : 0;
	}
	return \%i;
}

sub check_file {
	my ($file) = @_;

	open(my $fh, '<:raw', $file) or die "$file: $!\n";
	my $d = do { local $/; <$fh> };
	close($fh);

	my $hdr = 0;
	if (substr($d, 0, 2) eq 'MZ') {
		$hdr = unpack('V', substr($d, 0x3c, 4));
		die "$file: not a PE image\n" unless substr($d, $hdr, 4) eq "PE\0\0";
		$hdr += 4;
	}
	my ($machine, $nsec, undef, $symptr, $nsym, $optsz) =
	    unpack('v v V V V v', substr($d, $hdr, 18));

	my @sec;
	for my $s (0 .. $nsec - 1) {
		my $o = $hdr + 20 + $optsz + 40 * $s;
		my ($name, $va, $rawsize, $rawptr, $flags) =
		    (unpack('Z8', substr($d, $o, 8)), unpack('V V V', substr($d, $o + 12, 12)),
		    unpack('V', substr($d, $o + 36, 4)));
		# (the symbols of an image have RVAs, those of an object offsets
		# in their section)
		push @sec, { name => $name, va => $hdr ? $va : 0, size => $rawsize,
		    ptr => $rawptr, flags => $flags };
	}

	# Names for the findings: the symbols defined in each section.
	my %sym;
	if ($symptr && $nsym) {
		my $strtab = $symptr + 18 * $nsym;
		for (my $k = 0; $k < $nsym; $k++) {
			my $o = $symptr + 18 * $k;
			my ($name, $value, $secno, undef, $class, $naux) =
			    unpack('a8 V s v C C', substr($d, $o, 18));
			if (substr($name, 0, 4) eq "\0\0\0\0") {
				my $p = $strtab + unpack('V', substr($name, 4, 4));
				$name = unpack('Z*', substr($d, $p, 256));
			} else {
				$name =~ s/\0.*//s;
			}
			push @{$sym{$secno}}, [$value, $name]
			    if $secno > 0 && ($class == 2 || $class == 3)
			    && $name !~ /^[.\$]/ && $name !~ /^\d+\$/;
			$k += $naux;
		}
	}

	my %count = map { $_ => 0 } @KINDS;
	my @found;
	my $words = 0;
	for my $s (0 .. $#sec) {
		my $sec = $sec[$s];
		next unless $sec->{flags} & 0x20 && $sec->{size} && $sec->{ptr};
		my @w = unpack('V*', substr($d, $sec->{ptr}, $sec->{size} & ~3));
		my @ins = map { decode($w[$_], 4 * $_) } 0 .. $#w;
		$words += @w;
		my @names = sort { $a->[0] <=> $b->[0] } @{$sym{$s + 1} || []};

		my $where = sub {
			my ($k) = @_;
			my $pc = 4 * $k + $sec->{va};
			my ($base, $name) = (0, $sec->{name});
			for my $n (@names) {
				last if $n->[0] > $pc;
				($base, $name) = @$n;
			}
			return sprintf('%s+0x%x', $name, $pc - $base);
		};
		my $add = sub {
			my ($kind, $k, $text) = @_;
			$count{$kind}++;
			push @found, sprintf('  %-17s %-36s %08x  %s', $kind,
			    $where->($k), $w[$k], $text)
			    if $verbose || $count{$kind} <= $SHOW;
		};

		for my $k (0 .. $#ins) {
			my $p = $ins[$k];
			my $inslot = $k > 0 && $ins[$k - 1]{br};

			$add->('MIPS II+', $k, "$p->{name}: $ISA[$p->{isa}]")
			    if $p->{isa} != 1;
			$add->('branch in slot', $k, "$p->{name} after $ins[$k - 1]{name}")
			    if $p->{br} && $inslot;
			$add->('FP compare', $k, "$p->{name} then $ins[$k + 1]{name}")
			    if $p->{fcmp} && $k < $#ins && $ins[$k + 1]{bc1};
			if ($p->{hilo_r}) {
				for my $n ($k + 1, $k + 2) {
					$add->('HI/LO', $k, "$p->{name} ... $ins[$n]{name}")
					    if $n <= $#ins && $ins[$n]{hilo_w};
				}
			}

			next unless $p->{delay};
			next unless defined $p->{fwr} || (defined $p->{wr} && $p->{wr});
			my @next = ($k + 1);
			if ($inslot) {
				my $b = $ins[$k - 1];
				if ($b->{br} eq 'jump') {
					$add->('load in jump slot', $k, "$p->{name} in the slot of $b->{name}");
					next;
				}
				@next = () if $b->{always};
			push @next, $b->{target} / 4
				    if $b->{target} >= 0 && $b->{target} / 4 <= $#ins;
			}
			for my $n (@next) {
				next if $n > $#ins;
				my $q = $ins[$n];
				my $hit;
				if (defined $p->{wr} && grep { $_ == $p->{wr} } @{$q->{rd}}) {
					$hit = "r$p->{wr}";
					# lwl/lwr to the same register may follow each other
					undef $hit if $p->{name} =~ /^lw[lr]$/
					    && $q->{name} =~ /^lw[lr]$/ && $q->{wr} == $p->{wr};
				}
				$hit = "f$p->{fwr}" if defined $p->{fwr}
				    && grep { $_ == $p->{fwr} } @{$q->{frd}};
				$add->('load delay', $k, "$p->{name} then $q->{name} uses $hit")
				    if $hit;
			}
		}
	}

	my $real = 0;
	$real += $count{$_} for grep { $_ ne 'load in jump slot' } @KINDS;
	printf "%s: %s, %d instructions: %s\n", $file,
	    $MACHINE{$machine} // sprintf('machine 0x%x', $machine), $words,
	    join(', ', map { "$count{$_} $_" } grep { $count{$_} } @KINDS) || 'clean';
	print "$_\n" for @found;
	return $real ? 1 : 0;
}
