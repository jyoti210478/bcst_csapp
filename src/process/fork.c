/* BCST - Introduction to Computer Systems
 * Author:      yangminz@outlook.com
 * Github:      https://github.com/yangminz/bcst_csapp
 * Bilibili:    https://space.bilibili.com/4564101
 * Zhihu:       https://www.zhihu.com/people/zhao-yang-min
 * This project (code repository and videos) is exclusively owned by yangminz 
 * and shall not be used for commercial and profitting purpose 
 * without yangminz's permission.
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "headers/cpu.h"
#include "headers/memory.h"
#include "headers/interrupt.h"
#include "headers/syscall.h"
#include "headers/process.h"

static uint64_t fork_naive_copy();
static uint64_t fork_cow();

uint64_t syscall_fork()
{
    return fork_naive_copy();
}

// Implementation

static uint64_t get_newpid()
{
    // find the max pid of current processes
    pcb_t *p = get_current_pcb();
    pcb_t *x = p;
    uint64_t max_pid = p->pid;
    while (x->next != p)
    {
        max_pid = max_pid < x->pid ? x->pid : max_pid;
        x = x->next;
    }
    return max_pid + 1;
}

// Update rax register in user frame
void update_userframe_returnvalue(pcb_t *p, uint64_t retval)
{
    p->context.regs.rax = retval;
    uint64_t uf_vaddr = (uint64_t)p->kstack + KERNEL_STACK_SIZE 
                        - sizeof(trapframe_t) - sizeof(userframe_t);
    userframe_t *uf = (userframe_t *)uf_vaddr;
    uf->regs.rax = retval;
}

pte123_t *copy_pagetable_dfs(pte123_t *src, int level)
{
    // allocate one page for destination
    pte123_t *dst = KERNEL_malloc(sizeof(pte123_t) * PAGE_TABLE_ENTRY_NUM);

    // copy the current page table to destination
    memcpy(dst, src, sizeof(pte123_t) * PAGE_TABLE_ENTRY_NUM);

    if (level == 4)
    {
        return dst;
    }

    // check source
    for (int i = 0; i < PAGE_TABLE_ENTRY_NUM; ++ i)
    {
        if (src[i].present == 1)
        {
            pte123_t *src_next = (pte123_t *)(uint64_t)(src[i].paddr);
            dst[i].paddr = (uint64_t)copy_pagetable_dfs(src_next, level + 1);
        }
    }

    return dst;
}

#define PAGE_TABLE_ENTRY_PADDR_MASK (~((0xffffffffffffffff >> 12) << 12))

static int compare_pagetables_recursive(pte123_t *p, pte123_t *q, int level)
{
    for (int i = 0; i < PAGE_TABLE_ENTRY_NUM; ++ i)
    {
        if ((p[i].pte_value & PAGE_TABLE_ENTRY_PADDR_MASK) != 
            (q[i].pte_value & PAGE_TABLE_ENTRY_PADDR_MASK))
        {
            // this entry not match
            assert(0);
        }

        if (level < 4 && p[i].present == 1 &&
            compare_pagetables_recursive(
                (pte123_t *)(uint64_t)p[i].paddr, 
                (pte123_t *)(uint64_t)q[i].paddr, level + 1) == 0)
        {
            // sub-pages not match
            assert(0);
        }
    }

    return 1;
}

static int compare_pagetables(pte123_t *p, pte123_t *q)
{
    return compare_pagetables_recursive(p, q, 1);
}

static uint64_t fork_naive_copy()
{
    // Note that all system calls are actually compiled to binaries
    // Operating system itself is a binary, a software, executed by CPU
    // Here we use C language to write the fork
    // But actually, we should write instructions:
    // mov_handler
    // add_handler, etc.
    //
    // Now, let's start to write `fork`
    
    // DIRECT COPY
    // find all pages of current process:
    pcb_t *parent_pcb = get_current_pcb();

    // ATTENTION HERE!!!
    // In a realistic OS, you need to allocate kernel pages as well
    // And then copy the data on parent kernel page to child's.
    pcb_t *child_pcb = KERNEL_malloc(sizeof(pcb_t));
    memcpy(child_pcb, parent_pcb, sizeof(pcb_t));

    // update child PID
    child_pcb->pid = get_newpid();

    // COW optimize: create a kernel stack for child process
    // NOTE KERNEL STACK MUST BE ALIGNED
    kstack_t *child_kstack = aligned_alloc(KERNEL_STACK_SIZE, KERNEL_STACK_SIZE);
    memcpy(child_kstack, (kstack_t *)parent_pcb->kstack, sizeof(kstack_t));
    child_pcb->kstack = child_kstack;
    child_kstack->threadinfo.pcb = child_pcb;

    // prepare child process context (especially RSP) for context switching
    // for child process to be switched to
    child_pcb->context.regs.rsp = (uint64_t)child_kstack + KERNEL_STACK_SIZE -
                                    sizeof(trapframe_t) - sizeof(userframe_t);

    // add child process PCB to linked list for scheduling
    // it's better the child process is right after parent
    if (parent_pcb->prev == parent_pcb)
    {
        // parent is the only PCB in the linked list
        assert(parent_pcb->next == parent_pcb);
        parent_pcb->prev = child_pcb;
    }
    parent_pcb->next = child_pcb;
    child_pcb->prev = parent_pcb;

    // Fork's secret:
    // call once, return twice
    update_userframe_returnvalue(parent_pcb, child_pcb->pid);
    update_userframe_returnvalue(child_pcb, 0);

    // copy the entire page table of parent
    child_pcb->mm.pgd = copy_pagetable_dfs(parent_pcb->mm.pgd, 1);
    assert(compare_pagetables(child_pcb->mm.pgd, parent_pcb->mm.pgd) == 1);

    // TODO: find physical frames to copy the pages of parent

    // All copy works are done here
    return 0;
}

static uint64_t fork_cow()
{
    return 0;
}