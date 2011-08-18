#ifndef EXACT_MATCHER_HH
#define EXACT_MATCHER_HH
#include "scoring.hh"
#include <iostream>
#include <sstream>
#include <list>
#include <algorithm>
#include "matrices.hh"

extern "C"
{
	#include <ViennaRNA/fold_vars.h>
	#include <ViennaRNA/utils.h>
	#include <ViennaRNA/PS_dot.h>
	#include <ViennaRNA/fold.h>
	int    PS_rna_plot(char *string, char *structure, char *file);
	int    PS_rna_plot_a(char *string, char *structure, char *file, char *pre, char *post);
	float  fold(const char *sequence, char *structure);
}

using namespace std;

namespace LocARNA {

typedef size_t size_type;
typedef vector<unsigned int> intVec;
typedef pair<unsigned int,unsigned int> intPair;
typedef pair<intPair, intPair> intPPair;
typedef vector<intPPair>::const_iterator IntPPairCITER;

class StringHash
{
   public:
      size_t operator()(const string &myStr) const
      {
         unsigned long hash = 5381;

         for (unsigned int i = 0; i < myStr.length(); i++)
         {
            hash = ((hash << 5) + hash) + myStr[i]; // hash * 33 + cStateString[i]
         }
         return hash;
   }
};

class StringEq {
public:
   bool operator()(const string &a,const string &b) const
   {
      return (a == b);
   }
};

class SinglePattern
{
public:
      SinglePattern(){};
      SinglePattern(const string& myId,const string& seqId,const intVec& mySinglePattern);

	virtual ~SinglePattern();

   const string&        getmyId()  const;
   const string&	getseqId() const;
   const intVec&        getPat() const;

private:

      string         myId;
      string	     seqId;
      intVec         pattern;
};

//--------------------------------------------------------------------------
// class PatternPair
//    is able to manage an EPM, consists of 2 singlepatterns, one in each RNA
//--------------------------------------------------------------------------
class PatternPair
   {
      public:
      PatternPair(const string& myId,const int& mySize,const SinglePattern& myFirstPat,const SinglePattern& mySecPat, const string& structure_, int& score_)
                  : id(myId),size(mySize),first(myFirstPat),second(mySecPat), structure(structure_), EPMscore(score_)
      {  score = EPMscore;  };

      virtual ~PatternPair()
      {
    	insideBounds.clear();
      };

      const string& 		getId() const;
      const int& 			getSize() const;
      const SinglePattern& 	getFirstPat() const;
      const	SinglePattern& 	getSecPat() const;
			void			resetBounds();
			void			setOutsideBounds(intPPair myPPair);
			intPPair 		getOutsideBounds();
			void			addInsideBounds(intPPair myPPair);
      const vector<intPPair>& getInsideBounds();
			void			setEPMScore(int myScore);
			int 			getScore();
			int 			getEPMScore();
      string& get_struct();

      private:
         string         	id;
         int            	size;
         SinglePattern  	first;
         SinglePattern  	second;
	  
	 string 		structure;
         int				score;
         int				EPMscore;
         vector<intPPair>   insideBounds;
         intPPair           outsideBounds;
   };
   
//--------------------------------------------------------------------------
// class PatternPairMap
//    manage a set of EPMs (PatternPair)
//--------------------------------------------------------------------------
class PatternPairMap
{
   public:
	  typedef  PatternPair                                  selfValueTYPE;
	  typedef  PatternPair*				               		SelfValuePTR;

      typedef  multimap<int,SelfValuePTR,greater<int> >     orderedMapTYPE;
      typedef  orderedMapTYPE::const_iterator               orderedMapCITER;
      typedef  orderedMapTYPE::iterator                     orderedMapITER;
      typedef  list<SelfValuePTR>                           patListTYPE;
      typedef  patListTYPE::iterator                        patListITER;
      typedef  patListTYPE::const_iterator                  patListCITER;
      typedef  __gnu_cxx::hash_map<string,SelfValuePTR,StringHash,StringEq> PatternIdMapTYPE;


         PatternPairMap();
         PatternPairMap(const PatternPairMap& myPairMap)
                           :patternList(myPairMap.patternList),
                            patternOrderedMap(myPairMap.patternOrderedMap),
                            idMap(myPairMap.idMap)  {};

      virtual ~PatternPairMap();

               void              add( const string& id,
                                      const int& mysize,
                                      const SinglePattern& first,
                                      const SinglePattern& second,
				      const string& structure,
				      int score
				    );
               void              add(const SelfValuePTR value);
               void              makeOrderedMap();
               void              updateFromMap();
      const    PatternPair&      getPatternPair(const string& id)const;
      const    SelfValuePTR      getPatternPairPTR(const string& id)const;
      const    patListTYPE&      getList() const;
      const    orderedMapTYPE&   getOrderedMap() const;
               orderedMapTYPE&   getOrderedMap2();
      const    int               size()   const;
			   int  			 getMapBases();

   private:

     patListTYPE        patternList;
     orderedMapTYPE     patternOrderedMap;
     PatternIdMapTYPE   idMap;
};

class LCSEPM
{
    public:

					LCSEPM(const int& size1_, const int& size2_,
					  const 		PatternPairMap& myPatterns,
										PatternPairMap& myLCSEPM,
										int&		    mySize,
										int&			myScore,
						const int& EPM_min_size_
					      )

    	                                   :size1(size1_),
					    size2(size2_),
					    matchedEPMs(myLCSEPM),
    	                                    patterns(myPatterns),
    	                                    LCSEPMsize(mySize),
    	                                    LCSEPMscore(myScore),
    	                                    EPM_min_size(EPM_min_size_){};
		
					void MapToPS(const string& sequenceA, const string& sequenceB, int& mySize, PatternPairMap& myMap, const string& file1, const string& file2);
        virtual 		~LCSEPM();

        		void    calculateLCSEPM();

	private:

		struct	HoleKeyS;
        struct	HoleKeyS
        {
        	PatternPairMap::SelfValuePTR    pattern;
            intPPair                        bounds;
        };

        typedef     HoleKeyS                            HoleKey;
        typedef     HoleKey*                            HoleKeyPTR;
        typedef     multimap<int,HoleKeyPTR>            HoleOrderingMapTYPE;
        typedef     HoleOrderingMapTYPE::const_iterator HoleMapCITER;

        void    preProcessing				();
        void    calculateHoles				();
	    void    calculatePatternBoundaries	(PatternPair* myPair);
        void 	calculateTraceback			(const int i,const int j,const int k,const int l,vector < vector<int> > holeVec);
        int 	D_rec						(const int& i,const  int& j,const int& k,const int& l,vector < vector<int> >& D_h,const bool debug);
        int 	max3						(int a, int b, int c);
	
	//!@brief returns the structure of the given sequence
	char* getStructure(PatternPairMap& myMap, bool firstSeq, int length);
	
	string intvec2str(const std::vector<unsigned int>& V, const std::string delim){
	      stringstream oss;
	      copy(V.begin(), V.end(), ostream_iterator<unsigned int>(oss, delim.c_str()));
	      string tmpstr;
	      tmpstr = oss.str();
	      if (tmpstr.length()>0) tmpstr.erase(tmpstr.end()-1);
	      return tmpstr;
	  }
	string upperCase(string seq){
	  string s= "";
	  for(unsigned int i= 0; i<seq.length(); i++)
	    s+= toupper(seq[i]);
	  return s;
	}
				vector< vector<PatternPairMap::SelfValuePTR> >	EPM_Table;
				const int& size1;
				const int& size2;
				PatternPairMap&									matchedEPMs;
				HoleOrderingMapTYPE    					 		holeOrdering;
        const 	PatternPairMap&         						patterns;
				int& 											LCSEPMsize;
				int& 											LCSEPMscore;
	const int& EPM_min_size;
				
};


class Mapping{
	typedef std::vector<int> pos_vec;
	typedef std::vector<pos_vec> bp_mapping;
	

public:
	  //! constructor
	Mapping(const BasePairs &bps_,RnaData &rnadata_,const double &prob_unpaired_threshold_):
	  prob_unpaired_threshold(prob_unpaired_threshold_),
	  bps(bps_),
	  rnadata(rnadata_)
	  {
		compute_mapping();
	}


private:
	
	const double &prob_unpaired_threshold;
	const BasePairs &bps;
	RnaData &rnadata;
	bp_mapping pos_vecs; //! mapping from the new positions to the sequence positions (i.e. which positions relative to the beginning of the arc are valid)
	bp_mapping new_pos_vecs; //!mapping from the sequence positions to the new positions;
		                 //!sequence positions are relative to the beginning of the arc
                                 //!vec contains -1 if sequence position isn't valid
		
	void compute_mapping();

public:
  
	//!is sequential matching from position new_pos-1 to position new_pos possible?
	bool seq_matching(size_type arcIdx,size_type new_pos)const {
		return pos_vecs.at(arcIdx).at(new_pos-1)+1==pos_vecs.at(arcIdx).at(new_pos);
	}
	
	//!is position k valid (i.e. does probability that the base k is unpaired under the loop exceed some threshold) for the basepair with index arcIdx? 
	bool is_valid_pos(Arc arc,size_type k) const{
		return rnadata.prob_unpaired_in_loop(k,arc.left(),arc.right())>=prob_unpaired_threshold;
	}

	//!returns the sequence position corresponding to the position new_pos in the matrix
	int get_pos_in_seq(const Arc &arc, size_type new_pos) const{
		return pos_vecs.at(arc.idx()).at(new_pos)+arc.left();
	}

	//!returns the new position in the matrix corresponding to the position pos in the sequence
	//!returns -1 if position pos isn't valid
	int get_pos_in_new_seq(const Arc &arc, size_type pos) const{
		return new_pos_vecs.at(arc.idx()).at(pos-arc.left());
	}

	//!returns the number of valid positions for a basepair with index arcIdx
	int number_of_valid_pos(size_type arcIdx) const{
		return pos_vecs.at(arcIdx).size();
	}
	
	//for debugging
	void print_vec() const;
	//!class distructor
	virtual ~Mapping(){
		new_pos_vecs.clear();
		pos_vecs.clear();
	}
	   
};


//!a class for the representation of exact pattern matches (EPM)
class EPM{
	 const Sequence &seqA_;
	 const Sequence &seqB_;

	struct EPM_entry{
		int pos1;
		int pos2;
		char str;
	};

	intVec pat1Vec;
	intVec pat2Vec;
	int list_it;
	string structure;
	
	//! the EPM consists of the positions and the corresponding structure
	list<EPM_entry> epm;

	typedef list<EPM_entry>::iterator iter;
	iter cur_it; //! current position

        //!contains the indices of the arcMatches which need to be considered and a corresponding iterator which
	//!gives the position where the traceback of the arcMatch should be inserted
	std::vector<pair<ArcMatch::idx_type,iter> > arcmatches_to_do;
	PatternPairMap mcsPatterns;


public:
	
        //!Constructor
	EPM(const Sequence &seqA, const Sequence &seqB)
	: seqA_(seqA),seqB_(seqB), mcsPatterns() {
		reset();
	}

	virtual ~EPM(){
		pat1Vec.clear();
		pat2Vec.clear();
		epm.clear();
		arcmatches_to_do.clear();
	}
	//!reset epm and reset current position (cur_it) to the beginning of epm
	void reset(){
		pat1Vec.clear();
		pat2Vec.clear();
		epm.clear();
		structure.clear();
		cur_it=epm.begin();
		list_it= 0;
	}

	//!reset current position to the end position of epm
	void reset_cur_it(){
		cur_it=epm.end();
		cur_it--;
		list_it= pat1Vec.size();
		list_it--;
		if(list_it<0) list_it= 0;
	}

	//!appends a position and the corresponding structure to the epm and increments the iterator
	void append(int pos1_, int pos2_, char c){
		struct EPM_entry entry = {pos1_,pos2_,c};
		epm.push_back(entry);
		pat1Vec.push_back(pos1_);
		pat2Vec.push_back(pos2_);
		cur_it++;
		list_it++;
	}

	//!appends an arcMatch to the epm
	void append_arcmatch(const ArcMatch &am){
		append(am.arcA().left(),am.arcB().left(),'(');
		append(am.arcA().right(),am.arcB().right(),')');
	}

	//!adds a positions and the corresponding structure to the epm at cur_it and decrements the iterator
	void add(int pos1_, int pos2_,char c){
		struct EPM_entry entry = {pos1_,pos2_,c};
		epm.insert(cur_it,entry);
		pat1Vec.insert(pat1Vec.begin()+ list_it,pos1_);
		pat2Vec.insert(pat2Vec.begin()+ list_it,pos2_);
		cur_it--;
		list_it--;
		if(list_it<0) list_it= 0;
	}


	//!adds an arcmatch at the current position (cur_it) and stores the corresponding arcMatch
	void add_arcmatch(const ArcMatch &am){
		add(am.arcA().right(),am.arcB().right(),')');
		store_arcmatch(am.idx());
		add(am.arcA().left(),am.arcB().left(),'(');
	}

	//!stores the index of the arcMatch and the current position (cur_it) in the vector arc_matches_to_do
	void store_arcmatch(ArcMatch::idx_type idx){
		arcmatches_to_do.push_back(pair<ArcMatch::idx_type,iter>(idx,cur_it));
	}

	//!checks if there are arcMatches left which need to be processed
	bool arcmatch_to_process(){
		return arcmatches_to_do.begin()!=arcmatches_to_do.end();
	}

	//!returns the index of the last arcMatch in the vector arcmatches_to_do
	ArcMatch::idx_type next_arcmatch(){
		pair<ArcMatch::idx_type,iter> lastEl = arcmatches_to_do.back();
		cur_it = lastEl.second;
		ArcMatch::idx_type result=lastEl.first;
		arcmatches_to_do.pop_back();
		return result;
	}


	void print_epm(ostream &out, int score){
		string 	seq="";
		if(epm.begin()==epm.end()) return;
		for(iter it=epm.begin();it!=epm.end();it++){
			if(seqA_[it->pos1]!=seqB_[it->pos2]){cerr << "ERROR " << endl;}
			seq+= seqA_[it->pos1][0];
			cout << seqA_[it->pos1][0];
		}
		out << " ";
		for(iter it=epm.begin();it!=epm.end();it++){
			out << it->str;
		}
		out << endl;
		stringstream pat1;
		pat1 << "";
		for(iter it=epm.begin();it!=epm.end();it++){
			pat1 << it->pos1;
			out << it->pos1;
			iter tmp = epm.end(); tmp--;
			if(it!=tmp){
			  out << ":";
			  pat1 << ":";
			}
		}
		out << " ";
		stringstream pat2;
		pat2 << "";
		for(iter it=epm.begin();it!=epm.end();it++){
			pat2 << it->pos2;
			out << it->pos2;
			iter tmp = epm.end(); tmp--;
			if(it!=tmp){
			  out << ":";
			  pat2 << ":";
			}	
		}
		out << endl;
		cout << "epm size " << pat1Vec.size() << " and score " << score << endl;
	}

	//for debugging
	void print_arcmatches_to_do(ostream &out){
		out << "to do vector " << endl;
		for(std::vector<pair<ArcMatch::idx_type,iter> >::iterator it=arcmatches_to_do.begin();it!=arcmatches_to_do.end();it++){
			out << it->first << endl;
		}
		out << endl;
	}
	void sort_patVec(){
		sort(pat1Vec.begin(), pat1Vec.end());
		sort(pat2Vec.begin(), pat2Vec.end());
	}
	intVec getPat1Vec() const{
		return pat1Vec;
	}
	intVec getPat2Vec() const{
		return pat2Vec;
	}
	
	void add_pattern(string patId ,SinglePattern pattern1 ,SinglePattern pattern2, int score){
	  set_struct();
	  mcsPatterns.add(patId, pat1Vec.size(), pattern1, pattern2, structure, score);
	}
	
	PatternPairMap& get_patternPairMap() {
	  return mcsPatterns;
	}
	bool isEmpty(){
	  return epm.empty();
	}
	void set_struct(){
	  structure= "";
	  for(iter it=epm.begin();it!=epm.end();it++){
			structure+= it->str;
		}
	}
};

class ExactMatcher {
  
  
private:

    typedef size_t size_type;
    const Sequence &seqA;
    const Sequence &seqB;
    const ArcMatches &arc_matches;
    const BasePairs &bpsA;
    const BasePairs &bpsB;
    const Mapping &mappingA;
    const Mapping &mappingB;
    

    EPM epm;

    ScoreMatrix A;
    ScoreMatrix G;
    ScoreMatrix B;
    ScoreMatrix F;

    ScoreVector arc_match_score; //!vector for the arcMatch scores: score under the arcMatch with potential stacking probabilities

    struct Trace_entry{
    	infty_score_t score;
    	pair<int,int> *next_pos;
    	ArcMatch::idx_type *arc_match_idx;
    };


    Matrix<Trace_entry> Trace; //!for traceback

    int EPM_threshold;
    int EPM_min_size;
    double test;
    int alpha_1;
    int alpha_2;
    int alpha_3;
    const string& sequenceA;
    const string& sequenceB;
    enum{in_B,in_G,in_A};
    const string& file1;
    const string& file2;
    
    PatternPairMap myLCSEPM; //!PatternPairMap result of chaining algorithm
    
    // ----------------------------------------
    // evaluate the recursions / fill matrices
    
    
    //!computes matrices A,G and B
    void compute_matrices(const ArcMatch &arc_match);

    //! computes matrix F
    void compute_F();
    
    //!helper function for compute_matrices
    infty_score_t seq_str_matching(ScoreMatrix &mat,const ArcMatch &arc_match, size_type i, size_type j,bool matrixB);
    
    //! computes score for arcMatch: score under the arcMatch plus the probability of the two basepairs of the arcMatch
    infty_score_t score_for_arc_match(const ArcMatch &am);
    
    //! computes the stacking score: if stacking occurs with respect to a structure, the stacking probability is taken as a score
    infty_score_t score_for_stacking(const ArcMatch &am, const ArcMatch &inner_am);

    //!compute the backward score and the forward pointer
    void trace_F();
    
    //!traverses matrix F
    void trace_in_F(std::ostream &out);
    
    //!adds the structure and the position corresponding to the position pos_ in the matrix if
    //!the position isn't contained in another epm (score==pos_infty)
    bool add(const ArcMatch &am,pair<int,int> pos_, char c);
    
    //!adds the arcMatch to the epm if the positions aren't contained in another epm (score==pos_infty)
    bool add_arcmatch(const ArcMatch &am, bool undo);
    
    //!outputs the exact matching starting at position $(i,j)$ while setting the processed elements Trace(i,j) to -inf
    void get_matching(size_type i, size_type j, std::ostream &out);
    
    //!recomputes matrices A,G and B for arcMatch recursively and stores the traceback
    bool trace_AGB(const ArcMatch &am, bool undo);
    
    //!checks the structural case for the traceback in the matrices A,G and B
    //!if an inner_am is encountered, it is stored for later processing and the left and right endpoint is added to the epm structure 
    bool str_traceAGB(const ScoreMatrix &mat, const ArcMatch &am, size_type posA, size_type posB,pair<int,int> &curPos, bool undo);
    
    void reset_trace(size_type i, size_type j);
    
    //!converts string to uppercase
    string upperCase(string seq){
	  string s= "";
	  for(unsigned int i= 0; i<seq.length(); i++)
	    s+= toupper(seq[i]);
	  return s;
	}
    
    //Debugging
    void output_trace_matrix();
    void output_arc_match_score();
    void print_EPM_start_pos(list<pair<pair<int,int>,infty_score_t> > &EPM_start_pos);
    
public:

    //! construct with sequences and possible arc matches
    ExactMatcher(const Sequence &seqA_,const Sequence &seqB_,const ArcMatches &arc_matches_,const Mapping &mappingA_, const Mapping &mappingB_,
		 const int &threshold_,const int &min_size_,const int &alpha_1,const int &alpha_2, const int &alpha_3, const string& sequenceA_,
		 const string& sequenceB_, const string& file1_, const string& file2_);
    ~ExactMatcher();
    
    //! computes the score of best exact matching
    //! side effect: fills the A,G,B and F matrices
    void
    compute_matchings();
    
    //! write all exact matchings to out.
    //! pre: call to compute_matchings()
    void
    write_matchings(std::ostream &out);
    
    //! outputs anchor constraints to be used as input for locarna
    void
    output_locarna();
};



} //end namespace

#endif //  EXACT_MATCHER_HH
