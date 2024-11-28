        lw      0       1       data1   
        beq     1       2       jump1 
        lw      0       2       data2  
        add     2       2       3       
jump1   add     1       1       1
        halt
data1   .fill   2
data2   .fill   3