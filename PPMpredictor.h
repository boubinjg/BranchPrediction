#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include "utils.h"
#include "tracer.h"
#include <bitset>

#define UINT16      unsigned short int

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
//Circular Shift Register for folding purposes
typedef struct csr {
	UINT32 val;
	UINT32 origLen;
	UINT32 newLen;
} csr_t;

typedef struct bimodVal {
	UINT32 pred; //prediction (3 bits) 
	bool m; //meta-prediction (1 bit) 
} bimodVal_t;

typedef struct ppmVal {
	UINT32 pred; //predictoin (3 bits)
	UINT32 tag; //tag (8 bits)
	bool u; //useful bit
} ppmVal_t;

typedef struct prediction{
	bool pred;    //prediction value
	int table; //table the prediction came from
	UINT32 index; //index in the table that the prediction came from
} prediction_t;

class PREDICTOR{

  // The state is defined for Gshare, change for your design

 private:


  //unsigned long long  ghr;           // global history register
  bitset<80> ghr;
  UINT32  *pht;          // pattern history table
  UINT32  historyLength; // history length
  UINT32  numPhtEntries; // entries in pht
  
  //UINT32* ppmIndex;
  //UINT32* ppmTag;

  //add for local predictor
  //local pattern history table
  //UINT32 pht_local_bit_size;
  UINT32 *pht_local;
  UINT32 numPhtLocalEntries;

  //branch history table for local branch predictor
  UINT32 bht_history_length;
  UINT32 numBhtEntries;
  UINT32 bht_bit_size;
  UINT16 *bht;

  //for tournament counter
  UINT32 *predictorChooseCounter;
  UINT32 numTournamentCounter;

  bimodVal_t *bimodalTable;
  ppmVal_t *ppmTables[4];
  //bimodVal_t bimodalTable[2048];
  //ppmVal_t ppmTables[4][512];
  //bimodVal_t bimodalTable[2048];


  UINT32 ppmHistory[4];
  csr_t *csrIndex;
  csr_t *csrTag[2];

  //csr_t csrIndex[4];
  //csr_t csrTag[2][4];


  prediction_t pred;

 public:

  // The interface to the four functions below CAN NOT be changed

  PREDICTOR(void);
  bool    GetPrediction(UINT32 PC);

  //add for tournament predictor
  //bool    GetLocalPrediction(UINT32 PC);
  //bool    GetGlobalPrediction(UINT32 PC);

  void    UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget);
  void    TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget);
  
  void    steal(UINT32 pc, UINT32 table, UINT32 index, UINT32 bimodalIndex, bool predDir);
  
  UINT32  getTag(UINT32 PC, int table, UINT32 tagSize);
  UINT32  getIndex(UINT32 PC, int table, UINT32 tagSize);
  void    initFold(csr_t *shift, UINT32 origLen, UINT32 newLen);
  void    fold(csr_t *shift);

  // Contestants can define their own functions below

};


/***********************************************************/
#endif

