/* See COPYRIGHT for copyright information. */

#ifndef JOS_INC_ENV_H
#define JOS_INC_ENV_H

#include <inc/types.h>
#include <inc/trap.h>
#include <inc/memlayout.h>
#include <kern/spinlock.h>

typedef int32_t envid_t;

// An environment ID 'envid_t' has three parts:
//
// +1+---------------21-----------------+--------10--------+
// |0|          Uniqueifier             |   Environment    |
// | |                                  |      Index       |
// +------------------------------------+------------------+
//                                       \--- ENVX(eid) --/
//
// The environment index ENVX(eid) equals the environment's offset in the
// 'envs[]' array.  The uniqueifier distinguishes environments that were
// created at different times, but share the same environment index.
//
// All real environments are greater than 0 (so the sign bit is zero).
// envid_ts less than 0 signify errors.  The envid_t == 0 is special, and
// stands for the current environment.

#define LOG2NENV		10
#define NENV			(1 << LOG2NENV)
#define ENVX(envid)		((envid) & (NENV - 1))

// Values of env_status in struct Env
enum EnvStatus{
	ENV_FREE = 0,
	ENV_DYING = 1,
	ENV_RUNNABLE = 2,
	ENV_RUNNING = 3,
	ENV_NOT_RUNNABLE = 4,
};

// Special environment types
enum EnvType {
	ENV_TYPE_USER = 0,
	ENV_TYPE_IDLE = 1,
	ENV_TYPE_FS = 2,		// File system server
	ENV_TYPE_NS = 3,		// Network server
};

struct Env {
	struct Trapframe env_tf;	// Saved registers
	struct Env *env_link;		// Next free Env
	envid_t env_id;			// Unique environment identifier
	envid_t env_parent_id;		// env_id of this env's parent
	enum EnvType env_type;		// Indicates special system environments
	enum EnvStatus env_status;		// Status of the environment
	uint32_t env_runs;		// Number of times environment has run
	int env_cpunum;			// The CPU that the env is running on

	// Address space
	pde_t *env_pgdir;		// Kernel virtual address of page dir

	// Exception handling
	void *env_pgfault_upcall;	// Page fault upcall entry point

	// Lab 4 IPC
	bool env_ipc_recving;		// Env is blocked receiving
	void *env_ipc_dstva;		// VA at which to map received page
	uint32_t env_ipc_value;		// Data value sent to us
	envid_t env_ipc_from;		// envid of the sender
	int env_ipc_perm;		// Perm of page mapping received

  // Challenge: non-retry send.
  // acquire the lock when performing operation on waiting_id.
  // leave and yield when anyone is waiting
  struct spinlock env_ipc_queue_lock;
  envid_t env_ipc_waiting_count;

  // buffer for outgoing data
	bool env_ipc_sending;		// Env is blocked sending
	void *env_ipc_va_send;		// VA at which to map sending page
	uint32_t env_ipc_value_send;		// Data value to send
	envid_t env_ipc_to;		// envid of the receiver
	int env_ipc_perm_send;		// Perm of page mapping received
};

#endif // !JOS_INC_ENV_H
