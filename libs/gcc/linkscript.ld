MEMORY
{
	ROM (rx) :  ORIGIN = 0x102C7040, LENGTH = 0x0005A000
	RAM (rwx) : ORIGIN = 0xf03D2000, LENGTH = 0x00019000
}

SECTIONS
{
    . = 0x102C7040;
	.initdata : 
	{ 
		KEEP(*(.initdata));
	} > ROM	
	text : 
	{ 
	    *(.text) 
	} > ROM
	.rodata : 
	{
	    *(.rodata)
	} > ROM
	.data : 
	{
		PROVIDE_HIDDEN (__data_load = LOADADDR(.data));
		PROVIDE_HIDDEN (__data_start = .);
		*(.data)
		PROVIDE_HIDDEN (_edata = .);
	} > RAM  AT > ROM
	.bss : 
	{
		PROVIDE_HIDDEN (__bss_start__ = .);
		*(.bss)
		PROVIDE_HIDDEN (__bss_end__ = .);
	} > RAM
}