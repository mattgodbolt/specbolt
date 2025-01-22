#pragma once

#include <array>
#include <string_view>

namespace specbolt {

constexpr std::array<std::string_view, 256> Opcodes = {"nop", "ld bc,nnnn", "ld (bc),a", "inc bc", "inc b", "dec b",
    "ld b,nn", "rlca", "ex af,af'", "add hl,bc", "ld a,(bc)", "dec bc", "inc c", "dec c", "ld c,nn", "rrca",
    "djnz offset", "ld de,nnnn", "ld (de),a", "inc de", "inc d", "dec d", "ld d,nn", "rla", "jr offset", "add hl,de",
    "ld a,(de)", "dec de", "inc e", "dec e", "ld e,nn", "rra", "jr nz,offset", "ld hl,nnnn", "ld (nnnn),hl", "inc hl",
    "inc h", "dec h", "ld h,nn", "daa", "jr z,offset", "add hl,hl", "ld hl,(nnnn)", "dec hl", "inc l", "dec l",
    "ld l,nn", "cpl", "jr nc,offset", "ld sp,nnnn", "ld (nnnn),a", "inc sp", "inc (hl)", "dec (hl)", "ld (hl),nn",
    "scf", "jr c,offset", "add hl,sp", "ld a,(nnnn)", "dec sp", "inc a", "dec a", "ld a,nn", "ccf", "ld b,b", "ld b,c",
    "ld b,d", "ld b,e", "ld b,h", "ld b,l", "ld b,(hl)", "ld b,a", "ld c,b", "ld c,c", "ld c,d", "ld c,e", "ld c,h",
    "ld c,l", "ld c,(hl)", "ld c,a", "ld d,b", "ld d,c", "ld d,d", "ld d,e", "ld d,h", "ld d,l", "ld d,(hl)", "ld d,a",
    "ld e,b", "ld e,c", "ld e,d", "ld e,e", "ld e,h", "ld e,l", "ld e,(hl)", "ld e,a", "ld h,b", "ld h,c", "ld h,d",
    "ld h,e", "ld h,h", "ld h,l", "ld h,(hl)", "ld h,a", "ld l,b", "ld l,c", "ld l,d", "ld l,e", "ld l,h", "ld l,l",
    "ld l,(hl)", "ld l,a", "ld (hl),b", "ld (hl),c", "ld (hl),d", "ld (hl),e", "ld (hl),h", "ld (hl),l", "halt",
    "ld (hl),a", "ld a,b", "ld a,c", "ld a,d", "ld a,e", "ld a,h", "ld a,l", "ld a,(hl)", "ld a,a", "add a,b",
    "add a,c", "add a,d", "add a,e", "add a,h", "add a,l", "add a,(hl)", "add a,a", "adc a,b", "adc a,c", "adc a,d",
    "adc a,e", "adc a,h", "adc a,l", "adc a,(hl)", "adc a,a", "sub a,b", "sub a,c", "sub a,d", "sub a,e", "sub a,h",
    "sub a,l", "sub a,(hl)", "sub a,a", "sbc a,b", "sbc a,c", "sbc a,d", "sbc a,e", "sbc a,h", "sbc a,l", "sbc a,(hl)",
    "sbc a,a", "and a,b", "and a,c", "and a,d", "and a,e", "and a,h", "and a,l", "and a,(hl)", "and a,a", "xor a,b",
    "xor a,c", "xor a,d", "xor a,e", "xor a,h", "xor a,l", "xor a,(hl)", "xor a,a", "or a,b", "or a,c", "or a,d",
    "or a,e", "or a,h", "or a,l", "or a,(hl)", "or a,a", "cp b", "cp c", "cp d", "cp e", "cp h", "cp l", "cp (hl)",
    "cp a", "ret nz", "pop bc", "jp nz,nnnn", "jp nnnn", "call nz,nnnn", "push bc", "add a,nn", "rst 00", "ret z",
    "ret", "jp z,nnnn", "shift cb", "call z,nnnn", "call nnnn", "adc a,nn", "rst 8", "ret nc", "pop de", "jp nc,nnnn",
    "out (nn),a", "call nc,nnnn", "push de", "sub nn", "rst 10", "ret c", "exx", "jp c,nnnn", "in a,(nn)",
    "call c,nnnn", "shift dd", "sbc a,nn", "rst 18", "ret po", "pop hl", "jp po,nnnn", "ex (sp),hl", "call po,nnnn",
    "push hl", "and nn", "rst 20", "ret pe", "jp hl", "jp pe,nnnn", "ex de,hl", "call pe,nnnn", "shift ed", "xor a,nn",
    "rst 28", "ret p", "pop af", "jp p,nnnn", "di", "call p,nnnn", "push af", "or nn", "rst 30", "ret m", "ld sp,hl",
    "jp m,nnnn", "ei", "call m,nnnn", "shift fd", "cp nn", "rst 38"};

}
