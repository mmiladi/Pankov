#!/usr/bin/perl -I /home/will/Soft/locarna-1.8.1-alpha/lib/perl

use MLocarna;

$inputfilename=$ARGV[0];

my $aln = read_clustalw_alnloh("$inputfilename");
 
for my $i (0..@$aln-1) {
    
    $aln->[$i]{seq} =~ tr/ACGTU/UGCAA/;
    $aln->[$i]{seq} = reverse($aln->[$i]{seq});
}

my $fh=*STDOUT;
write_clustalw_alnloh($fh,$aln,75);
