#include <fstream>
#include <sstream>
#include <map>

#include "aux.hh"
#include "rna_data.hh"

#include "alphabet.hh"

namespace LocARNA {

RnaData::RnaData(const std::string &file, bool stacking_):
    sequence(),
    stacking(stacking_),
    arc_probs_(0),
    arc_2_probs_(0),
    seq_constraints_("")
{
  read(file);
}

// decide on file format and call either readPS or readPP
void RnaData::read(const std::string &filename) {
  
    std::ifstream in(filename.c_str());
    if (! in.good()) {
	std::cerr << "Cannot read "<<filename<<std::endl;
	exit(-1);
    }
    
    std::string s;
    // read first line and decide about file-format
    in >> s;
    in.close();
    if (s == "%!PS-Adobe-3.0") {
	// try reading as dot.ps file (as generated by RNAfold)
	readPS(filename);
    } else if (s == "<ppml>") {
	// proprietary PPML file format
	//readPPML(filename);
    } else {
	// try reading as PP-file (proprietary format, which is easy to read and contains pair probs)
	readPP(filename);
    }

    
    // DUMP for debugging
    //std::cout << arc_probs_ << std::endl;
    //std::cout << arc_2_probs_ << std::endl;

}

void RnaData::readPS(const std::string &filename) {
    std::ifstream in(filename.c_str());
    
    bool contains_stacking_probs=false; // does the file contain probabilities for stacking
    
    std::string s;
    while (in >> s && s!="/sequence") {
	if (s=="stacked") contains_stacking_probs=true;
    }
    
    if (stacking && ! contains_stacking_probs) {
	std::cerr << "WARNING: Stacking requested, but no stacking probabilities in dot plot!" << std::endl;
    }

    
    in >> s; in >> s;

    std::string seqstr="";
    while (in >> s && s!=")") {
	s = s.substr(0,s.size()-1); // chop of last character
	// cout << s <<endl;
	seqstr+=s;
    }
    
    std::string seqname = seqname_from_filename(filename);
    
    //! sequence characters should be upper case, and 
    //! Ts translated to Us
    normalize_rna_sequence(seqstr);
        
    sequence.append_row(seqname,seqstr);
            
    std::string line;
    
    while (getline(in,line)) {
	if (line.length()>4) {
	    std::string type=line.substr(line.length()-4);
	    if (type == "ubox"
		||
		type == "lbox"
		) {
		
		std::istringstream ss(line);
		unsigned int i,j;
		double p;
		ss >> i >> j >> p;
		
		p*=p;
		
		//std::cout << i << " " << j << std::endl;
		
		if (! (1<=i && i<j && j<=sequence.length())) {
		    std::cerr << "WARNING: Input dotplot "<<filename<<" contains invalid line " << line << " (indices out of range)" << std::endl;
		    //exit(-1);
		} else {
		    if (type=="ubox") {
			set_arc_prob(i,j,p);
		    }
		    else if (stacking && contains_stacking_probs && type=="lbox") { // read a stacking probability
			set_arc_2_prob(i,j,p); // we store the joint probability of (i,j) and (i+1,j-1)
		    }
		}
	    }
	}
    }
}

void RnaData::readPP(const std::string &filename) {
    std::ifstream in(filename.c_str());
    
    std::string name;
    std::string seqstr;
    
    
    // ----------------------------------------
    // read sequence/alignment
    
    std::map<std::string,std::string> seq_map;
    
    std::string line;
    
    while (getline(in,line) && line!="#" ) {
	if (line.length()>0 && line[0]!=' ') {
	    std::istringstream in(line);
	    in >> name >> seqstr;
	    
	    normalize_rna_sequence(seqstr);
	    
	    if (name != "SCORE:") { // ignore the (usually first) line that begins with SCORE:
		if (name == "#C") {
		    seq_constraints_ += seqstr;
		} else {
		    seq_map[name] += seqstr;
		}
	    }
	}
    }
    
    for (std::map<std::string,std::string>::iterator it=seq_map.begin(); it!=seq_map.end(); ++it) {
	// std::cout << "SEQ: " << it->first << " " << it->second << std::endl;
	sequence.append_row(it->first,it->second);
    }
    
    // ----------------------------------------
    // read base pairs
    
    int i,j;
    double p;

    // std::cout << "LEN: " << len<<std::endl;
    
    while( getline(in,line) ) {
      std::istringstream in(line);
      
      in>>i>>j>>p;
      
      if ( in.fail() ) continue; // skip lines that do not specify a base pair probability
      
      if (i>=j) {
	  std::cerr << "Error in PP input line \""<<line<<"\" (i>=j).\n"<<std::endl;
	  exit(-1);
      }
      
      set_arc_prob(i,j,p);
      
      if (stacking) {
	  double p2;
	  
	  if (in >> p2) {
	      set_arc_2_prob(i,j,p2); // p2 is joint prob of (i,j) and (i+1,j-1)
	  }
      }      
    }
}

/*
void RnaData::readPPML(const std::string &filename) {

    std::ifstream in(filename.c_str());
    
    std::string tag;
    while (in>>tag) {
	if (tag == "<score>") {
	    readScore(in);
	} else if  (tag == "<alignment>") {
	    readAlignment(in);
	} else if(tag == "<bpp>") {
	    readBPP(in);
	} else if(tag == "<constraints>") {
	    readConstraints(in);
	}
    }

    std::string name;
    std::string seqstr;
    
}
*/

std::string RnaData::seqname_from_filename(const std::string &s) const {
    size_type i;
    size_type j;
    
    assert(s.length()>0);
    
    for (i=s.length(); i>0 && s[i-1]!='/'; i--)
	;

    for (j=i; j<s.length() && s[j]!='.'; j++)
	;

    std::string name=s.substr(i,j-i);

    if (name.length()>3 && name.substr(name.length()-3,3) == "_dp")
	name=name.substr(0,name.length()-3);
    
    return name;
}

}
