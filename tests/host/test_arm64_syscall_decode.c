/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"

#include <stdbool.h>
#include <ir0/syscall_id.h>

void test_arm64_syscall_decode_contract(void)
{
	unsigned int decoded = 0;
	unsigned int number;
	bool seen[IR0_SYSCALL_LINUX_COUNT] = { false };

	TEST_BEGIN("arm64 syscall ABI decoder contract");
	ASSERT(syscall_decode_number(43) == IR0_SYSCALL_STATFS);
	ASSERT(syscall_decode_number(44) == IR0_SYSCALL_FSTATFS);
	ASSERT(syscall_decode_number(62) == IR0_SYSCALL_LSEEK);
	ASSERT(syscall_decode_number(63) == IR0_SYSCALL_READ);
	ASSERT(syscall_decode_number(64) == IR0_SYSCALL_WRITE);
	ASSERT(syscall_decode_number(73) == IR0_SYSCALL_PPOLL);
	ASSERT(syscall_decode_number(98) == IR0_SYSCALL_FUTEX);
	ASSERT(syscall_decode_number(124) == IR0_SYSCALL_SCHED_YIELD);
	ASSERT(syscall_decode_number(129) == IR0_SYSCALL_KILL);
	ASSERT(syscall_decode_number(132) == IR0_SYSCALL_SIGALTSTACK);
	ASSERT(syscall_decode_number(165) == IR0_SYSCALL_GETRUSAGE);
	ASSERT(syscall_decode_number(179) == IR0_SYSCALL_SYSINFO);
	ASSERT(syscall_decode_number(233) == IR0_SYSCALL_MADVISE);
	ASSERT(syscall_decode_number(78) == IR0_SYSCALL_READLINKAT);
	ASSERT(syscall_decode_number(93) == IR0_SYSCALL_EXIT);
	ASSERT(syscall_decode_number(172) == IR0_SYSCALL_GETPID);
	ASSERT(syscall_decode_number(220) == IR0_SYSCALL_CLONE);
	ASSERT(syscall_decode_number(139) == IR0_SYSCALL_RT_SIGRETURN);
	ASSERT(syscall_decode_number(222) == IR0_SYSCALL_MMAP);
	ASSERT(syscall_decode_number(293) == IR0_SYSCALL_RSEQ);
	ASSERT(syscall_decode_number(0) == IR0_SYSCALL_UNKNOWN);
	ASSERT(syscall_decode_number(UINT64_MAX) == IR0_SYSCALL_UNKNOWN);
	for (number = 0; number <= 471; number++)
	{
		enum ir0_syscall_id id = syscall_decode_number(number);

		if (id == IR0_SYSCALL_UNKNOWN)
			continue;
		ASSERT(id > IR0_SYSCALL_UNKNOWN && id < IR0_SYSCALL_LINUX_COUNT);
		ASSERT(!seen[id]);
		seen[id] = true;
		decoded++;
	}
	/* Linux asm-generic calls backed by existing IR0 common handlers. */
	ASSERT(syscall_decode_number(95) == IR0_SYSCALL_WAITID);
	ASSERT(syscall_decode_number(37) == IR0_SYSCALL_LINKAT);
	ASSERT(syscall_decode_number(112) == IR0_SYSCALL_CLOCK_SETTIME);
	ASSERT(syscall_decode_number(33) == IR0_SYSCALL_MKNODAT);
	ASSERT(syscall_decode_number(20) == IR0_SYSCALL_EPOLL_CREATE1);
	ASSERT(syscall_decode_number(21) == IR0_SYSCALL_EPOLL_CTL);
	ASSERT(syscall_decode_number(22) == IR0_SYSCALL_EPOLL_PWAIT);
	ASSERT(syscall_decode_number(85) == IR0_SYSCALL_TIMERFD_CREATE);
	ASSERT(syscall_decode_number(86) == IR0_SYSCALL_TIMERFD_SETTIME);
	ASSERT(syscall_decode_number(87) == IR0_SYSCALL_TIMERFD_GETTIME);
	ASSERT(syscall_decode_number(199) == IR0_SYSCALL_SOCKETPAIR);
	ASSERT(syscall_decode_number(206) == IR0_SYSCALL_SENDTO);
	ASSERT(syscall_decode_number(207) == IR0_SYSCALL_RECVFROM);
	ASSERT(syscall_decode_number(279) == IR0_SYSCALL_MEMFD_CREATE);
	ASSERT(syscall_decode_number(47) == IR0_SYSCALL_FALLOCATE);
	ASSERT(syscall_decode_number(67) == IR0_SYSCALL_PREAD64);
	ASSERT(syscall_decode_number(68) == IR0_SYSCALL_PWRITE64);
	ASSERT(syscall_decode_number(71) == IR0_SYSCALL_SENDFILE);
	ASSERT(syscall_decode_number(227) == IR0_SYSCALL_MSYNC);
	ASSERT(syscall_decode_number(291) == IR0_SYSCALL_STATX);
	ASSERT(syscall_decode_number(69) == IR0_SYSCALL_PREADV);
	ASSERT(syscall_decode_number(70) == IR0_SYSCALL_PWRITEV);
	ASSERT(syscall_decode_number(276) == IR0_SYSCALL_RENAMEAT2);
	ASSERT(syscall_decode_number(76) == IR0_SYSCALL_SPLICE);
	ASSERT(syscall_decode_number(77) == IR0_SYSCALL_TEE);
	ASSERT(syscall_decode_number(436) == IR0_SYSCALL_CLOSE_RANGE);
	ASSERT(syscall_decode_number(285) == IR0_SYSCALL_COPY_FILE_RANGE);
	ASSERT(syscall_decode_number(223) == IR0_SYSCALL_FADVISE64);
	ASSERT(syscall_decode_number(75) == IR0_SYSCALL_VMSPLICE);
	ASSERT(syscall_decode_number(84) == IR0_SYSCALL_SYNC_FILE_RANGE);
	ASSERT(syscall_decode_number(269) == IR0_SYSCALL_SENDMMSG);
	ASSERT(syscall_decode_number(243) == IR0_SYSCALL_RECVMMSG);
	ASSERT(syscall_decode_number(168) == IR0_SYSCALL_GETCPU);
	ASSERT(syscall_decode_number(122) == IR0_SYSCALL_SCHED_SETAFFINITY);
	ASSERT(syscall_decode_number(123) == IR0_SYSCALL_SCHED_GETAFFINITY);
	ASSERT(syscall_decode_number(118) == IR0_SYSCALL_SCHED_SETPARAM);
	ASSERT(syscall_decode_number(120) == IR0_SYSCALL_SCHED_GETSCHEDULER);
	ASSERT(syscall_decode_number(270) == IR0_SYSCALL_PROCESS_VM_READV);
	ASSERT(syscall_decode_number(271) == IR0_SYSCALL_PROCESS_VM_WRITEV);
	ASSERT(syscall_decode_number(232) == IR0_SYSCALL_MINCORE);
	ASSERT(syscall_decode_number(228) == IR0_SYSCALL_MLOCK);
	ASSERT(syscall_decode_number(284) == IR0_SYSCALL_MLOCK2);
	ASSERT(syscall_decode_number(213) == IR0_SYSCALL_READAHEAD);
	ASSERT(syscall_decode_number(267) == IR0_SYSCALL_SYNCFS);
	ASSERT(syscall_decode_number(283) == IR0_SYSCALL_MEMBARRIER);
	ASSERT(syscall_decode_number(90) == IR0_SYSCALL_CAPGET);
	ASSERT(syscall_decode_number(153) == IR0_SYSCALL_TIMES);
	ASSERT(syscall_decode_number(161) == IR0_SYSCALL_SETHOSTNAME);
	ASSERT(syscall_decode_number(125) == IR0_SYSCALL_SCHED_GET_PRIORITY_MAX);
	ASSERT(syscall_decode_number(272) == IR0_SYSCALL_KCMP);
	ASSERT(syscall_decode_number(286) == IR0_SYSCALL_PREADV2);
	ASSERT(syscall_decode_number(434) == IR0_SYSCALL_PIDFD_OPEN);
	ASSERT(syscall_decode_number(424) == IR0_SYSCALL_PIDFD_SEND_SIGNAL);
	ASSERT(syscall_decode_number(438) == IR0_SYSCALL_PIDFD_GETFD);
	ASSERT(syscall_decode_number(437) == IR0_SYSCALL_OPENAT2);
	ASSERT(syscall_decode_number(439) == IR0_SYSCALL_FACCESSAT2);
	ASSERT(syscall_decode_number(5) == IR0_SYSCALL_SETXATTR);
	ASSERT(syscall_decode_number(8) == IR0_SYSCALL_GETXATTR);
	ASSERT(syscall_decode_number(151) == IR0_SYSCALL_SETFSUID);
	ASSERT(syscall_decode_number(170) == IR0_SYSCALL_SETTIMEOFDAY);
	ASSERT(syscall_decode_number(264) == IR0_SYSCALL_NAME_TO_HANDLE_AT);
	ASSERT(syscall_decode_number(266) == IR0_SYSCALL_CLOCK_ADJTIME);
	ASSERT(syscall_decode_number(435) == IR0_SYSCALL_CLONE3);
	ASSERT(syscall_decode_number(440) == IR0_SYSCALL_PROCESS_MADVISE);
	ASSERT(syscall_decode_number(441) == IR0_SYSCALL_EPOLL_PWAIT2);
	ASSERT(syscall_decode_number(452) == IR0_SYSCALL_FCHMODAT2);
	ASSERT(decoded == 219);
	TEST_END();
}
