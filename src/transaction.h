#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <atomic>

using namespace std;

namespace Tm {

class VariableBase;
class Transaction;
extern thread_local shared_ptr<Transaction> currentTransaction;
extern thread_local unsigned int threadId;

/**
 * Objects of this class are stored in thread local var currentTransaction.
 * Contents of this class is mostly a list of hooks to be called on Variables.
 */
class Transaction
{
// My friend, class Variable, takes care of reads and writes (mostly).
// Variables can tamper with transaction internals.
template <typename T> friend class Variable;

/* static variables - all that is related to the irrevocable transaction
 */

protected:
	/// lock guarding at-most-one irrevocable transaction
	static atomic_flag irrTransactionLock;

public:
	/// creates a transaction object and "starts" / "begins" the transaction
    Transaction();

	/** \brief tries to commit
	 *  \throws CommitFailedException */
	void commit();

	/** \brief requests the transaction to become irrevocable
	 *  \throws IrrevocTransException */
	void irr(); 

	/// aborts the transaction
	void abort();

	/// performs final cleanup; first part is \sa{Transaction::cleanup()}
    virtual ~Transaction();
protected:

	/// before commit, aborts all transactions that read some var that is to be just overritten
	void killReaders();
	
	/// called while transitting to irrevocalbe state, locks all values from read set
	bool acquireReadset();
	
	/// frees most of the memory held by the transaction and unlock all locks
	void cleanup();
	
	/// If any trans overwrites a read of this trans, it takes this lock. Without it, this trans cannot commit.
	atomic_flag cleanReadsetLock {ATOMIC_FLAG_INIT};
	
	/// Can only be taken by this transaction or the irrevoc. If irrevoc fails locking, it knows we successfully commit
	atomic_flag commitLock {ATOMIC_FLAG_INIT};
	
	/// set to true upon finishing the commit
	atomic<bool> comitted {false};
	
	/// set to true if the transaction has or has been aborted
	atomic<bool> aborted {false};
	
	/* Allowed states:
	 * comitted  aborted 
	 *     0        0 
	 *     0        1 
	 *     1        0
	 */
	
	/// Keeps track if the transaction transitted to irrevocable state
	bool amIIrrevocable = false;
	
	/**
	 * IMPORTANT:
	 * 
	 * Address to the real variable (its global copy) is used as the key most of the time.
	 * This guarantees uniqueness.
	 * 
	 * For read buffer, raw pointer is used as it is easy to tell when the buffer must be freed.
	 * 
	 * For writes however this gets more complicated, thus shared ptr is used; however, it is
	 * impossible to store shared_ptr to unknown at compile type template, thus ptr to shared_ptr is used.
	 */
	
	/**
	 * \brief Keeps addresses of buffers (local copies) for read variables
	 * key is *raw* ptr of the variable class object
	 * val is *raw* ptr of local copy
	 **/
	unordered_map<VariableBase*, void*> rsetBuffers;
	
	/**
	 * \brief Holds addresses of buffers (local copies) for updated variables
	 * key is *raw* ptr of  of the variable class object
	 * val is ptr to shared ptr of local copy
	 **/
	unordered_map<VariableBase*, void*> wsetBuffers;
	
	/// locks taken by transaction
	unordered_set<atomic_flag*> locksHeld;
	
	/// For irr trans keeps track of hijacked buffers 
	unordered_map<VariableBase*, void*> hijackedWsetBuffers;
};

/*namespace TM end*/}

#ifdef TRACK_ABORTS
 extern atomic<int> __MY_abortPlaces[255];
 #define ABORT_LOG_SOURCE(place) __MY_abortPlaces[place].fetch_add(1, memory_order_relaxed);
 inline void printAbortSources(){
	for(int i = 0; i<255; ++i){
		int aborts = __MY_abortPlaces[i].load();
		if(aborts!=0){
			printf ("Source: %3d    Aborts: %10d\n", i, aborts);
		}
	}
 }
#else
 #define ABORT_LOG_SOURCE(place)
#endif


#endif // TRANSACTION_H
