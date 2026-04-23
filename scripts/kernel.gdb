# GDB helper script for IR0 kernel debugging via QEMU.
# Usage: gdb -x scripts/kernel.gdb

# Connect to QEMU GDB stub
target remote :1234
symbol-file kernel-x64.bin

# Useful default breakpoints
break kmain
break kernel_panic

# Pretty-print settings
set print pretty on
set print array on
set pagination off

# Custom commands

define lsproc
    set $p = process_list
    printf "PID\tSTATE\tNAME\n"
    while $p
        printf "%d\t%d\t(process@%p)\n", $p->task.pid, $p->state, $p
        set $p = $p->next
    end
end
document lsproc
List all processes in the process_list linked list.
end

define lsmod
    set $d = registered_drivers
    printf "NAME\t\tLANG\tSTATE\n"
    while $d
        printf "%s\t\t%d\t%d\n", $d->info.name, $d->info.language, $d->state
        set $d = $d->next
    end
end
document lsmod
List all registered ir0 drivers.
end

define btall
    set $p = process_list
    while $p
        printf "--- PID %d ---\n", $p->task.pid
        if $p->task.rsp
            set $saved_rsp = $rsp
            set $saved_rbp = $rbp
            set $saved_rip = $rip
            set $rsp = $p->task.rsp
            set $rbp = $p->task.rbp
            set $rip = $p->task.rip
            bt
            set $rsp = $saved_rsp
            set $rbp = $saved_rbp
            set $rip = $saved_rip
        end
        set $p = $p->next
    end
end
document btall
Print backtrace for every process.
end
