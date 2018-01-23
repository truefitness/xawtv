#!/usr/bin/perl
while (<>) {
	chop;
	s/([\\\"])/\\$1/g;
	print "\"$_\\n\"\n";
}
