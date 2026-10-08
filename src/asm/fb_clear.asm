; ---------------------------------------------------------------------------
; void rc_fb_clear(uint8_t *fb, uint32_t count, uint8_t color)
;
; Fill `count` bytes at `fb` with `color`.
;
;   rdi  fb      destination
;   esi  count   number of bytes
;   dl   color   palette index
;
; Clobbers: rax, rcx, rdi
; Reference: rc_fb_clear_ref (src/Reference.cpp)
;
; rep stosb is the fastest fill on any CPU with ERMSB (Ivy Bridge and later,
; Zen) and still correct everywhere else. DF is clear on entry per the ABI.
; ---------------------------------------------------------------------------

default rel
section .text
global rc_fb_clear

rc_fb_clear:
	mov	ecx, esi	; count (zero-extends into rcx)
	movzx	eax, dl		; color
	rep	stosb
	ret

section .note.GNU-stack noalloc noexec nowrite progbits
