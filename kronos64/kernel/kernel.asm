; KronOS64 - Combined kernel binary
; Includes entry point and main code
[bits 16]
section .text

%include "entry.asm"
%include "main.asm"
%include "fs.asm"
