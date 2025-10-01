/*
 * Copyright (c) 2025 amigazen project
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GenDo - Amiga Autodoc Generator
 * 
 * This tool extracts autodoc markup from source code files and generates
 * Amiga documentation in .doc format and optionally AmigaGuide format.
 * 
 * Command line usage:
 * GenDo #?.c #?.cpp TO mylib.doc AMIGAGUIDE
 * 
 * Features:
 * - Parse autodoc markup from C/C++ source files
 * - Support Amiga wildcards for file selection
 * - Generate .doc format with table of contents
 * - Optional AmigaGuide output
 * - ANSI C/C89 compliant code
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

/* Error codes are already defined in dos/dos.h */

/* Version and stack information */
static const char *verstag = "$VER: GenDo 1.0 (01/10/25)";
static const char *stack_cookie = "$STACK: 8192";

/* Global library bases */
struct Library *UtilityBase = NULL;

/* Maximum sizes and limits */
#define MAX_LINE_LENGTH 512
#define MAX_FILENAME_LENGTH 256
#define MAX_AUTODOCS 256
#define MAX_FILES 64
#define MAX_STRING_LENGTH 1024

/* Autodoc structure */
typedef struct {
    STRPTR module_name;
    STRPTR function_name;
    STRPTR name;
    STRPTR synopsis;
    STRPTR function_desc;
    STRPTR inputs;
    STRPTR result;
    STRPTR example;
    STRPTR notes;
    STRPTR bugs;
    STRPTR see_also;
    BOOL is_internal;
    BOOL is_obsolete;
    LONG line_number;
} Autodoc;

/* File structure for tracking source files */
typedef struct {
    STRPTR filename;
    BPTR file_handle;
    LONG line_count;
} SourceFile;

/* Configuration structure */
typedef struct {
    STRPTR output_doc;
    STRPTR output_guide;
    SourceFile *source_files;
    LONG file_count;
    Autodoc *autodocs;
    LONG autodoc_count;
    BOOL generate_guide;
    BOOL verbose;
    LONG line_length;
    BOOL word_wrap;
    BOOL convert_comments;
    BOOL no_form_feed;
    BOOL no_toc;
    BOOL preserve_order;
} Config;

/* Function prototypes */
LONG parse_command_line(Config *config, LONG argc, STRPTR *argv);
BOOL process_source_files(Config *config);
void sort_autodocs(Config *config);
BOOL parse_autodoc_from_file(SourceFile *file, Config *config);
BOOL is_autodoc_start(const char *line);
BOOL is_autodoc_end(const char *line);
BOOL is_internal_autodoc(const char *line);
BOOL is_obsolete_autodoc(const char *line);
STRPTR extract_module_function(const char *line);
STRPTR is_section_header(const char *line);
BOOL parse_autodoc_section(BPTR file, Autodoc *autodoc, STRPTR line);
void store_section_content(Autodoc *autodoc, const char *section, const char *content);
STRPTR read_autodoc_line(BPTR file);
BOOL generate_doc_output(Config *config);
BOOL generate_guide_output(Config *config);
void cleanup_config(Config *config);
void print_usage(void);
LONG expand_wildcards(Config *config, STRPTR *file_array, LONG file_count);
BOOL match_pattern(const char *pattern, const char *filename);
int main(int argc, char *argv[]);

/* Helper function for string duplication using Amiga memory allocation */
STRPTR strdup_amiga(const char *str)
{
    LONG len = strlen(str);
    STRPTR copy = AllocVec(len + 1, MEMF_CLEAR);
    if (copy) {
        strcpy(copy, str);
    }
    return copy;
}

/* Check if line is a section header and return the section name */
STRPTR is_section_header(const char *line)
{
    const char *p = line;
    const char *start, *end;
    LONG len;
    STRPTR result;
    
    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t') p++;
    
    /* Must start with asterisk */
    if (*p != '*') return NULL;
    p++;
    
    /* Skip spaces after asterisk */
    while (*p == ' ') p++;
    
    /* Check for known section headers */
    if (strncmp(p, "NAME", 4) == 0 && (p[4] == ' ' || p[4] == '\t' || p[4] == '\n' || p[4] == '\r')) {
        return "NAME";
    }
    if (strncmp(p, "SYNOPSIS", 8) == 0 && (p[8] == ' ' || p[8] == '\t' || p[8] == '\n' || p[8] == '\r')) {
        return "SYNOPSIS";
    }
    if (strncmp(p, "FUNCTION", 8) == 0 && (p[8] == ' ' || p[8] == '\t' || p[8] == '\n' || p[8] == '\r')) {
        return "FUNCTION";
    }
    if (strncmp(p, "DESCRIPTION", 11) == 0 && (p[11] == ' ' || p[11] == '\t' || p[11] == '\n' || p[11] == '\r')) {
        return "FUNCTION"; /* Map DESCRIPTION to FUNCTION */
    }
    if (strncmp(p, "INPUTS", 6) == 0 && (p[6] == ' ' || p[6] == '\t' || p[6] == '\n' || p[6] == '\r')) {
        return "INPUTS";
    }
    if (strncmp(p, "PARAMETERS", 10) == 0 && (p[10] == ' ' || p[10] == '\t' || p[10] == '\n' || p[10] == '\r')) {
        return "INPUTS"; /* Map PARAMETERS to INPUTS */
    }
    if (strncmp(p, "RESULT", 6) == 0 && (p[6] == ' ' || p[6] == '\t' || p[6] == '\n' || p[6] == '\r')) {
        return "RESULT";
    }
    if (strncmp(p, "RETURNS", 7) == 0 && (p[7] == ' ' || p[7] == '\t' || p[7] == '\n' || p[7] == '\r')) {
        return "RESULT"; /* Map RETURNS to RESULT */
    }
    if (strncmp(p, "EXAMPLE", 7) == 0 && (p[7] == ' ' || p[7] == '\t' || p[7] == '\n' || p[7] == '\r')) {
        return "EXAMPLE";
    }
    if (strncmp(p, "EXAMPLES", 8) == 0 && (p[8] == ' ' || p[8] == '\t' || p[8] == '\n' || p[8] == '\r')) {
        return "EXAMPLE"; /* Map EXAMPLES to EXAMPLE */
    }
    if (strncmp(p, "NOTES", 5) == 0 && (p[5] == ' ' || p[5] == '\t' || p[5] == '\n' || p[5] == '\r')) {
        return "NOTES";
    }
    if (strncmp(p, "BUGS", 4) == 0 && (p[4] == ' ' || p[4] == '\t' || p[4] == '\n' || p[4] == '\r')) {
        return "BUGS";
    }
    if (strncmp(p, "SEE ALSO", 8) == 0 && (p[8] == ' ' || p[8] == '\t' || p[8] == '\n' || p[8] == '\r')) {
        return "SEE ALSO";
    }
    if (strncmp(p, "WARNING", 7) == 0 && (p[7] == ' ' || p[7] == '\t' || p[7] == '\n' || p[7] == '\r')) {
        return "NOTES"; /* Map WARNING to NOTES */
    }
    if (strncmp(p, "WARNINGS", 8) == 0 && (p[8] == ' ' || p[8] == '\t' || p[8] == '\n' || p[8] == '\r')) {
        return "NOTES"; /* Map WARNINGS to NOTES */
    }
    
    /* Check for flexible section header pattern: * SECTION_NAME */
    start = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
    end = p;
    
    /* Must have at least 2 characters for section name */
    len = end - start;
    if (len >= 2) {
        /* Check if it looks like a section name (all uppercase letters and spaces) */
        BOOL valid = TRUE;
        const char *check = start;
        while (check < end) {
            if (!((*check >= 'A' && *check <= 'Z') || *check == ' ')) {
                valid = FALSE;
                break;
            }
            check++;
        }
        
        if (valid) {
            result = AllocVec(len + 1, MEMF_CLEAR);
            if (result) {
                strncpy(result, start, len);
                result[len] = '\0';
            }
            return result;
        }
    }
    
    return NULL;
}

/* Match a filename against a wildcard pattern using Amiga DOS APIs */
BOOL match_pattern(const char *pattern, const char *filename)
{
    STRPTR tokenized_pattern;
    LONG pattern_len;
    LONG result;
    
    /* Allocate buffer for tokenized pattern (2x source length + 2 bytes as per docs) */
    pattern_len = strlen(pattern);
    tokenized_pattern = AllocVec(pattern_len * 2 + 2, MEMF_CLEAR);
    if (!tokenized_pattern) {
        return FALSE;
    }
    
    /* Parse the pattern using Amiga DOS ParsePattern */
    result = ParsePattern((STRPTR)pattern, tokenized_pattern, pattern_len * 2 + 2);
    
    if (result == -1) {
        /* Error in pattern parsing */
        FreeVec(tokenized_pattern);
        return FALSE;
    }
    
    /* Use MatchPattern to check if filename matches the tokenized pattern */
    result = MatchPattern(tokenized_pattern, (STRPTR)filename);
    
    FreeVec(tokenized_pattern);
    if (result == 1) {
        return TRUE;
    } else {
        return FALSE;
    }
}

/* Expand wildcards in file patterns and populate config->source_files */
LONG expand_wildcards(Config *config, STRPTR *file_array, LONG file_count)
{
    struct AnchorPath *ap;
    STRPTR *expanded_files;
    LONG expanded_count = 0;
    LONG max_files = file_count * 1000; /* Allow for wildcard expansion - very generous limit */
    LONG i;
    LONG err;
    LONG j;
    
    /* Allocate space for expanded file list */
    expanded_files = (STRPTR*)AllocVec(max_files * sizeof(STRPTR), MEMF_CLEAR);
    if (!expanded_files) {
        Printf("GenDo: Out of memory for file expansion\n");
        return 0;
    }
    
    /* Allocate AnchorPath for directory scanning */
    ap = (struct AnchorPath*)AllocVec(sizeof(struct AnchorPath) + 1024, MEMF_CLEAR);
    if (!ap) {
        Printf("GenDo: Out of memory for directory scanning\n");
        FreeVec(expanded_files);
        return 0;
    }
    
    ap->ap_Strlen = 1024;
    ap->ap_BreakBits = SIGBREAKF_CTRL_C;
    
    /* Process each file pattern */
    for (i = 0; i < file_count; i++) {
        if (!file_array[i]) continue;
        
        if (config->verbose) {
            Printf("GenDo: Expanding pattern '%s'\n", file_array[i]);
        }
        
        /* Use MatchFirst to scan for files matching the pattern */
        err = MatchFirst(file_array[i], ap);
        if (err == 0) {
            do {
                if (ap->ap_Info.fib_DirEntryType < 0) { /* Regular file */
                    /* Build full path from ap->ap_Buf which contains the full path */
                    STRPTR full_path = AllocVec(strlen(ap->ap_Buf) + 1, MEMF_CLEAR);
                    if (full_path) {
                        strcpy(full_path, ap->ap_Buf);
                        
                        if (expanded_count >= max_files) {
                            /* Reallocate with more space */
                            LONG new_max = max_files * 2;
                            STRPTR *new_files = (STRPTR*)AllocVec(new_max * sizeof(STRPTR), MEMF_CLEAR);
                            if (new_files) {
                                /* Copy existing files */
                                for (j = 0; j < expanded_count; j++) {
                                    new_files[j] = expanded_files[j];
                                }
                                FreeVec(expanded_files);
                                expanded_files = new_files;
                                max_files = new_max;
                                
                                if (config->verbose) {
                                    Printf("GenDo: Expanded file list to %ld entries\n", max_files);
                                }
                            } else {
                                /* Out of memory, free the file and continue */
                                FreeVec(full_path);
                                continue;
                            }
                        }
                        
                        expanded_files[expanded_count] = full_path;
                        expanded_count++;
                        
                        if (config->verbose) {
                            Printf("GenDo: Found file '%s'\n", full_path);
                        }
                    }
                }
            } while ((err = MatchNext(ap)) == 0);
            
            if (err == ERROR_NO_MORE_ENTRIES) {
                err = 0; /* Normal completion */
            }
        } else if (err == ERROR_OBJECT_NOT_FOUND) {
            /* Pattern doesn't match anything, try as literal filename */
            BPTR fh = Open(file_array[i], MODE_OLDFILE);
            if (fh) {
                Close(fh);
                /* File exists, add it directly */
                if (expanded_count >= max_files) {
                    /* Reallocate with more space */
                    LONG new_max = max_files * 2;
                    STRPTR *new_files = (STRPTR*)AllocVec(new_max * sizeof(STRPTR), MEMF_CLEAR);
                    if (new_files) {
                        /* Copy existing files */
                        for (j = 0; j < expanded_count; j++) {
                            new_files[j] = expanded_files[j];
                        }
                        FreeVec(expanded_files);
                        expanded_files = new_files;
                        max_files = new_max;
                        
                        if (config->verbose) {
                            Printf("GenDo: Expanded file list to %ld entries\n", max_files);
                        }
                    } else {
                        /* Out of memory, skip this file */
                        continue;
                    }
                }
                
                expanded_files[expanded_count] = strdup_amiga(file_array[i]);
                expanded_count++;
                
                if (config->verbose) {
                    Printf("GenDo: Found file '%s'\n", file_array[i]);
                }
            }
        }
    }
    
    /* Clean up */
    FreeVec(ap);
    
    if (expanded_count == 0) {
        Printf("GenDo: No files found matching the specified patterns\n");
        FreeVec(expanded_files);
        return 0;
    }
    
    /* Allocate and populate source_files */
    config->file_count = expanded_count;
    config->source_files = (SourceFile*)AllocVec(expanded_count * sizeof(SourceFile), MEMF_CLEAR);
    if (config->source_files) {
        for (i = 0; i < expanded_count; i++) {
            config->source_files[i].filename = expanded_files[i];
            config->source_files[i].file_handle = 0;
            config->source_files[i].line_count = 0;
        }
    }
    
    FreeVec(expanded_files);
    return expanded_count;
}

/* Sort autodocs alphabetically by function name */
void sort_autodocs(Config *config)
{
    LONG i, j;
    Autodoc *temp;
    
    if (!config->autodocs || config->autodoc_count <= 1) {
        return; /* Nothing to sort */
    }
    
    /* Simple bubble sort - stable and easy to understand */
    for (i = 0; i < config->autodoc_count - 1; i++) {
        for (j = 0; j < config->autodoc_count - 1 - i; j++) {
            if (strcmp(config->autodocs[j].function_name, config->autodocs[j + 1].function_name) > 0) {
                /* Swap the autodocs */
                temp = (Autodoc*)AllocVec(sizeof(Autodoc), MEMF_CLEAR);
                if (temp) {
                    *temp = config->autodocs[j];
                    config->autodocs[j] = config->autodocs[j + 1];
                    config->autodocs[j + 1] = *temp;
                    FreeVec(temp);
                }
            }
        }
    }
    
    if (config->verbose) {
        Printf("GenDo: Sorted %ld autodoc entries alphabetically\n", config->autodoc_count);
    }
}

/* Parse command line arguments using ReadArgs */
LONG parse_command_line(Config *config, LONG argc, STRPTR *argv)
{
    struct RDArgs *rdargs;
    
    /* Template for ReadArgs */
    static UBYTE template[] = "FILES/M/A,TO/K,AMIGAGUIDE/S,VERBOSE/S,LINELENGTH/N,WORDWRAP/S,CONVERTCOMMENTS/S,NOFORMFEED/S,NOTOC/S,PRESERVEORDER/S";
    LONG args[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; /* files, to, amigaguide, verbose, linelength, wordwrap, convertcomments, noformfeed, notoc, preserveorder */
    
    /* Initialize config */
    config->output_doc = NULL;
    config->output_guide = NULL;
    config->source_files = NULL;
    config->file_count = 0;
    config->autodocs = NULL;
    config->autodoc_count = 0;
    config->generate_guide = FALSE;
    config->verbose = FALSE;
    config->line_length = 78;
    config->word_wrap = TRUE;
    config->convert_comments = TRUE;
    config->no_form_feed = FALSE;
    config->no_toc = FALSE;
    
    /* Parse arguments */
    rdargs = ReadArgs(template, args, NULL);
    if (!rdargs) {
        Printf("GenDo: Failed to parse command line arguments\n");
        return RETURN_FAIL;
    }
    
    /* Process FILES argument */
    if (args[0]) { /* FILES argument */
        STRPTR *file_array = (STRPTR*)args[0];
        LONG file_count = 0;
        
        /* Count files */
        while (file_array[file_count]) {
            file_count++;
        }
        
        if (file_count > 0) {
            /* Expand wildcards and populate source_files */
            LONG expanded_count = expand_wildcards(config, file_array, file_count);
            if (expanded_count == 0) {
                FreeArgs(rdargs);
                return RETURN_FAIL;
            }
        }
    }
    
    /* Set other arguments */
    if (args[1]) {
        config->output_doc = strdup_amiga((STRPTR)args[1]);
        if (!config->output_doc) {
            Printf("GenDo: Out of memory for output filename\n");
            FreeArgs(rdargs);
            return RETURN_FAIL;
        }
    } else {
        Printf("GenDo: Output filename required (use TO filename)\n");
        FreeArgs(rdargs);
        return RETURN_FAIL;
    }
    
    if (args[2]) config->generate_guide = TRUE;
    if (args[3]) config->verbose = TRUE;
    if (args[4]) config->line_length = (LONG)args[4];
    if (args[5]) config->word_wrap = TRUE;
    if (args[6]) config->convert_comments = TRUE;
    if (args[7]) config->no_form_feed = TRUE;
    if (args[8]) config->no_toc = TRUE;
    if (args[9]) config->preserve_order = TRUE;
    
    /* Validate required arguments - output_doc already validated above */
    
    /* Debug output */
    if (config->verbose) {
        Printf("GenDo: Output file: %s\n", config->output_doc);
        Printf("GenDo: File count: %ld\n", config->file_count);
        if (config->generate_guide) {
            Printf("GenDo: Guide file: %s\n", config->output_guide);
        }
    }
    
    if (config->generate_guide) {
        /* Generate guide filename from doc filename */
        LONG doc_len = strlen(config->output_doc);
        char *dot;
        config->output_guide = AllocVec(doc_len + 8, MEMF_CLEAR);
        if (config->output_guide) {
            strcpy(config->output_guide, config->output_doc);
            /* Replace .doc with .guide */
            dot = strchr(config->output_guide, '.');
            if (dot) {
                strcpy(dot, ".guide");
            } else {
                strcpy(config->output_guide + doc_len, ".guide");
            }
        }
    }
    
    FreeArgs(rdargs);
    return RETURN_OK;
}

/* Process all source files and extract autodocs */
BOOL process_source_files(Config *config)
{
    LONG i;
    BOOL success = TRUE;
    
    if (config->verbose) {
        Printf("GenDo: Processing %ld source files\n", config->file_count);
    }
    
    for (i = 0; i < config->file_count; i++) {
        if (config->verbose) {
            Printf("GenDo: Processing file: %s\n", config->source_files[i].filename);
        }
        
        if (!parse_autodoc_from_file(&config->source_files[i], config)) {
            Printf("GenDo: Warning: Failed to process file %s\n", 
                   config->source_files[i].filename);
            success = FALSE;
        }
    }
    
    if (config->verbose) {
        Printf("GenDo: Extracted %ld autodocs\n", config->autodoc_count);
    }
    
    return success;
}

/* Parse autodoc from a single source file */
BOOL parse_autodoc_from_file(SourceFile *file, Config *config)
{
    BPTR file_handle;
    char line[MAX_LINE_LENGTH];
    LONG line_number = 0;
    BOOL in_autodoc = FALSE;
    Autodoc current_autodoc;
    STRPTR module_func;
    char *slash;
    
    /* Open the source file */
    file_handle = Open(file->filename, MODE_OLDFILE);
    if (!file_handle) {
        Printf("GenDo: Cannot open file: %s\n", file->filename);
        return FALSE;
    }
    
    /* Initialize current autodoc */
    current_autodoc.module_name = NULL;
    current_autodoc.function_name = NULL;
    current_autodoc.name = NULL;
    current_autodoc.synopsis = NULL;
    current_autodoc.function_desc = NULL;
    current_autodoc.inputs = NULL;
    current_autodoc.result = NULL;
    current_autodoc.example = NULL;
    current_autodoc.notes = NULL;
    current_autodoc.bugs = NULL;
    current_autodoc.see_also = NULL;
    current_autodoc.is_internal = FALSE;
    current_autodoc.is_obsolete = FALSE;
    current_autodoc.line_number = 0;
    
    /* Read file line by line */
    while (FGets(file_handle, line, sizeof(line))) {
        line_number++;
        
        /* Check for autodoc start */
        if (is_autodoc_start(line)) {
            if (in_autodoc) {
                /* Finish previous autodoc */
                if (current_autodoc.name) {
                    /* Add to config autodocs array */
                    if (config->autodoc_count < MAX_AUTODOCS) {
                        config->autodocs[config->autodoc_count] = current_autodoc;
                        config->autodoc_count++;
                    }
                }
            }
            
            /* Start new autodoc */
            in_autodoc = TRUE;
            current_autodoc.line_number = line_number;
            current_autodoc.is_internal = is_internal_autodoc(line);
            current_autodoc.is_obsolete = is_obsolete_autodoc(line);
            
            /* Extract module/function name */
            module_func = extract_module_function(line);
            if (module_func) {
                current_autodoc.module_name = module_func;
                /* Try to split module and function */
                slash = strchr(module_func, '/');
                if (slash) {
                    /* Make a copy of the function name part */
                    current_autodoc.function_name = strdup_amiga(slash + 1);
                } else {
                    /* No slash found, use the whole module name as function name */
                    current_autodoc.function_name = strdup_amiga(module_func);
                }
                
                /* Debug output */
                if (config->verbose) {
                    char *module_str;
                    char *function_str;
                    
                    if (current_autodoc.module_name) {
                        module_str = current_autodoc.module_name;
                    } else {
                        module_str = "NULL";
                    }
                    
                    if (current_autodoc.function_name) {
                        function_str = current_autodoc.function_name;
                    } else {
                        function_str = "NULL";
                    }
                    
                    Printf("GenDo: Module: %s, Function: %s\n", module_str, function_str);
                }
            }
            
            /* Parse autodoc content */
            if (!parse_autodoc_section(file_handle, &current_autodoc, line)) {
                in_autodoc = FALSE;
            }
        }
        /* Check for autodoc end */
        else if (in_autodoc && is_autodoc_end(line)) {
            /* Finish current autodoc */
            if (current_autodoc.name) {
                /* Add to config autodocs array */
                if (config->autodoc_count < MAX_AUTODOCS) {
                    config->autodocs[config->autodoc_count] = current_autodoc;
                    config->autodoc_count++;
                }
            }
            in_autodoc = FALSE;
        }
        /* Process autodoc content */
        else if (in_autodoc) {
            /* This will be handled by parse_autodoc_section */
        }
    }
    
    /* Finish last autodoc if file ended while in one */
    if (in_autodoc && current_autodoc.name) {
        if (config->autodoc_count < MAX_AUTODOCS) {
            config->autodocs[config->autodoc_count] = current_autodoc;
            config->autodoc_count++;
        }
    }
    
    Close(file_handle);
    return TRUE;
}

/* Check if line starts an autodoc */
BOOL is_autodoc_start(const char *line)
{
    const char *p = line;
    
    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t') p++;
    
    /* Check for /****** or ******* pattern */
    if (strncmp(p, "/****** ", 8) == 0) {
        return TRUE;
    }
    if (strncmp(p, "******* ", 8) == 0) {
        return TRUE;
    }
    
    /* Check for internal marker /****i* */
    if (strncmp(p, "/****i* ", 8) == 0) {
        return TRUE;
    }
    
    /* Check for author marker /****h* */
    if (strncmp(p, "/****h* ", 8) == 0) {
        return TRUE;
    }
    
    /* Check for obsolete marker /****o* */
    if (strncmp(p, "/****o* ", 8) == 0) {
        return TRUE;
    }
    
    /* Check for flexible marker pattern: /****XX* where XX can be any 2 chars */
    if (p[0] == '/' && p[1] == '*' && p[2] == '*' && p[3] == '*' && 
        p[4] == '*' && p[5] == '*' && p[6] != '*' && p[7] == ' ') {
        return TRUE;
    }
    
    return FALSE;
}

/* Check if line ends an autodoc */
BOOL is_autodoc_end(const char *line)
{
    const char *p = line;
    
    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t') p++;
    
    /* Look for exactly "***" at start of line (after whitespace) */
    if (strncmp(p, "***", 3) == 0) {
        return TRUE;
    }
    
    return FALSE;
}

/* Check if autodoc is internal (6th character is 'i') */
BOOL is_internal_autodoc(const char *line)
{
    const char *p = line;
    
    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t') p++;
    
    /* Check for /****i* pattern */
    if (strncmp(p, "/****i* ", 8) == 0) {
        return TRUE;
    }
    
    /* Check for flexible pattern where 6th character is 'i' */
    if (p[0] == '/' && p[1] == '*' && p[2] == '*' && p[3] == '*' && 
        p[4] == '*' && p[5] == '*' && p[6] == 'i' && p[7] == ' ') {
        return TRUE;
    }
    
    return FALSE;
}

/* Check if autodoc is obsolete (6th character is 'o') */
BOOL is_obsolete_autodoc(const char *line)
{
    const char *p = line;
    
    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t') p++;
    
    /* Check for /****o* pattern */
    if (strncmp(p, "/****o* ", 8) == 0) {
        return TRUE;
    }
    
    /* Check for flexible pattern where 6th character is 'o' */
    if (p[0] == '/' && p[1] == '*' && p[2] == '*' && p[3] == '*' && 
        p[4] == '*' && p[5] == '*' && p[6] == 'o' && p[7] == ' ') {
        return TRUE;
    }
    
    return FALSE;
}

/* Extract module/function name from autodoc header line */
STRPTR extract_module_function(const char *line)
{
    const char *start = line;
    const char *end;
    LONG len;
    STRPTR result;
    
    /* Skip leading whitespace */
    while (*start == ' ' || *start == '\t') start++;
    
    /* Skip the autodoc start pattern - handle all marker types */
    if (strncmp(start, "/****** ", 8) == 0) {
        start += 8;
    } else if (strncmp(start, "******* ", 8) == 0) {
        start += 8;
    } else if (strncmp(start, "/****i* ", 8) == 0) {
        start += 8;
    } else if (strncmp(start, "/****h* ", 8) == 0) {
        start += 8;
    } else if (strncmp(start, "/****o* ", 8) == 0) {
        start += 8;
    } else if (start[0] == '/' && start[1] == '*' && start[2] == '*' && 
               start[3] == '*' && start[4] == '*' && start[5] == '*' && 
               start[6] != '*' && start[7] == ' ') {
        /* Flexible marker pattern */
        start += 8;
    } else {
        return NULL;
    }
    
    /* Find end of module/function name (space or end of line) */
    end = start;
    while (*end && *end != ' ' && *end != '\n' && *end != '\r' && *end != '\t') end++;
    
    /* Validate that we found a reasonable module/function name */
    len = end - start;
    if (len > 0 && len < 100) {
        /* Create a properly null-terminated string */
        result = AllocVec(len + 1, MEMF_CLEAR);
        if (result) {
            strncpy(result, start, len);
            result[len] = '\0';
        }
        return result;
    }
    
    return NULL;
}

/* Clean up content by removing excessive newlines and normalizing whitespace */
STRPTR clean_content(const char *content)
{
    LONG len;
    STRPTR cleaned;
    const char *src;
    char *dst;
    BOOL last_was_newline;
    BOOL last_was_cr;
    BOOL last_was_space;
    
    len = strlen(content);
    cleaned = AllocVec(len + 1, MEMF_CLEAR);
    if (!cleaned) return NULL;
    
    src = content;
    dst = (char*)cleaned;
    last_was_newline = FALSE;
    last_was_cr = FALSE;
    last_was_space = FALSE;
    
    while (*src) {
        if (*src == '\n') {
            if (!last_was_newline) {
                *dst++ = *src;
            }
            last_was_newline = TRUE;
            last_was_cr = FALSE;
            last_was_space = FALSE;
        } else if (*src == '\r') {
            if (!last_was_cr) {
                *dst++ = *src;
            }
            last_was_cr = TRUE;
            last_was_newline = FALSE;
            last_was_space = FALSE;
        } else if (*src == '\f') {
            /* Preserve form feed characters */
            *dst++ = *src;
            last_was_newline = FALSE;
            last_was_cr = FALSE;
            last_was_space = FALSE;
        } else if (*src == ' ') {
            /* Collapse multiple spaces into single space, but preserve structure */
            if (!last_was_space && !last_was_newline && !last_was_cr) {
                *dst++ = ' ';
            }
            last_was_space = TRUE;
            last_was_newline = FALSE;
            last_was_cr = FALSE;
        } else if (*src == '\t') {
            /* Preserve tabs as they are important for autodoc formatting */
            *dst++ = *src;
            last_was_newline = FALSE;
            last_was_cr = FALSE;
            last_was_space = FALSE;
        } else {
            /* Handle special characters that might be UTF-8 encoded */
            if ((unsigned char)*src == 0xCF && (unsigned char)*(src+1) == 0x80) {
                /* UTF-8 ? (0xCF 0x80) -> Latin-1 ? (0xF0) */
                *dst++ = (unsigned char)0xF0;
                src++; /* Skip the second byte */
            } else if ((unsigned char)*src == 0xC3 && (unsigned char)*(src+1) == 0x80) {
                /* UTF-8 À (0xC3 0x80) -> Latin-1 À (0xC0) */
                *dst++ = (unsigned char)0xC0;
                src++; /* Skip the second byte */
            } else if ((unsigned char)*src == 0xC3 && (unsigned char)*(src+1) == 0x9F) {
                /* UTF-8 ß (0xC3 0x9F) -> Latin-1 ß (0xDF) */
                *dst++ = (unsigned char)0xDF;
                src++; /* Skip the second byte */
            } else {
                /* Preserve all other characters including special Latin-1 characters */
                *dst++ = *src;
            }
            last_was_newline = FALSE;
            last_was_cr = FALSE;
            last_was_space = FALSE;
        }
        src++;
    }
    
    /* Remove trailing whitespace and newlines */
    while (dst > (char*)cleaned && (dst[-1] == '\n' || dst[-1] == '\r' || 
           dst[-1] == ' ' || dst[-1] == '\t')) {
        dst--;
    }
    *dst = '\0';
    
    return cleaned;
}

/* Store section content in the appropriate autodoc field */
void store_section_content(Autodoc *autodoc, const char *section, const char *content)
{
    STRPTR cleaned = clean_content(content);
    if (!cleaned) return;
    
    if (strcmp(section, "NAME") == 0) {
        autodoc->name = cleaned;
    }
    else if (strcmp(section, "SYNOPSIS") == 0) {
        autodoc->synopsis = cleaned;
    }
    else if (strcmp(section, "FUNCTION") == 0) {
        autodoc->function_desc = cleaned;
    }
    else if (strcmp(section, "INPUTS") == 0) {
        autodoc->inputs = cleaned;
    }
    else if (strcmp(section, "RESULT") == 0) {
        autodoc->result = cleaned;
    }
    else if (strcmp(section, "EXAMPLE") == 0) {
        autodoc->example = cleaned;
    }
    else if (strcmp(section, "NOTES") == 0) {
        autodoc->notes = cleaned;
    }
    else if (strcmp(section, "BUGS") == 0) {
        autodoc->bugs = cleaned;
    }
    else if (strcmp(section, "SEE ALSO") == 0) {
        autodoc->see_also = cleaned;
    }
    else {
        /* Free cleaned content if section not recognized */
        FreeVec(cleaned);
    }
}

/* Parse autodoc section content */
BOOL parse_autodoc_section(BPTR file, Autodoc *autodoc, STRPTR line)
{
    char current_line[MAX_LINE_LENGTH];
    STRPTR current_section = NULL;
    STRPTR section_content = NULL;
    LONG content_len = 0;
    LONG max_content = 4096;
    
    /* Allocate initial content buffer */
    section_content = AllocMem(max_content, MEMF_CLEAR);
    if (!section_content) return FALSE;
    
    /* Process the header line first */
    if (strlen(line) > 0) {
        /* This is the header line, we already processed it */
    }
    
    /* Read autodoc content until we hit the end */
    while (FGets(file, current_line, sizeof(current_line))) {
        /* Process line content */
        
        /* Check for section headers using flexible recognition */
        STRPTR section_name = is_section_header(current_line);
        if (section_name) {
            /* Store previous section if it had content */
            if (current_section && content_len > 0) {
                section_content[content_len] = '\0';
                store_section_content(autodoc, current_section, section_content);
            }
            current_section = section_name;
            content_len = 0;
        }
        /* Check for autodoc end */
        else if (is_autodoc_end(current_line)) {
            break;
        }
        /* Process content line */
        else if (current_section) {
            /* Handle lines that start with asterisk (standard autodoc format) */
            if (strncmp(current_line, "*", 1) == 0) {
                /* Skip the leading "*" and preserve original indentation */
                const char *content;
                LONG line_len;
                const char *p = current_line + 1;
                
                /* Skip only spaces after "*", preserve tabs and other formatting */
                while (*p == ' ') p++;
                
                content = p;
                line_len = strlen(content);
                
                /* Remove trailing whitespace but preserve the line structure */
                while (line_len > 0 && (content[line_len-1] == ' ' || content[line_len-1] == '\n' || content[line_len-1] == '\r')) {
                    line_len--;
                }
                
                /* Add the line if it has content or if it's an empty line (preserve structure) */
                if (content_len + line_len + 2 < max_content) {
                    if (line_len > 0) {
                        strncpy(section_content + content_len, content, line_len);
                        content_len += line_len;
                    }
                    section_content[content_len] = '\n';
                    content_len++;
                }
            }
            /* Handle lines that don't start with asterisk but are still content */
            else if (strlen(current_line) > 0) {
                /* Skip leading whitespace */
                const char *content = current_line;
                while (*content == ' ' || *content == '\t') content++;
                
                /* Only process if there's actual content */
                if (*content && *content != '\n' && *content != '\r') {
                    LONG line_len = strlen(content);
                    
                    /* Remove trailing whitespace */
                    while (line_len > 0 && (content[line_len-1] == ' ' || content[line_len-1] == '\t' || 
                           content[line_len-1] == '\n' || content[line_len-1] == '\r')) {
                        line_len--;
                    }
                    
                    /* Add the line if there's content */
                    if (line_len > 0 && content_len + line_len + 2 < max_content) {
                        strncpy(section_content + content_len, content, line_len);
                        content_len += line_len;
                        section_content[content_len] = '\n';
                        content_len++;
                    }
                }
            }
        }
    }
    
    /* Store the last section if it had content */
    if (current_section && content_len > 0) {
        section_content[content_len] = '\0';
        store_section_content(autodoc, current_section, section_content);
    }
    
    FreeMem(section_content, max_content);
    return TRUE;
}

/* Generate .doc format output */
BOOL generate_doc_output(Config *config)
{
    BPTR file_handle;
    LONG i;
    Autodoc *doc;
    
    /* Open output file */
    file_handle = Open(config->output_doc, MODE_NEWFILE);
    if (!file_handle) {
        LONG error = IoErr();
        Printf("GenDo: Cannot create output file: %s\n", config->output_doc);
        Printf("GenDo: Error code: %ld\n", error);
        if (error == ERROR_OBJECT_EXISTS) {
            Printf("GenDo: File already exists. Delete it first or use a different name.\n");
        } else if (error == ERROR_DISK_FULL) {
            Printf("GenDo: Disk is full.\n");
        } else if (error == ERROR_WRITE_PROTECTED) {
            Printf("GenDo: Disk is write protected.\n");
        } else if (error == ERROR_DIR_NOT_FOUND) {
            Printf("GenDo: Directory not found. Check the path.\n");
        }
        return FALSE;
    }
    
    /* Write table of contents if not disabled */
    if (!config->no_toc) {
        FPrintf(file_handle, "TABLE OF CONTENTS\n\n");
        
        for (i = 0; i < config->autodoc_count; i++) {
            if (config->autodocs[i].module_name) {
                FPrintf(file_handle, "%s\n", config->autodocs[i].module_name);
            }
        }
        FPrintf(file_handle, "\n");
        
        /* Add form feed after table of contents */
        if (!config->no_form_feed) {
            FPrintf(file_handle, "\f");
        }
    }
    
    /* Write autodocs */
    for (i = 0; i < config->autodoc_count; i++) {
        doc = &config->autodocs[i];
        
        if (doc->module_name) {
            FPrintf(file_handle, "\f%s                                                       %s\n", doc->module_name, doc->module_name);
            FPrintf(file_handle, " \n");
            
            if (doc->name) {
                FPrintf(file_handle, "   NAME\n");
                FPrintf(file_handle, "%s\n\n", doc->name);
            }
            
            if (doc->synopsis) {
                FPrintf(file_handle, "   SYNOPSIS\n");
                FPrintf(file_handle, "%s\n\n", doc->synopsis);
            }
            
            if (doc->function_desc) {
                FPrintf(file_handle, "   FUNCTION\n");
                FPrintf(file_handle, "%s\n\n", doc->function_desc);
            }
            
            if (doc->inputs) {
                FPrintf(file_handle, "   INPUTS\n");
                FPrintf(file_handle, "%s\n\n", doc->inputs);
            }
            
            if (doc->result) {
                FPrintf(file_handle, "   RESULT\n");
                FPrintf(file_handle, "%s\n\n", doc->result);
            }
            
            if (doc->example) {
                FPrintf(file_handle, "   EXAMPLE\n");
                FPrintf(file_handle, "%s\n\n", doc->example);
            }
            
            if (doc->notes) {
                FPrintf(file_handle, "   NOTES\n");
                FPrintf(file_handle, "%s\n\n", doc->notes);
            }
            
            if (doc->bugs) {
                FPrintf(file_handle, "   BUGS\n");
                FPrintf(file_handle, "%s\n\n", doc->bugs);
            }
            
            if (doc->see_also) {
                FPrintf(file_handle, "   SEE ALSO\n");
                FPrintf(file_handle, "%s\n\n", doc->see_also);
            }
            
            FPrintf(file_handle, " \n");
        }
    }
    
    Close(file_handle);
    return TRUE;
}

/* Generate AmigaGuide format output */
BOOL generate_guide_output(Config *config)
{
    BPTR file_handle;
    LONG i;
    Autodoc *doc;
    
    if (!config->output_guide) return TRUE;
    
    /* Open output file */
    file_handle = Open(config->output_guide, MODE_NEWFILE);
    if (!file_handle) {
        Printf("GenDo: Cannot create guide file: %s\n", config->output_guide);
        return FALSE;
    }
    
    /* Write AmigaGuide header */
    FPrintf(file_handle, "@database %s\n", config->output_guide);
    FPrintf(file_handle, "\n");
    FPrintf(file_handle, "@Node Main \"Amiga Autodoc Documentation\"\n");
    FPrintf(file_handle, "@Next \"toc\"\n");
    FPrintf(file_handle, "@Prev \"main\"\n");
    FPrintf(file_handle, "\n");
    FPrintf(file_handle, "Amiga Autodoc Documentation\n");
    FPrintf(file_handle, "Generated by GenDo v1.0\n");
    FPrintf(file_handle, "\n");
    FPrintf(file_handle, "$VER: %s 1.0 (Generated by GenDo)\n", config->output_guide);
    FPrintf(file_handle, "\n");
    FPrintf(file_handle, "This documentation contains autodocs extracted from source code.\n");
    FPrintf(file_handle, "\n");
    FPrintf(file_handle, "@{b}Table of Contents@{ub}\n");
    FPrintf(file_handle, "@{\"toc\" link \"View Table of Contents\"}\n");
    FPrintf(file_handle, "\n");
    FPrintf(file_handle, "@{b}Functions@{ub}\n");
    
    /* Write function links with proper formatting */
    for (i = 0; i < config->autodoc_count; i++) {
        if (config->autodocs[i].function_name) {
            FPrintf(file_handle, "@{\"%s\" link \"%s\"}\n", config->autodocs[i].function_name, config->autodocs[i].function_name);
        }
    }
    
    FPrintf(file_handle, "\n");
    FPrintf(file_handle, "@EndNode\n");
    
    /* Write table of contents node */
    FPrintf(file_handle, "@Node toc \"Table of Contents\"\n");
    FPrintf(file_handle, "@Next \"main\"\n");
    FPrintf(file_handle, "@Prev \"main\"\n");
    FPrintf(file_handle, "\n");
    FPrintf(file_handle, "@{b}Table of Contents@{ub}\n");
    FPrintf(file_handle, "\n");
    
    for (i = 0; i < config->autodoc_count; i++) {
        if (config->autodocs[i].function_name) {
            FPrintf(file_handle, "@{\"%s\" link \"%s\"}\n", config->autodocs[i].function_name, config->autodocs[i].function_name);
        }
    }
    
    FPrintf(file_handle, "\n");
    FPrintf(file_handle, "@{\"main\" link \"Back to Main\"}\n");
    FPrintf(file_handle, "\n");
    FPrintf(file_handle, "@EndNode\n");
    
    /* Write function nodes */
    for (i = 0; i < config->autodoc_count; i++) {
        doc = &config->autodocs[i];
        
        if (doc->function_name) {
            FPrintf(file_handle, "@Node %s \"%s\"\n", doc->function_name, doc->function_name);
            
            /* Set next link - last function links back to main */
            if (i == config->autodoc_count - 1) {
                FPrintf(file_handle, "@Next \"main\"\n");
            } else {
                FPrintf(file_handle, "@Next \"%s\"\n", config->autodocs[i + 1].function_name);
            }
            
            /* Set previous link - first function links back to main */
            if (i == 0) {
                FPrintf(file_handle, "@Prev \"main\"\n");
            } else {
                FPrintf(file_handle, "@Prev \"%s\"\n", config->autodocs[i - 1].function_name);
            }
            
            FPrintf(file_handle, "\n");
            
            FPrintf(file_handle, "@{b}%s@{ub}\n", doc->function_name);
            FPrintf(file_handle, "\n");
            
            if (doc->synopsis) {
                FPrintf(file_handle, "@{b}SYNOPSIS@{ub}\n");
                FPrintf(file_handle, "%s\n\n", doc->synopsis);
            }
            
            if (doc->function_desc) {
                FPrintf(file_handle, "@{b}FUNCTION@{ub}\n");
                FPrintf(file_handle, "%s\n\n", doc->function_desc);
            }
            
            if (doc->inputs) {
                FPrintf(file_handle, "@{b}INPUTS@{ub}\n");
                FPrintf(file_handle, "%s\n\n", doc->inputs);
            }
            
            if (doc->result) {
                FPrintf(file_handle, "@{b}RESULT@{ub}\n");
                FPrintf(file_handle, "%s\n\n", doc->result);
            }
            
            if (doc->example) {
                FPrintf(file_handle, "@{b}EXAMPLE@{ub}\n");
                FPrintf(file_handle, "%s\n\n", doc->example);
            }
            
            if (doc->notes) {
                FPrintf(file_handle, "@{b}NOTES@{ub}\n");
                FPrintf(file_handle, "%s\n\n", doc->notes);
            }
            
            if (doc->bugs) {
                FPrintf(file_handle, "@{b}BUGS@{ub}\n");
                FPrintf(file_handle, "%s\n\n", doc->bugs);
            }
            
            if (doc->see_also) {
                FPrintf(file_handle, "\n");
                FPrintf(file_handle, "@{b}SEE ALSO@{ub}\n");
                FPrintf(file_handle, "%s\n\n", doc->see_also);
            }
            
            FPrintf(file_handle, "@{\"main\" link \"Back to Main\"}\n");
            FPrintf(file_handle, "\n");
            FPrintf(file_handle, "@EndNode\n");
        }
    }
    
    Close(file_handle);
    return TRUE;
}

/* Clean up configuration and allocated memory */
void cleanup_config(Config *config)
{
    LONG i;
    Autodoc *doc;
    
    /* Free output_doc and output_guide - they are now allocated memory */
    if (config->output_doc) {
        FreeVec(config->output_doc);
    }
    
    if (config->source_files) {
        for (i = 0; i < config->file_count; i++) {
            if (config->source_files[i].filename) {
                FreeVec(config->source_files[i].filename);
            }
        }
        FreeVec(config->source_files);
    }
    
    if (config->autodocs) {
        for (i = 0; i < config->autodoc_count; i++) {
            doc = &config->autodocs[i];
            if (doc->module_name) FreeVec(doc->module_name);
            if (doc->function_name) FreeVec(doc->function_name);
            if (doc->name) FreeVec(doc->name);
            if (doc->synopsis) FreeVec(doc->synopsis);
            if (doc->function_desc) FreeVec(doc->function_desc);
            if (doc->inputs) FreeVec(doc->inputs);
            if (doc->result) FreeVec(doc->result);
            if (doc->example) FreeVec(doc->example);
            if (doc->notes) FreeVec(doc->notes);
            if (doc->bugs) FreeVec(doc->bugs);
            if (doc->see_also) FreeVec(doc->see_also);
        }
        FreeVec(config->autodocs);
    }
    
    if (config->output_guide) {
        FreeVec(config->output_guide);
    }
}

/* Print usage information */
void print_usage(void)
{
    Printf("GenDo - Amiga Documentation Generator v1.0\n");
    Printf("Usage: GenDo FILES=file1,file2,... TO=output.doc [AMIGAGUIDE] [options]\n");
    Printf("\n");
    Printf("Parameters:\n");
    Printf("  FILES=file1,file2,...  Source files to process (supports Amiga wildcards)\n");
    Printf("  TO=filename.doc        Output .doc file\n");
    Printf("  AMIGAGUIDE            Generate AmigaGuide output (.guide file)\n");
    Printf("  VERBOSE               Show verbose output\n");
    Printf("  LINELENGTH=n          Set line length (default: 78)\n");
    Printf("  WORDWRAP              Enable word wrapping (default)\n");
    Printf("  CONVERTCOMMENTS       Convert comments (default)\n");
    Printf("  NOFORMFEED            Disable form feeds\n");
    Printf("  NOTOC                 Disable table of contents\n");
    Printf("  PRESERVEORDER         Preserve original order (don't sort alphabetically)\n");

}

/* Main entry point */
int main(int argc, char *argv[])
{
    Config config;
    LONG result = RETURN_OK;
    
    /* Open required libraries */
    UtilityBase = OpenLibrary("utility.library", 37);
    if (!UtilityBase) {
        Printf("GenDo: Cannot open utility.library\n");
        return RETURN_FAIL;
    }
    
    /* Initialize config */
    memset(&config, 0, sizeof(Config));
    
    /* Parse command line */
    if (parse_command_line(&config, 0, NULL) != RETURN_OK) {
        print_usage();
        result = RETURN_FAIL;
        goto cleanup;
    }
    
    /* Allocate memory for autodocs */
    config.autodocs = AllocVec(MAX_AUTODOCS * sizeof(Autodoc), MEMF_CLEAR);
    if (!config.autodocs) {
        Printf("GenDo: Out of memory\n");
        result = RETURN_FAIL;
        goto cleanup;
    }
    
    /* Process source files */
    if (config.file_count == 0) {
        Printf("GenDo: No source files specified\n");
        result = RETURN_FAIL;
        goto cleanup;
    }
    
    if (!process_source_files(&config)) {
        Printf("GenDo: Failed to process source files\n");
        result = RETURN_FAIL;
        goto cleanup;
    }
    
    /* Sort autodocs alphabetically unless preserve order is requested */
    if (!config.preserve_order) {
        sort_autodocs(&config);
    }
    
    /* Check if any autodocs were found */
    if (config.autodoc_count == 0) {
        if (config.verbose) {
            Printf("GenDo: No autodoc content found in source files\n");
        }
        result = RETURN_OK;  /* Not an error, just no content */
        goto cleanup;
    }
    
    /* Generate output files */
    if (!generate_doc_output(&config)) {
        Printf("GenDo: Failed to generate .doc output\n");
        result = RETURN_FAIL;
        goto cleanup;
    }
    
    if (config.generate_guide) {
        if (!generate_guide_output(&config)) {
            Printf("GenDo: Failed to generate AmigaGuide output\n");
            result = RETURN_FAIL;
            goto cleanup;
        }
        Printf("GenDo: Generated %s and %s\n", config.output_doc, config.output_guide);
    } else {
        Printf("GenDo: Generated %s\n", config.output_doc);
    }
    
cleanup:
    /* Clean up */
    cleanup_config(&config);
    
    if (UtilityBase) {
        CloseLibrary(UtilityBase);
    }
    
    return result;
}

