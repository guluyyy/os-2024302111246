.section .rodata
 .global _uprog_table
_uprog_table:
 .quad _uprog_sh_start
 .quad _uprog_sh_end
 .asciz "sh"
 .balign 8
 .global _uprog_sh_start
_uprog_sh_start:
 .incbin "user-flat/sh.bin"
 .global _uprog_sh_end
_uprog_sh_end:
 .balign 8
 .quad _uprog_hi_start
 .quad _uprog_hi_end
 .asciz "hi"
 .balign 8
 .global _uprog_hi_start
_uprog_hi_start:
 .incbin "user-flat/hi.bin"
 .global _uprog_hi_end
_uprog_hi_end:
 .balign 8
 .quad _uprog_spin_start
 .quad _uprog_spin_end
 .asciz "spin"
 .balign 8
 .global _uprog_spin_start
_uprog_spin_start:
 .incbin "user-flat/spin.bin"
 .global _uprog_spin_end
_uprog_spin_end:
 .balign 8
 .quad _uprog_badecall_start
 .quad _uprog_badecall_end
 .asciz "badecall"
 .balign 8
 .global _uprog_badecall_start
_uprog_badecall_start:
 .incbin "user-flat/badecall.bin"
 .global _uprog_badecall_end
_uprog_badecall_end:
 .balign 8
 .quad _uprog_bufstorm_start
 .quad _uprog_bufstorm_end
 .asciz "bufstorm"
 .balign 8
 .global _uprog_bufstorm_start
_uprog_bufstorm_start:
 .incbin "user-flat/bufstorm.bin"
 .global _uprog_bufstorm_end
_uprog_bufstorm_end:
 .balign 8
 .quad 0
 .quad 0
