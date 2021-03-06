/**
 * \file sparse.cc
 *
 * \brief Defines main function of SPARSE
 *
 * Copyright (C) Milad Miladi <miladim(@)informatik.uni-freiburg.de>
 */


#include <iostream>
#include <fstream>
#include <vector>

//#include <math.h>

#include "LocARNA/sequence.hh"
#include "LocARNA/basepairs.hh"
#include "LocARNA/alignment.hh"
#include "LocARNA/aligner_n.hh"
#include "LocARNA/rna_data.hh"
#include "LocARNA/arc_matches.hh"
#include "LocARNA/match_probs.hh"
#include "LocARNA/ribosum.hh"
#include "LocARNA/ribofit.hh"
#include "LocARNA/anchor_constraints.hh"
#include "LocARNA/sequence_annotation.hh"
#include "LocARNA/trace_controller.hh"
#include "LocARNA/multiple_alignment.hh"
#include "LocARNA/sparsification_mapper.hh"
#include "LocARNA/global_stopwatch.hh"
#include "LocARNA/pfold_params.hh"
#include "LocARNA/main_helper.icc"

using namespace std;
using namespace LocARNA;

//! Version string (from configure.ac via autoconf system)
const std::string 
VERSION_STRING = (std::string)PACKAGE_STRING; 

// ------------------------------------------------------------
// Parameter


// ------------------------------------------------------------
//
// Options
//
#include "LocARNA/options.hh"

//! \brief Switch on/off trace back
//! @note never made it into command line
const bool DO_TRACE=true;

//! \brief Structure for command line parameters of locarna
//!
//! Encapsulating all command line parameters in a common structure
//! avoids name conflicts and makes downstream code more informative.
//!
struct command_line_parameters : public MainHelper::std_command_line_parameters {
    double max_uil_length_ratio; // max unpaired in loop length ratio
    double max_bpil_length_ratio; // max base pairs in loop length ratio

    int indel_score_loop; //!< indel extension score

    
    int indel_opening_score; //!< indel opening score
    
    int indel_opening_loop_score; //!< indel opening score for loops

    double prob_unpaired_in_loop_threshold; //!< threshold for prob_unpaired_in_loop
    double prob_basepair_in_loop_threshold; //!< threshold for prob_basepait_in_loop

    bool opt_special_gap_symbols; //!< whether to use special gap
                                  //!symbols in the alignment result

    bool opt_mea_alignment; //!< whether to perform mea alignment
    bool opt_mea_gapcost; //!< whether to use mea gapcost
    int mea_alpha; //!< mea alpha
    int mea_beta; //!< mea beta
    int mea_gamma; //!< mea gamma
    int probability_scale; //!< probability scale

    bool opt_normalized; //!< whether to do normalized alignment
    int normalized_L; //!< normalized_L

    bool opt_track_closing_bp; //!< whether to track right end of a closing basepair



};


//! \brief holds command line parameters of locarna  
command_line_parameters clp;


//! defines command line parameters
option_def my_options[] = {
    {"",0,0,O_SECTION,0,O_NODEFAULT,"","cmd_only"},

    {"help",'h',&clp.opt_help,O_NO_ARG,0,O_NODEFAULT,"","Help"},
    {"galaxy-xml",0,&clp.opt_galaxy_xml,O_NO_ARG,0,O_NODEFAULT,"","Galaxy xml wrapper"},
    {"version",'V',&clp.opt_version,O_NO_ARG,0,O_NODEFAULT,"","Version info"},
    {"verbose",'v',&clp.opt_verbose,O_NO_ARG,0,O_NODEFAULT,"","Verbose"},
    {"quiet",'q',&clp.opt_quiet,O_NO_ARG,0,O_NODEFAULT,"","Quiet"},

    {"",0,0,O_SECTION,0,O_NODEFAULT,"","Scoring_parameters"},

    {"match",'m',0,O_ARG_INT,&clp.match_score,"50","score","Match score"},
    {"mismatch",'M',0,O_ARG_INT,&clp.mismatch_score,"0","score","Mismatch score"},
    {"ribosum-file",0,0,O_ARG_STRING,&clp.ribosum_file,"RIBOSUM85_60","f","Ribosum file"},
    {"use-ribosum",0,0,O_ARG_BOOL,&clp.use_ribosum,"true","bool","Use ribosum scores"},
    {"indel",'i',0,O_ARG_INT,&clp.indel_score,"-350","score","Indel score"},
    {"indel-loop",'i',0,O_ARG_INT,&clp.indel_score_loop,"-350","score","Indel score for loops"},
    {"indel-opening",0,0,O_ARG_INT,&clp.indel_opening_score,"-600","score",
     "Indel opening score"},
    {"indel-opening-loop",0,0,O_ARG_INT,&clp.indel_opening_loop_score,"-900","score",
     "Indel opening score for loops"},
    {"struct-weight",'s',0,O_ARG_INT,&clp.struct_weight,"200","score",
     "Maximal weight of 1/2 arc match"},
    {"exp-prob",'e',&clp.opt_exp_prob,O_ARG_DOUBLE,&clp.exp_prob,O_NODEFAULT,"prob",
     "Expected probability"},
    {"tau",'t',0,O_ARG_INT,&clp.tau_factor,"100","factor","Tau factor in percent"},
    //    {"exclusion",'E',0,O_ARG_INT,&clp.exclusion_score,"0","score","Exclusion weight"},
    //    {"stacking",0,&clp.opt_stacking,O_NO_ARG,0,O_NODEFAULT,"","Use stacking terms (needs stack-probs by RNAfold -p2)"},
    //    {"new-stacking",0,&clp.opt_newstacking,O_NO_ARG,0,O_NODEFAULT,"","Use new stacking terms (needs stack-probs by RNAfold -p2)"},

    /*    {"",0,0,O_SECTION,0,O_NODEFAULT,"","Type of locality"},

          {"struct-local",0,0,O_ARG_BOOL,&clp.struct_local,"false","bool","Structure local"},
          {"sequ-local",0,0,O_ARG_BOOL,&clp.sequ_local,"false","bool","Sequence local"},
          {"free-endgaps",0,0,O_ARG_STRING,&clp.free_endgaps,"----","spec","Whether and which end gaps are free. order: L1,R1,L2,R2"},
          {"normalized",0,&clp.opt_normalized,O_ARG_INT,&clp.normalized_L,"0","L","Normalized local alignment with parameter L"},
    */
    
    {"",0,0,O_SECTION,0,O_NODEFAULT,"","Controlling_output"},

    {"width",'w',0,O_ARG_INT,&clp.output_width,"120","columns","Output width"},
    {"clustal",0,&clp.opt_clustal_out,O_ARG_STRING,&clp.clustal_out,O_NODEFAULT,"file",
     "Clustal output"},
    {"stockholm",0,&clp.opt_stockholm_out,O_ARG_STRING,&clp.stockholm_out,O_NODEFAULT,"file",
     "Stockholm output"},
    {"pp",0,&clp.opt_pp_out,O_ARG_STRING,&clp.pp_out,O_NODEFAULT,"file","PP output"},
    {"alifold-consensus-dp",0,&clp.opt_alifold_consensus_dp,O_NO_ARG,0,O_NODEFAULT,"",
     "Compute consensus dot plot by alifold"},
    {"consensus-structure",0,0,O_ARG_STRING,&clp.cons_struct_type,"alifold","type",
     "Type of consensus structures written to screen and stockholm output [alifold|mea|none]"},
    {"write-structure",0,&clp.opt_write_structure,O_NO_ARG,0,O_NODEFAULT,"",
     "Write guidance structure in output"},
    {"special-gap-symbols",0,&clp.opt_special_gap_symbols,O_NO_ARG,0,O_NODEFAULT,"",
     "Special distinct gap symbols for loop gaps or gaps caused by sparsification"},
    {"stopwatch",0,&clp.opt_stopwatch,O_NO_ARG,0,O_NODEFAULT,"","Print run time information."},

    {"",0,0,O_SECTION,0,O_NODEFAULT,"","Heuristics for speed accuracy trade off"},

    {"min-prob",'p',0,O_ARG_DOUBLE,&clp.min_prob,"0.0005","prob","Minimal probability"},
    {"max-bps-length-ratio",0,0,O_ARG_DOUBLE,&clp.max_bps_length_ratio,"1.3","factor",
     "Maximal ratio of #base pairs divided by sequence length (default: 1.3)"},
    {"max-uil-length-ratio",0,0,O_ARG_DOUBLE,&clp.max_uil_length_ratio,"0.0","factor",
     "Maximal ratio of #unpaired bases in loops divided by sequence length (def: no effect)"},
    {"max-bpil-length-ratio",0,0,O_ARG_DOUBLE,&clp.max_bpil_length_ratio,"0.0","factor",
     "Maximal ratio of #base pairs in loops divided by loop length (def: no effect)"},
    {"max-diff-am",'D',0,O_ARG_INT,&clp.max_diff_am,"30","diff",
     "Maximal difference for sizes of matched arcs"},
    {"max-diff",'d',0,O_ARG_INT,&clp.max_diff,"-1","diff",
     "Maximal difference for alignment traces"},
    {"max-diff-at-am",0,0,O_ARG_INT,&clp.max_diff_at_am,"-1","diff",
     "Maximal difference for alignment traces, only at arc match positions"},
    {"max-diff-aln",0,0,O_ARG_STRING,&clp.max_diff_alignment_file,"","aln file",
     "Maximal difference relative to given alignment (file in clustalw format))"},
    {"max-diff-pw-aln",0,0,O_ARG_STRING,&clp.max_diff_pw_alignment,"","alignment",
     "Maximal difference relative to given alignment (string, delim=AMPERSAND)"},
    {"max-diff-relax",0,&clp.opt_max_diff_relax,O_NO_ARG,0,O_NODEFAULT,"",
     "Relax deviation constraints in multiple aligmnent"},
    {"min-am-prob",'a',0,O_ARG_DOUBLE,&clp.min_am_prob,"0.0005","amprob",
     "Minimal Arc-match probability"},
    {"min-bm-prob",'b',0,O_ARG_DOUBLE,&clp.min_bm_prob,"0.0005","bmprob",
     "Minimal Base-match probability"},
    {"prob-unpaired-in-loop-threshold",0,0,O_ARG_DOUBLE,&clp.prob_unpaired_in_loop_threshold,
     "0.00005","threshold","Threshold for prob_unpaired_in_loop"},
    //todo: is the default threshold value reasonable?
    {"prob-basepair-in-loop-threshold",0,0,O_ARG_DOUBLE,&clp.prob_basepair_in_loop_threshold,
     "0.0001","threshold","Threshold for prob_basepair_in_loop"},
    
    {"",0,0,O_SECTION,0,O_NODEFAULT,"","MEA_score controlling options"},

    {"mea-alignment",0,&clp.opt_mea_alignment,O_NO_ARG,0,O_NODEFAULT,"","Do MEA alignment"},
    {"probcons-file",0,&clp.opt_probcons_file,O_ARG_STRING,&clp.probcons_file,
     O_NODEFAULT,"file","Probcons parameter file"},
    {"match-prob-method",0,0,O_ARG_INT,&clp.match_prob_method,"0","int",
     "Method for computation of match probs"},
    {"temperature",0,0,O_ARG_INT,&clp.temperature,"150","int","Temperature for PF-computation"},
    {"pf-struct-weight",0,0,O_ARG_INT,&clp.pf_struct_weight,"200","weight",
     "Structure weight in PF-computation"},
    {"mea-gapcost",0,&clp.opt_mea_gapcost,O_NO_ARG,0,O_NODEFAULT,"",
     "Use gap cost in mea alignment"},   
    {"mea-alpha",0,0,O_ARG_INT,&clp.mea_alpha,"0","weight","Weight alpha for MEA"},
    {"mea-beta",0,0,O_ARG_INT,&clp.mea_beta,"200","weight","Weight beta for MEA"},
    {"mea-gamma",0,0,O_ARG_INT,&clp.mea_gamma,"100","weight","Weight gamma for MEA"},
    {"probability-scale",0,0,O_ARG_INT,&clp.probability_scale,"10000","scale",
     "Scale for probabilities/resolution of mea score"},
    {"write-match-probs",0,&clp.opt_write_matchprobs,O_ARG_STRING,&clp.matchprobs_file,
     O_NODEFAULT,"file","Write match probs to file (don't align!)"},
    {"read-match-probs",0,&clp.opt_read_matchprobs,O_ARG_STRING,&clp.matchprobs_file,
     O_NODEFAULT,"file","Read match probabilities from file"},
    {"write-arcmatch-scores",0,&clp.opt_write_arcmatch_scores,O_ARG_STRING,
     &clp.arcmatch_scores_file,O_NODEFAULT,"file","Write arcmatch scores (don't align!)"},
    {"read-arcmatch-scores",0,&clp.opt_read_arcmatch_scores,O_ARG_STRING,
     &clp.arcmatch_scores_file,O_NODEFAULT,"file","Read arcmatch scores"},
    {"read-arcmatch-probs",0,&clp.opt_read_arcmatch_probs,O_ARG_STRING,
     &clp.arcmatch_scores_file,O_NODEFAULT,
     "file","Read arcmatch probabilities (weight by mea_beta/100)"},
    
    {"",0,0,O_SECTION,0,O_NODEFAULT,"","Constraints"},

    {"noLP",0,&clp.no_lonely_pairs,O_NO_ARG,0,O_NODEFAULT,"","No lonely pairs"},
    {"maxBPspan",0,0,O_ARG_INT,&clp.max_bp_span,"-1","span","Limit maximum base pair span (default=off)"},

    {"",0,0,O_SECTION_HIDE,0,O_NODEFAULT,"","Hidden Options"},
    // TODO: make ribofit visible
    {"ribofit",0,0,O_ARG_BOOL,&clp.opt_ribofit,"false",
     "bool","Use Ribofit base and arc match scores (overrides ribosum)"},
    

    {"",0,0,O_SECTION,0,O_NODEFAULT,"","Input_files RNA sequences and pair probabilities"},

    {"",0,0,O_ARG_STRING,&clp.fileA,O_NODEFAULT,"input1","Input file 1"},
    {"",0,0,O_ARG_STRING,&clp.fileB,O_NODEFAULT,"input2","Input file 2"},
    {"",0,0,0,0,O_NODEFAULT,"",""}
};


// ------------------------------------------------------------

// ------------------------------------------------------------
// MAIN

/** 
 * \brief Main method of executable locarna
 * 
 * @param argc argument counter
 * @param argv argument vector
 * 
 * @return success
 */
int
main(int argc, char **argv) {
    stopwatch.start("total");

    // make sure that unsupported features are turned off; consider moving
    // to standard clp class
    clp.opt_local_file_output = false;
    clp.opt_pos_output = false;
    clp.opt_local_output = false;
    clp.struct_local=false;
    clp.sequ_local=false;
    clp.free_endgaps="";
    //clp.opt_normalized=0;    
    clp.opt_stacking=false;
    clp.opt_new_stacking=false;

    typedef std::vector<int>::size_type size_type;

    // ------------------------------------------------------------
    // Process options
    bool process_success=process_options(argc,argv,my_options);

    if (clp.opt_help) {
	cout << "sparse - a tool for pairwise fast alignment of RNAs"<<endl<<endl;
	
	//cout << VERSION_STRING<<endl<<endl;

	print_help(argv[0],my_options);

	cout << "Report bugs to <miladim (at) informatik.uni-freiburg.de>."<<endl<<endl;
	return 0;
    }
    
    if (clp.opt_quiet) { clp.opt_verbose=false;} // quiet overrides verbose

    if (clp.opt_galaxy_xml) {
    	print_galaxy_xml((char *)"sparse",my_options);
    	return 0;
    }

    if (clp.opt_version || clp.opt_verbose) {
	cout << "sparse ("<< VERSION_STRING<<")"<<endl;
	if (clp.opt_version) return 0; else cout <<endl;
    }

    if (!process_success) {
	std::cerr << "ERROR --- "
		  <<O_error_msg<<std::endl;
	print_usage(argv[0],my_options);
	return -1;
    }

    if (clp.opt_stopwatch) {
	stopwatch.set_print_on_exit(true);
    }
    
    if (clp.opt_verbose) {
	print_options(my_options);
    }
    

    // --------------------
    //Forbid unsupported option of SPARSE
    if ( clp.struct_local ) {
        std::cerr << "Exclusions is not supported" << std::endl;
        return -1;
    }
   

    //noLP is not supported by sparse recursion but yet useful for calculating probablities with RNAfold
    if( clp.no_lonely_pairs ) {
        // std::cerr << "WARNING: No lonely pairs option is not supported by sparse algortihm" << std::endl;
        //	return -1;
    }
    if( clp.sequ_local )  {
        std::cerr << "Local sequence alignment is not supported" << std::endl;
        return -1;
    }
    if( clp.opt_stacking || clp.opt_new_stacking) {
        std::cerr << "Stacking is not supported" << std::endl;
        return -1;
    }
    /*  if(clp.free_endgaps.compare("----") != 0 ) {
	std::cerr << "Free end gaps is not supported" << std::endl;
	return -1;
        }
    */





    // ------------------------------------------------------------
    // parameter consistency
    if (clp.opt_read_arcmatch_scores && clp.opt_read_arcmatch_probs) {
	std::cerr << "You cannot specify arc match score and probabilities file simultaneously."<<std::endl;
	return -1;
    }
    
    if (clp.probability_scale<=0) {
	std::cerr << "Probability scale must be greater 0."<<std::endl;
	return -1;
    }
    
    if (clp.struct_weight<0) {
	std::cerr << "Structure weight must be greater equal 0."<<std::endl;
	return -1;
    }

    // ----------------------------------------
    // temporarily turn off stacking unless background prob is set
    //
    if (clp.opt_stacking && !clp.opt_exp_prob) {
	std::cerr << "WARNING: stacking turned off. "
		  << "Stacking requires setting a background probability "
		  << "explicitely (option --exp-prob)." << std::endl;
	clp.opt_stacking=false;
    }


    // ----------------------------------------  
    // Ribosum matrix
    //
    RibosumFreq *ribosum;
    Ribofit *ribofit;
    MainHelper::init_ribo_matrix(clp,&ribosum,&ribofit);    
    
    // ------------------------------------------------------------
    // Get input data and generate data objects
    //

    PFoldParams pfparams(clp.no_lonely_pairs,clp.opt_stacking||clp.opt_new_stacking, clp.max_bp_span, 2);
    
    ExtRnaData *rna_dataA=0;
    try {
	rna_dataA = new ExtRnaData(clp.fileA,
				   clp.min_prob,
				   clp.prob_basepair_in_loop_threshold,
				   clp.prob_unpaired_in_loop_threshold,
				   clp.max_bps_length_ratio,
				   clp.max_uil_length_ratio,
				   clp.max_bpil_length_ratio,
				   pfparams);
    } catch (failure &f) {
	std::cerr << "ERROR: failed to read from file "<<clp.fileA <<std::endl
		  << "       "<< f.what() <<std::endl;
	return -1;
    }
    
    ExtRnaData *rna_dataB=0;
    try {
	rna_dataB = new ExtRnaData(clp.fileB,
				   clp.min_prob,
				   clp.prob_basepair_in_loop_threshold,
				   clp.prob_unpaired_in_loop_threshold,
				   clp.max_bps_length_ratio,
				   clp.max_uil_length_ratio,
				   clp.max_bpil_length_ratio,
				   pfparams);
    } catch (failure &f) {
	std::cerr << "ERROR: failed to read from file "<<clp.fileB <<std::endl
		  << "       "<< f.what() <<std::endl;
	if (rna_dataA) delete rna_dataA;
	return -1;
    }
    
    const Sequence &seqA=rna_dataA->sequence();
    const Sequence &seqB=rna_dataB->sequence();

    size_type lenA=seqA.length();
    size_type lenB=seqB.length();


    //---------------------------------------------------------------
    //Anchor constraint alignment is not supported by sparse yet
    if ( ! (seqA.annotation(MultipleAlignment::AnnoType::anchors).single_string()=="") ||
         ! (seqB.annotation(MultipleAlignment::AnnoType::anchors).single_string()=="") ) {
	std::cout << "WARNING sequence constraints found in the input but will be ignored."<<std::endl;

    }
    LocARNA::SequenceAnnotation emptyAnnotation;
    rna_dataA->set_anchors(emptyAnnotation);
    rna_dataB->set_anchors(emptyAnnotation);
    //---------------------------------------------------------------


    // --------------------
    // handle max_diff restriction  
    
    // missing: proper error handling in case that lenA, lenB, and
    // max_diff_pw_alignment/max_diff_alignment_file are incompatible
    
    // do inconsistency checking for max_diff_pw_alignment and max_diff_alignment_file
    //
    if (clp.max_diff_pw_alignment!="" && clp.max_diff_alignment_file!="") {
	std::cerr <<"Cannot simultaneously use both options --max-diff-pw-alignment"
                  <<" and --max-diff-alignment-file."<<std::endl;
	return -1;
    }

    // construct TraceController and check inconsistency for with
    // multiplicity of sequences
    //

    MultipleAlignment *multiple_ref_alignment=NULL;
    
    if (clp.max_diff_alignment_file!="") {
	multiple_ref_alignment = new MultipleAlignment(clp.max_diff_alignment_file);
    } else if (clp.max_diff_pw_alignment!="") {
	if ( seqA.num_of_rows()!=1 || seqB.num_of_rows()!=1 ) {
	    std::cerr << "Cannot use --max-diff-pw-alignemnt for aligning of alignments." 
                      << std::endl;
	    return -1;
	}
	
	std::vector<std::string> alistr;
	split_at_separator(clp.max_diff_pw_alignment,'&',alistr);
	
	if (alistr.size()!=2) {
	    std::cerr << "Invalid argument to --max-diff-pw-alignemnt;"
                      <<" require exactly one '&' separating the alignment strings."
		      << std::endl; 
	    return -1;
	}
    
	if (alistr[0].length() != alistr[1].length()) {
	    std::cerr << "Invalid argument to --max-diff-pw-alignemnt;"
                      <<" alignment strings have unequal lengths."
		      << std::endl; 
	    return -1;
	}
	
	multiple_ref_alignment = new MultipleAlignment(seqA.seqentry(0).name(),
						       seqB.seqentry(0).name(),
						       alistr[0],
						       alistr[1]);
    }

    // if (multiple_ref_alignment) {
    // 	std::cout<<"Reference aligment:"<<std::endl;
    // 	multiple_ref_alignment->print_debug(std::cout);
    // 	std::cout << std::flush;
    // }
    
    TraceController trace_controller(seqA,seqB,multiple_ref_alignment,
                                     clp.max_diff,clp.opt_max_diff_relax);
    
    
    // ------------------------------------------------------------
    // Handle constraints (optionally)
    
    AnchorConstraints 
        seq_constraints(lenA,
                        seqA.annotation(MultipleAlignment::AnnoType::anchors).single_string(),
                        lenB,
                        seqB.annotation(MultipleAlignment::AnnoType::anchors).single_string());
    
    if (clp.opt_verbose) {
	if (! seq_constraints.empty()) {
	    std::cout << "Found sequence constraints."<<std::endl;
	}
    }
    
    // ----------------------------------------
    // construct set of relevant arc matches
    //
    ArcMatches *arc_matches;
    
    // ------------------------------------------------------------
    // handle reading and writing of arcmatch_scores
    //
    // (needed for mea alignment with probabilistic consistency
    // transformation of arc match scores)
    //
    if (clp.opt_read_arcmatch_scores || clp.opt_read_arcmatch_probs) {
	if (clp.opt_verbose) {
	    std::cout << "Read arcmatch scores from file "
                      << clp.arcmatch_scores_file << "." <<std::endl;
	}
	arc_matches = new ArcMatches(seqA,
				     seqB,
				     clp.arcmatch_scores_file,
				     clp.opt_read_arcmatch_probs
				     ? ((clp.mea_beta*clp.probability_scale)/100)
				     : -1,
				     clp.max_diff_am!=-1
				     ? (size_type)clp.max_diff_am
				     : std::max(lenA,lenB),
				     clp.max_diff_at_am!=-1
				     ? (size_type)clp.max_diff_at_am
				     : std::max(lenA,lenB),
				     trace_controller,
				     seq_constraints
				     );
    } else {
	// initialize from RnaData
	arc_matches = new ArcMatches(*rna_dataA,
				     *rna_dataB,
				     clp.min_prob,
				     clp.max_diff_am!=-1
				     ? (size_type)clp.max_diff_am
				     : std::max(lenA,lenB),
				     clp.max_diff_at_am!=-1
				     ? (size_type)clp.max_diff_at_am
				     : std::max(lenA,lenB),
				     trace_controller,
				     seq_constraints
				     );
    }
    
    const BasePairs &bpsA = arc_matches->get_base_pairsA();
    const BasePairs &bpsB = arc_matches->get_base_pairsB();
    
    // ----------------------------------------
    // report on input in verbose mode
    if (clp.opt_verbose) MainHelper::report_input(seqA,seqB,*arc_matches);

	std::cout << "Sequence B: "<<std::endl;
	seqB.write(cout);
	std::cout<<" (Length:"<< seqB.length()<<", Basepairs:"<<bpsB.num_bps() << ")" <<std::endl;

	cout <<std::endl 
	     <<"Base Pair Matches: "<<arc_matches->num_arc_matches() << "." <<std::endl;
	// cout << "Base Identity: "<<(seq_identity(seqA,seqB)*100)<<endl; 

    // construct sparsification mapper for seqs A,B
    SparsificationMapper mapperA(bpsA, *rna_dataA, clp.prob_unpaired_in_loop_threshold,
                                 clp.prob_basepair_in_loop_threshold, true);
    SparsificationMapper mapperB(bpsB, *rna_dataB, clp.prob_unpaired_in_loop_threshold,
                                 clp.prob_basepair_in_loop_threshold, true);

    //TODO: It is inefficient to create mapper_arcsX, if track closing pair is not enabled
	//construct mappers where right_sdj list is indexed by arcIndex
	SparsificationMapper mapper_arcsA(bpsA, *rna_dataA, clp.prob_unpaired_in_loop_threshold,
			clp.prob_basepair_in_loop_threshold, false);
	SparsificationMapper mapper_arcsB(bpsB, *rna_dataB, clp.prob_unpaired_in_loop_threshold,
			clp.prob_basepair_in_loop_threshold, false);

    // ------------------------------------------------------------
    // Sequence match probabilities (for MEA-Alignment)
    //
    // perform parameter consistency checks
    if (clp.opt_read_matchprobs && !clp.opt_mea_alignment) {
        std::cerr << "Warning: clp.opt_read_matchprobs ignored for non-mea alignment.\n"; 
    }
    if ( (clp.opt_write_matchprobs || clp.opt_mea_alignment)
         && ribosum==NULL && ribofit==NULL
         ) {
        std::cerr << "ERROR: Ribosum/fit is required for mea_alignment"
                  << " and computing matchprobs."<<std::endl;
        exit(-1);
    }
    //
    MatchProbs *match_probs=0L;
    if (clp.opt_write_matchprobs || clp.opt_mea_alignment) {
        match_probs = MainHelper::init_match_probs(clp,rna_dataA,rna_dataB,ribosum,ribofit);
    }
    if (clp.opt_write_matchprobs) {
        MainHelper::write_match_probs(clp, match_probs);
        if (!clp.opt_write_arcmatch_scores) { return 0; } // return from main()
    }
    //

    // ----------------------------------------
    // construct scoring

    // Scoring Parameter
    //        
    double my_exp_probA = clp.opt_exp_prob?clp.exp_prob:prob_exp_f(lenA);
    double my_exp_probB = clp.opt_exp_prob?clp.exp_prob:prob_exp_f(lenB);
    //
    ScoringParams 
        scoring_params(clp.match_score,
                       clp.mismatch_score,
                       // In true mea alignment gaps are only 
                       // scored for computing base match probs.
                       // Consequently, we set the indel and indel opening cost to 0
                       // for the case of mea alignment!
                       (clp.opt_mea_alignment && !clp.opt_mea_gapcost)
                       ?0
                       :clp.indel_score * (clp.opt_mea_gapcost?clp.probability_scale/100:1),
                       (clp.opt_mea_alignment && !clp.opt_mea_gapcost)
                       ?0
                       :(clp.indel_score_loop 
                         * (clp.opt_mea_gapcost?clp.probability_scale/100:1)),
                       (clp.opt_mea_alignment && !clp.opt_mea_gapcost)
                       ?0
                       :(clp.indel_opening_score 
                         * (clp.opt_mea_gapcost?clp.probability_scale/100:1)),
                       (clp.opt_mea_alignment && !clp.opt_mea_gapcost)
                       ?0
                       :(clp.indel_opening_loop_score 
                         * (clp.opt_mea_gapcost?clp.probability_scale/100:1)),
                       ribosum,
                       ribofit,
                       0, //unpaired_weight
                       clp.struct_weight,
                       clp.tau_factor,
                       clp.exclusion_score,
                       my_exp_probA,
                       my_exp_probB,
                       clp.temperature,
                       clp.opt_stacking,
                       clp.opt_new_stacking,
                       clp.opt_mea_alignment,
                       clp.mea_alpha,
                       clp.mea_beta,
                       clp.mea_gamma,
                       clp.probability_scale
                       );



    Scoring scoring(seqA,
		    seqB,
		    *rna_dataA,
		    *rna_dataB,
		    *arc_matches,
		    match_probs,
		    scoring_params,
		    false // no Boltzmann weights
		    );    

    if (clp.opt_write_arcmatch_scores) {
	if (clp.opt_verbose) {
	    std::cout << "Write arcmatch scores to file "
                      << clp.arcmatch_scores_file<<" and exit."<<std::endl;
	}
	arc_matches->write_arcmatch_scores(clp.arcmatch_scores_file,scoring);
	return 0;
    }
        

    // ------------------------------------------------------------
    // Computation of the alignment score
    //

    // initialize aligner object, which does the alignment computation
    AlignerN aligner = AlignerN::create()
    . sparsification_mapperA(mapperA)
	. sparsification_mapperB(mapperB)
	. sparsification_mapper_arcsA(mapper_arcsA)
	. sparsification_mapper_arcsB(mapper_arcsB)
	. seqA(seqA)
	. seqB(seqB)
	. arc_matches(*arc_matches)
	. scoring(scoring)
	//. no_lonely_pairs(clp.no_lonely_pairs)
	. no_lonely_pairs(false) // ignore no lonely pairs in alignment algo
	. struct_local(clp.struct_local)
	. sequ_local(clp.sequ_local)
	. free_endgaps(clp.free_endgaps)
	. max_diff_am(clp.max_diff_am)
	. max_diff_at_am(clp.max_diff_at_am)
	. trace_controller(trace_controller)
	. min_am_prob(clp.min_am_prob)
	. min_bm_prob(clp.min_bm_prob)
	. stacking(clp.opt_stacking || clp.opt_new_stacking)
	. track_closing_bp(clp.opt_track_closing_bp)
	. constraints(seq_constraints);


   
    
    infty_score_t score;

    // otherwise compute the best alignment
    score = aligner.align();
    
    // ----------------------------------------
    // report score
    //
    if (!clp.opt_quiet) {
        std::cout << "Score: "<<score<<std::endl<<std::endl;
    }

    // ------------------------------------------------------------
    // Traceback
    //
    if (DO_TRACE) {
	    
	if (clp.opt_verbose) {
	    std::cout << "Traceback."<<std::endl;
	}
	
	aligner.trace();

    }

    bool return_code=0;
    
    if (DO_TRACE) { // if we did a trace (one way or
        // the other)

        // ----------------------------------------
	// write alignment in different output formats
	//
	const Alignment &alignment = aligner.get_alignment();
        
        std::string consensus_structure=""; 
        
	RnaData *consensus =
            MainHelper::consensus(clp,
                                  pfparams,
                                  my_exp_probA, my_exp_probB,
                                  rna_dataA, rna_dataB,
                                  alignment,
                                  consensus_structure);
        
        return_code = MainHelper::write_alignment(clp,
                                                  score,
                                                  consensus_structure,
                                                  consensus,
                                                  alignment,
                                                  multiple_ref_alignment);
        
        // ----------------------------------------
        // write alignment to screen
	
        if (!clp.opt_quiet) {
            MultipleAlignment ma(alignment,clp.opt_local_output,clp.opt_special_gap_symbols);

            if (clp.opt_write_structure) {
                // annotate multiple alignment with structures
                std::string structureA=alignment.dot_bracket_structureA(clp.opt_local_output);
                std::string structureB=alignment.dot_bracket_structureB(clp.opt_local_output);
                ma.prepend(MultipleAlignment::SeqEntry("",structureA));
                ma.append(MultipleAlignment::SeqEntry("",structureB));
            }

            if (consensus_structure!="") {
                ma.append(MultipleAlignment::SeqEntry(clp.cons_struct_type,
                                                      consensus_structure));
            }
	    
            ma.write(std::cout,clp.output_width,MultipleAlignment::FormatType::CLUSTAL);
		
            std::cout<<endl;
	}
        
        if (consensus) { delete consensus; }

	    
    }
    
    if (match_probs) delete match_probs;
    delete arc_matches;
    if (multiple_ref_alignment) delete multiple_ref_alignment;
    if (ribosum) delete ribosum;
    if (ribofit) delete ribofit;
    
    delete rna_dataA;
    delete rna_dataB;
      
    stopwatch.stop("total");

    // ----------------------------------------
    // DONE
    return return_code;
}
