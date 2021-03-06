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
#include "LocARNA/aligner_nn.hh"
#include "LocARNA/rna_data.hh"
#include "LocARNA/arc_matches.hh"
#include "LocARNA/match_probs.hh"
#include "LocARNA/ribosum.hh"
#include "LocARNA/ribofit.hh"
#include "LocARNA/anchor_constraints.hh"
#include "LocARNA/sequence_annotation.hh"
#include "LocARNA/trace_controller.hh"
#include "LocARNA/ribosum85_60.icc"
#include "LocARNA/multiple_alignment.hh"
#include "LocARNA/sparsification_mapper.hh"
#include "LocARNA/global_stopwatch.hh"
#include "LocARNA/pfold_params.hh"

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
struct command_line_parameters {
    //! only pairs with a probability of at least min_prob are taken into account
    double min_prob; 

    //! maximal ratio of number of base pairs divided by sequence
    //! length. This serves as a second filter on the "significant"
    //! base pairs.
    double max_bps_length_ratio;

    double max_uil_length_ratio; // max unpaired in loop length ratio
    double max_bpil_length_ratio; // max base pairs in loop length ratio

    int match_score; //!< match score
    
    int mismatch_score; //!< mismatch score
    
    int indel_score; //!< indel extension score

    int indel_score_loop; //!< indel extension score
    
    int indel_opening_score; //!< indel opening score
    
    int indel_opening_loop_score; //!< indel opening score for loops

    int temperature; //!< temperature
    
    int struct_weight; //!< structure weight

    //! contribution of sequence similarity in an arc match (in percent)
    int tau_factor;

    bool no_lonely_pairs; //!< no lonely pairs option

    //! allow exclusions for maximizing alignment of connected substructures
    bool struct_local;

    bool sequ_local; //!< maximize alignment of subsequences

    //! specification of free end gaps, order left end sequence 1,
    //! right 1, left 2, right 2 e.g. "+---" allows free end gaps at
    //! the left end of the first alignment string ; "----" forbids
    //! free end gaps
    std::string free_endgaps;
    
    //! maximal difference for positions of alignment
    //! traces (only used for ends of arcs)
    int max_diff; 
    
    //! maximal difference between two arc ends, -1 is off
    int max_diff_am;

    //! maximal difference for alignment traces, at arc match
    //! positions
    int max_diff_at_am;

    //! pairwise reference alignment for max-diff heuristic,
    //!separator &
    std::string max_diff_pw_alignment;
    
    //! reference alignment for max-diff heuristic, name of clustalw
    //! format file
    std::string max_diff_alignment_file;

    //! use relaxed variant of max diff with reference alignment
    bool opt_max_diff_relax; 

    //! Score contribution per exclusion
    //! set to zero for unrestricted structure locality
    int exclusion_score; 
    
    //! expected probability of a base pair (null-model)
    double exp_prob;
    
    //! expected probability given?
    bool opt_exp_prob;

    //! width of alignment output
    int output_width;

    // ------------------------------------------------------------
    // File arguments
    
    //! first input file 
    std::string fileA;
    
    //! second input file
    std::string fileB;

    std::string clustal_out; //!< name of clustal output file

    bool opt_clustal_out; //!< whether to write clustal output to file

    std::string pp_out; //!< name of pp output file
    
    bool opt_pp_out; //!< whether to write pp output to file
    
    bool opt_help; //!< whether to print help
    bool opt_galaxy_xml; //!< whether to print a galaxy xml wrapper for the parameters
    bool opt_version; //!< whether to print version
    bool opt_verbose; //!< whether to print verbose output
    bool opt_local_output; //!< whether to use local output
    bool opt_pos_output; //!< whether to output positions

    bool opt_write_structure; //!< whether to write structure
    bool opt_special_gap_symbols; //!< whether to use special gap symbols in the alignment result
    bool opt_stopwatch; //!< whether to print verbose output

    bool opt_stacking; //!< whether to use stacking scores
    bool opt_new_stacking; //!< whether to use new stacking scores

    bool opt_track_closing_bp; //!< whether to track right end of a closing basepair
    bool opt_use_conditional_scoring; //!< whether to use the conditional probability scoring
    int opt_multiloop_deletion; //!< whether to allow aligning an entire branch of a multiloop to gap

    std::string ribosum_file; //!< ribosum_file
    bool use_ribosum; //!< use_ribosum

    bool opt_ribofit; //!< ribofit

    bool opt_probcons_file; //!< whether to probcons_file
    std::string probcons_file; //!< probcons_file

    bool opt_mea_alignment; //!< whether to mea_alignment

    bool opt_write_matchprobs; //!< whether to write_matchprobs
    bool opt_read_matchprobs; //!< whether to read_matchprobs
    std::string matchprobs_file; //!< matchprobs_file

    bool opt_write_arcmatch_scores; //!< whether to write_arcmatch_scores

    bool opt_read_arcmatch_scores; //!< whether to read arcmatch scores
    bool opt_read_arcmatch_probs; //!< whether to read arcmatch probabilities
    std::string arcmatch_scores_file; //!< arcmatch scores file

    int match_prob_method; //!< method for computing match probabilities

    double min_am_prob; //!< only matched arc-pair with a probability of at least min_am_prob are taken into account
    double min_bm_prob; //!< only matched base-pair with a probability of at least min_bm_prob are taken into account

    bool opt_subopt; //!< find suboptimal solution (either k-best or all solutions better than a threshold)

    //    int kbest_k; //!< kbest_k
    //    int subopt_threshold; //!< subopt_threshold

    std::string seq_anchors_A; //!< sequence anchors A
    std::string seq_anchors_B; //!< sequence anchors B

    bool opt_ignore_constraints; //!< whether to ignore_constraints

    int pf_struct_weight; //!< pf_struct_weight

    bool opt_mea_gapcost; //!< whether to use mea gapcost
    int mea_alpha; //!< mea alpha
    int mea_beta; //!< mea beta
    int mea_gamma; //!< mea gamma
    int probability_scale; //!< probability scale

    bool opt_normalized; //!< whether to do normalized alignment
    int normalized_L; //!< normalized_L

    double prob_unpaired_in_loop_threshold; //!< threshold for prob_unpaired_in_loop
    double prob_basepair_in_loop_threshold; //!< threshold for prob_basepait_in_loop

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

    {"",0,0,O_SECTION,0,O_NODEFAULT,"","Scoring_parameters"},

    {"match",'m',0,O_ARG_INT,&clp.match_score,"50","score","Match score"},
    {"mismatch",'M',0,O_ARG_INT,&clp.mismatch_score,"0","score","Mismatch score"},
    {"ribosum-file",0,0,O_ARG_STRING,&clp.ribosum_file,"RIBOSUM85_60","f","Ribosum file"},
    {"use-ribosum",0,0,O_ARG_BOOL,&clp.use_ribosum,"true","bool","Use ribosum scores"},
    {"indel",'i',0,O_ARG_INT,&clp.indel_score,"-350","score","Indel score"},
    {"indel-loop",'i',0,O_ARG_INT,&clp.indel_score_loop,"-350","score","Indel score for loops"},
    {"indel-opening",0,0,O_ARG_INT,&clp.indel_opening_score,"-600","score","Indel opening score"},
    {"indel-opening-loop",0,0,O_ARG_INT,&clp.indel_opening_loop_score,"-900","score","Indel opening score for loops"},
    {"struct-weight",'s',0,O_ARG_INT,&clp.struct_weight,"200","score","Maximal weight of 1/2 arc match"},
    {"exp-prob",'e',&clp.opt_exp_prob,O_ARG_DOUBLE,&clp.exp_prob,O_NODEFAULT,"prob","Expected probability"},
    {"tau",'t',0,O_ARG_INT,&clp.tau_factor,"100","factor","Tau factor in percent"},

    {"track-closing-bp",0,&clp.opt_track_closing_bp,O_NO_ARG,0,O_NODEFAULT,"","Track right end of a closing basepair "},
    {"use-conditional-scoring",0,&clp.opt_use_conditional_scoring,O_NO_ARG,0,O_NODEFAULT,"","Use conditional probability scoring "},
    {"multiloop-deletion",0,0, O_ARG_INT,&clp.opt_multiloop_deletion,"0","diff","Maximum allowed length of a  multiloop branch to be aligned gap, "
    		"value 0 disables computation "},

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
    {"clustal",0,&clp.opt_clustal_out,O_ARG_STRING,&clp.clustal_out,O_NODEFAULT,"file","Clustal output"},
    {"pp",0,&clp.opt_pp_out,O_ARG_STRING,&clp.pp_out,O_NODEFAULT,"file","PP output"},
    
    //    {"local-output",'L',&clp.opt_local_output,O_NO_ARG,0,O_NODEFAULT,"","Output only local sub-alignment"},
    //    {"pos-output",'P',&clp.opt_pos_output,O_NO_ARG,0,O_NODEFAULT,"","Output only local sub-alignment positions"},
    {"write-structure",0,&clp.opt_write_structure,O_NO_ARG,0,O_NODEFAULT,"","Write guidance structure in output"},
    {"special-gap-symbols",0,&clp.opt_special_gap_symbols,O_NO_ARG,0,O_NODEFAULT,"","Special distinct gap symbols for loop gaps or gaps caused by sparsification"},

    {"stopwatch",0,&clp.opt_stopwatch,O_NO_ARG,0,O_NODEFAULT,"","Print run time information."},

    {"",0,0,O_SECTION,0,O_NODEFAULT,"","Heuristics for speed accuracy trade off"},

    {"min-prob",'p',0,O_ARG_DOUBLE,&clp.min_prob,"0.0005","prob","Minimal probability"},
    {"max-bps-length-ratio",0,0,O_ARG_DOUBLE,&clp.max_bps_length_ratio,"1.3","factor","Maximal ratio of #base pairs divided by sequence length (default: 1.3)"},
    {"max-uil-length-ratio",0,0,O_ARG_DOUBLE,&clp.max_uil_length_ratio,"0.0","factor","Maximal ratio of #unpaired bases in loops divided by sequence length (default: no effect)"},
    {"max-bpil-length-ratio",0,0,O_ARG_DOUBLE,&clp.max_bpil_length_ratio,"0.0","factor","Maximal ratio of #base pairs in loops divided by loop length (default: no effect)"},
    {"max-diff-am",'D',0,O_ARG_INT,&clp.max_diff_am,"30","diff","Maximal difference for sizes of matched arcs"},
    {"max-diff",'d',0,O_ARG_INT,&clp.max_diff,"-1","diff","Maximal difference for alignment traces"},
    {"max-diff-at-am",0,0,O_ARG_INT,&clp.max_diff_at_am,"-1","diff","Maximal difference for alignment traces, only at arc match positions"},
    {"max-diff-aln",0,0,O_ARG_STRING,&clp.max_diff_alignment_file,"","aln file","Maximal difference relative to given alignment (file in clustalw format))"},
    {"max-diff-pw-aln",0,0,O_ARG_STRING,&clp.max_diff_pw_alignment,"","alignment","Maximal difference relative to given alignment (string, delim=AMPERSAND)"},
    {"max-diff-relax",0,&clp.opt_max_diff_relax,O_NO_ARG,0,O_NODEFAULT,"","Relax deviation constraints in multiple aligmnent"},
    {"min-am-prob",'a',0,O_ARG_DOUBLE,&clp.min_am_prob,"0.0005","amprob","Minimal Arc-match probability"},
    {"min-bm-prob",'b',0,O_ARG_DOUBLE,&clp.min_bm_prob,"0.0005","bmprob","Minimal Base-match probability"},
    {"prob-unpaired-in-loop-threshold",0,0,O_ARG_DOUBLE,&clp.prob_unpaired_in_loop_threshold,"0.00005","threshold","Threshold for prob_unpaired_in_loop"},
    {"prob-basepair-in-loop-threshold",0,0,O_ARG_DOUBLE,&clp.prob_basepair_in_loop_threshold,"0.0001","threshold","Threshold for prob_basepair_in_loop"}, //todo: is the default threshold value reasonable?
    
    //    {"",0,0,O_SECTION,0,O_NODEFAULT,"","Special sauce options"},
    //    {"kbest",0,&clp.opt_subopt,O_ARG_INT,&clp.kbest_k,"-1","k","Enumerate k-best alignments"},
    //    {"better",0,&clp.opt_subopt,O_ARG_INT,&clp.subopt_threshold,"-1000000","t","Enumerate alignments better threshold t"},
    
    {"",0,0,O_SECTION,0,O_NODEFAULT,"","MEA_score controlling options"},

    {"mea-alignment",0,&clp.opt_mea_alignment,O_NO_ARG,0,O_NODEFAULT,"","Do MEA alignment"},
    {"probcons-file",0,&clp.opt_probcons_file,O_ARG_STRING,&clp.probcons_file,O_NODEFAULT,"file","Probcons parameter file"},

    {"match-prob-method",0,0,O_ARG_INT,&clp.match_prob_method,"0","int","Method for computation of match probs"},
    {"temperature",0,0,O_ARG_INT,&clp.temperature,"150","int","Temperature for PF-computation"},
    {"pf-struct-weight",0,0,O_ARG_INT,&clp.pf_struct_weight,"200","weight","Structure weight in PF-computation"},

    {"mea-gapcost",0,&clp.opt_mea_gapcost,O_NO_ARG,0,O_NODEFAULT,"","Use gap cost in mea alignment"},   
    {"mea-alpha",0,0,O_ARG_INT,&clp.mea_alpha,"0","weight","Weight alpha for MEA"},
    {"mea-beta",0,0,O_ARG_INT,&clp.mea_beta,"200","weight","Weight beta for MEA"},
    {"mea-gamma",0,0,O_ARG_INT,&clp.mea_gamma,"100","weight","Weight gamma for MEA"},
    {"probability-scale",0,0,O_ARG_INT,&clp.probability_scale,"10000","scale","Scale for probabilities/resolution of mea score"},

    {"write-match-probs",0,&clp.opt_write_matchprobs,O_ARG_STRING,&clp.matchprobs_file,O_NODEFAULT,"file","Write match probs to file (don't align!)"},
    {"read-match-probs",0,&clp.opt_read_matchprobs,O_ARG_STRING,&clp.matchprobs_file,O_NODEFAULT,"file","Read match probabilities from file"},

    {"write-arcmatch-scores",0,&clp.opt_write_arcmatch_scores,O_ARG_STRING,&clp.arcmatch_scores_file,O_NODEFAULT,"file","Write arcmatch scores (don't align!)"},
    {"read-arcmatch-scores",0,&clp.opt_read_arcmatch_scores,O_ARG_STRING,&clp.arcmatch_scores_file,O_NODEFAULT,"file","Read arcmatch scores"},
    {"read-arcmatch-probs",0,&clp.opt_read_arcmatch_probs,O_ARG_STRING,&clp.arcmatch_scores_file,O_NODEFAULT,"file","Read arcmatch probabilities (weight by mea_beta/100)"},
    
    {"",0,0,O_SECTION,0,O_NODEFAULT,"","Constraints"},

    {"noLP",0,&clp.no_lonely_pairs,O_NO_ARG,0,O_NODEFAULT,"","No lonely pairs"},
    //    {"ignore-constraints",0,&clp.opt_ignore_constraints,O_NO_ARG,0,O_NODEFAULT,"","Ignore constraints in pp-file"},
    

    {"",0,0,O_SECTION_HIDE,0,O_NODEFAULT,"","Hidden Options"},
    // TODO: relocate ribofit
    {"ribofit",0,0,O_ARG_BOOL,&clp.opt_ribofit,"false","bool","Use Ribofit base and arc match scores (overrides ribosum)"},

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
	printf("USAGE: ");
	print_usage(argv[0],my_options);
	printf("\n");
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
    if ( clp.struct_local )
        {
            std::cerr << "Exclusions is not supported" << std::endl;
            return -1;
        }
   

    //noLP is not supported by sparse recursion but yet useful for calculating probablities with RNAfold
    if( clp.no_lonely_pairs )
        {
            std::cerr << "WARNING: No lonely pairs option is not supported by sparse algortihm" << std::endl;
            //	return -1;
        }
    if( clp.sequ_local )
        {
            std::cerr << "Local sequence alignment is not supported" << std::endl;
            return -1;
        }
    if( clp.opt_stacking || clp.opt_new_stacking)
        {
            std::cerr << "Stacking is not supported" << std::endl;
            return -1;
        }
    /*  if(clp.free_endgaps.compare("----") != 0 )
        {
	std::cerr << "Free end gaps is not supported" << std::endl;
	return -1;
        }
    */
    if (clp.opt_subopt) {
    	std::cerr
            << "ERROR: suboptimal alignment not supported."
            <<std::endl;
        return -1;
    }




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

    //
	if (clp.opt_use_conditional_scoring && !clp.opt_track_closing_bp ) {
	std::cerr << "Error: conditonal scoring only works if track_closing_bp is enabled" << std::endl;
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
    RibosumFreq *ribosum=NULL;
    Ribofit *ribofit=NULL;
    
    if (clp.opt_ribofit) {
	ribofit = new Ribofit_will2014;
    }

    if (clp.use_ribosum) {
	if (clp.ribosum_file == "RIBOSUM85_60") {
	    if (clp.opt_verbose) {
		std::cout <<"Use built-in ribosum."<<std::endl;
	    }
            ribosum = new Ribosum85_60;
	} else {
	    ribosum = new RibosumFreq(clp.ribosum_file);
	}
	/*
	  std::cout <<" A: "<< ribosum->base_nonstruct_prob('A')
	  <<" C: "<< ribosum->base_nonstruct_prob('C')
	  <<" G: "<< ribosum->base_nonstruct_prob('G')
	  <<" U: "<< ribosum->base_nonstruct_prob('U')
	  << std::endl;
	
	  ribosum->print_basematch_scores_corrected();
	*/
    }
        
    
    // ------------------------------------------------------------
    // Get input data and generate data objects
    //

    PFoldParams pfparams(clp.no_lonely_pairs,clp.opt_stacking||clp.opt_new_stacking,-1, 2); //bpspan disabled
    
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
    
    // missing: proper error handling in case that lenA, lenB, and max_diff_pw_alignment/max_diff_alignment_file are incompatible 
    
    // do inconsistency checking for max_diff_pw_alignment and max_diff_alignment_file
    //
    if (clp.max_diff_pw_alignment!="" && clp.max_diff_alignment_file!="") {
	std::cerr <<"Cannot simultaneously use both options --max-diff-pw-alignemnt and --max-diff-alignment-file."<<std::endl;
	return -1;
    }

    // construct TraceController and check inconsistency for with multiplicity of sequences
    //

    MultipleAlignment *multiple_ref_alignment=NULL;
    
    if (clp.max_diff_alignment_file!="") {
	multiple_ref_alignment = new MultipleAlignment(clp.max_diff_alignment_file);
    } else if (clp.max_diff_pw_alignment!="") {
	if ( seqA.num_of_rows()!=1 || seqB.num_of_rows()!=1 ) {
	    std::cerr << "Cannot use --max-diff-pw-alignemnt for aligning of alignments." << std::endl;
	    return -1;
	}
	
	std::vector<std::string> alistr;
	split_at_separator(clp.max_diff_pw_alignment,'&',alistr);
	
	if (alistr.size()!=2) {
	    std::cerr << "Invalid argument to --max-diff-pw-alignemnt; require exactly one '&' separating the alignment strings."
		      << std::endl; 
	    return -1;
	}
    
	if (alistr[0].length() != alistr[1].length()) {
	    std::cerr << "Invalid argument to --max-diff-pw-alignemnt; alignment strings have unequal lengths."
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
    
    TraceController trace_controller(seqA,seqB,multiple_ref_alignment,clp.max_diff,clp.opt_max_diff_relax);
    
    
    // ------------------------------------------------------------
    // Handle constraints (optionally)
    
    AnchorConstraints seq_constraints(lenA,
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
    // (needed for mea alignment with probabilistic consistency transformation of arc match scores)
    //
    if (clp.opt_read_arcmatch_scores || clp.opt_read_arcmatch_probs) {
	if (clp.opt_verbose) {
	    std::cout << "Read arcmatch scores from file " << clp.arcmatch_scores_file << "." <<std::endl;
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
    if (clp.opt_verbose) {
	std::cout << "Sequence A: "<<std::endl;
	seqA.write(cout);
	std::cout<<" (Length:"<< seqA.length()<<", Basepairs:"<<bpsA.num_bps() << ")" <<std::endl;

	std::cout << "Sequence B: "<<std::endl;
	seqB.write(cout);
	std::cout<<" (Length:"<< seqB.length()<<", Basepairs:"<<bpsB.num_bps() << ")" <<std::endl;

	cout <<std::endl 
	     <<"Base Pair Matches: "<<arc_matches->num_arc_matches() << "." <<std::endl;
	// cout << "Base Identity: "<<(seq_identity(seqA,seqB)*100)<<endl; 
    }

    // construct sparsification mapper for seqs A,B
//    SparsificationMapper mapperA(bpsA, *rna_dataA, clp.prob_unpaired_in_loop_threshold, clp.prob_basepair_in_loop_threshold, true);
//    SparsificationMapper mapperB(bpsB, *rna_dataB, clp.prob_unpaired_in_loop_threshold, clp.prob_basepair_in_loop_threshold, true);

	//TODO: It is inefficient to create mapper_arcsX, if track closing pair is not enabled
    //construct mappers where right_sdj list is indexed by arcIndex
	SparsificationMapper mapper_arcsA(bpsA, *rna_dataA, clp.prob_unpaired_in_loop_threshold, clp.prob_basepair_in_loop_threshold, false);
	SparsificationMapper mapper_arcsB(bpsB, *rna_dataB, clp.prob_unpaired_in_loop_threshold, clp.prob_basepair_in_loop_threshold, false);


    // ------------------------------------------------------------
    // Sequence match probabilities (for MEA-Alignment)
    //
    MatchProbs *match_probs=0L;

    if (clp.opt_read_matchprobs && !clp.opt_mea_alignment) {
	std::cerr << "Warning: clp.opt_read_matchprobs ignored for non-mea alignment.\n"; 
    }

    if (clp.opt_write_matchprobs || clp.opt_mea_alignment) {
	match_probs = new MatchProbs;

	if (!clp.use_ribosum) {
	    std::cerr << "WARNING: Ribosum scoring used for mea_alignment and computing matchprobs."<<std::endl;
	}

	if (clp.opt_read_matchprobs) {
	    match_probs->read_sparse(clp.matchprobs_file,seqA.length(),seqB.length());
	} else {
	    if (clp.match_prob_method==1) {
		if (!clp.opt_probcons_file) {
		    std::cerr << "Probcons parameter file required for pairHMM-style computation"
			      <<" of basematch probabilities."<<std::endl;
		    print_usage(argv[0],my_options);
		    std::cerr << std::endl;
		    return -1;
		}
		if (clp.opt_verbose) {
		    std::cout << "Compute match probabilities using pairHMM."<<std::endl; 
		}

		match_probs->pairHMM_probs(seqA,seqB,clp.probcons_file);
	    } else {
		bool sl=clp.sequ_local;
		if (clp.match_prob_method==2) sl=true;
		if (clp.match_prob_method==3) sl=false;

		if (clp.opt_verbose) {
		    std::cout << "Compute match probabilities using PF sequence alignment."<<std::endl; 
		}

		match_probs->pf_probs(*rna_dataA,
				      *rna_dataB,
				      ribosum->get_basematch_scores(),
				      ribosum->alphabet(),
				      clp.indel_opening_score/100.0,
				      clp.indel_score/100.0,
				      clp.pf_struct_weight/100.0,
				      clp.temperature/100.0,
				      sl);
	    }
	}

	if (clp.opt_write_matchprobs) {
	    if (clp.opt_verbose) {
		std::cout << "Write match probabilities to file "<<clp.matchprobs_file<<"."<<std::endl; 
	    }

	    match_probs->write_sparse(clp.matchprobs_file,1.0/clp.probability_scale);
	    if (!clp.opt_write_arcmatch_scores) return 0; // else we exit there!
	}
    }
   

    // ----------------------------------------
    // construct scoring

    // Scoring Parameter
    //        
    double my_exp_probA = clp.opt_exp_prob?clp.exp_prob:prob_exp_f(lenA);
    double my_exp_probB = clp.opt_exp_prob?clp.exp_prob:prob_exp_f(lenB);
    //
    ScoringParams scoring_params(clp.match_score,
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
				 :clp.indel_score_loop * (clp.opt_mea_gapcost?clp.probability_scale/100:1),
				 (clp.opt_mea_alignment && !clp.opt_mea_gapcost)
				 ?0
				 :clp.indel_opening_score * (clp.opt_mea_gapcost?clp.probability_scale/100:1),
				 (clp.opt_mea_alignment && !clp.opt_mea_gapcost)
				 ?0
				 :clp.indel_opening_loop_score * (clp.opt_mea_gapcost?clp.probability_scale/100:1),
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
		    false, // no Boltzmann weights
		    clp.opt_use_conditional_scoring
		    );    

    if (clp.opt_write_arcmatch_scores) {
	if (clp.opt_verbose) {
	    std::cout << "Write arcmatch scores to file "<< clp.arcmatch_scores_file<<" and exit."<<std::endl;
	}
	arc_matches->write_arcmatch_scores(clp.arcmatch_scores_file,scoring);
	return 0;
    }
        

    // ------------------------------------------------------------
    // Computation of the alignment score
    //

    // initialize aligner object, which does the alignment computation
    AlignerNN aligner = AlignerNN::create()
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
	. multiloop_deletion(clp.opt_multiloop_deletion)

	. constraints(seq_constraints);


    
    // enumerate suboptimal alignments (using interval splitting)
    if (clp.opt_subopt) {
    	std::cerr << "ERROR: suboptimal alignment not supported." << std::endl;

	/*aligner.suboptimal(clp.kbest_k,
          clp.subopt_threshold,
          clp.opt_normalized,
          clp.normalized_L,
          clp.output_width,
          clp.opt_verbose,
          clp.opt_local_output,
          clp.opt_pos_output,
          clp.opt_write_structure
          );
          return 0;
	*/
    }
    
    infty_score_t score;

    // if option --normalized <L> is given, then do normalized local alignemnt
    if (clp.opt_normalized) {
        std::cerr
            << "ERROR: Normalized alignment not supported."
            <<std::endl;
        return -1;


        // 	// do some option consistency checks and output errors
        // 	if (clp.struct_local) {
        // 	    std::cerr 
        // 		<< "ERROR: Normalized structure local alignment not supported."
        // 		<<std::endl
        // 		<< "LocARNA ignores struct_local option."<<std::endl;
        // 	    return -1;
        // 	}
        // 	if (!clp.sequ_local) { // important: in the Aligner class, we rely on this
        // 	    std::cerr 
        // 		<< "ERROR: Normalized alignment requires option --sequ_local."<<std::endl;
        // 	    return -1;
        // 	}
        // //	score = aligner.normalized_align(clp.normalized_L,clp.opt_verbose);
	
    } else {
	
	// ========== STANDARD CASE ==========
	
    	// otherwise compute the best alignment
	score = aligner.align();
    
    }
    
    // ----------------------------------------
    // report score
    //
    std::cout << "Score: "<<score<<std::endl;


    // ------------------------------------------------------------
    // Traceback
    //
    if ((!clp.opt_normalized) && DO_TRACE) {
	    
	if (clp.opt_verbose) {
	    std::cout << "Traceback."<<std::endl;
	}
	
	aligner.trace();
	
	// for debugging:
	//if (clp.opt_verbose)
	//    aligner.get_alignment().write_debug(std::cout);
    }

    bool return_code=0;
    
    if (clp.opt_normalized || DO_TRACE) { // if we did a trace (one way or
        // the other)

	const Alignment &alignment = aligner.get_alignment();
	
	if (clp.opt_pos_output) {
	    std::cout << "HIT "<<score
		      <<alignment.local_startA()<<" "
		      <<alignment.local_startB()<<" "
		      <<alignment.local_endA()<<" "
		      <<alignment.local_endB()<<" "
		      <<std::endl;
	} 
	if (!clp.opt_pos_output && !clp.opt_local_output) {
	    MultipleAlignment ma(alignment,clp.opt_local_output,clp.opt_special_gap_symbols);

	    if (clp.opt_write_structure) {
		// annotate multiple alignment with structures
		ma.prepend(MultipleAlignment::SeqEntry("",
						       alignment.dot_bracket_structureA(clp.opt_local_output)));
		ma.append(MultipleAlignment::SeqEntry("",
						      alignment.dot_bracket_structureB(clp.opt_local_output)));
	    }
	    
	    if (clp.opt_local_output) {
		std::cout  << std::endl 
			   << "\t+" << alignment.local_startA() << std::endl
			   << "\t+" << alignment.local_startB() << std::endl
			   << std::endl;
	    }
	    
	    ma.write(std::cout,clp.output_width);

	    if (clp.opt_local_output) {
		std::cout  << std::endl 
			   << "\t+" << alignment.local_endA() << std::endl
			   << "\t+" << alignment.local_endB() << std::endl
			   << std::endl;
	    }

	}
	
	std::cout<<endl;
	
	// test MultipleAlignment
	if (clp.opt_verbose) {
	    MultipleAlignment resultMA(aligner.get_alignment());
	    //std::cout << "MultipleAlignment"<<std::endl; 
	    //resultMA.print_debug(cout);
	    if (multiple_ref_alignment) {
		std::cout << "Deviation to reference: "<< multiple_ref_alignment->deviation(resultMA)<<std::endl;
	    }
	}
	
	// ----------------------------------------
	// optionally write output formats
	//
	if (clp.opt_clustal_out) {
            ofstream out(clp.clustal_out.c_str());
	    if (out.good()) {

		MultipleAlignment ma(alignment, false,clp.opt_special_gap_symbols);
		
		out << "CLUSTAL W --- "<<PACKAGE_STRING;
		
		// for legacy, clustal files of pairwise alignments contain the score 
		if (seqA.num_of_rows()==1 && seqB.num_of_rows()==1)
		    out  <<" --- Score: " << score;
		out <<std::endl<<std::endl;

		if (clp.opt_write_structure) {
		    // annotate multiple alignment with structures
		    ma.prepend(MultipleAlignment::SeqEntry("",alignment.dot_bracket_structureA(false)));
		    ma.append(MultipleAlignment::SeqEntry("",alignment.dot_bracket_structureB(false)));
		}

		ma.write(out,clp.output_width);
	    
	    } else {
		cerr << "Cannot write to "<<clp.clustal_out<<endl<<"! Exit.";
		return_code = -1;
	    }
	}
	if (clp.opt_pp_out) {

	    ofstream out(clp.pp_out.c_str());
	    if (out.good()) {

		RnaData consensus(*rna_dataA,
				  *rna_dataB,
				  aligner.get_alignment(),
				  my_exp_probA,
				  my_exp_probB);
		
		consensus.write_pp(out);
	    } else {
		cerr << "Cannot write to "<<clp.pp_out<<endl<<"! Exit.";
		return_code = -1;
	    }
	}
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
