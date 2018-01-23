#!/usr/bin/perl
use strict;

sub fix_filename ($) {
	my $name = shift @_;

	$name =~ tr/_A-Z/-a-z/;
	$name =~ s/^freq-//;
	$name .= ".list";
	return $name;
}

my ($name,$filename);

while (<>) {

	# channel lists
	if (m/\{\s*\"(\w+)\",\s*(\w+)\s*\}/) {
		print FILE "[$1]\n";
		print FILE "freq = $2\n";
		print FILE "\n";
		next;
	}
	if (m/static struct CHANLIST (\w+)/) {
		$filename = fix_filename($1);
		close FILE;
		open FILE, "> $filename" or die "open $filename: $!";
		next;
	}
	if (m/\#define (FREQ_\w+)/) {
		$filename = fix_filename($1);
		close FILE;
		open FILE, "> $filename" or die "open $filename: $!";
		next;
	}
	if (m/^\s+(FREQ_\w+)/) {
		$filename = fix_filename($1);
		print FILE "#include \"$filename\"\n";
		next;
	}

	# index file
	if (m/struct CHANLISTS chanlists/) {
		close FILE;
		open FILE, "> Index.map" or die "open Index.map: $!";
	}
	if (m/\{\s*\"([-a-zA-Z]+)\",\s+(\w+)/) {
		$name = $1;
		$filename = fix_filename($2);
		print FILE "[$name]\n";
		print FILE "file = $filename\n";
		print FILE "\n";
		next;
	}

	next if m/\#include/;
	next if m/^\/\*/;
	next if m/^\s*};/;
	next if m/^\s*$/;
	next if m/^\s*\\\s*$/;

	print "Oops: $_";
}
close FILE;
