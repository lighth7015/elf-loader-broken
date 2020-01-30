/*
 * ELF loadable modules
 *
 * 12 Feb 2011, Yury Ossadchy
 */
#include <stdio.h>
#include <string.h>
#include "elf-module-private.h"

size_t elf_module_get_size(elf_module_t *elf) { return elf->size; }
void *elf_module_get_data(elf_module_t *elf) { return elf->data; }
void elf_module_set_data(elf_module_t *elf, void *data) { elf->data = data; }
static char *elf_module_sym_name(elf_module_t *elf, int offs) { return elf->names + offs; }

static void elf_module_layout(elf_module_t *elf) {
	Elf_Shdr *shdr;
	int i = 1;
	
	for ( shdr = &elf->sections[i]; 
		i < elf->header->e_shnum; 
		i++, shdr = &elf->sections[i])
	{
		if (shdr->sh_flags & SHF_ALLOC) {
			shdr->sh_addr = shdr->sh_addralign? 
				(elf->size + shdr->sh_addralign - 1) & ~(shdr->sh_addralign - 1): 
				elf->size;
			
			elf->size = shdr->sh_addr + shdr->sh_size;
		}
	}
}

static void *elf_module_get_ptr(elf_module_t *elf, Elf_Addr addr){
	return (void *) (((intptr_t) elf->start) + ((intptr_t) addr));
}

static void *elf_module_sec_ptr(elf_module_t *elf, Elf_Shdr *shdr) {
	return (void *)(((intptr_t) elf->header) + (intptr_t) shdr->sh_offset);
}

static int elf_module_reloc(elf_module_t *elf) {
	int i;
	Elf_Shdr *shdr;
	
	for ( i = 0, shdr = &elf->sections[i];
		i < elf->header->e_shnum;
		i++,
		shdr = &elf->sections[i]) 
	{
		switch (shdr->sh_type) {
		case SHT_REL:
			elf_module_reloc_section(elf, shdr);
			break;
		
		case SHT_RELA:
			elf_module_reloca_section(elf, shdr);
			break;
		}
		shdr++;
	}
	return 0;
}

static int 
elf_module_link(elf_module_t *elf, elf_module_link_cbs_t *cbs) {
	int result = -EME_NOEXEC;
	int n = elf->symtab->sh_size / sizeof(Elf_Sym);
	
	Elf_Sym *symtab = elf_module_sec_ptr(elf, elf->symtab);

	for ( Elf_Sym *sym = &symtab[1]
		, *end = &symtab[n]
		; sym < end
		; sym++)
	{
		switch (sym->st_shndx)
		{
		case SHN_COMMON:
			goto finished;

		case SHN_ABS:
			break;
		
		/* resolve external symbol */
		case SHN_UNDEF:
			sym->st_value = (Elf_Addr) (intptr_t) 
				cbs->resolve( elf, elf_module_sym_name(elf, sym->st_name));
			
			if (!sym->st_value) {
				result = -EME_UNDEFINED_REFERENCE;
				goto finished;
			}
			
			break;
		/* bind to physical section location and define as accessible symbol */
		default:
			sym->st_value += (Elf_Addr) (intptr_t) 
				elf_module_get_ptr( elf, elf->sections[sym->st_shndx].sh_addr);
			
			if (ELF_SYM_TYPE(sym->st_info) != STT_SECTION) {
				result = cbs->define(elf, elf_module_sym_name(elf, sym->st_name),
							  (void *) (intptr_t) sym->st_value);
				
				if (result < 0) {
					goto finished;
				}
			}
		}
	}
	
finished:
	return result;
}

int elf_module_init(elf_module_t *elf, void *data, size_t size) {
	int i = 1, result = -EME_NOEXEC;
	elf->header = data;
	
	if (!memcmp(elf->header->e_ident, ELF_MAGIC, sizeof(ELF_MAGIC) - 1)
	&& elf_module_check_machine(elf))
	{	
		elf->sections = (void *) (((intptr_t) data) + ((intptr_t) elf->header->e_shoff));
		elf->strings = (void *) (((intptr_t) data) + ((intptr_t) elf->sections[elf->header->e_shstrndx].sh_offset));
		
		elf->size = 0;
		
		/* section 0 is reserved */
		for ( Elf_Shdr *shdr = &elf->sections[i]
			; i < elf->header->e_shnum
			; i++
			, shdr = &elf->sections[i])
		{
			if (shdr->sh_type == SHT_SYMTAB) {
				elf->symtab = &elf->sections[i];
				elf->strtab = &elf->sections[elf->sections[i].sh_link];
				elf->names = (void *) (((intptr_t) data) +
					((intptr_t) elf->strtab->sh_offset));
			}
		}
		
		elf_module_layout(elf);
		result = 0;
	}
	
	return result;
}

int elf_module_load(elf_module_t *elf, void *dest, elf_module_link_cbs_t *cbs) {
	elf->start = dest;
	Elf_Shdr *shdr;
	
	int i = 1, res = 0;
	
	for ( shdr = &elf->sections[i]
		; i < elf->header->e_shnum
		; i++
		, shdr = &elf->sections[i])
	{	
		if (shdr->sh_flags & SHF_ALLOC) {
			void* address = (void *) (((intptr_t) elf->header) + ((intptr_t) shdr->sh_offset));
			memcpy(elf_module_get_ptr(elf, shdr->sh_addr), address, shdr->sh_size);
		}
	}
	
	if ((res = elf_module_link(elf, cbs)) >= 0) {
		res = elf_module_reloc(elf);
	}
	
	return res;
}

void *elf_module_lookup_symbol(elf_module_t *elf, char *name) {
	int n = elf->symtab->sh_size / sizeof(Elf_Sym);
	Elf_Sym *sym = (void *) (((intptr_t) elf->header) + 
							 ((intptr_t) elf->symtab->sh_offset) + 
							 ((intptr_t) sizeof(Elf_Sym)));
	
	for ( int i = 1
		; i < n
		; i++
		, sym++) 
	{
		switch (sym->st_shndx) {
		case SHN_ABS:
			break;

		case SHN_UNDEF:
			break;

		default:
			if (!strcmp(elf_module_sym_name(elf, sym->st_name), name))
				return (void *) (intptr_t) sym->st_value;
		}
	}
	return NULL;
}
