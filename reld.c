#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#ifndef _WIN32
#include <sys/mman.h>
#endif
#include <elf-module.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"

#ifdef DEBUGGING
# define DPRINTF(module, fmt, ...) debug_print(module, fmt, 0, ## __VA_ARGS__)
# define ABORT_MSG(module, fmt, ...) debug_print(module, fmt, 1, ## __VA_ARGS__)
#else
# define ABORT_MSG(module, fmt, ...) debug_print(module, fmt, 1, ## __VA_ARGS__)
# define DPRINTF(x, ...) do {} while (0)
#endif

#pragma clang diagnostic pop

static void* nullptr = NULL;

__attribute__((section("_GLOBAL_OFFSET_TABLE_")))
intptr_t global_offset_table;

/* Module table entry */
typedef struct module {
    struct module *next;
    char *name;
} module_t;

/* Symbol table entry */
typedef struct symbol {
    struct symbol *next;
    module_t *module;
    void *addr;
    char *name;
} symbol_t;

/* Symbol table */
static symbol_t *symtab = NULL;

/* Module table */
static module_t core_module = {
    .name = "core"
};

static module_t *modtab = &core_module;
static module_t* loader = &core_module;

static void 
debug_print(module_t* module, char *fmt, int code, ...) {
	char *buffer, *line;
	va_list args;
	
	va_start(args, code);
	int length = vsnprintf(nullptr, 0, fmt, args);

	buffer = calloc(sizeof(char), length);
	vsprintf(buffer, fmt, args);
	
	const char* format = code > 0
		? "[ module: %-24s] ERROR: %s"
		: "[ module: %-24s] %s";
	
	va_end(args);
	
	length = snprintf( nullptr, 0, format, module->name, buffer );
	line = calloc(sizeof(char), ++length );
	
	snprintf( line, length, format, module->name, buffer );
	free(buffer);
	
	puts(line);
	free(line);
	
	if (code > 0) {
		exit(1);
	}
}

static void *load_file(char *file, size_t *size)
{
    FILE *f = fopen(file, "rb");
    void *result;

    if (!f)
		return NULL;

    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    result = malloc(*size);

    if (!result)
		goto out;

    if (fread(result, 1, *size, f) < *size) {
		free(result);
		result = NULL;
		goto out;
    }

out:
    fclose(f);
    return result;
}

/*
 * Define new symbol. If elf is NULL, it's core API function.
 */
static int 
define_symbol(elf_module_t *elf, char *name, void *addr) {
	module_t *module = elf
		? elf_module_get_data(elf)
		: &core_module;
	
	if (name[0] != '.'
	&& (strcmp(name, "_GLOBAL_OFFSET_TABLE_") != 0)) {
		DPRINTF(module, "Exporting symbol '%s' (defined in module %s)", name, module->name);
	}
	
	symbol_t *symbol,
			 *current = nullptr;
	
	for ( symbol = symtab
		; symbol
		; symbol = symbol->next) 
	{	
		if (symbol->name[0] != '.' && name[0] != '.'
		&& (strcmp(symbol->name, "_GLOBAL_OFFSET_TABLE_") != 0)) {
			if (!strcmp(symbol->name, name)) {
				ABORT_MSG(module, "Duplicate definition of `%s'.", name);
			}
			else {
				DPRINTF(loader, "Symbol: (%s@%s)", symbol->name, name);
			}
		}
	}
	
	current = (symbol_t *)
		malloc(sizeof(*symbol) + strlen(name) + 1);
	
	current->name = strdup(name);
	current->addr = addr;
	
	current->module = module;
	current->next = symtab;
	
	symtab = current;
    return 0;
}

static symbol_t *lookup_symbol(char *name)
{
	symbol_t *symbol = nullptr;
    
	for ( symbol_t *handle = symtab
		; handle && !symbol
		; handle = handle->next)
	{	
		if (handle->name[0] != '.' && name[0] != '.'
		&& (strcmp(handle->name, "_GLOBAL_OFFSET_TABLE_") != 0)
		&& (strcmp(name, "_GLOBAL_OFFSET_TABLE_") != 0)) {
			DPRINTF( loader, "Symbol name: (%s, %s)", name, handle->name);
		}
		
		if ( strcmp(handle->name, name) == 0
		&& ( handle->name[0] != '.' )) {
			symbol = handle;
		}
	}
	
sym_finished:	
    return symbol;
}

static module_t *lookup_module(char *name)
{
	module_t *module = nullptr;
    
	for ( module_t *handle = modtab
		; handle && !module
		; handle = handle->next)
	{	
		if (strcmp(handle->name, name) == 0) {
			DPRINTF(loader, "Module found: %s", handle->name);
			module = handle;
		}
	}
	
mod_finished:	
    return module;
}

static void load_module(char *module);

/*
 * Resolve symbol callback.
 *
 * If symbol is external(starts with 'MODULENAME__'), calls
 * load_module, if needed.
 */
static void *resolve_symbol(elf_module_t *elf, char *name) {
    char *ptr, *modname;
	symbol_t *sym;

    /* load module, if needed */
	if (name[0] != '.' 
	&& (strcmp(name, "_GLOBAL_OFFSET_TABLE_") != 0)) {
		DPRINTF( loader, "resolve symbol: %s", name);
	}
    
	if ((ptr = strstr(name, "__")) != NULL) {
		modname = malloc(ptr - name + 1);
		memset( modname, '\0', ptr - name + 1);
		
		strncpy(modname, name, ptr - name);
		DPRINTF( loader, "Locating external symbol [%s@%s].", name, modname);
		
		if (strcmp(name, modname) && !lookup_module(modname)) {
			load_module(modname);
		}
    }
	
	if (( sym = lookup_symbol( name ))) {
		if (strcmp( name, "_GLOBAL_OFFSET_TABLE_" ) != 0) {
			DPRINTF( loader, "%s found in %s", name, sym->module->name);
		}
		
		return sym->addr;
	}
    else {
		DPRINTF( loader, "Undefined symbol: %s (0x%08x)", name, sym );
    }
	
	return NULL;
}

static void *allocate(size_t size) {
#if _WIN32
    return malloc(size);
#else
    return mmap(NULL, size,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
}

void hexdump(const void* data, size_t size) {
	char ascii[17];
	size_t i, j;
	
	ascii[16] = '\0';
	
	printf(" %08x  ", 0);
	
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			printf(" ");
			if ((i+1) % 16 == 0) {
				printf(" | %s |\n %08x  ", ascii, i + 1);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf(" | %s |\n X", ascii);
			}
		}
	}
	
	puts("");
}

static void load_module(char *module) {
    int err;
    size_t size;
    void *core;
    char fullname[128];
    elf_module_t elf;
    module_t *mod =
    	(module_t *) calloc(1, sizeof(module_t));

    elf_module_link_cbs_t link = {
		.define = define_symbol,
		.resolve = resolve_symbol,
    };
    void *data;

    DPRINTF( loader, "Loading module %s.gz", module);
    mod->name = module;

    sprintf(fullname, "%s.mod", module);
	uint32_t result = 0;

    if (!( data = load_file( fullname, &size ))) {
		ABORT_MSG( loader, "Error loading module '%s'", module);
	}
    
    if (elf_module_init(&elf, data, size) < 0) {
		hexdump(data, size);
		ABORT_MSG( loader, "Error loading module '%s': Not an executable.", fullname);
    }
    
    elf_module_set_data(&elf, mod);

    if (!( core = allocate( size ))) {
		ABORT_MSG( loader, "Error loading module '%s': Out of memory.", fullname);
	}
	
    if (( result = elf_module_load( &elf, core, &link )) < 0) {
		ABORT_MSG( loader, "Error loading module '%s': Error %d", result );
	}
    
    free(data);
    mod->next = modtab;
	
    modtab = mod;
}

typedef void (*module_init)(void);

int main(int argc, char **argv) {
    module_init module_main;
	
	if (argc < 2) {
		DPRINTF( loader, "");
		DPRINTF( loader, "ELF Module Loader");
		DPRINTF( loader, "");
		DPRINTF( loader, "Usage: <module__function> [ options ] [ mod1 mod2 mod3 ... ]");
	}
	else {
		/* standard definitions(core API) */
		define_symbol(NULL, "_GLOBAL_OFFSET_TABLE_", ((void *) global_offset_table));
		define_symbol(NULL, "printf", ((void *) printf));
		
		/* load and link modules and set options, if any */
		for ( int i = 2; i < argc; i++) {	
			if (strcmp(argv[i], "-v") != 0) {
				load_module(argv[i]);
			}
		}
		
		/* call module_main */
		if ((( module_main = (module_init) resolve_symbol( nullptr, argv[1] )))) {
			DPRINTF( loader, "Executing module proc. at %p", module_main );
			//module_main();
		}
		else {
			DPRINTF( loader, "Unable to find symbol '%s'.", argv[1] );
		}
		
	}
    return 0;
}
