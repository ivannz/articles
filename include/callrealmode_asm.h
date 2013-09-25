
#ifndef _CALLREALMODE_ASM_H
#define _CALLREALMODE_ASM_H

#include "types.h"

#define CALLREALMODE_OFFSET 0x5000 // адрес, по которому копируется 16битный код

// типы диапазонов физической памяти
// диапазоны с типом 0x1 доступны для использования
#define SYSMEMMAP_TYPE_AVAILABLE    0x1
#define SYSMEMMAP_TYPE_RESERVED     0x2
#define SYSMEMMAP_TYPE_ACPI_RECLAIM 0x3
#define SYSMEMMAP_TYPE_ACPI_NVS     0x4

enum callrealmode_Func 
{
    CALLREALMODE_FUNC_GETSYSMEMMAP = 0
};

struct callrealmode_GetSysMemMap
{
    u32 num;
    u32 next_num;
    u64 base;
    u64 len;
    u32 type;
}  __attribute__ ((packed)); // структура выравнена по 1 байту

// эта структура передается функции 
// callrealmode_Call и возвращается ей.
struct callrealmode_Data
{
    enum callrealmode_Func func : 16;
    union 
    {
        // здесь удобно добавлять новые типы данных 
        // для новых типов вызовов.
        struct callrealmode_GetSysMemMap getsysmemmap;
    };
} __attribute__ ((packed)); // структура выравнена по 1 байту

// метки из файла callrealmode_asm.s, обрамляющие 
// код на asm. Используются для копирования.
extern char callrealmode_start;
extern char callrealmode_end;

#endif