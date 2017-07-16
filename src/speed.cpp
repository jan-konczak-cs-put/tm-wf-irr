#include "tmapi.h"
#include <vector>
#include <cstdio>
#include <functional>
#include <atomic>
#include <random>
#include <list>

#include <boost/program_options.hpp>

// C++11 has no thread interrupt at all :(
#include <boost/thread.hpp>
// but at least I don't have to implement barriers 
#include <boost/thread/barrier.hpp>
#include <boost/chrono.hpp>

using namespace std;

// benchmark parameters:
const int threadNo = 1;
int timeSecs;
int varsNo;
int transfersPerTransaction;
int readsPerTransaction;

bool irr = false;


thread_local default_random_engine generator(boost::chrono::high_resolution_clock::now().time_since_epoch().count());

struct stats {
	int successfull = 0;
	int aborted = 0;
	int selfAborted = 0;
	
	stats & operator += (const stats & other) {
		successfull += other.successfull;
		aborted += other.aborted;
		selfAborted += other.selfAborted;
		return *this;
	}
};

void setup(int argc, char ** argv);
void threadFunc(stats & threadStats, boost::barrier * b);
void printStats(stats & s);
void makeSomeTransaction(stats & threadStats);
void initVars();
void finalChecks();
void freeVars();
void run();

int main(int argc, char ** argv){
	
	setup(argc, argv);
	
	printf("\nRevocable:\n");
	
	initVars();
	
	run();
	
	freeVars();
	
	irr = true;
	
	printf("\nIrrevocable:\n");
	
	initVars();
	
	run();
	
	freeVars();
	
	return 0;
}

void run(){
	vector<boost::thread> threads;
	vector<stats> threadStats(threadNo);
	
	boost::barrier threadBarrier(threadNo+1);
	
	for(int i = 0 ; i < threadNo; ++i){
		threads.push_back(boost::thread(threadFunc, ref(threadStats[i]), &threadBarrier));
	}
	
	auto sleepTime = boost::chrono::seconds(timeSecs);
	threadBarrier.wait();
	boost::this_thread::sleep_for(sleepTime);
	for(boost::thread & t: threads)
		t.interrupt();
	for(boost::thread & t: threads)
		t.join();
	
	atomic_thread_fence(memory_order_seq_cst);
	
	
	stats finalStats;
	for(int i = 0 ; i < threadNo; ++i)
		finalStats += threadStats[i];
	
	finalChecks();
	
	printStats(finalStats);
}

void setup(int argc, char ** argv){
	boost::program_options::options_description opts;
	opts.add_options()
		("seconds,s", boost::program_options::value<int>(&timeSecs)->default_value( 1), "Benchmark length in seconds")
		("vars,v", boost::program_options::value<int>(&varsNo)->default_value(1024), "Number of variables")
		("transfers,w", boost::program_options::value<int>(&transfersPerTransaction)->default_value(10), "Transfers per transaction (1 × read + 2 × write)")
		("reads,r", boost::program_options::value<int>(&readsPerTransaction)->default_value(70), "Reads per transaction")
		("help,h", "this help")
	;
	
	boost::program_options::variables_map vm;
	boost::program_options::store(boost::program_options::parse_command_line(argc, argv, opts), vm);
	boost::program_options::notify(vm);
	
	if (vm.count("help")) {
		cout << opts << "\n";
		exit(0);
	}
	
	if(varsNo < 2 || timeSecs < 1 || transfersPerTransaction < 0 || readsPerTransaction < 0 
		|| transfersPerTransaction + readsPerTransaction < 1 || readsPerTransaction > varsNo){
		printf("Stupid arguments detected. Be gone!\n");
		exit(1);
	}
	
	printf("Seconds: %d\nVars: %d\nTransfers/transaction %d\nReads/transaction %d\n",
		       timeSecs,   varsNo,  transfersPerTransaction,  readsPerTransaction);
}

void threadFunc(stats & threadStats, boost::barrier * b){
	b->wait();
	
	while(true){
		makeSomeTransaction(threadStats);
		boost::this_thread::interruption_point();
	}
}

void printStats(stats& s){
	printf("Successfull: %d tx total, %f tx/s\n", s.successfull, s.successfull/double(timeSecs));
}

//////////////////////////////
//////////////////////////////

typedef tuple<Tm::Variable<int>*, Tm::Variable<int>*, int> transferDescr;

vector<Tm::Variable<int>*> vars;
int varsSum = 0;

enum TransResult {Success, Abort};

TransResult runTransaction(list<transferDescr>& todo, vector<Tm::Variable<int>*>& reads, stats & threadStats);


void initVars(){
	varsSum = 0;
	normal_distribution<> startDist(100, 33);
	for(int i=0; i < varsNo; ++i){
		int amount = startDist(generator);
		if(amount<0)amount=0;
		vars.push_back(new Tm::Variable<int>(amount));
		varsSum+=amount;
	}
}

void freeVars()
{
	for(auto v : vars)
		delete v;
	vars.clear();
}

list<transferDescr> generateTransfers() {
	list<transferDescr> result;
	
	thread_local static uniform_int_distribution<> varDist(0, varsNo-1);
	thread_local static uniform_int_distribution<> amountDist(1, 25);
	
	for(int i = 0 ; i < transfersPerTransaction; ++i){
		int a = varDist(generator), b;
		// roll b until a!=b
		while(a == (b = varDist(generator)));
		result.push_back(transferDescr(vars[a], vars[b], amountDist(generator)));
	}
	
	return result;
}

vector<Tm::Variable<int>*> generateReads(){
	thread_local static uniform_int_distribution<> varDist(0, varsNo-1);
	
	set<int> varNums;
	while((int)varNums.size()!=readsPerTransaction)
		      varNums.insert(varDist(generator));
	
	vector<Tm::Variable<int>*> result;
	
	for(int varnum : varNums)
		result.push_back(vars[varnum]);
	
	shuffle(result.begin(), result.end(), generator);
	
	return result;
}

void makeSomeTransaction(stats & threadStats){
	list<transferDescr> transfers = generateTransfers();
	vector<Tm::Variable<int>*> reads = generateReads();
	
	TransResult res = runTransaction(transfers, reads, threadStats);
	switch(res){
		case TransResult::Success:
			threadStats.successfull++;
			return;
		case TransResult::Abort:
			throw "‽";
	}
}


TransResult runTransaction(list<transferDescr>& todo, vector<Tm::Variable<int>*>& reads, stats & threadStats) {
	try{
		
		int readsPerTransfer = readsPerTransaction/(transfersPerTransaction>0?transfersPerTransaction:1);
		auto readIt =  reads.begin();
		[[gnu::unused]] volatile int lastRead;
		
		Tm::beginT();
		
		if(irr)
			Tm::irrT();
		
		for(transferDescr & d : todo) {
			
			for (int r=0; r < readsPerTransfer; ++r){
				lastRead = (*readIt)->ro();
				++readIt;
			}
			
			Tm::Variable<int> * from   = get<0>(d);
			Tm::Variable<int> * to     = get<1>(d);
			int                 amount = get<2>(d);
			
			if(from->ro() < amount){
				from->rw();
				to->rw();
				continue;
			}
			from->rw()-=amount;
			to->rw()+=amount;
		}
		
		while(readIt!=reads.end()){
			lastRead = (*readIt)->ro();
			++readIt;
		}
		
		Tm::commitT();
	} catch(const Tm::InvalidUseException & iue) {
		throw ":?";
	} catch(const Tm::TransactionException & te) {
		return TransResult::Abort;
	}
	return TransResult::Success;
}

void finalChecks(){
	int endSum = 0;
	try{
		Tm::beginT();
		for(int i=0; i < varsNo; ++i)
			endSum+=vars[i]->rw();
		if(endSum!=varsSum)
			printf("TM problem - endSum!=varsSum\n");
		Tm::irrT();
		Tm::commitT();
		if(endSum==varsSum)
			printf("All fine\n");
	} catch (Tm::AccessFailedException ex) {
		printf("TM problem - %s\n", typeid(ex).name());
	} catch (Tm::IrrevocTransException ex) {
		printf("TM problem - %s\n", typeid(ex).name());
	} catch (Tm::CommitFailedException ex) {
		printf("TM problem - %s\n", typeid(ex).name());
	} catch (Tm::TransactionException ex) {
		printf("TM problem - %s\n", typeid(ex).name());
	}
}
