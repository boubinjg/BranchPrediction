#include "predictor.h"
#include <cstdlib>
#include <time.h>
#include <bitset>
#include <fstream>

#define BIMODAL_SIZE      16  //2^16 rows of 2bit counters
//#define TAGE_TABLE_SIZE   12  //2^12 rows of 16 bits
#define TAGE_TAG_SIZE     11  //11 tag bits
#define TAGE_PRED_SIZE    3   //3 prediction bits for TAGE
#define BIMODAL_PRED_SIZE 2   //2 prediction bits for bimodal

#define HIST_1  640           //history for tables high to low
#define HIST_2  403
#define HIST_3  240
#define HIST_4  160
#define HIST_5  101
#define HIST_6  64
#define HIST_7  40 
#define HIST_8  25 
#define HIST_9  16
#define HIST_10 10
#define HIST_11 6
#define HIST_12 4


#define BIMODAL_PRED_MAX  3   //maximum bimodal prediction (2 bits)
#define TAGE_PRED_MAX     7   //maximum TAGE prediction (3 bits)
#define PRED_U_MAX        3   //number of useful bits

#define BIMODAL_PRED_INIT 2   //init bimodal prediction to 2 (weakly taken)
#define TAGE_PRED_INIT    0   //init tage pred to 0 (strongly not taken)

#define WEAKLY_TAKEN      4   
#define WEAKLY_NOT_TAKEN  3

#define NUM_TAGE_TABLES   12  //number of tables

#define LOOP_TABLE_SIZE   10  //2^7 entries
#define LOOP_TAG_SIZE     14  //14 bit tag
#define LOOP_CONF_MAX     3   //2 bit confidence 
#define LOOP_IT_MAX       14  //2^14 max iteration count
#define LOOP_AGE_MAX      8   //the highest possible age = 2^age_max

#define ALTPRED_BET_MAX   15  //cap on alt-pred better
#define ALTPRED_BET_INIT  8   //init for the alt-pred better count

#define PHR_LEN           16  //len of path history

#define CLOCK_MAX         18  //2^CLOCK_MAX = number of cycles before reset

#define LOG 0 //1 if you want logs, 0 if you don't.

/////////////// STORAGE BUDGET JUSTIFICATION //////////////////////////////////////////////////
// Binomial table: 2^16 2-bit counters = 2^17 bits
// Tage tables: 4
// Tage table size: 2^12
// Tage tag size: 11 bits + 3 bit prediction counter and 2 bit useful counter
// Tables * TableSize * tagSize = 4 * 16 * 2^12 = 2^17
// Loop table: 2^7 entries
// Loop entry: 14 tag bits + 14 iteration count bits + 3 confidence bits + 5 age bits = 36 bits
// Total Size = Tage tables + Binom table + loop table = 2^18 bits + 576 bit loop  = 32KB + 576
///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////

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

PREDICTOR::PREDICTOR(void)
{
 	//init logs for debugging. Only works if LOG isn't 0
	initLog();
	log("Starting execution");

	//find size of each TAGE table
   	log("attempting to make new var");
	
	tageTableSize = new UINT32[NUM_TAGE_TABLES];
	tageTableSize[0] = 9;
	tageTableSize[1] = 9;
	tageTableSize[2] = 10;
	tageTableSize[3] = 10;
	tageTableSize[4] = 10;
	tageTableSize[5] = 10;
	tageTableSize[6] = 11;
	tageTableSize[7] = 11;
	tageTableSize[8] = 11;
	tageTableSize[9] = 11;
	tageTableSize[10] = 10;
	tageTableSize[11] = 10;
	
	tageTagSize = new UINT32[NUM_TAGE_TABLES];
	tageTagSize[0] = 15;
	tageTagSize[1] = 14;
	tageTagSize[2] = 13;
	tageTagSize[3] = 12;
	tageTagSize[4] = 12;
	tageTagSize[5] = 11;
	tageTagSize[6] = 10;
	tageTagSize[7] = 9;
	tageTagSize[8] = 8;
	tageTagSize[9] = 8;
	tageTagSize[10] = 7;
	tageTagSize[11] = 7;


	log("to tag init");
	tagTables = new tagVal_t*[NUM_TAGE_TABLES];
	//initialize TAGE tag tables
    	
	
	for(UINT32 i = 0; i < NUM_TAGE_TABLES; i++) {
		log("initialized ", i);
		UINT32 tableSize = (1<<tageTableSize[i]);
       		tagTables[i] = new tagVal_t[tableSize];
        	for(UINT32 j =0; j < tableSize; j++) {
            		tagTables[i][j].pred = 0; //3 bits
            		tagTables[i][j].tag = 0;  //11 bits 
            		tagTables[i][j].u = 0;    //2 bit
        	} 
		//log("tageTableSize: ", tageTableSize);
    	}
   	log("done tag");
	//find number of bimodal table entries
  	numBimodalEntries = (1 << BIMODAL_SIZE);
	//create bimodal table
    	bimodal = new bimodVal_t[numBimodalEntries];

	//initialize bimodal predictions for each table
     	for(UINT32 i=0; i< numBimodalEntries; i++) {
       		bimodal[i].pred = BIMODAL_PRED_INIT;
     	}
	
	loopTableSize = (1<<LOOP_TABLE_SIZE);
	loopTable = new loopVal_t[loopTableSize];
	for(UINT32 i = 0; i<loopTableSize; i++){
		loopTable[i].loopCount = 0;
		loopTable[i].currentIter = 0;
		loopTable[i].tag = 0;
		loopTable[i].conf = 0;
		loopTable[i].age = 0;
		loopTable[i].pred = false;
		loopTable[i].used = false;
	}
	log("to hist init");
    	//initialize geometric history lengths for TAGE tables
	tageHistory = new UINT32[NUM_TAGE_TABLES]; 
    	tageHistory[0] = HIST_1;
    	tageHistory[1] = HIST_2;
    	tageHistory[2] = HIST_3;
    	tageHistory[3] = HIST_4;
	tageHistory[4] = HIST_5;
    	tageHistory[5] = HIST_6;
    	tageHistory[6] = HIST_7;
    	tageHistory[7] = HIST_8;
	tageHistory[8] = HIST_9;
    	tageHistory[9] = HIST_10;
    	tageHistory[10] = HIST_11;
    	tageHistory[11] = HIST_12;
	
    	log("done hist init");
	//create circular shift registers
    	csrIndex = new csr_t[NUM_TAGE_TABLES];
	csrTag = new csr_t*[2];
    	csrTag[0] = new csr_t[NUM_TAGE_TABLES];
    	csrTag[1] = new csr_t[NUM_TAGE_TABLES];
	//initialize circular shift registers
	for(UINT32 i = 0; i<NUM_TAGE_TABLES; i++){
		initFold(&csrIndex[i], tageHistory[i], tageTagSize[i]);
		initFold(&csrTag[0][i], tageHistory[i], tageTagSize[i]);
	        initFold(&csrTag[1][i], tageHistory[i], tageTagSize[i]-1); 
	}	
	 
	// initialize global prediction
        pred.pred = -1;
        pred.altPred = -1;
        pred.table = NUM_TAGE_TABLES;
       	pred.altTable = NUM_TAGE_TABLES;
       
	//initialize indices
	tageIndex = new UINT32[NUM_TAGE_TABLES];
       	for(UINT32 i=0; i < NUM_TAGE_TABLES; i++) {    
            	tageIndex[i] = 0;
       	}
	//initialize tags
	tageTag = new UINT32[NUM_TAGE_TABLES];
       	for(UINT32 i=0; i < NUM_TAGE_TABLES; i++) {    
            	tageTag[i] = 0;
       	}
	//init clock
       	clock = 0;
       	clockState = 0;
	//init path history
       	PHR = 0;
	//init global history
       	GHR.reset();
	//init alt meta-veriable
       	altBetterCount = ALTPRED_BET_INIT;
	//reset random seed
	srand(time(NULL));
	log("exit init");
	log("tt test: ", tagTables[0][0].tag);

}      

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

bool   PREDICTOR::GetPrediction(UINT32 PC){
	log("in pred");
  	//get bimodal index
	UINT32 bimodalIndex = (PC) % (numBimodalEntries);
	//get loop predictor index
	UINT32 loopIndex = (PC) % (loopTableSize);  	
	
	log("Check loop");
	//check loop counter
	UINT32 loopTag = (PC) % (1<<LOOP_TAG_SIZE);
	if(loopTable[loopIndex].tag == loopTag &&
	   loopTable[loopIndex].currentIter < loopTable[loopIndex].loopCount){ //if the loop is executing
		loopTable[loopIndex].pred = TAKEN;
	} else if(loopTable[loopIndex].tag == loopTag &&
		  loopTable[loopIndex].currentIter == loopTable[loopIndex].loopCount) { //if loop is over
		loopTable[loopIndex].pred = NOT_TAKEN;
	} 
	if(loopTable[loopIndex].tag == loopTag &&
	   loopTable[loopIndex].conf == LOOP_CONF_MAX) { //if loop predictor is confident
		loopTable[loopIndex].used = true;       //use and return
		return loopTable[loopIndex].pred;
	}
	//if prediction hasn't been made, used = false
	loopTable[loopIndex].used = false;

	//else use TAGE
	log("get tag");
	//initialize tags
    	for(int i = 0; i < NUM_TAGE_TABLES; i++) {	
		tageTag[i] = getTag(PC, i, tageTagSize[i]);
    	}
    	//initialize index
	log("get index");
	UINT32 offset[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} ;
       	for(int i = 0; i < NUM_TAGE_TABLES; i++) {
            	tageIndex[i] = getIndex(PC, i, tageTableSize[i], offset[i]);
       	}
       	log("initialize pred");
        //initialize prediction
       	pred.pred = -1;
       	pred.altPred = -1;
       	pred.table = NUM_TAGE_TABLES;
       	pred.altTable = NUM_TAGE_TABLES;
      
	log("check tags");
       	for(UINT32 i = 0; i < NUM_TAGE_TABLES; i++) { //check for tag hits  
		log("accessing index: ", tageIndex[i]);
		log("tag: ", tageTag[i]);
		log("value: ", tagTables[0][0].tag);
	        if(tagTables[i][tageIndex[i]].tag == tageTag[i]) { //tag hit
                	pred.table = i;
			pred.index = tageIndex[i];
               	 	break;
            	}  
       	}      
	log("check tags for altpred");
        for(UINT32 i = pred.table + 1; i < NUM_TAGE_TABLES; i++) { //check for tag hits on lower tables
                if(tagTables[i][tageIndex[i]].tag == tageTag[i]) { //tag hit
                    	pred.altTable = i;
			pred.altIndex = tageIndex[i];
                    	break;
                }  
        }    
        log("make pred");
   	if(pred.table < NUM_TAGE_TABLES) { //if we haven't missed a table        
       		if(pred.altTable == NUM_TAGE_TABLES) { //if altPred missed a table
           		pred.altPred = (bimodal[bimodalIndex].pred > BIMODAL_PRED_MAX/2); //use bimodal
       		} else{ //if altpred hit a table
           		if(tagTables[pred.altTable][pred.altIndex].pred >= TAGE_PRED_MAX/2) //use bimodal prediction
                		pred.altPred = TAKEN;
            		else 
                		pred.altPred = NOT_TAKEN;
       		}
        	if((tagTables[pred.table][pred.index].pred  != WEAKLY_NOT_TAKEN) || //if pred is not weak,
		   (tagTables[pred.table][pred.index].pred != WEAKLY_TAKEN) ||     
		   (tagTables[pred.table][pred.index].u != 0) ||                    //useful,
		   (altBetterCount < ALTPRED_BET_INIT)) {                           //altpred historically not useful
            		pred.pred = tagTables[pred.table][pred.index].pred >= TAGE_PRED_MAX/2;
            		return pred.pred; //return best prediction
        	} else {
            		return pred.altPred; //return alt-pred
        	}
    	} else { //if both missed
        	pred.altPred =  (bimodal[bimodalIndex].pred > BIMODAL_PRED_MAX/2); //use bimodal table prediction
        	return pred.altPred; //return alt-pred
    	}
	log("out pred");
}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
 
void  PREDICTOR::UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget){
	log("in update");
   	bool newInTable;    
	UINT32 bimodalIndex = (PC) % (numBimodalEntries); //get bimodal index

	UINT32 loopIndex = (PC) % (loopTableSize);
	UINT32 loopTag = (PC) & (1<<LOOP_TAG_SIZE);
	//update loop perdictor
	if(loopTable[loopIndex].tag != loopTag && loopTable[loopIndex].age > 0){ //if tag miss
		--(loopTable[loopIndex].age); //decrease age
	} else { //if tag hit:
		if(loopTable[loopIndex].age == 0){ //if entry is old or blank
			//initialize a new entry
			loopTable[loopIndex].tag = (PC) % (1<<LOOP_TAG_SIZE);
			loopTable[loopIndex].age = (1<<LOOP_AGE_MAX) + 1;
			loopTable[loopIndex].currentIter = 1;
			loopTable[loopIndex].loopCount = (1<<LOOP_IT_MAX); 
			loopTable[loopIndex].conf = 0;
			loopTable[loopIndex].pred = 0;
		} else {
			if(loopTable[loopIndex].pred == resolveDir) { //prediction was correct
				if(loopTable[loopIndex].currentIter != loopTable[loopIndex].loopCount){
					++(loopTable[loopIndex].currentIter);
				} else if(loopTable[loopIndex].currentIter == loopTable[loopIndex].loopCount){
					loopTable[loopIndex].currentIter = 0;
					if(loopTable[loopIndex].conf < LOOP_CONF_MAX)
						++(loopTable[loopIndex].conf);
				}
			} else { //prediction was incorrect
				if(loopTable[loopIndex].age == (1<<LOOP_AGE_MAX)) { 
					loopTable[loopIndex].loopCount = loopTable[loopIndex].currentIter;
					loopTable[loopIndex].currentIter = 0;
					loopTable[loopIndex].conf = 1;
				} else {
					loopTable[loopIndex].loopCount = 0;
					loopTable[loopIndex].currentIter = 0;
					loopTable[loopIndex].tag = 0;
					loopTable[loopIndex].conf = 0;
					loopTable[loopIndex].age = 0;
					loopTable[loopIndex].pred = false;
				}
			}
		}
		if(loopTable[loopIndex].used){
			return;
		}
	}
	
	//update prediction counters in tag/bimodal tables
	UINT32 predictionVal = -1;
    	if(pred.table < NUM_TAGE_TABLES) { // update prediction counters
		predictionVal = tagTables[pred.table][pred.index].pred; 
        	if(resolveDir && predictionVal < TAGE_PRED_MAX) {   //if TAKEN and pred<max
			++(tagTables[pred.table][pred.index].pred); //increment
        	} else if(!resolveDir && predictionVal > 0) {       //if NOT TAKEN and pred>0
			--(tagTables[pred.table][pred.index].pred); //decrement
        	}
    	} else { //do the same for bimodal
		predictionVal = bimodal[bimodalIndex].pred;
                if(resolveDir && predictionVal < BIMODAL_PRED_MAX) {
            		++(bimodal[bimodalIndex].pred);
        	} else if(!resolveDir && predictionVal > 0) {
            		--(bimodal[bimodalIndex].pred);
        	}
    	}
	
    	//check age of current tag entry, given we hit an entry
	if(pred.table < NUM_TAGE_TABLES) { //if we hit an entry
	    	if((tagTables[pred.table][pred.index].u == 0) &&                    //if entry is not useful
		  ((tagTables[pred.table][pred.index].pred  == WEAKLY_NOT_TAKEN) || //and weakly predicted
	          (tagTables[pred.table][pred.index].pred  == WEAKLY_TAKEN))) {                
                	newInTable = true;                                          //it's considered new
			if (pred.pred != pred.altPred) {                            //if preds were different
		    		if (pred.altPred == resolveDir) {                   //if altpred was right
					if (altBetterCount < ALTPRED_BET_MAX) {     //bound by this value 
                            			altBetterCount++;                   //increment
                        		}    
		      		} else if (altBetterCount > 0) {                    //if altpred was wrong
                        		altBetterCount--;                           //decrement
                    		}
                	}
	    	}
	}

	//steal entry
    	//if((!newInTable) || (newInTable && (pred.pred != resolveDir))) { //if table's not new, or pred is wrong
		if (((predDir != resolveDir) & (pred.table > 0))) { //if pred is wrong and there was a tag miss     
	    		bool alloc = false;
			for (int i = 0; i < pred.table; i++) {
				if (tagTables[i][tageIndex[i]].u == 0) //if one isn't useful
                			alloc = true;
	      		}
	    		if (!alloc) { //decrease usefulness, don't evict
				for (int i = pred.table - 1; i >= 0; i--) {
		    			tagTables[i][tageIndex[i]].u--;
                		}
            		} else { //else
				for(int i = pred.table-1; i>=0; i--){
					if((tagTables[i][tageIndex[i]].u == 0 && !(rand()%10))) {
						if(resolveDir) { //if TAKEN
                                                        tagTables[i][tageIndex[i]].pred = WEAKLY_TAKEN; 
                                                } else  { //if NOT TAKEN
                                                        tagTables[i][tageIndex[i]].pred = WEAKLY_NOT_TAKEN;
                                                }    
                                                tagTables[i][tageIndex[i]].tag = tageTag[i]; //reset tag
                                                tagTables[i][tageIndex[i]].u = 0;            //set to useless
                                                break; 

					}
				}
	    		}
		}
    	//}    
	
	// update usefuness bit (no meta-pred)
	if(pred.table < NUM_TAGE_TABLES) {
        	if ((predDir != pred.altPred)) { //if altpred wasn't used
	    		if (predDir == resolveDir && tagTables[pred.table][pred.index].u < PRED_U_MAX )  //if prediction was correct
				++(tagTables[pred.table][pred.index].u); //set useful
			else if(predDir != resolveDir && tagTables[pred.table][pred.index].u > 0)
				--(tagTables[pred.table][pred.index].u); //set not useful
		}  
	}
	
	//increment clock to eventually reset useful bits
	clock++;
        //for every 2^CLOCK_MAX instructions
	if(clock == (1<<CLOCK_MAX)) { 	   //currently 256k as in paper
            	clock = 0;                 //reset clock
            	if(clockState == 1) {      //change clock state
                	clockState = 0;
            	} else {
                	clockState = 1;
            	}
	    	for(UINT32 i = 0; i < NUM_TAGE_TABLES; i++){ //for all tags
			for(UINT32 j = 0; j < (1<<tageTableSize[i]); j++){
				tagTables[i][j].u &= (clockState+1); //if clockstate = 0, reset lower bit
								     //else reset upper bit
			}
		}

	}
 	//update the GHR
  	GHR = (GHR << 1);
  	if(resolveDir == TAKEN){
    		GHR.set(0,1); 
  	}

	//perform folding
    	for (int i = 0; i < NUM_TAGE_TABLES; i++) {
            fold(&csrIndex[i]);
            fold(&csrTag[0][i]);
            fold(&csrTag[1][i]);
    	}
  	
	//update path history
    	PHR = (PHR << 1);
    	if(PC & 1) {
       		PHR = PHR + 1;
    	}
    	PHR = (PHR & ((1 << PHR_LEN) - 1));
	log("out pred");
}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

//hash function for the new tag for the ppm table
UINT32 PREDICTOR::getTag(UINT32 PC, int table, UINT32 tagSize) {
        UINT32 tag = (PC ^ csrTag[0][table].val ^ (csrTag[1][table].val << 1));
        return (tag & ((1 << tagSize) -1));
}

//hash function for the index to the ppm table
UINT32 PREDICTOR::getIndex(UINT32 PC, int table, UINT32 tagSize, UINT32 phrOffset) {
	UINT32 index = PC ^ (PC >> tagSize) ^ csrIndex[table].val ^ PHR ^ (PHR & ((1<<phrOffset)-1));
	return (index & ((1 << tagSize)-1));
}

void PREDICTOR::initFold(csr *shift, UINT32 origLen, UINT32 newLen){
        shift->val = 0;
        shift->origLen = origLen;
        shift->newLen = newLen;
}

void PREDICTOR::fold(csr_t *shift){
        shift->val = (shift->val << 1) + GHR[0];
        shift->val ^= ((shift->val & (1 << shift->newLen)) >> shift->newLen);
        shift->val ^= (GHR[shift->origLen] << (shift->origLen % shift->newLen));
        shift->val &= ((1 << shift->newLen) -1);
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
