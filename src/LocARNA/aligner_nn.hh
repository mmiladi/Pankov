#ifndef LOCARNA_ALIGNER_NN_HH
#define LOCARNA_ALIGNER_NN_HH

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "sequence.hh"
#include "basepairs.hh"
#include "sparsification_mapper.hh"
#include "arc_matches.hh"
#include "alignment.hh"

#include "params.hh"
#include "scoring.hh"

#include "matrix.hh"

#include "aligner_restriction.hh"


namespace LocARNA {

    /**
     * \brief Implements SPARSE, next generation alignment algorithm for locarna

     * Performs the alignment of two sequences and their associated
     * sets of weighted basepairs

     * An object always knows about the two sequences and the two
     * weighted base pair sets

     * usage: construct, align, trace, get_alignment
     */
    class AlignerNN {
    public:
	typedef BasePairs__Arc Arc;	//!< type for an arc a.k.a base pair
	typedef SparsificationMapper::ArcIdx ArcIdx; //!< type for arc index
	typedef SparsificationMapper::ArcIdxVec ArcIdxVec; //!< vector of arc indices
	typedef SparsificationMapper::matidx_t matidx_t; //!< type for a matrix position
	typedef SparsificationMapper::seq_pos_t seq_pos_t; //!< type for a sequence position
	typedef SparsificationMapper::index_t index_t; //!< type for an index



	//! type of matrix M
	typedef ScoreMatrix M_matrix_t;

    private:

    bool trace_debugging_output; //!< a static switch to enable generating debugging logs
    bool do_cond_bottom_up;

    protected:
	const AlignerNParams *params; //!< the parameter for the alignment

	// TODO: const Scoring?
	Scoring *scoring; //!< the scores
	Scoring *mod_scoring; //!< used in normalized scoring, when we need to modify the scoring
	const Sequence &seqA; //!< sequence A
	const Sequence &seqB; //!< sequence B

	const SparsificationMapper& mapper_arcsA; //!< sparsification mapping for seq A indexed by arcs
	const SparsificationMapper& mapper_arcsB; //!< sparsification mapping for seq B indexed by arcs


	const ArcMatches &arc_matches; //!< the potential arc matches between A and B

	const BasePairs &bpsA; //!< base pairs of A
	const BasePairs &bpsB; //!< base pairs of B

	//! restriction of AlignerN
	AlignerRestriction r;

	//! matrix indexed by the arc indices of rnas A and B
	ScoreMatrix Dmat;

	//! matrix indexed by positions of elements of the seqA positions and the arc indices of RNA B
	ScoreMatrix IAmat;
	//! matrix indexed by positions of elements of the seqB positions and the arc indices of RNA A
	ScoreMatrix IBmat;


	//! matrix indexed by positions of elements of the seqA positions and the arc indices of RNA B
	ScoreMatrix IADmat;
	//! matrix indexed by positions of elements of the seqB positions and the arc indices of RNA A
	ScoreMatrix IBDmat;

	//! matrix for the affine gap cost model base deletion
	ScoreMatrix Emat;
	//! matrix for the affine gap cost model base insertion
	ScoreMatrix Fmat;

	/**
	 * @brief M matrix
	 *
	 * use only one M matrix (unlike in Aligner), since we don't handle structure locality
	 */
	M_matrix_t M;

	//! matrix to store cost of deleting/inserting a subsequence
	//! of sequence A, indexed by neighboring positions of the
	//! range
	ScoreMatrix gapCostAmat;
	
	//! matrix to store cost of deleting/inserting a subsequence
	//! of sequence B, indexed by neighboring positions of the
	//! range
	ScoreMatrix gapCostBmat; 


	int min_i; //!< subsequence of A left end, not used in sparse
	int min_j; //!< subsequence of B left end, not used in sparse
	int max_i; //!< subsequence of A right end, not used in sparse
	int max_j; //!< subsequence of B right end, not used in sparse

	bool D_created; //!< flag, is D already created?

	Alignment alignment; //!< resulting alignment

	// ============================================================
	/**
	 * @brief Provides the standard view on the scoring
	 * @see ModifiedScoringView
	 *
	 * @note We use a template-based scheme to switch between use of
	 * the unmodified score and the modified score without run-time
	 * penalty for the standard case the mechanism is used for methods
	 * compute_M_entry and trace_noex
	 */
	class UnmodifiedScoringViewNN {
	protected:
	    const AlignerNN *alignerNN_; //!< aligner object for that the view is provided
	public:

	    /**
	     * Construct for AlignerN object
	     *
	     * @param alignerN The aligner object
	     */
	    explicit
            UnmodifiedScoringViewNN(const AlignerNN *alignerNN): alignerNN_(alignerNN) {};

	    /**
	     * Get scoring object
	     *
	     * @return (unmodified) scoring object of aligner
	     */
	    // TODO: const
            Scoring *scoring() const {return alignerNN_->scoring;}

	    /**
	     * View on matrix D
	     *
	     * @param a arc in A
	     * @param b arc in B
	     *
	     * @return D matrix entry for match of a and b
	     */
	    infty_score_t D(const Arc &a, const Arc &b) const {
		return alignerNN_->Dmat(a.idx(),b.idx());
	    }


	    /**
	     * View on matrix D
	     *
	     * @param arcX arc in A/B determined by isA
	     * @param arcY arc in B/A determined by isA
	     * @param isA swap arcX/Y parameters
	     * @return D matrix entry for match of arcX<->arcY
	     */
	    infty_score_t D(const Arc &arcX,const Arc &arcY, bool isA) {
		if (isA)
		    return D(arcX,arcY);
		else
		    return D(arcY,arcX);
	    }

	    /**
	     * View on matrix D
	     *
	     * @param am arc match
	     *
	     * @return D matrix entry for arc match am
	     */
	    infty_score_t D(const ArcMatch &am) const {
		return D(am.arcA(),am.arcB());
	    }
	};


	/**
	 * @brief Provides a modified view on the scoring
	 *
	 * This view is used when
	 * computing length normalized local alignment.  
	 * @see UnmodifiedScoringView
	 */
	//TODO: required? ModifiedScoringView
	class ModifiedScoringViewNN {
	protected:
	    const AlignerNN *alignerNN_; //!< aligner object for that the view is provided

	    score_t lambda_; //!< factor for modifying scoring

	    /**
	     * Computes length of an arc
	     *
	     * @param a the arc
	     *
	     * @return length of arc a
	     */
	    size_t
	    arc_length(const Arc &a) const {
		return a.right()-a.left()+1;
	    }
	public:

	    /**
	     * Construct for AlignerN object
	     *
	     * @param alignerN The aligner object
	     *
	     * @note scoring object in aligner has to be modified by lambda already
	     */
	    explicit
            ModifiedScoringViewNN(const AlignerNN *alignerNN)
		: alignerNN_(alignerNN),lambda_(0) {}

	    /**
	     * Change modification factor lambda
	     *
	     * @param lambda modification factor
	     */
	    void
	    set_lambda(score_t lambda) {
		lambda_=lambda;
	    }

	    /**
	     * Get scoring object
	     *
	     * @return modified scoring object of aligner
	     */
	    const Scoring *scoring() const {return alignerNN_->mod_scoring;}

	    /**
	     * View on matrix D
	     *
	     * @param a arc in A
	     * @param b arc in B
	     *
	     * @return modified D matrix entry for match of a and b
	     */
	    infty_score_t D(const Arc &a,const Arc &b) const {
		return alignerNN_->Dmat(a.idx(),b.idx())
		    -lambda_*(arc_length(a)+arc_length(b));
	    }


	    /**
	     * View on matrix D
	     *
	     * @param am arc match
	     *
	     * @return modified D matrix entry for arc match am
	     */
	    infty_score_t D(const ArcMatch &am) const {
		return alignerNN_->Dmat(am.arcA().idx(),am.arcB().idx())
		    -lambda_*(arc_length(am.arcA())+arc_length(am.arcB()));
	    }
	};


	// TODO: const
	UnmodifiedScoringViewNN def_scoring_view; //!< Default scoring view
	ModifiedScoringViewNN mod_scoring_view; //!< Modified scoring view for normalized alignment

	Arc traceback_closing_arcA;
	Arc traceback_closing_arcB;

	bool is_innermost_arcA;
	bool is_innermost_arcB;
	// ============================================================


	/**
	 * \brief initialize matrices M
	 *
	 * initialize first column and row of matrices M for
	 * the alignment below of arc match (a,b).
	 * First row/column means the row al and column bl.
	 *
	 * @param al left end of arc a
	 * @param ar right end of arc a
	 * @param bl left end of arc b
	 * @param br right end of arc b
	 * @param sv Scoring view
	 * 
	 */
	template <class ScoringView>
	void init_M_E_F(ArcIdx idxA, ArcIdx idxB,ScoringView sv);


	/**
	 * \brief compute and stores score of aligning subsequences to the gap
	 *
	 * @param isA a switch to determine the target sequence A or B
	 * @param sv the scoring view to be used
	 *
	*/
	template <class ScoringView>
	void computeGapCosts(bool isA, ScoringView sv);

	/**
	 * \brief aux function, returns the left side position of and Arc
	 *        Supports psueodoarc extra indexing
	 */
	pos_type get_arc_leftend(ArcIdx &arc, bool isA);

	/**
	 * \brief return score of aligning a subsequence to the gap
	 *
	 * @param leftSide sequence position before left side of the subsequence
	 * @param rightSide sequence position after right side of the subsequence
	 * @param isA a switch to determine the target sequence A or B
	 */
	infty_score_t getGapCostBetween( pos_type leftSide, pos_type rightSide, bool isA);


	/**
	 * \brief compute IA/IB value of single element
	 *
	 * @param xl position in sequence A/B: left end of current arc match
	 * @param arcY arc in sequence B/A, for which score is computed
	 * @param i position in sequence A/B, for which score is computed
	 * @param isA switch to determine IX is IA/IB
	 * @param sv the scoring view to be used
	 * @returns score of IX(i,arcX) for the left end arc element i
	 * 
	 */
	template<class ScoringView>
	infty_score_t compute_IX(ArcIdx idxX, const Arc& arcY, pos_type i,bool isA, ScoringView sv);

	/**
	 * \brief fills all IA values using default scoring scheme
	 *
	 * @param al position in sequence A: left end of current arc match
	 * @param arcB arc in sequence B, for which score is computed
	 * @param max_ar rightmost possible for an arc starting with the left end al in sequence A: maximum right end of current arc match
	 *
	 */
	void fill_IA_entries ( ArcIdx idxA, Arc arcB, pos_type max_ar);

	/**
	 * \brief fills all IB values using default scoring scheme
	 *
	 * @param arcA arc in sequence A, for which score is computed
	 * @param bl position in sequence B: left end of current arc match
	 * @param max_br rightmost possible for an arc with the left end of bl in sequence B: maximum right end of current arc match
	 *
	 */
	void fill_IB_entries ( Arc arcA, ArcIdx idxB, pos_type max_br);

	/**
	* \brief compute E matrix value of single matrix element
	*
	* @param al
	* @param i_index position in sequence A:
	* @param j_index position in sequence B:
	* @param i_seq_pos position in sequence A, for which score is computed
	* @param i_prev_seq_pos position in sequence A, first valid seq position before i_index
	* @param sv the scoring view to be used
	* @returns score of E(i,j)
	*/
		template<class ScoringView>
	infty_score_t compute_E_entry(seq_pos_t al_seq_pos, matidx_t i_index, matidx_t j_index, seq_pos_t i_seq_pos, seq_pos_t i_prev_seq_pos, ScoringView sv);

	/**
	* \brief compute F matrix value of single matrix element
	*
	* @param bl
	* @param i_index position in sequence A:
	* @param j_index position in sequence B:
	* @param j_seq_pos position in sequence B, for which score is computed
	* @param j_prev_seq_pos position in sequence B, first valid seq position before i_index
	* @param sv the scoring view to be used
	* @returns score of E(i,j)
	*/
		template<class ScoringView>
	infty_score_t  compute_F_entry(seq_pos_t bl_seq_pos, matidx_t i_index, matidx_t j_index, seq_pos_t i_seq_pos, seq_pos_t i_prev_seq_pos, ScoringView sv);
	/**
	 * \brief compute M value of single matrix element
	 *
	 * @param al position in sequence A: left end of current arc match
	 * @param bl position in sequence B: left end of current arc match
	 * @param index_i index position in sequence A, for which score is computed
	 * @param index_j index position in sequence B, for which score is computed
	 * @param sv the scoring view to be used
	 * @returns score of M(i,j) for the arcs left ended by al, bl
	 *
	 */

	template<class ScoringView>
		infty_score_t compute_M_entry(ArcIdx idxA, ArcIdx idxB,
				seq_pos_t al_seq_pos,seq_pos_t bl_seq_pos, matidx_t index_i, matidx_t index_j,ScoringView sv);

	void
    fill_D_entry(const Arc &arcA,const Arc &arcB);

	// Fill entries of domain insertion deletion i.e. IA with empty B sub-sequence and the opposite
	void
	compute_IAB_entries_domain(seq_pos_t start, seq_pos_t end, bool isA);

//---------------------------------------------------------------------------------

	/**
	 * align the loops closed by arcs (al,ar) and (bl,br).
	 * in structure local alignment, this allows to introduce exclusions
	 *
	 * @param al left end of arc a
	 * @param ar right end of arc a
	 * @param bl left end of arc b
	 * @param br right end of arc b
	 * @param allow_exclusion whether to allow exclusions, not supported by sparse
	 * 
	 * @pre arc-match (al,ar)~(bl,br) valid due to constraints and heuristics
	 */
	void fill_M_entries(const Arc &arcA, const Arc &arcB );


	/** 
	 * \brief trace back base deletion within a match of arcs
	 * 
	 * @param al left end of arc in A
	 * @param i  right end of subsequence in A
	 * @param bl left end of arc in B
	 * @param j right end of subsequence in B
	 * @param top_level whether alignment is on top level
	 * @param sv scoring view
	 */
	template<class ScoringView>
	void trace_E(ArcIdx idxA, matidx_t i_index, ArcIdx idxB, matidx_t j_index, bool top_level, ScoringView sv);

	/**
	 * \brief trace back base insertion within a match of arcs
	 *
	 * @param al left end of arc in A
	 * @param i  right end of subsequence in A
	 * @param bl left end of arc in B
	 * @param j right end of subsequence in B
	 * @param top_level whether alignment is on top level
	 * @param sv scoring view
	 */
	template<class ScoringView>
	void trace_F(ArcIdx idxA, matidx_t i_index, ArcIdx idxB, matidx_t j_index, bool top_level, ScoringView sv);


	/** 
	 * \brief trace back within an arc match
	 * 
	 * @param al left end of arc in A
	 * @param i_index  index for right end of subsequence in A
	 * @param bl index for left end of arc in B
	 * @param j_index right end of subsequence in B
	 * @param tl whether alignment is on top level
	 * @param sv scoring view 
	 */
	template<class ScoringView>
	void trace_M(ArcIdx idxA, matidx_t i_index, ArcIdx idxB, matidx_t j_index, bool tl, ScoringView sv);

	/** 
	 * \brief standard cases in trace back (without handling of exclusions)
	 * 
	 * @param al left end of arc in A
	 * @param i  right end of subsequence in A
	 * @param bl left end of arc in B
	 * @param j right end of subsequence in B
	 * @param top_level whether alignment is on top level
	 * @param sv scoring view 
	 */
	template <class ScoringView>
	void trace_M_noex(ArcIdx idxA, pos_type i,
			ArcIdx idxB,pos_type j,
			  bool top_level,
			  ScoringView sv);


	/**
	* trace IAD IBD matrix
	* @param arcA the corresponding arc in A which defines the IAD/IBD element
	* @param arcB the corresponding arc in B which defines the IAD/IBD element
	* @param sv scoring view
	*/
	template <class ScoringView>
	void trace_IXD(const Arc &arcA, const Arc &arcB, bool isA, ScoringView sv);


	/**
	 * trace D matrix
	 * @param arcA the corresponding arc in A which defines the D element
	 * @param arcB the corresponding arc in B which defines the D element
	 * @param sv scoring view
	 */
	template <class ScoringView>
	void trace_D(const Arc &arcA, const Arc &arcB, ScoringView sv);

	// /**
	//  * trace D matrix
	//  * @param am the corresponding arcs which defines the D element
	//  * @param sv scoring view
	//  */
	// template <class ScoringView>
	// void trace_D(const ArcMatch &am, ScoringView sv);

	/**
	 * trace IA/IB matrix
	 * @param xl position in sequence A/B: left end of current arc match
	 * @param i position in sequence A/B, for which score is computed
	 * @param arcY arc in sequence B/A, for which score is computed
	 * @param isA switch to determine IA/IB
	 * @param sv the scoring view to be used
	 */
	template <class ScoringView>
	void trace_IX(ArcIdx idxX, pos_type i, const Arc &arcY, bool isA, ScoringView sv);

	/**
	   create the entries in the D matrix
	   This function is called by align() (unless D_created)
	*/
	void align_D();


	
	/** 
	 * Read/Write access to D matrix
	 * 
	 * @param am Arc match
	 * 
	 * @return entry of D matrix for am
	 */
	infty_score_t &D(const ArcMatch &am) {
	    return Dmat(am.arcA().idx(),am.arcB().idx());
	}


	/**
	 * Read/Write access to D matrix
	 *
	 * @param arcX arc in A/B determined by isA
	 * @param arcY arc in B/A determined by isA
	 * @param isA swap arcX/Y parameters
	 * @return D matrix entry for match of arcX<->arcY
	 */
	infty_score_t &D(const Arc &arcX,const Arc &arcY, bool isA) {
	    if (isA)
		return Dmat(arcX.idx(),arcY.idx());
	    else
		return Dmat(arcY.idx(),arcX.idx());
	}

	/**
	 * Read/Write access to D matrix
	 * 
	 * @param arcA arc in sequence A
	 * @param arcB arc in sequence B
	 * 
	 * @return entry of D matrix for match of arcA and arcB
	 */
	infty_score_t &D(const Arc &arcA,const Arc &arcB) {
	    return Dmat(arcA.idx(),arcB.idx());
	}


	/**
	 * Read/Write access to IAorIB matrix
	 *
	 * @param i rightside position in seqA/B
	 * @param arc arc in A/B
	 * @param isA switch to determine IA/IB
	 * @return IA/IB matrix entry for position k and arc
	 */
	infty_score_t &IX(const pos_type i, const Arc &arc, bool isA) {

		if ( isA )
			return IAmat(i, arc.idx());
		else
			return IBmat(arc.idx(), i);

	}

	/**
	 * Read/Write access to IAD orIBD matrix
	 *
	 * @param i rightside position in seqA/B
	 * @param arac arc in A/B
	 * @param isA switch to determine IA/IB
	 * @return IA/IB matrix entry for position k and arc
	 */
	infty_score_t &IXD(const Arc &arc1, const Arc &arc2, bool isA) {

		if ( isA )
			return IADmat(arc1.idx(), arc2.idx());
		else
			return IBDmat(arc2.idx(), arc1.idx());

	}

	/**
	 * Read/Write access to IA matrix
	 * @param i rightside position in seqA
	 * @param b arc in B
	 *
	 * @return IA matrix entry for position i of a and arc b
	 */
	infty_score_t &IA(const pos_type i, const Arc &b) {
	    return IAmat(i, b.idx());
	}

	/**
	 * Read/Write access to IB matrix
	 * @param a arc in A
	 * @param k rightside position in seqB
	 *
	 * @return IB matrix entry for arc a and position k in b
	 */
	infty_score_t &IB(const Arc &a, const pos_type k) {
		return IBmat(a.idx(), k);
	}

	/**
	 * do the trace back through the alignment matrix
	 * with partial recomputation
	 * pre: call align() to fill the top-level matrix
	 */
	template <class ScoringView>
	void trace(ScoringView sv);

    public:
	//! copy constructor
	AlignerNN(const AlignerNN &a);

	/** 
	 * @brief Construct from parameters
	 * @param ap parameter for aligner
         * @note ap is copied to allow reference to a temporary
         * @note for implicit type cast
         */
	AlignerNN(const AlignerParams &ap);
	/**
	 * @brief create with named parameters
	 * @return parameter object
	 */
	static 
	AlignerNParams create() {return AlignerNParams();} 

	//! destructor
	~AlignerNN();

	//! return the alignment that was computed by trace()
	Alignment const & 
	get_alignment() const {return alignment;} 

	//! compute the alignment score
	infty_score_t
	align();

	//! offer trace as public method. Calls trace(def_scoring_view).
	void
	trace();

    };

} //end namespace LocARNA

#endif // LOCARNA_ALIGNER_NN_HH
