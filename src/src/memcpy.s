    .global _FastCOPY
_FastCOPY:
    move.l #1100, d0
1:
    movem.l (a0)+,d1-d7/a2
    movem.l (a0)+,a3-a6
    movem.l d1-d7/a2-a6,(a1)
    add.l      #48,a1
    dbra d0,1b
    rts