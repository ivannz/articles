#ifndef _HARDWARE_H
#define _HARDWARE_H

// (1) ����������� ��� ���������� �������� � ������������ ����
// ������� ����������� �������� ��������� � ������
#define __STR(...) #__VA_ARGS__
#define S(...) __STR(__VA_ARGS__)

// (2) ���������, �������������� ��� ������� ������ ACPI
// ��� ��������� ������� �� ����������� ������������ ACPI
struct u64_field
{
	u32 low_32;
	u32 high_32;
}__attribute__((__packed__));

struct u48_field
{
	u32 low_32;
	u16 high_16;
}__attribute__((__packed__));

struct u24_field
{
	u16 low_16;
	u8 high_8;
}__attribute__((__packed__));

struct RSDP_structure
{
	char signature[8];
	u8 checksum;
	struct u48_field OEMID;
	u8 revision;
	u32 RSDT_addr;
	u32 length;
	struct u64_field XSDT_addr;
	u8 extended_chechsum;
	struct u24_field reserved;

}__attribute__((__packed__));

struct DESCRIPTION_HEADER
{
	char signature[4];
	u32 length;
	u8 revision;
	u8 checksum;
	struct u48_field OEMID;
	struct u64_field OEM_table_ID;
	u32 OEM_revision;
	u32 creator_ID;
	u32 creator_revision;

}__attribute__((__packed__));

struct RSDT_table
{
	struct DESCRIPTION_HEADER header;
	//entry[n]
}__attribute__((__packed__));

struct MADT_table
{
	struct DESCRIPTION_HEADER header;	
	u32 local_APIC_addr;
	u32 flags;
	//APIC structures
}__attribute__((__packed__));

// ��������� ������� MADT
#define MADT_SIG_APIC 0x43495041

// ��������� ����� ������ � MADT ��������������� ���� ����������
// ��� ���������� � ��� ������ flags ��� �������� ���������� � APIC_ID
struct Processor_Local_APIC
{
	u8 type; //=0
	u8 length; //=8
	u8 ACPI_processor_ID;
	u8 APIC_ID;
	u32 flags;
}__attribute__((__packed__));


// ����� ����� ����� ������������� ����, ������� �������,
// �� ����������� �������� ���������
// ��������� ����������, ��� ��� gcc inline ��������� �������� ���:
// __asm__ __volatile__ ("<����������>" : <�������� ���������> : <������� ���������>);
// ��� ��������� ���� ������� ����� ����� � ��������� �� �����

// (3) ������� ��� ������ �������� TSC. ��� 64-� ������ ������� ������ 
// �������� ���� ����������, ������� ���������� ��� ������ ����.
// ������������ ������������ ���������� rdtsc.
static inline unsigned long long rdtsc(void) 
{
    unsigned long long a;
    __asm__ __volatile__ ("rdtsc" : "=A" (a));
    return a;
}

// (4) ������� ��� ������ � ������ MSR (Model Specific Register)
#define __rdmsr(msr,val1,val2) \
     __asm__ __volatile__("rdmsr" \
              : "=a" (val1), "=d" (val2) \
              : "c" (msr))

#define __rdmsrl(msr,val) do { unsigned long a__,b__; \
       __asm__ __volatile__("rdmsr" \
                : "=a" (a__), "=d" (b__) \
                : "c" (msr)); \
       val = a__ | ((u64)b__<<32); \
} while(0);

#define __wrmsr(msr,val1,val2) \
     __asm__ __volatile__("wrmsr" \
              : /* no outputs */ \
              : "c" (msr), "a" (val1), "d" (val2))

static inline void __wrmsrl(unsigned int msr, u64 val)
{
        u32 lo, hi;
        lo = (u32)val;
        hi = (u32)(val >> 32);
        __wrmsr(msr, lo, hi);
}


// (5) �������, ������� ������� �������� �� ���������. 
// ��������� � ��� ������ �� ������. ���������� nop
static inline void __rep_nop(void)
{
    asm volatile ( "rep;nop" : : : "memory" );
}

// (6) ������� ��� ��������� �����
static inline void forever()
{
    while(1) __rep_nop();
}

// (7) ������� ������� ����������� �������������� ����������
// ����� ���������� cpuid
static inline void __cpuid_count( int op, int count,
    u32 *eax, u32 *ebx, u32 *ecx, u32 *edx )
{
    asm ( "cpuid"
          : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
          : "a" (op), "c" (count) );
}

// ��� ���� = ������ 1 ����
typedef char spinlock_t;

// ��� ����� ����������� ��� ����������� �� 
// http://orangetide.com/src/bitvisor-1.3/include/core/spinlock.h
// ������� �������� ������������ ���� � ��������� ���
static inline void SmpSpinlock_LOCK(spinlock_t *l)
{
    char dummy;

    __asm__ __volatile__("3: \n"
              " xchg  %1, %0 \n" /* exchange %0 and *l */
              " test  %0, %0 \n" /* check whether %0 is 0 */
              " je    1f \n" /* if 0, succeeded */
              "2: \n"
              " pause \n" /* spin loop hint */
              " cmp   %1, %0 \n" /* compare %0 and *l */
              " je    2b \n" /* if not 0, do spin loop */
              " jmp   3b \n" /* if 0, try again */
              "1: \n"
#ifdef __x86_64__
              : "=r" (dummy)
#else
              : "=abcd" (dummy)
#endif
              , "=m" (*l)
              : "0" ((char)1)
              : "cc");
}

// ������� ������������ ����
static inline void SmpSpinlock_UNLOCK(spinlock_t *l)
{
    char dummy;

    __asm__ __volatile__("xchg %1, %0 \n"
#ifdef __x86_64__
              : "=r" (dummy)
#else
              : "=abcd" (dummy)
#endif        
              , "=m" (*l)
              : "0" ((char)0));
}

// ������� ������������� ����
static inline void SmpSpinlock_INIT(spinlock_t *l)
{
    SmpSpinlock_UNLOCK(l);
}


// (8) ��������� ��������, �������������� ��� ������ � APIC
// ��� ��� ����� �� ����������� ������������ Intel CPU Reference Manual

// ����� �������� �� ��������� �������� LAPIC ID
#define APIC_LAPIC_ID 			0x20
// ����� �������� ICR ��� LAPIC
#define APIC_LAPIC_ICR 			0x300
// ��� � CPUID ��� �������� EDX, ������� �������� �� ���������� LAPIC �� ����������
#define CPUID_1_EDX_APIC_BIT		0x200
// ����� MSR, ������� ������ ���� ��� ���������� LAPIC
#define MSR_IA32_APIC_BASE_MSR		0x1B
// ��� � ���� ��������, ����������� �� �� ������� LAPIC ��� ���
#define MSR_IA32_APIC_BASE_MSR_APIC_GLOBAL_ENABLE_BIT	0x800
// ����� ��� ��������, ������� �������� ����� LAPIC (�� ������ �������� �� 4Kb)
#define MSR_IA32_APIC_BASE_MSR_APIC_BASE_MASK	0xFFFFFF000ULL

// ����� ��������� ��� �������� LAPIC ICR
#define APIC_DELSTAT_MASK	0x00001000
// ��������� �������� ������
# define APIC_DELSTAT_IDLE	0x00000000

// ����� ���� � ����� ���������� ������� ��� �������� ICR
#define APIC_DEST_MASK		0x000c0000
// ��� ���������� - ������ ���� ����������
# define APIC_DEST_DESTFLD	0x00000000

// ����� ���� � ��������������� ���� ���������� ������� ��� �������� ICR
#define APIC_ID_MASK		0xff000000
// �������� ��� ���� � ��������������� ���� ���������� ������� � �������� ICR
#define	APIC_ID_SHIFT		24
// �������� ����������������� ����� � �������� ICR
#define APIC_RESV2_MASK		0xfff00000
#define APIC_RESV1_MASK		0x00002000
#define	APIC_ICRLO_RESV_MASK	(APIC_RESV1_MASK | APIC_RESV2_MASK)

// �������� ����� ����� ������� ��� �������� ICR
// �������� ����� �� ������������ � ���������� ��� �������� IPI
# define APIC_TRIGMOD_EDGE	0x00000000
# define APIC_LEVEL_ASSERT	0x00004000
# define APIC_LEVEL_DEASSERT	0x00000000
# define APIC_DESTMODE_PHY	0x00000000
# define APIC_DELMODE_INIT	0x00000500
# define APIC_DELMODE_STARTUP	0x00000600


// (10) ������� ��� ������ � ��������� CR0
static inline unsigned int __get_cr0( void )
{
    register ulong res;
    __asm__ __volatile__("movl %%cr0, %0\n\t" : "=r" (res) : ); 
    return(res);
}

static inline void __set_cr0( ulong addr )
{
    __asm__ __volatile__("movl %0, %%cr0\n\t" : : "r" (addr));
}

// ���������� ���������� ����� � CR0 ������� ����������� ��� ������������� FPU
#define X86_CR0_EM              0x00000004 /* Require FPU Emulation    (RO) */
#define X86_CR0_TS              0x00000008 /* Task Switched            (RW) */

// (11) �������, ������� �������� ��������� FPU �� ���� ����������
static inline void __enable_fpu()
{
  ulong cr0 = __get_cr0();
  cr0 &= ~(X86_CR0_TS | X86_CR0_EM);
  __set_cr0(cr0);
  asm ( "fnclex\r\nfninit\r\n"   : : );
}


#endif

