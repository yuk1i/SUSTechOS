#!/usr/bin/perl -w

# Generate usys.S, the stubs for syscalls.

print "# generated by usys.pl - do not edit\n";

print "#include \"../../os/syscall_ids.h\"\n";

sub entry {
    my $name = shift;
    print ".global $name\n";
    print "${name}:\n";
    print " li a7, SYS_${name}\n";
    print " ecall\n";
    print " ret\n";
}
	
entry("fork");
entry("exec");
entry("exit");
entry("wait");
entry("kill");
entry("getpid");
entry("getppid");
entry("sleep");
entry("yield");
entry("sbrk");
entry("mmap");
entry("read");
entry("write");
entry("pipe");
entry("close");
entry("gettimeofday");
entry("ktest");

