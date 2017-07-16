#ifndef TMAPI_H
#define TMAPI_H

/**
 * \file tmapi.h
 * \brief This file is intended to be the header to be included by the program
 **/

#include <functional>
using namespace std;

namespace Tm {
	
	/// Total maximum number of threads executing transactions; can be set only before variables are created
	extern unsigned int maxThreadNum;
	
	// exception tree
	
	/*       */ /// base class for all exceptions
	/* #     */ class TransactionException{};
	/* |     */ /// base class that incorporates abort while accessing (reading or writing) some var
	/* |-#   */ class AccessFailedException:public TransactionException{};
	/* | |   */ 
	/* | +-# */ class ReadFailedException  :public AccessFailedException{};
	/* | |   */ 
	/* | +-# */ class WriteFailedException :public AccessFailedException{};
	/* |     */ /// failed to transit to irrevocable state
	/* +-#   */ class IrrevocTransException:public TransactionException{};
	/* |     */
	/* +-#   */ class CommitFailedException:public TransactionException{};
	/* |     */ /// thrown on nesting transactions, aborting outside transaction, reading vars without transactions etc.
	/* +-#   */ class InvalidUseException  :public TransactionException{};

	/**
	 * \brief Starts a new transaction in current thread
	 * \throws InvalidUseException if there already exists some transaction
	 */
	void beginT();
	
	/**
	 * \brief Transits current transaction to irrevocable state
	 * \throws InvalidUseException if there is no transaction in current thread
	 * \throws IrrevocTransException if the operation failed
	 */
	void irrT();
	
	/**
	 * \brief Explicitly aborts current transaction
	 * \throws InvalidUseException if there is no transaction in current thread
	 */
	void abortT();
	
	/**
	 * \brief Commits current transaction
	 * \throws InvalidUseException if there is no transaction in current thread
	 * \throws CommitFailedException if the commit failed
	 */
	void commitT();
	
	/** 
	 * \brief Function called whenever a variable is read or written to outside transaction. By default it throws an exception.
	 */
	extern function<void ()> nonTransAccess;
	
	/** 
	 * \brief Function called on abort explicitly forced by the user on an irrevocable transaction.
	 * By default it throws an exception.
	 */
	extern function<void ()> forcingAbortOnIrr;
};

/// definition of Variable template class
#include "variable.h"

#endif // TMAPI_H
