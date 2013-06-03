//// ������� �����������
#include "types.h"
#include "printf.h"
#include "string.h"
#include "hardware.h"

//// ����� ���������, ������ ��� ����� �����
// ������������ ���������� ������������� ����.
// ��� ������� ����� 16, �� ����� ��������� � 256
#define SMP_MAX_CPU_COUNT 16

// ��������� ��������� ������������� ��������� ������� ����������
// ����������� ������ ������� �� ����� ������, �������� ��� ������ ���������
// ��� ������������ ��� �� ������� ������ �������
#define CPU_FREQ 1000UL

//// ���������, ������������ � ������������ ����, ������� ��������� AP
// ���������� �����, �� �������� ������������� ���, ����������� AP, 
// ������� � ������ ����������
#define SMP_AP_STARTUP_CODE     0x6000
// �����, �� �������� ����������� ������ ������� ESP ��� ������� AP
// ��������� ��� ���� ����������� �����������, �� ������� �� ��� ����� ���� ����
// �� ������� �������� ������� ������ ��� ����� ������� �� AP
// ������ �� ����� ������ ������ ���������
#define SMP_AP_STARTUP_ESPP     0x5000

// ������� �������� ��� ����� ��������� ��������. ������������ � ����������
#define _BASE    SMP_AP_STARTUP_CODE
#define _ESP     SMP_AP_STARTUP_ESPP

// ��������� � �����������, ��������������� ��� ������������� AP
// � ��� ��� ��������� ������ ������� ������ ���������� ESP ��� ������� ����������
// �� ������ ���� ���� ��������� ��������� TR_SMP_AP_STARTUP_DATA_ESPP
typedef struct 
{
    u32 ptrESP[SMP_MAX_CPU_COUNT];
} __attribute__ ((packed)) SmpBootCpusApInfo_t;

// ������ ����� ����������... ��� ������� ����� ����������� �� ������ AP
// ���� ����� ����: ������������������� AP, �������� ��� � Protected Mode � �������� �� C ������� SmpApMain

// GCC ��������� ����� ������� ��������� ������������ ������� ����� � C ���
asm (
    // �������� ���������� ��� � ������� ���� ���������
    "    .text                    \n"
    // ��������� ��������� ����� �� 8 ����
    "    .align    8              \n"
    // ��������� ������ trSmpEntry, ������� ����� ����� 
    // �������������� � C ��� ����������� ��������� �� ������ ����
    // .global ������ ������ ������� ��� C ����
    "    .globl SmpEntry        \n"
    // ��������������� ����� trSmpEntry
    "SmpEntry  :                \n"
    // �������������� ����� trSmpEntry16, ���������� ������ 16-�� ������� ����
    // .code16 ������� �����������, ��� ����� ���� 16-�� ������ ���
    // ������, ��� AP �������� � Real Mode (16 ���)
    "SmpEntry16: .code16        \n"
    // ���������� ����� ��� ����������� ������ C �������
    "    cld                      \n"
    // ��������� ����������
    "    cli                      \n"
    // ������ �������� CR0 � EAX
    "    mov  %cr0,%eax           \n"
    // ������������� �����, ����������� ��� �������� � Protected Mode:
    //  PE (Protected Mode Enable), TS (Task Switch)
    "    or	  $0x00000009,%eax    \n" // PE|TS
    // ���������� ���������� �������� ������� � CR0
    "    mov  %eax,%cr0           \n"
    // �� � Protected Mode! �� ��� ���� ��� ��������� GDT 
    // (�������� ��� �� �������������)
    // � ���������� � ������� ��������� ����� ����� � ����

    // ���������� � EAX �����, �� �������� ������������� ���������� GDT
    // ����� ����������� �� ����� trSmp16_gdt_desr
    // trSmp16_gdt_desr - �������� ������������ ������ ���� �� LD �������
    // � ��� ����� ���������� ���������� �����. 
    // (*) ��������� ��� ����� ���������� �� ������ _BASE, �� ���������� �����
    // ����������� ���: trSmp16_gdt_desr - trSmpEntry + _BASE
    "    movl $(Smp16_gdt_desr - SmpEntry + "S(_BASE)"), %eax \n"
    // ��������� ���������� GDT
    "    lgdt (%eax)                 \n"
    // ������ ������� �� 32-� ������ ��� �� ������, ������� ��� ������ � LD
    // ������� ������� � ��� ���� ��� ����� ����� ������������ ����: 
    //   - ���� �� ������ _BASE (��� ����� �������)
    //   - ������ �� ������, ������� ��������� ���������, ��������� ��� LD �������
    // �� ��������� ���������� (�� �������) AP �������� ��� �� ������ _BASE
    // ����� ������ �� trSmpEntry32 - ����� ����������� ������������ ��� �� 
    // ������������ �������, ������� ��������� LD
    // ����: ������� �� 32-� ������ ���, ������� ���������� ����� �� GDT ��������
    "    ljmpl $0x08, $SmpEntry32  \n"
    
    //// ���������� GDT
    // ������������ �� 8 ���� ���������� ��� ���������� lgdt
    "    .align 8                    \n"
    // ���������� �������� ��� ����: ������ ������� 16 ��� � ����� ������� 32 ����
    "Smp16_gdt_desr:               \n"
    // ������ ����������� <�����> - <������> - 1. -1 ����� �� �������� lgdt
    "    .word Smp16_gdt_end - Smp16_gdt - 1     \n"
    // ����� ����������� ���������� (*)
    "    .long Smp16_gdt - SmpEntry + "S(_BASE)" \n"

    // ������� GDT, �� ������� ��������� GDTR
    // ������������ �� 8 ���� ���������� ��� GDT
    "    .align 8                    \n"
    // ������� �������� 2 ������������ � ������� ����������, ������� ��������� �� �������� GDT
    "Smp16_gdt:                    \n"
    // ������� ���������� ��������
    "    .quad 0x0000000000000000    \n"
    // ������� �������� ����� ����� ���������� � Intel CPU Reference Manual

    // ���������� �������� 32-� ������� ���� ��� Ring-0. �������� �� 0 �� 4G
    // ������� 8 (0x8)
    "    .quad 0x00cf9a000000ffff    \n"
    // ���������� �������� 32-� ������ ������ ��� Ring-0. �������� �� 0 �� 4G
    // ������� 16 (0x10)
    "    .quad 0x00cf92000000ffff    \n"
    "Smp16_gdt_end:                \n"

    // ������ 32-� ������� ����. ���� ��� ��������� ��� �����
    // ������ ��� �������� ������� � ��� ����� ��� ������������������� ���� (EBP/ESP)
    // � �������� �������� � 
    // .code32 - ������� �����������, ��� ����� ��� 32-� ������ ���
    "SmpEntry32: .code32          \n"
    // ���������� 16 (�������� 16-�� ������� �������� ������) � AX
    "    mov  $16,%ax     \n"
    // ������������� ��� ��������� ��������� � 16
    "    mov  %ax,%ds               \n"
    "    mov  %ax,%es               \n"
    "    mov  %ax,%fs               \n"
    "    mov  %ax,%gs               \n"
    "    mov  %ax,%ss               \n"
    // �������� ��� ��������
    "    xor  %eax, %eax            \n"
    "    xor  %ebx, %ebx            \n"
    "    xor  %ecx, %ecx            \n"
    "    xor  %edx, %edx            \n"

    // ��� ����, ����� �������� ��������� �� ESP ���� ����� ���� LAPIC ID
    // ��� ����� ��������, �������� ������� �� ���������� LAPIC
    // ��-������, ����� ����� ������� ����� ��� ���������� LAPIC

    // ������� ����� ������� MSR � ECX
    "    mov  $"S(MSR_IA32_APIC_BASE_MSR)",%ecx      \n"
    // ������ MSR �� ������ � ECX
    "    rdmsr  \n"
    // � EAX ������ ����� ���� LAPIC
    // ����� �������� ������� 12 ���
    "    and  $0xfffff000,%eax         \n"
    // ����� ��������� � ������ 0x20 (����� �������� APIC_LAPIC_ID)
    "    add  $"S(APIC_LAPIC_ID)",%eax \n"
    // ����� � ��� � EAX ������ ����� ����� �����  0xfee00020
    // ������ �������� ����� �������� LAPIC ID � EBX
    "    mov  (%eax),%ebx      \n"
    // ��� ���� LAPIC ID - ��� ������� ����, ������� EBX >> 24
    "    shrl $0x18, %ebx           \n"

    // ����� ������� � EBX ��������� ������������� �������� ���� ����������
    // �������� ������ ���� ����� �����
    // �������� � EDX ����� �������
    "    mov $"S(_ESP)", %edx         \n"
    // ������ ������� � EAX ����� �����
    // �� ����� ������������� �� ������: (EDX + EBX * 4 + 0)
    // ����� ��������� �������� �� ������ ������ ����� ��������������� �����������:
    "    movl 0(%edx, %ebx, 4), %eax  \n"
    // ������������� �������� ESP � EBP, ���������� �� EAX
    "    movl %eax,%esp               \n"
    "    movl %esp,%ebp               \n"

    // �������� ��� ��������
    "    xor  %eax, %eax            \n"
    "    xor  %ebx, %ebx            \n"
    "    xor  %ecx, %ecx            \n"
    "    xor  %edx, %edx            \n"

    // ������ ����� ������� ������� �� C
    "    call SmpApMain             \n"

    // �� ������ ������ ����������� ����, ����� ��������� Main
    "_ap_loop:                        \n"
    "    jmp _ap_loop                 \n"
    "    .globl SmpEntry_END        \n"
    // ����� ����� ����
    "SmpEntry_END:                  \n"
    // ����� ��� �� ���� ������ 32-� ������� ����
    ".text                            \n"
    ".code32                          \n"
    );

// ���������� ������������ ����� ��� ���� �� �
// ��������� ��� ����� - �� ��� ����������� ��� �������, 
// ����� ��� ����� ���� ������������ ��� �����������
extern void SmpEntry(void);
extern void SmpEntry_END(void);

// ������� ���������� ���, ������� ����� �������������� ��� ������������� ����
spinlock_t SmpMainLock;
// ����������, ������� ����� ������� ���������� ��� ���������� CPU 
// � ������ ������� ������ BSP
ulong SmpStartedCpus = 1;

// ���� ���������� ����� ���� ���� ���������� � ���������� �������� ��������
// ���� ���� ����� �������������� ��� ������ ��� ������ ����������
ulong SmpStartedCpusStarted = 0;

// �������, ������ ��������� �������� � �������� LAPIC ID
void SmpStartCpu(u8 id);

// ��������� ����� ���� ������� �� C++, � � ��� C, �� ����� ���������� bool
typedef int bool;
#define true 1
#define false 0

// ������ � ������� �������� � ACPI
// ���� ����: ������� ������ LAPIC ID ��� ���� ���� � �������

// ������� ��������� ���������� ����������, ������� ����� ������� ���������� �������
// ������ �������� LAPIC ID
u8 cpu_ids[SMP_MAX_CPU_COUNT];
// ���������� �������� LAPIC ID
u32 cpu_count = 0;
// ������������ �������� LAPIC ID
u32 cpu_top_id = 0;

// �������� ����������� ����� ��� �������� ������� ACPI - ��� ����������� ��������
int Acpi_SignatureCheck(char* val1, char* val2,int len)
{
  int i;
  for (i = 0; i < len; i++)
    if (val1[i] != val2[i])
      return 0;
  return 1;
}

// ��� ������� ��������� ������� MADT � ��������� ������ cpu_ids
int Acpi_Parse_MADT( struct MADT_table* madt )
{
  int parsed = sizeof(struct MADT_table);
  int founded = 0;
  cpu_top_id = 0;

  // �������� ����� ��� �������
  while (parsed < madt->header.length)
  {
    u8* p_type = (u8*)((u32)madt + parsed);
    // �������� ��������� ������
    struct Processor_Local_APIC* s = (struct Processor_Local_APIC* ) p_type;
    
    parsed += s->length;
    // ���� ������ - ��� ������ � ���� ����������
    if (*p_type == 0 && s->length == 8)
    { 
        // ��� ���� ���������� ��������� � �������������?
        if (s->flags & 1)
        {
            if (founded >= SMP_MAX_CPU_COUNT)
                return 0;

            // ����� ��������� ����!
            u8 id = s->APIC_ID;
            printf("\nCPU %x ACPI_PROC=%x APIC_ID=%x", s->flags, s->ACPI_processor_ID, s->APIC_ID);
            // �������� ���
            cpu_ids[founded] = id;
            founded++;
            if (cpu_top_id < id)
                cpu_top_id = id;
        }
     }
  }
  cpu_count = founded;
  return 1;
}

// ��� ������� ���� � ���������� ������ ���������� ������� RSDP
// ��� ����� � ������ ����� 0xE0000 � 0xFFFFF ������ ��������� "RSD PTR "
struct RSDP_structure* Acpi_Find_RSDP(void)
{
	char* adr = (char*) 0xE0000;
	char sign[8]={'R','S','D',' ','P','T','R',' '};
	while (!Acpi_SignatureCheck(adr, sign, 8)
		&& adr < (char*)0xfffff)
	{
		adr+=4;
	}
	if (adr == (char*)0xfffff)
		return NULL;

	return (struct RSDP_structure*)adr;
}

// ������ ���������� � ����� ����������
int Acpi_CpusGetInfo()
{
    // �������� ����� RSDP
    struct RSDP_structure* rsdp = (struct RSDP_structure*)Acpi_Find_RSDP();
    if (!rsdp)
    {
        cpu_ids[0] = 0;
        cpu_count = 1;
        cpu_top_id = 0;
        return true;
    }
	
    // �������� ����� �� ������� RSDT
    struct RSDT_table * rsdt = (struct RSDT_table *)rsdp->RSDT_addr;
    // ������ ���� � RSDT ����� MADT - ��� �� ������������ �������
    {
	u32* adr;
        for (adr = (u32*)((u32)rsdt + sizeof(struct RSDT_table)); 
            adr < (u32*)((u32)rsdt + rsdt->header.length); 
            adr++)
        {
            // ������� ��������� �������
            if (*(u32*)((void*) *adr) == MADT_SIG_APIC)
            {
                struct MADT_table* madt = (struct MADT_table*)((void*) *adr);
                // ��������� ������ �������
                if (madt->header.revision < 1) 
                {
                    printf("\nError in MADT: wrong revision should be 1, but = 0x%x",madt->header.revision);
                    return 0;
                }
                // �������� ��������� �������
                Acpi_Parse_MADT(madt);
                return 1;
            }
	}
    }
    return 0;
}

// ����� ���� ��������� ������� ��� ������ � LAPIC

// volatile ������� �����������, ��� ��� ���������� ����� ���� ���������
// ��� ������ �����������. ��������� LAPIC - ��� ����������, 
// �� ��� ����� ������ �������� ����� ���� ��������� ��� ����������� �� gcc

// ������ ������� ������� ����� LAPIC
volatile u32 LapicBase_lo = 0;
volatile u32 LapicBase_hi = 0;
// ����� ��������� ��������� �� ��� ����� ������ 64-� ���������� �������� ICR
static volatile u32 *LapicICR_lo;
static volatile u32 *LapicICR_hi;

// ��������������� �������, ������������ LAPIC ID �������� ����
int Lapic_GetID()
{
    return (int)((*(unsigned int *)(LapicBase_lo + APIC_LAPIC_ID)) >> 24);
}

// ������� ��������� ����������� LAPIC � �������
int Lapic_Available()
{
  u32 a, b, c, d, msr_h, msr_l;
  
  // �������� ���� � CPUID
  __cpuid_count(1, 0, &a, &b, &c, &d);
  if (!(d & CPUID_1_EDX_APIC_BIT))
    return 0;

  // �������� ���� ENABLE � ����� �������� APIC BASE
  __rdmsr(MSR_IA32_APIC_BASE_MSR, msr_l, msr_h);
  if (!(msr_l & MSR_IA32_APIC_BASE_MSR_APIC_GLOBAL_ENABLE_BIT))
    return 0;

  return 1;
}

// ��� ������ � LAPIC ��� ����������� ������ ������������ ��������
// ��������� �� ������ ����� PIT, �� ���������� � ��� �� ���������, �������
// ������� �������� ��������� ����� �������� ������������� �������� TSC
// ��������� TSC ���������� � ������, �� ����� ����� ��������� ����� ���������
// �������� ������� ����������
void TscDelay( u64 ns )
{
    // ns - � ������������ (10^-9)
    u64 target = rdtsc() + ns * CPU_FREQ;
    while (rdtsc() < target)
        ;
}

// ��������� ��� ������� ����� �� http://fxr.watson.org/fxr/source/i386/i386/mp_machdep.c
// � ���������� �������������

// ������� �������� ���������� LAPIC ������������ ��������� �������
// ������� ������ ������� ��������� APIC_DELSTAT_IDLE � �������� ICR
int Lapic_IpiWait()
{
    while (1)
    {
        if (((*LapicICR_lo) & APIC_DELSTAT_MASK) == APIC_DELSTAT_IDLE)
            return 1;
    }
    return 0;
}

// ������� ���������� ��������� ������� � ICR
// ���������: ������� ������ ���������� ������ � ������ MMIO ��� LAPIC ICR
void Lapic_IpiRaw( u32 icrlo, u32 dest )
{
    u32 value;

    // ��� ������������� ���������� �������� � ������� ����� ICR
    if ((icrlo & APIC_DEST_MASK) == APIC_DEST_DESTFLD) 
    {
        value = *LapicICR_hi;
        value &= ~APIC_ID_MASK;
        value |= dest << APIC_ID_SHIFT;
        *LapicICR_hi = value;
    }

    // ���������� �������� � ������ ����� ICR
    value = (*LapicICR_lo);
    value &= APIC_ICRLO_RESV_MASK;
    value |= icrlo;
    *LapicICR_lo = value;
}

// ������� �������� IPI �� �������� LAPIC ID, � ��������� �������� �������
// vector - ��� ��������� ���������� ����� ������ ����������, ������� AP ������ ���������
void Lapic_IpiStartup( int apic_id, int vector )
{
    // �������� ��������� �� �����
    printf("\nTRY START %x %x %x:%x", apic_id, vector, LapicICR_lo, LapicICR_hi);
    
    // ������ �������� INIT IPI. ����� ����� AP ����� ������� STARTUP IPI
    Lapic_IpiRaw(APIC_DEST_DESTFLD | APIC_TRIGMOD_EDGE |
        APIC_LEVEL_ASSERT | APIC_DESTMODE_PHY | APIC_DELMODE_INIT, apic_id);
    Lapic_IpiWait(); // ������� ���������� ICR
    TscDelay(10000); // �������� ~10mS

    // �������� STARTUP IPI, � ��������� ������ � �������� AP ������ ����������
    Lapic_IpiRaw(APIC_DEST_DESTFLD | APIC_TRIGMOD_EDGE |
        APIC_LEVEL_DEASSERT | APIC_DESTMODE_PHY | APIC_DELMODE_STARTUP |
        vector, apic_id);
    Lapic_IpiWait();  // ������� ���������� ICR
    TscDelay(200);    // �������� ~200uS
    
    // �������� ������ STARTUP IPI, ��� � ��������
    Lapic_IpiRaw(APIC_DEST_DESTFLD | APIC_TRIGMOD_EDGE |
        APIC_LEVEL_DEASSERT | APIC_DESTMODE_PHY | APIC_DELMODE_STARTUP |
        vector, apic_id);
    Lapic_IpiWait();  // ������� ���������� ICR
    TscDelay(200);    // �������� ~200uS
}


// ��������� ������� ��������������� ��������� ��� ��������� 
// � ������ ���� ���� � �������

// ������ ������� ����������� SMP
int SmpSetup()
{
    if (!Lapic_Available())
    {
        printf("\nNO APIC",0);
        return 0;
    }

    // ������ �������� �������� MSR, ����� ����������� ���� LAPIC
    __rdmsr(MSR_IA32_APIC_BASE_MSR, LapicBase_lo, LapicBase_hi);
    printf("\nMSR_APIC_BASE: %x %x", LapicBase_hi, LapicBase_lo);
    // ��������� �����, ��� ����������� ������
    LapicBase_lo &= MSR_IA32_APIC_BASE_MSR_APIC_BASE_MASK;
    // ��������� ��������� �� ��� ����� �������� LAPIC ICR
    LapicICR_lo = (u32 *)(LapicBase_lo + APIC_LAPIC_ICR);
    LapicICR_hi = (u32 *)(LapicBase_lo + APIC_LAPIC_ICR + 0x10);
    printf("\nAPIC: ICR %x", LapicICR_lo);
    
    // �������� ���������� � ���� ��� ������ AP
    printf("\nAPIC: AP code %x %x", (u32)SmpEntry, ((u32)SmpEntry_END - (u32)SmpEntry));
    // �������� ��� AP �� ��������� ������ _BASE
    memcpy((void *)SMP_AP_STARTUP_CODE, (void *)SmpEntry, ((u32)SmpEntry_END - (u32)SmpEntry));

    // �������������� ���
    SmpSpinlock_INIT(&SmpMainLock);
    return 1;
}

// ������� ��������� AP � �������� LAPIC ID
void SmpStartAp(u8 id)
{
    //�������� ��������� �� ������ ESP
    volatile SmpBootCpusApInfo_t *startup_info = (SmpBootCpusApInfo_t *)(SMP_AP_STARTUP_ESPP);
    // ��������� ����� ESP ������������ 5Mb (0x500000)
    // ������ ������� ����� 16 * 4Kb (0x10000)
    startup_info->ptrESP[id] = 0x500000 + 0x10000 * id;
    // ���������� INIT-SIPI-SIPI, �������� ��������� ����� ����
    Lapic_IpiStartup(id, (SMP_AP_STARTUP_CODE >> 12));
}

// ������� ������� �������, ����� ��� AP ����������
// ������ AP ����� ����������� ������� SmpStartedCpus
// ������� �� ������ ���� ����������� �������� ��������
void SmpWaitAllAps( ulong count )
{
    ulong real_count = 0;
    while (1)
    {
        // ��������� ���, ��������� ������ ������� �������� ��������
        SmpSpinlock_LOCK(&SmpMainLock);
        real_count = SmpStartedCpus;
        SmpSpinlock_UNLOCK(&SmpMainLock);

        // ���� ��� ���� �����������, �� ������� �� �������
        if (real_count == count)
        {
            printf("\nBSP: DONE: count %lu / %lu", real_count, count);
            return;
        }

        // �����, ���� �����-�� �����
        printf("\nBSP: count %lu / %lu", real_count, count);
        TscDelay(10000UL);
    }
}

// ������� ��������� ��� ���� ���������� � ������� � ��������� �� � ����� ��������
// BSP ��� ���� ���������� ����������. ����� ��������� ��� AP ���������� ����� ������� SmpReleaseAllAps
void SmpPrepare(void)
{
    int i = 0;

    SmpSetup();
    Acpi_CpusGetInfo();
    // �������� ���������� � �������� �����
    printf("\nHV: CPUS detected %d top id %d ids: %x %x %x %x", 
        cpu_count, cpu_top_id, cpu_ids[0], cpu_ids[1], cpu_ids[2], cpu_ids[3]);
    
    // ��������� ��� ����
    for (i = 1; i < cpu_count; i++)
    {
      SmpStartAp(cpu_ids[i]);
    }
    // ������� ������� �� ���� ����
    SmpWaitAllAps(cpu_count);
}

// ������� ������ ���, ����������� ��������� �������� �������� ����� �� ���� AP
void SmpReleaseAllAps()
{
        SmpSpinlock_LOCK(&SmpMainLock);
        SmpStartedCpusStarted = 1;
        SmpSpinlock_UNLOCK(&SmpMainLock);
}

// ������� ������������ ��������� AP � �������
// ��� �����, ��� ������ ����������� ���������� �������
void SmpApRegister( u8 id )
{
    if (id != 0)
    {
        printf("\n -> DONE!");
        SmpSpinlock_LOCK(&SmpMainLock);
        SmpStartedCpus++;
        SmpSpinlock_UNLOCK(&SmpMainLock);
    }
}

void ap_cpu_worker(int index);

// � ��� �������, ������� ���������� ������ AP
void SmpApMain()
{
    // ���������� ����������� LAPIC ID
    u8 id = Lapic_GetID();
    int i = 0;
    int ready = 0;

    // ���� ���� ������ �� ID
    for (i = 0; i < cpu_count; i++)
    {
       if (cpu_ids[i] == id)
          break;
    }
    SmpApRegister(id);
    printf("\n--> READY %x %x", id, &id);
    
    // ������� ���������� ��������� �������� ��������
    while (!ready)
    {
        
        SmpSpinlock_LOCK(&SmpMainLock);
        ready = SmpStartedCpusStarted;
        SmpSpinlock_UNLOCK(&SmpMainLock);
    }

    // �������� ������� � �������� ���������
    // � ������� �������� �� ID, � ������ �� 0 �� CPU count
    // � ���� ������� ����� ��������� Ray Tracing
    ap_cpu_worker(i);
    forever();
}



