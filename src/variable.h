#ifndef VARIABLE_H
#define VARIABLE_H

#include <cassert>

#include <memory>
#include <atomic>
#include <functional>
#include <vector>

#include "transaction.h"
#include "tmapi.h"

using namespace std;

namespace Tm {

/// Parent class for all Variable\<T\> objects, which allows calling variable-related functions from Transaction class
class VariableBase {

// Variables like transactions.
//As variables are in public API, methods called from outside are not made public but accessed this way.
friend class Transaction;

public:
	
	VariableBase() = default;
	
	VariableBase(const VariableBase &) = delete;
	
	virtual ~VariableBase(){};

protected:
	
	/// called i.a. on pre-commit to invalidate vars
	virtual void killReaders() = 0;
	
	/// called on transitting to irr in order to lock a read value.
	virtual atomic_flag * acquireRead() = 0;
	
	/// deleting void* is a bad idea, so this must be done here...
	virtual void deleteFromRset(void * rawBuff) = 0;
	
	/// deleting void* is a bad idea, so this must be done here...
	virtual void deleteFromWset(void * rawBuff) = 0;
	
	/// deleting void* is a bad idea, so this must be done here...
	virtual void deleteFromHijacked(void * rawBuff) = 0;
	
	/// called on commit to make the changes of an ordinarty trans. permanent
	virtual void performWrite(Transaction *, void * rawBuff) = 0;
	
	/// called on commit to make the changes of an irrevocable trans. permanent
	virtual void performWriteAsIrr(Transaction *, void * rawBuff) = 0;
	
	atomic<bool> usedByIrr {false};
	
	/// when var is dirty, then value and version are not consistent
	atomic<bool> dirty {false};
	
	/// we need another dirty for the irrevocable transaction for hijack-related reasons
	atomic<bool> dirtyIrr {false};
	
	/// the transaction which has the lock can update global copy (i.e. var)
	atomic_flag lock {ATOMIC_FLAG_INIT};
	
	vector<weak_ptr<Transaction>> readers {maxThreadNum};
	
	// ghr.... missing atomic ops on shared ptrs in gcc are nasty...
	
	struct WeakPtrHolder{
		WeakPtrHolder() = default;
		WeakPtrHolder(const WeakPtrHolder& o): ptr(o.ptr == nullptr ? nullptr : new weak_ptr<Transaction>(*o.ptr)){};
		weak_ptr<Transaction>* ptr = nullptr;
		virtual ~WeakPtrHolder() {delete ptr;}
	};
	
	/** When updating \sa{mostRecentLockOwner} old ptr cannot be garbage-collected, since it can be used by irr;
	 * thus it's stored here and deleted by the next transaction */
	WeakPtrHolder previousLockOwner;
	// Since we store here plain ptr to weak ptr (see below), using weak_ptr or shared_ptr<weak_ptr> would
	// make mess when anyone copies vars, as {var x = y ; begin ; w(x) ; commit ; } would free more than it should
	
	/// overwritten after successful lock
	atomic<weak_ptr<Transaction>*> mostRecentLockOwner {nullptr};
	
};

/** This class must wrap any variable shared among transactions.
 * 
 * Variable\<T\> object can be copied at will.
 * 
 * To gain read/write access one must call \sa{ro()} / \sa{rw()}.
 * Reads and writes are allowed only from within transactions. */

template <typename T>
class Variable : public VariableBase
{
protected:
	/// the real variable
	shared_ptr<T> varPtr;
	
	/// adds this variable to read set with given buffer and version
	inline void setRset(Tm::Transaction* ctb, T* buffer){
		ctb->rsetBuffers[this] = buffer;
	}
	
	/// takes this variable back from read set and returns the buffer
	inline T* unsetRset(Tm::Transaction* ctb, const decltype(currentTransaction->rsetBuffers.find(nullptr)) & rsetElement){
		T* ret = (T*) rsetElement->second;
		ctb->rsetBuffers.erase(rsetElement);
		return ret;
	}
	
	/// adds this variable to write set with given buffer
	inline void setWset(Tm::Transaction* ctb, shared_ptr<T>* buffer){
		ctb->wsetBuffers[this]=buffer;
	}
	
public:
	
	/// auto-constructs the variable
	Variable() : varPtr(new T) {}
	
	/// auto-constructs the variable and initializes with _val
	Variable(T val) : Variable() {
		*varPtr=val;
	}
	
	Variable(const Variable &) = delete;
	
	virtual ~Variable(){
		delete mostRecentLockOwner.load(memory_order_relaxed);
	}
	
	/**
	 * \brief Gives read-only access to the variable
	 * \throws InvalidUseException if there is no active transaction in this thread
	 * \throws ReadFailedException if a conflict has been detected and the transaction was aborted
	 **/
	const T & ro(){
		if(!currentTransaction){
			nonTransAccess();
			return *varPtr;
		}
		
		// performance hack
		Tm::Transaction* ctb = currentTransaction.get();
		
		// first, let's check the read and write set
		{
			const auto & elementR = ctb->rsetBuffers.find(this);
			
			if (elementR != ctb->rsetBuffers.end()) {
				// ok, the variable is in the read set.
				
				// to be precise, it's here
				T * buffer = (T*) elementR->second;
				
				// so let's give our buffer to the user
				return *buffer;
			}
			
			auto elementW = ctb->wsetBuffers.find(this);
			
			if (elementW != ctb->wsetBuffers.end()) {
				// the var is here:
				shared_ptr<T> * buffer = (shared_ptr<T>*) elementW->second;
				
				// so let's give it to the user
				return **buffer;
			}
		}
		
		if(ctb->amIIrrevocable){
			return roIrr(ctb);
		}
		
		// Visible read - let's bookkeep the read
		// sic: this makes a weak ptr from a shared one
		readers[threadId] = currentTransaction;
		
		// make our read visible to others
		atomic_thread_fence(memory_order_seq_cst);
		
		// if dirty is true, then the writer may not notice us. Also, we're deemed to abort.
		if(dirty.load(memory_order_relaxed) || dirtyIrr.load(memory_order_relaxed)) {
			ABORT_LOG_SOURCE(7);
			ctb->abort();
			throw ReadFailedException();
		}
		
		// We must now make sure that we see a recent version of var
		atomic_thread_fence(memory_order_acquire);
		
		// which we read right now
		T * buffer = new T(*varPtr);
		
		// next we need to check if we are consistent.
		// any transaction that could have altered the var, must have set aborted to true earlier
		if(ctb->aborted.load(memory_order_acquire)) {
			ABORT_LOG_SOURCE(13);
			delete buffer;
			ctb->abort();
			throw ReadFailedException();
		}
			
		setRset(ctb, buffer);
		
		return *buffer;
	}

	/**
	 * \brief Gives read-write access to the variable
	 * \throws InvalidUseException if there is no active transaction in this thread
	 * \throws WriteFailedException if a conflict has been detected and the transaction was aborted
	 **/
	T & rw(){
		if(!currentTransaction){
			nonTransAccess();
			return *varPtr;
		}
		
		// performance hack
		Tm::Transaction* ctb = currentTransaction.get();
		
		// first, let's check the write set
		auto element = ctb->wsetBuffers.find(this);
		
		if (element!=ctb->wsetBuffers.end()) {
			// the var is here:
			shared_ptr<T> * buffer = (shared_ptr<T>*) element->second;
			
			// so let's give it to the user
			return **buffer;
		}
		
		if(ctb->amIIrrevocable){
			return rwIrr(ctb);
		}
		
		// first access to the variable.
		// let's go!
		
		if (usedByIrr.load(memory_order_acquire)){
			// uhm... conflicting with an irrevocable cannot end well
			ctb->abort();
			ABORT_LOG_SOURCE(8);
			throw WriteFailedException();
		}
		
		if(lock.test_and_set(memory_order_acquire)){
			// someone else has the lock, that's bad (for us)
			ctb->abort();
			ABORT_LOG_SOURCE(9);
			throw WriteFailedException();
		}
		
		// There is a chance that a concurrent irr trans read mostRecentLockOwner and is going to use preLockOwner
		weak_ptr<Transaction>* preLockOwner = mostRecentLockOwner.load(memory_order_acquire);
		// But there is no chance that any trans is using this one (if that irr trans that uses ↑ would still exist, I 
		// wouldn't be here as usedByIrr would not let me in)
		delete previousLockOwner.ptr;
		// So, let's dely delete of preLockOwner till anyone is here again
		previousLockOwner.ptr = preLockOwner;
		
		mostRecentLockOwner.store(new weak_ptr<Transaction>(currentTransaction), memory_order_relaxed);
		
		if (usedByIrr.load(memory_order_acquire)){
			// this check (for the second time) is a must.
			// without, the irrevocable transaction might not see the owner, but the owner would operate
			lock.clear(memory_order_relaxed);
			ctb->abort();
			ABORT_LOG_SOURCE(10);
			throw WriteFailedException();
		}
		
		// we won the lock :-)
		
		shared_ptr<T>* buffer = nullptr;
		
		// first, let's see if it has been read before
		auto rsetElement = ctb->rsetBuffers.find(this);
		if (rsetElement!=ctb->rsetBuffers.end()) {
			// if we did read the var, its value is correct, as we just have validated the read set (after getting the lock)
			// so we remove buffer from rset and re-use it for wset. 
			buffer = new shared_ptr<T>(unsetRset(ctb, rsetElement));
		}
		
		if(buffer==nullptr){
			atomic_thread_fence(memory_order_acquire);
			// we can't just copy the pointer, we need another item
			buffer = new shared_ptr<T>(new T(*varPtr));
		}
		
		// even though this is write, what this funcion returns is a non-const reference to val;
		// so we must ensure that if someone reads it, it's going to be opaque.
		if(ctb->aborted.load(memory_order_acquire)){
			// our state is inconsistent.
			delete buffer;
			lock.clear(memory_order_relaxed);
			ctb->abort();
			ABORT_LOG_SOURCE(11);
			throw WriteFailedException();
		}
		
		
		setWset(ctb, buffer);
		
		ctb->locksHeld.insert(&lock);
		
		return **buffer;
	}
	
protected:
	/// called by ro() when the var is neither in read- nor in write-set
	const T &  roIrr(Tm::Transaction* ctb) {
		irrAcquire(ctb, true);
		
		// this won't loop, as irrAcquire adds var to rset/wset
		return ro();
	}
	
	/// called by rw() when the var is not in write-set, but potentially in read-set.
	T &  rwIrr(Tm::Transaction* ctb) {
		
		// first, let's see if the var is in read set
		auto rsetElement = ctb->rsetBuffers.find(this);
		if (rsetElement!=ctb->rsetBuffers.end()) {
			// let's take read bufer
			auto readBuffer = unsetRset(ctb, rsetElement);
			
			// we can reuse it directly here
			setWset(ctb, new shared_ptr<T>(readBuffer));
		} else {
			irrAcquire(ctb, false);
		}
		
		// this won't loop, as irrAcquire adds var to wset
		return rw();
	}
	
	/// called each time when an irrevocable transaction acquires a never-seen-before variable
	void irrAcquire(Tm::Transaction* ctb, bool wantReadOnly) {
		
		// tell others to hold back
		usedByIrr.store(true, memory_order_relaxed);
		
		do { // this is not a loop, this is syntactic sugar to replace goto's
		
			if(!lock.test_and_set(memory_order_relaxed)){
				// that was easy.
				
				// we're irr, so we don't need to add us to lock owners
				// (as it is read only by irr, and there can be at most one irr)
				ctb->locksHeld.insert(&lock);
				break;
			}
			
			// look up who has the lock
			
			weak_ptr<Transaction> * pmrlowp = mostRecentLockOwner.load(memory_order_relaxed);
			
			if (pmrlowp == nullptr){
				// if lock owner tries to progress, it will die due to usedByIrr (unless it waits or it already finished).
				break;
			}
			
			shared_ptr<Transaction> lockOwner = pmrlowp->lock();
			
			if(!lockOwner){
				// previous lock owner belongs to a forgotten past.
				break;
			}
			
			if(!lockOwner->commitLock.test_and_set(memory_order_relaxed)){
				// kaboom. that transaction can no longer commit.
				// let's mark for future that it's aborted.
				lockOwner->aborted.store(true, memory_order_relaxed);
				break;
			}
			
			// locking owner commit lock failed
			if(lockOwner->aborted.load(memory_order_relaxed)
				|| lockOwner->comitted.load(memory_order_relaxed) ){
				// this possible owner already finished
				// let's ignore it
				break;
			}
			
			// this is a live owner!
			
			// it has checked all commit conditions and will just write its updates.
			// so, we must hijack its buffer.
			
			const auto & it = lockOwner->wsetBuffers.find(this);
			assert (it != lockOwner->wsetBuffers.end());
			shared_ptr<T>* hijackedBuffer = ((shared_ptr<T>*) it->second);
			
			// we must keep track of the buffer, and we must properly keep track of its use count as well
			ctb->hijackedWsetBuffers[this] = new shared_ptr<T>(*hijackedBuffer);
			
			atomic_thread_fence(memory_order_acquire);
			
			// we must use value that is in this buffer
			setWset(ctb, new shared_ptr<T>(new T(**hijackedBuffer)));
			return;
			
		} while(false);
		
		// whatever happened until now, we have exclusive access to the global var
		
		if(wantReadOnly){
			setRset(ctb, new T(*varPtr));
		} else {
			setWset(ctb, new shared_ptr<T>(new T(*varPtr)));
		}
	}
	
	/**
	 * \brief When transitting to irrevocable state, tries to lock the variable as read.
	 * \returns empty ptr on failure, locked lock on success
	 */
    atomic_flag * acquireRead() override {
		
		// we're irr, so we don't need to add us to potential owners
		
		usedByIrr.store(true, memory_order_relaxed);
		
		// If this read is not consistent, then either:
		//  – we won't get the lock
		//  – we have been aborted (but didn't notice it yet)
		
		if(lock.test_and_set(memory_order_relaxed)){
			return nullptr;
		}
		
		return &lock;
	}
	
	void killReaders() override {
		for( unsigned int i = 0 ; i < maxThreadNum ; ++i ){
			// don't kill self
			if(i==threadId) continue;
			
			shared_ptr<Transaction> possReader = readers[i].lock();
			
			// don't kill the program
			if(!possReader) continue;
			
			// kill everything that gives in.
			if(!possReader->cleanReadsetLock.test_and_set(memory_order_relaxed)){
				possReader->aborted.store(true, memory_order_relaxed);
			}
			// 1) those that aborted/committed -> meh.
			// 2) irrevocable -> won't die - got their lock. Besides, we're dead aleready. Walking dead [transaction].
			//                   simply when they read the variable, we got shot. We're going to notice that soon.
		}
	}
	
	void deleteFromRset(void * rawBuff) override {
		// (*readers)[threadId].reset();
		T* buffer = (T*) rawBuff;
		delete buffer;
	}
	
	void deleteFromWset(void * rawBuff) override {
		shared_ptr<T>* buffer = (shared_ptr<T>*) rawBuff;
		delete buffer;
		// this delete auto-cascades.
	}
	
	void deleteFromHijacked(void * rawBuff) override {
		shared_ptr<T>* buffer = (shared_ptr<T>*) rawBuff;
		delete buffer;
		// this delete auto-cascades as well
	}
	
	void performWriteAsIrr(Transaction * ctb, void * rawBuff) override {
		shared_ptr<T>* newVal = (shared_ptr<T>*) rawBuff;
		
		// now... if there is a hijacked transaction...
		const auto & hijacked = ctb->hijackedWsetBuffers.find(this);
		if(hijacked != ctb->hijackedWsetBuffers.end()){
			shared_ptr<T>* hijackedBuffer = (shared_ptr<T>*) hijacked->second;
			**hijackedBuffer = **newVal;
			
			varPtr = *hijackedBuffer;
		} else {
			// I have the most recent value, so acquire fence is not needed here
			varPtr = *newVal;
		}
		
		// my changes need to be made visible
		dirtyIrr.store(false, memory_order_release);
	}
	
	void performWrite(Transaction * ctb, void * rawBuff) override {
		shared_ptr<T>* newValShared = (shared_ptr<T>*) rawBuff;
		
		varPtr = *newValShared;
		
		dirty.store(false, memory_order_release);
	}
};

/*namespace TM end*/}
#endif // VARIABLE_H
