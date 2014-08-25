#ifndef __LIBPAXOS_H
#define __LIBPAXOS_H

#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>

/* Logging and verbosity levels */
#define PAXOS_LOG_QUIET 0
#define PAXOS_LOG_ERROR 1
#define PAXOS_LOG_INFO 2
#define PAXOS_LOG_DEBUG 3

/* Paxos instance ids and ballots */
typedef uint32_t iid_t;
typedef uint32_t ballot_t;

struct paxos_config
{
	/*show log level*/
	int		verbosity;

	/*Learner conf*/
	int		learn_instances;
	int		learner_catch_up;

	/*Proposer conf*/
	int		proposer_timeout;
	int		proposer_preexec_window;

	/*Acceptor conf*/

	/*BDB storge conf*/
	int		bdb_sync;
	int		bdb_cachesize;
	char*	bdb_env_path;
	char*	bdb_db_filename;
	int		bdb_trash_files;
};

extern struct paxos_config paxos_config;

int		paxos_quorum(int acceptors);

/*log functions*/
void	paxos_log(int level, const char* format, va_list ap);
void	paxos_log_error(const char* format, ...);
void	paxos_log_info(const char* format, ...);
void	paxos_log_debug(const char* format, ...);

/*
	TODO MAX_N_OF_PROPOSERS should be removed.
	The maximum number of proposers must be fixed beforehand
	(this is because of unique ballot generation).
	The proposers must be started with different IDs.
	This number MUST be a power of 10.
*/
#define MAX_N_OF_PROPOSERS  10

#endif

