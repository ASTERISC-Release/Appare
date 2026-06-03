.section .text

.align 0x1000
.global ud2_function
ud2_function:
    ud2
    retq
ud2_function_end:

.space 0x1000 - (ud2_function_end - ud2_function)