/*
 * Utility functions for trap handling in Supervisor mode.
 */

#include "riscv.h"
#include "process.h"
#include "strap.h"
#include "syscall.h"
#include "pmm.h"
#include "vmm.h"
#include "sched.h"
#include "util/functions.h"
#include "memlayout.h"

#include "spike_interface/spike_utils.h"

//
// handling the syscalls. will call do_syscall() defined in kernel/syscall.c
//
static void handle_syscall(trapframe *tf) {
  // tf->epc points to the address that our computer will jump to after the trap handling.
  // for a syscall, we should return to the NEXT instruction after its handling.
  // in RV64G, each instruction occupies exactly 32 bits (i.e., 4 Bytes)
  tf->epc += 4;

  // TODO (lab1_1): remove the panic call below, and call do_syscall (defined in
  // kernel/syscall.c) to conduct real operations of the kernel side for a syscall.
  // IMPORTANT: return value should be returned to user app, or else, you will encounter
  // problems in later experiments!
  //panic( "call do_syscall to accomplish the syscall and lab1_1 here.\n" );
  tf->regs.a0=do_syscall(tf->regs.a0,tf->regs.a1,tf->regs.a2,tf->regs.a3,tf->regs.a4,tf->regs.a5,tf->regs.a6,tf->regs.a7);
  //此处的a0-a7是指已经载入的8个寄存器内的数据，其中a0代表的是用户系统调用的类型。
  //a2和a3表示的内容为要在屏幕上打印的内容以及字符串长度，其余的全部是0（在printf和exit中定义的形参）
  //返回值为0，将其写入a0寄存器中，表示系统调用完成
}

//
// global variable that store the recorded "ticks". added @lab1_3
static uint64 g_ticks = 0;
//
// added @lab1_3
//
void handle_mtimer_trap() {
  sprint("Ticks %d\n", g_ticks);
  // TODO (lab1_3): increase g_ticks to record this "tick", and then clear the "SIP"
  // field in sip register.
  // hint: use write_csr to disable the SIP_SSIP bit in sip.
  //panic( "lab1_3: increase g_ticks by one, and clear SIP field in sip register.\n" );
  g_ticks++;  //将g_ticks进行自增操作，用于计数
  write_csr(sip, 0);  //对SIP的SIP_SSIP位清零，以保证下次再发生时钟中断时，M态的函数将该位设置为1会导致S模式的下一次中断。
}

//
// the page fault handler. added @lab2_3. parameters:
// sepc: the pc when fault happens;
// stval: the virtual address that causes pagefault when being accessed.
//
void handle_user_page_fault(uint64 mcause, uint64 sepc, uint64 stval) {
  sprint("handle_page_fault: %lx\n", stval);
  switch (mcause) {
    case CAUSE_STORE_PAGE_FAULT:
      // TODO (lab2_3): implement the operations that solve the page fault to
      // dynamically increase application stack.
      // hint: first allocate a new physical page, and then, maps the new page to the
      // virtual address that causes the page fault.
      //panic( "You need to implement the operations that actually handle the page fault in lab2_3.\n" );
      if ((stval < USER_STACK_TOP) && (stval > USER_STACK_TOP - PGSIZE * 20)) {  //判断stval与USER_STACK_TOP的大小关系
        void* pa = alloc_page();  //分配一个物理页
        user_vm_map((pagetable_t)current->pagetable, ROUNDDOWN(stval, PGSIZE), PGSIZE, (uint64)pa, prot_to_type(PROT_WRITE | PROT_READ, 1));
        //调用map_pages函数，其中current为当前物理页面的首地址，stval是要被映射的逻辑地址, PGSIZE是建立映射的区间长度，pa是要被映射的首地址，最后是访问权限
      }  
      break;
    default:
      sprint("unknown page fault.\n");
      break;
  }
}

//
// implements round-robin scheduling. added @lab3_3
//
void rrsched() {
  // TODO (lab3_3): implements round-robin scheduling.
  // hint: increase the tick_count member of current process by one, if it is bigger than
  // TIME_SLICE_LEN (means it has consumed its time slice), change its status into READY,
  // place it in the rear of ready queue, and finally schedule next process to run.
  //panic( "You need to further implement the timer handling in lab3_3.\n" );
  current->tick_count++; //先将tick_count加1
  if (current->tick_count >= TIME_SLICE_LEN) {  //判断tick_count是否大于等于TIME_SLICE_LEN（2）
    current->tick_count = 0;  //若大于，则说明要进行时间片的轮转，于是将tick_count清0重新计数
    current->status = READY;  //将当前进程的状态设置为就绪状态
    insert_to_ready_queue( current );  //加入就绪队列
    schedule();  //进程调度函数
  }
}
//
// kernel/smode_trap.S will pass control to smode_trap_handler, when a trap happens
// in S-mode.
//
void smode_trap_handler(void) {
  // make sure we are in User mode before entering the trap handling.
  // we will consider other previous case in lab1_3 (interrupt).
  if ((read_csr(sstatus) & SSTATUS_SPP) != 0) panic("usertrap: not from user mode");

  assert(current);
  // save user process counter.
  current->trapframe->epc = read_csr(sepc);

  // if the cause of trap is syscall from user application.
  // read_csr() and CAUSE_USER_ECALL are macros defined in kernel/riscv.h
  uint64 cause = read_csr(scause);

  // use switch-case instead of if-else, as there are many cases since lab2_3.
  switch (cause) {
    case CAUSE_USER_ECALL:
      handle_syscall(current->trapframe);
      break;
    case CAUSE_MTIMER_S_TRAP:
      handle_mtimer_trap();
      // invoke round-robin scheduler. added @lab3_3
      rrsched();
      break;
    case CAUSE_STORE_PAGE_FAULT:
    case CAUSE_LOAD_PAGE_FAULT:
      // the address of missing page is stored in stval
      // call handle_user_page_fault to process page faults
      handle_user_page_fault(cause, read_csr(sepc), read_csr(stval));
      break;
    default:
      sprint("smode_trap_handler(): unexpected scause %p\n", read_csr(scause));
      sprint("            sepc=%p stval=%p\n", read_csr(sepc), read_csr(stval));
      panic( "unexpected exception happened.\n" );
      break;
  }

  // continue (come back to) the execution of current process.
  switch_to(current);
}
