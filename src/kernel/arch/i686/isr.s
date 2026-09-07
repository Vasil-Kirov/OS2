

%macro isr_err_stub 1
isr_stub_%+%1:
	push dword %1
    jmp isr_common
    iret 
%endmacro

%macro isr_no_err_stub 1
isr_stub_%+%1:
    push dword 0
	push dword %1
    jmp isr_common
    iret
%endmacro

extern interrupt_handler

isr_common:
    pusha
    push ds
	push es
    mov ax, 2 << 3 ; KERNEL_DATA_SEGMENT
    mov ds, ax
    mov es, ax

    push esp               ; -> InterruptFrame *
    call interrupt_handler
    mov esp, eax

    pop es
    pop ds
    popa
    add esp, 8             ; vector + error code
    iret

%assign i 0
%rep 256
  %if i == 8 || (i >= 10 && i <= 14) || i == 17 || i == 21 || i == 29 || i == 30
    isr_err_stub i
  %else
    isr_no_err_stub i
  %endif
  %assign i i+1
%endrep

global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dd isr_stub_%+i
%assign i i+1
%endrep

