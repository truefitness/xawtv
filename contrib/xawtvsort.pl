#!/usr/bin/perl
# Autor   : D. Landolt
# Date    : 26.8.2002
# Version : 0.9
# Name    : xawtvsort.pl
# Description : Utility to sort the xawtv frequency table
# Changers :
# 27.8.2002
# Marks works now. Some correction to handel correct.
# 30.8.2002
# Version 0.10 made
# Tool is now with menu and supports keystrokens for sortings.
# 30.8.2002
# Version 0.11 made
# You cann now chose tv canels with return key it needs v4lctl program
# 31.8.2002
# Version 0.12 made
# Scrolling when moving down is now working

use Tk;
use Tk::Table;
use Tk::Frame;
use Tk::FileSelect;
use Tk::Dialog;

$maxentry=0;
$minentry=3; # Marks the first entry witch is a station
$version = "0.12";
$xawtvfile=$ENV{HOME}."/.xawtv";


@stations = ();

$top= new MainWindow;
# Menu erstellen
$Menuleiste = $top->Frame()->pack(-side => top, -fill => x);
$DateiMenu = $Menuleiste->Menubutton(-text => "File", -underline => 0)->pack(-side => left, -padx => 2);
$HelpMenu = $Menuleiste->Menubutton(-text => "Help", -underline => 0)->pack(-side => right, -padx => 2);

$DateiMenu->command(-label => "Open",
		    -underline => 1,
		    -command => sub{ &OpenFile(); });
$DateiMenu->command(-label => "Save",
		    -underline => 1,
		    -command => sub{ SaveXawtv($xawtvfile) });
$DateiMenu->command(-label => "Save as",
		    -underline => 6,
		    -command => sub { $top->Dialog(-text => "Sorry Save as not jet implemented",
						   -title => "Sorry")->Show; } );
$DateiMenu->separator();
$DateiMenu->command(-label => "Exit",
		    -underline => 1,
		    -command => [ $top => 'destroy' ] );

$HelpMenu->command(-label => "keyboard",
		   -underline => 0,
		   -command => sub { $top->Dialog(-text =>
						"up\t\t= go up\n".
						"down\t\t= go down\n".
						"shift-up\t\t= move up\n".
						"shift-down\t= move down\n".
						"ctrl-up\t\t= move at top\n".
						"ctrl-down\t= move to bottom\n".
						"home\t\t= go to first entry\n".
						"end\t\t= go to last entry\n".
						"ctrl-l\t\t= (un)locks entry\n".
						"ctrl-m\t\t= (un)marks entry\n".
						"ctrl-u\t\t= unmarks all\n".
						"ctrl-s\t\t= sorts stations\n".
						"ctrl-n\t\t= sorts numbers\n".
						"return\t\t= sets the sender\n",
						 -buttons => [qw/Close/] ,
						 -title => "Keyboard Help"
						)->Show; } ) ;
$HelpMenu->separator();
$HelpMenu->command(-label => "about",
		   -underline => 0,
		   -command => sub { $hd = $top->Dialog(-text =>
						"Autor D. Landolt\n".
						"Version $version\n\n".
						"perl/tk programm for sorting .xawtv file",
						 -buttons => [qw/Close/] ,
						 -title => "Help about"
						);
					$hd->Show; } ) ;


# Add a Table for Sorting

$hframe = $top->Frame->pack;

$table = $top->Table(-rows => 15,
		-columns => 8,
		-fixedrows => 1,
		-takefocus => 0,
		-scrollbars => e
		);

$Statusleiste = $top->Frame()->pack(-side => bottom, -fill => x)->pack;
$StatusLabel = $Statusleiste->Label(-text => "File : ")->pack(side => left, padx => 2);
$StatusText = $Statusleiste->Label(-text =>  $xawtvfile )->pack(side => left, padx => 2);
$StatusVersion = $Statusleiste->Label(-text =>  $version )->pack(side => right, padx => 2);

ReadXawtv($xawtvfile);
$table->pack;


MainLoop;

sub OpenFile {
	my $FileDialog = $top->FileSelect(-directory => ".");
	my $file = $FileDialog->Show;
	if ($file ne "" ) {
		foreach $x ( 1..$table->totalRows ) {
			foreach $y ( 1..$table->totalColumns) {
				$table->put($x,$y,"");
			}
		}
		$StatusText->configure(-text => "Reading : ".$xawtvfile );
		$top->update;
		ReadXawtv($file);
		$xawtvfile = $file;
	}
}

sub ReadXawtv {
	($filename) = @_;
	my $nr=1;
	@stations = ();
	$maxentry = 0;


	$table->put(0,1,$table->Button(-text => "Station",
				    -underline => 0,
				    -command => sub{ tablesortstation() } ));
	$table->bind(Tk::Entry, '<Control-s>', sub { tablesortstation() } );
	$table->put(0,2,$table->Button(-text => "Nr",
				    -underline => 0,
				    -command => sub{ tablesortnr() } ));
	$table->bind(Tk::Entry, '<Control-n>', sub { tablesortnr() } );
	$table->put(0,3,$table->Button(-text => "UnMark",
				    -underline => 0,
				    -command => sub{ unmarkall() } ));
	$table->bind(Tk::Entry, '<Control-u>', sub { unmarkall() } );
	$table->put(0,4,"Lock\nmove");
	$table->put(0,5,"1\nUp");
	$table->put(0,6,"1\nDown");
	$table->put(0,7,"all\nUp");
	$table->put(0,8,"all\nDown");
	open(XA,"$filename") || die ".xawtv nicht gefunden";
	while (<XA>) {
		chomp;
		if(/^\[(.*)\]/) {
			$StationName = $1;
			push(@stations,$StationName); # In den Zwischenspeicher mit dem Originalname
			$table->put($nr,1, $table->Entry());
			$widget = $table->get($nr,1);
			$widget->insert(0,$StationName);
			$table->put($nr,2,$nr);
			my $nr_nr=$nr;
			my @command  = ();
			if($StationName ne "global" && $StationName ne "defaults") {
				$table->put($nr,3, $table->Checkbutton());
				$table->get($nr,3)->deselect();
				$table->put($nr,4, $table->Checkbutton());
				$table->get($nr,4)->deselect();
				$table->put($nr,5, $table->Button(-text => "^",
								-command => sub{ movecolumn($nr_nr,"up")} ));
				$table->put($nr,6, $table->Button(-text => "v",
								-command => sub{ movecolumn($nr_nr,"down")} ));
				$table->put($nr,7, $table->Button(-text => "^^",
								-command => sub{ moveallcolumn($nr_nr,"up")} ));
				$table->put($nr,8, $table->Button(-text => "vv",
								-command => sub{ moveallcolumn($nr_nr,"down")} ));
				$table->get($nr,1)->bind(Tk::Entry , '<Down>', sub{ settablefocus('down') } );
				$table->get($nr,1)->bind(Tk::Entry , '<Up>', sub{ settablefocus('up') } );
				$table->get($nr,1)->bind(Tk::Entry , '<Shift-Tab>', sub{ settablefocus('left') } );
				$table->get($nr,1)->bind(Tk::Entry , '<Tab>', sub{ settablefocus('right') } );
				$table->get($nr,1)->bind(Tk::Entry , '<Shift-Up>', sub{ invokebutton(5); } );
				$table->get($nr,1)->bind(Tk::Entry , '<Shift-Down>', sub{ invokebutton(6); } );
				$table->get($nr,1)->bind(Tk::Entry , '<Control-Up>', sub{ invokebutton(7); } );
				$table->get($nr,1)->bind(Tk::Entry , '<Control-Down>', sub{ invokebutton(8); } );
				$table->get($nr,1)->bind(Tk::Entry , '<Control-m>', sub{ invokebutton(3); } );
				$table->get($nr,1)->bind(Tk::Entry , '<Control-l>', sub{ invokebutton(4); } );
				$table->get($nr,1)->bind(Tk::Entry , '<Return>', sub{ choseTv(); } );
				$table->get($nr,1)->bind(Tk::Entry , '<Home>', sub{ settablefocus('home'); } );
				$table->get($nr,1)->bind(Tk::Entry , '<End>', sub{ settablefocus('end');} );
			} else {
				$widget->configure(-state => "disabled");
				$table->put($nr,3, $table->Checkbutton());
				$table->get($nr,3)->deselect();
				$table->get($nr,3)->configure(-state => "disabled");
				$table->put($nr,4, $table->Checkbutton());
				$table->get($nr,4)->deselect();
				$table->get($nr,4)->configure(-state => "disabled");
				$table->get($nr,1)->configure(-bg => "gray");
			}
			$nr++;
		} else {
			$Stations{$nr -1} .= $_."\n";
		}
	}
	close XA;
	$StatusText->configure(-text => $xawtvfile);
	$table->get($minentry,1)->focus();
	$maxentry=$nr -1;
}

sub choseTv {
	my ($x, $y) = $table->Posn($top->focusCurrent);
	my $sn = $table->get($x,2)->cget(-text);
	system("v4lctl setstation \"".$stations[$sn - 1].'"');
	print "v4lctl setstation ".$stations[$sn - 1]."\n";
}

sub invokebutton {
	my ($bnr) = @_;
	($x, $y) = $table->Posn($top->focusCurrent);
	# print "Invokebutton\n";
	my $entry = $table->get($x,2)->cget(-text);

	$table->get($x,$bnr)->invoke;

	if ( $bnr > 4) {
		settablefocus("up",$entry) if($bnr == 5);
		settablefocus("down",$entry) if($bnr == 6);
		settablefocus("up",$entry) if($bnr == 7);
		settablefocus("down",$entry) if($bnr == 8);
	}

}

sub settablefocus {
	my ($direct,$entry) = @_;
	my ($x, $y) = $table->Posn($top->focusCurrent);

	if ( $entry ne '' ) {
		my $loop = $x;
		my $pm = 0;
		$pm = 1 if($direct eq 'down');
		$pm = -1 if($direct eq 'up');
		while( $entry ne $table->get($loop, 2)->cget(-text) ) {
			# print "x = $x Loop =  $loop pm = $pm\n";
			$loop += $pm;
			$loop = $minentry if($loop > $maxentry);
			$loop = $maxentry if($loop > $maxentry);
			break if($loop eq $x); # Notbremse
		}
		# print "Found : x = $x Loop =  $loop pm = $pm\n";
		# $loop -= $pm;
		$pm = ( $loop > $x) ? 1 : -1; # Neue Richtung festlegen für Scrolling
		my $loop2 = $x;
		while ($loop2 != $loop) {
			# print "Visible : x = $x Loop =  $loop pm = $pm \$_ = $_\n";
			$loop2 += $pm;
			$table->yview(scroll,$pm) if (! $table->get($loop2, $y)->viewable);
		}
		$table->get($loop, $y)->focus;
	} else {

		if ($direct eq 'down') {
			if ( $x  >= $maxentry) {
				settablefocus("home");
			} else {
				$table->yview(scroll,1) if (! $table->get($x+1, $y)->viewable);
				$table->get($x +1 , $y)->focus;
			}
		}
		if ($direct eq 'up') {
			if ( $x == $minentry) {
				settablefocus("end");
			} else {
				$table->yview(scroll,-1) if (! $table->get($x-1, $y)->viewable);
				$table->get($x -1 , $y)->focus;
			}
		}
		if ($direct eq 'end') {
			$table->yview(moveto, 1);
			$table->get($maxentry, $y)->focus;
		}
		if ($direct eq 'home') {
			$table->yview(moveto, 0);
			$table->get($minentry, $y)->focus;
		}
	}

}

sub SaveXawtv {
	my ($filename) = @_;
	open(XA,">$filename");
	# XA= STDOUT;

	my $oldtext = $StatusText->cget(-text);
	$StatusText->configure(-text => "Saveing : ".$filename );
	$top->update;

	@stations = (); # Initialize Aerea
	print XA "[".$table->get(1,1)->get."]\n".$Stations{1};
	push(@stations,$table->get(1,1)->get);
	print XA "[".$table->get(2,1)->get."]\n".$Stations{2};
	push(@stations,$table->get(2,1)->get);
	for $i ($minentry..$maxentry) {
		print XA "[".$table->get($i,1)->get."]\n".$Stations{$table->get($i,2)->cget(-text)};
		push(@stations,$table->get($i,1)->get);
	}
	close XA;
	$StatusText->configure(-text => $oldtext );
}


sub moveallcolumn() {
	my ($nr,$direct) = @_;
	my $laufnr=$nr;

	my ( $marks, @allMarks ) = getmarked();

	if ($marks < 1) {
		while ( $laufnr > 2 && $laufnr <= $maxentry ) {
			$laufnr = movecolumn($laufnr,$direct);
		}
	} else {
		if($direct eq "down" ) { @allMarks = sort { $b cmp $a } @allMarks; }
		my $mnr=0;
		foreach(@allMarks) {
			# print "Move $_ nach $direct\n";
			$laufnr = $_;
			$mnr++;
			while ( ( $laufnr > 2 + $mnr  && $direct eq "up" )  || ( $laufnr <= ( $maxentry - $mnr)  && $direct eq "down") ) {
				$laufnr = movesinglecolumn($laufnr,$direct);
			}
		}
	}
}

sub movecolumn {
	my ($nr,$direct) = @_;
	my $dest;
	my ( $marks, @allMarks ) = getmarked();
	# print "$#allMarks -> Anzahl im Array\n";
	if ($marks < 1) {
		return movesinglecolumn($nr,$direct);
	} else {
		# print "Marked sind $marks Nr\n";
		if($direct eq "down" ) { @allMarks = sort { $b cmp $a } @allMarks; }

		foreach(@allMarks) {
			# print "Move $_ nach $direct\n";
			$dest = movesinglecolumn($_,$direct);
		}
		return $dest;
	}
}

sub movesinglecolumn {
	my ($nr,$direct) = @_;
	my $plmi=0;   # verschiebeindex -1 = auf +1 = ab;
	my $spreiz=1; # Spreizung bei gesperrten Einträgen;
	my $dest=$nr; # Startindex
	if($direct eq "up") {
		$plmi=-1;
	} else {
		$plmi=+1;
	}

	# Sperrung überspringen
	while(($dest = $nr + $plmi * $spreiz) <= $maxentry && $table->get($dest = $nr + $plmi * $spreiz,4)->{'Value'} == 1) {
		$spreiz++;
	}

	if ( $dest > 2 && $dest <= $maxentry ) {
		exchangevalue($nr,$dest);
	}
	return $dest;
}

sub getmarked {
	# Zurückbringen der Markierten Einträge
	my $i ;
	@nrArray = ();
	my $hasmarks=0;
	# print "getmarkde\n";
	for $i ( $minentry..$maxentry ) {
		if($table->get($i,  3)->{'Value'} == 1) {
			push(@nrArray,$i);
			$hasmarks++;
		}
	}
	return($hasmarks, @nrArray );
}

sub exchangevalue {
	my ($source,$dest) = @_;
	my $indexex = $index = $table->get($source,2)->cget(-text);
	my $stringex = $string = $table->get($source,1)->get;
	my $markex = $mark = $table->get($source,3)->{'Value'};
	# Zielnummer und String holen
	my $indexex = $table->get($dest ,2)->cget(-text);
	my $stringex = $table->get($dest, 1)->get;
	my $markex = $table->get($dest, 3)->{'Value'};
	# print "$nr / $dest / $indexex / $stringex \n";
	# String Verschieben
	$table->get($source,  1)->delete(0,length($string));
	$table->get($source,  1)->insert(0,$stringex);
	$table->get($dest,1)->delete(0,length($stringex));
	$table->get($dest,1)->insert(0,$string);
	# Nummer verschieben
	$table->get($source,  2)->configure(-text => $indexex);
	$table->get($dest,2)->configure(-text => $index);
	# Marker Verschieben
	if($markex == 1) {
		$table->get($source,  3)->select;
	} else {
		$table->get($source,  3)->deselect;
	}
	if($mark == 1) {
		$table->get($dest,3)->select;
	} else {
		$table->get($dest,3)->deselect;
	}

}

sub tablesortstation {
	my $i;
	# print "Tablesort\n";
	my $oldtext = $StatusText->cget(-text);
	$StatusText->configure(-text => "sorting Stations");
	$top->update;
	foreach $i ($minentry ..  $maxentry-1) {
		# print " Sort $i\n";
		if ( $table->get($i,1)->get gt $table->get($i+1 ,1)->get ) {
			exchangevalue($i,$i + 1);
			tablesortstation();
		}
	}
	$StatusText->configure(-text => $oldtext);
}
sub tablesortnr {
	# print "Tablesort Nr\n";
	my $oldtext = $StatusText->cget(-text);
	$StatusText->configure(-text => "sorting Nr");
	$top->update;
	foreach $i ($minentry ..  $maxentry-1) {
		# print " Sort $i\n";
		if ( $table->get($i,2)->cget(-text) > $table->get($i+1 ,2)->cget(-text) ) {
			exchangevalue($i,$i + 1);
			tablesortnr();
		}
	}
	$StatusText->configure(-text => $oldtext);
}

sub unmarkall {
	foreach $i ( 3..$maxentry) {
		$table->get($i,3)->deselect;
	}
}

