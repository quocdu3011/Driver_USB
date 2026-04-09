savedcmd_secure_aes.o := ld -m elf_x86_64 -z noexecstack --no-warn-rwx-segments   -r -o secure_aes.o @secure_aes.mod 
