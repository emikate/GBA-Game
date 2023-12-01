@ assembly1.s


.global assembly1
assembly1: 
 
    sub r3, r0, r0
    cmp r0, #17
    bge .yes
    add r1, r0, r3
    cmp r1, #17
    blt .no
.yes:
    mov r0, #1
    mov pc, lr
.no:
    mov r0, #0
    mov pc, lr  
 
