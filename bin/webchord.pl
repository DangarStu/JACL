#!/usr/bin/perl

# Web Chord v1.1 - A CGI script	to convert a ChordPro file to HTML
# Copyright 1998-2003 Martin Vilcans (martin@mamaviol.org)
#
# CGI parameters:
#  chopro   - this parameter can be submitted by a form	as a 
#	      text field or file upload.
#
# History:
# 1998-07-20 Version 1.0
# 2003-08-03 Version 1.1 Uses stylesheets

#use strict;		# doesn't work with file upload
use CGI;

my($q,$infile,$chopro,$uplinfo,$output,$i);

$q = new CGI;		# query	parameters
print $q->header();

open(LOG,'>>/usr/local/jacl/logs/webchord.log') || die "Cannot open log: $!";

print LOG "---\n" . localtime()	. ": from host $ENV{'REMOTE_HOST'}\n";

$infile	= $q->param('chordpro');

unless(defined($infile)) {
	bailout("No chordpro parameter");
}

$infile = "/var/www/downloads/chordpro/" . $infile;

my $chopro = do {
    local $/ = undef;
    open my $fh, "<", $infile
        or die "could not open $infile: $!";
    <$fh>;
};

close $fh || print LOG "close failed: $!\n";

chopro2html($chopro);

print $q->end_html();
exit;

sub bailout
{
	my($msg) = @_;
	print LOG $msg."\n";
	print "<HTML><HEAD><TITLE>Web Chord: Error</TITLE></HEAD>" .
		"<BODY><H1>Error</H1><P>\n$msg\n</P>" .
		"</BODY></HTML>";
	exit;
}

sub chopro2html
# outputs the HTML code of
# the chordpro file in the first parameter
{
	my($chopro) = @_;

	$chopro	=~ s/\</\&lt;/g; # replace < with &lt;
	$chopro	=~ s/\>/\&gt;/g; # replace > with &gt;
	$chopro	=~ s/\&/\&amp;/g; # replace & with &amp;

	my($title);
	if(($chopro =~ /^{title:(.*)}/mi) || ($chopro =~ /^{t:(.*)}/mi)) {
		# title	found
		$title = $1;
	} else {
		# no title found
		$title = "ChordPro song";
	}
	print <<_END_;
<HTML><HEAD><TITLE>$title</TITLE>
<STYLE TYPE="text/css"><!--
H1 {
font-family: "Arial", Helvetica;
font-size: 24pt;
}
H2 {
font-family: "Arial", Helvetica;
font-size: 20pt;
}
.lyrics, .lyrics_chorus { font-family: "Arial", Helvetica; font-size: 16pt; }
.lyrics_tab, .lyrics_chorus_tab { font-family: "Arial", Helvetica; font-size: 16pt; }
.lyrics_chorus, .lyrics_chorus_tab, .chords_chorus, .chords_chorus_tab { font-weight: bold; }
.chords, .chords_chorus, .chords_tab, .chords_chorus_tab { font-family: "Arial", Helvetica; font-size: 14pt; color: grey; padding-right: 4pt;}

.comment, .comment_italic, .comment_box { font-family: "Arial", Helvetica; background-color: #dddddd; }
.comment_italic { font-style: italic; }
.comment_box { border: solid; }
--></STYLE>
</HEAD><BODY>
<!--\nConverted from ChordPro format with Web Chord by	Martin Vilcans
(see http://www.algonet.se/~marvil/webchord-->
_END_

	my($mode) = 0; # mode defines which class to use

	#mode =           0           1              2             3
	#	      normal      chorus         normal+tab    chorus+tab
	my @lClasses = ('lyrics', 'lyrics_chorus', 'lyrics_tab', 'lyrics_chorus_tab'  );
	my @cClasses = ('chords', 'chords_chorus', 'chords_tab', 'chords_chorus_tab'  );

	while($chopro ne '') {
		$chopro	=~ s/(.*)\n?//; # extract and remove first line
		$_ = $1;
		chomp;

		if(/^#(.*)/) {                                  # a line starting with # is a comment
			print "<!--$1-->\n";                    # insert as HTML comment
		} elsif(/{(.*)}/) {                             # this is a command
			$_ = $1;
			if(/^title:/i || /^t:/i) {                  # title
				print "<H1>$'</H1>\n";
			} elsif(/^subtitle:/i || /^st:/i) {         # subtitle
				print "<H2>$'</H2>\n";
			} elsif(/^start_of_chorus/i || /^soc/i)	{   # start_of_chorus
				$mode |= 1;
			} elsif(/^end_of_chorus/i || /^eoc/i) {     # end_of_chorus
				$mode &= ~1;
			} elsif(/^comment:/i ||	/^c:/i)	{           # comment
				print "<P class=\"comment\">$'</P>\n";
			} elsif(/^comment_italic:/i || /^ci:/i)	{   # comment_italic
				print "<P class=\"comment_italic\">$'</P>\n";
			} elsif(/^comment_box:/i || /^cb:/i) {      # comment_box
				print "<P class=\"comment_box\">$'</P>\n";
			} elsif(/^start_of_tab/i || /^sot/i) {      # start_of_tab
				$mode |= 2;
			} elsif(/^end_of_tab/i || /^eot/i) {        # end_of_tab
				$mode &= ~2;
			} else {
				print "<!--Unsupported command:	$_-->\n";
			}
		} else { # this is a line with chords and lyrics
			my(@chords,@lyrics);
			@chords=("");
			@lyrics=();
			s/\s/\&nbsp;/g;					# replace spaces with hard spaces
			while(s/(.*?)\[(.*?)\]//) {
				push(@lyrics,$1);
				push(@chords,$2	eq '\'|' ? '|' : $2);
			}
			push(@lyrics,$_);				# rest of line (after last chord) into @lyrics

			if($lyrics[0] eq "") {			# line began with a chord
				shift(@chords);				# remove first item
				shift(@lyrics);				# (they	are both empty)
			}

			if(@lyrics==0) {	# empty	line?
				print "<BR>\n";
			} elsif(@lyrics==1 && $chords[0] eq "")	{	# line without chords
				print "<DIV class=\"$lClasses[$mode]\">$lyrics[0]</DIV>\n";
			} else {
				print "<TABLE cellpadding=0 cellspacing=0>";
				print "<TR>\n";
				my($i);
				for($i = 0; $i < @chords; $i++) {
					print "<TD class=\"$cClasses[$mode]\">$chords[$i]</TD>";
				}
				print "</TR>\n<TR>\n";
				for($i = 0; $i < @lyrics; $i++) {
					print "<TD class=\"$lClasses[$mode]\">$lyrics[$i]</TD>";
				}
				print "</TR></TABLE>\n";
			}
		}
	}	#while
}
