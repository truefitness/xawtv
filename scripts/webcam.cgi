#!/usr/bin/perl
#
# send a movie (well, sort of) to the browser.  Works with netscape only,
# other browsers get a static image.  Uses serverpush, i.e. sends the frames
# as multipart/x-mixed-replace elements.
#
# (c) 1999 Gerd Knorr <kraxel@goldbach.in-berlin.de>
#
use strict;

# config
my $IMAGE  = "$ENV{DOCUMENT_ROOT}/images/webcam.jpeg";
my $MAXSEC = 600;	# 10 min timeout

###########################################################################

undef $/;
$|=1;

my $BO = "wrdlbrmpft";

my $serverpush = ($ENV{HTTP_USER_AGENT} =~ /^Mozilla/ &&
		  $ENV{HTTP_USER_AGENT} !~ /[Cc]ompatible/);

my $start = time;
my @st = stat($IMAGE) or die "stat $IMAGE: $!";
my $mtime = $st[9];

if ($serverpush) {
	print "Content-Type: multipart/x-mixed-replace; boundary=\"$BO\"\r\n";
	print "\r\n";
	print "\r\n--$BO\r\n";
}

for (;;) {
	# read image
	open IMG, "<$IMAGE";
	my $image = <IMG>;
	close IMG;

	# send it
	print  "Content-Type: image/jpeg\r\n";
	printf "Content-Length: %d\r\n",length($image);
	print  "\r\n";
	print $image;
	last unless $serverpush;

	# send multipart border
	if (time - $start > $MAXSEC) {
		print "\r\n--$BO--\r\n";
		last;
	} else {
		print "\r\n--$BO\r\n";
	}

	# wait until there is a new image
	foreach my $i (1 .. $MAXSEC) {
		sleep 1;
		@st = stat($IMAGE);
		if ($st[9] != $mtime) {
			$mtime = $st[9];
			last;
		}
	}
}
exit;
