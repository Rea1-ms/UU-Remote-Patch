EXTERN uu_version_exports:QWORD
.code
forward MACRO n
proxy&n PROC
    jmp QWORD PTR [uu_version_exports + n * 8]
proxy&n ENDP
ENDM
forward 0
forward 1
forward 2
forward 3
forward 4
forward 5
forward 6
forward 7
forward 8
forward 9
forward 10
forward 11
forward 12
forward 13
forward 14
forward 15
forward 16
END
