/*
 * Copyright (c) 2025 amigazen project
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GenMaki - Makefile Format Converter
 * 
 * This tool converts between different Amiga makefile formats:
 * - GNU Makefiles (makefile, Makefile, GNUmakefile)
 * - SAS/C SMakefiles (smakefile, SMakefile)
 * - DICE dmakefiles (dmakefile, Dmakefile, DMAKEFILE)
 * - Lattice LMKFILES (lmkfile, LMKFILE)
 * 
 * Supports conversion between all formats with intelligent format detection
 * and comprehensive syntax mapping.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <utility/tagitem.h>
#include <utility/utility.h>
#include <ctype.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>

/* Version and stack information - referenced to avoid warnings */
static const char *verstag = "$VER: GenMaki 1.0 (01/09/25)";
static const char *stack_cookie = "$STACK: 4096";

/* Global library bases */
struct Library *UtilityBase = NULL;

/* Version strings are referenced by the linker */

/* Maximum sizes and limits */
#define MAX_LINE_LENGTH 512
#define MAX_FILENAME_LENGTH 256
#define MAX_VARIABLES 64
#define MAX_RULES 128
#define MAX_COMMANDS 256

/* Makefile format types */
typedef enum {
    FORMAT_UNKNOWN = 0,
    FORMAT_GNU_MAKE,
    FORMAT_SAS_C,
    FORMAT_DICE,
    FORMAT_LATTICE
} MakefileFormat;

/* Variable structure */
typedef struct {
    STRPTR name;
    STRPTR value;
    BOOL is_immediate;  /* For DICE immediate resolution */
} Variable;

/* Command structure */
typedef struct {
    STRPTR command;
    BOOL is_continuation;
} Command;

/* Rule structure */
typedef struct {
    STRPTR targets;
    STRPTR dependencies;
    Command *commands;
    LONG command_count;
    BOOL is_pattern_rule;
    BOOL is_dice_form4;  /* DICE :: syntax */
} Rule;

/* Makefile structure */
typedef struct {
    MakefileFormat format;
    STRPTR filename;
    Variable *variables;
    LONG variable_count;
    Rule *rules;
    LONG rule_count;
    STRPTR *comments;
    LONG comment_count;
} Makefile;

/* Conversion configuration */
typedef struct {
    STRPTR input_file;
    STRPTR output_file;
    STRPTR filetype;
    MakefileFormat target_format;
    BOOL save_to_file;  /* Derived from presence of output_file */
    BOOL verbose;
} Config;

/* Function prototypes */
LONG my_strlen(const char *str);
STRPTR my_strdup(const char *str);
void my_strcpy(char *dest, const char *src);
LONG my_strcmp(const char *s1, const char *s2);
LONG my_stricmp(const char *s1, const char *s2);
char *trim_whitespace(char *str);
char *skip_whitespace(char *str);

/* File detection and format identification */
STRPTR find_makefile(void);
MakefileFormat detect_format(STRPTR filename);
MakefileFormat parse_filetype_string(STRPTR filetype);
STRPTR format_to_string(MakefileFormat format);

/* Parsing functions */
BOOL parse_makefile(STRPTR filename, Makefile *makefile);
BOOL parse_gnu_makefile(BPTR file, Makefile *makefile);
BOOL parse_sas_makefile(BPTR file, Makefile *makefile);
BOOL parse_dice_makefile(BPTR file, Makefile *makefile);
BOOL parse_lattice_makefile(BPTR file, Makefile *makefile);

/* Conversion functions */
BOOL convert_makefile(Makefile *source, MakefileFormat target_format, STRPTR output_file);
BOOL convert_to_gnu_make(Makefile *source, BPTR output);
BOOL convert_to_sas_make(Makefile *source, BPTR output);
BOOL convert_to_dice_make(Makefile *source, BPTR output);
BOOL convert_to_lattice_make(Makefile *source, BPTR output);

/* Utility functions */
void cleanup_makefile(Makefile *makefile);
void cleanup_config(Config *config);
void print_usage(void);
BOOL validate_config(Config *config);
STRPTR map_compiler_option(STRPTR option, MakefileFormat from, MakefileFormat to);
STRPTR map_command(STRPTR command, MakefileFormat from, MakefileFormat to);
STRPTR convert_cflags(STRPTR flags, MakefileFormat from, MakefileFormat to);

/* Simple string functions since utility.library doesn't have all we need */
LONG my_strlen(const char *str)
{
    const char *s = str;
    while (*s) s++;
    return s - str;
}

STRPTR my_strdup(const char *str)
{
    LONG len = my_strlen(str);
    STRPTR result = AllocVec(len + 1, MEMF_CLEAR);
    if (result) {
        my_strcpy((char *)result, str);
    }
    return result;
}

void my_strcpy(char *dest, const char *src)
{
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

LONG my_strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

LONG my_stricmp(const char *s1, const char *s2)
{
    while (*s1 && (tolower(*s1) == tolower(*s2))) {
        s1++;
        s2++;
    }
    return tolower(*(unsigned char *)s1) - tolower(*(unsigned char *)s2);
}

char *trim_whitespace(char *str)
{
    char *end;
    while (*str == ' ' || *str == '\t') str++;
    if (*str == 0) return str;
    end = str + my_strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t')) end--;
    *(end+1) = 0;
    return str;
}

char *skip_whitespace(char *str)
{
    while (*str == ' ' || *str == '\t') str++;
    return str;
}

int main(int argc, char *argv[])
{
    struct RDArgs *rda;
    Config config;
    Makefile source_makefile;
    STRPTR found_file = NULL;
    LONG retcode = RETURN_OK;
    
    /* Basic debug output - will be controlled by verbose flag after parsing */
    
    /* Initialize configuration */
    {
        UBYTE *ptr = (UBYTE *)&config;
        LONG i;
        for (i = 0; i < sizeof(Config); i++) {
            ptr[i] = 0;
        }
    }
    
    /* Initialize makefile structure */
    {
        UBYTE *ptr = (UBYTE *)&source_makefile;
        LONG i;
        for (i = 0; i < sizeof(Makefile); i++) {
            ptr[i] = 0;
        }
    }
    
    /* Parse command line arguments */
    {
        static UBYTE template[] = "FROM/K,TO/K,FILETYPE/K,VERBOSE/S,HELP/S";
        LONG args[5] = {0, 0, 0, 0, 0}; /* from, to, filetype, verbose, help */
        rda = ReadArgs(template, args, NULL);
        
        /* Check if help was requested */
        if (args[4] != 0) {
            print_usage();
            return RETURN_OK;
        }
        
        /* Check for valid arguments */
        if (!rda) {
            Printf("GenMaki: Invalid arguments\n");
            return RETURN_ERROR;
        }
        
        config.input_file = (STRPTR)args[0];
        config.output_file = (STRPTR)args[1];
        config.filetype = (STRPTR)args[2];
        config.verbose = (args[3] != 0);
        
        if (config.verbose) {
            Printf("GenMaki: ReadArgs successful\n");
        }
        
        /* Debug: Show what arguments were parsed */
        if (config.verbose) {
            Printf("GenMaki: Parsed arguments - FROM=%s, TO=%s, FILETYPE=%s\n", 
                   config.input_file ? (char *)config.input_file : "NULL",
                   config.output_file ? (char *)config.output_file : "NULL", 
                   config.filetype ? (char *)config.filetype : "NULL");
        }
        /* Save to file is implied by presence of TO argument */
        config.save_to_file = (args[1] != 0);
    }
    
    /* Open required libraries */
    if (config.verbose) {
        Printf("GenMaki: Opening utility.library...\n");
    }
    UtilityBase = (struct Library *)OpenLibrary("utility.library", 0);
    
    if (!UtilityBase) {
        Printf("GenMaki: Failed to open utility.library\n");
        goto cleanup;
    }
    
    if (config.verbose) {
        Printf("GenMaki: Library opened successfully\n");
    }
    
    /* Find input makefile if not specified */
    if (!config.input_file) {
        if (config.verbose) {
            Printf("GenMaki: No input file specified, searching for makefiles...\n");
        }
        found_file = find_makefile();
        if (!found_file) {
            Printf("GenMaki: No makefile found in current directory\n");
            Printf("GenMaki: Use FROM=filename to specify a makefile, or HELP for usage\n");
            retcode = RETURN_ERROR;
            goto cleanup;
        }
        config.input_file = found_file;
        if (config.verbose) {
            Printf("GenMaki: Found makefile: %s\n", config.input_file);
        }
    }
    
    /* Detect source format */
    if (config.verbose) {
        Printf("GenMaki: Detecting format of '%s'...\n", config.input_file);
    }
    source_makefile.format = detect_format(config.input_file);
    if (source_makefile.format == FORMAT_UNKNOWN) {
        Printf("GenMaki: Unable to determine makefile format for '%s'\n", config.input_file);
        retcode = RETURN_ERROR;
        goto cleanup;
    }
    
    if (config.verbose) {
        Printf("GenMaki: Detected source format: %s\n", format_to_string(source_makefile.format));
        Printf("GenMaki: Found %ld variables, %ld rules\n", 
               source_makefile.variable_count, source_makefile.rule_count);
    }
    
    /* Determine target format */
    if (config.filetype) {
        config.target_format = parse_filetype_string(config.filetype);
        if (config.target_format == FORMAT_UNKNOWN) {
            Printf("GenMaki: Unknown target format '%s'\n", config.filetype);
            retcode = RETURN_ERROR;
            goto cleanup;
        }
    } else {
        /* Use default conversion targets */
        switch (source_makefile.format) {
            case FORMAT_GNU_MAKE:
            case FORMAT_LATTICE:
                config.target_format = FORMAT_SAS_C;
                break;
            case FORMAT_DICE:
            case FORMAT_SAS_C:
                config.target_format = FORMAT_GNU_MAKE;
                break;
            default:
                Printf("GenMaki: No default target format for source format\n");
                retcode = RETURN_ERROR;
                goto cleanup;
        }
    }
    
    if (config.verbose) {
        Printf("GenMaki: Target format: %s\n", format_to_string(config.target_format));
    }
    
    /* Parse source makefile */
    if (!parse_makefile(config.input_file, &source_makefile)) {
        Printf("GenMaki: Failed to parse makefile '%s'\n", config.input_file);
        retcode = RETURN_ERROR;
        goto cleanup;
    }
    
    if (config.verbose) {
        Printf("GenMaki: Parsed %ld variables and %ld rules\n", 
               source_makefile.variable_count, source_makefile.rule_count);
    }
    
    /* Determine output file */
    if (config.save_to_file && !config.output_file) {
        /* Generate default output filename based on target format */
        /* Note: Amiga filesystems are case insensitive, so exact case doesn't matter */
        STRPTR default_name = NULL;
        switch (config.target_format) {
            case FORMAT_GNU_MAKE:
                default_name = "Makefile";
                break;
            case FORMAT_SAS_C:
                default_name = "smakefile";
                break;
            case FORMAT_DICE:
                default_name = "dmakefile";
                break;
            case FORMAT_LATTICE:
                default_name = "lmkfile";
                break;
        }
        config.output_file = default_name;
    }
    
    /* Convert makefile */
    if (config.verbose) {
        Printf("GenMaki: Starting conversion...\n");
    }
    
    if (!convert_makefile(&source_makefile, config.target_format, config.output_file)) {
        Printf("GenMaki: Failed to convert makefile\n");
        retcode = RETURN_ERROR;
        goto cleanup;
    }
    
    if (config.verbose) {
        Printf("GenMaki: Conversion completed successfully\n");
    }
    
    if (config.save_to_file) {
        Printf("GenMaki: Successfully converted to '%s'\n", config.output_file);
    } else {
        Printf("GenMaki: Conversion completed\n");
    }
    
cleanup:
    /* Cleanup */
    cleanup_makefile(&source_makefile);
    cleanup_config(&config);
    
    if (found_file) {
        FreeVec(found_file);
    }
    
    if (rda) {
        FreeArgs(rda);
    }
    
    if (UtilityBase) CloseLibrary(UtilityBase);
    
    return retcode;
}

STRPTR find_makefile(void)
{
    /* Standard makefile names to look for (Amiga filesystems are case insensitive) */
    STRPTR candidates[] = {
        "makefile", "Makefile", "MAKEFILE", "GNUmakefile",
        "smakefile", "SMakefile", "SMAKEFILE",
        "dmakefile", "Dmakefile", "DMAKEFILE",
        "lmkfile", "LMKFILE"
    };
    LONG num_candidates = sizeof(candidates) / sizeof(candidates[0]);
    STRPTR found_files[16];
    LONG found_count = 0;
    LONG i;
    BPTR file;
    
    /* Initialize found_files array */
    for (i = 0; i < 16; i++) {
        found_files[i] = NULL;
    }
    
    /* Scan for all possible makefile names */
    /* Note: Amiga Open() function is case insensitive, so we include common variations */
    for (i = 0; i < num_candidates; i++) {
        file = Open(candidates[i], MODE_OLDFILE);
        if (file) {
            Close(file);
            if (found_count < 16) {
                found_files[found_count] = my_strdup(candidates[i]);
                found_count++;
            }
        }
    }
    
    if (found_count == 0) {
        return NULL;
    }
    
    if (found_count == 1) {
        STRPTR result = found_files[0];
        if (result) {
            return result;
        }
    }
    
    /* Multiple makefiles found */
    Printf("GenMaki: Multiple makefiles found in current directory:\n");
    for (i = 0; i < found_count; i++) {
        Printf("  %s\n", found_files[i]);
        FreeVec(found_files[i]);
    }
    Printf("GenMaki: Please specify which file to convert using FROM=filename\n");
    
    return NULL;
}

MakefileFormat detect_format(STRPTR filename)
{
    BPTR file;
    UBYTE line[MAX_LINE_LENGTH];
    MakefileFormat format = FORMAT_UNKNOWN;
    BOOL found_gnu_syntax = FALSE;
    BOOL found_dice_syntax = FALSE;
    BOOL found_sas_syntax = FALSE;
    BOOL found_lattice_syntax = FALSE;
    LONG line_count = 0;
    char *trimmed;
    
    file = Open(filename, MODE_OLDFILE);
    if (!file) {
        return FORMAT_UNKNOWN;
    }
    
    /* Scan first 50 lines for format-specific syntax */
    while (FGets(file, line, sizeof(line)) && line_count < 50) {
        trimmed = trim_whitespace((char *)line);
        
        /* Skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == '#') {
            line_count++;
            continue;
        }
        
        /* Check for GNU Make specific syntax */
        if (strstr(trimmed, "%.o:") || strstr(trimmed, "$@") || 
            strstr(trimmed, "$<") || strstr(trimmed, "$^") ||
            strstr(trimmed, "CC=gcc") || strstr(trimmed, "CC = gcc")) {
            found_gnu_syntax = TRUE;
        }
        
        /* Check for DICE specific syntax */
        if (strstr(trimmed, "%(left)") || strstr(trimmed, "%(right)") ||
            strstr(trimmed, "::")) {
            found_dice_syntax = TRUE;
        }
        
        /* Check for SAS/C specific syntax */
        if (strstr(trimmed, ".c.o:") || strstr(trimmed, "$*.o") ||
            strstr(trimmed, "OBJNAME=") || strstr(trimmed, "slink")) {
            found_sas_syntax = TRUE;
        }
        
        /* Check for Lattice specific syntax */
        if (strstr(trimmed, "blink") || strstr(trimmed, "lc ") ||
            strstr(trimmed, "WITH")) {
            found_lattice_syntax = TRUE;
        }
        
        line_count++;
    }
    
    Close(file);
    
    /* Determine format based on found syntax */
    if (found_dice_syntax) {
        format = FORMAT_DICE;
    } else if (found_gnu_syntax) {
        format = FORMAT_GNU_MAKE;
    } else if (found_sas_syntax) {
        format = FORMAT_SAS_C;
    } else if (found_lattice_syntax) {
        format = FORMAT_LATTICE;
    }
    
    return format;
}

MakefileFormat parse_filetype_string(STRPTR filetype)
{
    if (my_stricmp(filetype, "smake") == 0 || my_stricmp(filetype, "smakefile") == 0 ||
        my_stricmp(filetype, "sasc") == 0) {
        return FORMAT_SAS_C;
    }
    
    if (my_stricmp(filetype, "dmake") == 0 || my_stricmp(filetype, "dmakefile") == 0 ||
        my_stricmp(filetype, "dice") == 0) {
        return FORMAT_DICE;
    }
    
    if (my_stricmp(filetype, "makefile") == 0 || my_stricmp(filetype, "make") == 0 ||
        my_stricmp(filetype, "gnumakefile") == 0 || my_stricmp(filetype, "gnu") == 0 ||
        my_stricmp(filetype, "gcc") == 0) {
        return FORMAT_GNU_MAKE;
    }
    
    if (my_stricmp(filetype, "lmk") == 0 || my_stricmp(filetype, "lmkfile") == 0 ||
        my_stricmp(filetype, "lattice") == 0) {
        return FORMAT_LATTICE;
    }
    
    return FORMAT_UNKNOWN;
}

STRPTR format_to_string(MakefileFormat format)
{
    switch (format) {
        case FORMAT_GNU_MAKE: return "GNU Make";
        case FORMAT_SAS_C: return "SAS/C";
        case FORMAT_DICE: return "DICE";
        case FORMAT_LATTICE: return "Lattice";
        default: return "Unknown";
    }
}

BOOL parse_makefile(STRPTR filename, Makefile *makefile)
{
    BPTR file;
    BOOL success = FALSE;
    
    file = Open(filename, MODE_OLDFILE);
    if (!file) {
        return FALSE;
    }
    
    makefile->filename = my_strdup(filename);
    makefile->variables = AllocVec(sizeof(Variable) * MAX_VARIABLES, MEMF_CLEAR);
    makefile->rules = AllocVec(sizeof(Rule) * MAX_RULES, MEMF_CLEAR);
    
    if (!makefile->variables || !makefile->rules) {
        Close(file);
        return FALSE;
    }
    
    /* Parse based on detected format */
    switch (makefile->format) {
        case FORMAT_GNU_MAKE:
            success = parse_gnu_makefile(file, makefile);
            break;
        case FORMAT_SAS_C:
            success = parse_sas_makefile(file, makefile);
            break;
        case FORMAT_DICE:
            success = parse_dice_makefile(file, makefile);
            break;
        case FORMAT_LATTICE:
            success = parse_lattice_makefile(file, makefile);
            break;
        default:
            success = FALSE;
            break;
    }
    
    Close(file);
    return success;
}

BOOL parse_gnu_makefile(BPTR file, Makefile *makefile)
{
    UBYTE line[MAX_LINE_LENGTH];
    BOOL in_rule = FALSE;
    Rule *current_rule = NULL;
    char *trimmed;
    char *equals;
    char *name;
    char *value;
    char *colon;
    char *targets;
    char *deps;
    char *command;
    
    while (FGets(file, line, sizeof(line))) {
        trimmed = trim_whitespace((char *)line);
        
        /* Skip empty lines */
        if (*trimmed == '\0') {
            in_rule = FALSE;
            current_rule = NULL;
            continue;
        }
        
        /* Handle comments */
        if (*trimmed == '#') {
            /* Store comment */
            if (makefile->comment_count < MAX_COMMANDS) {
                makefile->comments = AllocVec(sizeof(STRPTR) * MAX_COMMANDS, MEMF_CLEAR);
                if (makefile->comments) {
                    makefile->comments[makefile->comment_count] = my_strdup(trimmed);
                    makefile->comment_count++;
                }
            }
            continue;
        }
        
        /* Check for variable assignment */
        if (strchr(trimmed, '=') && !strchr(trimmed, ':')) {
            equals = strchr(trimmed, '=');
            if (equals) {
                *equals = '\0';
                name = trim_whitespace(trimmed);
                value = trim_whitespace(equals + 1);
                
                /* Remove quotes if present */
                if (*value == '"' && value[strlen(value)-1] == '"') {
                    value[strlen(value)-1] = '\0';
                    value++;
                }
                
                if (makefile->variable_count < MAX_VARIABLES) {
                    makefile->variables[makefile->variable_count].name = my_strdup(name);
                    makefile->variables[makefile->variable_count].value = my_strdup(value);
                    makefile->variables[makefile->variable_count].is_immediate = FALSE;
                    makefile->variable_count++;
                }
            }
            in_rule = FALSE;
            current_rule = NULL;
            continue;
        }
        
        /* Check for rule (target: dependencies) */
        if (strchr(trimmed, ':')) {
            colon = strchr(trimmed, ':');
            if (colon) {
                *colon = '\0';
                targets = trim_whitespace(trimmed);
                deps = trim_whitespace(colon + 1);
                
                if (makefile->rule_count < MAX_RULES) {
                    current_rule = &makefile->rules[makefile->rule_count];
                    current_rule->targets = my_strdup(targets);
                    current_rule->dependencies = my_strdup(deps);
                    current_rule->commands = AllocVec(sizeof(Command) * MAX_COMMANDS, MEMF_CLEAR);
                    current_rule->command_count = 0;
                    current_rule->is_pattern_rule = (strchr(targets, '%') != NULL);
                    current_rule->is_dice_form4 = FALSE;
                    makefile->rule_count++;
                    in_rule = TRUE;
                }
            }
            continue;
        }
        
        /* Handle command lines (must start with tab) */
        if (in_rule && current_rule && (*trimmed == '\t' || *trimmed == ' ')) {
            command = skip_whitespace(trimmed);
            if (*command && current_rule->command_count < MAX_COMMANDS) {
                current_rule->commands[current_rule->command_count].command = my_strdup(command);
                current_rule->commands[current_rule->command_count].is_continuation = FALSE;
                current_rule->command_count++;
            }
        } else {
            in_rule = FALSE;
            current_rule = NULL;
        }
    }
    
    return TRUE;
}

BOOL parse_sas_makefile(BPTR file, Makefile *makefile)
{
    UBYTE line[MAX_LINE_LENGTH];
    BOOL in_rule = FALSE;
    Rule *current_rule = NULL;
    char *trimmed;
    char *equals;
    char *name;
    char *value;
    char *colon;
    char *targets;
    char *deps;
    char *command;
    
    while (FGets(file, line, sizeof(line))) {
        trimmed = trim_whitespace((char *)line);
        
        /* Skip empty lines */
        if (*trimmed == '\0') {
            in_rule = FALSE;
            current_rule = NULL;
            continue;
        }
        
        /* Handle comments */
        if (*trimmed == ';') {
            /* Store comment */
            if (makefile->comment_count < MAX_COMMANDS) {
                makefile->comments = AllocVec(sizeof(STRPTR) * MAX_COMMANDS, MEMF_CLEAR);
                if (makefile->comments) {
                    makefile->comments[makefile->comment_count] = my_strdup(trimmed);
                    makefile->comment_count++;
                }
            }
            continue;
        }
        
        /* Check for variable assignment */
        if (strchr(trimmed, '=') && !strchr(trimmed, ':')) {
            equals = strchr(trimmed, '=');
            if (equals) {
                *equals = '\0';
                name = trim_whitespace(trimmed);
                value = trim_whitespace(equals + 1);
                
                if (makefile->variable_count < MAX_VARIABLES) {
                    makefile->variables[makefile->variable_count].name = my_strdup(name);
                    makefile->variables[makefile->variable_count].value = my_strdup(value);
                    makefile->variables[makefile->variable_count].is_immediate = FALSE;
                    makefile->variable_count++;
                }
            }
            in_rule = FALSE;
            current_rule = NULL;
            continue;
        }
        
        /* Check for SAS/C pattern rule (.c.o:) */
        if (strstr(trimmed, ".c.o:") || strstr(trimmed, ".s.o:")) {
            if (makefile->rule_count < MAX_RULES) {
                current_rule = &makefile->rules[makefile->rule_count];
                current_rule->targets = my_strdup("*.o");
                current_rule->dependencies = my_strdup("*.c");
                current_rule->commands = AllocVec(sizeof(Command) * MAX_COMMANDS, MEMF_CLEAR);
                current_rule->command_count = 0;
                current_rule->is_pattern_rule = TRUE;
                current_rule->is_dice_form4 = FALSE;
                makefile->rule_count++;
                in_rule = TRUE;
            }
            continue;
        }
        
        /* Check for regular rule (target: dependencies) */
        if (strchr(trimmed, ':')) {
            colon = strchr(trimmed, ':');
            if (colon) {
                *colon = '\0';
                targets = trim_whitespace(trimmed);
                deps = trim_whitespace(colon + 1);
                
                if (makefile->rule_count < MAX_RULES) {
                    current_rule = &makefile->rules[makefile->rule_count];
                    current_rule->targets = my_strdup(targets);
                    current_rule->dependencies = my_strdup(deps);
                    current_rule->commands = AllocVec(sizeof(Command) * MAX_COMMANDS, MEMF_CLEAR);
                    current_rule->command_count = 0;
                    current_rule->is_pattern_rule = FALSE;
                    current_rule->is_dice_form4 = FALSE;
                    makefile->rule_count++;
                    in_rule = TRUE;
                }
            }
            continue;
        }
        
        /* Handle command lines (must start with tab) */
        if (in_rule && current_rule && (*trimmed == '\t' || *trimmed == ' ')) {
            command = skip_whitespace(trimmed);
            if (*command && current_rule->command_count < MAX_COMMANDS) {
                current_rule->commands[current_rule->command_count].command = my_strdup(command);
                current_rule->commands[current_rule->command_count].is_continuation = FALSE;
                current_rule->command_count++;
            }
        } else {
            in_rule = FALSE;
            current_rule = NULL;
        }
    }
    
    return TRUE;
}

BOOL parse_dice_makefile(BPTR file, Makefile *makefile)
{
    UBYTE line[MAX_LINE_LENGTH];
    BOOL in_rule = FALSE;
    Rule *current_rule = NULL;
    char *trimmed;
    char *equals;
    char *name;
    char *value;
    char *colon;
    char *targets;
    char *deps;
    char *command;
    char *double_colon;
    BOOL is_immediate;
    
    while (FGets(file, line, sizeof(line))) {
        trimmed = trim_whitespace((char *)line);
        
        /* Skip empty lines */
        if (*trimmed == '\0') {
            in_rule = FALSE;
            current_rule = NULL;
            continue;
        }
        
        /* Handle comments */
        if (*trimmed == '#') {
            /* Store comment */
            if (makefile->comment_count < MAX_COMMANDS) {
                makefile->comments = AllocVec(sizeof(STRPTR) * MAX_COMMANDS, MEMF_CLEAR);
                if (makefile->comments) {
                    makefile->comments[makefile->comment_count] = my_strdup(trimmed);
                    makefile->comment_count++;
                }
            }
            continue;
        }
        
        /* Check for variable assignment */
        if (strchr(trimmed, '=') && !strchr(trimmed, ':')) {
            equals = strchr(trimmed, '=');
            if (equals) {
                *equals = '\0';
                name = trim_whitespace(trimmed);
                value = trim_whitespace(equals + 1);
                
                /* DICE has immediate variable resolution */
                is_immediate = TRUE;
                
                if (makefile->variable_count < MAX_VARIABLES) {
                    makefile->variables[makefile->variable_count].name = my_strdup(name);
                    makefile->variables[makefile->variable_count].value = my_strdup(value);
                    makefile->variables[makefile->variable_count].is_immediate = is_immediate;
                    makefile->variable_count++;
                }
            }
            in_rule = FALSE;
            current_rule = NULL;
            continue;
        }
        
        /* Check for DICE Form 4 rule (:: syntax) */
        if (strstr(trimmed, "::")) {
            double_colon = strstr(trimmed, "::");
            if (double_colon) {
                *double_colon = '\0';
                targets = trim_whitespace(trimmed);
                deps = trim_whitespace(double_colon + 2);
                
                if (makefile->rule_count < MAX_RULES) {
                    current_rule = &makefile->rules[makefile->rule_count];
                    current_rule->targets = my_strdup(targets);
                    current_rule->dependencies = my_strdup(deps);
                    current_rule->commands = AllocVec(sizeof(Command) * MAX_COMMANDS, MEMF_CLEAR);
                    current_rule->command_count = 0;
                    current_rule->is_pattern_rule = FALSE;
                    current_rule->is_dice_form4 = TRUE;
                    makefile->rule_count++;
                    in_rule = TRUE;
                }
            }
            continue;
        }
        
        /* Check for regular rule (target: dependencies) */
        if (strchr(trimmed, ':')) {
            colon = strchr(trimmed, ':');
            if (colon) {
                *colon = '\0';
                targets = trim_whitespace(trimmed);
                deps = trim_whitespace(colon + 1);
                
                if (makefile->rule_count < MAX_RULES) {
                    current_rule = &makefile->rules[makefile->rule_count];
                    current_rule->targets = my_strdup(targets);
                    current_rule->dependencies = my_strdup(deps);
                    current_rule->commands = AllocVec(sizeof(Command) * MAX_COMMANDS, MEMF_CLEAR);
                    current_rule->command_count = 0;
                    current_rule->is_pattern_rule = FALSE;
                    current_rule->is_dice_form4 = FALSE;
                    makefile->rule_count++;
                    in_rule = TRUE;
                }
            }
            continue;
        }
        
        /* Handle command lines (must start with tab) */
        if (in_rule && current_rule && (*trimmed == '\t' || *trimmed == ' ')) {
            command = skip_whitespace(trimmed);
            if (*command && current_rule->command_count < MAX_COMMANDS) {
                current_rule->commands[current_rule->command_count].command = my_strdup(command);
                current_rule->commands[current_rule->command_count].is_continuation = FALSE;
                current_rule->command_count++;
            }
        } else {
            in_rule = FALSE;
            current_rule = NULL;
        }
    }
    
    return TRUE;
}

BOOL parse_lattice_makefile(BPTR file, Makefile *makefile)
{
    UBYTE line[MAX_LINE_LENGTH];
    UBYTE full_line[MAX_LINE_LENGTH * 4]; /* Buffer for multi-line variables */
    BOOL in_rule = FALSE;
    BOOL in_with_block = FALSE;
    BOOL in_continuation = FALSE;
    Rule *current_rule = NULL;
    char *trimmed;
    char *equals;
    char *name;
    char *value;
    char *colon;
    char *targets;
    char *deps;
    char *command;
    LONG line_len;
    LONG full_line_len;
    
    full_line[0] = '\0';
    full_line_len = 0;
    
    while (FGets(file, line, sizeof(line))) {
        line_len = strlen((char *)line);
        
        /* Handle line continuations */
        if (line_len > 0 && line[line_len - 1] == '\\') {
            /* Remove the backslash and append to full_line */
            line[line_len - 1] = '\0';
            if (full_line_len + line_len < sizeof(full_line) - 1) {
                strcat((char *)full_line, (char *)line);
                full_line_len += line_len - 1;
                in_continuation = TRUE;
                continue;
            }
        } else if (in_continuation) {
            /* End of continuation, append this line too */
            if (full_line_len + line_len < sizeof(full_line) - 1) {
                strcat((char *)full_line, (char *)line);
                full_line_len += line_len;
            }
            in_continuation = FALSE;
        } else {
            /* Regular line, use it directly */
            strcpy((char *)full_line, (char *)line);
            full_line_len = line_len;
        }
        
        trimmed = trim_whitespace((char *)full_line);
        
        /* Skip empty lines */
        if (*trimmed == '\0') {
            if (in_with_block) {
                in_with_block = FALSE;
            } else {
                in_rule = FALSE;
                current_rule = NULL;
            }
            continue;
        }
        
        /* Handle comments */
        if (*trimmed == ';') {
            /* Store comment */
            if (makefile->comment_count < MAX_COMMANDS) {
                makefile->comments = AllocVec(sizeof(STRPTR) * MAX_COMMANDS, MEMF_CLEAR);
                if (makefile->comments) {
                    makefile->comments[makefile->comment_count] = my_strdup(trimmed);
                    makefile->comment_count++;
                }
            }
            continue;
        }
        
        /* Check for WITH block (Lattice specific) */
        if (my_stricmp(trimmed, "WITH") == 0) {
            in_with_block = TRUE;
            continue;
        }
        
        /* Check for variable assignment */
        if (strchr(trimmed, '=') && !strchr(trimmed, ':')) {
            equals = strchr(trimmed, '=');
            if (equals) {
                *equals = '\0';
                name = trim_whitespace(trimmed);
                value = trim_whitespace(equals + 1);
                
                if (makefile->variable_count < MAX_VARIABLES) {
                    makefile->variables[makefile->variable_count].name = my_strdup(name);
                    makefile->variables[makefile->variable_count].value = my_strdup(value);
                    makefile->variables[makefile->variable_count].is_immediate = FALSE;
                    makefile->variable_count++;
                    
                    /* Debug output removed for production */
                }
            }
            in_rule = FALSE;
            current_rule = NULL;
            continue;
        }
        
        /* Check for Lattice pattern rule (.c.o:) */
        if (strstr(trimmed, ".c.o:") || strstr(trimmed, ".s.o:")) {
            if (makefile->rule_count < MAX_RULES) {
                current_rule = &makefile->rules[makefile->rule_count];
                current_rule->targets = my_strdup("*.o");
                current_rule->dependencies = my_strdup("*.c");
                current_rule->commands = AllocVec(sizeof(Command) * MAX_COMMANDS, MEMF_CLEAR);
                current_rule->command_count = 0;
                current_rule->is_pattern_rule = TRUE;
                current_rule->is_dice_form4 = FALSE;
                makefile->rule_count++;
                in_rule = TRUE;
            }
            continue;
        }
        
        /* Check for regular rule (target: dependencies) */
        if (strchr(trimmed, ':')) {
            colon = strchr(trimmed, ':');
            if (colon) {
                *colon = '\0';
                targets = trim_whitespace(trimmed);
                deps = trim_whitespace(colon + 1);
                
                if (makefile->rule_count < MAX_RULES) {
                    current_rule = &makefile->rules[makefile->rule_count];
                    current_rule->targets = my_strdup(targets);
                    current_rule->dependencies = my_strdup(deps);
                    current_rule->commands = AllocVec(sizeof(Command) * MAX_COMMANDS, MEMF_CLEAR);
                    current_rule->command_count = 0;
                    current_rule->is_pattern_rule = FALSE;
                    current_rule->is_dice_form4 = FALSE;
                    makefile->rule_count++;
                    in_rule = TRUE;
                }
            }
            continue;
        }
        
        /* Handle command lines (must start with tab) */
        if (in_rule && current_rule && (*trimmed == '\t' || *trimmed == ' ')) {
            command = skip_whitespace(trimmed);
            if (*command && current_rule->command_count < MAX_COMMANDS) {
                current_rule->commands[current_rule->command_count].command = my_strdup(command);
                current_rule->commands[current_rule->command_count].is_continuation = FALSE;
                current_rule->command_count++;
                
                /* Debug output removed for production */
            }
        } else if (in_with_block) {
            /* Handle WITH block content */
            if (makefile->rule_count > 0) {
                current_rule = &makefile->rules[makefile->rule_count - 1];
                if (current_rule->command_count < MAX_COMMANDS) {
                    current_rule->commands[current_rule->command_count].command = my_strdup(trimmed);
                    current_rule->commands[current_rule->command_count].is_continuation = FALSE;
                    current_rule->command_count++;
                }
            }
        } else {
            in_rule = FALSE;
            current_rule = NULL;
        }
        
        /* Reset buffer for next line */
        full_line[0] = '\0';
        full_line_len = 0;
    }
    
    return TRUE;
}

BOOL convert_makefile(Makefile *source, MakefileFormat target_format, STRPTR output_file)
{
    BPTR output;
    BOOL success = FALSE;
    
    if (output_file) {
        output = Open(output_file, MODE_NEWFILE);
        if (!output) {
            Printf("GenMaki: Failed to create output file '%s'\n", output_file);
            return FALSE;
        }
    } else {
        output = Output();
    }
    
    /* Convert based on target format */
    switch (target_format) {
        case FORMAT_GNU_MAKE:
            success = convert_to_gnu_make(source, output);
            break;
        case FORMAT_SAS_C:
            success = convert_to_sas_make(source, output);
            break;
        case FORMAT_DICE:
            success = convert_to_dice_make(source, output);
            break;
        case FORMAT_LATTICE:
            success = convert_to_lattice_make(source, output);
            break;
        default:
            success = FALSE;
            break;
    }
    
    if (output_file) {
        Close(output);
    }
    
    return success;
}

BOOL convert_to_gnu_make(Makefile *source, BPTR output)
{
    LONG i, j;
    
    /* Write header comment */
    FPrintf(output, "# Converted to GNU Make format from %s\n", format_to_string(source->format));
    FPrintf(output, "# Generated by GenMaki\n\n");
    
    /* Convert variables */
    for (i = 0; i < source->variable_count; i++) {
        STRPTR name = source->variables[i].name;
        STRPTR value = source->variables[i].value;
        
        /* Map compiler variables */
        if (my_stricmp(name, "CC") == 0) {
            if (my_stricmp(value, "sc") == 0 || my_stricmp(value, "lc") == 0) {
                FPrintf(output, "CC = cc\n");
            } else if (my_stricmp(value, "dcc") == 0) {
                FPrintf(output, "CC = cc\n");
            } else {
                FPrintf(output, "CC = %s\n", value);
            }
        } else {
            FPrintf(output, "%s = %s\n", name, value);
        }
    }
    
    if (source->variable_count > 0) {
        FPrintf(output, "\n");
    }
    
    /* Convert rules */
    for (i = 0; i < source->rule_count; i++) {
        Rule *rule = &source->rules[i];
        
        if (rule->is_pattern_rule) {
            /* Convert pattern rules */
            if (source->format == FORMAT_SAS_C || source->format == FORMAT_LATTICE) {
                /* .c.o: -> %.o: %.c */
                FPrintf(output, "%.o: %.c\n");
            } else if (source->format == FORMAT_DICE) {
                /* DICE pattern rules need special handling */
                FPrintf(output, "%.o: %.c\n");
            }
        } else {
            /* Regular rules */
            FPrintf(output, "%s: %s\n", rule->targets, rule->dependencies);
        }
        
        /* Convert commands */
        for (j = 0; j < rule->command_count; j++) {
            STRPTR command = rule->commands[j].command;
            STRPTR converted_cmd = map_command(command, source->format, FORMAT_GNU_MAKE);
            
            FPrintf(output, "\t%s\n", converted_cmd);
        }
        
        FPrintf(output, "\n");
    }
    
    return TRUE;
}

BOOL convert_to_sas_make(Makefile *source, BPTR output)
{
    LONG i, j;
    
    /* Write header comment */
    FPrintf(output, "; Converted to SAS/C SMakefile format from %s\n", format_to_string(source->format));
    FPrintf(output, "; Generated by GenMaki\n\n");
    
    /* Convert variables */
    for (i = 0; i < source->variable_count; i++) {
        STRPTR name = source->variables[i].name;
        STRPTR value = source->variables[i].value;
        
        /* Map compiler variables */
        if (my_stricmp(name, "CC") == 0) {
            if (my_stricmp(value, "gcc") == 0 || my_stricmp(value, "cc") == 0) {
                FPrintf(output, "CC = sc\n");
            } else if (my_stricmp(value, "dcc") == 0) {
                FPrintf(output, "CC = sc\n");
            } else if (my_stricmp(value, "lc") == 0) {
                FPrintf(output, "CC = sc\n");
            } else {
                FPrintf(output, "CC = %s\n", value);
            }
        } else if (my_stricmp(name, "CFLAGS") == 0) {
            /* Convert CFLAGS from source format to SAS/C */
            STRPTR converted_flags = convert_cflags(value, source->format, FORMAT_SAS_C);
            FPrintf(output, "CFLAGS = %s\n", converted_flags);
            if (converted_flags != value) {
                FreeVec(converted_flags);
            }
        } else {
            FPrintf(output, "%s = %s\n", name, value);
        }
    }
    
    if (source->variable_count > 0) {
        FPrintf(output, "\n");
    }
    
    /* Convert rules */
    for (i = 0; i < source->rule_count; i++) {
        Rule *rule = &source->rules[i];
        
        if (rule->is_pattern_rule) {
            /* Convert pattern rules to SAS/C format */
            if (source->format == FORMAT_GNU_MAKE) {
                /* %.o: %.c -> .c.o: */
                FPrintf(output, ".c.o:\n");
            } else if (source->format == FORMAT_DICE) {
                /* DICE pattern rules -> .c.o: */
                FPrintf(output, ".c.o:\n");
            } else {
                FPrintf(output, ".c.o:\n");
            }
        } else {
            /* Regular rules */
            FPrintf(output, "%s: %s\n", rule->targets, rule->dependencies);
        }
        
        /* Convert commands */
        if (rule->command_count > 0) {
            for (j = 0; j < rule->command_count; j++) {
                STRPTR command = rule->commands[j].command;
                STRPTR converted_cmd = map_command(command, source->format, FORMAT_SAS_C);
                
                FPrintf(output, "\t%s\n", converted_cmd);
            }
        } else {
            /* Add a comment for rules without commands */
            FPrintf(output, "\t; No commands specified - may need manual conversion\n");
        }
        
        FPrintf(output, "\n");
    }
    
    return TRUE;
}

BOOL convert_to_dice_make(Makefile *source, BPTR output)
{
    LONG i, j;
    
    /* Write header comment */
    FPrintf(output, "# Converted to DICE dmakefile format from %s\n", format_to_string(source->format));
    FPrintf(output, "# Generated by GenMaki\n\n");
    
    /* Convert variables */
    for (i = 0; i < source->variable_count; i++) {
        STRPTR name = source->variables[i].name;
        STRPTR value = source->variables[i].value;
        
        /* Map compiler variables */
        if (my_stricmp(name, "CC") == 0) {
            if (my_stricmp(value, "gcc") == 0 || my_stricmp(value, "cc") == 0) {
                FPrintf(output, "CC = dcc\n");
            } else if (my_stricmp(value, "sc") == 0) {
                FPrintf(output, "CC = dcc\n");
            } else if (my_stricmp(value, "lc") == 0) {
                FPrintf(output, "CC = dcc\n");
            } else {
                FPrintf(output, "CC = %s\n", value);
            }
        } else {
            FPrintf(output, "%s = %s\n", name, value);
        }
    }
    
    if (source->variable_count > 0) {
        FPrintf(output, "\n");
    }
    
    /* Convert rules */
    for (i = 0; i < source->rule_count; i++) {
        Rule *rule = &source->rules[i];
        
        if (rule->is_pattern_rule) {
            /* Convert pattern rules to DICE format */
            if (source->format == FORMAT_GNU_MAKE) {
                /* %.o: %.c -> %(left): %(right) */
                FPrintf(output, "%(left): %(right)\n");
            } else if (source->format == FORMAT_SAS_C || source->format == FORMAT_LATTICE) {
                /* .c.o: -> %(left): %(right) */
                FPrintf(output, "%(left): %(right)\n");
            } else {
                FPrintf(output, "%(left): %(right)\n");
            }
        } else if (rule->is_dice_form4) {
            /* DICE Form 4 rule (:: syntax) */
            FPrintf(output, "%s :: %s\n", rule->targets, rule->dependencies);
        } else {
            /* Regular rules */
            FPrintf(output, "%s: %s\n", rule->targets, rule->dependencies);
        }
        
        /* Convert commands */
        for (j = 0; j < rule->command_count; j++) {
            STRPTR command = rule->commands[j].command;
            STRPTR converted_cmd = map_command(command, source->format, FORMAT_DICE);
            
            FPrintf(output, "\t%s\n", converted_cmd);
        }
        
        FPrintf(output, "\n");
    }
    
    return TRUE;
}

BOOL convert_to_lattice_make(Makefile *source, BPTR output)
{
    LONG i, j;
    
    /* Write header comment */
    FPrintf(output, "; Converted to Lattice lmkfile format from %s\n", format_to_string(source->format));
    FPrintf(output, "; Generated by GenMaki\n\n");
    
    /* Convert variables */
    for (i = 0; i < source->variable_count; i++) {
        STRPTR name = source->variables[i].name;
        STRPTR value = source->variables[i].value;
        
        /* Map compiler variables */
        if (my_stricmp(name, "CC") == 0) {
            if (my_stricmp(value, "gcc") == 0 || my_stricmp(value, "cc") == 0) {
                FPrintf(output, "CC = lc\n");
            } else if (my_stricmp(value, "sc") == 0) {
                FPrintf(output, "CC = lc\n");
            } else if (my_stricmp(value, "dcc") == 0) {
                FPrintf(output, "CC = lc\n");
            } else {
                FPrintf(output, "CC = %s\n", value);
            }
        } else {
            FPrintf(output, "%s = %s\n", name, value);
        }
    }
    
    if (source->variable_count > 0) {
        FPrintf(output, "\n");
    }
    
    /* Convert rules */
    for (i = 0; i < source->rule_count; i++) {
        Rule *rule = &source->rules[i];
        
        if (rule->is_pattern_rule) {
            /* Convert pattern rules to Lattice format */
            if (source->format == FORMAT_GNU_MAKE) {
                /* %.o: %.c -> .c.o: */
                FPrintf(output, ".c.o:\n");
            } else if (source->format == FORMAT_SAS_C) {
                /* .c.o: -> .c.o: (same) */
                FPrintf(output, ".c.o:\n");
            } else if (source->format == FORMAT_DICE) {
                /* %(left): %(right) -> .c.o: */
                FPrintf(output, ".c.o:\n");
            } else {
                FPrintf(output, ".c.o:\n");
            }
        } else {
            /* Regular rules */
            FPrintf(output, "%s: %s\n", rule->targets, rule->dependencies);
        }
        
        /* Convert commands */
        for (j = 0; j < rule->command_count; j++) {
            STRPTR command = rule->commands[j].command;
            STRPTR converted_cmd = map_command(command, source->format, FORMAT_LATTICE);
            
            FPrintf(output, "\t%s\n", converted_cmd);
        }
        
        FPrintf(output, "\n");
    }
    
    return TRUE;
}

void cleanup_makefile(Makefile *makefile)
{
    LONG i;
    
    if (makefile->filename) FreeVec(makefile->filename);
    
    if (makefile->variables) {
        for (i = 0; i < makefile->variable_count; i++) {
            if (makefile->variables[i].name) FreeVec(makefile->variables[i].name);
            if (makefile->variables[i].value) FreeVec(makefile->variables[i].value);
        }
        FreeVec(makefile->variables);
    }
    
    if (makefile->rules) {
        for (i = 0; i < makefile->rule_count; i++) {
            if (makefile->rules[i].targets) FreeVec(makefile->rules[i].targets);
            if (makefile->rules[i].dependencies) FreeVec(makefile->rules[i].dependencies);
            if (makefile->rules[i].commands) {
                LONG j;
                for (j = 0; j < makefile->rules[i].command_count; j++) {
                    if (makefile->rules[i].commands[j].command) {
                        FreeVec(makefile->rules[i].commands[j].command);
                    }
                }
                FreeVec(makefile->rules[i].commands);
            }
        }
        FreeVec(makefile->rules);
    }
    
    if (makefile->comments) {
        for (i = 0; i < makefile->comment_count; i++) {
            if (makefile->comments[i]) FreeVec(makefile->comments[i]);
        }
        FreeVec(makefile->comments);
    }
}

void cleanup_config(Config *config)
{
    /* Note: input_file, output_file, and filetype come from ReadArgs
     * and should NOT be freed by us - they're managed by the system */
    /* Only free memory we allocated ourselves */
}

void print_usage(void)
{
    Printf("Usage: GenMaki [FROM=file] [TO=file] [FILETYPE=format] [VERBOSE] [HELP]\n");
    Printf("\n");
    Printf("Arguments:\n");
    Printf("  FROM=file      - Input makefile (optional, auto-detects if not specified)\n");
    Printf("  TO=file        - Output filename (if not specified, outputs to stdout)\n");
    Printf("  FILETYPE=format - Target format (optional, uses defaults if not specified)\n");
    Printf("  VERBOSE        - Show detailed conversion information and warnings\n");
    Printf("  HELP           - Show this help message\n");
    Printf("\n");
    Printf("Supported formats:\n");
    Printf("  smake, smakefile, sasc    - SAS/C SMakefile format\n");
    Printf("  dmake, dmakefile, dice    - DICE dmakefile format\n");
    Printf("  makefile, make, gnu, gcc  - GNU Makefile format\n");
    Printf("  lmk, lmkfile, lattice     - Lattice lmkfile format\n");
    Printf("\n");
    Printf("Default conversions:\n");
    Printf("  GNU Makefile -> SAS/C SMakefile\n");
    Printf("  Lattice lmkfile -> SAS/C SMakefile\n");
    Printf("  DICE dmakefile -> GNU Makefile\n");
    Printf("  SAS/C smakefile -> GNU Makefile\n");
    Printf("\n");
    Printf("Examples:\n");
    Printf("  GenMaki                                    # Auto-detect and convert\n");
    Printf("  GenMaki FROM=makefile FILETYPE=sasc SAVE   # Convert to SAS/C format\n");
    Printf("  GenMaki FROM=lmkfile TO=Makefile SAVE      # Convert to GNU Make\n");
}

BOOL validate_config(Config *config)
{
    if (config->save_to_file && !config->output_file) {
        Printf("GenMaki: TO=filename required when using SAVE\n");
        return FALSE;
    }
    
    return TRUE;
}

STRPTR convert_cflags(STRPTR flags, MakefileFormat from, MakefileFormat to)
{
    /* Convert compiler flags between different Amiga compilers */
    STRPTR result;
    STRPTR new_flags;
    STRPTR option;
    STRPTR mapped_option;
    STRPTR current;
    STRPTR start;
    LONG flags_len;
    LONG new_flags_len;
    BOOL first_option;
    
    result = my_strdup(flags);
    if (!result) return flags;
    
    /* If no conversion needed, return original */
    if (from == to) {
        return result;
    }
    
    flags_len = strlen(flags);
    new_flags = AllocVec(flags_len * 2, MEMF_CLEAR);
    if (!new_flags) {
        return result;
    }
    
    new_flags_len = 0;
    first_option = TRUE;
    current = flags;
    
    /* Parse individual options separated by spaces */
    while (*current) {
        /* Skip leading spaces */
        while (*current == ' ') current++;
        if (!*current) break;
        
        /* Find end of current option */
        start = current;
        while (*current && *current != ' ') current++;
        
        /* Extract option */
        if (current > start) {
            option = AllocVec(current - start + 1, MEMF_CLEAR);
            if (option) {
                strncpy((char *)option, start, current - start);
                option[current - start] = '\0';
                
                /* Map the option */
                mapped_option = map_compiler_option(option, from, to);
                
                /* Add to result if not empty */
                if (mapped_option && strlen(mapped_option) > 0) {
                    if (!first_option) {
                        strcat((char *)new_flags, " ");
                        new_flags_len++;
                    }
                    strcat((char *)new_flags, mapped_option);
                    new_flags_len += strlen(mapped_option);
                    first_option = FALSE;
                }
                
                /* Clean up */
                FreeVec(option);
                if (mapped_option != option) {
                    FreeVec(mapped_option);
                }
            }
        }
    }
    
    FreeVec(result);
    return new_flags;
}

STRPTR map_compiler_option(STRPTR option, MakefileFormat from, MakefileFormat to)
{
    /* Map compiler options between different Amiga compilers */
    STRPTR result;
    STRPTR new_option;
    LONG option_len;
    
    result = my_strdup(option);
    if (!result) return option;
    
    option_len = strlen(option);
    
    /* Lattice C to SAS/C mapping */
    if (from == FORMAT_LATTICE && to == FORMAT_SAS_C) {
        if (my_stricmp(option, "-O") == 0) {
            new_option = my_strdup("OPTIMIZE");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-DNONAMES") == 0) {
            new_option = my_strdup("NOSTANDARDIO");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (strstr(option, "-DDEFBLOCKING=")) {
            /* Remove this option as it's not applicable to SAS/C */
            FreeVec(result);
            return my_strdup("");
        } else if (strstr(option, "-I")) {
            /* Convert -Ipath to INCLUDEDIR=path: */
            new_option = AllocVec(option_len + 20, MEMF_CLEAR);
            if (new_option) {
                strcpy((char *)new_option, "INCLUDEDIR=");
                strcat((char *)new_option, option + 2); /* Skip "-I" */
                strcat((char *)new_option, ":");
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-v") == 0) {
            new_option = my_strdup("VERBOSE");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-d2") == 0 || my_stricmp(option, "-y") == 0) {
            new_option = my_strdup("DEBUG=L");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-ms") == 0) {
            new_option = my_strdup("DATA=NEAR");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (strstr(option, "-D")) {
            /* Convert -DNAME=VALUE to DEF=NAME=VALUE or DEF=NAME */
            new_option = AllocVec(option_len + 10, MEMF_CLEAR);
            if (new_option) {
                strcpy((char *)new_option, "DEF=");
                strcat((char *)new_option, option + 2); /* Skip "-D" */
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-w") == 0) {
            new_option = my_strdup("IGN=A");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-g") == 0) {
            new_option = my_strdup("DEBUG=FF");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-c") == 0) {
            new_option = my_strdup("OBJNAME");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-E") == 0) {
            new_option = my_strdup("PPONLY");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-a") == 0) {
            new_option = my_strdup("DISASM");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        }
    }
    
    /* Lattice C to DICE mapping */
    if (from == FORMAT_LATTICE && to == FORMAT_DICE) {
        if (my_stricmp(option, "-O") == 0) {
            new_option = my_strdup("-O");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-DNONAMES") == 0) {
            /* Remove this option as it's not applicable to DICE */
            FreeVec(result);
            return my_strdup("");
        } else if (strstr(option, "-DDEFBLOCKING=")) {
            /* Remove this option as it's not applicable to DICE */
            FreeVec(result);
            return my_strdup("");
        } else if (strstr(option, "-I")) {
            /* Keep -I format for DICE */
            return result;
        } else if (my_stricmp(option, "-v") == 0) {
            new_option = my_strdup("-v");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-d2") == 0 || my_stricmp(option, "-y") == 0) {
            new_option = my_strdup("-d1");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-ms") == 0) {
            new_option = my_strdup("-ms");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (strstr(option, "-D")) {
            /* Keep -D format for DICE */
            return result;
        } else if (my_stricmp(option, "-w") == 0) {
            /* DICE doesn't support -w, remove it */
            FreeVec(result);
            return my_strdup("");
        } else if (my_stricmp(option, "-g") == 0) {
            new_option = my_strdup("-s -d1");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-c") == 0) {
            new_option = my_strdup("-c");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-E") == 0) {
            /* DICE uses dcpp for preprocessing */
            new_option = my_strdup("-E");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-a") == 0) {
            new_option = my_strdup("-a");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        }
    }
    
    /* Lattice C to GNU Make mapping */
    if (from == FORMAT_LATTICE && to == FORMAT_GNU_MAKE) {
        if (my_stricmp(option, "-O") == 0) {
            new_option = my_strdup("-O2");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-DNONAMES") == 0) {
            /* Remove this option as it's not applicable to GCC */
            FreeVec(result);
            return my_strdup("");
        } else if (strstr(option, "-DDEFBLOCKING=")) {
            /* Remove this option as it's not applicable to GCC */
            FreeVec(result);
            return my_strdup("");
        } else if (strstr(option, "-I")) {
            /* Keep -I format for GCC */
            return result;
        } else if (my_stricmp(option, "-v") == 0) {
            new_option = my_strdup("-v");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-d2") == 0 || my_stricmp(option, "-y") == 0) {
            new_option = my_strdup("-g");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-ms") == 0) {
            new_option = my_strdup("-m68000");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (strstr(option, "-D")) {
            /* Keep -D format for GCC */
            return result;
        } else if (my_stricmp(option, "-w") == 0) {
            new_option = my_strdup("-w");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-g") == 0) {
            new_option = my_strdup("-g");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-c") == 0) {
            new_option = my_strdup("-c");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-E") == 0) {
            new_option = my_strdup("-E");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-a") == 0) {
            /* GCC doesn't have -a, use -S for assembly output */
            new_option = my_strdup("-S");
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        }
    }
    
    /* SAS/C to other formats */
    if (from == FORMAT_SAS_C) {
        if (my_stricmp(option, "OPTIMIZE") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-O2");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-O");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-O");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "NOSTANDARDIO") == 0) {
            /* Remove this option as it's SAS/C specific */
            FreeVec(result);
            return my_strdup("");
        } else if (strstr(option, "INCLUDEDIR=")) {
            /* Convert INCLUDEDIR=path: to -Ipath */
            new_option = AllocVec(option_len + 5, MEMF_CLEAR);
            if (new_option) {
                strcpy((char *)new_option, "-I");
                strcat((char *)new_option, option + 11); /* Skip "INCLUDEDIR=" */
                /* Remove trailing colon if present */
                if (new_option[strlen((char *)new_option) - 1] == ':') {
                    new_option[strlen((char *)new_option) - 1] = '\0';
                }
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "DEBUG=L") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-g");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-d1");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-d2");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "DATA=NEAR") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-m68000");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-ms");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-ms");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "VERBOSE") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-v");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-v");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-v");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "IGN=A") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-w");
            } else if (to == FORMAT_DICE) {
                /* DICE doesn't support -w, remove it */
                FreeVec(result);
                return my_strdup("");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-w");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (strstr(option, "DEF=")) {
            /* Convert DEF=NAME=VALUE to -DNAME=VALUE or DEF=NAME to -DNAME */
            new_option = AllocVec(option_len + 5, MEMF_CLEAR);
            if (new_option) {
                strcpy((char *)new_option, "-D");
                strcat((char *)new_option, option + 4); /* Skip "DEF=" */
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "OBJNAME") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-c");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-c");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-c");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "PPONLY") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-E");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-E");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-E");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "DISASM") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-S");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-a");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-a");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        }
    }
    
    /* DICE to other formats */
    if (from == FORMAT_DICE) {
        if (my_stricmp(option, "-O") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-O2");
            } else if (to == FORMAT_SAS_C) {
                new_option = my_strdup("OPTIMIZE");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-O");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-d1") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-g");
            } else if (to == FORMAT_SAS_C) {
                new_option = my_strdup("DEBUG=L");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-d2");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-ms") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-m68000");
            } else if (to == FORMAT_SAS_C) {
                new_option = my_strdup("DATA=NEAR");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-ms");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (strstr(option, "-D")) {
            /* Keep -D format for other compilers */
            return result;
        } else if (my_stricmp(option, "-v") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-v");
            } else if (to == FORMAT_SAS_C) {
                new_option = my_strdup("VERBOSE");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-v");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-c") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-c");
            } else if (to == FORMAT_SAS_C) {
                new_option = my_strdup("OBJNAME");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-c");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-E") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-E");
            } else if (to == FORMAT_SAS_C) {
                new_option = my_strdup("PPONLY");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-E");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-a") == 0) {
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-S");
            } else if (to == FORMAT_SAS_C) {
                new_option = my_strdup("DISASM");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-a");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-s") == 0) {
            /* DICE -s is for debug symbols, map to appropriate debug options */
            if (to == FORMAT_GNU_MAKE) {
                new_option = my_strdup("-g");
            } else if (to == FORMAT_SAS_C) {
                new_option = my_strdup("DEBUG=FF");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-g");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        }
    }
    
    /* GNU Make to other formats */
    if (from == FORMAT_GNU_MAKE) {
        if (my_stricmp(option, "-O2") == 0) {
            if (to == FORMAT_SAS_C) {
                new_option = my_strdup("OPTIMIZE");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-O");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-O");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-g") == 0) {
            if (to == FORMAT_SAS_C) {
                new_option = my_strdup("DEBUG=L");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-d1");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-d2");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-m68000") == 0) {
            if (to == FORMAT_SAS_C) {
                new_option = my_strdup("DATA=NEAR");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-ms");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-ms");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (strstr(option, "-D")) {
            /* Keep -D format for other compilers */
            return result;
        } else if (my_stricmp(option, "-v") == 0) {
            if (to == FORMAT_SAS_C) {
                new_option = my_strdup("VERBOSE");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-v");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-v");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-w") == 0) {
            if (to == FORMAT_SAS_C) {
                new_option = my_strdup("IGN=A");
            } else if (to == FORMAT_DICE) {
                /* DICE doesn't support -w, remove it */
                FreeVec(result);
                return my_strdup("");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-w");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-c") == 0) {
            if (to == FORMAT_SAS_C) {
                new_option = my_strdup("OBJNAME");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-c");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-c");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-E") == 0) {
            if (to == FORMAT_SAS_C) {
                new_option = my_strdup("PPONLY");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-E");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-E");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        } else if (my_stricmp(option, "-S") == 0) {
            if (to == FORMAT_SAS_C) {
                new_option = my_strdup("DISASM");
            } else if (to == FORMAT_DICE) {
                new_option = my_strdup("-a");
            } else if (to == FORMAT_LATTICE) {
                new_option = my_strdup("-a");
            } else {
                return result;
            }
            if (new_option) {
                FreeVec(result);
                return new_option;
            }
        }
    }
    
    /* If no mapping found, return original option */
    return result;
}

STRPTR map_command(STRPTR command, MakefileFormat from, MakefileFormat to)
{
    /* Basic command mapping - more sophisticated mapping can be added later */
    STRPTR result;
    STRPTR new_cmd;
    STRPTR args;
    STRPTR arg_start;
    
    result = my_strdup(command);
    if (!result) return command;
    
    /* Map compiler commands */
    if (strstr(command, "gcc") && to != FORMAT_GNU_MAKE) {
        if (to == FORMAT_SAS_C) {
            /* Replace gcc with sc and add OBJNAME parameter */
            new_cmd = AllocVec(strlen(command) + 50, MEMF_CLEAR);
            if (new_cmd) {
                strcpy((char *)new_cmd, "sc ");
                strcat((char *)new_cmd, command + 4); /* Skip "gcc " */
                strcat((char *)new_cmd, " OBJNAME=$*.o");
                FreeVec(result);
                return new_cmd;
            }
        } else if (to == FORMAT_DICE) {
            /* Replace gcc with dcc */
            new_cmd = AllocVec(strlen(command) + 10, MEMF_CLEAR);
            if (new_cmd) {
                strcpy((char *)new_cmd, "dcc ");
                strcat((char *)new_cmd, command + 4); /* Skip "gcc " */
                FreeVec(result);
                return new_cmd;
            }
        } else if (to == FORMAT_LATTICE) {
            /* Replace gcc with lc */
            new_cmd = AllocVec(strlen(command) + 10, MEMF_CLEAR);
            if (new_cmd) {
                strcpy((char *)new_cmd, "lc ");
                strcat((char *)new_cmd, command + 4); /* Skip "gcc " */
                FreeVec(result);
                return new_cmd;
            }
        }
    }
    
    /* Map linker commands */
    if (strstr(command, "blink") && to == FORMAT_SAS_C) {
        /* Convert blink to slink */
        new_cmd = AllocVec(strlen(command) + 10, MEMF_CLEAR);
        if (new_cmd) {
            strcpy((char *)new_cmd, "slink ");
            strcat((char *)new_cmd, command + 6); /* Skip "blink " */
            FreeVec(result);
            return new_cmd;
        }
    } else if (strstr(command, "slink") && to != FORMAT_SAS_C) {
        if (to == FORMAT_GNU_MAKE) {
            /* Convert slink to gcc link command */
            new_cmd = AllocVec(strlen(command) + 50, MEMF_CLEAR);
            if (new_cmd) {
                strcpy((char *)new_cmd, "cc ");
                /* Extract object files and output name from slink command */
                strcat((char *)new_cmd, "-o program"); /* Simplified */
                FreeVec(result);
                return new_cmd;
            }
        }
    }
    
    /* Map file operations */
    if (strstr(command, "rm") && to != FORMAT_GNU_MAKE) {
        if (to == FORMAT_SAS_C) {
            new_cmd = AllocVec(strlen(command) + 20, MEMF_CLEAR);
            if (new_cmd) {
                strcpy((char *)new_cmd, "delete ");
                /* Remove wildcards and -f flag, keep only specific filenames */
                args = command;
                while (*args && *args != ' ') args++; /* Skip "rm" */
                while (*args == ' ') args++; /* Skip spaces */
                while (*args == '-') { /* Skip flags like -f */
                    while (*args && *args != ' ') args++;
                    while (*args == ' ') args++;
                }
                /* Copy remaining arguments, but remove wildcards */
                arg_start = args;
                while (*args) {
                    if (*args == '*' || *args == '?') {
                        /* Skip wildcard patterns - Amiga doesn't support them in delete */
                        while (*args && *args != ' ') args++;
                        while (*args == ' ') args++;
                    } else {
                        args++;
                    }
                }
                if (arg_start < args) {
                    strncat((char *)new_cmd, arg_start, args - arg_start);
                }
                strcat((char *)new_cmd, " QUIET");
                FreeVec(result);
                return new_cmd;
            }
        } else if (to == FORMAT_DICE) {
            new_cmd = AllocVec(strlen(command) + 10, MEMF_CLEAR);
            if (new_cmd) {
                strcpy((char *)new_cmd, "delete ");
                /* Similar processing for DICE */
                args = command;
                while (*args && *args != ' ') args++;
                while (*args == ' ') args++;
                while (*args == '-') {
                    while (*args && *args != ' ') args++;
                    while (*args == ' ') args++;
                }
                strcat((char *)new_cmd, args);
                FreeVec(result);
                return new_cmd;
            }
        } else if (to == FORMAT_LATTICE) {
            new_cmd = AllocVec(strlen(command) + 10, MEMF_CLEAR);
            if (new_cmd) {
                strcpy((char *)new_cmd, "Delete ");
                /* Similar processing for Lattice */
                args = command;
                while (*args && *args != ' ') args++;
                while (*args == ' ') args++;
                while (*args == '-') {
                    while (*args && *args != ' ') args++;
                    while (*args == ' ') args++;
                }
                strcat((char *)new_cmd, args);
                FreeVec(result);
                return new_cmd;
            }
        }
    }
    
    return result;
}
