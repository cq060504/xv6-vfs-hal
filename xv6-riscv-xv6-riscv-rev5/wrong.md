- 1.python3 test-xv6.py orphan
riscv64-linux-gnu-ld: warning: kernel/kernel has a LOAD segment with RWX permissions
riscv64-linux-gnu-objdump -S kernel/kernel > kernel/kernel.asm
riscv64-linux-gnu-objdump -t kernel/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > kernel/kernel.sym
rm: cannot remove 'fs.img': No such file or directory
Command failed with exit code 1
Traceback (most recent call last):
  File "/xv6/test-xv6.py", line 216, in <module>
    main()
  File "/xv6/test-xv6.py", line 212, in main
    f()
  File "/xv6/test-xv6.py", line 180, in test_dorphan
    dorphan()
  File "/xv6/test-xv6.py", line 149, in dorphan
    q.match('wait')
  File "/xv6/test-xv6.py", line 94, in match
    self.error()
  File "/xv6/test-xv6.py", line 81, in error
    print("FAIL: match failed", regexps)
                                ^^^^^^^
NameError: name 'regexps' is not defined
root@c69c3687c4a1:/xv6# 

- 2.ps 
exec ps failed
- 3.python3 test-xv6.py all
riscv64-linux-gnu-ld: warning: kernel/kernel has a LOAD segment with RWX permissions
riscv64-linux-gnu-objdump -S kernel/kernel > kernel/kernel.asm
riscv64-linux-gnu-objdump -t kernel/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > kernel/kernel.sym
rm: cannot remove 'fs.img': No such file or directory

- 4.usertests starting
NO TESTS EXECUTED
