; ---------------------------------------------------------------------------
; void rc_draw_span(const SpanArgs *args)
;
; Textured horizontal span for floors and ceilings, 16.16 fixed-point u/v
; stepping, column-major texture, light table lookup:
;
;   for i in 0 .. count-1:
;       u = (ufrac >> 16) & umask
;       v = (vfrac >> 16) & vmask
;       dest[i] = colormap[source[(u << vshift) | v]]
;       ufrac += ustep
;       vfrac += vstep
;
;   rdi  args    -> SpanArgs (include/AsmRoutines.h)
;
; Register use in the loop:
;   r8   dest        r9   dest end    rsi  source     rdx  colormap
;   r10d ufrac       r11d vfrac       ebx  ustep      ebp  vstep
;   r12d umask       r13d vmask       cl   vshift
;   eax  u / texel   edi  v
;
; Clobbers: rax, rcx, rdx, rsi, rdi, r8-r11
; Saves and restores rbx, rbp, r12, r13 (callee-saved). Leaf function.
; Reference: rc_draw_span_ref (src/Reference.cpp)
; ---------------------------------------------------------------------------

default rel

; Mirror of struct SpanArgs; offsets are pinned by static_asserts in C++.
struc SpanArgs
	.dest:		resq 1	; 0
	.source:	resq 1	; 8
	.colormap:	resq 1	; 16
	.count:		resd 1	; 24
	.ufrac:		resd 1	; 28
	.vfrac:		resd 1	; 32
	.ustep:		resd 1	; 36
	.vstep:		resd 1	; 40
	.umask:		resd 1	; 44
	.vmask:		resd 1	; 48
	.vshift:	resd 1	; 52
endstruc

section .text
global rc_draw_span

rc_draw_span:
	movsxd	rax, dword [rdi + SpanArgs.count]
	test	rax, rax
	jle	.empty			; count <= 0: nothing to draw

	push	rbx
	push	rbp
	push	r12
	push	r13

	mov	r8, [rdi + SpanArgs.dest]
	lea	r9, [r8 + rax]		; one past the last pixel
	mov	rsi, [rdi + SpanArgs.source]
	mov	rdx, [rdi + SpanArgs.colormap]
	mov	r10d, [rdi + SpanArgs.ufrac]
	mov	r11d, [rdi + SpanArgs.vfrac]
	mov	ebx, [rdi + SpanArgs.ustep]
	mov	ebp, [rdi + SpanArgs.vstep]
	mov	r12d, [rdi + SpanArgs.umask]
	mov	r13d, [rdi + SpanArgs.vmask]
	mov	ecx, [rdi + SpanArgs.vshift]	; shift count must live in cl

.loop:
	mov	eax, r10d
	shr	eax, 16
	and	eax, r12d		; u
	shl	eax, cl			; u * height
	mov	edi, r11d
	shr	edi, 16
	and	edi, r13d		; v
	or	eax, edi		; column-major texel index
	movzx	eax, byte [rsi + rax]	; texel
	movzx	eax, byte [rdx + rax]	; shaded through the light table
	mov	[r8], al
	inc	r8
	add	r10d, ebx		; ufrac += ustep (wraps mod 2^32)
	add	r11d, ebp		; vfrac += vstep
	cmp	r8, r9
	jb	.loop

	pop	r13
	pop	r12
	pop	rbp
	pop	rbx
.empty:
	ret

section .note.GNU-stack noalloc noexec nowrite progbits
