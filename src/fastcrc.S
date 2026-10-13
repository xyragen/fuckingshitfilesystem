	.text
	.p2align 4
	.globl fsfs_crc32c_asm
	.type fsfs_crc32c_asm, @function
fsfs_crc32c_asm:
	mov $0xFFFFFFFF, %eax
	test %rsi, %rsi
	jz .Lfin
	cmp $8, %rsi
	jb .Lbyte
	mov $8, %r8
.Lqword:
	mov (%rdi), %rdx
	crc32 %rdx, %rax
	add %r8, %rdi
	sub %r8, %rsi
	cmp %r8, %rsi
	jae .Lqword
.Lbyte:
	test %rsi, %rsi
	jz .Lfin
.Lbloop:
	movzbl (%rdi), %ecx
	crc32 %ecx, %eax
	inc %rdi
	dec %rsi
	jnz .Lbloop
.Lfin:
	xor $0xFFFFFFFF, %eax
	ret
	.size fsfs_crc32c_asm, .-fsfs_crc32c_asm
	.section .note.GNU-stack,"",@progbits
