#include "tmapi.h"

#include "transaction.h"

#include <iostream>

namespace Tm {

bool checkIfCompilerCupportsWaitFree() {
	/* In C++ specs 'lock-free' is not defined, but what it really means is that
	   is 'does not use locks at all, instead it uses atomic CPU instructions' */
	
	// 0 is lock-based, 1 is adress-dependant, 2 is always atomic instructions
	
	if (ATOMIC_BOOL_LOCK_FREE!=2)
		throw "atomic<bool> is not guaranteed to be 'lock-free' in c++ terms";
	
	if (ATOMIC_INT_LOCK_FREE!=2)
		throw "atomic<int> is not guaranteed to be 'lock-free' in c++ terms";
	
	#ifndef NDEBUG
	cerr << "c++ 'lock-freedom' of atomic<int> and atomic<bool> guaraneteed by compiler\n" << flush;
	#endif
	return true;
}

// The line below is not neat :-(
// But it's wait-free and fast, so…

// upper bound for the total number of theads over the whole application run
unsigned int maxThreadNum = 32;
atomic<unsigned int> threadIdSequencer;
/// Thread id of this thread, equal to number of threads started before
thread_local unsigned int threadId = threadIdSequencer.fetch_add(1, memory_order_relaxed);


function<void ()> nonTransAccess = [](){throw InvalidUseException(); };

function<void ()> forcingAbortOnIrr = [](){throw InvalidUseException(); };

static const volatile bool checkIfCompilerCupportsWaitFree_ = checkIfCompilerCupportsWaitFree();

/// \brief Stores the current transaction
thread_local shared_ptr<Transaction> currentTransaction;

void beginT() {
	if(currentTransaction) {
		// nesting? yuck!
		throw InvalidUseException();
	}
	
	currentTransaction.reset(new Transaction);
}


void abortT() {
	if(!currentTransaction) {
		// wait, there is no transaction running in this thread!
		throw InvalidUseException();
	}
	
	currentTransaction->abort();
}

void irrT() {
	if(!currentTransaction) {
		// wait, there is no transaction running in this thread!
		throw InvalidUseException();
	}
	
	currentTransaction->irr();
}


void commitT() {
	if(!currentTransaction) {
		// wait, there is no transaction running in this thread!
		throw InvalidUseException();
	}
	
	currentTransaction->commit();
}


/*namespace TM end*/}