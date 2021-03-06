#include "aux.hh"
#include "global_stopwatch.hh"

#include "aligner_nn.hh"
#include "anchor_constraints.hh"
#include "trace_controller.hh"
// #include "d_matrix.hh"

#include <cmath>
#include <cassert>

#include <queue>

#include <iostream>



namespace LocARNA {


    // ------------------------------------------------------------
    // AlignerNN: align / compute similarity
    //
//TODO: initilizeze innner most attributes or remove them
    AlignerNN::AlignerNN(const AlignerNN &a)
	: params(new AlignerNParams(*a.params)),
	  scoring(a.scoring),
	  mod_scoring(0),
	  seqA(a.seqA),
	  seqB(a.seqB),
	  mapper_arcsA(a.mapper_arcsA),
	  mapper_arcsB(a.mapper_arcsB),
	  arc_matches(a.arc_matches),
	  bpsA(a.bpsA),
	  bpsB(a.bpsB),
	  r(a.r),
	  Dmat(a.Dmat),
	  IAmat(a.IAmat),
	  IBmat(a.IBmat),
	  IADmat(a.IADmat),
	  IBDmat(a.IBDmat),
	  Emat(a.Emat),
	  Fmat(a.Fmat),
	  M(a.M),
	  gapCostAmat(a.gapCostAmat),
	  gapCostBmat(a.gapCostBmat),
	  min_i(a.min_i),
	  min_j(a.min_j),
	  max_i(a.max_i),
	max_j(a.max_j),
	D_created(a.D_created),
	alignment(a.alignment),
	def_scoring_view(this),
	mod_scoring_view(this),
	traceback_closing_arcA(a.traceback_closing_arcA),//TODO: What to set as index?
	traceback_closing_arcB(a.traceback_closing_arcB)
    {}

    AlignerNN::AlignerNN(const AlignerParams &ap_ )
	: params(new AlignerNParams(dynamic_cast<const AlignerNParams &>(ap_))), //TODO: Check if needed to switch to AlignerNNParams?
	  scoring(params->scoring_),
	  mod_scoring(0),
	  seqA(*params->seqA_), seqB(*params->seqB_),
	  mapper_arcsA(*params->sparsification_mapper_arcsA_),
	  mapper_arcsB(*params->sparsification_mapper_arcsB_),
	  arc_matches(*params->arc_matches_),
	  bpsA(params->arc_matches_->get_base_pairsA()),
	  bpsB(params->arc_matches_->get_base_pairsB()),
	  r(1,1,params->seqA_->length(),params->seqB_->length()),
	  min_i(0),
	  min_j(0),
	  max_i(0),
          max_j(0),
	  D_created(false),
	  alignment(*params->seqA_,*params->seqB_),
          def_scoring_view(this),
          mod_scoring_view(this),
          traceback_closing_arcA(0, 0, params->seqA_->length()),//TODO: What to set as index?
          traceback_closing_arcB(0, 0, params->seqB_->length())
    {
	
	Dmat.resize(bpsA.num_bps()+1,bpsB.num_bps()+1); //+1 for empty arcs
	Dmat.fill(infty_score_t::neg_infty);

	IAmat.resize(mapper_arcsA.get_max_info_vec_size()+1, bpsB.num_bps()+1);//+1 for empty arcs
	IBmat.resize(bpsA.num_bps()+1, mapper_arcsB.get_max_info_vec_size()+1);//+1 for empty arcs

	IADmat.resize(bpsA.num_bps()+1,bpsB.num_bps()+1);//+1 for empty arcs
	IADmat.fill(infty_score_t::neg_infty);

	IBDmat.resize(bpsA.num_bps()+1,bpsB.num_bps()+1);//+1 for empty arcs
	IBDmat.fill(infty_score_t::neg_infty);

	M.resize(mapper_arcsA.get_max_info_vec_size()+1,mapper_arcsB.get_max_info_vec_size()+1);
	Emat.resize(mapper_arcsA.get_max_info_vec_size()+1, mapper_arcsB.get_max_info_vec_size()+1);
	Fmat.resize(mapper_arcsA.get_max_info_vec_size()+1, mapper_arcsB.get_max_info_vec_size()+1);


	gapCostAmat.resize(seqA.length()+3, seqA.length()+3);
	gapCostBmat.resize(seqB.length()+3, seqB.length()+3);

	trace_debugging_output=false; //!< a static switch to enable generating debugging logs
	do_cond_bottom_up=false;


    }


    //destructor
    AlignerNN::~AlignerNN() {
        assert(params != 0);
        delete params;
	if (mod_scoring!=0) delete mod_scoring;
    }

    // Computes and stores score of aligning a the subsequence between
    // different possible leftSides & rightSides to the gap
    template <class ScoringView>
    void AlignerNN::computeGapCosts(bool isA, ScoringView sv)
    {
	if (trace_debugging_output) {
	    std::cout << "computeGapCosts " << (isA?'A':'B') << std::endl;
	}
	const Sequence& seqX = isA?seqA:seqB;
	ScoreMatrix& gapCostXmat = isA?gapCostAmat:gapCostBmat;
	for( pos_type leftSide = 0;  leftSide <= seqX.length(); leftSide++)
	    {
		infty_score_t gap_score = (infty_score_t)0;
		gapCostXmat(leftSide, leftSide) = gap_score;
		for( pos_type lastPos = leftSide+1;  lastPos <= seqX.length(); lastPos++)
		    {
			if ( (isA && params->constraints_->aligned_in_a(lastPos))
			     || ( !isA && params->constraints_->aligned_in_b(lastPos)) ) {
			    gap_score = infty_score_t::neg_infty;
			    //		break;
			}
			else {
			    gap_score += sv.scoring()->gapX( lastPos, isA);
			}

			gapCostXmat(leftSide,lastPos+1) = gap_score;
		    }
		/*for (;lastPos <= seqX.length(); lastPos++)
		  {
		  gapCostXmat(leftSide,lastPos+1) = gap_score;
		  }*/
	    }
	if (trace_debugging_output)
	    std::cout << "computed computeGapCosts " << (isA?'A':'B') << std::endl;

    }

    // Returns score of aligning a the subsequence between leftSide &
    // rightSide to the gap, not including right/left side
    inline
    infty_score_t AlignerNN::getGapCostBetween( pos_type leftSide, pos_type rightSide, bool isA)
    //todo: Precompute the matrix?!
    {
//	    if (trace_debugging_output) std::cout <<
//	    "getGapCostBetween: leftSide:" << leftSide <<
//	    "rightSide:" << rightSide << "isA:" << isA << std::endl;
//	if (leftSide >= rightSide && rightSide == 0) // empty arc for domain ins
//		return (infty_score_t)0;

	assert(leftSide < rightSide);

	return (isA?gapCostAmat(leftSide,rightSide):gapCostBmat(leftSide, rightSide));
    }


    // Compute an element of the matrix IA/IB
    template<class ScoringView>
    infty_score_t
    AlignerNN::compute_IX(ArcIdx idxX, const Arc& arcY,
			 matidx_t i_index, bool isA, ScoringView sv) {

	//constraints are ignored,
	//params->constraints_->aligned_in_a(i_seq_pos);
	bool constraints_aligned_pos = false; 
	const BasePairs &bpsX = isA? bpsA : bpsB;
	const SparsificationMapper &mapper_arcsX = isA ? mapper_arcsA : mapper_arcsB;

	pos_type xl = get_arc_leftend(idxX, isA);

	seq_pos_t i_seq_pos = mapper_arcsX.get_pos_in_seq_new(idxX, i_index);
	
	seq_pos_t i_prev_seq_pos = mapper_arcsX.get_pos_in_seq_new(idxX, i_index-1);
	//TODO: Check border i_index==1,0
	const ArcIdxVec &arcIdxVecX = mapper_arcsX.valid_arcs_right_adj(idxX, i_index);

	infty_score_t max_score = infty_score_t::neg_infty;

	//base insertion/deletion
	if ( !constraints_aligned_pos  ) {
	    infty_score_t gap_score =  
		getGapCostBetween(i_prev_seq_pos, i_seq_pos, isA)
		+ sv.scoring()->gapX(i_seq_pos, isA);

	    if (gap_score.is_finite()) {
		// convert the base gap score to the loop gap score
		gap_score =
		    (infty_score_t)(sv.scoring()->loop_indel_score( gap_score.finite_value() ));
		// todo: unclean interface and casting
	    }
	    infty_score_t base_indel_score = IX(i_index-1, arcY, isA) + gap_score;
	    max_score = std::max( max_score, base_indel_score);
	}

	//arc deletion + align left side of the arc to gap
	for (ArcIdxVec::const_iterator arcIdx = arcIdxVecX.begin(); 
	     arcIdx != arcIdxVecX.end(); ++arcIdx) {
	    const Arc& arcX = bpsX.arc(*arcIdx);
	    infty_score_t gap_score =  getGapCostBetween(xl, arcX.left(), isA);
	    if (gap_score.is_finite()) {
		// convert the base gap score to the loop gap score
		gap_score = (infty_score_t)(sv.scoring()->loop_indel_score( gap_score.finite_value()));
	    }
	    infty_score_t arc_indel_score_extend =
		IXD(arcX, arcY, isA) + sv.scoring()->arcDel(arcX, isA) + gap_score;

	    if (arc_indel_score_extend > max_score) {
		max_score = arc_indel_score_extend;
	    }
	    
	    infty_score_t arc_indel_score_open = 
		sv.D(arcX, arcY, isA) + sv.scoring()->arcDel(arcX, isA)
		+ gap_score + sv.scoring()->indel_opening_loop();
	    if (arc_indel_score_open > max_score) {
		max_score = arc_indel_score_open;
	    }

//	    std::cout << ">>> arc_indel_score_extend=IXD(" << arcX << "," << arcY<< ")"<< IXD(arcX, arcY, isA) << "+" <<  sv.scoring()->arcDel(arcX, isA) << "+" << gap_score
//	    		<< "="<< arc_indel_score_extend << "  arc_indel_score_open:" << arc_indel_score_open << std::endl;
	}
	
	return max_score;
    }


    //Compute an entry of matrix E
    template<class ScoringView>
    infty_score_t
    AlignerNN::compute_E_entry(seq_pos_t al_seq_pos, matidx_t i_index, matidx_t j_index, seq_pos_t i_seq_pos, seq_pos_t i_prev_seq_pos, ScoringView sv)
    {
	bool constraints_aligned_pos_A = false; // TOcheck: Probably unnecessary, constraints are not considered
	if (i_seq_pos <= al_seq_pos || constraints_aligned_pos_A) //check possibility of base deletion
	    return infty_score_t::neg_infty;

	// base del
	infty_score_t gap_cost =
	    getGapCostBetween(i_prev_seq_pos, i_seq_pos, true) + sv.scoring()->gapA(i_seq_pos);
	infty_score_t extend_score = gap_cost + Emat(i_index-1,j_index);
	infty_score_t open_score = 
	    M(i_index-1, j_index) + gap_cost + sv.scoring()->indel_opening();
	return  (std::max(extend_score, open_score ));
    }


    //Compute an entry of matrix F
    template<class ScoringView>
    infty_score_t
    AlignerNN::compute_F_entry(seq_pos_t bl_seq_pos,
			      matidx_t i_index, matidx_t j_index,
			      seq_pos_t j_seq_pos, seq_pos_t j_prev_seq_pos, ScoringView sv)
    {
	bool constraints_aligned_pos_B = false;
	// TOcheck: Probably unnecessary, constraints are not considered
	if (j_seq_pos <= bl_seq_pos || constraints_aligned_pos_B) //check possibility of base deletion
	    return infty_score_t::neg_infty;

	// base ins
	infty_score_t gap_cost =
	    getGapCostBetween(j_prev_seq_pos, j_seq_pos, false) + sv.scoring()->gapB(j_seq_pos);
	infty_score_t extend_score = Fmat(i_index,j_index-1) + gap_cost;
	infty_score_t open_score = 
	    M(i_index, j_index-1) + gap_cost + sv.scoring()->indel_opening();
	return  (std::max(extend_score, open_score ));
    }

    //Compute an entry of matrix M
	template<class ScoringView>
	infty_score_t
	AlignerNN::compute_M_entry(ArcIdx idxA, ArcIdx idxB,
							seq_pos_t al_seq_pos,seq_pos_t bl_seq_pos,
				  matidx_t i_index, matidx_t j_index, ScoringView sv) {

	bool constraints_alowed_edge = true;
	// constraints are ignored,  params->constraints_->allowed_edge(i_seq_pos, j_seq_pos)
	tainted_infty_score_t max_score = infty_score_t::neg_infty;
	//define variables for sequence positions
	seq_pos_t i_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, i_index);
	seq_pos_t j_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB, j_index);
	seq_pos_t i_prev_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, i_index-1);
	//TODO: Check border i_index==1,0
	seq_pos_t j_prev_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB, j_index-1);
	//TODO: Check border j_index==1,0
	 if (trace_debugging_output) {
	     std::cout << "compute_M_entry al_seq_pos: " << al_seq_pos << " bl_seq_pos: " << bl_seq_pos
	 	      << "i:" << i_seq_pos << " i_index: " << i_index << " i_prev:" << i_prev_seq_pos
	 	      << " j: " << j_seq_pos << " j_index:"<< j_index <<  " j_prev:" << j_prev_seq_pos << std::endl;
	 }

	score_t opening_cost_A=0;
	if (i_prev_seq_pos < (i_seq_pos - 1)) {
		//implicit base deletion because of sparsification
		opening_cost_A = sv.scoring()->indel_opening();
	}

	score_t opening_cost_B=0;
	if (j_prev_seq_pos < (j_seq_pos - 1)) {
		//implicit base insertion because of sparsification
		opening_cost_B = sv.scoring()->indel_opening();
	}

	// base match
	if ( constraints_alowed_edge
		 && mapper_arcsA.pos_unpaired(idxA, i_index)
		 && mapper_arcsB.pos_unpaired(idxB, j_index) ) {

		infty_score_t gap_match_score =
		getGapCostBetween(i_prev_seq_pos, i_seq_pos, true)
		+ getGapCostBetween(j_prev_seq_pos, j_seq_pos, false)
		+ (sv.scoring()->basematch(i_seq_pos, j_seq_pos));

		max_score =
		std::max( max_score,
			  gap_match_score + opening_cost_B
			  + Emat(i_index-1, j_index-1) );
		max_score =
		std::max( max_score,
			  gap_match_score + opening_cost_A
			  + Fmat(i_index-1, j_index-1) );
		max_score =
		std::max( max_score,
			  gap_match_score
			  + opening_cost_A + opening_cost_B
			  + M(i_index-1, j_index-1) );

	}
	// base del, for efficiency compute_E/F entry invoked within compute_M_entry
	Emat(i_index, j_index) =
		compute_E_entry(al_seq_pos, i_index, j_index, i_seq_pos, i_prev_seq_pos, sv);
	max_score = std::max(max_score,  (tainted_infty_score_t)Emat(i_index, j_index));

	// base ins
	Fmat(i_index, j_index) =
		compute_F_entry(bl_seq_pos, i_index, j_index, j_seq_pos, j_prev_seq_pos, sv);
	max_score = std::max(max_score,  (tainted_infty_score_t)Fmat(i_index, j_index));


	//list of valid arcs ending at i/j
	const ArcIdxVec& arcsA = mapper_arcsA.valid_arcs_right_adj(idxA, i_index);
	const ArcIdxVec& arcsB = mapper_arcsB.valid_arcs_right_adj(idxB, j_index);

	if(params->multiloop_deletion_ > 0 ) {

		//domain deletion
		for (ArcIdxVec::const_iterator arcAIdx = arcsA.begin();
		 arcAIdx != arcsA.end();
		 ++arcAIdx) {
			const Arc& arcA = bpsA.arc(*arcAIdx);
			if(params->multiloop_deletion_< (arcA.right()-arcA.left()+1) ) // Todo: if the adjlist is sorted we can return instead of continue
				continue;
			matidx_t  arcA_left_index_before   =
			mapper_arcsA.first_valid_mat_pos_before(idxA, arcA.left(), al_seq_pos);
			seq_pos_t arcA_left_seq_pos_before =
			mapper_arcsA.get_pos_in_seq_new(idxA, arcA_left_index_before);

			opening_cost_A = 0;
//			if (arcA_left_seq_pos_before < (arcA.left() - 1)) {
//			// implicit base deletion because of sparsification
//			opening_cost_A = sv.scoring()->indel_opening();
//			}

			Arc empty_arcB = BasePairs__Arc(bpsB.num_bps(), 0, 0);

			infty_score_t arc_indel_score_open =
			sv.D(arcA, empty_arcB) +
			(infty_score_t)(sv.scoring()->loop_indel_score(getGapCostBetween( arcA_left_seq_pos_before, arcA.left(), true).finite_value())) +
					sv.scoring()->arcDel(arcA, true)
			 + sv.scoring()->indel_opening_loop();


			tainted_infty_score_t domain_del_score =
				arc_indel_score_open
				+ opening_cost_A
				+ M(arcA_left_index_before, j_index);
			max_score = std::max(max_score,  domain_del_score);
			if (trace_debugging_output) {
					std::cout << "M: domain del: arcA" << arcA
						  << " D(arcA,empty_arcB)=" << sv.D(arcA, empty_arcB)
						  << " sv.scoring()->arcDel(arcA, true)="
						  <<sv.scoring()->arcDel(arcA, true)
						  << "M(" << arcA_left_index_before <<","
						  << j_index << ")="
						  << M(arcA_left_index_before, j_index)
						  << std::endl;
			}

		}
		//domain insertion
		for (ArcIdxVec::const_iterator arcBIdx = arcsB.begin();
		 arcBIdx != arcsB.end();
		 ++arcBIdx) {
			const Arc& arcB = bpsB.arc(*arcBIdx);
			if(params->multiloop_deletion_< (arcB.right()-arcB.left()+1) ) // Todo: if the adjlist is sorted we can return instead of continue
				continue;

			matidx_t  arcB_left_index_before   =
			mapper_arcsB.first_valid_mat_pos_before(idxB, arcB.left(), bl_seq_pos);
			seq_pos_t arcB_left_seq_pos_before =
			mapper_arcsB.get_pos_in_seq_new(idxB, arcB_left_index_before);
			opening_cost_B = 0;
//			if (arcB_left_seq_pos_before < (arcB.left() - 1)) {
//			// implicit base deletion because of sparsification
//			opening_cost_B = sv.scoring()->indel_opening();
//			}

			Arc empty_arcA = BasePairs__Arc(bpsA.num_bps(), 0, 0);

			infty_score_t arc_indel_score_open = (infty_score_t)sv.scoring()->loop_indel_score(
					getGapCostBetween( arcB_left_seq_pos_before, arcB.left(), false).finite_value()) +
			sv.D(empty_arcA, arcB) + sv.scoring()->arcDel(arcB, false)
			 + sv.scoring()->indel_opening_loop();

			tainted_infty_score_t domain_ins_score =
				arc_indel_score_open
				+ opening_cost_B
				+ M(i_index, arcB_left_index_before);
			max_score = std::max(max_score,  domain_ins_score);
			if (trace_debugging_output) {
					std::cout << "M: domain ins: arcB" << arcB
						  << " D(arcA,empty_arcB)=" << sv.D(empty_arcA, arcB)
						  << " sv.scoring()->arcDel(arcB, true)="
						  <<sv.scoring()->arcDel(arcB, false)
						  << "M(" << i_index <<","
						  << arcB_left_index_before << ")="
						  << M(i_index, arcB_left_index_before)
						  << std::endl;
			}
		}
	}
	// arc match
	for (ArcIdxVec::const_iterator arcAIdx = arcsA.begin();
		 arcAIdx != arcsA.end();
		 ++arcAIdx) {
		const Arc& arcA = bpsA.arc(*arcAIdx);
		matidx_t  arcA_left_index_before   =
		mapper_arcsA.first_valid_mat_pos_before(idxA, arcA.left(), al_seq_pos);
		seq_pos_t arcA_left_seq_pos_before =
		mapper_arcsA.get_pos_in_seq_new(idxA, arcA_left_index_before);

		opening_cost_A = 0;
		if (arcA_left_seq_pos_before < (arcA.left() - 1)) {
		// implicit base deletion because of sparsification
		opening_cost_A = sv.scoring()->indel_opening();
		}

		for (ArcIdxVec::const_iterator arcBIdx = arcsB.begin();
		 arcBIdx != arcsB.end();
		 ++arcBIdx) {
		const Arc& arcB = bpsB.arc(*arcBIdx);


		matidx_t arcB_left_index_before =
			mapper_arcsB.first_valid_mat_pos_before(idxB, arcB.left(), bl_seq_pos);
		seq_pos_t arcB_left_seq_pos_before =
			mapper_arcsB.get_pos_in_seq_new(idxB, arcB_left_index_before);

		opening_cost_B = 0;
		if (arcB_left_seq_pos_before < (arcB.left() - 1)) {
			//implicit base insertion because of sparsification
			opening_cost_B = sv.scoring()->indel_opening();
		}

		if (trace_debugging_output) {
			std::cout << "\tmatching arcs: arcA" << arcA << "arcB:" << arcB
				  << " D(arcA,arcB)=" << sv.D( arcA, arcB )
				  << " sv.scoring()->arcmatch(arcA, arcB)="
				  <<sv.scoring()->arcmatch(arcA, arcB)
				  << "M(" << arcA_left_index_before <<","
				  << arcB_left_index_before << ")="
				  << M(arcA_left_index_before, arcB_left_index_before)
				  << std::endl;
		}
		infty_score_t gap_match_score =
			getGapCostBetween( arcA_left_seq_pos_before, arcA.left(), true)
			+ getGapCostBetween( arcB_left_seq_pos_before, arcB.left(), false)
			+ sv.D( arcA, arcB ) + sv.scoring()->arcmatch(arcA, arcB);

		tainted_infty_score_t arc_match_score =
			gap_match_score
			+ opening_cost_A + opening_cost_B
			+ M(arcA_left_index_before, arcB_left_index_before);

//		 if (trace_debugging_output) {
//		     std::cout << "gap_match_score:" << gap_match_score << std::endl;
//		     std::cout << "arc_match_score:" << arc_match_score << std::endl;
//		 }

		arc_match_score =
			std::max( arc_match_score,
				  (gap_match_score
				   + opening_cost_B
				   + Emat(arcA_left_index_before,
					  arcB_left_index_before)) );
		arc_match_score =
			std::max( arc_match_score,
				  (gap_match_score
				   + opening_cost_A
				   + Fmat(arcA_left_index_before,
					  arcB_left_index_before)) );

		if (arc_match_score > max_score) {
			max_score = arc_match_score;
			is_innermost_arcA = false;
			is_innermost_arcB = false;
			 if (trace_debugging_output)	{
				std::cout << "compute_M_entry arcs " << arcA << " , "
					  << arcB << "arc match score: " << arc_match_score
					  << std::endl;
			 }
		}
		}
	}
	return max_score;
	}

    // initializing matrix M
    //
    template <class ScoringView>
    void
    AlignerNN::init_M_E_F(ArcIdx idxA, ArcIdx idxB, ScoringView sv) {

	// alignments that have empty subsequence in A (i=al) and
	// end with gap in alistr of B do not exist ==> -infty

	if (trace_debugging_output) {
	    std::cout << "init_state "
//		      << " al: " << al << " bl: " << bl
//		      << " ar: " << ar << " br: " << br
		      << std::endl;
	}

	//empty sequences A,B
	M(0,0) = (infty_score_t)0;

	Emat(0,0) = infty_score_t::neg_infty;//tocheck:validity
	Fmat(0,0) = infty_score_t::neg_infty;//tocheck:validity


	// init first column
	//
	infty_score_t indel_score = (infty_score_t)(sv.scoring()->indel_opening());
	for (matidx_t i_index = 1; i_index < mapper_arcsA.number_of_valid_mat_pos(idxA); i_index++) {

	    seq_pos_t i_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA,i_index);
	    // if (params->trace_controller.min_col(i)>bl) break;
	    // no trace controller in this version
	    
	    //tocheck:toask: check alignment constraints in the
	    //invalid positions between valid gaps

	    if (!indel_score.is_neg_infty()) { //checked for optimization
		/*	    if (params->constraints_->aligned_in_a(i_seq_pos) )
			    {
			    indel_score=infty_score_t::neg_infty;
			    }
			    else */ {
		    seq_pos_t i_prev_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA,i_index-1);
		    indel_score = indel_score + getGapCostBetween(i_prev_seq_pos, i_seq_pos, true) + sv.scoring()->gapA(i_seq_pos);
		}
	    }
	    Emat(i_index, 0) = indel_score;
	    Fmat(i_index, 0) = infty_score_t::neg_infty;
	    M(i_index,0) = indel_score;//same as Emat(i_index, 0);

	}

	// init first row
	//
	indel_score = (infty_score_t)(sv.scoring()->indel_opening());
	for (matidx_t j_index=1; j_index < mapper_arcsB.number_of_valid_mat_pos(idxB); j_index++) {
	    seq_pos_t j_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB,j_index);
	    if (!indel_score.is_neg_infty()) { //checked for optimization
		/* if (params->constraints_->aligned_in_b(j_seq_pos)) {
		   indel_score=infty_score_t::neg_infty;
		   }
		   else*/ {
		    seq_pos_t j_prev_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB,j_index-1);

		    indel_score = indel_score + getGapCostBetween(j_prev_seq_pos, j_seq_pos, false) + sv.scoring()->gapB(j_seq_pos); //toask: infty_score_t operator+ overloading
		}
	    }
	    Emat(0,j_index) = infty_score_t::neg_infty;
	    Fmat(0,j_index) = indel_score;
	    M(0,j_index) = indel_score; // same as Fmat(0,j_index);

	}

    }


    //fill IA entries for a column with fixed al, arcB
    void
    AlignerNN::fill_IA_entries( ArcIdx idxA, Arc arcB, pos_type max_ar )
    {

	IAmat(0, arcB.idx()) = infty_score_t::neg_infty;
	if (params->multiloop_deletion_> 0 && arcB.idx()==bpsB.num_bps())
		IAmat(0, arcB.idx()) = (infty_score_t)0; //domain insdel base case

	matidx_t max_right_index;
	max_right_index = mapper_arcsA.number_of_valid_mat_pos(idxA);

	if (trace_debugging_output)
		    std::cout << "fill_IA_entries: " <<  "idxA=" << idxA << "max_ar=" << max_ar << " max_right_index: " << max_right_index << ", arcB=" << arcB << std::endl;

	for (matidx_t i_index = 1; i_index < max_right_index; i_index++) {

	    IAmat(i_index, arcB.idx()) = compute_IX(idxA, arcB, i_index, true, def_scoring_view);
	    // std::cout << "      IAmat(" << i_index << "," << arcB.idx() << ")=" << IAmat(i_index, arcB.idx()) << std::endl;
	    //fill IAD matrix entries //tocheck: verify
	    seq_pos_t i_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, i_index);
	    seq_pos_t i_prev_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, i_index-1);

	}
	//	std::cout << "fill_IA_entries al: "<< al << " arcB.idx: " << arcB.idx() << " arcB.left: " << arcB.left() << " arcB.right: " << arcB.right() << " IAmat: " << std::endl << IAmat << std::endl;
    }

    //fill IB entries for a row with fixed arcA, bl
    void AlignerNN::fill_IB_entries ( Arc arcA,ArcIdx idxB, pos_type max_br)
    {
	if (trace_debugging_output)
	    std::cout << "fill_IB_entries: " << "arcA=" << arcA<< ", idxB=" << idxB << "max_br=" << max_br << std::endl;

	IBmat(arcA.idx(), 0) = infty_score_t::neg_infty;
	if (params->multiloop_deletion_> 0 && arcA.idx()==bpsA.num_bps())
		IBmat(arcA.idx(), 0) = (infty_score_t)0; //domain insdel base case

	pos_type max_right_index;
	max_right_index = mapper_arcsB.number_of_valid_mat_pos(idxB);

	for (pos_type j_index = 1; j_index < max_right_index; j_index++) {		// limit entries due to trace control


	    IBmat(arcA.idx(), j_index) = compute_IX(idxB, arcA, j_index, false, def_scoring_view);
	    // std::cout << "IBmat( << " << arcA.idx() << "," <<  j_index << ")=" << IBmat(arcA.idx(), j_index) << std::endl;
	    //fill IBD matrix entries
	    seq_pos_t j_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB, j_index);
	    seq_pos_t j_prev_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB, j_index-1);
	}
//		std::cout << "fill_IB_entries arcA: " << arcA << " bl: "<< bl <<  " IBmat: " << std::endl << IBmat << std::endl;
    }

//compute/align matrix M
 void
 AlignerNN::fill_M_entries(const Arc &arcA, const Arc &arcB)  {

	assert(params->track_closing_bp_); // Track the exact closing right ends of al and bl

	//todo: How to behave in psudeoarcs covering all seq?

	//todo: Get rid of this line below:
//	pos_type al = arcA.left(); pos_type ar = arcA.right();
//	pos_type bl = arcB.left(); pos_type br = arcB.right();


	ArcIdx idxA = arcA.idx();
	ArcIdx idxB = arcB.idx();
	if (trace_debugging_output) {
		std::cout << "fill_M_entries: arcs: " << arcA << "  " << arcB << std::endl;
	}
	//initialize M
	init_M_E_F(idxA, idxB, def_scoring_view);

	if (trace_debugging_output) {
		std::cout << "init_M finished" << std::endl;
	}


	//iterate through valid entries
	matidx_t max_i_index = mapper_arcsA.number_of_valid_mat_pos(idxA);
	matidx_t max_j_index = mapper_arcsB.number_of_valid_mat_pos(idxB);
	//std::cout << "max_ij_index set to " << max_i_index << " " << max_j_index << std::endl;
	for (matidx_t i_index = 1;
		 i_index < max_i_index ;
		 i_index++) {
		/*
		//tomark: constraints
		// limit entries due to trace controller
		pos_type min_col = std::max(bl+1,params->trace_controller.min_col(i));
		pos_type max_col = std::min(br-1,params->trace_controller.max_col(i));
		*/
		for (matidx_t j_index = 1;
		 j_index < max_j_index;
		 j_index++) {

		// E and F matrix entries will be computed by compute_M_entry
		M(i_index,j_index) = compute_M_entry(idxA, idxB, arcA.left(), arcB.left(), i_index,j_index,def_scoring_view);
		//toask: where should we care about non_default scoring views

//		 if (trace_debugging_output) {
//		     std::cout << "M["<< i_index << "," << j_index << "]="
//		 	      << M(i_index,j_index) << std::endl;
//		 }
		}
	}
	// if (trace_debugging_output) {
	//     std::cout << "align_M aligned M is :" << std::endl << M << std::endl;
	// }
}
 // compute the entries in the D matrix that
     // can be computed from the matrix/matrices M, IA, IB
     // for the subproblem al,bl,max_ar,max_br
     // pre: M,IA,IB matrices are computed by a call to
 void
    AlignerNN::fill_D_entry(const Arc &arcA,const Arc &arcB){
//    	index_t al = arcA.left();
//    	index_t bl = arcB.left();
	 	ArcIdx idxA = arcA.idx();
	 	ArcIdx idxB = arcB.idx();
    	//define variables for sequence positions & sparsed indices
		seq_pos_t ar_seq_pos = arcA.right();
		seq_pos_t br_seq_pos = arcB.right();

		if (trace_debugging_output) {
		std::cout << "compute_D_entry(arcA:" << arcA << " arcB:" << arcB  << std::endl;
		}
		UnmodifiedScoringViewNN sv = def_scoring_view;


		matidx_t ar_prev_mat_idx_pos = mapper_arcsA.number_of_valid_mat_pos(idxA)-1; //TODO: VERY IMPORTNANT: -1 or not?

		matidx_t br_prev_mat_idx_pos = mapper_arcsB.number_of_valid_mat_pos(idxB)-1;

		seq_pos_t ar_prev_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, ar_prev_mat_idx_pos);
		infty_score_t jumpGapCostA = getGapCostBetween(ar_prev_seq_pos, ar_seq_pos, true);
		seq_pos_t br_prev_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB, br_prev_mat_idx_pos);
		infty_score_t jumpGapCostB = getGapCostBetween(br_prev_seq_pos, br_seq_pos, false);

		if (trace_debugging_output) {
		std::cout << " ar_prev_mat_idx_pos:" << ar_prev_mat_idx_pos
			  << " br_prev_mat_idx_pos:" << br_prev_mat_idx_pos << std::endl;
		std::cout << " ar_prev_seq_pos:" << ar_prev_seq_pos
			  << " br_prev_seq_pos:" << br_prev_seq_pos << std::endl;
		}

		//M,IA,IB scores
		// infty_score_t m=
		//   Ms[0](ar_prev_mat_idx_pos, br_prev_mat_idx_pos) + jumpGapCostA + jumpGapCostB;

		//-----three cases for gap extension/initiation ---

		score_t opening_cost_A=0;
		if (ar_prev_seq_pos < (ar_seq_pos - 1)) {
		//implicit base deletion because of sparsification
		opening_cost_A = sv.scoring()->indel_opening();
		}

		score_t opening_cost_B=0;
		if (br_prev_seq_pos < (br_seq_pos - 1)) {
		//implicit base insertion because of sparsification
		opening_cost_B = sv.scoring()->indel_opening();
		}

		infty_score_t gap_score = jumpGapCostA + jumpGapCostB;
		infty_score_t mdel =
		(infty_score_t)(gap_score + opening_cost_B
				+ Emat(ar_prev_mat_idx_pos, br_prev_mat_idx_pos));
		infty_score_t mins =
		(infty_score_t)(gap_score + opening_cost_A
				+ Fmat(ar_prev_mat_idx_pos, br_prev_mat_idx_pos));
		infty_score_t mm =
		(infty_score_t)(gap_score + opening_cost_A
				+ opening_cost_B
				+  M(ar_prev_mat_idx_pos, br_prev_mat_idx_pos) );

		if (trace_debugging_output)	{
		std::cout << "mdel=" << mdel
			  << " mins=" << mins
			  << " mm=" << mm << std::endl;
		std::cout << "mm=" << gap_score << "+" << opening_cost_A <<
				"+"<< opening_cost_B <<
				"+" <<  "M(" << ar_prev_mat_idx_pos << "," << br_prev_mat_idx_pos << "):" <<  M(ar_prev_mat_idx_pos, br_prev_mat_idx_pos) << std::endl;
		}

		infty_score_t m = std::max( mm, std::max(mdel, mins));

		//TODO: IMPORTANT REASON to add prob arc score here is ambigiou
		if (do_cond_bottom_up) {
		if (is_innermost_arcA || is_innermost_arcB)
			m = m + scoring->arcmatch(arcA, arcB, false, true);
		}
		//------------------------------



		infty_score_t ia= IAmat(ar_prev_mat_idx_pos,idxB) + jumpGapCostA;
		infty_score_t ib= IBmat(idxA,br_prev_mat_idx_pos) + jumpGapCostB;

		assert(IADmat(idxA,idxB)
		   == infty_score_t::neg_infty || IADmat(idxA,idxB) == ia);
		assert(IBDmat(idxA, idxB)
		   == infty_score_t::neg_infty || IBDmat(idxA, idxB) == ib);
		IADmat(idxA, idxB) = ia; //TODO: avoid recomputation
		IBDmat(idxA, idxB) = ib; //TODO: avoid recomputation
		if (trace_debugging_output) {
			std::cout << "Set IAD(" << arcA.idx() <<","<< arcB.idx()<<") =" << ia <<std::endl;
			std::cout << "Set IBD(" << arcA.idx() <<","<< arcB.idx()<<") =" << ib <<std::endl;
			std::cout << "m=" << m << " ia=" << ia << " ib=" << IBmat(arcA.idx(),br_prev_mat_idx_pos) <<"+" << jumpGapCostB << "=" << ib << std::endl;
		}

		//	assert(ia == iad);

		//	cout << "IBDmat" << std::endl  << IBDmat << std::endl;
		//	assert(ib == ibd);



		assert (! params->struct_local_);

		D(arcA, arcB) = std::max(m, ia);
		D(arcA, arcB) = std::max(D(arcA,arcB), ib );
		if (trace_debugging_output)
		 std::cout <<"DD["<< arcA << "," <<arcB <<"]:" << D(arcA, arcB) << std::endl;

    }


	// Fill entries of domain insertion deletion i.e. IA with empty B sub-sequence and the opposite
 	void AlignerNN::compute_IAB_entries_domain(seq_pos_t start, seq_pos_t end, bool isA) {

 		const SparsificationMapper& mapper_arcsX = (isA)?mapper_arcsA:mapper_arcsB;
 		const BasePairs &bspX = (isA)?bpsA:bpsB;
 		const BasePairs &bpsY = (isA)?bpsB:bpsA;;

	for (pos_type xl=end+1; xl>start; ) {
		xl--;
		if (trace_debugging_output) std::cout << "align_D xl: " << xl << std::endl;

		const BasePairs::LeftAdjList &adjlX = bspX.left_adjlist(xl);
		if ( adjlX.empty() )
		{
			if (trace_debugging_output)	std::cout << "empty left_adjlist(xl=)" << xl << std::endl;
			continue;
		}
 		Arc empty_arcY = BasePairs__Arc(bpsY.num_bps(), 0, 0);
		for (BasePairs::LeftAdjList::const_iterator arcX =
				adjlX.begin(); arcX != adjlX.end(); ++arcX) {

			if(params->multiloop_deletion_< (arcX->right()-arcX->left()+1) ) // Todo: if the adjlist is sorted we can return instead of continue
				continue;
			if (trace_debugging_output) {
				std::cout << "align_D domain insertion isA=" << isA  << "  arcX():" << *arcX << std::endl;
				std::cout << "fill_IA_entries: " << xl << "," << arcX->right() <<   std::endl;
				std::cout << "                 arcX:" <<  *arcX  << std::endl;
			}

			if (isA){
				scoring->set_closing_arcs(*arcX, empty_arcY);
				is_innermost_arcA = true;
				fill_IA_entries(arcX->idx(), empty_arcY, arcX->right());
			}
			else {
				scoring->set_closing_arcs(empty_arcY, *arcX);
				is_innermost_arcB = true;
				fill_IB_entries(empty_arcY, arcX->idx(),  arcX->right());
			}

			matidx_t xr_prev_mat_idx_pos = mapper_arcsX.number_of_valid_mat_pos(arcX->idx())-1; //TODO: VERY IMPORTNANT: -1 or not?
			seq_pos_t xr_prev_seq_pos = mapper_arcsX.get_pos_in_seq_new(arcX->idx(), xr_prev_mat_idx_pos);
			infty_score_t jumpGapCostX = (infty_score_t)scoring->loop_indel_score(
					getGapCostBetween(xr_prev_seq_pos, arcX->right(), isA).finite_value());
			if (isA) {
				infty_score_t ix= IAmat(xr_prev_mat_idx_pos,empty_arcY.idx()) + jumpGapCostX;
				IADmat(arcX->idx(), empty_arcY.idx()) = ix;
				D(*arcX, empty_arcY) = ix;
			}
			else {
				infty_score_t ix= IBmat(empty_arcY.idx(), xr_prev_mat_idx_pos) + jumpGapCostX;
				IBDmat(empty_arcY.idx(), arcX->idx()) = ix;
				D(empty_arcY, *arcX) = ix;
			}
		}
	}

 	}
    // compute all entries D
    void
    AlignerNN::align_D() {
	computeGapCosts(true, def_scoring_view);//gap costs A //tocheck:always def_score view!
	computeGapCosts(false, def_scoring_view);//gap costs B //tocheck:always def_score view!

	if(params->multiloop_deletion_> 0) {
		// Fill entries of domain insertion deletion i.e. IA with empty B sub-sequence and the opposite
		compute_IAB_entries_domain(r.startA(), r.endA(), true);
		compute_IAB_entries_domain(r.startB(), r.endB(), false);
	}

	// for al in r.endA() .. r.startA

	for (pos_type al=r.endA()+1; al>r.startA(); ) {
	    al--;
	    if (trace_debugging_output) std::cout << "align_D al: " << al << std::endl;

	    const BasePairs::LeftAdjList &adjlA = bpsA.left_adjlist(al);
	    if ( adjlA.empty() )
		{
		    if (trace_debugging_output)	std::cout << "empty left_adjlist(al=)" << al << std::endl;
		    continue;
		}

	    //	pos_type max_bl = std::min(r.endB(),params->trace_controller.max_col(al)); //tomark: trace_controller
	    //	pos_type min_bl = std::max(r.startB(),params->trace_controller.min_col(al));



	    pos_type max_bl = r.endB();
	    pos_type min_bl = r.startB();

	    // for bl in max_bl .. min_bl
	    for (pos_type bl=max_bl+1; bl > min_bl;) {
		bl--;

		const BasePairs::LeftAdjList &adjlB = bpsB.left_adjlist(bl);

		if ( adjlB.empty() )
		    {
			if (trace_debugging_output)	std::cout << "empty left_adjlist(bl=)" << bl << std::endl;
			continue;
		    }
//		std::cout << "*** al: " << al << "    bl:" << bl << std::endl;
		// ------------------------------------------------------------
		// old code for finding maximum arc ends:

		// pos_type max_ar=adjlA.begin()->right();	//tracecontroller not considered
		// pos_type max_br=adjlB.begin()->right();

		// //find the rightmost possible basepair for left base al
		// for (BasePairs::LeftAdjList::const_iterator arcA = adjlA.begin();
		// 	 arcA != adjlA.end(); arcA++)
		// 	{
		// 	    if (max_ar < arcA->right() )
		// 		max_ar = arcA->right();
		// 	}
		// //find the rightmost possible basepair for left base bl
		// for (BasePairs::LeftAdjList::const_iterator arcB = adjlB.begin();
		// 	 arcB != adjlB.end(); arcB++)
		// 	{
		// 	    if (max_br < arcB->right() )
		// 		max_br = arcB->right();
		// 	}
	    

		 // Track the exact closing right ends of al and bl




		for (BasePairs::LeftAdjList::const_iterator arcA =
				adjlA.begin(); arcA != adjlA.end(); ++arcA) {
			for (BasePairs::LeftAdjList::const_iterator arcB =
					adjlB.begin(); arcB != adjlB.end(); ++arcB) {
				scoring->set_closing_arcs(*arcA, *arcB);
				if (trace_debugging_output) {
					std::cout << "align_D arcA:" << *arcA << ", arcB:" << *arcB << std::endl;
					std::cout << "fill_M_entries: " << al << "," << arcA->right() << "  " <<  bl << "," << arcB->right() << std::endl;
					std::cout << "                 arcA:" <<  *arcA << "  arcB" << *arcB << std::endl;
				}
				is_innermost_arcA = true;
				is_innermost_arcB = true;
				//compute matrix M
				//	    stopwatch.start("compM");
				fill_M_entries(*arcA, *arcB); //fill_M_entries(al, arcA->right(), bl, arcB->right());
				fill_IA_entries(arcA->idx(), *arcB, arcA->right());
				fill_IB_entries(*arcA, arcB->idx(), arcB->right());
				fill_D_entry(*arcA, *arcB);
				//	    stopwatch.stop("compM");

			}




			//TODO: Make the IA and IB calculations with exact right side(?)
			// from aligner.cc: find maximum arc ends
/*			pos_type max_ar = al;
			pos_type max_br = bl;

			// get the maximal right ends of any arc match with left ends (al,bl)
			// in noLP mode, we don't consider cases without immediately enclosing arc match
			arc_matches.get_max_right_ends(al, bl, &max_ar, &max_br,
					params->no_lonely_pairs_);


			//compute IA
			//	    stopwatch.start("compIA");
			for (BasePairs::LeftAdjList::const_iterator arcB =
					adjlB.begin(); arcB != adjlB.end(); ++arcB) {
				scoring->set_closing_arcs(BasePairs__Arc(0, al, max_ar) , *arcB); //TODO: Verfiy this, specially the constructor and idx
				std:cout << "fill_IA_entries: " << al << "," << max_ar << "  " <<  arcB->left() << "," << arcB->right();

				fill_IA_entries(al, *arcB, max_ar);
			}
			//	    stopwatch.stop("compIA");

			//comput IB
			//	    stopwatch.start("compIB");
			for (BasePairs::LeftAdjList::const_iterator arcA =
					adjlA.begin(); arcA != adjlA.end(); ++arcA) {
				scoring->set_closing_arcs(*arcA, BasePairs__Arc(0, bl, max_br)); //TODO: Verfiy this, specially the constructor and idx
				fill_IB_entries(*arcA, bl, max_br);
			}
			//	    stopwatch.stop("compIB");
*/
			// ------------------------------------------------------------
			// now fill matrix D entries
			//
//			fill_D_entries(al, bl);

		}
	    }
	}
	if (trace_debugging_output) std::cout << "M matrix:" << std::endl << M << std::endl;
	if (trace_debugging_output) std::cout << "D matrix:" << std::endl << Dmat << std::endl;

	D_created=true; // now the matrix D is built up
    }


    // compute the alignment score
    infty_score_t
    AlignerNN::align() {
	// ------------------------------------------------------------
	// computes D matrix (if not already done) and then does the alignment on the top level
	// ------------------------------------------------------------
	if (!D_created)
	    {
		stopwatch.start("alignD");
		align_D();
		stopwatch.stop("alignD");
	    }

	if (params->sequ_local_) {
	    throw failure("sequ_local is not supported by sparse");
	} else { // sequence global alignment

	    // align toplevel globally with potentially free endgaps
	    //(as given by description params->free_endgaps) return
	    //align_top_level_free_endgaps(); 
	    //tomark: implement align_top_level_free_endgaps()
	    
//	    seq_pos_t ps_al = r.startA()-1; //ends of pseudo-arc match
	    seq_pos_t ps_ar = r.endA()+1;
//	    seq_pos_t ps_bl = r.startB()-1;
	    seq_pos_t ps_br = r.endB()+1;

		//TODO: IMPORTANT: Hopefully index 0 is asigned to the pseudo arc!
	    matidx_t last_index_A = mapper_arcsA.number_of_valid_mat_pos(bpsA.num_bps())-1;
	    seq_pos_t last_valid_seq_pos_A = mapper_arcsA.get_pos_in_seq_new(bpsA.num_bps(), last_index_A);
	    matidx_t last_index_B = mapper_arcsB.number_of_valid_mat_pos(bpsB.num_bps())-1;
	    seq_pos_t last_valid_seq_pos_B = mapper_arcsB.get_pos_in_seq_new(bpsB.num_bps(), last_index_B);
	    if(trace_debugging_output) {
		std::cout << "Align top level with "
			  << ", last_index_A:" << last_index_A 
			  << "/last_seq_posA:" << last_valid_seq_pos_A 
			  << ", last_index_B:" << last_index_B 
			  << "/last_seq_posB:" << last_valid_seq_pos_B
			  << std::endl;
	    }
	    
	    // stopwatch.start("align top level");
		scoring->set_closing_arcs(BasePairs__Arc(bpsA.num_bps(), 0, seqA.length()+1),BasePairs__Arc(bpsB.num_bps(), 0, seqB.length()+1)); //TODO: check it
		std::cout << "align top level" << std::endl;

		fill_M_entries(BasePairs__Arc(bpsA.num_bps(), 0, seqA.length()+1),BasePairs__Arc(bpsB.num_bps(), 0, seqB.length()+1));

	    // tocheck: always use get_startA-1 (not zero) in
	    // sparsification_mapper and other parts
	    // stopwatch.stop("align top level");
	    
	    if (trace_debugging_output) std::cout << "M matrix: " << bpsA.num_bps() << " ," << last_index_A <<
	    		"  , " <<  bpsB.num_bps() <<  "," << last_index_B << std::endl
						  << M << std::endl;
	    if (trace_debugging_output) {
		std::cout << "M(" << last_index_A << "," 
			  << last_index_B << ")=" 
			  << M( last_index_A, last_index_B)
			  << " getGapCostBetween are:"
			  << getGapCostBetween( last_valid_seq_pos_A, ps_ar, true) << std::endl;
		// << " " << getGapCostBetween( last_valid_seq_pos_B,ps_br, false)
		// << std::endl;
	    }

	    return M( last_index_A, last_index_B)
		//toask: where should we care about non_default scoring views
		+ getGapCostBetween( last_valid_seq_pos_A, ps_ar, true)
		+ getGapCostBetween( last_valid_seq_pos_B, ps_br, false); //no free end gaps
	}
    }

    // ------------------------------------------------------------

    template <class ScoringView>
    void AlignerNN::trace_IX(ArcIdx idxX, matidx_t i_index,
			     const Arc &arcY, bool isA, ScoringView sv)
    {
	const BasePairs &bpsX = isA? bpsA : bpsB;
	const SparsificationMapper &mapper_arcsX = isA ? mapper_arcsA: mapper_arcsB;
	bool constraints_aligned_pos = false;
	pos_type xl = get_arc_leftend(idxX, isA);

	seq_pos_t i_seq_pos = mapper_arcsX.get_pos_in_seq_new(idxX, i_index);
	if (trace_debugging_output) std::cout << "****trace_IX****" << (isA?"A ":"B ") << " (" << xl << ","<< i_seq_pos << "] , " << arcY << std::endl;



	if ( i_seq_pos <= xl )
	    {
		for (size_type k = arcY.left()+1; k < arcY.right(); k++) {
		    if (isA)
			alignment.append(-2,k);
		    else
			alignment.append(k,-2);
		}
		return;
	    }

	// base del and ins

	if( !constraints_aligned_pos )
	    {
		seq_pos_t i_prev_seq_pos = xl;
		if (i_index > 0)
			i_prev_seq_pos = mapper_arcsX.get_pos_in_seq_new(idxX, i_index-1);


		infty_score_t gap_score =  getGapCostBetween(i_prev_seq_pos, i_seq_pos, isA)  + sv.scoring()->gapX(i_seq_pos, isA);
		if( gap_score.is_finite() )
		    {    	// convert the base gap score to the loop gap score
			gap_score  = (infty_score_t)(sv.scoring()->loop_indel_score( gap_score.finite_value())); // todo: unclean interface and casting
			if (IX(i_index, arcY, isA) == IX(i_index-1, arcY, isA) + gap_score )
			    {
				trace_IX( idxX, i_index-1, arcY, isA, sv);
				for ( size_type k = i_prev_seq_pos + 1; k <= i_seq_pos; k++)
				    {
					if (isA)
					    alignment.append(k, -2);
					else
					    alignment.append(-2, k);
				    }
				return;
			    }
		    }
	    }



	const ArcIdxVec &arcIdxVecX = mapper_arcsX.valid_arcs_right_adj(idxX, i_index);

	for (ArcIdxVec::const_iterator arcIdx = arcIdxVecX.begin(); arcIdx != arcIdxVecX.end(); ++arcIdx)
	{
		const Arc& arcX = bpsX.arc(*arcIdx);
		if (trace_debugging_output) std::cout << "arcX=" << arcX  << std::endl;

	
		infty_score_t gap_score =  getGapCostBetween(xl, arcX.left(), isA);

		if (gap_score.is_finite())
		    {    // convert the base gap score to the loop gap score
			gap_score = (infty_score_t)(sv.scoring()->loop_indel_score( gap_score.finite_value()));
//			 std::cout << "IBDmat(arcY.idx(),arcX.idx()) before:" << IBDmat(arcY.idx(),arcX.idx()) <<std::endl;

//			std::cout << "*** sv.D(" << arcX << "," <<  arcY << "," <<  isA << ")=" << sv.D(arcX, arcY, isA) << std::endl;
//			assert (! (IXD(arcX, arcY, isA) == infty_score_t::neg_infty) );
			infty_score_t arc_indel_score_extend = IXD(arcX, arcY, isA) + sv.scoring()->arcDel(arcX, isA) + gap_score;
			infty_score_t arc_indel_score_open = sv.D(arcX, arcY, isA) + sv.scoring()->arcDel(arcX, isA) + gap_score + sv.scoring()->indel_opening_loop();

//			std::cout << "arc_indel_score_open:" << arc_indel_score_open << "=" << sv.D(arcX, arcY, isA)  << "+" <<  sv.scoring()->arcDel(arcX, isA) << "+" <<  gap_score << "+" << sv.scoring()->indel_opening_loop() << std::endl;
//			std::cout << "arc_indel_score_extend: " << arc_indel_score_extend << "=" << IXD(arcX, arcY, isA) << "+" <<  sv.scoring()->arcDel(arcX, isA) << "+" <<  gap_score << std::endl;
			if ( IX(i_index, arcY, isA) == arc_indel_score_extend) {

			    if (trace_debugging_output) std::cout << "Arc Deletion extension for X " << (isA?"A ":"B ") << "arcX=" << arcX << " arcY=" << arcY << std::endl;
			    if (isA)
				{
				    alignment.add_basepairA(arcX.left(), arcX.right());
				    for (size_type k = xl+1; k <= arcX.left(); k++) {
					alignment.append(k, -2);
				    }
//				    IADmat(arcX.idx(),arcY.idx()) = sv.D(arcX, arcY); //tocheck: donna know why :-D
				    trace_IXD(arcX, arcY, isA, sv);

				    alignment.append(arcX.right(), -2);
				}
			    else
				{
				    alignment.add_basepairB(arcX.left(), arcX.right());
				    for (size_type k = xl+1; k <= arcX.left(); k++) {
					alignment.append(-2, k);
				    }
//				    IBDmat(arcY.idx(),arcX.idx()) = sv.D(arcY, arcX); //tocheck: donna know why :-D
				    trace_IXD(arcY, arcX, isA, sv);

				    alignment.append(-2, arcX.right());

				}
			    return;
			}


			if ( IX(i_index, arcY, isA) == arc_indel_score_open) {

			    if (trace_debugging_output) std::cout << "Arc Deletion opening for X " << (isA?"A ":"B ") << std::endl;
			    if (isA)
				{
				    alignment.add_deleted_basepairA(arcX.left(), arcX.right());
				    for (size_type k = xl+1; k <= arcX.left(); k++) {
					alignment.append(k, -2);
				    }

				    trace_D(arcX, arcY, sv);

				    alignment.append(arcX.right(), -2);
				}
			    else
				{
				    alignment.add_deleted_basepairB(arcX.left(), arcX.right());
				    for (size_type k = xl+1; k <= arcX.left(); k++) {
					alignment.append(-2, k);
				    }

				    trace_D(arcY, arcX, sv);

				    alignment.append(-2, arcX.right());

				}
			    return;
			}

		    }
	}
	std::cerr << "WARNING: trace_IX No trace found!" << std::endl;

    }
    // AlignerNN: traceback
    template<class ScoringView>
    void AlignerNN::trace_IXD(const Arc &arcA, const Arc &arcB,bool isA, ScoringView sv) {

	if (trace_debugging_output) std::cout << "****trace_IXD****" << (isA?"A ":"B ") << arcA << " " << arcB  << std::endl;
	assert(! params->struct_local_);


	ArcIdx idxA = arcA.idx();
	ArcIdx idxB = arcB.idx();

//	seq_pos_t al = arcA.left();
	seq_pos_t ar_seq_pos = arcA.right();
//	seq_pos_t bl = arcB.left();
	seq_pos_t br_seq_pos = arcB.right();


	// --------------------
	// case of stacking: not supported
	assert(! scoring->stacking());

	// --------------------
	// handle the case that arc match is not stacked

	if (isA)//trace IAD
	    {
		matidx_t ar_prev_mat_idx_pos = mapper_arcsA.first_valid_mat_pos_before(idxA, ar_seq_pos, arcA.left());
		seq_pos_t ar_prev_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, ar_prev_mat_idx_pos);
		infty_score_t jumpGapCostA = getGapCostBetween(ar_prev_seq_pos, ar_seq_pos, true);

		//first compute IA
		traceback_closing_arcA = Arc(0, arcA.left(), arcA.right());
		sv.scoring()->set_closing_arcs(traceback_closing_arcA, traceback_closing_arcB);
		fill_IA_entries(idxA, arcB, ar_seq_pos);
//		std::cout << "    IAD:" << IADmat(arcA.idx(), arcB.idx()) << "?=" << IA( ar_prev_mat_idx_pos, arcB ) << "+" << jumpGapCostA << std::endl;
//		assert (! (IADmat(arcA.idx(), arcB.idx()) == infty_score_t::neg_infty) );

		if (IADmat(arcA.idx(), arcB.idx())  == IA( ar_prev_mat_idx_pos, arcB ) + jumpGapCostA )
		    {
			trace_IX(idxA, ar_prev_mat_idx_pos, arcB, true, sv);
			for ( size_type k = ar_prev_seq_pos + 1; k < ar_seq_pos; k++)
			    {
				alignment.append(k, -1);
			    }
			return;
		    }
	    }
	else //trace IBD
	    {
		matidx_t br_prev_mat_idx_pos = mapper_arcsB.first_valid_mat_pos_before(idxB, br_seq_pos, arcB.left()); //tocheck: ar or ar-1?
		seq_pos_t br_prev_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB, br_prev_mat_idx_pos);
		infty_score_t jumpGapCostB = getGapCostBetween(br_prev_seq_pos, br_seq_pos, false);

		traceback_closing_arcB = Arc(0, arcB.left(), arcB.right());
		sv.scoring()->set_closing_arcs(traceback_closing_arcA, traceback_closing_arcB);
		fill_IB_entries(arcA, idxB, br_seq_pos);
		if (trace_debugging_output)
		    std::cout << "IXD(" << arcA.idx() << "," << arcB.idx() << ")="  << IBDmat(arcA.idx(), arcB.idx()) << " ?== " << IB(arcA, br_prev_mat_idx_pos ) << "+" << jumpGapCostB << std::endl;

		if (IBDmat(arcA.idx(), arcB.idx())  ==  IB(arcA, br_prev_mat_idx_pos ) + jumpGapCostB )
		    {

			trace_IX(idxB, br_prev_mat_idx_pos, arcA, false, sv);
			for ( size_type k = br_prev_seq_pos + 1; k < br_seq_pos; k++)
			    {
				alignment.append(-1, k);
			    }
			return;
		    }
	    }
	std::cerr << "WARNING: trace_IXD No trace found!" << std::endl;

	return;
    }


    // AlignerNN: traceback
    template<class ScoringView>
    void AlignerNN::trace_D(const Arc &arcA, const Arc &arcB, ScoringView sv) {
	
	assert(!params->no_lonely_pairs_); //take special care of noLP in this method

	if (trace_debugging_output) std::cout << "****trace_D****" << arcA << " " << arcB  << "sv.D(arcA, arcB)=" << sv.D(arcA, arcB) <<std::endl;
	assert(! params->struct_local_);

	traceback_closing_arcA = Arc(0, arcA.left(), arcA.right());
	traceback_closing_arcB = Arc(0, arcB.left(), arcB.right());
	sv.scoring()->set_closing_arcs(traceback_closing_arcA, traceback_closing_arcB);
	is_innermost_arcA = true;
	is_innermost_arcB = true;
	ArcIdx idxA = arcA.idx();
	ArcIdx idxB = arcB.idx();
//	seq_pos_t al = arcA.left();
	seq_pos_t ar_seq_pos = arcA.right();
//	seq_pos_t bl = arcB.left();
	seq_pos_t br_seq_pos = arcB.right();


	// --------------------
	// case of stacking: not supported
	assert(! scoring->stacking());

	if(params->multiloop_deletion_> 0) {
		// --------------------
		// Case domain  deletion
		if (arcB.idx() == bpsB.num_bps()) {
				fill_IA_entries(idxA, arcB, ar_seq_pos);
				matidx_t ar_prev_mat_idx_pos = mapper_arcsA.number_of_valid_mat_pos(idxA)-1; //TODO: VERY IMPORTNANT: -1 or not?

				seq_pos_t ar_prev_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, ar_prev_mat_idx_pos);
				infty_score_t jumpGapCostA = (infty_score_t)sv.scoring()->loop_indel_score(
						getGapCostBetween(ar_prev_seq_pos, ar_seq_pos, true).finite_value());
				infty_score_t ia= IAmat(ar_prev_mat_idx_pos,arcB.idx()) + jumpGapCostA;

				if ( sv.D(arcA, arcB) == ia ) {
					if (trace_debugging_output) std::cout << "     trace_D domain deletion" << std::endl;

					trace_IX(idxA, ar_prev_mat_idx_pos, arcB, true, sv);
					for ( size_type k = ar_prev_seq_pos + 1; k < ar_seq_pos; k++)
						{
						alignment.append(k, -1);
						}
					return;
				}
				else {
					std::cerr << "No Trace was found! ****trace_D****" << arcA << " " << arcB  << std::endl;
					return;
				}
		//				D(*arcA, empty_arcB) = IAmat(arcA->right()-1, empty_arcB.idx());
		//				fill_D_entry(*arcA, empty_arcB);

				//	    stopwatch.stop("compM");

		}



		// --------------------
		// Case domain insertion
		if (arcA.idx() == bpsA.num_bps()) {
				fill_IB_entries(arcA, idxB, br_seq_pos);
				matidx_t br_prev_mat_idx_pos = mapper_arcsB.number_of_valid_mat_pos(idxB)-1; //TODO: VERY IMPORTNANT: -1 or not?
				seq_pos_t br_prev_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB, br_prev_mat_idx_pos);
				infty_score_t jumpGapCostB = getGapCostBetween(br_prev_seq_pos, br_seq_pos, false);

				infty_score_t ib= IBmat(arcA.idx(), br_prev_mat_idx_pos) + jumpGapCostB;
				if ( sv.D(arcA, arcB) == ib ) {
					if (trace_debugging_output) std::cout << "     trace_D domain insertion" << std::endl;

					trace_IX(idxB, br_prev_mat_idx_pos, arcA, false, sv);
					for ( size_type k = br_prev_seq_pos + 1; k < br_seq_pos; k++)
						{
						alignment.append(-1, k);
						}
					return;
				}
				else {
					std::cerr << "No Trace was found! ****trace_D****" << arcA << " " << arcB  << std::endl;
					return;
				}
		//				D(*arcA, empty_arcB) = IAmat(arcA->right()-1, empty_arcB.idx());
		//				fill_D_entry(*arcA, empty_arcB);

				//	    stopwatch.stop("compM");

		}
	}
	// --------------------
	// now handle the case that arc match is not stacked


	matidx_t ar_prev_mat_idx_pos = mapper_arcsA.first_valid_mat_pos_before(idxA, ar_seq_pos,arcA.left());
	seq_pos_t ar_prev_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, ar_prev_mat_idx_pos);
	infty_score_t jumpGapCostA = getGapCostBetween(ar_prev_seq_pos, ar_seq_pos, true);

	matidx_t br_prev_mat_idx_pos = mapper_arcsB.first_valid_mat_pos_before(idxB, br_seq_pos,arcB.left()); //tocheck: ar or ar-1?
	seq_pos_t br_prev_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB, br_prev_mat_idx_pos);
	infty_score_t jumpGapCostB = getGapCostBetween(br_prev_seq_pos, br_seq_pos, false);

	// first recompute M
	fill_M_entries(arcA, arcB);


	//-----three cases for gap extension/initiation ---

	score_t opening_cost_A=0;
	if (ar_prev_seq_pos < (ar_seq_pos - 1)) {
	    //implicit base deletion because of sparsification
	    opening_cost_A = sv.scoring()->indel_opening();
	}

	score_t opening_cost_B=0;
	if (br_prev_seq_pos < (br_seq_pos - 1)) { //implicit base insertion because of sparsification
	    opening_cost_B = sv.scoring()->indel_opening();
	}

	infty_score_t gap_score = jumpGapCostA + jumpGapCostB;
	if (trace_debugging_output) {
		std::cout <<  "sv.D(" << arcA << ", " << arcB << ")=" <<sv.D(arcA, arcB) << "?==" << (infty_score_t)(gap_score + opening_cost_A + opening_cost_B +  M(ar_prev_mat_idx_pos, br_prev_mat_idx_pos) )
				<< std::endl;
		std::cout << "?==" << gap_score << "+" << opening_cost_A<< "+" <<
				opening_cost_B << "+" <<  "M(" << ar_prev_mat_idx_pos << br_prev_mat_idx_pos << "):"<<M(ar_prev_mat_idx_pos, br_prev_mat_idx_pos) << std::endl;
	}
	score_t inner_score = 0;
	if (do_cond_bottom_up)
		{if (is_innermost_arcA || is_innermost_arcB) {
			inner_score = sv.scoring()->arcmatch(arcA, arcB, false, true);
		}}

	if (sv.D(arcA, arcB) == (infty_score_t)(gap_score + opening_cost_B + inner_score+ Emat(ar_prev_mat_idx_pos, br_prev_mat_idx_pos)))
	    {
		trace_E(idxA, ar_prev_mat_idx_pos, idxB, br_prev_mat_idx_pos, false, def_scoring_view);
	    }
	else if (sv.D(arcA, arcB) == (infty_score_t)(gap_score + opening_cost_A + inner_score+ Fmat(ar_prev_mat_idx_pos, br_prev_mat_idx_pos)))
	    {
		trace_F(idxA, ar_prev_mat_idx_pos, idxB, br_prev_mat_idx_pos, false, def_scoring_view);
	    }
	else if (sv.D(arcA, arcB) == (infty_score_t)(gap_score + opening_cost_A + inner_score+ opening_cost_B +  M(ar_prev_mat_idx_pos, br_prev_mat_idx_pos) ))
	    {
//		std::cout << "traceD-M " << arcA << "  " << arcB << std::endl;
		trace_M(idxA, ar_prev_mat_idx_pos, idxB, br_prev_mat_idx_pos, false, def_scoring_view);
	    }
	else //todo: throw exception?
	{
		//first compute IA
		fill_IA_entries(idxA, arcB, ar_seq_pos);
		if ( sv.D(arcA, arcB) == IA( ar_prev_mat_idx_pos, arcB ) + jumpGapCostA )
			{
			assert(IADmat(arcA.idx(),arcB.idx()) == infty_score_t::neg_infty || IADmat(arcA.idx(),arcB.idx()) == sv.D(arcA, arcB));
			//		IADmat(arcA.idx(),arcB.idx()) = sv.D(arcA, arcB); //tocheck: prevent recomputation

			trace_IX(idxA, ar_prev_mat_idx_pos, arcB, true, sv);
			for ( size_type k = ar_prev_seq_pos + 1; k < ar_seq_pos; k++)
				{
				alignment.append(k, -1);
				}
			return;
			}

		fill_IB_entries(arcA, idxB, br_seq_pos);
		if (sv.D(arcA, arcB) ==  IB(arcA, br_prev_mat_idx_pos ) + jumpGapCostB )
			{
			assert(IBDmat(arcA.idx(),arcB.idx()) == infty_score_t::neg_infty || IBDmat(arcA.idx(), arcB.idx()) == sv.D(arcA, arcB));
	//		IBDmat(arcA.idx(),arcB.idx()) = sv.D(arcA, arcB); //tocheck: prevent recomputation

			trace_IX(idxB, br_prev_mat_idx_pos, arcA, false, sv);
			for ( size_type k = br_prev_seq_pos + 1; k < br_seq_pos; k++)
				{
				alignment.append(-1, k);
				}
			return;
			}

		std::cerr << "No Trace was found! ****trace_D****" << arcA << " " << arcB  << std::endl;
	}
	//------------------------------


	for ( size_type k = ar_prev_seq_pos + 1; k < ar_seq_pos; k++)
	    {
		alignment.append(k, -1);
	    }
	for ( size_type k = br_prev_seq_pos + 1; k < br_seq_pos; k++)
	    {
		alignment.append(-1, k);
	    }

	return;
    }

    // looks like we don't use this
    // template<class ScoringView>
    // void AlignerNN::trace_D(const ArcMatch &am, ScoringView sv) {

    // 	trace_D(am.arcA(), am.arcB(), sv);
    // }

    // do the trace for base deletion within one arc match.
    // only the cases without exclusions are considered
    template <class ScoringView>
    void AlignerNN::trace_E(ArcIdx idxA, matidx_t i_index, ArcIdx idxB, matidx_t j_index, bool top_level, ScoringView sv)
    {
	seq_pos_t i_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, i_index);
	if (trace_debugging_output) std::cout << "******trace_E***** " << " idxA:" << idxA << " idxB:"<< idxB << " i:" << i_seq_pos << " :: " <<  Emat(i_index,j_index) << std::endl;


	seq_pos_t i_prev_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, i_index-1); //TODO: Check border i_index==1,0


	// base del
	infty_score_t gap_cost =
	    getGapCostBetween(i_prev_seq_pos, i_seq_pos, true) + sv.scoring()->gapA(i_seq_pos);
	if (Emat(i_index, j_index) == gap_cost + Emat(i_index-1,j_index) )
	    {
		if (trace_debugging_output) {
		    std::cout << "base deletion E" << i_index-1 << " , " << j_index << std::endl;
		}
		trace_E(idxA, i_index-1, idxB, j_index, top_level, sv);
		alignment.append(i_seq_pos, -1);
		return;
	    }
	else  if (Emat(i_index, j_index) == M(i_index-1, j_index) + gap_cost + sv.scoring()->indel_opening())
	    {
		if (trace_debugging_output) {
		    std::cout << "base deletion M" << i_index-1
			      << " , " << j_index << std::endl;
		}
		trace_M(idxA, i_index-1, idxB, j_index, top_level, sv);
		alignment.append(i_seq_pos, -1);
		return;
	    }
	    std::cerr << "WARNING: trace_E No trace found!" << std::endl;
    }

    template <class ScoringView>
    void AlignerNN::trace_F(ArcIdx idxA, matidx_t i_index, ArcIdx idxB, matidx_t j_index, bool top_level, ScoringView sv)
    {

	seq_pos_t j_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB, j_index);

	if (trace_debugging_output) {
	    std::cout << "******trace_F***** " << " idxA:" << idxA << " idxB:"<< idxB << " j:" << j_seq_pos << " :: " <<  Fmat(i_index,j_index) << std::endl;
	}
	

	seq_pos_t j_prev_seq_pos =
	    mapper_arcsB.get_pos_in_seq_new(idxB, j_index-1); //TODO: Check border j_index==1,0

	// base ins
	infty_score_t gap_cost =
	    getGapCostBetween(j_prev_seq_pos, j_seq_pos, false) + sv.scoring()->gapB(j_seq_pos);
	
	if (Fmat(i_index, j_index) == Fmat(i_index,j_index-1) + gap_cost)
	    {
		if (trace_debugging_output) std::cout << "base insertion F" << i_index << " , " << j_index-1 << std::endl;
		trace_F(idxA, i_index, idxB, j_index-1, top_level, sv);
		alignment.append(-1, j_seq_pos);
		return;
	    }
	else if (Fmat(i_index, j_index) == M(i_index, j_index-1) + gap_cost  + sv.scoring()->indel_opening())
	    {
		if (trace_debugging_output) std::cout << "base insertion M" << i_index << " , " << j_index-1 << std::endl;
		trace_M(idxA, i_index, idxB, j_index-1, top_level, sv);
		alignment.append(-1, j_seq_pos);
		return;
	    }
	std::cerr << "WARNING: trace_F No trace found!" << std::endl;
    }

    pos_type AlignerNN::get_arc_leftend(ArcIdx &idxX, bool isA) {
    	if (isA){
    		if (idxX==bpsA.num_bps())
    			return 0;
    		else
    			return bpsA.arc(idxX).left();
    	}
    	else {
    		if (idxX==bpsB.num_bps())
				return 0;
			else
				return bpsB.arc(idxX).left();
    	}
    }

    // trace and handle all cases that do not involve exclusions
    template<class ScoringView>
    void AlignerNN::trace_M_noex(ArcIdx idxA, matidx_t i_index, ArcIdx idxB, matidx_t j_index, bool top_level, ScoringView sv)
    {
	seq_pos_t i_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, i_index);
	seq_pos_t j_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB, j_index);



	pos_type al = get_arc_leftend(idxA, true);
	pos_type bl = get_arc_leftend(idxB, false);

	if ( i_seq_pos == al && j_seq_pos == bl )
	    return;

	seq_pos_t i_prev_seq_pos = al; //tocheck: Important
	if ( i_seq_pos > al )
	    i_prev_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, i_index-1); //TODO: Check border i_index==1,0
	seq_pos_t j_prev_seq_pos = bl;
	if (j_seq_pos > bl )
	    j_prev_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB, j_index-1); //TODO: Check border j_index==1,0
	bool constraints_alowed_edge = true; // constraints are not considered,  params->constraints_->allowed_edge(i_seq_pos, j_seq_pos)
	bool constraints_aligned_pos_A = false; // TOcheck: Probably unnecessary, constraints are not considered
	bool constraints_aligned_pos_B = false; // TOcheck: Probably unnecessary, constraints are not considered
	// determine where we get M(i,j) from



//	 std::cout << i_index << "-" << j_index << " " << i_seq_pos << "-" << j_seq_pos << "  " << al << "-" << bl <<std::endl;


	if (  i_seq_pos > al && j_seq_pos > bl &&
	      constraints_alowed_edge )
	    {

		//------------------------------------
		// calculate possible opening gap costs
		infty_score_t opening_cost_A;
		if (i_prev_seq_pos < (i_seq_pos - 1)) //implicit base deletion because of sparsification
		    opening_cost_A = (infty_score_t)(sv.scoring()->indel_opening());
		else
		    opening_cost_A = (infty_score_t)0;

		infty_score_t opening_cost_B;
		if (j_prev_seq_pos < (j_seq_pos - 1)) //implicit base insertion because of sparsification
		    opening_cost_B = (infty_score_t)(sv.scoring()->indel_opening());
		else
		    opening_cost_B  = (infty_score_t)0;
		//------------------------------------
		// base match

		infty_score_t gap_match_score = getGapCostBetween(i_prev_seq_pos, i_seq_pos, true) +
				getGapCostBetween(j_prev_seq_pos, j_seq_pos, false) + (sv.scoring()->basematch(i_seq_pos, j_seq_pos));
		//base match and continue with deletion
		if (M(i_index,j_index) == (infty_score_t)(gap_match_score + opening_cost_B + Emat(i_index-1, j_index-1)) )
		    {
			if (trace_debugging_output) std::cout << "base match E" << i_index << " , " << j_index << std::endl;
			trace_E(idxA, i_index-1, idxB, j_index-1, top_level, sv );
			alignment.append(i_seq_pos,j_seq_pos);
			return;

		    }
		else   	//base match and continue with insertion
		    if (M(i_index,j_index) == (infty_score_t)(gap_match_score + opening_cost_A + Fmat(i_index-1, j_index-1)) )
			{
			    if (trace_debugging_output) std::cout << "base match F" << i_index << " , " << j_index << std::endl;
			    trace_F(idxA, i_index-1, idxB, j_index-1, top_level, sv );
			    alignment.append(i_seq_pos,j_seq_pos);
			    return;
			}
		    else	//base match, then continue with M case again, so both gap opening costs(if possible) should be included
			if (M(i_index,j_index) == (infty_score_t)(gap_match_score + opening_cost_A + opening_cost_B + M(i_index-1, j_index-1)) )
			    {
				if (trace_debugging_output) std::cout << "base match M" << i_index << " , " << j_index << std::endl;
				trace_M(idxA, i_index-1, idxB, j_index-1, top_level, sv);
				alignment.append(i_seq_pos,j_seq_pos);
				return;
			    }

		/*	for ( size_type k = i_prev_seq_pos + 1; k < i_seq_pos; k++)
			{
			alignment.append(k, -1);
			}
			for ( size_type k = j_prev_seq_pos + 1; k < j_seq_pos; k++)
			{
			alignment.append(-1, k);
			}
		*/
	    }

	// base deletion
	if (  i_seq_pos > al &&
	      !constraints_aligned_pos_A
	      && M(i_index,j_index) == Emat(i_index, j_index) )
	    {

		if (trace_debugging_output) std::cout << "base deletion E" << i_index << " , " << j_index << std::endl;

		trace_E(idxA, i_index, idxB, j_index, top_level, sv);
		/* for ( size_type k = i_prev_seq_pos + 1; k <= i_seq_pos; k++)
		   {
		   alignment.append(k, -1);
		   }*/
		return;
	    }

	// base insertion
	if (  j_seq_pos > bl &&
	      !constraints_aligned_pos_B
	      && M(i_index,j_index) == Fmat(i_index, j_index) )
	    {
		if (trace_debugging_output) std::cout << "base insertion F" << i_index << " , " << j_index << std::endl;

		trace_F(idxA, i_index, idxB, j_index, top_level, sv);
		/*for ( size_type k = j_prev_seq_pos + 1; k <= j_seq_pos; k++)
		  {
		  alignment.append(-1, k);
		  }*/
		return;
	    }



	// only consider arc match cases if edge (i,j) is allowed and valid! (assumed valid)
	if ( ! constraints_alowed_edge  )
	    {
		std::cerr << "WARNING: unallowed edge" << std::endl;
		return;
	    }


	// here (i,j) is allowed and valid,
	const ArcIdxVec& arcsA = mapper_arcsA.valid_arcs_right_adj(idxA, i_index);
	const ArcIdxVec& arcsB = mapper_arcsB.valid_arcs_right_adj(idxB, j_index);

	if(params->multiloop_deletion_> 0) {
		//  domain ins/del
		//domain deltion
		for (ArcIdxVec::const_iterator arcAIdx = arcsA.begin();
		 arcAIdx != arcsA.end();
		 ++arcAIdx) {
			const Arc& arcA = bpsA.arc(*arcAIdx);
			matidx_t  arcA_left_index_before   =
			mapper_arcsA.first_valid_mat_pos_before(idxA, arcA.left(), al);
			seq_pos_t arcA_left_seq_pos_before =
			mapper_arcsA.get_pos_in_seq_new(idxA, arcA_left_index_before);

			score_t opening_cost_A = 0;
//			if (arcA_left_seq_pos_before < (arcA.left() - 1)) {
//			// implicit base deletion because of sparsification
//			score_t opening_cost_A = sv.scoring()->indel_opening();
//			}

			Arc empty_arcB = BasePairs__Arc(bpsB.num_bps(), 0, 0);
			sv.scoring()->set_closing_arcs(traceback_closing_arcA, traceback_closing_arcB);

			infty_score_t arc_indel_score_open = (infty_score_t)scoring->loop_indel_score(
					getGapCostBetween( arcA_left_seq_pos_before, arcA.left(), true).finite_value()) +
			sv.D(arcA, empty_arcB) + sv.scoring()->arcDel(arcA, true)
			 + sv.scoring()->indel_opening_loop();


			tainted_infty_score_t domain_del_score =
				arc_indel_score_open
				+ opening_cost_A
				+ M(arcA_left_index_before, j_index);

			if ( M(i_index, j_index) == domain_del_score ) {


				if (trace_debugging_output) std::cout << "domain del M"<< arcA   << std::endl;

				trace_M(idxA, arcA_left_index_before, idxB, j_index, top_level, sv);


				alignment.add_basepairA(arcA.left(), arcA.right());
				alignment.append(arcA.left(),-1);

				// do the trace below the arc match

				trace_D(arcA, empty_arcB, sv);
				alignment.append(arcA.right(),-1);
				return;
			}


		}

		//domain insertion
		for (ArcIdxVec::const_iterator arcBIdx = arcsB.begin();
		 arcBIdx != arcsB.end();
		 ++arcBIdx) {
			const Arc& arcB = bpsB.arc(*arcBIdx);
			matidx_t  arcB_left_index_before   =
			mapper_arcsB.first_valid_mat_pos_before(idxB, arcB.left(), bl);
			seq_pos_t arcB_left_seq_pos_before =
			mapper_arcsB.get_pos_in_seq_new(idxB, arcB_left_index_before);

			score_t opening_cost_B = 0;
//			if (arcB_left_seq_pos_before < (arcB.left() - 1)) {
//			// implicit base deletion because of sparsification
//			score_t opening_cost_B = sv.scoring()->indel_opening();
//			}

			Arc empty_arcA = BasePairs__Arc(bpsA.num_bps(), 0, 0);
			sv.scoring()->set_closing_arcs(traceback_closing_arcA, traceback_closing_arcB);

			infty_score_t arc_indel_score_open = getGapCostBetween( arcB_left_seq_pos_before, arcB.left(), false) +
			sv.D(empty_arcA, arcB) + sv.scoring()->arcDel(arcB, false)
			 + sv.scoring()->indel_opening_loop();


			tainted_infty_score_t domain_ins_score =
				arc_indel_score_open
				+ opening_cost_B
				+ M(i_index, arcB_left_index_before);

			if ( M(i_index, j_index) == domain_ins_score ) {


				if (trace_debugging_output) std::cout << "domain ins M"<< arcB   << std::endl;

				trace_M(idxA, i_index, idxB, arcB_left_index_before, top_level, sv);


				alignment.add_basepairB(arcB.left(), arcB.right());
				alignment.append(-1, arcB.left());

				// do the trace below the arc match

				trace_D(empty_arcA, arcB, sv);
				alignment.append(-1, arcB.right());
				return;
			}


		}
	}
	//  arc match


	for (ArcIdxVec::const_iterator arcAIdx = arcsA.begin(); 
	     arcAIdx != arcsA.end(); ++arcAIdx) {

		const Arc& arcA = bpsA.arc(*arcAIdx);

		matidx_t arcA_left_index_before =
		    mapper_arcsA.first_valid_mat_pos_before(idxA, arcA.left(), al);

		seq_pos_t arcA_left_seq_pos_before =
		    mapper_arcsA.get_pos_in_seq_new(idxA, arcA_left_index_before);
		
		score_t opening_cost_A=0;
		if (arcA_left_seq_pos_before < (arcA.left() - 1)) {
		    //implicit base deletion because of sparsification
		    opening_cost_A = sv.scoring()->indel_opening();
		}

		for (ArcIdxVec::const_iterator arcBIdx = arcsB.begin(); 
		     arcBIdx != arcsB.end(); ++arcBIdx)
		    {

			const Arc& arcB = bpsB.arc(*arcBIdx);
			// std::cout << "trace_M_noex: arcA=" << arcA << " arcB=" << arcB << std::endl;
			matidx_t arcB_left_index_before =
			    mapper_arcsB.first_valid_mat_pos_before(idxB, arcB.left(), bl);
			seq_pos_t arcB_left_seq_pos_before =
			    mapper_arcsB.get_pos_in_seq_new(idxB, arcB_left_index_before);

			score_t opening_cost_B=0;
			if (arcB_left_seq_pos_before < (arcB.left() - 1)) {
			    //implicit base insertion because of sparsification
			    opening_cost_B = sv.scoring()->indel_opening();
			}
			sv.scoring()->set_closing_arcs(traceback_closing_arcA, traceback_closing_arcB);
			infty_score_t gap_match_score =
			    getGapCostBetween(arcA_left_seq_pos_before, arcA.left(), true)
			    + getGapCostBetween(arcB_left_seq_pos_before, arcB.left(), false)
			    + sv.D( arcA, arcB ) + sv.scoring()->arcmatch(arcA, arcB);


			//arc match, then continue with deletion
			if ( M(i_index, j_index) ==	(infty_score_t)(gap_match_score + opening_cost_B + Emat (arcA_left_index_before, arcB_left_index_before)) )
			    {
				if (trace_debugging_output) std::cout << "arcmatch E"<< arcA <<";"<< arcB << " :: "   << std::endl;

				trace_E(idxA, arcA_left_index_before, idxB, arcB_left_index_before, top_level, sv);


				/*for ( size_type k = arcA_left_seq_pos_before + 1; k < arcA.left(); k++)
				  {
				  alignment.append(k, -1);
				  }
				  for ( size_type k = arcB_left_seq_pos_before + 1; k < arcB.left(); k++)
				  {
				  alignment.append(-1, k);
				  }
				*/
				alignment.add_basepairA(arcA.left(), arcA.right());
				alignment.add_basepairB(arcB.left(), arcB.right());
				alignment.append(arcA.left(),arcB.left());


				// do the trace below the arc match

				trace_D(arcA, arcB, sv);
				alignment.append(arcA.right(),arcB.right());
				return;

			    }
			//arc match, then continue with insertion case
			else if ( M(i_index, j_index) ==
				  (infty_score_t)(gap_match_score + opening_cost_A + Fmat (arcA_left_index_before, arcB_left_index_before)) )
			    {

				if (trace_debugging_output) std::cout << "arcmatch F"<< arcA <<";"<< arcB << " :: "   << std::endl;

				trace_F(idxA, arcA_left_index_before, idxB, arcB_left_index_before, top_level, sv);


				/*for ( size_type k = arcA_left_seq_pos_before + 1; k < arcA.left(); k++)
				  {
				  alignment.append(k, -1);
				  }
				  for ( size_type k = arcB_left_seq_pos_before + 1; k < arcB.left(); k++)
				  {
				  alignment.append(-1, k);
				  }
				*/
				alignment.add_basepairA(arcA.left(), arcA.right());
				alignment.add_basepairB(arcB.left(), arcB.right());
				alignment.append(arcA.left(),arcB.left());

				// do the trace below the arc match

				trace_D(arcA, arcB, sv);
				alignment.append(arcA.right(),arcB.right());
				return;

			    }
			//arc match, then continue with general M case
			else if ( M(i_index, j_index) == gap_match_score  + opening_cost_A + opening_cost_B + M(arcA_left_index_before, arcB_left_index_before) )
			    {

				if (trace_debugging_output) std::cout << "arcmatch M"<< arcA <<";"<< arcB << " :: "   << std::endl;

				trace_M(idxA, arcA_left_index_before, idxB, arcB_left_index_before, top_level, sv);

				/*for ( size_type k = arcA_left_seq_pos_before + 1; k < arcA.left(); k++)
				  {
				  alignment.append(k, -1);
				  }
				  for ( size_type k = arcB_left_seq_pos_before + 1; k < arcB.left(); k++)
				  {
				  alignment.append(-1, k);
				  }
				*/
				alignment.add_basepairA(arcA.left(), arcA.right());
				alignment.add_basepairB(arcB.left(), arcB.right());
				alignment.append(arcA.left(),arcB.left());

				// do the trace below the arc match

				trace_D(arcA, arcB, sv);
				alignment.append(arcA.right(),arcB.right());
				return;
			    }
		    }
	    }

	 std::cerr << "WARNING: No trace found!" << std::endl;
    }

    // do the trace within one arc match.
    // the cases without exclusions are delegated to trace_noex
    template <class ScoringView>
    void
    AlignerNN::trace_M(ArcIdx idxA, matidx_t i_index, ArcIdx idxB, matidx_t j_index, bool top_level, ScoringView sv) {
	//pre: M matrices for arc computed
	assert(! params->sequ_local_); //Local seq alignment not implemented yet.

	seq_pos_t i_seq_pos = mapper_arcsA.get_pos_in_seq_new(idxA, i_index);
	seq_pos_t j_seq_pos = mapper_arcsB.get_pos_in_seq_new(idxB, j_index);
	if (trace_debugging_output) std::cout << "******trace_M***** " << " idxA:" << idxA << " i:" << i_seq_pos <<" idxB:"<< idxB << " j:" << j_seq_pos << " :: " <<  M(i_index,j_index) << std::endl;

	//    if ( i_seq_pos <= al ) {
	//	for (int k = bl+1; k <= j_seq_pos; k++) { //TODO: end gaps cost is not free
	//
	//	    if ( ((al == r.startA()-1) && mapperB.is_valid_pos_external(k))
	//		    || ( (al != r.startA()-1) && mapperB.is_valid_pos((k)) )
	//		alignment.append(-1,k);
	//
	//
	//	}
	//    }

	//    if (j_seq_pos <= bl) {
	//	for (int k = al+1;k <= i_seq_pos; k++) {
	//
	//	    if ( ((bl == r.startB()-1) && mapperA.is_valid_pos_external(k))
	//		    || ( (bl != r.startB()-1) && mapperA.is_valid_pos(k)) )
	//		alignment.append(k,-1);
	//
	//	}
	//	return;
	//    }


	trace_M_noex(idxA, i_index, idxB, j_index, top_level, sv);
    }

    template<class ScoringView>
    void
    AlignerNN::trace(ScoringView sv) {
	// pre: last call align_in_arcmatch(r.startA()-1,r.endA()+1,r.startB()-1,r.endB()+1);
	//      or align_top_level_locally for sequ_local alignent

	// reset the alignment strings (to empty strings)
	// such that they can be written again during the trace
	alignment.clear();

	// free end gap version: trace_M(r.startA()-1,max_i,r.startB()-1,max_j,true,sv);
//	seq_pos_t ps_al = r.startA() - 1;
	matidx_t last_mat_idx_pos_A = mapper_arcsA.number_of_valid_mat_pos(bpsA.num_bps()) -1;//tocheck: check the correctness
	//seq_pos_t last_seq_pos_A = mapperA.get_pos_in_seq_new(ps_al, last_mat_idx_pos_A);

//	seq_pos_t ps_bl = r.startB() - 1;
	matidx_t last_mat_idx_pos_B = mapper_arcsB.number_of_valid_mat_pos(bpsB.num_bps()) -1;//tocheck: check the correctness
	//seq_pos_t last_seq_pos_B = mapperB.get_pos_in_seq_new(ps_bl, last_mat_idx_pos_B);


	traceback_closing_arcA = Arc(bpsA.num_bps(), 0, seqA.length()+1);//TODO: What to set as index?
	traceback_closing_arcB = Arc(bpsB.num_bps(), 0, seqB.length()+1);//TODO: What to set as index?

	trace_M(bpsA.num_bps(), last_mat_idx_pos_A, bpsB.num_bps(), last_mat_idx_pos_B, true, sv); //TODO: right side for trace_M differs with align_M
	/*    for ( size_type k = last_seq_pos_A + 1; k <= r.endA(); k++)//tocheck: check the correctness
	      {
	      alignment.append(k, -1);
	      }
	      for ( size_type k = last_seq_pos_B + 1; k <= r.endB(); k++)//tocheck: check the correctness
	      {
	      alignment.append(-1, k);
	      }
	*/
    }

    void
    AlignerNN::trace() {
	stopwatch.start("trace");

	trace(def_scoring_view);

	stopwatch.stop("trace");
    }


    //! type of a task (used in computing k-best alignment)
    typedef std::pair<AlignerRestriction,infty_score_t> task_t;

} //end namespace LocARNA
