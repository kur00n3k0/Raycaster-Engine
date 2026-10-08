; ---------------------------------------------------------------------------
; void rc_draw_sprite_col(const ColumnArgs *args)
;
; Masked vertical span for sprites. Same stepping as rc_draw_column, but
; texels equal to 0xFF (PAL_TRANSPARENT) are skipped:
;
;   for i in 0 .. count-1:
;       t = source[(frac >> 16) & mask]
;       if t != 0xFF: dest[i * pitch] = colormap[t]
;       frac += step
;
;   rdi  args    -> ColumnArgs (include/AsmRoutines.h)
;
; Register use in the loop:
;   r8   dest        rsi  source      rdx  colormap
;   r9   pitch       ecx  count       r10d frac
;   r11d step        edi  mask        eax  scratch
;
; Clobbers: rax, rcx, rdx, rsi, rdi, r8-r11 (all caller-saved, leaf function)
; Reference: rc_draw_sprite_col_ref (src/Reference.cpp)
; ---------------------------------------------------------------------------

default rel

; Mirror of struct ColumnArgs; offsets are pinned by static_asserts in C++.
struc ColumnArgs
	.dest:		resq 1	; 0
	.source:	resq 1	; 8
	.colormap:	resq 1	; 16
	.pitch:		resd 1	; 24
	.count:		resd 1	; 28
	.frac:		resd 1	; 32
	.step:		resd 1	; 36
	.mask:		resd 1	; 40
	.pad:		resd 1	; 44
endstruc

section .text
global rc_draw_sprite_col

rc_draw_sprite_col:
	mov	ecx, [rdi + ColumnArgs.count]
	test	ecx, ecx
	jle	.done			; count <= 0: nothing to draw

	mov	r8, [rdi + ColumnArgs.dest]
	mov	rsi, [rdi + ColumnArgs.source]
	mov	rdx, [rdi + ColumnArgs.colormap]
	movsxd	r9, dword [rdi + ColumnArgs.pitch]
	mov	r10d, [rdi + ColumnArgs.frac]
	mov	r11d, [rdi + ColumnArgs.step]
	mov	edi, [rdi + ColumnArgs.mask]	; args pointer no longer needed

.loop:
	mov	eax, r10d
	shr	eax, 16			; integer texel
	and	eax, edi		; wrap to texture height
	movzx	eax, byte [rsi + rax]	; texel
	cmp	eax, 0xFF
	je	.skip			; transparent: leave the pixel alone
	movzx	eax, byte [rdx + rax]	; shaded through the light table
	mov	[r8], al
.skip:
	add	r8, r9			; next row
	add	r10d, r11d		; frac += step (wraps mod 2^32)
	dec	ecx
	jnz	.loop

.done:
	ret

section .note.GNU-stack noalloc noexec nowrite progbits
