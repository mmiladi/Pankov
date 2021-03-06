#!/usr/bin/env perl
# -*- perl -*-

### ============================================================================
### MAN-PAGE
###

=head1 NAME

reliability-profile.pl

=head1 SYNOPSIS

reliability-profile.pl [options] <locarna-output-dir>

=head1 DESCRIPTION

reliability-profile.pl generates a reliability profile plot of a
multiple alignment generated by mlocarna --probabilistic.

=head1 OPTIONS

=over 4

=item B<--help>

Brief help message

=item B<--man>

Full documentation

=item B<--seqname>=seqname

Project to sequence name

=item B<--dont-predict>

Turn off predicting. (def=on) 

=item B<--fit-penalty>=penalty

Penalty for on/off switching in fit

=item B<--fit-once-on>

Restrict fitting to being exactly once on

=item B<--title>=title

Title of plot

=item B<--out>=filename

Output filename

=item B<--offset>=pos

Offset of sequence in genome

=item B<--signals>=list

List of (from,to,orientation) triples. 
Show signals in plot and compared infered signal to them.
Give list as string "from0 to0 orientation0;from1 to1 orientation1 ..."
Specify multi-range signals by from0a to0a from0b to0b ... 

=item B<--structure-weight>=w

Weight of structure against sequence (1.0)

=item B<--show-sw>

Show the influence of structure weight in the plot

=item B<--beta>=f

Inverse temperature beta

=item B<--dont-plot>

Skip plotting, only output

=item B<--write-R-script>=file

Write the R script to file

=item B<--revcompl>

Plot and fit a reverse complement

=item B<--write-subseq>

Write the subsequence of fit

=item B<--output-format>=f

Output format (f = pdf or png, def=pdf)

=item B<--show-fitonoff>

Show the on/off values for the fit

=back

The target directory of mlocarna is required for obtaining the
reliability information. Optionally, the reliability plot will be
projected to the sequence with given sequence name (or containing
"seqname" as prefix).

=cut


use warnings;
use strict;

use FindBin;

## ------------------------------------------------------------
## global constants

my $LOCARNAP_FIT="$FindBin::Bin/locarnap_fit";

##------------------------------------------------------------
## options
use Getopt::Long;
use Pod::Usage;

my $help;
my $man;
my $quiet;
my $verbose;

my $seqname="";

my $dont_predict=0;

my $fitpenalty=0.3;

my $outfile="rel";

my $title="";

my $offset=1; ## (genomic) position of the first base in sequence seqname

my $signals=""; ## string giving location of signals, relative to offset

my $structure_weight=1.5; ## structure weight for fitting

my $fit_once_on;

my $signal_names="";

my $show_sw;

my $beta=12;

my $dont_plot;

my $write_R_script;

my $revcompl=0;

my $write_subseq;

my $output_format="pdf";

my $show_fitonoff=0;

my $output_width=12;

my $output_height=4;


## Getopt::Long::Configure("no_ignore_case");

GetOptions(	   
    "verbose" => \$verbose,
    "quiet" => \$quiet,   
    "help"=> \$help,
    "man" => \$man,
    "seqname=s" => \$seqname,
    "dont-predict" => \$dont_predict,
    "fit-penalty=f" => \$fitpenalty,
    "fit-once-on" => \$fit_once_on,
    "beta=f" => \$beta,
    "out=s" => \$outfile,
    "title=s" => \$title,
    "offset=i" => \$offset,
    "signals=s" => \$signals,
    "signal-names=s" => \$signal_names,
    "structure-weight=f" => \$structure_weight,
    "show-sw" => \$show_sw,
    "revcompl" => \$revcompl,
    "dont-plot" => \$dont_plot,
    "write-R-script=s" => \$write_R_script,
    "show-fitonoff" => \$show_fitonoff,
    "write-subseq" => \$write_subseq,
    "output-format=s" => \$output_format,
    "output-width=i" => \$output_width,
    "output-height=i" => \$output_height
    ) || pod2usage(2);

pod2usage(1) if $help;
pod2usage(-exitstatus => 0, -verbose => 2) if $man;


if ($#ARGV!=0 && $#ARGV!=1) {print STDERR "Need locarna output directory or filenames.\n"; pod2usage(-exitstatus => -1);}

my $alnfile;
my $bmrelfile;

if (@ARGV==1) { 
    my $dir=$ARGV[0];
    # print STDERR "Use files from LocARNA output directory $dir\n";
    $alnfile = "$dir/results/result.aln";
    $bmrelfile = "$dir/results/result.bmreliability";
} else {
    $alnfile = $ARGV[0];
    $bmrelfile = $ARGV[1];
}


#if (!defined($seqname)) {
#    print STDERR "Giving a sequence name is mandatory.\n";
#    pod2usage(1);
#}


## ------------------------------------------------------------


sub perl2Rvector {
    my @vec=@_;
    my $s="@vec";
    chomp $s;
    $s =~ s/\s+/,/g;
    $s="c($s)";
    return $s;
}

sub perl2Rstring_vector {
    my @vec=@_;
    my $s="@vec";
    chomp $s;
    if ($s ne "") {
	$s =~ s/\s+/\",\"/g;
	$s="c(\"$s\")";
	return $s;
    } else {
	return "c()";
    }
}


## ------------------------------------------------------------
## main part
#

if ( $output_format ne "pdf" && $output_format ne "png" ) {
    print STDERR "Unsupported output format $output_format\n";
    exit -1;
}

if ($outfile !~ /\.(.*)$/) {
    $outfile .= ".$output_format";
}


if (defined($show_sw) && ($show_sw!=0)) {$show_sw=1;} else {$show_sw=0;}


my $sequence_alistr=""; ## alignment string of sequence

if ($seqname ne "") {
    ## get sequence
    #
    open(IN,$alnfile) || die "Cannot read alignment from file $alnfile.\n";
    while(my $line=<IN>) {
	chomp $line;
	if ($line=~/^$seqname\S*\s+(.+)$/) {
	    $sequence_alistr.=$1;
	}
    }
    close IN;

    #print "Sequence $seqname in $alnfile (alignment string): $sequence_alistr\n";

    if ($sequence_alistr eq "") {
	print STDERR "No sequence with given name found in alignment.\n";
	exit(-1);
    }
}

### prepare plotting of signals
# parse $signals
#
my $num_signals=0;

my $signal_sizes="c()";

if (defined($signals) && ($signals ne "")) {
    my @signals_list=split /\s*;\s*/,$signals;
    
    my @signal_sizes=();
    
    foreach my $s (@signals_list) {
	my @s_list = split /\s+/,$s;
	push @signal_sizes, $#s_list/2;
	$num_signals++;
    
	if ($#s_list%2 != 0) {
	    print STDERR "Require (possibly repeated) \"from\" \"to\" pairs and \"orientation\" (+1/-1) per signal.\n  Got: \"$s\" in \"$signals\".\n";
	    exit(-1);
	}
    }
    
    $signals=~s/\s*;\s*/ /g;
    @signals_list=split /\s+/,$signals;
    
    $signals=perl2Rvector(@signals_list);
    $signal_sizes=perl2Rvector(@signal_sizes);
} else {
    $signals="c()";
}





my $on_list="c()";
my $off_list="c()";
my $on_value="";
my $off_value="";
my $fit;


if (!$dont_predict) {

## prepare data for fitting

my $tmpfile="tmp.$$";
open(TMP,">$tmpfile") || die "Cannot write $tmpfile";

my @relprof=(); ## reliability profile for the reference sequence.
                ## basically the same as content of tmpfile.
                ## we use this memory copy for computing the reliability score.

open(IN,$bmrelfile) || die "Cannot read $bmrelfile";

my $len=0; ## determine length of the profile 
while(<IN>) {
    my @line=split /\s+/,$_;
    
    my $pos=$line[0];
    my $seqrel=$line[1];
    my $strrel=$line[2];
    my $rel = ($seqrel + $structure_weight * $strrel)/($structure_weight);
    
    if ($seqname eq "" || substr($sequence_alistr,$pos-1,1) ne "-") {
	print TMP "$rel\n";
	push @relprof,$rel;
	$len++;
    }
}

close TMP;

### do fit
my $fit_cmd="cat $tmpfile | $LOCARNAP_FIT - --delta $fitpenalty".($fit_once_on?" --once-on":"").(defined($beta)?" --beta $beta":"");
my @fit_answer = readpipe($fit_cmd);

#print STDOUT "fit call: $fit_cmd\n";
unlink $tmpfile;


foreach my $line (@fit_answer) {
    if ($line =~ /FIT (.*)/) {
	$fit=$1;
    } elsif ($line =~ /ONOFF (.+) (.+)/) {
	$on_value=$1;
	$off_value=$2;
    }
}

chomp $fit;

my @fitlist=split /\s+/,$fit;


## ----------------------------------------
## compute reliability score for fitted region (for fit_once_on only)
if ($fit_once_on) {
    
    # for the score, simply add the column reliabilities
    # in the hit region and normalize by hit length
    my $hit_score=0;
    my $total_score=0;
    for (my $i=0; $i<=$len; $i++) {
	$total_score += $relprof[$i-1];
    }
    for (my $i=$fitlist[0]; $i<=$fitlist[1]; $i++) {
	$hit_score += $relprof[$i-1]; ## entries in fitlist have offset 1
    }
    my $outside_score = $total_score - $hit_score;
    
    ## compute averages
    $hit_score /= $fitlist[1]-$fitlist[0]+1;
    
    my $outside_len = $len - ($fitlist[1]-$fitlist[0]+1);
    if ($outside_len > 0) {
	$outside_score /= $outside_len;
    } 
    # otherwise $outside_score equals 0 already
    
    $total_score /= $len;
    
    print "SCORE $hit_score $outside_score\n";
}

## end compute reliability score
## ----------------------------------------



if ( $write_subseq ) {
    my $sequence = $sequence_alistr;
    $sequence =~ s/-//g;
    
    if ($sequence ne "") {
        my $fastanamestr="";
        if ($title ne "") {
            $fastanamestr="$title";
        } elsif ($seqname ne "") {
            $fastanamestr="$seqname";
        } else {
            $fastanamestr="unknown"
        }
        print "SEQ ".">".$fastanamestr." fit=".($fitlist[0]+$offset-1)."-".($fitlist[1]+$offset-1)."\n";
        print "SEQ ".substr($sequence,$fitlist[0]-1,$fitlist[1]-$fitlist[0]+1)."\n";
        if (@fitlist>2) {
            print STDERR "WARNING: SEQ reports subsequence of the first predicted signal range only.\n";
        }
    } else {
        print STDERR "Writing the subsequene requires sequence name\n";
    }
}



if (($#fitlist+1)%2!=0) {
    push @fitlist, $len;
}


if ($revcompl) {

    @fitlist = map { $len+1-$_ } @fitlist;
    
    @fitlist= reverse(@fitlist);
    
}



if ($on_value ne "") {
    print "ONOFF $on_value $off_value\n";
}




if ($fit_once_on) {
    print "FIT ".($fitlist[0]+$offset-1)." ".($fitlist[1]+$offset-1)."\n";
} else {
    print "FIT ";
    for (my $i=0; $i<$#fitlist; $i+=2) {
	print( ($fitlist[$i]+$offset-1)." ".($fitlist[$i+1]+$offset-1)." ");
    }
    print "\n";
}



##

my @on_list=();
my @off_list=();

for(my $i=0; $i<$#fitlist; $i+=2) {
    push @on_list,$fitlist[$i]-1;
    push @off_list,$fitlist[$i+1];
}

$on_list  = perl2Rvector(@on_list);
$off_list = perl2Rvector(@off_list);

## end of prediction/fitting


}



$signal_names = perl2Rstring_vector(split /\s+/,$signal_names);

my $resolution=64;
my $format_extra_str="";

if ( $output_format eq "png" ) {
    $output_width *= $resolution;
    $output_height *= $resolution;
    $format_extra_str=",res=$resolution,bg=\"transparent\",antialias=\"gray\"";
} elsif ( $output_format eq "pdf" ) { 
    $format_extra_str=",version=\"1.4\"";
}

## call R for plotting
my $rscript = "$output_format(\"$outfile\",width=$output_width,height=$output_height$format_extra_str)

rel    <- read.table(\"$bmrelfile\");
seqrel <- rel[[2]]
strrel <- rel[[3]]

if (\"$seqname\" != \"\") {
  seq <- \"$sequence_alistr\"
  seq <- strsplit(seq,split=\"\")
  tab <- unlist(seq)!=\"-\"

  seqrel<-seqrel[tab]
  strrel<-strrel[tab]
}

len<-length(seqrel)

if ($revcompl) {
  seqrel<-seqrel[len:1]
  strrel<-strrel[len:1]
}



if ($show_sw) {
  seqrel <- seqrel/$structure_weight
}


totalrel <- seqrel+strrel;

anno_space<-0.075

maxy <- max(c(1,totalrel))+anno_space*($num_signals+1);


firstpos <- $offset
lastpos  <- $offset+len-1

if ($revcompl) {
  the_xlim <- c(lastpos,firstpos)
} else {
  the_xlim <- c(firstpos,lastpos)
}

# set margin
# b, l, t, r
par(mar=c(6,2.5,1,1))

# open plot (and draw threshold)
plot(c(0),c(0),type=\"l\",                                        
     xlab=\"\",ylab=\"\",
     xlim=the_xlim,ylim=c(0,maxy),
     yaxp=c(0,1,2))

## title inside of plot
legend(\"topleft\",\"$title\",bty=\"n\")


# total reliability
polygon(c(firstpos,firstpos:lastpos,lastpos),c(0,totalrel,0),col=rgb(0.8,0.8,0.9,0.5),lwd=2,border=FALSE)
lines(firstpos:lastpos,totalrel,col=\"blue\",lwd=2)

# plot structure reliability
polygon(c(firstpos,firstpos:lastpos,lastpos),c(0,strrel,0),col=rgb(0.3,0.3,0.5,0.8),lwd=1,border=FALSE)


## draw other signals
signals<-$signals;
signal_sizes<-$signal_sizes;

signal_starts <- 1:$num_signals

signal_starts[1]<-1;
if ($num_signals>1) {
  for (i in 2:$num_signals) {
    signal_starts[i]<-signal_starts[i-1]+signal_sizes[i-1]*2+1;
  }
}

colors <- c(
    rgb(0.6,0.1,0.1,0.9),
    rgb(0.6,0.6,0.1,0.9),
    rgb(0.1,0.6,0.6,0.9),
    rgb(0.6,0.1,0.6,0.9)
);
colors<-c(colors,colors);

if ($num_signals>0) {
  
  for (i in 1:$num_signals) {
    orientation <- signals[signal_starts[i]+signal_sizes[i]*2];
    sig_y  <- maxy-i*anno_space;
    
    for (j in 0:(signal_sizes[i]-1)) {

      sig_x <- c(signals[signal_starts[i]+j*2],signals[signal_starts[i]+j*2+1]);
          
      ## draw arrows
      if (orientation!=0) {
        the_code <- 1+(orientation+1)/2;
        arrows(sig_x[1],sig_y,sig_x[2],sig_y,lwd=4,col=colors[i],code=the_code,angle=20,length=0.15);
      } else {
        lines(sig_x,c(sig_y,sig_y),lwd=4,col=colors[i]);
      }
    }
  }
}

#draw inferred on-signal
hit_color <- rgb(0.1,0.6,0.1,0.9)
  
if ($dont_predict!=1) {
  
  on  <- $on_list;
  off <- $off_list;

  if (length(on)>0) {
    for (i in 1:length(on)) {
      lines(c($offset+on[i],$offset+off[i]-1),c(maxy,maxy),lwd=7,col=hit_color);
    }
  }

  ### draw on/off values
  if ($show_fitonoff) {
    lines(c($offset,$offset+len),c($on_value,$on_value),lty=2,lwd=1)
    lines(c($offset,$offset+len),c($off_value,$off_value),lty=2,lwd=1)
  }
}


signal_names<-$signal_names;

if (length(signal_names)>0 || ($dont_predict!=1)) {
  legend(\"bottom\",c(\"LocARNA\",signal_names),lwd=7,col=c(hit_color,colors),horiz=TRUE,inset=-0.4);
  # ,xpd=TRUE
}
";


if (!$dont_plot) {
    open(R,"|Rscript -");
    print R $rscript;
    close R;
}

if ($write_R_script) {
    open (FILE,">$write_R_script") || die "Cannot write R script to file";
    print FILE $rscript;
    close FILE;
}


## ------------------------------------------------------------
