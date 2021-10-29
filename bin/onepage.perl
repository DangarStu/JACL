#!/usr/bin/perl

if ($#ARGV == -1) {
    print "usage: onepage <pagelist> <outputfile>\n";
    exit;
}

$documentList = $ARGV[0];
$outputFile = $ARGV[1];
$currentQuestion = 0;

print "using $documentList to build $outputFile.\n";

$baseName = $questionList;
$baseName =~ s/(.+)\..*/\1/;

open (DOCUMENT_LIST, "$documentList") 
	|| die "Unable to open list of questions";
open (GLOBAL_HEADER, "globalheader.html")
	|| die "Unable to open globalheader.html";
open (GLOBAL_FOOTER, "globalfooter.html")
	|| die "Unable to open globalfooter.html";
open (OUTPUT_FILE, ">$outputFile")
	|| die "Unable to write output file: $outputFile";

while (<GLOBAL_HEADER>) {
	print OUTPUT_FILE;
}

while (<DOCUMENT_LIST>) {
	chomp;
    if (/(.*html)$/) {
		AddFile($1);
    }
}

print QUIZ_PROGRAM $questionCode;

while (<GLOBAL_FOOTER>) {
	print OUTPUT_FILE;
}

print "$outputFile created\n";

sub AddFile()
{

    print "reading $_[0]\n";

	open (CURRENT_FILE, "$_[0]")
		|| die "Unable to write output file: $outputFile";

	$outputing = 0;

	while (<CURRENT_FILE>) {
		s/<a.*?href=".*?#(.*?>)/<a href="#\1/gi;

		if ($_ =~ /<body.*?>/ || $_ =~ /<BODY.*?>/) {
			$outputing = 1;
			s/<body>//;
		}

		if ($_ =~ /END OF BODY/) {
			$outputing = 0;
		}
			
		if ($outputing == 1) {
			print OUTPUT_FILE;
		}

	}
}
