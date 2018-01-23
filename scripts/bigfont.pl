#!/usr/bin/perl -w

# defaults
$fname = "fixed";
$debug = 0;

# parse args
while (defined($ARGV[0]) && $ARGV[0] =~ /^-(.+)/) {
    if ($1 eq "fn") {
	$fname = $ARGV[1];
	shift; shift;
    } else {
	printf STDERR "unknown option $ARGV[0]";
	exit 1;
    }
}

########################################################################

sub bitscale() {
    local($in) = $_[0];
    local($scale) = $_[1];
    local($i, $out);

    for ($i = 0, $out = 0; $in != 0; $i++) {
	if ($in & (1 << $i)) {
	    $in  &= ~(1 << $i);
	    $out |= (1 << ($i * $scale + $scale-1));
	}
    }
    return $out;
}

sub printline () {
    local($bits) = $_[0];
    local($left) = $_[1];
    local($width) = $_[2];
    local($start,$val,$i);

    $start = 4 * length($bits);
    $val = hex($bits);

    print STDERR "|" if $debug;
    for ($i = $start-1; $i >= $start-$width; $i--) {
	if ($val & (1 << ($left+$i))) {
	    print STDERR "*" if $debug;
	} else {
	    print STDERR " " if $debug;
	}
    }
    $big = &bitscale($val,3);
    printf(STDERR "| %s | %0*x |\n",$bits,$start/4*3,$big) if $debug;
    $big |= ($big >> 1);
    printf("%0*x\n",$start/4*3,$big);
    printf("%0*x\n",$start/4*3,$big);
    printf("%0*x\n",$start/4*3,0);
}

sub parsechar () {
    local($name) = $_[0];
    local($sw,$sh,$dw,$dh);
    local($b1,$b2,$b3,$b4);
    local(@bitmap);
    local($i) = 0;
    local($n) = 0;
    local($s) = 3;

    print STDERR "--\n"	if $debug;
    print STDERR "*"	if !$debug;
    print "STARTCHAR $name\n";
    while (<FONT>) {
	last if /ENDCHAR/;
	if (/(ENCODING|BITMAP)/) {
	    print;
	} elsif (/SWIDTH (\S*) (\S*)/) {
	    $sw = $1, $sh = $2;
	    printf("SWIDTH %d %d\n",$s*$sw,$s*$sh);
	} elsif (/DWIDTH (\S*) (\S*)/) {
	    $dw = $1, $dh = $2;
	    printf("DWIDTH %d %d\n",$s*$dw,$s*$dh);
	} elsif (/BBX (\S*) (\S*) (\S*) (\S*)/){
	    $b1 = $1, $b2 = $2, $b3 = $3, $b4 = $4;
	    printf("BBX %d %d %d %d\n",$s*$b1,$s*$b2,$s*$b3,$s*$b4);
	} elsif (/^([a-f0-9]*)$/){
	    $bitmap[$n++] = $1;
	} else {
	    print "oops: $_";
	    exit(1);
	}
    }
    for ($i = 0; $i < $n; $i++) {
	&printline($bitmap[$i],$b3,$dw);
    }
    print "ENDCHAR\n";
}

########################################################################

open(FONT,"fstobdf -fn $fname |") || die;

while (<FONT>) {
    if (/STARTCHAR (.*)/) {
	&parsechar($1);
    } elsif (/FONT -(\w*)-(\w*)-(\w*)-(\w*)-(\w*)-(\w*)-(\d*)-(\w*)-(\w*)-(\w*)-(\w*)-(\d*)-(.*)/) {
	printf("FONT -Xxl-Led$2-$3-$4-$5-$6-%d-$8-$9-$10-$11-%d-$13\n",
	       $7*3,$12*3);
    } elsif (/FONT \"-(\w*)-(\w*)-(\w*)-(\w*)-(\w*)-(\w*)-(\d*)-(\w*)-(\w*)-(\w*)-(\w*)-(\d*)-(.*)/) {
	printf("FONT \"-Xxl-Led$2-$3-$4-$5-$6-%d-$8-$9-$10-$11-%d-$13\n",
	       $7*3,$12*3);
    } elsif (/SIZE (\S*) (\S*) (\S*)/) {
	printf("SIZE %d %d %d\n",$1*3,$2,$3);
    } elsif (/FOUNDRY \"(\S*)\"/) {
	printf("FOUNDRY \"Led\"\n");
    } elsif (/(PIXEL_SIZE|POINT_SIZE|AVERAGE_WIDTH|X_HEIGHT|QUAD_WIDTH|FONT_ASCENT|FONT_DESCENT) (\S*)/) {
	printf("$1 %d\n",3*$2);
    } else {
	print;
    }
}

print STDERR " done\n"	if !$debug;
