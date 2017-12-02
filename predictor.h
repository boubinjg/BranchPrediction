#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include "utils.h"
#include "tracer.h"
#include <bitset>

#define NUM_TAGE_TABLES 12

#define UINT16	     unsigned short int

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
//Circular Shift Register for folding purposes
typedef struct csr {
    	UINT16 val;
    	UINT16 origLen;
    	UINT16 newLen;
} csr_t;

typedef struct bimodVal{
	UINT32 pred; //prediction (2 bits)
	//bool m; //metapredictor (1 bit) (eliminated)
} bimodVal_t;

typedef struct tagVal {
    	UINT16 pred;
    	UINT16 tag;
    	UINT16 u;
} tagVal_t;

typedef struct prediction{
	bool pred;
	bool altPred;
	int table;
	int altTable;
	UINT32 index;
	UINT32 altIndex;
} prediction_t;

typedef struct loopVal{
	UINT32 loopCount;     //loop count?
	UINT32 currentIter;   //current iteration of the loop
	UINT32 tag;           //n bit tag 
	UINT32 conf;          //2 bit confidence counter
	UINT32 age;
	bool pred;
	bool used;
} loopVal_t;

class PREDICTOR{

  // The state is defined for Gshare, change for your design

private:
  	bitset<1001> *GHR;           // global history register
  	UINT32 PHR; 		   //path history
	
	//tables
	bimodVal_t *bimodal;       //bimodal table
	UINT32  numBimodalEntries; //number of entries in bimodal table
	tagVal_t **tagTables;                 //TAGE table
	//UINT32 tageTableSize;	              //number of entries in TAGE table
	loopVal_t *loopTable;                 //loop table
	UINT32 loopTableSize;                 //number of loop table entries

	UINT32 *tageTableSize;
	UINT32 *tageTagSize;

	UINT32 *tageHistory;                  //number ofGHR bits examined by CSR to index a given table
	csr_t *csrIndex;                      //circular shift register for indices 
	csr_t **csrTag;                     //2 circular shift registers for tags
	 
	prediction_t pred;                    //global prediction
	
 	UINT32 *tageIndex;                    //index calculated for a given table 
	UINT32 *tageTag;                      //tag calculated for a given table
	UINT32 clock;                         //global clock
  	bool clockState;                      //clocl flip it
  	INT32 altBetterCount;                 //number of times altpred is better than prd
public:

  	// The interface to the four functions below CAN NOT be changed

  	PREDICTOR(void);
  	bool    GetPrediction(UINT32 PC);  

  	void    UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget);
  	void    TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget);
  	
	//void    steal(UINT32 PC, UINT32 table, UINT32 index, UINT32 bimodalIndex, bool predDir);

	UINT32  getTag(UINT32 PC, int table, UINT32 tagSize);
	UINT32  getIndex(UINT32 PC, int table, UINT32 tagSize, UINT32 phrOffset);
	void    initFold(csr_t *shift, UINT32 origLen, UINT32 newLen);
	void    fold(csr_t *shift);

  	// Contestants can define their own functions below

};


/***********************************************************/
#endif

