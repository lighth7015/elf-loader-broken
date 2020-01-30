/*
 * ELF loadable modules
 *
 * Architecture-dependend functions for x86.
 *
 * 12 Feb 2011, Yury Ossadchy
 */
#include "elf-module-private.h"

int elf_module_check_machine(elf_module_t *elf)
{
	return elf->header->e_machine == EM_386
	    || elf->header->e_machine == EM_486;
}

int elf_module_reloc_section(elf_module_t *elf, Elf_Shdr *shdr) {
    int n = shdr->sh_size / sizeof(Elf_Rel), status = -EME_UNSUPPORTED;
    Elf_Sym *symtab = (Elf_Sym *) elf->header + elf->symtab->sh_offset;

    for ( Elf_Rel *rel = (Elf_Rel *) elf->header + shdr->sh_offset
		, 		  *end = (Elf_Rel *) &rel[n]
		; rel < end
		; rel++)
	{
		Elf_Addr *ptr = (Elf_Addr *) ((intptr_t *) (((intptr_t *) elf->start) +
													((intptr_t) elf->sections[shdr->sh_info].sh_addr) +
													((intptr_t) rel->r_offset)));
		
		Elf_Sym *sym = &symtab[ELF_REL_SYM(rel->r_info)];
		
		switch (ELF_REL_TYPE(rel->r_info)) {
		case R_386_32:
			*ptr += sym->st_value;
			break;

		case R_386_PC32:
			*ptr += sym->st_value - (Elf_Addr) ((intptr_t) ptr);
			break;
		
		default:
			goto elf_reloc_finished;
		}
    }
    
    status = 0;
    
elf_reloc_finished:    
    return status;
}

int elf_module_reloca_section(elf_module_t *elf, Elf_Shdr *shdr) {
    return -EME_UNSUPPORTED;
}
