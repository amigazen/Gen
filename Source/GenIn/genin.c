/*
 * Copyright (c) 2025 amigazen project
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * GenIn
* 
 * This tool generates .info files from specification files or command-line parameters.
 * It supports both standard Workbench icons and custom image-based icons.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/imageclass.h>
#include <workbench/workbench.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <graphics/gfx.h>
#include <graphics/displayinfo.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <graphics/gfxbase.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <utility/utility.h>
#include <workbench/startup.h>
#include <ctype.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/datatypes.h>
#include <proto/utility.h>

/* Simple strlen function since utility.library doesn't have it */
LONG my_strlen(const char *str)
{
    const char *s = str;
    while (*s) s++;
    return s - str;
}

/* Maximum sizes and limits */
#define MAX_LINE_LENGTH 256
#define MAX_PARAM_LENGTH 128
#define MAX_VALUE_LENGTH 256
#define MAX_TOOLTYPES 16
#define ICON_SIZE 128

/* Parameter types */
typedef enum {
    PARAM_TYPE,
    PARAM_STACK,
    PARAM_TOOLTYPE,
    PARAM_TARGET,
    PARAM_IMAGE,
    PARAM_DEFICON,
    PARAM_UNKNOWN
} ParamType;

/* Configuration structure */
typedef struct {
    STRPTR type;
    LONG stack;
    STRPTR tooltypes[MAX_TOOLTYPES];
    LONG tooltype_count;
    STRPTR target;
    STRPTR resolved_target;
    STRPTR image;
    STRPTR deficon;
    BOOL force;
} Config;

/* Function prototypes */
LONG my_strlen(const char *str);
ParamType parse_param_type(STRPTR param);
BOOL parse_config_file(STRPTR filename, Config *config);
BOOL parse_single_icon_config(BPTR file, Config *config);
BOOL validate_config(Config *config);
struct DiskObject *load_default_icon(STRPTR deficon);
struct DiskObject *load_standard_deficon(STRPTR type);
STRPTR load_and_process_image(STRPTR image_path);
BOOL create_info_file(Config *config, struct DiskObject *source_diskobj);
void cleanup_config(Config *config);
void print_usage(void);
STRPTR resolve_target_path(STRPTR spec_file, STRPTR target);
BOOL validate_filename(STRPTR filename);
STRPTR strip_info_extension(STRPTR filename);

// Helper to trim whitespace in-place (returns pointer to trimmed string)
char *trim_whitespace(char *str) {
    char *end;
    while (*str == ' ' || *str == '\t') str++;
    if (*str == 0) return str;
    end = str + my_strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t')) end--;
    *(end+1) = 0;
    return str;
}

int main(int argc, char *argv[])
{
    struct RDArgs *rda;
    Config config;
    STRPTR spec_file = NULL;
    struct DiskObject *source_diskobj = NULL;
    LONG retcode = RETURN_OK;
    BOOL success = FALSE;
    
    /* Initialize configuration */
    {
        UBYTE *ptr = (UBYTE *)&config;
        LONG i;
        for (i = 0; i < sizeof(Config); i++) {
            ptr[i] = 0;
        }
    }
    
    /* Set default stack size */
    config.stack = 4096;
    
    /* Parse command line arguments */
    {
        static UBYTE template[] = "SPECFILE/K,TYPE/K,STACK/K,TARGET/K,IMAGE/K,DEFICON/K,TOOLTYPE/K,FORCE/S,HELP/S";
        LONG args[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0}; /* spec_file, type, stack, target, image, deficon, tooltype, force, help */
        rda = ReadArgs(template, args, NULL);
        
        /* Check if help was requested */
        if (args[8] != 0) {
            print_usage();
            return RETURN_OK;
        }
        
        /* Check for valid arguments */
        if (!rda) {
            Printf("GenIn: Invalid arguments\n");
            return RETURN_ERROR;
        }
        
        /* Either SPECFILE or TARGET must be provided */
        if (args[0] == 0 && args[3] == 0) {
            Printf("GenIn: Either SPECFILE or TARGET argument is required\n");
            return RETURN_ERROR;
        }
        
        spec_file = (STRPTR)args[0];
        config.force = (args[7] != 0);
        
        /* Set command line parameters if provided */
        if (args[1]) config.type = (STRPTR)args[1];
        if (args[2]) {
            /* Convert stack string to number */
            config.stack = 0;
            {
                STRPTR stack_str = (STRPTR)args[2];
                LONG i;
                for (i = 0; stack_str[i] != '\0'; i++) {
                    if (stack_str[i] >= '0' && stack_str[i] <= '9') {
                        config.stack = config.stack * 10 + (stack_str[i] - '0');
                    }
                }
            }
        }
        /* If no STACK provided, default of 4096 is already set */
        if (args[3]) config.target = (STRPTR)args[3];
        if (args[4]) config.image = (STRPTR)args[4];
        if (args[5]) config.deficon = (STRPTR)args[5];
        if (args[6]) {
            /* Add tooltype from command line */
            if (config.tooltype_count < MAX_TOOLTYPES) {
                config.tooltypes[config.tooltype_count] = (STRPTR)args[6];
                config.tooltype_count++;
            }
        }
    }
    
    /* Open required libraries */
    IconBase = (struct Library *)OpenLibrary("icon.library", 0);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 0);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 0);
    DataTypesBase = (struct Library *)OpenLibrary("datatypes.library", 0);
    UtilityBase = (struct Library *)OpenLibrary("utility.library", 0);
    
    if (!IconBase || !IntuitionBase || !GfxBase || !DataTypesBase || !UtilityBase) {
        Printf("GenIn: Failed to open required libraries\n");
        goto cleanup;
    }
    
    /* Parse configuration file if provided, otherwise use command line parameters */
    if (spec_file) {
        if (!parse_config_file(spec_file, &config)) {
            Printf("GenIn: Failed to parse configuration file '%s'\n", spec_file);
            goto cleanup;
        }
    } else {
        /* Use command line parameters directly */
        if (!validate_config(&config)) {
            Printf("GenIn: Invalid configuration\n");
            goto cleanup;
        }
        
        /* Resolve target path for command line target */
        config.resolved_target = AllocVec(my_strlen(config.target) + 1, MEMF_CLEAR);
        if (config.resolved_target) {
            Strncpy((char *)config.resolved_target, (char *)config.target, my_strlen(config.target));
            config.resolved_target[my_strlen(config.target)] = '\0';
        } else {
            Printf("GenIn: Failed to resolve target path\n");
            goto cleanup;
        }
        
        /* Strip .info extension from resolved target */
        {
            STRPTR stripped_target = strip_info_extension(config.resolved_target);
            if (stripped_target) {
                FreeVec(config.resolved_target);
                config.resolved_target = stripped_target;
            } else {
                Printf("GenIn: Failed to process target filename\n");
                goto cleanup;
            }
        }
        
        Printf("GenIn: Resolved target path: '%s'\n", config.resolved_target);
        
        /* Check if target file already exists */
        if (!config.force) {
            UBYTE target_path[512];
            BPTR file;
            
            /* Construct full path with .info extension for existence check */
            Strncpy((char *)target_path, (char *)config.resolved_target, sizeof(target_path) - 1);
            target_path[sizeof(target_path) - 1] = '\0';
            if (!AddPart((char *)target_path, ".info", sizeof(target_path))) {
                Printf("GenIn: Path too long when checking target existence\n");
                retcode = RETURN_ERROR;
                goto cleanup;
            }
            
            file = Open((STRPTR)target_path, MODE_OLDFILE);
            if (file) {
                Close(file);
                Printf("GenIn: Target file '%s' already exists. Use FORCE to overwrite.\n", (char *)target_path);
                retcode = RETURN_ERROR;
                goto cleanup;
            }
        }
        
        /* Load icon image */
        if (config.image) {
            /* TODO: Implement image loading and conversion to DiskObject */
            Printf("GenIn: Image loading not yet implemented\n");
            goto cleanup;
        } else if (config.deficon) {
            /* Try ENVARC:Sys/def_ first, then fall back to standard deficon using TYPE */
            source_diskobj = load_default_icon(config.deficon);
                            if (!source_diskobj) {
                    /* Fall back to standard deficon using TYPE */
                    Printf("GenIn: Falling back to standard deficon using TYPE '%s'\n", config.type);
                    source_diskobj = load_standard_deficon(config.type);
                    if (!source_diskobj) {
                        Printf("GenIn: Failed to load any deficon for '%s'\n", config.deficon);
                        goto cleanup;
                    }
                }
        } else {
            /* No DEFICON specified, use TYPE for standard deficon */
            Printf("GenIn: No DEFICON specified, using TYPE '%s' for standard deficon\n", config.type);
            source_diskobj = load_standard_deficon(config.type);
            if (!source_diskobj) {
                Printf("GenIn: Failed to load standard deficon for type '%s'\n", config.type);
                goto cleanup;
            }
        }
        
        /* Create .info file */
        if (!create_info_file(&config, source_diskobj)) {
            Printf("GenIn: Failed to create .info file\n");
            retcode = RETURN_ERROR;
            goto cleanup;
        }
        
        Printf("GenIn: Successfully created '%s.info'\n", config.resolved_target);
        success = TRUE;
        goto cleanup;
    }
    
    /* All processing is now handled in parse_config_file or the command line branch above */
    success = TRUE;
    
cleanup:
    /* Cleanup */
    if (source_diskobj) {
        FreeDiskObject(source_diskobj);
    }
    cleanup_config(&config);
    
    if (rda) {
        FreeArgs(rda);
    }
    
    if (IconBase) CloseLibrary(IconBase);
    if (IntuitionBase) CloseLibrary(IntuitionBase);
    if (GfxBase) CloseLibrary(GfxBase);
    if (DataTypesBase) CloseLibrary(DataTypesBase);
    if (UtilityBase) CloseLibrary(UtilityBase);
    
    return retcode;
}

ParamType parse_param_type(STRPTR param)
{
    if (Stricmp(param, "TYPE") == 0) return PARAM_TYPE;
    if (Stricmp(param, "STACK") == 0) return PARAM_STACK;
    if (Stricmp(param, "TOOLTYPE") == 0) return PARAM_TOOLTYPE;
    if (Stricmp(param, "TARGET") == 0) return PARAM_TARGET;
    if (Stricmp(param, "IMAGE") == 0) return PARAM_IMAGE;
    if (Stricmp(param, "DEFICON") == 0) return PARAM_DEFICON;
    return PARAM_UNKNOWN;
}

BOOL parse_config_file(STRPTR filename, Config *config)
{
    BPTR file;
    BOOL success = TRUE;
    
    file = Open(filename, MODE_OLDFILE);
    if (!file) {
        return FALSE;
    }
    
    /* Parse multiple icon definitions */
    while (success) {
        /* Parse a single icon definition */
        success = parse_single_icon_config(file, config);
        
        if (success) {
            /* Create the icon */
            struct DiskObject *source_diskobj = NULL;
            
            /* Validate configuration */
            if (!validate_config(config)) {
                Printf("GenIn: Invalid configuration\n");
                success = FALSE;
                break;
            }
            
            /* Resolve target path */
            if (filename) {
                /* Resolve relative to spec file location */
                config->resolved_target = resolve_target_path(filename, config->target);
            } else {
                /* Command line target - use as-is */
                config->resolved_target = AllocVec(my_strlen(config->target) + 1, MEMF_CLEAR);
                if (config->resolved_target) {
                    Strncpy((char *)config->resolved_target, (char *)config->target, my_strlen(config->target));
                    config->resolved_target[my_strlen(config->target)] = '\0';
                }
            }
            
            if (!config->resolved_target) {
                Printf("GenIn: Failed to resolve target path\n");
                success = FALSE;
                break;
            }
            
            /* Strip .info extension from resolved target */
            {
                STRPTR stripped_target = strip_info_extension(config->resolved_target);
                if (stripped_target) {
                    FreeVec(config->resolved_target);
                    config->resolved_target = stripped_target;
                } else {
                    Printf("GenIn: Failed to process target filename\n");
                    success = FALSE;
                    break;
                }
            }
            
            Printf("GenIn: Resolved target path: '%s'\n", config->resolved_target);
            
            /* Check if target file already exists */
            if (!config->force) {
                UBYTE target_path[512];
                BPTR check_file;
                
                /* Construct full path with .info extension for existence check */
                Strncpy((char *)target_path, (char *)config->resolved_target, sizeof(target_path) - 1);
                target_path[sizeof(target_path) - 1] = '\0';
                if (!AddPart((char *)target_path, ".info", sizeof(target_path))) {
                    Printf("GenIn: Path too long when checking target existence\n");
                    success = FALSE;
                    break;
                }
                
                check_file = Open((STRPTR)target_path, MODE_OLDFILE);
                if (check_file) {
                    Close(check_file);
                    Printf("GenIn: Target file '%s' already exists. Use FORCE to overwrite.\n", (char *)target_path);
                    success = FALSE;
                    break;
                }
            }
            
            /* Load icon image */
            if (config->image) {
                /* TODO: Implement image loading and conversion to DiskObject */
                Printf("GenIn: Image loading not yet implemented\n");
                success = FALSE;
                break;
            } else if (config->deficon) {
                /* Try ENVARC:Sys/def_ first, then fall back to standard deficon using TYPE */
                source_diskobj = load_default_icon(config->deficon);
                if (!source_diskobj) {
                    /* Fall back to standard deficon using TYPE */
                    Printf("GenIn: Falling back to standard deficon using TYPE '%s'\n", config->type);
                    source_diskobj = load_standard_deficon(config->type);
                    if (!source_diskobj) {
                        Printf("GenIn: Failed to load any deficon for '%s'\n", config->deficon);
                        success = FALSE;
                        break;
                    }
                }
            } else {
                /* No DEFICON specified, use TYPE for standard deficon */
                Printf("GenIn: No DEFICON specified, using TYPE '%s' for standard deficon\n", config->type);
                source_diskobj = load_standard_deficon(config->type);
                if (!source_diskobj) {
                    Printf("GenIn: Failed to load standard deficon for type '%s'\n", config->type);
                    success = FALSE;
                    break;
                }
            }
            
            /* Create .info file */
            if (!create_info_file(config, source_diskobj)) {
                Printf("GenIn: Failed to create .info file\n");
                success = FALSE;
                break;
            }
            
            Printf("GenIn: Successfully created '%s.info'\n", config->resolved_target);
            
            /* Cleanup for this icon */
            if (source_diskobj) {
                FreeDiskObject(source_diskobj);
            }
            cleanup_config(config);
            
            /* Reset config for next icon */
            {
                UBYTE *ptr = (UBYTE *)config;
                LONG i;
                for (i = 0; i < sizeof(Config); i++) {
                    ptr[i] = 0;
                }
            }
        }
    }
    
    Close(file);
    return success;
}

BOOL parse_single_icon_config(BPTR file, Config *config)
{
    UBYTE line[MAX_LINE_LENGTH];
    UBYTE param[MAX_PARAM_LENGTH];
    UBYTE value[MAX_VALUE_LENGTH];
    LONG len;
    STRPTR equals_pos;
    STRPTR value_start;
    STRPTR comment_pos;
    ParamType param_type;
    BOOL found_any_params = FALSE;
    char *trimmed_param;
    char *trimmed_value;
    
    while (FGets(file, line, sizeof(line))) {
        /* Get length of the line */
        len = my_strlen((char *)line);
        
        /* Remove newline and carriage return */
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
            len--;
        }
        if (len > 0 && line[len-1] == '\r') {
            line[len-1] = '\0';
            len--;
        }
        
        /* Skip empty lines */
        if (len == 0) {
            /* If we found any parameters, this empty line ends the icon definition */
            if (found_any_params) {
                return TRUE;
            }
            continue;
        }
        
        /* Handle comments - find ';' and truncate line */
        comment_pos = NULL;
        {
            LONG i;
            for (i = 0; line[i] != '\0'; i++) {
                if (line[i] == ';') {
                    comment_pos = &line[i];
                    break;
                }
            }
        }
        if (comment_pos) {
            *comment_pos = '\0';
            len = comment_pos - (STRPTR)line;
        }
        
        /* Skip lines that are only comments or whitespace */
        if (len == 0) {
            continue;
        }
        
        /* Find equals sign */
        {
            LONG i;
            equals_pos = NULL;
            for (i = 0; line[i] != '\0'; i++) {
                if (line[i] == '=') {
                    equals_pos = &line[i];
                    break;
                }
            }
        }
        if (!equals_pos) {
            continue;
        }
        
        /* Extract parameter name */
        len = equals_pos - (STRPTR)line;
        if (len >= sizeof(param)) {
            continue;
        }
        CopyMem(line, param, len);
        param[len] = '\0';
        
        /* Extract value */
        value_start = equals_pos + 1;
        while (*value_start == ' ' || *value_start == '\t') {
            value_start++;
        }
        
        /* Handle quoted values */
        if (*value_start == '"') {
            value_start++; /* Skip opening quote */
            {
                LONG value_len = my_strlen((char *)value_start);
                if (value_len > 0 && value_start[value_len-1] == '"') {
                    /* Temporarily null-terminate before the closing quote */
                    value_start[value_len-1] = '\0';
                }
            }
        }
        
        /* Copy value */
        Strncpy((char *)value, (char *)value_start, sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';
        
        /* Trim whitespace from parameter and value */
        trimmed_param = trim_whitespace((char *)param);
        trimmed_value = trim_whitespace((char *)value);
        
        /* Parse parameter */
        param_type = parse_param_type(trimmed_param);
        switch (param_type) {
            case PARAM_TYPE:
                if (config->type) {
                    Printf("GenIn: Multiple TYPE parameters specified\n");
                    return FALSE;
                }
                {
                    LONG value_len = my_strlen(trimmed_value);
                    config->type = AllocVec(value_len + 1, MEMF_CLEAR);
                    {
                        LONG i;
                        for (i = 0; i < value_len; i++) {
                            config->type[i] = trimmed_value[i];
                        }
                        config->type[value_len] = '\0';
                    }
                }
                Printf("GenIn: Parsed TYPE = '%s'\n", config->type);
                found_any_params = TRUE;
                break;
                
            case PARAM_STACK:
                if (config->stack != 4096) {
                    Printf("GenIn: Multiple STACK parameters specified\n");
                    return FALSE;
                }
                config->stack = 0;
                {
                    LONG i;
                    for (i = 0; trimmed_value[i] != '\0'; i++) {
                        if (trimmed_value[i] >= '0' && trimmed_value[i] <= '9') {
                            config->stack = config->stack * 10 + (trimmed_value[i] - '0');
                        }
                    }
                }
                Printf("GenIn: Parsed STACK = %ld\n", config->stack);
                found_any_params = TRUE;
                break;
                
            case PARAM_TOOLTYPE:
                if (config->tooltype_count >= MAX_TOOLTYPES) {
                    Printf("GenIn: Too many TOOLTYPE entries\n");
                    return FALSE;
                }
                
                /* Check if this is a key=value format */
                {
                    STRPTR key_end = NULL;
                    LONG i;
                    for (i = 0; trimmed_value[i] != '\0'; i++) {
                        if (trimmed_value[i] == '=') {
                            key_end = &trimmed_value[i];
                            break;
                        }
                    }
                    if (key_end) {
                        /* Extract key for uniqueness check */
                        LONG key_len = key_end - (char *)trimmed_value;
                        UBYTE key[MAX_PARAM_LENGTH];
                        LONG i;
                        
                        if (key_len >= sizeof(key)) {
                            Printf("GenIn: TOOLTYPE key too long\n");
                            return FALSE;
                        }
                        
                        CopyMem(trimmed_value, key, key_len);
                        key[key_len] = '\0';
                        
                        /* Check for duplicate keys */
                        for (i = 0; i < config->tooltype_count; i++) {
                            STRPTR existing_key_end = NULL;
                            LONG j;
                            for (j = 0; config->tooltypes[i][j] != '\0'; j++) {
                                if (config->tooltypes[i][j] == '=') {
                                    existing_key_end = &config->tooltypes[i][j];
                                    break;
                                }
                            }
                            if (existing_key_end) {
                                LONG existing_key_len = existing_key_end - config->tooltypes[i];
                                if (existing_key_len == key_len && 
                                    Stricmp((char *)key, config->tooltypes[i]) == 0) {
                                    Printf("GenIn: Duplicate TOOLTYPE key '%s'\n", (char *)key);
                                    return FALSE;
                                }
                            }
                        }
                    }
                }
                
                {
                    LONG value_len = my_strlen(trimmed_value);
                    config->tooltypes[config->tooltype_count] = AllocVec(value_len + 1, MEMF_CLEAR);
                    {
                        LONG i;
                        for (i = 0; i < value_len; i++) {
                            config->tooltypes[config->tooltype_count][i] = trimmed_value[i];
                        }
                        config->tooltypes[config->tooltype_count][value_len] = '\0';
                    }
                    config->tooltype_count++;
                }
                Printf("GenIn: Parsed TOOLTYPE[%ld] = '%s'\n", config->tooltype_count - 1, config->tooltypes[config->tooltype_count - 1]);
                found_any_params = TRUE;
                break;
                
            case PARAM_TARGET:
                if (config->target) {
                    Printf("GenIn: Multiple TARGET parameters specified\n");
                    return FALSE;
                }
                {
                    LONG value_len = my_strlen(trimmed_value);
                    config->target = AllocVec(value_len + 1, MEMF_CLEAR);
                    {
                        LONG i;
                        for (i = 0; i < value_len; i++) {
                            config->target[i] = trimmed_value[i];
                        }
                        config->target[value_len] = '\0';
                    }
                }
                Printf("GenIn: Parsed TARGET = '%s'\n", config->target);
                found_any_params = TRUE;
                break;
                
            case PARAM_IMAGE:
                if (config->image) {
                    Printf("GenIn: Multiple IMAGE parameters specified\n");
                    return FALSE;
                }
                {
                    LONG value_len = my_strlen(trimmed_value);
                    config->image = AllocVec(value_len + 1, MEMF_CLEAR);
                    {
                        LONG i;
                        for (i = 0; i < value_len; i++) {
                            config->image[i] = trimmed_value[i];
                        }
                        config->image[value_len] = '\0';
                    }
                }
                Printf("GenIn: Parsed IMAGE = '%s'\n", config->image);
                found_any_params = TRUE;
                break;
                
            case PARAM_DEFICON:
                if (config->deficon) {
                    Printf("GenIn: Multiple DEFICON parameters specified\n");
                    return FALSE;
                }
                {
                    LONG value_len = my_strlen(trimmed_value);
                    config->deficon = AllocVec(value_len + 1, MEMF_CLEAR);
                    {
                        LONG i;
                        for (i = 0; i < value_len; i++) {
                            config->deficon[i] = trimmed_value[i];
                        }
                        config->deficon[value_len] = '\0';
                    }
                }
                Printf("GenIn: Parsed DEFICON = '%s'\n", config->deficon);
                found_any_params = TRUE;
                break;
                
            default:
                Printf("GenIn: Unknown parameter '%s'\n", (char *)trimmed_param);
                break;
        }
    }
    
    /* End of file - return TRUE if we found any parameters */
    return found_any_params;
}

BOOL validate_config(Config *config)
{
    STRPTR stripped_target;
    STRPTR filename_part;
    LONG i;
    LONG last_slash;
    
    if (!config->type) {
        Printf("GenIn: TYPE parameter is mandatory\n");
        return FALSE;
    }
    
    if (!config->target) {
        Printf("GenIn: TARGET parameter is mandatory\n");
        return FALSE;
    }
    
    if (!config->type) {
        Printf("GenIn: TYPE parameter is mandatory\n");
        return FALSE;
    }
    
    if (config->image && config->deficon) {
        Printf("GenIn: IMAGE and DEFICON cannot be specified together\n");
        return FALSE;
    }
    
    /* Strip .info extension from target for validation */
    stripped_target = strip_info_extension(config->target);
    if (!stripped_target) {
        Printf("GenIn: Failed to process target filename\n");
        return FALSE;
    }
    
    /* Extract filename part for validation */
    filename_part = stripped_target;
    last_slash = -1;
    for (i = 0; stripped_target[i] != '\0'; i++) {
        if (stripped_target[i] == '/' || stripped_target[i] == ':') {
            last_slash = i;
        }
    }
    if (last_slash >= 0) {
        filename_part = &stripped_target[last_slash + 1];
    }
    
    /* Validate filename */
    if (!validate_filename(filename_part)) {
        Printf("GenIn: Invalid filename '%s' in TARGET\n", filename_part);
        FreeVec(stripped_target);
        return FALSE;
    }
    
    FreeVec(stripped_target);
    return TRUE;
}

struct DiskObject *load_default_icon(STRPTR deficon)
{
    struct DiskObject *diskobj = NULL;
    UBYTE deficon_path[512];
    
    /* Always try ENVARC:Sys/def_ first */
    Strncpy((char *)deficon_path, "ENVARC:Sys/def_", sizeof(deficon_path) - 1);
    deficon_path[sizeof(deficon_path) - 1] = '\0';
    
    if (!AddPart((char *)deficon_path, (char *)deficon, sizeof(deficon_path))) {
        Printf("GenIn: Path too long when constructing deficon path\n");
        return NULL;
    }
    
    Printf("GenIn: Trying ENVARC:Sys/def_ icon: '%s'\n", (char *)deficon_path);
    
    /* Try to load the deficon using GetDiskObjectNew */
    diskobj = GetDiskObjectNew((STRPTR)deficon_path);
    
    if (diskobj) {
        Printf("GenIn: Successfully loaded ENVARC:Sys/def_ icon '%s'\n", deficon);
        return diskobj;
    }
    
    Printf("GenIn: ENVARC:Sys/def_ icon '%s' not found, falling back to standard deficon\n", deficon);
    return NULL;
}

struct DiskObject *load_standard_deficon(STRPTR type)
{
    struct DiskObject *diskobj = NULL;
    LONG def_type = -1;
    
    /* Map string to standard type constant */
    if (Stricmp(type, "disk") == 0) {
        def_type = WBDISK;
    } else if (Stricmp(type, "drawer") == 0) {
        def_type = WBDRAWER;
    } else if (Stricmp(type, "tool") == 0) {
        def_type = WBTOOL;
    } else if (Stricmp(type, "project") == 0) {
        def_type = WBPROJECT;
    } else if (Stricmp(type, "garbage") == 0) {
        def_type = WBGARBAGE;
    } else if (Stricmp(type, "kick") == 0) {
        def_type = WBKICK;
    } else if (Stricmp(type, "device") == 0) {
        def_type = WBDEVICE;
    }
    
    if (def_type != -1) {
        /* Use GetDefDiskObject for standard types */
        Printf("GenIn: Loading standard deficon type %ld for '%s'\n", def_type, type);
        diskobj = GetDefDiskObject(def_type);
        
        if (diskobj) {
            Printf("GenIn: Successfully loaded standard deficon for type '%s'\n", type);
        } else {
            Printf("GenIn: Could not load standard deficon for type '%s'\n", type);
        }
    } else {
        Printf("GenIn: Unknown type '%s' for standard deficon\n", type);
    }
    
    return diskobj;
}

STRPTR load_and_process_image(STRPTR image_path)
{
    Object *dt_object;
    STRPTR icon_data;
    LONG icon_size;
    
    /* Load image using datatypes */
    dt_object = NewDTObject(image_path,
                           DTA_SourceType, DTST_FILE,
                           DTA_GroupID, GID_PICTURE,
                           TAG_END);
    
    if (!dt_object) {
        return NULL;
    }
    
    /* Get bitmap header */
    /* TODO: Implement proper datatypes bitmap extraction */
    /* For now, we'll create a basic icon structure */
    
    /* Create basic icon data */
    icon_size = sizeof(struct DiskObject) + (ICON_SIZE * ICON_SIZE / 8); /* Basic icon data */
    icon_data = AllocVec(icon_size, MEMF_CLEAR);
    if (!icon_data) {
        DisposeDTObject(dt_object);
        return NULL;
    }
    
    /* TODO: Implement proper bitmap to icon conversion */
    /* This would involve:
       - Converting to Amiga palette (4 colors for standard icons)
       - Creating proper icon bitmap planes
       - Handling transparency
       - Scaling if needed */
    
    DisposeDTObject(dt_object);
    return icon_data;
}

BOOL create_info_file(Config *config, struct DiskObject *source_diskobj)
{
    struct DiskObject *diskobj;
    struct DiskObject *test_obj;
    LONG i;
    UBYTE target_path[512];
    STRPTR *tooltype_array = NULL;
    BOOL result;
    BOOL valid;
    LONG expected_type;
    LONG loaded_count;
    
    /* Use resolved target path and add .info extension */
    Strncpy((char *)target_path, (char *)config->resolved_target, sizeof(target_path) - 1);
    target_path[sizeof(target_path) - 1] = '\0';
    
    /* Add .info extension */
    if (!AddPart((char *)target_path, ".info", sizeof(target_path))) {
        Printf("GenIn: Path too long when constructing target path\n");
        return FALSE;
    }
    
    /* Create disk object using icon.library */
    if (Stricmp(config->type, "tool") == 0) {
        diskobj = NewDiskObject(WBTOOL);
    } else if (Stricmp(config->type, "project") == 0) {
        diskobj = NewDiskObject(WBPROJECT);
    } else if (Stricmp(config->type, "drawer") == 0) {
        diskobj = NewDiskObject(WBDRAWER);
    } else {
        diskobj = NewDiskObject(WBDISK);
    }
    
    if (!diskobj) {
        return FALSE;
    }
    
    /* Set basic properties */
    diskobj->do_Magic = WB_DISKMAGIC;
    diskobj->do_Version = WB_DISKVERSION;
    
    /* Copy icon data from source if available */
    if (source_diskobj) {
        /* Copy the gadget (which contains the icon image) */
        CopyMem(&source_diskobj->do_Gadget, &diskobj->do_Gadget, sizeof(struct Gadget));
        Printf("GenIn: Copied icon data from source deficon\n");
    }
    
    /* Set stack (always has a default value now) */
    diskobj->do_StackSize = config->stack;
    
    /* Set tooltypes */
    if (config->tooltype_count > 0) {
        /* Create NULL-terminated tooltype array */
        tooltype_array = (STRPTR *)AllocVec(sizeof(STRPTR) * (config->tooltype_count + 1), MEMF_CLEAR);
        if (tooltype_array) {
            for (i = 0; i < config->tooltype_count; i++) {
                tooltype_array[i] = config->tooltypes[i];
            }
            tooltype_array[config->tooltype_count] = NULL; /* NULL terminate */
            diskobj->do_ToolTypes = tooltype_array;
        }
    }
    
    /* Set default tool */
    diskobj->do_DefaultTool = config->resolved_target;
    
    /* Set position */
    diskobj->do_CurrentX = NO_ICON_POSITION;
    diskobj->do_CurrentY = NO_ICON_POSITION;
    
    /* Set drawer */
    diskobj->do_DrawerData = NULL;
    
    /* Set tool types */
    diskobj->do_ToolTypes = tooltype_array;
    
    /* Create .info file using icon.library */
    result = PutDiskObject((STRPTR)target_path, diskobj);
    
    /* Cleanup */
    if (tooltype_array) {
        FreeVec(tooltype_array);
    }
    FreeDiskObject(diskobj);
    
    /* Validate the saved file by loading it back */
    if (result) {
        test_obj = GetDiskObject((STRPTR)target_path);
        if (!test_obj) {
            Printf("GenIn: Warning - Created file but could not load it back for validation\n");
            return FALSE;
        }
        
        /* Validate basic properties */
        valid = TRUE;
        
        /* Check magic and version */
        if (test_obj->do_Magic != WB_DISKMAGIC || test_obj->do_Version != WB_DISKVERSION) {
            Printf("GenIn: Warning - Invalid magic/version in saved file\n");
            valid = FALSE;
        }
        
        /* Check type */
        if (Stricmp(config->type, "tool") == 0) {
            expected_type = WBTOOL;
        } else if (Stricmp(config->type, "project") == 0) {
            expected_type = WBPROJECT;
        } else if (Stricmp(config->type, "drawer") == 0) {
            expected_type = WBDRAWER;
        } else {
            expected_type = WBPROJECT;
        }
        
        if (test_obj->do_Type != expected_type) {
            Printf("GenIn: Warning - Type mismatch in saved file (expected %ld, got %ld)\n", expected_type, test_obj->do_Type);
            valid = FALSE;
        }
        
        /* Check stack size */
        if (test_obj->do_StackSize != config->stack) {
            Printf("GenIn: Warning - Stack size mismatch in saved file (expected %ld, got %ld)\n", config->stack, test_obj->do_StackSize);
            valid = FALSE;
        }
        
        /* Check tooltype count and content */
        if (config->tooltype_count > 0) {
            loaded_count = 0;
            if (test_obj->do_ToolTypes) {
                while (test_obj->do_ToolTypes[loaded_count] != NULL) {
                    loaded_count++;
                }
            }
            if (loaded_count != config->tooltype_count) {
                Printf("GenIn: Warning - Tooltype count mismatch (expected %ld, got %ld)\n", config->tooltype_count, loaded_count);
                valid = FALSE;
            } else {
                /* Check each tooltype string */
                for (i = 0; i < config->tooltype_count; i++) {
                    if (!test_obj->do_ToolTypes[i] || Stricmp(test_obj->do_ToolTypes[i], config->tooltypes[i]) != 0) {
                        Printf("GenIn: Warning - Tooltype mismatch at index %ld\n", i);
                        valid = FALSE;
                        break;
                    }
                }
            }
        }
        
        /* Check default tool */
        if (test_obj->do_DefaultTool && Stricmp(test_obj->do_DefaultTool, config->target) != 0) {
            Printf("GenIn: Warning - Default tool mismatch in saved file\n");
            valid = FALSE;
        }
        
        FreeDiskObject(test_obj);
        
        if (!valid) {
            Printf("GenIn: Validation failed - file may be corrupted\n");
            return FALSE;
        }
        
        Printf("GenIn: File validation successful\n");
    }
    
    return result;
}

void cleanup_config(Config *config)
{
    LONG i;
    
    if (config->type) FreeVec(config->type);
    if (config->target) FreeVec(config->target);
    if (config->resolved_target) FreeVec(config->resolved_target);
    if (config->image) FreeVec(config->image);
    if (config->deficon) FreeVec(config->deficon);
    
    for (i = 0; i < config->tooltype_count; i++) {
        if (config->tooltypes[i]) {
            FreeVec(config->tooltypes[i]);
        }
    }
}

STRPTR strip_info_extension(STRPTR filename)
{
    LONG len;
    STRPTR result;
    
    if (!filename) {
        return NULL;
    }
    
    len = my_strlen(filename);
    
    /* Check if filename ends with .info */
    if (len >= 5 && Stricmp(&filename[len-5], ".info") == 0) {
        /* Allocate memory for filename without .info extension */
        result = AllocVec(len - 4 + 1, MEMF_CLEAR);
        if (result) {
            Strncpy((char *)result, (char *)filename, len - 5);
            result[len - 5] = '\0';
            return result;
        }
    }
    
    /* Return copy of original filename if no .info extension */
    result = AllocVec(len + 1, MEMF_CLEAR);
    if (result) {
        Strncpy((char *)result, (char *)filename, len);
        result[len] = '\0';
        return result;
    }
    
    return NULL;
}

BOOL validate_filename(STRPTR filename)
{
    LONG i;
    UBYTE c;
    
    if (!filename || my_strlen(filename) == 0) {
        return FALSE;
    }
    
    /* Check for invalid characters according to Amiga standards */
    for (i = 0; filename[i] != '\0'; i++) {
        c = filename[i];
        
        /* Invalid characters: / : * ? " < > | */
        if (c == '/' || c == ':' || c == '*' || c == '?' || 
            c == '"' || c == '<' || c == '>' || c == '|') {
            return FALSE;
        }
        
        /* Control characters (ASCII 0-31) */
        if (c < 32) {
            return FALSE;
        }
    }
    
    /* Check for reserved names */
    if (Stricmp(filename, "CON") == 0 || Stricmp(filename, "CON:") == 0 ||
        Stricmp(filename, "AUX") == 0 || Stricmp(filename, "AUX:") == 0 ||
        Stricmp(filename, "PRT") == 0 || Stricmp(filename, "PRT:") == 0 ||
        Stricmp(filename, "NIL") == 0 || Stricmp(filename, "NIL:") == 0) {
        return FALSE;
    }
    
    return TRUE;
}

STRPTR resolve_target_path(STRPTR spec_file, STRPTR target)
{
    UBYTE spec_dir[512];
    UBYTE resolved_path[512];
    STRPTR result;
    LONG i;
    LONG last_slash;
    
    if (!spec_file || !target) {
        return NULL;
    }
    
    /* Extract directory from spec file path */
    Strncpy((char *)spec_dir, (char *)spec_file, sizeof(spec_dir) - 1);
    spec_dir[sizeof(spec_dir) - 1] = '\0';
    
    /* Find last slash in spec file path */
    last_slash = -1;
    for (i = 0; spec_dir[i] != '\0'; i++) {
        if (spec_dir[i] == '/' || spec_dir[i] == ':') {
            last_slash = i;
        }
    }
    
    if (last_slash >= 0) {
        /* Terminate at last slash to get directory */
        spec_dir[last_slash + 1] = '\0';
    } else {
        /* No directory component, use current directory */
        spec_dir[0] = '\0';
    }
    
    /* Check if target is absolute (starts with / or :) */
    if (target[0] == '/' || target[0] == ':') {
        /* Target is absolute, use as-is */
        Strncpy((char *)resolved_path, (char *)target, sizeof(resolved_path) - 1);
    } else {
        /* Target is relative, combine with spec file directory */
        Strncpy((char *)resolved_path, (char *)spec_dir, sizeof(resolved_path) - 1);
        if (!AddPart((char *)resolved_path, (char *)target, sizeof(resolved_path))) {
            Printf("GenIn: Path too long when resolving target '%s'\n", target);
            return NULL;
        }
    }
    
    /* Allocate and return resolved path */
    result = AllocVec(my_strlen((char *)resolved_path) + 1, MEMF_CLEAR);
    if (result) {
        Strncpy((char *)result, (char *)resolved_path, my_strlen((char *)resolved_path));
        result[my_strlen((char *)resolved_path)] = '\0';
        return result;
    }
    
    return NULL;
}

void print_usage(void)
{
    Printf("Usage: GenIn [SPECFILE=file] [TYPE=type] [STACK=size] [TARGET=name] [IMAGE=file] [DEFICON=name] [TOOLTYPE=key=value] [FORCE] [HELP]\n");
    Printf("\n");
    Printf("Arguments:\n");
    Printf("  SPECFILE=file  - Specification file - uses same arguments\n");
    Printf("  TYPE=type      - Icon type: tool, project, or drawer (mandatory)\n");
    Printf("  STACK=size     - Stack size in bytes (default: 4096)\n");
    Printf("  TARGET=name    - Target filename for .info file (mandatory)\n");
    Printf("  IMAGE=file     - Path to custom image file\n");
    Printf("  DEFICON=name   - Default icon name to use\n");
    Printf("  TOOLTYPE=key=value - Tooltype entry (can be specified multiple times)\n");
    Printf("  FORCE          - Overwrite existing file\n");
    Printf("  HELP           - Show this help message\n");
    Printf("\n");
    Printf("Multiple icon definitions in spec file:\n");
    Printf("  Separate icon definitions with blank lines\n");
    Printf("  Comments start with ';' and continue to end of line\n");
    Printf("\n");
    Printf("Notes:\n");
    Printf("  - One of SPECFILE or TARGET must be provided\n");
    Printf("  - TYPE is mandatory and used as fallback for DEFICON\n");
    Printf("  - IMAGE and DEFICON cannot be specified together\n");
    Printf("  - TOOLTYPE keys must each be unique\n");
    Printf("  - DEFICON tries ENVARC:Sys/def_ first, then falls back to TYPE\n");
} 