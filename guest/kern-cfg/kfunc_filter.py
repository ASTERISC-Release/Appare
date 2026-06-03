def should_filter_function(name):
    import re
    # syscall_ent / interrupts
    hard_filter_set = ["syscall_exit_work", "raw_notifier_call_chain", "tick_sched_handle", "update_vsyscall", "update_wall_time",
                       "tick_do_update_jiffies64", "timekeeping_advance", "trigger_load_balance",
                       "timekeeping_update", "update_fast_timekeeper", "ntp_get_next_leap", "ntp_tick_length",
                       "account_system_time", "account_system_index_time", "__acct_update_integrals",
                       "__accumulate_pelt_segments", "__update_load_avg_cfs_rq", "__update_load_avg_se",
                       "calc_global_load", "cpuacct_charge", "cpuacct_account_field", "__queue_work", "kick_process",
                        "cgroup_rstat_updated", "update_curr", "update_cfs_group", "task_work_add", "try_to_wake_up",
                        "__smp_call_single_queue"]
    patterns = [
            re.compile("idle"),
            re.compile("irq"),
            re.compile("lock"),
            re.compile("mutex"),
            re.compile("rcu"),
            re.compile("kcompactd"),
            re.compile("ktime"),
            re.compile("timer"),
            re.compile("tick"),
            re.compile("apic"),
            re.compile("preempt"),
            re.compile(r"account_.*time"),
            re.compile("cputime"),
            re.compile(r"acct_.*_.*time"),
            re.compile(r".*_.*ipi"),
            re.compile(r".*_.*IPI"),
            re.compile(r".*_.*IPI_.*"),
            re.compile(r".*_.*ipi_.*"),
            re.compile(r"cpuacct_.*"),
            re.compile(r"update.*_.*time.*"),
            re.compile("audit"), # auditd
        ]
    
    if name in hard_filter_set:
        return True

    if any(p.search(name) for p in patterns):
        return True
    
    return False