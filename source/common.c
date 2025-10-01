#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <dirent.h>
#include <zlib.h>

#include "types.h"
#include "common.h"

#define TMP_BUFF_SIZE 0x20000

//----------------------------------------
//String Utils
//----------------------------------------
int is_char_integer(char c)
{
    if (c >= '0' && c <= '9')
        return SUCCESS;
    return FAILED;
}

int is_char_letter(char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        return SUCCESS;
    return FAILED;
}

//----------------------------------------
//String Utils for Internationalization Fallback
//----------------------------------------

/*
 * Converte caracteres UTF-8 de Português (ç, ã, á, etc.) para suas versões ASCII.
 * Necessário para fontes limitadas a ASCII.
 * O buffer de destino 'dest' deve ser alocado pelo chamador e ter tamanho suficiente.
 */
void map_utf8_to_ascii_fallback(const char *src, char *dest, size_t dest_size) {
    if (!src || !dest || dest_size == 0) return;

    size_t i = 0;
    size_t j = 0;

    while (src[i] != '\0' && j < dest_size - 1) {
        unsigned char c = (unsigned char)src[i];

        // 1. Caractere ASCII simples, copia e avança 1 byte
        if (c >= 0x20 && c <= 0x7E) {
            dest[j++] = src[i++];
            continue;
        }

        // 2. Mapeamento UTF-8 de 2 Bytes (Maioria dos acentos e ç/Ç em PT-BR)
        if ((c & 0xE0) == 0xC0) {
            unsigned char next_c = (unsigned char)src[i+1];
            char replacement = 0; // 0 significa 'ignorar'

            if (c == 0xc3) {
                switch (next_c) {
                    case 0xa3: replacement = 'a'; break; // ã
                    case 0xa1: replacement = 'a'; break; // á
                    case 0xa2: replacement = 'a'; break; // â
                    case 0xa0: replacement = 'a'; break; // à
                    case 0xa7: replacement = 'c'; break; // ç
                    case 0xb5: replacement = 'o'; break; // õ
                    case 0xb3: replacement = 'o'; break; // ó
                    case 0xb4: replacement = 'o'; break; // ô
                    case 0xa9: replacement = 'e'; break; // é
                    case 0xaa: replacement = 'e'; break; // ê
                    case 0xad: replacement = 'i'; break; // í
                    case 0xba: replacement = 'u'; break; // ú
                    case 0xbc: replacement = 'u'; break; // ü
                    
                    case 0x83: replacement = 'A'; break; // Ã
                    case 0x81: replacement = 'A'; break; // Á
                    case 0x82: replacement = 'A'; break; // Â
                    case 0x87: replacement = 'C'; break; // Ç
                    case 0x95: replacement = 'O'; break; // Õ
                    case 0x93: replacement = 'O'; break; // Ó
                    case 0x9a: replacement = 'U'; break; // Ú
                    default: replacement = 0; break;
                }
            } 
            else if (c == 0xc2) {
                // Outros caracteres comuns como º e ª
                switch (next_c) {
                    case 0xba: replacement = 'o'; break; // º -> o
                    case 0xaa: replacement = 'a'; break; // ª -> a
                    default: replacement = 0; break;
                }
            }

            // Se encontrarmos uma substituição, adicionamos ela.
            if (replacement != 0) {
                dest[j++] = replacement;
            }
            
            i += 2; // Avança dois bytes (o caractere UTF-8 completo)
            continue;
        }

        // 3. Outros caracteres UTF-8 (3+ bytes ou inválidos): Ignora o byte
        i++;
    }

    dest[j] = '\0'; // Encerra a string de destino
}

//----------------------------------------
//FILE UTILS
//----------------------------------------

int file_exists(const char *path)
{
    if (access(path, F_OK) == 0) {
        return SUCCESS;
    }
    
    return FAILED;
}

int dir_exists(const char *path)
{
    struct stat sb;
    if ((stat(path, &sb) == 0) && S_ISDIR(sb.st_mode)) {
        return SUCCESS;
    }
    return FAILED;
}

int unlink_secure(const char *path)
{   
    if(file_exists(path)==SUCCESS)
    {
        chmod(path, 0777);
        return remove(path);
    }
    return FAILED;
}

/*
* Creates all the directories in the provided path. (can include a filename)
* (directory must end with '/')
*/
int mkdirs(const char* dir)
{
    char path[256];
    snprintf(path, sizeof(path), "%s", dir);

    char* ptr = strrchr(path, '/');
    *ptr = 0;
    ptr = path;
    ptr++;
    while (*ptr)
    {
        while (*ptr && *ptr != '/')
            ptr++;

        char last = *ptr;
        *ptr = 0;

        if (file_exists(path) == FAILED)
        {
            if (mkdir(path, 0777) < 0)
                return FAILED;
            else
                chmod(path, 0777);
        }
        
        *ptr++ = last;
        if (last == 0)
            break;

    }

    return SUCCESS;
}

int copy_file(const char* input, const char* output)
{
    size_t read, written;
    FILE *fd, *fd2;

    if (mkdirs(output) != SUCCESS)
        return FAILED;

    if((fd = fopen(input, "rb")) == NULL)
        return FAILED;

    if((fd2 = fopen(output, "wb")) == NULL)
    {
        fclose(fd);
        return FAILED;
    }

    char* buffer = malloc(TMP_BUFF_SIZE);

    if (!buffer)
        return FAILED;

    do
    {
        read = fread(buffer, 1, TMP_BUFF_SIZE, fd);
        written = fwrite(buffer, 1, read, fd2);
    }
    while ((read == written) && (read == TMP_BUFF_SIZE));

    free(buffer);
    fclose(fd);
    fclose(fd2);
    chmod(output, 0777);

    return (read - written);
}

uint32_t file_crc32(const char* input)
{
    Bytef *buffer;
    uLong crc = crc32(0L, Z_NULL, 0);
    size_t read;

    FILE* in = fopen(input, "rb");
    
    if (!in)
        return FAILED;

    buffer = malloc(TMP_BUFF_SIZE);
    do
    {
        read = fread(buffer, 1, TMP_BUFF_SIZE, in);
        crc = crc32(crc, buffer, read);
    }
    while (read == TMP_BUFF_SIZE);

    free(buffer);
    fclose(in);

    return crc;
}

int copy_directory(const char* startdir, const char* inputdir, const char* outputdir)
{
    char fullname[256];
    char out_name[256];
    struct dirent *dirp;
    int len = strlen(startdir);
    DIR *dp = opendir(inputdir);

    if (!dp) {
        return FAILED;
    }

    while ((dirp = readdir(dp)) != NULL) {
        if ((strcmp(dirp->d_name, ".")  != 0) && (strcmp(dirp->d_name, "..") != 0)) {
            snprintf(fullname, sizeof(fullname), "%s%s", inputdir, dirp->d_name);

            if (dirp->d_type == DT_DIR) {
                strcat(fullname, "/");
                if (copy_directory(startdir, fullname, outputdir) != SUCCESS) {
                    return FAILED;
                }
            } else {
                snprintf(out_name, sizeof(out_name), "%s%s", outputdir, &fullname[len]);
                if (copy_file(fullname, out_name) != SUCCESS) {
                    return FAILED;
                }
            }
        }
    }
    closedir(dp);

    return SUCCESS;
}

int clean_directory(const char* inputdir, const char* filter)
{
    DIR *d;
    struct dirent *dir;
    char dataPath[256];

    d = opendir(inputdir);
    if (!d)
        return FAILED;

    while ((dir = readdir(d)) != NULL)
    {
        if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0 && strstr(dir->d_name, filter) != NULL)
        {
            snprintf(dataPath, sizeof(dataPath), "%s" "%s", inputdir, dir->d_name);

            if (dir->d_type == DT_DIR) {
                strcat(dataPath, "/");
                clean_directory(dataPath, filter);
                rmdir(dataPath);
            }

            unlink_secure(dataPath);
        }
    }
    closedir(d);

    return SUCCESS;
}