#include "predictor.h"
#include <cstdlib>
#include <time.h>
#include <bitset>
#include <fstream>

#define BIMODAL_SIZE      16  //2^17 rows of 2bit counters
#define TAGE_TABLE_SIZE   12  //2^12 rows of 16 bits
#define TAGE_TAG_SIZE     11  //12 tag bits
#define TAGE_PRED_SIZE    3   //3 prediction bits for TAGE
#define BIMODAL_PRED_SIZE 2   //2 prediction bits for bimodal

#define HIST_1 130            //history for tables high to low
#define HIST_2 44
#define HIST_3 15
#define HIST_4 5

#define BIMODAL_PRED_MAX  3   //maximum bimodal prediction (2 bits)
#define TAGE_PRED_MAX     7   //maximum TAGE prediction (3 bits)
#define PRED_U_MAX        2   //number of useful bits

#define BIMODAL_PRED_INIT 2   //init bimodal prediction to 2 (weakly taken)
#define TAGE_PRED_INIT    0   //init tage pred to 0 (strongly not taken)

#define WEAKLY_TAKEN      4   
#define WEAKLY_NOT_TAKEN  3

#define NUM_TAGE_TABLES   4   //number of tables

#define ALTPRED_BET_MAX   15  //cap on alt-pred better
#define ALTPRED_BET_INIT  8   //init for the alt-pred better count

#define PHR_LEN           16  //len of path history

#define CLOCK_MAX         18  //2^CLOCK_MAX = number of cycles before reset

#define LOG 0 //1 if you want logs, 0 if you don't.

/////////////// STORAGE BUDGET JUSTIFICATION ////////////////////////////////
// Binomial table: 2^16 2-bit counters = 2^17 bits
// Tage tables: 4
// Tage table size: 2^12
// Tage tag size: 11 bits + 3 bit prediction counter and 2 bit useful counter
// Tables * TableSize * tagSize = 4 * 16 * 2^12 = 2^17
// Total Size = Tage tables + Binom table = 2^18 bits = 32KB
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

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
   	tageTableSize = (1 << TAGE_TABLE_SIZE);
    
	//initialize TAGE tag tables
    	for(UINT32 i = 0; i < NUM_TAGE_TABLES; i++) {
       		tagTables[i] = new tagVal_t[tageTableSize];
        	for(UINT32 j =0; j < tageTableSize; j++) {
            		tagTables[i][j].pred = 0; //3 bits
            		tagTables[i][j].tag = 0;  //11 bits 
            		tagTables[i][j].u = 0;    //2 bit
        	} 
    	}
   
	//find number of bimodal table entries
  	numBimodalEntries = (1 << BIMODAL_SIZE);
	//create bimodal table
    	bimodal = new bimodVal_t[numBimodalEntries];

	//initialize bimodal predictions for each table
     	for(UINT32 i=0; i< numBimodalEntries; i++) {
       		bimodal[i].pred = BIMODAL_PRED_INIT;
     	}

    	//initialize geometric history lengths for TAGE tables
    	tageHistory[0] = HIST_1;
    	tageHistory[1] = HIST_2;
    	tageHistory[2] = HIST_3;
    	tageHistory[3] = HIST_4;
    
	//create circular shift registers
    	csrIndex = new csr_t[NUM_TAGE_TABLES];
    	csrTag[0] = new csr_t[NUM_TAGE_TABLES];
    	csrTag[1] = new csr_t[NUM_TAGE_TABLES];
	//initialize circular shift registers
	for(UINT32 i = 0; i<NUM_TAGE_TABLES; i++){
		initFold(&csrIndex[i], tageHistory[i], TAGE_TABLE_SIZE);
		initFold(&csrTag[0][i], tageHistory[i], TAGE_TABLE_SIZE);
	        initFold(&csrTag[1][i], tageHistory[i], TAGE_TABLE_SIZE-1); 
	}	
	 
	// initialize global prediction
        pred.pred = -1;
        pred.altPred = -1;
        pred.table = NUM_TAGE_TABLES;
       	pred.altTable = NUM_TAGE_TABLES;
       
	//initialize indices
       	for(UINT32 i=0; i < NUM_TAGE_TABLES; i++) {    
            	tageIndex[i] = 0;
       	}
	//initialize tags
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
}      

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

bool   PREDICTOR::GetPrediction(UINT32 PC){

  	//get bimodal index
	UINT32 bimodalIndex   = (PC) % (numBimodalEntries);
  	
	//initialize tags
    	for(int i = 0; i < NUM_TAGE_TABLES; i++) {	 
		tageTag[i] = getTag(PC, i, TAGE_TABLE_SIZE);
    	}
    	//initialize index
	UINT32 offset[4] = {0, 0, 3, 5} ;
       	for(int i = 0; i < NUM_TAGE_TABLES; i++) {
            	tageIndex[i] = getIndex(PC, i, TAGE_TABLE_SIZE, offset[i]);
       	}
       
        //initialize prediction
       	pred.pred = -1;
       	pred.altPred = -1;
       	pred.table = NUM_TAGE_TABLES;
       	pred.altTable = NUM_TAGE_TABLES;
      
       	for(UINT32 i = 0; i < NUM_TAGE_TABLES; i++) { //check for tag hits  
            	if(tagTables[i][tageIndex[i]].tag == tageTag[i]) { //tag hit
                	pred.table = i;
			pred.index = tageIndex[i];
               	 	break;
            	}  
       	}      
        for(UINT32 i = pred.table + 1; i < NUM_TAGE_TABLES; i++) { //check for tag hits on lower tables
                if(tagTables[i][tageIndex[i]].tag == tageTag[i]) { //tag hit
                    	pred.altTable = i;
			pred.altIndex = tageIndex[i];
                    	break;
                }  
        }    
            
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
}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
 
void  PREDICTOR::UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget){
 
   	bool newInTable;    
	UINT32 bimodalIndex = (PC) % (numBimodalEntries); //get bimodal index
	
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
    	if((!newInTable) || (newInTable && (pred.pred != resolveDir))) { //if table's not new, or pred is wrong
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
                		int count = 0;
                		int uselessTables[NUM_TAGE_TABLES - 1] = {-1};
                	        for (int i = 0; i < pred.table; i++) { //find all useless tables
                    			if (tagTables[i][tageIndex[i]].u == 0) {
                        			count++;
                        			uselessTables[i] = i;
                    			}
                		}
				int maxTableToSteal = 0;
                		if(count == 1) { //if only one table useless table
                    			maxTableToSteal = uselessTables[0];
                		} else if(count > 1) { //else chose random number of tables to steal
                     			if(rand() % 2) {
                        			maxTableToSteal = uselessTables[(count-1)];
                    			} else {
                        			maxTableToSteal = uselessTables[(count-2)];
                    			}   
                		}
				//steal useless tag entry
				for (int i = maxTableToSteal; i >= 0; i--) {
		    			if ((tagTables[i][tageIndex[i]].u == 0)) {
                        			if(resolveDir) { //if TAKEN
                            				tagTables[i][tageIndex[i]].pred = WEAKLY_TAKEN; 
                        			} else	{ //if NOT TAKEN
                            				tagTables[i][tageIndex[i]].pred = WEAKLY_NOT_TAKEN;
                        			}    
                            			tagTables[i][tageIndex[i]].tag = tageTag[i]; //reset tag
                            			tagTables[i][tageIndex[i]].u = 0;            //set to useless
						break; 
		     			}
                		}
	    		}
		}
    	}    
	
	// update usefuness bit (no meta-pred)
	if(pred.table < NUM_TAGE_TABLES) {
        	if ((predDir != pred.altPred)) { //if altpred wasn't used
	    		if (predDir == resolveDir)  //if prediction was correct
				tagTables[pred.table][pred.index].u = 1; //set useful
			else 
				tagTables[pred.table][pred.index].u = 0; //set not useful
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
			for(UINT32 j = 0; j < tageTableSize; j++){
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
