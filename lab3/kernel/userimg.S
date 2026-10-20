.section .rodata
 .global _uprog_table
_uprog_table:
 .quad _uprog_sh_start
 .quad _uprog_sh_end
 .quad 0x0000000000001000
 .quad 0x0000000000001040
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
 .quad 0x0000000000001000
 .quad 0x0000000000001000
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
 .quad 0x0000000000001000
 .quad 0x0000000000001000
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
 .quad 0x0000000000001000
 .quad 0x0000000000001000
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
 .quad 0x0000000000001000
 .quad 0x0000000000001000
 .asciz "bufstorm"
 .balign 8
 .global _uprog_bufstorm_start
_uprog_bufstorm_start:
 .incbin "user-flat/bufstorm.bin"
 .global _uprog_bufstorm_end
_uprog_bufstorm_end:
 .balign 8
 .quad _uprog_badaccess_start
 .quad _uprog_badaccess_end
 .quad 0x0000000000001000
 .quad 0x0000000000001004
 .asciz "badaccess"
 .balign 8
 .global _uprog_badaccess_start
_uprog_badaccess_start:
 .incbin "user-flat/badaccess.bin"
 .global _uprog_badaccess_end
_uprog_badaccess_end:
 .balign 8
 .quad _uprog_copycross_start
 .quad _uprog_copycross_end
 .quad 0x0000000000001000
 .quad 0x0000000000001000
 .asciz "copycross"
 .balign 8
 .global _uprog_copycross_start
_uprog_copycross_start:
 .incbin "user-flat/copycross.bin"
 .global _uprog_copycross_end
_uprog_copycross_end:
 .balign 8
 .quad _uprog_memcap_start
 .quad _uprog_memcap_end
 .quad 0x0000000000001000
 .quad 0x0000000000001000
 .asciz "memcap"
 .balign 8
 .global _uprog_memcap_start
_uprog_memcap_start:
 .incbin "user-flat/memcap.bin"
 .global _uprog_memcap_end
_uprog_memcap_end:
 .balign 8
 .quad 0
 .quad 0
