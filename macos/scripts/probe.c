// Prints resource counters for a pid. Same numbers Activity Monitor uses; no root needed for own processes.
#include <libproc.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    struct rusage_info_v4 r;
    if (proc_pid_rusage(atoi(argv[1]), RUSAGE_INFO_V4, (rusage_info_t *)&r) != 0) return 1;
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    unsigned long long cpu_ns = (r.ri_user_time + r.ri_system_time) * tb.numer / tb.denom;
    printf("%llu %llu %llu %llu\n", r.ri_phys_footprint, cpu_ns, r.ri_pkg_idle_wkups, r.ri_interrupt_wkups);
    return 0;
}
