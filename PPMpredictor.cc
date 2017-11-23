#include "predictor.h"
#include <fstream>

#define UINT16      unsigned short int

//ppm predictor variables
#define BIMODAL_SIZE    11 //2k total indices with 4 bits (1 meta-pred, 3 pred) each for bimodal = 8k total usage
#define PPM_TABLE_SIZE  9  //512 total indices with 12 bits each for PPM tables = 6k total size
			   //12 bits: (3 pred + 8 tag + 1 u)
			   //with 4 ppm tables, thats a total of 24k storage for PPM tables, 
			   //and a total of 32k for ppm + bimodal   
#define PPM_TAG_SIZE 8
#define PPM_PRED_SIZE 3    
#define BIMODAL_PRED_SIZE 3

//history lengths for ppm tables for folding
#define HIST_1 10
#define HIST_2 20
#define HIST_3 40
#define HIST_4 80

#define BIMODAL_PRED_MAX 7
#define PPM_PRED_MAX     7

#define WEAKLY_TAKEN     4
#define WEAKLY_NOT_TAKEN 3

#define LOG 1
/////////////// STORAGE BUDGET JUSTIFICATION ////////////////
// Total storage budget: 52KB + 32 bits

// Total PHT counters for Global predictor: 2^16
// Total PHT size for global predictor = 2^16 * 2 bits/counter = 2^17 bits = 16KB
// GHR size for global predictor: 32 bits

// Total PHT counters for local predictor: 2^16
// Total PHT size for local predictor = 2^16 * 2 bits/counter = 2^17 bits = 16KB
// Total BHT size for local predictor = 2^11 * 16 bits/counter = 2^15 bits = 4KB
// Total Size for local predictor = 16KB + 4KB = 20KB

// Total Tournament counters is: 2^16
// Total Tournament counter's size = 2^16 * 2 bits/counter = 2^17 bits = 16KB
/////////////////////////////////////////////////////////////

void initLog(){
	if(LOG)
		std::remove("log.txt");
}

void log(std::string output){
	if(LOG) {
		std::ofstream out;
		out.open("log.txt", std::ios::app);
		out<<output<<std::endl;
	}
}
template <typename T> 
void log(std::string output, T i){
	if(LOG){
		std::ofstream out;
		out.open("log.txt", std::ios::app);
		out<<output<<i<<std::endl;
	}
}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

PREDICTOR::PREDICTOR(void){
  
  initLog();
  log("Starting execution");
 
  ghr = 0; //set global history to 0; 
  
  UINT32 ppmTableSize = (1<<PPM_TABLE_SIZE);

  //initialize bimodal table to all zeros
  
  log("init bimodal table values");
  //initiaize all ppm tables to all zeros
  for(int i = 0; i<4; i++){
	ppmTables[i] = new ppmVal_t[ppmTableSize]; //create mew ppm table i
	for(UINT32 j = 0; j<ppmTableSize; j++){
		ppmTables[i][j].pred = WEAKLY_NOT_TAKEN;  //set prediction ot weakly not taken
		ppmTables[i][j].tag = 0;                  //init tag to 0
		ppmTables[i][j].u = 0;                    //init usefulness to 0
	}
  } 
  log("init ppm table values");
  
  UINT32 bimodal = (1<<BIMODAL_SIZE);
  
  bimodalTable = new bimodVal_t[bimodal]; //create bimodal table
  for(UINT32 i = 0; i<bimodal; i++){
	bimodalTable[i].pred = WEAKLY_NOT_TAKEN; //set prediction to weakly not taken
	bimodalTable[i].m = 0;                   //set metaprediction to 0
  }  
  
  //history lengths for each table for folding
  ppmHistory[0] = HIST_1;
  ppmHistory[1] = HIST_2;
  ppmHistory[2] = HIST_3;
  ppmHistory[3] = HIST_4;
  log("Init history");
  
  //initialize all shift registers for folding
  csrTag[0] = new csr_t[4]; //create new circular shift registers, 2 for tag folding, one for index folding
  csrTag[1] = new csr_t[4];
  csrIndex = new csr_t[4];
  for(UINT32 i = 0; i<4; i++){
  	initFold(&csrTag[0][i], ppmHistory[i], PPM_TAG_SIZE); 
	initFold(&csrTag[1][i], ppmHistory[i], PPM_TAG_SIZE-1);
        initFold(&csrIndex[i], ppmHistory[i], PPM_TABLE_SIZE);
  }
  log("init fold");
  log("Init Complete");
  
}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

bool   PREDICTOR::GetPrediction(UINT32 PC){
    //log("Get Prediction");
    pred.table = -1;
    UINT32 BimodalIndex = (PC) % (1<<BIMODAL_SIZE);
  
    UINT32 ppmIndex[4] = {0};
    UINT32 ppmTag[4] = {0};
    
    //calculate the ppm indices and tags from folded GHR
    for(UINT32 i = 0; i<4; i++){
	ppmIndex[i] = getIndex(PC, i, PPM_TABLE_SIZE);
	ppmTag[i] = getTag(PC, i, PPM_TAG_SIZE);
    }
    //log("checking tables");
    //see if any of the calculated tags equal the tag at index
    for(int i = 3; i>=0; --i){
	if(ppmTables[i][ppmIndex[i]].tag == ppmTag[i]) {
		//log(" ");
		//log("table chosen: ",i);
		//log("ppmTag: ", ppmTag[i]);
		//log("stored tag: ", ppmTables[i][ppmIndex[i]].tag);
		//log("ppmIndex: ", ppmIndex[i]);

		if(ppmTables[i][ppmIndex[i]].pred > PPM_PRED_MAX/2) //pred is true if ppm table pred is >= 4
			pred.pred=TAKEN;
		else
			pred.pred=NOT_TAKEN;
		pred.table = i;
		pred.index = ppmIndex[i];
		break;
	}
    }
    //log("done checking");
    
    //if no preiction was found, use bimodal
   
    if(pred.table == -1) { //if still using bimodal table (all tag tables missed)
	if(bimodalTable[BimodalIndex].pred > BIMODAL_PRED_MAX/2) //pred is true if bimod table pred >= 4
		pred.pred=TAKEN;
	else
		pred.pred=NOT_TAKEN;
    } 
    return pred.pred;
    //log("Done predict");

}
/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

void  PREDICTOR::UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget){
 	//log("get update");
	UINT32 bimodalIndex = (PC) % (1<<BIMODAL_SIZE); //calculate bimodal index before ghr updates	

  	//update the prediction counter for the last prediction
	//log("update pred pred");
  	UINT32 predictionVal = -1;
	if(pred.table != -1) { //not bimodal table
		//log("Update ppm table ", pred.table);
		//log("update ppm index ", pred.index);
		//log("Current val: ", ppmTables[pred.table][pred.index].pred);
		predictionVal = ppmTables[pred.table][pred.index].pred;
  		if(predictionVal < PPM_PRED_MAX && resolveDir == TAKEN) {
		//	log("ppm Inc");
			++(ppmTables[pred.table][pred.index].pred);
		}
		else if(predictionVal > 0 && resolveDir == NOT_TAKEN) {
		//	log("ppm Dec");
			--(ppmTables[pred.table][pred.index].pred);
		}
		//log("ppm val now: ", ppmTables[pred.table][pred.index].pred);
  	} else {
		predictionVal = bimodalTable[bimodalIndex].pred;
 		if(predictionVal < BIMODAL_PRED_MAX && resolveDir == TAKEN) {
			++(bimodalTable[bimodalIndex].pred);
		}
		else if(predictionVal > 0 && resolveDir == NOT_TAKEN) {
			--(bimodalTable[bimodalIndex].pred);
		}
  	}		

	//log("allocating new entry");
	//log("test: ", ppmTables[3][135].tag);

	//if(resolveDir != pred.pred)
	//	log("Incorrect prediction");
	//allocate new entry (steal an entry)
	//log("allocate entry?");
	if(pred.table < 3 && predDir != resolveDir) { //if the prediction is wrong, and there was a tag miss
		//allocate entries in tables above pred.table (in tables where misses occured)
		//log("reallocating tables");
		//log("max table hit was ", pred.table);
		
		UINT32 n = pred.table+1; 
		
		bool stealRand = true; //see if we steal a random val or all reset vals
		for(UINT32 i = n; i<4; i++) {
		//	log("Checking u for table: ", i);
		//	log("U is: ", ppmTables[i][pred.index].u);

			if(ppmTables[i][pred.index].u == 0){ //if the useful bit is reset at index in any table n>x
				stealRand = false;
			} 
		}

		if(stealRand) { //if all u-bits are set, we steal a random entry in table n>x
			int tableToSteal = rand() % (4-n); //pick a random number between 0 and 4-n
			tableToSteal += n; //add n to it to get the table between n and 4 to steal
			log("random stealing element with tag: ", ppmTables[tableToSteal][pred.index].tag);
			steal(PC, tableToSteal, pred.index, bimodalIndex, resolveDir);
			log("Stolen tag is now: ", ppmTables[tableToSteal][pred.index].tag);
			
		} else { //else we steal all entries where u is 0
			//log("steal all");
			for(UINT32 i = n; i<4; i++){
				if(ppmTables[i][pred.index].u == 0){
					log("Stealing all element with tag: ", ppmTables[i][pred.index].tag);
					steal(PC, i, pred.index, bimodalIndex, resolveDir);
					//log("stolen: ", i);
					log("Stolen all tag is now:, ", ppmTables[i][pred.index].tag);
				}
			}
		}
	}

	//update bits u and m
	//log("update u and m");
	if(predDir != bimodalTable[bimodalIndex].pred && pred.table > -1) { //if pred was different than bimodal prediction
		if(bimodalTable[bimodalIndex].pred != resolveDir){ //bimodal was wrong
			ppmTables[pred.table][pred.index].u = 1;
			bimodalTable[bimodalIndex].m = 1;
		} else {
			ppmTables[pred.table][pred.index].u = 0;
			bimodalTable[bimodalIndex].m = 0;
		}
	}

	//fold the bits for each shift register we have: index, and the two tag registers
	//log("fold");
	ghr = (ghr << 1);

  	if(resolveDir == TAKEN){
    		ghr.set(0,1);
  	}
	for(int i = 0; i<4; i++){
		//log("broken csr: ", csrTag[1][2].newLen);
		fold(&csrTag[0][i]); 
		fold(&csrTag[1][i]);
        	fold(&csrIndex[i]);
	}	
	//log("Done update");

}
void PREDICTOR::steal(UINT32 pc, UINT32 table, UINT32 index, UINT32 bimodalIndex, bool predDir) {	
	UINT32 newTag = getTag(pc, table, PPM_TAG_SIZE);
	
	if(bimodalTable[bimodalIndex].m == 1) { //reinitialize by prediction direction
		if(predDir == TAKEN){
			ppmTables[table][index].pred = WEAKLY_TAKEN;
		} else { //not taken
			ppmTables[table][index].pred = WEAKLY_NOT_TAKEN;
		}
	} else { //if metapredictor is 0, initialize by bimodal prediction
		bool bimodDir = (bimodalTable[bimodalIndex].pred > BIMODAL_PRED_MAX/2);
		if(bimodDir) { //if bimodal predition is taken
			ppmTables[table][index].pred = WEAKLY_TAKEN;
		} else { //else bimodal prediction is not taken
			ppmTables[table][index].pred = WEAKLY_NOT_TAKEN;
		}
	}
	//log("Old tag: ", ppmTables[table][index].tag);
	//log("New tag: ", newTag);
	ppmTables[table][index].u = 0; //reset U
	ppmTables[table][index].tag = newTag; //reset tag to calculated tag

}
/////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////

//hash function for the new tag for the ppm table
UINT32 PREDICTOR::getTag(UINT32 PC, int table, UINT32 tagSize) {
	UINT32 tag = (PC xor csrTag[0][table].val xor (csrTag[1][table].val << 1));
	return (tag & ((1 << tagSize) -1));
}

//hash function for the index to the ppm table
UINT32 PREDICTOR::getIndex(UINT32 PC, int table, UINT32 tagSize) {
	UINT32 index = PC xor(PC >> (tagSize - table)) xor (csrIndex[table].val); //xor csrIndex[table].val; 
	return (index & ((1 << tagSize) - 1));
}

void PREDICTOR::initFold(csr *shift, UINT32 origLen, UINT32 newLen){
	shift->val = 0;
	shift->origLen = origLen;
	shift->newLen = newLen;
}

/*UINT32 PREDICTOR::getFoldedHistory(UINT32 foldLength, UINT32 histLength){
	unsigned long long ghrSto = ghr;
	UINT32 fold;
	for(int i = 0; i<histLength; i++){
		UINT32 mask = (1<<foldLength)-1;
		UINT32 temp = ghrsto & mask;
		fold = fold xor temp;
		ghrSto = ghrSto >> foldLength;
		
	}
	return fold;
}*/

void PREDICTOR::fold(csr *shift){
	shift->val = (shift->val << 1) | (ghr[0]); //add first it of ghr to shift register
	shift->val ^= ghr[shift->origLen] << (shift->origLen % shift->newLen);
	shift->val ^= (shift->val >> shift->newLen);
	shift->val &= (1 << shift->newLen) - 1;
}

void    PREDICTOR::TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget){

  // This function is called for instructions which are not
  // conditional branches, just in case someone decides to design
  // a predictor that uses information from such instructions.
  // We expect most contestants to leave this function untouched.

  return;
}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
