#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#ifdef PGTBL_SOL
#include "riscv.h"
#endif
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > UTOP)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_vmprint(void)
{
  struct proc *p;

  p = myproc();
  vmprint(p->pagetable);
  return 0;
}
#endif

#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  uint64 start;
  uint64 maskva;
  int npages;
  uint64 nbytes;
  char *bits;
  struct proc *p = myproc();

  argaddr(0, &start);
  argint(1, &npages);
  argaddr(2, &maskva);

  // A single kernel page can hold the result for this many user pages.
  if (npages < 0 || npages > PGSIZE * 8)
    return -1;
  if (npages == 0)
    return 0;

  nbytes = (npages + 7) / 8;
  bits = kalloc();
  if (bits == 0)
    return -1;
  memset(bits, 0, PGSIZE);

  // First collect the access bits and reject any range with an unmapped page.
  // Do not clear PTE_A yet: copyout can still fail for an invalid result buffer.
  for (int i = 0; i < npages; i++) {
    uint64 va = start + (uint64)i * PGSIZE;
    pte_t *pte;

    if (va < start || va >= MAXVA) {
      kfree(bits);
      return -1;
    }
    pte = walk(p->pagetable, va, 0);
    if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 ||
        (*pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      kfree(bits);
      return -1;
    }
    if (*pte & PTE_A)
      bits[i / 8] |= 1 << (i % 8);
  }

  if (copyout(p->pagetable, p->sz, maskva, bits, nbytes) < 0) {
    kfree(bits);
    return -1;
  }

  // Reset the recorded state so the next call reports accesses since this one.
  for (int i = 0; i < npages; i++) {
    uint64 va = start + (uint64)i * PGSIZE;
    pte_t *pte = walk(p->pagetable, va, 0);
    *pte &= ~PTE_A;
  }

  kfree(bits);
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

#ifdef LAB_LOCK
uint64
sys_cpupin(void)
{
  struct proc *p = myproc();
  int cpu;

  argint(0, &cpu);
  if (cpu < 0 || cpu >= NCPU)
    return -1;
  acquire(&p->lock);
  p->pincpu = &cpus[cpu];
  release(&p->lock);
  return 0;
}
#endif
