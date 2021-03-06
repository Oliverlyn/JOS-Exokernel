/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/e1000.h>
#include <kern/flexsc.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
   user_mem_assert(curenv, s, len, PTE_U);
      
	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
   if (curenv->env_type == ENV_TYPE_FLEX)
      return curenv->link->env_id;

	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;

   // If this process has a serving syscall thread, destroy it first
   if (e->link)
      env_destroy(e->link);

	env_destroy(e);

	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.

   struct Env *e;
   int error;

   if ((error = env_alloc(&e, curenv->env_id)) < 0)
      return error;
   
   // Set not runnable so it doesn't run until permitted 
   e->env_status = ENV_NOT_RUNNABLE;
   // Copy all trap frame registers from parent
   e->env_tf = curenv->env_tf;
   // Set %eax to 0 so it appears to return 0 in the child
   e->env_tf.tf_regs.reg_eax = 0;   
   // Return child id for the parent env
   return e->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.

   struct Env *e;
   int error;

   // Check status we're trying to set
   if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE)
      return -E_INVAL;
   // Get env from id and check if we have perm to change its status
   if ((error = envid2env(envid, &e, 1)) < 0)
      return error;

   e->env_status = status;

   return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3) with interrupts enabled.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
   
   struct Env *e;
   int error;

   // Check if we have permission to access this address
   user_mem_assert(curenv, tf, sizeof(struct Trapframe), PTE_U);

   // Get env from id and check if we have perm to change it
   if ((error = envid2env(envid, &e, 1)) < 0)
      return error;

   e->env_tf = *tf;   
   // Switch the CPL to 3 in code segment register
   e->env_tf.tf_cs = GD_UT | 3;   
   // Enable interrupts
   e->env_tf.tf_eflags |= FL_IF;

   return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.

   struct Env *e;
   int error;
   
   // Check if func is in a valid address range
//   if ((uintptr_t)func >= ULIM)
//      return -E_INVAL;

   // Get env from id and check if we have perm to change its status
   if ((error = envid2env(envid, &e, 1)) < 0)
      return error;

   e->env_pgfault_upcall = func;   

   return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.

   struct Env *e;
   struct PageInfo *page;
   int error;

   // Check if va >= UTOP and not page-aligned
   if ((uintptr_t)va >= UTOP || (uintptr_t)va & 0xFFF)
      return -E_INVAL;
   // Check if the permission bits are valid
   if (!(perm & (PTE_U | PTE_P)) || perm & ~PTE_SYSCALL)
      return -E_INVAL;
   // Get env from id and check if we have perm to change it
   if ((error = envid2env(envid, &e, 1)) < 0)
      return error;
   // Allocate the page and return error if no memory to allocate
   if (!(page = page_alloc(ALLOC_ZERO))) 
      return -E_NO_MEM;
   // Insert the page and return error if no mem to allocate pt 
   if ((error = page_insert(e->env_pgdir, page, va, perm)) < 0) {
      page_free(page);  // Free the page!
      return error;   
   }

   return 0;
}


// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
   
   struct Env *srce, *dste;
   struct PageInfo *page;
   pte_t *ptEntry;
   int error;

   // Get env from id and check if we have perm to change it
   if ((error = envid2env(srcenvid, &srce, 1)) < 0)
      return error;
   if ((error = envid2env(dstenvid, &dste, 1)) < 0)
      return error;
   // Check if srcva or dstva is >= UTOP or not page aligned
   if ((uintptr_t)srcva >= UTOP || (uintptr_t)srcva & 0xFFF \
         || (uintptr_t)dstva >= UTOP || (uintptr_t)dstva & 0xFFF)
      return -E_INVAL;
   // Check if srcva is mapped in srcenvid's address space
   if (!(page = page_lookup(srce->env_pgdir, srcva, &ptEntry)))
      return -E_INVAL;
   // Check if the permission bits are valid
   if (!(perm & (PTE_U | PTE_P)) || perm & ~PTE_SYSCALL)
      return -E_INVAL;
   // If srcva is read-only, perm cannot have write in it
   if (perm & PTE_W && !(*ptEntry & PTE_W))
      return -E_INVAL;
   // Insert page into dst env address space
   if ((error = page_insert(dste->env_pgdir, page, dstva, perm)) < 0)
      return error;    

   return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.

   struct Env *e;
   int error;
      
   // Check if va >= UTOP and not page-aligned
   if ((uintptr_t)va >= UTOP || (uintptr_t)va & 0xFFF)
      return -E_INVAL;
   // Get env from id and check if we have perm to change it
   if ((error = envid2env(envid, &e, 1)) < 0)
      return error;

   // Remove the mapping
   page_remove(e->env_pgdir, va);
   
   return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.

   struct PageInfo *page;
   pte_t *ptEntry;
   struct Env *e;
   int error;

   // Get the env struct (NOT checking permissions)
   if ((error = envid2env(envid, &e, 0)) < 0)
      return error;
   // See if the destination env is blocked
   if (!e->env_ipc_recving)
      return -E_IPC_NOT_RECV;

   // Block other threads from sending
   e->env_ipc_recving = 0;
   e->env_ipc_from = curenv->env_id;
   // Send the value
   e->env_ipc_value = value;
   e->env_ipc_perm = 0;

   // Check if both src and dst agree that a page is to be transferred
   if ((uintptr_t)srcva < UTOP && (uintptr_t)e->env_ipc_dstva < UTOP) {
      // Check if we're page aligned
      if ((uintptr_t)srcva & 0xFFF)
         return -E_INVAL;
      // Check if the permission bits are valid
      if (!(perm & (PTE_U | PTE_P)) || perm & ~PTE_SYSCALL)
         return -E_INVAL;
      // Check if srcva is mapped in current env's address space 
      if (!(page = page_lookup(curenv->env_pgdir, srcva, &ptEntry)))
         return -E_INVAL;
      // Check if entry at srcva is read-only
      if (perm & PTE_W && !(*ptEntry & PTE_W))
         return -E_INVAL;
      // Map the page
      if ((error = page_insert(e->env_pgdir, page, e->env_ipc_dstva, perm)) < 0)
         return error;    

      e->env_ipc_perm = perm;
   }

   // Mark the target env runnable again
   e->env_status = ENV_RUNNABLE;

   return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
   // FlexSC IPC only handle passing values for now
   if (curenv->env_type == ENV_TYPE_FLEX) {
      // Tell them we're ready to receive
      curenv->link->env_ipc_recving = 1;
      // Put user process to sleep
      curenv->link->env_status = ENV_NOT_RUNNABLE;
      return -E_BLOCKED;
   } 

	// LAB 4: Your code here.
   
   curenv->env_ipc_recving = 1;
   
   if ((uintptr_t)dstva < UTOP) {
      if ((uintptr_t)dstva & 0xFFF)
         return -E_INVAL;
      curenv->env_ipc_dstva = dstva;
   } 

   // Simulate a 0 return value sometime in the future
   curenv->env_tf.tf_regs.reg_eax = 0;

   // Block this env
   curenv->env_status = ENV_NOT_RUNNABLE; 
   sched_yield();

	return 0;
}

// Return the current time.
static int
sys_time_msec(void)
{
	// LAB 6: Your code here.
   return time_msec();
}

static int
sys_net_send_pckt(void *src, uint32_t len)
{
   // Check if packet address is in user space and doesn't exceed buffer size 
   if (src && ((uintptr_t)src >= UTOP || len > PBUFSIZE))
      return -E_INVAL; 
   
   return trans_pckt(src, len);
}

// Lab6 Challenge: map user page to kernel address space for zero-copy

static int
sys_net_recv_pckt(void *dstva)
{
   // If dstva == NULL, use mapped buffer
   if (dstva != NULL)
      user_mem_assert(curenv, dstva, RBUFSIZE, PTE_U | PTE_W | PTE_P);

   return recv_pckt(dstva);
}

// Challenge
static int
sys_env_set_priority(envid_t envid, int priority)
{
   int error;
   struct Env *e;

   // Check if it is a valid priority level
   if (priority < ENV_PR_HIGHEST || priority > ENV_PR_LOWEST)
      return -E_INVAL;

   // Only parent of a child can change its priority
   if ((error = envid2env(envid, &e, 1)) < 0)
      return error;

   e->env_priority = priority;

   return 0;   
}

// FlexSC system calls:

// A process must register a syscall page with this syscall in order
// to use the FlexSC facility.
static int 
flexsc_register(void *va)
{
   struct PageInfo *page;
   struct Env *scthread;
   void *scpage;
   int r;

   user_mem_assert(curenv, va, PGSIZE, PTE_W | PTE_U | PTE_P);
   
   if (!(scpage = scpage_alloc()))
      panic("Cannot allocate syscall page!");

   // Map syscall page into user-space memory address
   if (!(page = page_lookup(kern_pgdir, scpage, NULL)))
      return -E_INVAL;
   if ((r = page_insert(curenv->env_pgdir, page, 
        va, PTE_W | PTE_U | PTE_P)) < 0) {
      page_free(page);  // If we cannot insert, free the page!
      return r;   
   }

   // Spawn a syscall thread to work on the page
   if ((r = scthread_spawn(curenv)) < 0)
      panic("Cannot spawn a syscall thread: %e", r);

   cprintf("Spawned syscall thread %08x\n", r);
   scthread = &envs[ENVX(r)];

   // Set syscall page for process and its syscall thread
   scthread->scpage = scpage;
   curenv->scpage = scpage;
   // Link the user process and its syscall thread
   curenv->link = scthread;
   scthread->link = curenv;

   return 0;
}

// Process uses this system call to tell kernel that it cannot progress 
// further and is waiting on pending system calls to be processed.
// Puts the user thread to sleep. FlexSC will later wake up this
// process when at least 1 of the posted system calls are complete.
static int 
flexsc_wait()
{
   // Wake up the syscall thread for this process
   scthread_run(curenv->link);

   // Put this user process to sleep
   curenv->env_status = ENV_NOT_RUNNABLE;
   sched_yield();

   return 0;
}


// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.
   
   int32_t ret = 0;
   
	switch (syscallno) {
   case SYS_cputs:
      sys_cputs((char *)a1, (size_t)a2);
      break;      
   case SYS_cgetc:
      ret = sys_cgetc();
      break;
   case SYS_getenvid:
      ret = sys_getenvid();
      break;
   case SYS_env_destroy:
      ret = sys_env_destroy((envid_t)a1);
      break;
   case SYS_yield:
      sys_yield();
      break;
   case SYS_exofork:
      ret = sys_exofork();
      break;
   case SYS_env_set_status:
      ret = sys_env_set_status((envid_t)a1, (int)a2);
      break;
   case SYS_env_set_pgfault_upcall:
      ret = sys_env_set_pgfault_upcall((envid_t)a1, (void *)a2);
      break;
   case SYS_page_alloc:
      ret = sys_page_alloc((envid_t)a1, (void *)a2, (int)a3);
	   break;
   case SYS_page_map:
      ret = sys_page_map((envid_t)a1, (void *)a2, (envid_t)a3, 
                          (void *)a4, (int)a5);
      break;
   case SYS_page_unmap:
      ret = sys_page_unmap((envid_t)a1, (void *)a2);
      break;
   case SYS_ipc_try_send:
      ret = sys_ipc_try_send((envid_t)a1, (uint32_t)a2, (void *)a3, (unsigned)a4);
      break;
   case SYS_ipc_recv:
      ret = sys_ipc_recv((void *)a1);
      break;
   case SYS_env_set_priority:    // Lab 4 Challenge
      ret = sys_env_set_priority((envid_t)a1, (int)a2);
      break;
   case SYS_env_set_trapframe:
      ret = sys_env_set_trapframe((envid_t)a1, (struct Trapframe *)a2);
      break;
   case SYS_time_msec:
      ret = sys_time_msec();
      break;
   case SYS_net_send_pckt:
      ret = sys_net_send_pckt((void *)a1, (uint32_t)a2);
      break;
   case SYS_net_recv_pckt:
      ret = sys_net_recv_pckt((void *)a1);
      break;
   case FLEXSC_register:
      ret = flexsc_register((void *)a1);
      break;
   case FLEXSC_wait:
      ret = flexsc_wait();
      break; 
   default:
		return -E_INVAL;
	}

   return ret;
}

