#include "tmapi.h"
#include "transaction.h"
#include "variable.h"

#include <list>

namespace Tm {

// initializing statics
atomic_flag Transaction::irrTransactionLock{ATOMIC_FLAG_INIT};

Transaction::Transaction()
{
	// empty on purpose
}

// called from abort and commit
void Transaction::cleanup()
{
	// for comitted transactions locksHeld is already empty (unlocking order matters)
	for(auto m : locksHeld){
		m->clear(memory_order_relaxed);
	}
	locksHeld.clear();
	for(auto v : rsetBuffers)
		v.first->deleteFromRset(v.second);
	for(auto v : hijackedWsetBuffers)
		v.first->deleteFromHijacked(v.second);
	
	currentTransaction.reset();
}

Transaction::~Transaction()
{
	// must stay here for hijaccking purposes
	for(auto var : wsetBuffers){
		var.first->deleteFromWset(var.second);
	}
}


void Transaction::irr() {
	if(amIIrrevocable)
		// meh.
		return;
	
	if(irrTransactionLock.test_and_set(memory_order_relaxed)){
		// some other transaction has the lock - it's irrevocable or trying to become irrevocable (and won the lock).
		abort();
		ABORT_LOG_SOURCE(1);
		throw IrrevocTransException();
	}
	
	// My reads must become visible as reads of irrevocable transaction
	if(!acquireReadset()){
		irrTransactionLock.clear(memory_order_relaxed);
		abort();
		ABORT_LOG_SOURCE(3);
		throw IrrevocTransException();
	}
	
	// I need to make sure that nobody forces (or forced) my abort
	if(cleanReadsetLock.test_and_set(memory_order_relaxed) || commitLock.test_and_set(memory_order_relaxed)){
		for(auto & v : rsetBuffers)
			v.first->usedByIrr.store(false, memory_order_release);	
		irrTransactionLock.clear(memory_order_release);
		abort();
		ABORT_LOG_SOURCE(4);
		throw IrrevocTransException();
	}
	
	amIIrrevocable=true;
}

bool Transaction::acquireReadset() {
	list<atomic_flag*> acquired;
	list<Tm::VariableBase*> setAsUsedByIrr;
	
	for(auto var : rsetBuffers) {
		atomic_flag * locked = var.first->acquireRead();
		setAsUsedByIrr.push_back(var.first);
		if(!locked){
			for(auto v : setAsUsedByIrr)
				v->usedByIrr.store(false);
			for(auto m : acquired)
				m->clear(memory_order_relaxed);
			return false;
		}
		acquired.push_back(locked);
	}
	
	for(auto m : acquired) {
		locksHeld.insert(m);
	}
	
	return true;
}



void Transaction::abort()
{
	if(comitted.load(memory_order_relaxed))
		throw InvalidUseException();
	
	if(amIIrrevocable){
		forcingAbortOnIrr();
		for(auto & v : rsetBuffers)
			v.first->usedByIrr.store(false, memory_order_release);
		for(auto & var : wsetBuffers)
			var.first->usedByIrr.store(false, memory_order_release);
	}
	
	aborted.store(true, memory_order_relaxed);
	
	if(amIIrrevocable)
		irrTransactionLock.clear();
	
	// unlock happens in cleanup.
	
	cleanup();
}


void Transaction::killReaders(){

	// first, let's notice all changes
	atomic_thread_fence(memory_order_acquire);
	
	for(auto & var : wsetBuffers)
		var.first->killReaders();
}


void Transaction::commit()
{
	assert( ! comitted.load(memory_order_relaxed) );
	
	if(aborted.load(memory_order_relaxed)) {
		// we've been killed by a transaction that overwrote our read.
		assert(!amIIrrevocable);
		abort();
		ABORT_LOG_SOURCE(5);
		throw CommitFailedException();
	}
	
	for(auto & var : wsetBuffers){
		if(amIIrrevocable) // because of hijackedWset.find(var)!=0
			var.first->dirtyIrr.store(true, memory_order_relaxed);
		else
			var.first->dirty.store(true, memory_order_relaxed);
		// from now on, each new reader will notice that the variable is dirty.
		// this means that new readers are not going to spoil anything
	}
	
	atomic_thread_fence(memory_order_seq_cst);
	
	// sorry dudes, you didn't make it in time...
	killReaders();
	
	if(!amIIrrevocable){
		// as revocable, I need to take the lock now
		if(cleanReadsetLock.test_and_set(memory_order_release)){
			for(auto & var : wsetBuffers){
				var.first->dirty.store(false, memory_order_relaxed);
			}
			abort();
			ABORT_LOG_SOURCE(6);
			throw CommitFailedException();
		}
		if(commitLock.test_and_set(memory_order_release)){
			for(auto & var : wsetBuffers){
				var.first->dirty.store(false, memory_order_relaxed);
			}
			abort();
			ABORT_LOG_SOURCE(12);
			throw CommitFailedException();
		}
	}
	// else {
	//	// as irrevocable, I already have the lock
	//}
	
	// buffered writes are performed here & now
	if(amIIrrevocable)
		for(auto & var : wsetBuffers)
			var.first->performWriteAsIrr(this, var.second);
	else
		for(auto & var : wsetBuffers)
			var.first->performWrite(this, var.second);
	
	// sync vars among theads
	atomic_thread_fence(memory_order_release);
	
	// after fence the vars can be free from being marked as used by irr
	if(amIIrrevocable){
		for(auto v : rsetBuffers)
			v.first->usedByIrr.store(false, memory_order_relaxed);
		for(auto var : wsetBuffers)
			var.first->usedByIrr.store(false, memory_order_relaxed);
	}
	
	// record successful commit
	comitted.store(true, memory_order_relaxed);
	
	// sync all flags among threads
	atomic_thread_fence(memory_order_release);
	
	// unlock all locks, any order
	for(auto m : locksHeld)
		m->clear(memory_order_relaxed);
	locksHeld.clear();
	
	// except for this lock, which has to be ordered as last
	if(amIIrrevocable)
		irrTransactionLock.clear(memory_order_relaxed);
	
	cleanup();
}

/*namespace TM end*/}

#ifdef TRACK_ABORTS
 atomic<int> __MY_abortPlaces[255];
#endif