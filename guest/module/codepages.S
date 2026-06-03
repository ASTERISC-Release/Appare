.section .text

.global g_variable

.align 0x1000
.global code_page_1
code_page_1:
    movl  $100, g_variable
    retq

.align 0x1000
.global code_page_2
code_page_2:
    movl  $200, g_variable
    retq