// Базовые определения
#include "types.h"
#include "printf.h"
#include "string.h"
#include "hardware.h"

//// Общие константы, верные для всего файла
// Максимальное количество детектируемых ядер.
// Для примера взято 16, но можно поставить и 256
#define SMP_MAX_CPU_COUNT 16

// Следующая константа соответствует примерной частоте процессора
// Определение частоы выходит за рамки статьи, полэтому это просто константа
// Она используется для не точного замера времени
#define CPU_FREQ 1000UL

//// Константы, используемые в ассемблерном коде, который запускает AP
// Физический адрес, по которому располагается код, исполняемый AP, 
// начиная с первой инструкции
#define SMP_AP_STARTUP_CODE     0x6000
// Адрес, по которому располаестя массив адресов ESP для каждого AP
// Поскольку все ядра выполняются параллельно, то каждому из них нужен свой стэк
// Мы заранее выделяем области памяти для стека каждого из AP
// Массив по этому адресу хранит указатели
#define SMP_AP_STARTUP_ESPP     0x5000

// Краткие синонимы для ранее введенных констант. Используются в ассемблере
#define _BASE    SMP_AP_STARTUP_CODE
#define _ESP     SMP_AP_STARTUP_ESPP

// Структура с параметрами, использующимеся при инициализации AP
// У нас эта структура хранит тролько массив указателей ESP для каждого процессора
// На первое поле этой структуры указывает TR_SMP_AP_STARTUP_DATA_ESPP
typedef struct 
{
    u32 ptrESP[SMP_MAX_CPU_COUNT];
} __attribute__ ((packed)) SmpBootCpusApInfo_t;

// Теперь самое интересное... код который будет выполняться на каждом AP
// Цель этого кода: проинициализировать AP, переведя его в Protected Mode и прыгнуть на C функцию SmpApMain

// GCC позволяет таким образом вставлять ассемблерные функции прямо в C код
asm (
    // Включить дальнейший код в сегмент кода программы
    "    .text                    \n"
    // Выровнять стартовый адрес по 8 байт
    "    .align    8              \n"
    // Объявляем символ trSmpEntry, которая затем будет 
    // использоваться в C для определения указателя на начало кода
    // .global делает символ видимым для C кода
    "    .globl SmpEntry        \n"
    // Непосредственно метка trSmpEntry
    "SmpEntry  :                \n"
    // Дополнительная метка trSmpEntry16, отмечающая начало 16-ти битного кода
    // .code16 говорит компилятору, что далее идет 16-ти битный код
    // Помним, что AP стартует в Real Mode (16 бит)
    "SmpEntry16: .code16        \n"
    // Сбрасываем флаги для возможности вызова C функций
    "    cld                      \n"
    // Запрещаем прерывания
    "    cli                      \n"
    // Читаем значение CR0 в EAX
    "    mov  %cr0,%eax           \n"
    // Устанавливаем флаги, необходимые для перехода в Protected Mode:
    //  PE (Protected Mode Enable), TS (Task Switch)
    "    or	  $0x00000009,%eax    \n" // PE|TS
    // Записываем измененное значение обратно в CR0
    "    mov  %eax,%cr0           \n"
    // Мы в Protected Mode! Но нам надо еще настроить GDT 
    // (Сегменты еще не перезагружены)
    // И дескриптор и таблица объявлены далее прямо в коде

    // Записываем в EAX адрес, по которому располагается дескриптор GDT
    // Адрес дескриптора по метке trSmp16_gdt_desr
    // trSmp16_gdt_desr - смещение относительно адреса кода из LD скрипта
    // А нам нужен абсолютный физический адрес. 
    // (*) Поскольку код будет скопирован по адресу _BASE, то абсолютный адрес
    // вычисляется так: trSmp16_gdt_desr - trSmpEntry + _BASE
    "    movl $(Smp16_gdt_desr - SmpEntry + "S(_BASE)"), %eax \n"
    // Загружаем дескриптор GDT
    "    lgdt (%eax)                 \n"
    // Теперь прыгаем на 32-х битный код по адресу, который уже указан в LD
    // Другими словами у нас есть две копии этого ассмблерного кода: 
    //   - одна по адресу _BASE (это копия второго)
    //   - вторая по адресу, который определил линковщик, используя код LD скрипта
    // До следующей инструкции (ее включая) AP выполнял код по адресу _BASE
    // После прыжка на trSmpEntry32 - будет выполняться оригинальный код по 
    // оригинальным адресам, которые определил LD
    // Итак: прыгаем на 32-х битный код, который расположен сразу за GDT таблицей
    "    ljmpl $0x08, $SmpEntry32  \n"
    
    //// Дескриптор GDT
    // Выравнивание по 8 байт необходимо для инструкции lgdt
    "    .align 8                    \n"
    // Дескриптор содержит два поля: размер таблицы 16 бит и адрес таблицы 32 бита
    "Smp16_gdt_desr:               \n"
    // Размер вычисляется <конец> - <начало> - 1. -1 нужен по правилам lgdt
    "    .word Smp16_gdt_end - Smp16_gdt - 1     \n"
    // Адрес вычисляется аналогично (*)
    "    .long Smp16_gdt - SmpEntry + "S(_BASE)" \n"

    // Таблица GDT, на которую ссылается GDTR
    // Выравнивание по 8 байт необходимо для GDT
    "    .align 8                    \n"
    // Таблица содержит 2 дескприптора и нулевой дескриптор, который требуется по правилам GDT
    "Smp16_gdt:                    \n"
    // Нулевой дескриптор сегмента
    "    .quad 0x0000000000000000    \n"
    // Точниые значения битов можно посмотреть в Intel CPU Reference Manual

    // Дескриптор сегмента 32-х битного кода для Ring-0. Диапазон от 0 до 4G
    // Селетор 8 (0x8)
    "    .quad 0x00cf9a000000ffff    \n"
    // Дескриптор сегмента 32-х битных данных для Ring-0. Диапазон от 0 до 4G
    // Селетор 16 (0x10)
    "    .quad 0x00cf92000000ffff    \n"
    "Smp16_gdt_end:                \n"

    // Начало 32-х битного кода. Пока все выполняем без стэка
    // Прежде чем вызывать функцию С нам нужно еще проинициализировать стэк (EBP/ESP)
    // и обнулить регистры и 
    // .code32 - говорит компилятору, что далее уже 32-х битный код
    "SmpEntry32: .code32          \n"
    // Записываем 16 (селектор 16-ти битного сегмента данных) в AX
    "    mov  $16,%ax     \n"
    // Устанавливаем все селекторы сегментов в 16
    "    mov  %ax,%ds               \n"
    "    mov  %ax,%es               \n"
    "    mov  %ax,%fs               \n"
    "    mov  %ax,%gs               \n"
    "    mov  %ax,%ss               \n"
    // Обнуляем все регистры
    "    xor  %eax, %eax            \n"
    "    xor  %ebx, %ebx            \n"
    "    xor  %ecx, %ecx            \n"
    "    xor  %edx, %edx            \n"

    // Для того, чтобы получить указатель на ESP надо знать свой LAPIC ID
    // Его можно получить, прочитав регистр из устройства LAPIC
    // Во-первых, нужно найти базовый адрес для устройства LAPIC

    // Запишем номер нужного MSR в ECX
    "    mov  $"S(MSR_IA32_APIC_BASE_MSR)",%ecx      \n"
    // Читаем MSR по номеру в ECX
    "    rdmsr  \n"
    // В EAX теперь адрес базы LAPIC
    // Нужно обнулить младшие 12 бит
    "    and  $0xfffff000,%eax         \n"
    // Далее добавляем к адресу 0x20 (Адрес регистра APIC_LAPIC_ID)
    "    add  $"S(APIC_LAPIC_ID)",%eax \n"
    // Итого у нас в EAX скорее всего будет адрес  0xfee00020
    // Читаем значение этого регистра LAPIC ID в EBX
    "    mov  (%eax),%ebx      \n"
    // Сам байт LAPIC ID - это старший байт, поэтому EBX >> 24
    "    shrl $0x18, %ebx           \n"

    // Таким образом в EBX находится идентификатор текущего ядра процессора
    // Осталось узнать свой адрес стэка
    // Сохраним в EDX адрес массива
    "    mov $"S(_ESP)", %edx         \n"
    // Теперь запишем в EAX адрес стэка
    // Он будет располагаться по адресу: (EDX + EBX * 4 + 0)
    // Чтобы прочитать значение по такому адресу можно воспользоваться инструкцией:
    "    movl 0(%edx, %ebx, 4), %eax  \n"
    // Устанавливаем значения ESP и EBP, полученные из EAX
    "    movl %eax,%esp               \n"
    "    movl %esp,%ebp               \n"

    // Обнуляем все регистры
    "    xor  %eax, %eax            \n"
    "    xor  %ebx, %ebx            \n"
    "    xor  %ecx, %ecx            \n"
    "    xor  %edx, %edx            \n"

    // Теперь можно вызвать функцию из C
    "    call SmpApMain             \n"

    // На всякий слуяай бесконечный цикл, после окончания Main
    "_ap_loop:                        \n"
    "    jmp _ap_loop                 \n"
    "    .globl SmpEntry_END        \n"
    // Метка конца кода
    "SmpEntry_END:                  \n"
    // Далее так же идет секция 32-х битного кода
    ".text                            \n"
    ".code32                          \n"
    );

// Объявление ассемблерных меток для кода на С
// Поскольку это метки - то они объявляются как функции, 
// чтобы ими можно было пользоваться как указателями
extern void SmpEntry(void);
extern void SmpEntry_END(void);

// Объявим глобальный лок, который будет использоваться для синхронизации ядер
spinlock_t SmpMainLock;
// Переменная, которая будет хранить количество уже запущенных CPU 
// В начале запущен только BSP
ulong SmpStartedCpus = 1;

// Флаг готовности сразу всех ядер процессора к выполнению полезной нагрузки
// Этот флаг будет использоваться как барьер для старта рендеринга
ulong SmpStartedCpusStarted = 0;

// Функция, котрая запускает процесор с заданным LAPIC ID
void SmpStartCpu(u8 id);

// Поскольку часть кода бралось из C++, а у нас C, то нужно определить bool
typedef int bool;
#define true 1
#define false 0

// Начнем с функций связаных с ACPI
// Наша цель: собрать список LAPIC ID для всех ядер в системе

// Объявим несколько глобальных переменных, которые будут хранить результаты поисков
// Список найденых LAPIC ID
u8 cpu_ids[SMP_MAX_CPU_COUNT];
// Количество найденых LAPIC ID
u32 cpu_count = 0;
// Максимальное значение LAPIC ID
u32 cpu_top_id = 0;

// Проверка контрольной суммы для заданной таблицы ACPI - это необходимая проверка
int Acpi_SignatureCheck(char* val1, char* val2,int len)
{
  int i;
  for (i = 0; i < len; i++)
    if (val1[i] != val2[i])
      return 0;
  return 1;
}

// Эта функция разбирает таблицу MADT и заполняет массив cpu_ids
int Acpi_Parse_MADT( struct MADT_table* madt )
{
  int parsed = sizeof(struct MADT_table);
  int founded = 0;
  cpu_top_id = 0;

  // Проходим через всю таблицу
  while (parsed < madt->header.length)
  {
    u8* p_type = (u8*)((u32)madt + parsed);
    // Пытаемся разобрать запись
    struct Processor_Local_APIC* s = (struct Processor_Local_APIC* ) p_type;
    
    parsed += s->length;
    // Если запись - это запись о ядре процессора
    if (*p_type == 0 && s->length == 8)
    { 
        // Это ядро процессора разрешено к использованию?
        if (s->flags & 1)
        {
            if (founded >= SMP_MAX_CPU_COUNT)
                return 0;

            // Нашли очередное ядро!
            u8 id = s->APIC_ID;
            printf("\nCPU %x ACPI_PROC=%x APIC_ID=%x", s->flags, s->ACPI_processor_ID, s->APIC_ID);
            // Сохраним его
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

// Эта функция ищет в физической памяти компьютера таблицу RSDP
// Для этого в памяти между 0xE0000 и 0xFFFFF ищется сигнатура "RSD PTR "
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

// Читаем информацию о ядрах процессора
int Acpi_CpusGetInfo()
{
    // Пытаемся найти RSDP
    struct RSDP_structure* rsdp = (struct RSDP_structure*)Acpi_Find_RSDP();
    if (!rsdp)
    {
        cpu_ids[0] = 0;
        cpu_count = 1;
        cpu_top_id = 0;
        return true;
    }
	
    // Получаем адрес на таблицу RSDT
    struct RSDT_table * rsdt = (struct RSDT_table *)rsdp->RSDT_addr;
    // Теперь ищем в RSDT адрес MADT - это не обязательная таблица
    {
	u32* adr;
        for (adr = (u32*)((u32)rsdt + sizeof(struct RSDT_table)); 
            adr < (u32*)((u32)rsdt + rsdt->header.length); 
            adr++)
        {
            // Сверяем сигнатуру таблицы
            if (*(u32*)((void*) *adr) == MADT_SIG_APIC)
            {
                struct MADT_table* madt = (struct MADT_table*)((void*) *adr);
                // Проверяем версию таблицы
                if (madt->header.revision < 1) 
                {
                    printf("\nError in MADT: wrong revision should be 1, but = 0x%x",madt->header.revision);
                    return 0;
                }
                // Пытаемся разобрать таблицу
                Acpi_Parse_MADT(madt);
                return 1;
            }
	}
    }
    return 0;
}

// Далее идут несколько функций для работы с LAPIC

// volatile говорит компилятору, что эта переменная может быть измененеа
// без ведома компилятора. Поскольку LAPIC - это устройство, 
// то оно может менять значения своих свои регистров вне зависимости от gcc

// Сперва объявим базовый адрес LAPIC
volatile u32 LapicBase_lo = 0;
volatile u32 LapicBase_hi = 0;
// Далее объявлены указатели на две части адреса 64-х разрядного регистра ICR
static volatile u32 *LapicICR_lo;
static volatile u32 *LapicICR_hi;

// Вспомогательная функция, возвращающая LAPIC ID текущего ядра
int Lapic_GetID()
{
    return (int)((*(unsigned int *)(LapicBase_lo + APIC_LAPIC_ID)) >> 24);
}

// Функция проверяет доступность LAPIC в системе
int Lapic_Available()
{
  u32 a, b, c, d, msr_h, msr_l;
  
  // Проверка бита в CPUID
  __cpuid_count(1, 0, &a, &b, &c, &d);
  if (!(d & CPUID_1_EDX_APIC_BIT))
    return 0;

  // Проверка бита ENABLE в самом регистре APIC BASE
  __rdmsr(MSR_IA32_APIC_BASE_MSR, msr_l, msr_h);
  if (!(msr_l & MSR_IA32_APIC_BASE_MSR_APIC_GLOBAL_ENABLE_BIT))
    return 0;

  return 1;
}

// Для работы с LAPIC нам потребуется делать искуственные задержки
// Правильно их делать через PIT, но прерывания у нас не настроены, поэтому
// Функцию задержки выполняем через ожидание определенного значения TSC
// Поскольку TSC измеряется в тактах, то время можно посчитать через примерную
// тактовую частоту процессора
void TscDelay( u64 ns )
{
    // ns - в наносекундах (10^-9)
    u64 target = rdtsc() + ns * CPU_FREQ;
    while (rdtsc() < target)
        ;
}

// Следующие три функции взяты из http://fxr.watson.org/fxr/source/i386/i386/mp_machdep.c
// С небольшими модификациями

// Функция ожимания готовности LAPIC обрабатывать следующую команду
// Функция просто ожидает состояния APIC_DELSTAT_IDLE у регистра ICR
int Lapic_IpiWait()
{
    while (1)
    {
        if (((*LapicICR_lo) & APIC_DELSTAT_MASK) == APIC_DELSTAT_IDLE)
            return 1;
    }
    return 0;
}

// Функция записывает требуемую команду в ICR
// Подчеркну: Функция просто записывает данные в память MMIO для LAPIC ICR
void Lapic_IpiRaw( u32 icrlo, u32 dest )
{
    u32 value;

    // При необходимости записываем значения в верхнюю часть ICR
    if ((icrlo & APIC_DEST_MASK) == APIC_DEST_DESTFLD) 
    {
        value = *LapicICR_hi;
        value &= ~APIC_ID_MASK;
        value |= dest << APIC_ID_SHIFT;
        *LapicICR_hi = value;
    }

    // Записываем значения в нижнюю часть ICR
    value = (*LapicICR_lo);
    value &= APIC_ICRLO_RESV_MASK;
    value |= icrlo;
    *LapicICR_lo = value;
}

// Функция отсылает IPI на заданный LAPIC ID, с указанием значение вектора
// vector - это смещенный физический адрес первой инструкции, которую AP должен выполнить
void Lapic_IpiStartup( int apic_id, int vector )
{
    // Печатаем сообщение на экран
    printf("\nTRY START %x %x %x:%x", apic_id, vector, LapicICR_lo, LapicICR_hi);
    
    // Первым отсылаем INIT IPI. После этого AP будет ожидать STARTUP IPI
    Lapic_IpiRaw(APIC_DEST_DESTFLD | APIC_TRIGMOD_EDGE |
        APIC_LEVEL_ASSERT | APIC_DESTMODE_PHY | APIC_DELMODE_INIT, apic_id);
    Lapic_IpiWait(); // Ожидаем готовности ICR
    TscDelay(10000); // Ожидание ~10mS

    // Отсылаем STARTUP IPI, с указанием адреса с которого AP должен стартовать
    Lapic_IpiRaw(APIC_DEST_DESTFLD | APIC_TRIGMOD_EDGE |
        APIC_LEVEL_DEASSERT | APIC_DESTMODE_PHY | APIC_DELMODE_STARTUP |
        vector, apic_id);
    Lapic_IpiWait();  // Ожидаем готовности ICR
    TscDelay(200);    // Ожидание ~200uS
    
    // Отсылаем второй STARTUP IPI, как и положено
    Lapic_IpiRaw(APIC_DEST_DESTFLD | APIC_TRIGMOD_EDGE |
        APIC_LEVEL_DEASSERT | APIC_DESTMODE_PHY | APIC_DELMODE_STARTUP |
        vector, apic_id);
    Lapic_IpiWait();  // Ожидаем готовности ICR
    TscDelay(200);    // Ожидание ~200uS
}


// Следующие функции непосредственно выполняют всю настройку 
// и запуск всех ядер в системе

// Первая функция настраивает SMP
int SmpSetup()
{
    if (!Lapic_Available())
    {
        printf("\nNO APIC",0);
        return 0;
    }

    // Читаем значение регистра MSR, чтобы определеить базу LAPIC
    __rdmsr(MSR_IA32_APIC_BASE_MSR, LapicBase_lo, LapicBase_hi);
    printf("\nMSR_APIC_BASE: %x %x", LapicBase_hi, LapicBase_lo);
    // Применяем маску, для определения адреса
    LapicBase_lo &= MSR_IA32_APIC_BASE_MSR_APIC_BASE_MASK;
    // Сохраняем указатели на две части регистра LAPIC ICR
    LapicICR_lo = (u32 *)(LapicBase_lo + APIC_LAPIC_ICR);
    LapicICR_hi = (u32 *)(LapicBase_lo + APIC_LAPIC_ICR + 0x10);
    printf("\nAPIC: ICR %x", LapicICR_lo);
    
    // Печатаем информацию о коде для старта AP
    printf("\nAPIC: AP code %x %x", (u32)SmpEntry, ((u32)SmpEntry_END - (u32)SmpEntry));
    // Копируем код AP по заданному адресу _BASE
    memcpy((void *)SMP_AP_STARTUP_CODE, (void *)SmpEntry, ((u32)SmpEntry_END - (u32)SmpEntry));

    // Инициализируем лок
    SmpSpinlock_INIT(&SmpMainLock);
    return 1;
}

// Функция запускает AP с заданным LAPIC ID
void SmpStartAp(u8 id)
{
    //Получаем указатель на массив ESP
    volatile SmpBootCpusApInfo_t *startup_info = (SmpBootCpusApInfo_t *)(SMP_AP_STARTUP_ESPP);
    // Указываем адрес ESP относительно 5Mb (0x500000)
    // Размер каждого стека 16 * 4Kb (0x10000)
    startup_info->ptrESP[id] = 0x500000 + 0x10000 * id;
    // Отправляем INIT-SIPI-SIPI, указывая стартовый адрес кода
    Lapic_IpiStartup(id, (SMP_AP_STARTUP_CODE >> 12));
}

// Функция ожидает момента, когда все AP запустятся
// Каждый AP будет увеличивать счетчик SmpStartedCpus
// Поэтому мы просто ждем корректного значения счетчика
void SmpWaitAllAps( ulong count )
{
    ulong real_count = 0;
    while (1)
    {
        // Используя лок, синхронно читаем текущее значение счетчика
        SmpSpinlock_LOCK(&SmpMainLock);
        real_count = SmpStartedCpus;
        SmpSpinlock_UNLOCK(&SmpMainLock);

        // Если все ядра запустились, то выходим из функцмм
        if (real_count == count)
        {
            printf("\nBSP: DONE: count %lu / %lu", real_count, count);
            return;
        }

        // Иначе, ждем какое-то время
        printf("\nBSP: count %lu / %lu", real_count, count);
        TscDelay(10000UL);
    }
}

// Функция запускает все ядра процессора в системе и переводит их в режим ожидания
// BSP при этом возвращает управление. Чтобы запустить все AP достаточно будет вызвать SmpReleaseAllAps
void SmpPrepare(void)
{
    int i = 0;

    SmpSetup();
    Acpi_CpusGetInfo();
    // Печатаем информацию о найденых ядрах
    printf("\nHV: CPUS detected %d top id %d ids: %x %x %x %x", 
        cpu_count, cpu_top_id, cpu_ids[0], cpu_ids[1], cpu_ids[2], cpu_ids[3]);
    
    // Запускаем все ядра
    for (i = 1; i < cpu_count; i++)
    {
      SmpStartAp(cpu_ids[i]);
    }
    // Ожидаем отклика от всех ядер
    SmpWaitAllAps(cpu_count);
}

// Функция ставит бит, позволяющий запустить полезную нагрузку сразу на всех AP
void SmpReleaseAllAps()
{
        SmpSpinlock_LOCK(&SmpMainLock);
        SmpStartedCpusStarted = 1;
        SmpSpinlock_UNLOCK(&SmpMainLock);
}

// Функция регистрирует очередной AP в системе
// Для этого, она просто увеличивает глобальный счетчик
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

// И вот функция, которая вызывается каждым AP
void SmpApMain()
{
    // Определяем собственный LAPIC ID
    u8 id = Lapic_GetID();
    int i = 0;
    int ready = 0;

    // Ищем свой индекс по ID
    for (i = 0; i < cpu_count; i++)
    {
       if (cpu_ids[i] == id)
          break;
    }
    SmpApRegister(id);
    printf("\n--> READY %x %x", id, &id);
    
    // Ожидаем разрешения запустить полезную нагрузку
    while (!ready)
    {
        
        SmpSpinlock_LOCK(&SmpMainLock);
        ready = SmpStartedCpusStarted;
        SmpSpinlock_UNLOCK(&SmpMainLock);
    }

    // Вызываем функцию с полезной нагрузкой
    // В функцию передаем не ID, а индекс от 0 до CPU count
    // В этой функции будем выполнять Ray Tracing
    ap_cpu_worker(i);
    forever();
}



