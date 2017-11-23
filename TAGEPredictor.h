#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include "utils.h"
#include "tracer.h"
#include <bitset>

#define NUM_TAGE_TABLES 4

#define UINT16	     unsigned short int

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
//Circular Shift Register for folding purposes
typedef struct csr {
    	UINT32 val;
    	UINT32 origLen;
    	UINT32 newLen;
} csr_t;

typedef struct bimodVal{
	UINT32 pred; //prediction (2 bits)
	//bool m; //metapredictor (1 bit) (eliminated)
} bimodVal_t;

typedef struct tagVal {
    	UINT32 pred;
    	UINT32 tag;
    	UINT32 u;
} tagVal_t;

typedef struct prediction{
	bool pred;
	bool altPred;
	int table;
	int altTable;
	UINT32 index;
	UINT32 altIndex;
} prediction_t;

class PREDICTOR{

  // The state is defined for Gshare, change for your design

private:
  	bitset<131> GHR;           // global history register
  	UINT16 PHR; 		   //path history
	// Bimodal
	bimodVal_t *bimodal;       //bimodal table
	UINT32  numBimodalEntries; //number of entries in bimodal table
	tagVal_t *tagTables[NUM_TAGE_TABLES]; //TAGE table
	UINT32 tageTableSize;	              //number of entries in TAGE table

	UINT32 tageHistory[NUM_TAGE_TABLES];  //number ofGHR bits examined by CSR to index a given table
	csr_t *csrIndex;                      //circular shift register for indices 
	csr_t *csrTag[2];                     //2 circular shift registers for tags
	 
	prediction_t pred;                    //global prediction
	
 	UINT32 tageIndex[NUM_TAGE_TABLES];    //index calculated for a given table 
	UINT32 tageTag[NUM_TAGE_TABLES];      //tag calculated for a given table
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

