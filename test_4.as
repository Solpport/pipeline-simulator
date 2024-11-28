        lw      0       1       data1
        add     1       1       2
        sw      0       2       data2
        beq     1       2       skip
        add     2       2       3
skip    halt
data1   .fill   100
data2   .fill   0